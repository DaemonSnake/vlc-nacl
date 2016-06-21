/*****************************************************************************
 * Copyright © 2015 Cadonix, Richard Diamond
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#define MODULE_NAME ppapi_aout
#define MODULE_STRING "ppapi_aout"

#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_block_helper.h>
#include <vlc_ppapi.h>
#include <vlc_plugin.h>
#include <vlc_aout.h>
#include <vlc_aout_volume.h>
#include <vlc_modules.h>

#include <assert.h>

static int Open(vlc_object_t* obj);
static void Close(vlc_object_t* obj);
static void Play(audio_output_t* aout, block_t* block);

static void PPAPIAudioCallback(void* sample_buffer,
                               uint32_t buffer_size,
                               PP_TimeDelta latency_s,
                               void* user_data);

// Interestingly, this needs to be kept small, otherwise VLC will think there
// are constant timing issues.
#define BUFFER_SAMPLES 386

struct aout_sys_t {
  PP_Instance instance;
  PP_Resource audio;
  PP_Resource config;

  float volume_level;
  audio_volume_t* volume;
  module_t *volume_module;

  mtime_t rate; // either 44100 or 48000

  atomic_bool muted;

  atomic_uint_least64_t locks;
  vlc_mutex_t lock;

  bool flushed;

  // these two need the lock:
  mtime_t paused_at;
  mtime_t resumed_at;
  // ------------------------

  uint64_t last_underruns;
  atomic_uint_least64_t underruns;

  size_t restarts;
  atomic_int_least64_t last_run;

  block_bytestream_t ppapi_stream;

  // the vlc side does not lock to read these
  atomic_int_least64_t pts;
  atomic_bool pts_valid;

  audio_output_t* aout;
};

vlc_module_begin()
    set_shortname("PPAPI Audio")
    set_description("Output audio using the PPAPI")
    set_category(CAT_AUDIO)
    set_subcategory(SUBCAT_AUDIO_AOUT)
    set_capability("audio output", 50)
    set_callbacks(Open, Close)
vlc_module_end()

static void lock(aout_sys_t* sys) {
  vlc_mutex_lock(&sys->lock);
}
static void unlock(aout_sys_t* sys) {
  atomic_fetch_add(&sys->locks, 1);
  vlc_mutex_unlock(&sys->lock);
}

// sometimes, and especially on Windows, the audio callback thread will
// just exit. I haven't yet figured out why. So if the callback isn't
// called for over two seconds, restart it.
static int check_audio_playback(aout_sys_t* sys, size_t* restarts) {
  if(!atomic_load(&sys->pts_valid)) { return VLC_SUCCESS; }

  const mtime_t max_delay = atomic_load(&sys->pts) * 2;
  const mtime_t last_run = atomic_load(&sys->last_run);
  if(mdate() - last_run > max_delay) {
    if(restarts != NULL && *restarts >= 2) {
      // bail. it's helpless now.
      msg_Warn(sys->aout, "The audio callback thread has stopped responding `%u` time%s in a row!"
               " It's hopeless now.", *restarts, (*restarts > 1 ? "s" : ""));
      goto error;
    } else if(restarts != NULL) {
      *restarts += 1;
    }

    msg_Warn(sys->aout, "Somehow, the audio callback thread stopped responding. Restarting it.");

    const vlc_ppapi_audio_t* iaudio = vlc_getPPAPI_Audio();
    iaudio->StopPlayback(sys->audio);
    vlc_subResReference(sys->audio);
    sys->audio = 0;

    sys->audio = iaudio->Create(sys->instance,
                                sys->config,
                                PPAPIAudioCallback,
                                sys);
    if(sys->audio == 0) {
      msg_Err(sys->aout, "failed to create audio resource");
      goto error;
    }
    atomic_store(&sys->last_run, mdate());
    if(iaudio->StartPlayback(sys->audio) == PP_FALSE) {
      msg_Err(sys->aout, "failed to start audio playback");
      goto error;
    }
  }

  return VLC_SUCCESS;

 error:
  vlc_subResReference(sys->audio);
  sys->audio = 0;

  lock(sys);
  block_BytestreamEmpty(&sys->ppapi_stream);
  unlock(sys);
  return VLC_EGENERIC;
}

static int TimeGet(audio_output_t* aout, mtime_t* delay) {
  aout_sys_t* sys = (aout_sys_t*)aout->sys;

  const bool valid = atomic_load(&sys->pts_valid);
  if(!valid) {
    return VLC_EGENERIC;
  }

  *delay = atomic_load(&sys->pts);

  lock(sys);
  mtime_t samples_delay = 0;
  for(block_t* block = sys->ppapi_stream.p_block; block != NULL; block = block->p_next) {
    samples_delay += block->i_nb_samples;
    if(sys->ppapi_stream.p_block == block) {
      // remove the samples that are already read.
      const size_t bytes_per_sample = block->i_buffer / block->i_nb_samples;
      const size_t offset = sys->ppapi_stream.i_offset;
      const size_t samples = offset / bytes_per_sample;
      assert(samples <= samples_delay);
      samples_delay -= samples;
    }
  }
  unlock(sys);

  samples_delay *= CLOCK_FREQ;
  samples_delay /= sys->rate;
  *delay += samples_delay;


  //msg_Err(aout, "delay = `%llu`ns", *delay);
  return VLC_SUCCESS;
}
static int VolumeSet(audio_output_t* aout, float volume) {
  aout_sys_t* sys = (aout_sys_t*)aout->sys;

  sys->volume_level = volume;

  lock(sys);
  // set the volume for blocks we've already queued:
  for(block_t* block = sys->ppapi_stream.p_block; block != NULL; block = block->p_next) {
    sys->volume->amplify(sys->volume, block, sys->volume_level);
  }
  unlock(sys);

  return VLC_SUCCESS;
}

static void Play(audio_output_t* aout, block_t* block) {
  aout_sys_t* sys = aout->sys;

  sys->volume->amplify(sys->volume, block, sys->volume_level);

  if(check_audio_playback(sys, NULL) != 0) { return; }

  lock(sys);

  sys->flushed = false;

  block_BytestreamFlush(&sys->ppapi_stream);
  block_BytestreamPush(&sys->ppapi_stream, block);

  unlock(sys);

  const uint64_t underruns = atomic_load(&sys->underruns);
  if(underruns != sys->last_underruns) {
    msg_Warn(aout, "`%llu` buffer underruns", underruns - sys->last_underruns);
    sys->last_underruns = underruns;
  }
}

static void Pause(audio_output_t* aout, bool pause, mtime_t at) {
  aout_sys_t* sys = (aout_sys_t*)aout->sys;

  lock(sys);
  if(pause) {
    sys->paused_at = at;
    sys->resumed_at = VLC_TS_INVALID;

    vlc_getPPAPI_Audio()->StopPlayback(sys->audio);
  } else {
    sys->resumed_at = at;
    sys->paused_at = VLC_TS_INVALID;

    atomic_store(&sys->last_run, mdate());
    vlc_getPPAPI_Audio()->StartPlayback(sys->audio);
  }
  unlock(sys);
}

static void PPAPIAudioCallback(void* sample_buffer,
                               uint32_t buffer_size,
                               PP_TimeDelta latency_s,
                               void* user_data) {
  aout_sys_t* sys = (aout_sys_t*)user_data;
  mtime_t latency = (mtime_t)(latency_s * CLOCK_FREQ);

  atomic_store(&sys->last_run, mdate());

  atomic_store(&sys->pts, latency);
  atomic_store(&sys->pts_valid, true);

  // never output garbage:
  memset(sample_buffer, 0, (size_t) buffer_size);

  uint8_t* buffer = sample_buffer;

  for(;;) {
    mtime_t tnow = mdate();

    lock(sys);

    if(sys->ppapi_stream.p_block == NULL || sys->paused_at != VLC_TS_INVALID) {
      break;
    }

    size_t read = 0;
    if(atomic_load(&sys->muted)) {
      read = block_SkipMaxBytes(&sys->ppapi_stream, buffer_size);
    } else {
      read = block_GetMaxBytes(&sys->ppapi_stream, buffer, buffer_size);
    }

    buffer += read;
    buffer_size -= read;

    if(read != 0 || sys->flushed) {
      break;
    }

    unlock(sys);

    // spin until either the lock count increases, or we're start running close
    // to the presentation time.
    uint_least64_t locks = atomic_load(&sys->locks);
    for(;;) {
      msleep(CLOCK_FREQ / 1000);

      const mtime_t tdelta = mdate() - tnow;
      latency -= tdelta;

      if(locks != atomic_load(&sys->locks)) {
        break;
      } else if(latency < AOUT_MIN_PREPARE_TIME) {
        // buffer underrun!
        atomic_fetch_add(&sys->underruns, 1);
        return;
      }

      tnow = mdate();
    }
  }

  unlock(sys);
}

static int Start(audio_output_t* aout, audio_sample_format_t* fmt) {
  aout_sys_t* sys = (aout_sys_t*)aout->sys;

  if(sys->instance == 0 || sys->audio != 0) {
    msg_Err(aout, "am I already initialized?");
    return VLC_EGENERIC;
  }

  const vlc_ppapi_audio_t* iaudio = vlc_getPPAPI_Audio();

  if(sys->config == 0) {
    const vlc_ppapi_audio_config_t* iaudio_config = vlc_getPPAPI_AudioConfig();

    PP_AudioSampleRate rate = iaudio_config->RecommendSampleRate(sys->instance);
    if(rate == PP_AUDIOSAMPLERATE_NONE) {
      rate = __MAX(fmt->i_rate, PP_AUDIOSAMPLERATE_44100);
      rate = __MIN(rate, PP_AUDIOSAMPLERATE_48000);

      // if 44100 < rate < 48000 then set rate to which ever is closer.
      if(rate != PP_AUDIOSAMPLERATE_44100 ||
         rate != PP_AUDIOSAMPLERATE_48000) {
        if(rate < (PP_AUDIOSAMPLERATE_48000 - PP_AUDIOSAMPLERATE_44100) / 2) {
          rate = PP_AUDIOSAMPLERATE_44100;
        } else {
          rate = PP_AUDIOSAMPLERATE_48000;
        }
      }
    }
    msg_Info(aout, "using a sample rate of `%u`",
             (uint32_t)rate);
    sys->rate = (uint32_t)rate;

    /*const uint32_t sample_frame_count =
      iaudio_config->RecommendSampleFrameCount(sys->instance,
                                               rate,
                                               BUFFER_SAMPLES);
    msg_Info(aout, "using a sample frame count of `%u`",
    sample_frame_count);*/

    PP_Resource audio_config =
      iaudio_config->CreateStereo16Bit(sys->instance, rate,
                                       BUFFER_SAMPLES);
    if(audio_config == 0) {
      msg_Err(aout, "failed to create audio config");
      return VLC_EGENERIC;
    }
    sys->config = audio_config;
  }

  PP_Resource audio = iaudio->Create(sys->instance,
                                     sys->config,
                                     PPAPIAudioCallback,
                                     sys);
  if(audio == 0) {
    msg_Err(aout, "failed to create audio resource");
    return VLC_EGENERIC;
  }

  sys->audio = audio;
  sys->locks = ATOMIC_VAR_INIT(0);
  sys->paused_at = VLC_TS_INVALID;
  sys->resumed_at = VLC_TS_0;
  sys->flushed = true;
  block_BytestreamEmpty(&sys->ppapi_stream);

  sys->last_underruns = 0;
  sys->underruns = ATOMIC_VAR_INIT(0);

  sys->pts = ATOMIC_VAR_INIT(0);
  sys->pts_valid = ATOMIC_VAR_INIT(false);

  atomic_store(&sys->last_run, mdate());

  if(iaudio->StartPlayback(sys->audio) != PP_TRUE) {
    vlc_subResReference(sys->audio);
    sys->audio = 0;
    return VLC_EGENERIC;
  }

  // The PPAPI only supports stereo.
  fmt->i_original_channels =
    fmt->i_physical_channels = AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT;
  fmt->i_format = VLC_CODEC_S16L;
  fmt->i_rate = sys->rate;

  return VLC_SUCCESS;
}
static void Stop(audio_output_t* aout) {
  aout_sys_t* sys = (aout_sys_t*)aout->sys;

  if(sys->audio == 0) { return; }

  const vlc_ppapi_audio_t* iaudio = vlc_getPPAPI_Audio();

  iaudio->StopPlayback(sys->audio);

  vlc_subResReference(sys->audio);
  sys->audio = 0;
  lock(sys);
  block_BytestreamEmpty(&sys->ppapi_stream);
  unlock(sys);
}
static void Flush(audio_output_t* aout, bool wait) {
  aout_sys_t* sys = (aout_sys_t*)aout->sys;

  if(wait && sys->audio != 0) {
    lock(sys);
    sys->flushed = true;

    assert(sys->paused_at == VLC_TS_INVALID && sys->resumed_at != VLC_TS_INVALID);
    block_BytestreamFlush(&sys->ppapi_stream);
    bool empty = (sys->ppapi_stream.p_block == NULL);
    unlock(sys);

    size_t restarts = 0;

    for(; !empty;) {
      msleep(CLOCK_FREQ / 1000);

      if(check_audio_playback(sys, &restarts) != 0) { return; }

      lock(sys);
      block_BytestreamFlush(&sys->ppapi_stream);
      empty = (sys->ppapi_stream.p_block == NULL);
      unlock(sys);
    }
  } else {
    lock(sys);
    sys->flushed = true;
    block_BytestreamEmpty(&sys->ppapi_stream);
    unlock(sys);
  }

  return;
}
static int MuteSet(audio_output_t* aout, bool mute) {
  aout_sys_t* sys = (aout_sys_t*)aout->sys;

  atomic_store(&sys->muted, mute);

  return VLC_SUCCESS;
}

