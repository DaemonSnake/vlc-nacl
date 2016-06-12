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

#include <config.h>

#include <stdlib.h>
#include <assert.h>
#include <sys/mount.h>

#include <vlc/vlc.h>
/* VLC core API headers */
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_vout.h>

#include <vlc/libvlc_media_player.h>

#include <vlc_ppapi.h>

#include "../src/modules/modules.h"
#include "../lib/media_player_internal.h"

#include <ppapi/c/pp_module.h>

#include <ppapi/c/ppp_instance.h>
#include <ppapi/c/ppp_messaging.h>
#include <ppapi/c/ppb_view.h>

#include <ppapi/gles2/gl2ext_ppapi.h>

VLC_PPAPI_MODULE_NAME("vlc");

#define PLUGIN_INIT_SYMBOL(name)                       \
  int CONCATENATE(vlc_entry, name)                     \
       (int (*)(void*, void*, int, ...),               \
        void*);
#include "vlc_static_modules_init.h"
#undef PLUGIN_INIT_SYMBOL

// A few modules are manually disabled because they trigger asserts within
// clang:
#define PLUGIN_INIT_SYMBOL(name)                          \
  int CONCATENATE(vlc_entry, name)                        \
       (int (*one)(void*, void*, int, ...),               \
        void* two) {                                      \
    VLC_UNUSED(one); VLC_UNUSED(two);                     \
    return VLC_EGENERIC; }

#undef PLUGIN_INIT_SYMBOL

#define PLUGIN_INIT_SYMBOL(name)                \
  CONCATENATE(vlc_entry, name),

vlc_plugin_cb vlc_static_modules[] = {
#include "vlc_static_modules_init.h"
  NULL
};
#undef PLUGIN_INIT_SYMBOL

static PP_Module g_module;

int32_t PPP_InitializeModule(PP_Module mod, PPB_GetInterface get_interface);
void 	PPP_ShutdownModule (void);
const void* PPP_GetInterface(const char* interface_name);

static PP_Bool vlc_did_create(PP_Instance instance, uint32_t argc,
                              const char *argn_[], const char *argv_[]);
static void vlc_did_destroy(PP_Instance pp);
static void vlc_did_change_view(PP_Instance pp, PP_Resource view);
static void vlc_did_change_focus(PP_Instance pp, const PP_Bool visible);
static PP_Bool vlc_handle_document_load(PP_Instance pp, PP_Resource url_loader);

static void libvlc_logging_callback(void* data, int type,
                                    const libvlc_log_t* item,
                                    const char* format, va_list args);

static const struct PPP_Instance_1_1 g_ppp_instance = {
  vlc_did_create,
  vlc_did_destroy,
  vlc_did_change_view,
  vlc_did_change_focus,
  vlc_handle_document_load,
};

typedef struct instance_t {
  PP_Instance pp;
  libvlc_instance_t *vlc;
  libvlc_media_player_t* media_player;
  libvlc_media_list_player_t* media_list_player;
  libvlc_media_list_t* playlist;
} instance_t;

// the array of instances is stored after instances_t in memory.
typedef struct instances_t {
  size_t count;
} instances_t;

static instance_t* get_instances_array(instances_t* instances) {
  return (instance_t*)(instances + 1);
}

// MAY ONLY BE ACCESSED FROM THE MAIN THREAD, ie the thread that runs vlc_did_* below.
static instances_t* g_instances = NULL;

#define CHECKMALLOC(msg, var, ret) if (unlikely(var == NULL)) { printf("`%s` returned null!", msg); return (ret); } true
#define NULLABORT(msg, var) if (unlikely((var) == NULL)) { printf("`%s` returned null! Aborting! (" __FILE__ ":" __LINE__ ")", msg); abort(); } true