static int Open(vlc_object_t* obj) {
  audio_output_t* aout = (audio_output_t*)obj;
  aout_sys_t* sys = calloc(1, sizeof(aout_sys_t));
  if(sys == NULL) {
    return VLC_ENOMEM;
  }

  PP_Instance instance = 0;
  vlc_value_t val;
  if(var_GetChecked(aout->p_libvlc, "ppapi-instance", VLC_VAR_INTEGER, &val) != VLC_ENOVAR) {
    instance = (PP_Instance)val.i_int;
  }
  if(instance == 0) {
    instance = vlc_getPPAPI_InitializingInstance();
    if(instance == 0) {
      msg_Err(aout, "couldn't get a reference to the PPAPI instance handle");
      free(sys);
      return VLC_EGENERIC;
    }
  }

  sys->instance = instance;
  sys->audio = 0;
  sys->config = 0;
  sys->rate = 0;
  vlc_mutex_init(&sys->lock);
  sys->underruns = ATOMIC_VAR_INIT(0);
  sys->aout = aout;
  sys->flushed = false;
  sys->volume_level = 1.0;
  sys->volume = vlc_object_create(aout, sizeof(*sys->volume));
  if(sys->volume == NULL) {
    free(sys);
    return VLC_ENOMEM;
  }
  sys->volume->format = VLC_CODEC_S16L;
  sys->volume_module = module_need(sys->volume, "audio volume", NULL, false);
  if(sys->volume_module == NULL) {
    msg_Err(aout, "no audio volume module?");
    free(sys);
    return VLC_EGENERIC;
  }
  block_BytestreamInit(&sys->ppapi_stream);

  sys->restarts = 0;
  sys->last_run = ATOMIC_VAR_INIT(VLC_TS_INVALID);

  aout->start = Start;
  aout->stop  = Stop;
  aout->time_get = TimeGet;
  aout->play  = Play;
  aout->pause = Pause;
  aout->flush = Flush;
  aout->volume_set = VolumeSet;
  aout->mute_set = MuteSet;
  aout->device_select = NULL;
  aout->sys = sys;

  return VLC_SUCCESS;
}

static void Close(vlc_object_t* obj) {
  audio_output_t* aout = (audio_output_t*)obj;
  aout_sys_t* sys = (aout_sys_t*)aout->sys;

  Stop(aout);

  if(sys->volume != NULL) {
    vlc_object_release(sys->volume);
  }

  if(sys->volume_module != NULL) {
    module_unneed(sys->volume, sys->volume_module);
  }

  if(sys->config != 0) {
    vlc_subResReference(sys->config);
  }

  free(sys);
  aout->sys = NULL;
  aout->start = NULL;
  aout->stop  = NULL;
  aout->time_get = NULL;
  aout->play  = NULL;
  aout->pause = NULL;
  aout->flush = NULL;
  aout->volume_set = NULL;
  aout->mute_set = NULL;
  aout->device_select = NULL;
}