static instance_t* get_instance(PP_Instance inst) {
  if(g_instances == NULL) {
    return NULL;
  } else {
    assert(g_instances->count != 0);
    instance_t* instances = get_instances_array(g_instances);
    for(size_t i = 0; i < g_instances->count; i++) {
      instance_t* instance = &instances[i];
      if(instance->pp == inst) { return instance; }
    }

    return NULL;
  }
}
static instance_t* add_instance(PP_Instance pp_inst) {

  const size_t next_count = (g_instances != NULL ? g_instances->count : 0) + 1;
  const size_t new_array_bytes = sizeof(instance_t) * next_count;
  instances_t* new_instances = malloc(new_array_bytes + sizeof(instances_t));
  CHECKMALLOC("malloc new_instances", new_instances, NULL);

  new_instances->count = next_count;
  instance_t* new_array = get_instances_array(new_instances);

  if(g_instances != NULL) {
    memmove(new_array, get_instances_array(g_instances), new_array_bytes);
  }

  const size_t new_idx = next_count - 1;
  new_array[new_idx].pp = pp_inst;
  new_array[new_idx].vlc = NULL;
  new_array[new_idx].media_player = NULL;
  new_array[new_idx].media_list_player = NULL;
  new_array[new_idx].playlist = NULL;

  instances_t* old_instances = g_instances;
  g_instances = new_instances;
  if(old_instances != NULL) {
    free(old_instances);
  }

  return &get_instances_array(g_instances)[new_idx];
}

static PP_Bool remove_instance(instance_t* instance) {
  assert(instance != NULL);
  assert(g_instances != NULL && g_instances->count > 0);

  const size_t new_bytes = sizeof(instance_t) * (g_instances->count - 1);
  instances_t* new_instances = malloc(new_bytes + sizeof(instances_t));
  CHECKMALLOC("malloc new_instances", new_instances, PP_FALSE);

  new_instances->count = g_instances->count - 1;

  instance_t* cur_array = get_instances_array(g_instances);
  instance_t* new_array = get_instances_array(new_instances);

  const size_t slice_start = (size_t)(instance - cur_array);
  const size_t slice_end   = slice_start + 1;
  memmove(new_array, cur_array, (size_t)(instance - cur_array));
  memmove((uint8_t*)new_array + slice_start,
          (uint8_t*)cur_array + slice_end,
          new_bytes - slice_end);

  instances_t* old_instances = g_instances;
  g_instances = new_instances;

  if(instance->vlc != NULL) {
    libvlc_release(instance->vlc);
  }
  if(instance->media_player != NULL) {
    libvlc_media_player_release(instance->media_player);
  }
  if(instance->media_list_player != NULL) {
    libvlc_media_list_player_release(instance->media_list_player);
  }
  if(instance->playlist != NULL) {
    libvlc_media_list_release(instance->playlist);
  }

  free(old_instances);

  return PP_OK;
}

PP_Bool _internal_VLCInitializeGetInterface(PPB_GetInterface get_interface);

int32_t PPP_InitializeModule(PP_Module mod, PPB_GetInterface get_interface) {
  g_module = mod;

  if(_internal_VLCInitializeGetInterface(get_interface) != PP_TRUE) {
    return PP_FALSE;
  }

  if(!glInitializePPAPI(get_interface)) {
    printf("failed to initialize ppapi gles2 interface");
    abort();
    return PP_FALSE;
  } else {
    return PP_OK;
  }
}

// There's no need to implement this. All resources are force-freed by the sandbox when it exits.
void PPP_ShutdownModule() {}

const void* PPP_GetInterface(const char* interface_name) {
  if(strcmp(interface_name, "PPP_Instance;1.1") == 0) {
    return (const void*)&g_ppp_instance;
  } else {
    return NULL;
  }
}

// TODO don't leak shit.
static PP_Bool vlc_did_create(PP_Instance instance, uint32_t _argc,
                              const char *_argn[], const char *_argv[]) {
  vlc_setPPAPI_InitializingInstance(instance);

  VLC_UNUSED(_argc); VLC_UNUSED(_argn); VLC_UNUSED(_argv);

  if(vlc_PPAPI_InitializeInstance(instance) != VLC_SUCCESS) {
    return PP_FALSE;
  }

  PP_Bool ret = PP_FALSE;

  libvlc_instance_t* vlc_inst = NULL;
  libvlc_media_player_t* media_player = NULL;
  libvlc_media_list_player_t* media_list_player = NULL;
  libvlc_media_list_t* playlist = NULL;

  instance_t* new_inst = add_instance(instance);
  if(new_inst == NULL) {
    vlc_ppapi_log_error(instance, "failed to create the plugin instance object!\n");
    goto error;
  }

  vlc_inst = libvlc_new(0, NULL);
  if(vlc_inst == NULL) {
    vlc_ppapi_log_error(instance, "failed to create the vlc instance!\n");
    goto error;
  } else {
    new_inst->vlc = vlc_inst;
  }

  libvlc_log_set(vlc_inst, libvlc_logging_callback, (void*)new_inst);

  media_player = libvlc_media_player_new(vlc_inst);
  if(media_player == NULL) {
    vlc_ppapi_log_error(instance, "libvlc_media_player_t creation failed");
    goto error;
  } else {
    new_inst->media_player = media_player;
  }
  var_Create(media_player, "ppapi-instance", VLC_VAR_INTEGER);
  var_SetInteger(media_player, "ppapi-instance", instance);
  var_SetString(media_player, "vout", "ppapi_vout_graphics3d");

  if(-1 == libvlc_add_intf(vlc_inst, "ppapi_control")) {
    vlc_ppapi_log_error(instance, "failed to start `ppapi-control`");
    goto error;
  }

  ret = PP_TRUE;
  goto done;

 error:
  ret = PP_FALSE;
  if(new_inst != NULL) {
    remove_instance(new_inst);
  }

 done:
  vlc_setPPAPI_InitializingInstance(0);

  return ret;
}

static void vlc_did_destroy(PP_Instance pp) {
  instance_t* instance = get_instance(pp);
  if (instance == NULL) { return; /* TODO: log */ }

  if(remove_instance(instance) == PP_FALSE) {
    assert(PP_FALSE && "failed to remove an instance");
    return;
  }
}

static void vlc_did_change_view(PP_Instance pp, PP_Resource v) {
  instance_t* instance = get_instance(pp);
  if(instance == NULL) { return; }
  msg_Dbg(instance->media_player, "Instance changed its viewport");

  vlc_setPPAPI_InstanceViewport(pp, v);
}

static void vlc_did_change_focus(PP_Instance pp, const PP_Bool focus) {
  instance_t* instance = get_instance(pp);
  if(instance == NULL) { return; }
  msg_Dbg(instance->media_player, "Instance changed focus to `%s`",
           focus == PP_TRUE ? "true" : "false");

  vlc_setPPAPI_InstanceFocus(pp, focus);
}

static PP_Bool vlc_handle_document_load(PP_Instance pp, PP_Resource url_loader) {
  VLC_UNUSED(pp);
  VLC_UNUSED(url_loader);
  return PP_FALSE;
}

static PP_Var create_source_var(const libvlc_log_t* item) {
  const char format[] = "[module `%s`] [header `%s`] [object `%s`]";

  const char* module = item->psz_module;
  if(module == NULL) {
    module = "unknown";
  }
  const char* header = item->psz_header;
  if(header == NULL) {
    header = "unknown";
  }
  const char* object_type = item->psz_object_type;
  if(object_type == NULL) {
    object_type = "unknown";
  }

  const int buffer_size = snprintf(NULL, 0, format, module,
                                   header, object_type);

  char* buffer = alloca(buffer_size + 1);
  snprintf(buffer, buffer_size + 1, format, module, header, object_type);

  return vlc_ppapi_cstr_to_var(buffer, buffer_size);
}

static void libvlc_logging_callback(void* data, int libvlc_type,
                                    const libvlc_log_t* item,
                                    const char* format, va_list args) {
  instance_t* inst = (instance_t*)data;
  assert(inst != NULL);

  const int log_verbosity = vlc_getPPAPI_InstanceLogVerbosity(inst->pp);
  if(log_verbosity > libvlc_type) { return; }

  PP_LogLevel level = PP_LOGLEVEL_ERROR;
  switch(libvlc_type) {
  case LIBVLC_DEBUG: level = PP_LOGLEVEL_TIP; break;
  case LIBVLC_NOTICE: level = PP_LOGLEVEL_LOG; break;
  case LIBVLC_WARNING: level = PP_LOGLEVEL_WARNING; break;
  case LIBVLC_ERROR: level = PP_LOGLEVEL_ERROR; break;
  default: level = PP_LOGLEVEL_ERROR; break;
  }

  char* buffer = NULL;
  const int buffer_size = vasprintf(&buffer, format, args);
  if(buffer_size < 0) {
    printf("failed to format log\n");
    return;
  }

  PP_Var msg = vlc_ppapi_cstr_to_var(buffer, buffer_size);
  free(buffer);

  PP_Var src = create_source_var(item);
  vlc_getPPAPI_Console()->LogWithSource(inst->pp, level, src, msg);
  vlc_ppapi_deref_var(msg);
  vlc_ppapi_deref_var(src);
}
