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

#define MODULE_NAME ppapi_control
#define MODULE_STRING "ppapi_control"

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_ppapi.h>
#include <vlc_interface.h>
#include <vlc_atomic.h>
#include <vlc_playlist.h>
#include <vlc_input.h>
#include <vlc_meta.h>
#include <vlc_arrays.h>

#include "common.hpp"
#include "ver-b.hpp"
#include "ver-a.hpp"

#include <math.h>
#include <climits>

#include <ppapi/c/ppp_message_handler.h>
#include <ppapi/c/pp_directory_entry.h>

VLC_PPAPI_MODULE_NAME("ppapi control");

using namespace std;

static int Open(vlc_object_t* obj);
static void Close(vlc_object_t* obj);

static void* ControlThreadEntry(void* ctxt);

static int PlaylistEventCallback(vlc_object_t *p_this, char const *cmd,
                                 vlc_value_t oldval, vlc_value_t newval,
                                 void *p_data);

vlc_module_begin()
    set_shortname(N_("ppapi-control"))
    set_description(N_("Control VLC from JS via PPAPI's messaging interface"))
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_MAIN)
    set_capability("interface", 0)
    set_callbacks(Open, Close)
vlc_module_end()

static int Open(vlc_object_t* obj) {
  intf_thread_t* intf = (intf_thread_t*)obj;

  intf_sys_t* sys = new (std::nothrow) intf_sys_t;
  if(unlikely(sys == NULL)) {
    return VLC_ENOMEM;
  }

  PP_Instance instance = vlc_getPPAPI_InitializingInstance();

  const vlc_ppapi_message_loop_t* iloop = vlc_getPPAPI_MessageLoop();

  PP_Resource msg_loop = iloop->Create(instance);
  if(unlikely(msg_loop == 0)) {
    vlc_ppapi_log_error(instance, "failed to create ppapi message loop");
    msg_Err(intf, "failed to create ppapi message loop");
    free(sys);
    return VLC_EGENERIC;
  }

  sys->parent = intf;
  sys->instance = instance;
  sys->message_loop = msg_loop;

  vlc_mutex_init(&sys->event_versions_mutex);

  vlc_mutex_init(&sys->init_mutex);  // don't return until the message handler
                                     // thread has finished registering itself
                                     // with PPAPI.

  vlc_mutex_lock(&sys->init_mutex);
  const int spawn = vlc_clone(&sys->handler_thread_id,
                              &ControlThreadEntry,
                              sys,
                              VLC_THREAD_PRIORITY_LOW);
  if(unlikely(spawn != VLC_SUCCESS)) {
    vlc_ppapi_log_error(instance, "failed to spawn message loop thread");
    msg_Err(intf, "failed to spawn message loop thread");
    iloop->PostQuit(sys->message_loop, PP_TRUE);
    vlc_subResReference(sys->message_loop);
    vlc_mutex_unlock(&sys->init_mutex);
    vlc_mutex_destroy(&sys->init_mutex);
    free(sys);
    return spawn;
  }

  vlc_cond_init(&sys->init_cond);
  vlc_cond_wait(&sys->init_cond, &sys->init_mutex);

  vlc_mutex_unlock(&sys->init_mutex);

  vlc_mutex_destroy(&sys->init_mutex);
  vlc_cond_destroy(&sys->init_cond);

  var_AddCallback(pl_Get(sys->parent),
                  "input-current",
                  PlaylistEventCallback,
                  static_cast<void*>(sys));

  // make sure the root of instance has "ppapi-instance" set:
  var_Create(intf->p_libvlc, "ppapi-instance", VLC_VAR_INTEGER);
  var_SetInteger(intf->p_libvlc, "ppapi-instance", instance);

  return spawn;
}
static void Close(vlc_object_t* obj) {
  intf_thread_t* intf = (intf_thread_t*)obj;
  intf_sys_t* sys = intf->p_sys;

  // cache the handler id:
  const vlc_thread_t handler_id = sys->handler_thread_id;

  const vlc_ppapi_messaging_t* imsg = vlc_getPPAPI_Messaging();
  vlc_subResReference(sys->message_loop);
  imsg->UnregisterMessageHandler(sys->instance);

  void* returned;
  vlc_join(handler_id, &returned);
  VLC_UNUSED(returned);

  // Don't free sys; the message handler thread does that.
  intf->p_sys = NULL;
}

namespace sys {
  static void get_log_level(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
    VLC_UNUSED(version); VLC_UNUSED(decl);
    VLC_UNUSED(arg);

    ret = PP_MakeInt32(vlc_getPPAPI_InstanceLogVerbosity(sys->instance));
    return_code = JS_SUCCESS;
  }
  static void set_log_level(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
    VLC_UNUSED(version); VLC_UNUSED(decl);
    VLC_UNUSED(arg); VLC_UNUSED(ret);

    vlc_setPPAPI_InstanceLogVerbosity(sys->instance, arg.value.as_int);
    return_code = JS_SUCCESS;
  }
  static const js_static_property_decl_t g_log_level =
    add_static_property("/sys/log_level",
                        JS_API_FIRST_VERSION,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_INT32(),
                        get_log_level,
                        set_log_level);



  static void get_version(intf_sys_t* sys,
                          const uint32_t version,
                          const js_static_method_decl_t* decl,
                          const PP_Var arg,
                          int32_t& return_code,
                          PP_Var& ret) {
    VLC_UNUSED(version); VLC_UNUSED(decl);
    VLC_UNUSED(arg);

    const vlc_ppapi_var_dictionary_t* idict = vlc_getPPAPI_VarDictionary();

    ret = idict->Create();
    for(size_t i = 0; i < sizeof(version_strings) / sizeof(version_strings[0]); ++i) {
      struct PP_Var key = vlc_ppapi_mk_str(&version_strings[i].key);
      struct PP_Var value = vlc_ppapi_mk_str(&version_strings[i].value);

      if(idict->Set(ret, key, value) != PP_TRUE) {
        msg_Err(sys->parent, "failed to set dictionary key `%s` to the value `%s`",
                version_strings[i].key.init, version_strings[i].value.init);
      }
    }

    PP_Var package_major_k = vlc_ppapi_mk_str(&package_major);
    PP_Var package_major_v = PP_MakeInt32(PACKAGE_VERSION_MAJOR);

    PP_Var package_minor_k = vlc_ppapi_mk_str(&package_minor);
    PP_Var package_minor_v = PP_MakeInt32(PACKAGE_VERSION_MINOR);

    PP_Var package_extra_k = vlc_ppapi_mk_str(&package_extra);
    PP_Var package_extra_v = PP_MakeInt32(PACKAGE_VERSION_EXTRA);

    idict->Set(ret, package_major_k, package_major_v);
    idict->Set(ret, package_minor_k, package_minor_v);
    idict->Set(ret, package_extra_k, package_extra_v);

    return_code = JS_SUCCESS;
  }
  static const js_static_property_decl_t g_version =
    add_static_property("/sys/version",
                        JS_API_FIRST_VERSION,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_UNDEFINED(),
                        get_version,
                        nullptr);

  #define MAX_CACHE_PATH 512
  static void purge_file(intf_sys_t* sys, const PP_Resource file) {
    const vlc_ppapi_file_ref_t* ifile_ref = vlc_getPPAPI_FileRef();
    const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();
    char* filename_buffer = (char*)alloca(MAX_CACHE_PATH);

    size_t name_len = 0;
    PP_Var name = ifile_ref->GetPath(file);
    const char* name_str = ivar->VarToUtf8(name, &name_len);
    assert(name_len + 1 < MAX_CACHE_PATH);
    memset(filename_buffer, 0, MAX_CACHE_PATH);
    memmove((void*)filename_buffer, (void*)name_str, name_len);
    vlc_ppapi_deref_var(name);
    msg_Info(sys->parent, "deleting file `%s`", filename_buffer);

    ifile_ref->Delete(file, PP_BlockUntilComplete());
  }
  static void purge_dir(intf_sys_t* sys,
                        const PP_Resource dir,
                        const bool remove_dir = true) {
    const vlc_ppapi_file_ref_t* ifile_ref = vlc_getPPAPI_FileRef();
    const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();
    VLC_PPAPI_ARRAY_OUTPUT(entries_out, output);

    if(remove_dir) {
      char* filename_buffer = (char*)alloca(MAX_CACHE_PATH);
      size_t name_len = 0;
      PP_Var name = ifile_ref->GetPath(dir);
      const char* name_str = ivar->VarToUtf8(name, &name_len);
      assert(name_len + 1 < MAX_CACHE_PATH);
      memset(filename_buffer, 0, MAX_CACHE_PATH);
      memmove((void*)filename_buffer, (void*)name_str, name_len);
      vlc_ppapi_deref_var(name);
      msg_Info(sys->parent, "deleting folder `%s`", filename_buffer);
    }

    int32_t code = ifile_ref->ReadDirectoryEntries(dir, output, PP_BlockUntilComplete());
    assert(code == PP_OK); VLC_UNUSED(code);

    PP_DirectoryEntry* entries = static_cast<PP_DirectoryEntry*>(entries_out.data);
    const size_t entry_len = entries_out.elements;

    for(size_t i = 0; i < entry_len; i++) {
      if(entries[i].file_type == PP_FILETYPE_DIRECTORY) {
        purge_dir(sys, entries[i].file_ref);
      } else {
        purge_file(sys, entries[i].file_ref);
      }
      vlc_subResReference(entries[i].file_ref);
    }

    free(entries);
    if(remove_dir) {
      ifile_ref->Delete(dir, PP_BlockUntilComplete());
    }
  }

  static void purge_cache(intf_sys_t* sys,
                          const uint32_t version,
                          const js_static_method_decl_t* decl,
                          const PP_Var arg,
                          int32_t& return_code,
                          PP_Var& ret) {
    VLC_UNUSED(version); VLC_UNUSED(decl);
    VLC_UNUSED(arg); VLC_UNUSED(ret);

    const vlc_ppapi_file_ref_t* ifile_ref = vlc_getPPAPI_FileRef();

    return_code = JS_SUCCESS;

    const PP_Resource cache_dir = ifile_ref->Create(vlc_ppapi_get_temp_fs(sys->instance),
                                                    "/vlc-cache");
    if(cache_dir == 0) { return; }
    purge_dir(sys, cache_dir, false);
    vlc_subResReference(cache_dir);
  }
  static js_static_method_decl_t* g_handle_purge_cache =
    add_static_method("/sys/purge_cache()",
                      JS_API_FIRST_VERSION,
                      JS_API_CURRENT_VERSION,
                      JSV_MATCH_UNDEFINED(),
                      purge_cache);


  static void events_subscribe(intf_sys_t* sys,
                               const uint32_t version,
                               const js_static_method_decl_t* decl,
                               const PP_Var arg,
                               int32_t& return_code,
                               PP_Var& ret) {
    VLC_UNUSED(decl);
    vlc_mutex_lock(&sys->event_versions_mutex);
    auto& event_locations = sys->event_versions[version];
    auto result = event_locations.emplace(std::make_pair(vlc_string_var_to_str(arg), 1));
    if(!result.second) {
      result.first->second += 1;
    }
    ret = PP_MakeInt32(result.first->second);
    return_code = JS_SUCCESS;
    vlc_mutex_unlock(&sys->event_versions_mutex);
  }
  static void events_unsubscribe(intf_sys_t* sys,
                                 const uint32_t version,
                                 const js_static_method_decl_t* decl,
                                 const PP_Var arg,
                                 int32_t& return_code,
                                 PP_Var& ret) {
    VLC_UNUSED(decl);
    vlc_mutex_lock(&sys->event_versions_mutex);
    auto& event_locations = sys->event_versions[version];
    auto event_location_refs = event_locations
      .find(vlc_string_var_to_str(arg));
    if(event_location_refs != event_locations.end()) {
      event_location_refs->second -= 1;
      ret = PP_MakeInt32(event_location_refs->second);
    } else {
      ret = PP_MakeInt32(0);
    }

    return_code = JS_SUCCESS;

    if(event_location_refs != event_locations.end() &&
       event_location_refs->second == 0) {
      event_locations.erase(event_location_refs);
    }
    vlc_mutex_unlock(&sys->event_versions_mutex);
  }

  static js_static_method_decl_t* g_subscribe_to_event =
    add_static_method("/sys/events/subscribe_to_event()",
                      JS_API_FIRST_VERSION,
                      JS_API_CURRENT_VERSION,
                      JSV_MATCH_STRING(nullptr),
                      events_subscribe);
  static js_static_method_decl_t* g_unsubscribe_to_event =
    add_static_method("/sys/events/unsubscribe_from_event()",
                      JS_API_FIRST_VERSION,
                      JS_API_CURRENT_VERSION,
                      JSV_MATCH_STRING(nullptr),
                      events_unsubscribe);
}

static inline struct PP_Var typed_obj_base(const vlc_ppapi_var_dictionary_t* idict,
                                           struct PP_Var type, struct PP_Var subtype);
static struct PP_Var media_to_var(playlist_item_t* pli);
static struct PP_Var media_to_var(input_item_t* media);

VLC_PPAPI_STATIC_STR(event_key, "event");
VLC_PPAPI_STATIC_STR(value_key, "value");
VLC_PPAPI_STATIC_STR(location_key, "location");
VLC_PPAPI_STATIC_STR(version_key, "version");

static PP_Var mk_event(const char* baseloc, const char* event_name) {

  const vlc_ppapi_var_dictionary_t* idict  = vlc_getPPAPI_VarDictionary();
  PP_Var event = typed_obj_base(idict,
                                vlc_ppapi_mk_str(&event_key),
                                PP_MakeUndefined());

  PP_Var location;
  {
    const char* format = "/%s/event/%s()";
    // find out how much buffer we need
    const int written = snprintf(NULL, 0, format, baseloc, event_name);
    assert(written > 0);
    char* buffer = (char*)alloca(written + 1);
    // the real format:
    snprintf(buffer, written + 1, format, baseloc, event_name);
    location = vlc_ppapi_cstr_to_var(buffer, written);
  }
  idict->Set(event, vlc_ppapi_mk_str(&location_key),
             location);
  vlc_ppapi_deref_var(location);

  idict->Set(event, vlc_ppapi_mk_str(&g_api_version_key),
             PP_MakeInt32(JS_API_VER_A));

  return event;
}

static int InputEvent(vlc_object_t *p_this, char const *psz_cmd,
                      vlc_value_t oldval, vlc_value_t newval,
                      void *p_data)
{
  VLC_UNUSED(p_this); VLC_UNUSED(psz_cmd); VLC_UNUSED(oldval);
  const vlc_ppapi_var_dictionary_t* idict  = vlc_getPPAPI_VarDictionary();

  intf_sys_t* sys = static_cast<intf_sys_t*>(p_data);

  const int event_type = newval.i_int;
  const char* event_name = NULL;
  switch (event_type) {
  case INPUT_EVENT_STATE: event_name = "state"; break;
  case INPUT_EVENT_DEAD: event_name = "dead"; break;
  case INPUT_EVENT_RATE: event_name = "rate"; break;
  case INPUT_EVENT_POSITION: event_name = "position"; break;
  case INPUT_EVENT_LENGTH: event_name = "length"; break;
  case INPUT_EVENT_CHAPTER: event_name = "chapter"; break;
  case INPUT_EVENT_PROGRAM: event_name = "program"; break;
  case INPUT_EVENT_ES: event_name = "es"; break;
  case INPUT_EVENT_TELETEXT: event_name = "teletext"; break;
  case INPUT_EVENT_RECORD: event_name = "record"; break;
  case INPUT_EVENT_ITEM_META: event_name = "item-meta"; break;
  case INPUT_EVENT_ITEM_INFO: event_name = "item-info"; break;
  case INPUT_EVENT_ITEM_NAME: event_name = "item-name"; break;
  case INPUT_EVENT_ITEM_EPG: event_name = "item-epg"; break;
  case INPUT_EVENT_STATISTICS: event_name = "statistics"; break;
  case INPUT_EVENT_SIGNAL: event_name = "signal"; break;
  case INPUT_EVENT_AUDIO_DELAY: event_name = "audio-delay"; break;
  case INPUT_EVENT_SUBTITLE_DELAY: event_name = "subtitle-delay"; break;
  case INPUT_EVENT_BOOKMARK: event_name = "bookmark"; break;
  case INPUT_EVENT_CACHE: event_name = "cache"; break;
  case INPUT_EVENT_AOUT: event_name = "aout"; break;
  case INPUT_EVENT_VOUT: event_name = "vout"; break;
  default: msg_Warn(sys->parent, "unknown `intf-event` event type: `%d`",
                    event_type); return VLC_EGENERIC;
  }

  PP_Var event = mk_event("input", event_name);

  PP_Var value = PP_MakeUndefined();
  input_thread_t* input = playlist_CurrentInput(pl_Get(sys->parent));

  bool drop_input = input != NULL;

  if(input == NULL) {
    goto end;
  }

  switch (event_type) {
  case INPUT_EVENT_STATE: {
    value = PP_MakeInt32(0);
    input_Control(input, INPUT_GET_STATE, &value.value.as_int);
    break;
  }
  case INPUT_EVENT_DEAD: {
    drop_input = false;
    break;
  }
  case INPUT_EVENT_RATE: {
    value = PP_MakeInt32(0);
    input_Control(input, INPUT_GET_STATE, &value.value.as_int);
    break;
  }
  case INPUT_EVENT_POSITION: {
    value = PP_MakeDouble(0.0);
    input_Control(input, INPUT_GET_POSITION, &value.value.as_int);
    break;
  }
  case INPUT_EVENT_LENGTH: {
    int64_t length = 0;
    input_Control(input, INPUT_GET_LENGTH, &length);
    value = split_mtime(length);
    break;
  }
  case INPUT_EVENT_ITEM_META: {
    value = media_to_var(input_GetItem(input));
    break;
  }
  case INPUT_EVENT_CACHE: {
    value = PP_MakeDouble((double)var_GetFloat(input, "cache"));
    break;
  }

  default: break;
  }

 end:

  idict->Set(event,
             vlc_ppapi_mk_str(&value_key),
             value);
  vlc_ppapi_deref_var(value);
  vlc_getPPAPI_Messaging()->PostMessage(sys->instance, event);
  vlc_ppapi_deref_var(event);

  if(drop_input) {
    vlc_object_release(input);
  }

  return VLC_SUCCESS;
}

static PP_Var vlc_value_to_var(int type, vlc_value_t val) {
  const vlc_ppapi_var_dictionary_t* idict  = vlc_getPPAPI_VarDictionary();
  const vlc_ppapi_var_array_t* iarray = vlc_getPPAPI_VarArray();

  VLC_PPAPI_STATIC_STR(value_type, "value");

  PP_Var r = typed_obj_base(idict, vlc_ppapi_mk_str(&value_type),
                           PP_MakeInt32(type));
  PP_Var v = PP_MakeNull();
  switch(type) {
  case VLC_VAR_VOID: v = PP_MakeUndefined(); break;
  case VLC_VAR_BOOL: {
    if(val.b_bool) {
      v = PP_MakeBool(PP_TRUE);
    } else {
      v = PP_MakeBool(PP_FALSE);
    }
    break;
  }
  case VLC_VAR_INTEGER: v = split_mtime(val.i_int); break;
  case VLC_VAR_STRING: {
    v = vlc_ppapi_cstr_to_var(val.psz_string, strlen(val.psz_string));
    break;
  }
  case VLC_VAR_FLOAT: v = PP_MakeDouble((double)val.f_float); break;
  case VLC_VAR_ADDRESS: {
    // May only be used for identity's sake.
    v = PP_MakeInt32((int32_t)val.p_address);
    break;
  }
  case VLC_VAR_COORDS: {
    v = iarray->Create();
    iarray->SetLength(v, 2);
    iarray->Set(v, 0, PP_MakeInt32(val.coords.x));
    iarray->Set(v, 1, PP_MakeInt32(val.coords.y));
    break;
  }
  default: v = PP_MakeUndefined(); break;
  }

  idict->Set(r, vlc_ppapi_mk_str(&value_key),
             v);
  vlc_ppapi_deref_var(v);
  return r;
}

VLC_PPAPI_STATIC_STR(oldval_key, "old_value");
VLC_PPAPI_STATIC_STR(newval_key, "new_value");

static int PlaylistEventCallback(vlc_object_t *p_this, char const *cmd,
                                 vlc_value_t oldval, vlc_value_t newval,
                                 void *p_data) {

  const vlc_ppapi_var_dictionary_t* idict  = vlc_getPPAPI_VarDictionary();

  intf_sys_t* sys = static_cast<intf_sys_t*>(p_data);

  msg_Dbg(sys->parent, "playlist `%s` callback", cmd);

  PP_Var event = mk_event("playlist", cmd);

  const int type = var_Type(p_this, cmd);
  PP_Var oldval_var = vlc_value_to_var(type, oldval);
  PP_Var newval_var = vlc_value_to_var(type, newval);
  idict->Set(event, vlc_ppapi_mk_str(&oldval_key),
             oldval_var);
  vlc_ppapi_deref_var(oldval_var);
  idict->Set(event, vlc_ppapi_mk_str(&newval_key),
             newval_var);
  vlc_ppapi_deref_var(newval_var);

  vlc_getPPAPI_Messaging()->PostMessage(sys->instance, event);
  vlc_ppapi_deref_var(event);

  if(strcmp(cmd, "input-current") == 0) {
    if(oldval.p_address != nullptr) {
      var_DelCallback((input_thread_t*)oldval.p_address, "intf-event",
                      &InputEvent, static_cast<void*>(sys));
    }
    if(newval.p_address != nullptr) {
      var_AddCallback((input_thread_t*)newval.p_address, "intf-event",
                      &InputEvent, static_cast<void*>(sys));
    }
  }

  return VLC_SUCCESS;
}


static inline void deref_var(struct PP_Var v) {
  const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();
  ivar->Release(v);
}
static inline
struct PP_Var cstr_to_var(const char* str,
                          const vlc_ppapi_var_t* ivar) {
  if(ivar == NULL) {
    ivar = vlc_getPPAPI_Var();
  }

  if(str == NULL) {
    return PP_MakeNull();
  } else {
    const uint32_t len = (uint32_t)strlen(str);
    return ivar->VarFromUtf8(str, len);
  }
}

static inline PP_Var split_mtime(const mtime_t v) {
  lldiv_t d = lldiv (v, CLOCK_FREQ);
  const int32_t lower = d.quot;
  const int32_t upper = d.rem * (1000000000 / CLOCK_FREQ);

  const vlc_ppapi_var_array_t* iarray = vlc_getPPAPI_VarArray();

  const struct PP_Var array = iarray->Create();
  PP_Bool set;
  set = iarray->SetLength(array, 2);
  if(set != PP_TRUE) {
    deref_var(array);
    return PP_MakeUndefined();
  }

  set = iarray->Set(array, 0, PP_MakeInt32(lower));
  if(set != PP_TRUE) {
    deref_var(array);
    return PP_MakeUndefined();
  }

  set = iarray->Set(array, 1, PP_MakeInt32(upper));
  if(set != PP_TRUE) {
    deref_var(array);
    return PP_MakeUndefined();
  }

  return array;
}
static inline mtime_t unsplit_mtime(const PP_Var v) {
  const vlc_ppapi_var_array_t* iarray = vlc_getPPAPI_VarArray();

  assert(g_mtime_desc.check(v, false));

  const struct PP_Var lower_v = iarray->Get(v, 0);
  const struct PP_Var upper_v = iarray->Get(v, 1);

  const int32_t lower = get_var_int32(lower_v);
  const int32_t upper = get_var_int32(upper_v);

  const int64_t result = (CLOCK_FREQ * lower) + (upper / 1000);
  return result;
}
VLC_PPAPI_STATIC_STR(media_type_value, "media");
VLC_PPAPI_STATIC_STR(is_parsed_key, "parsed");
VLC_PPAPI_STATIC_STR(mrl_key, "mrl");
VLC_PPAPI_STATIC_STR(playlist_item_id_key, "playlist_item_id");
VLC_PPAPI_STATIC_STR(meta_key, "meta");
VLC_PPAPI_STATIC_STR(duration_key, "duration");

static inline struct PP_Var typed_obj_base(const vlc_ppapi_var_dictionary_t* idict,
                                           struct PP_Var type, struct PP_Var subtype) {

  assert(idict != NULL);

  if(type.type == PP_VARTYPE_NULL || type.type == PP_VARTYPE_UNDEFINED) {
    return PP_MakeNull();
  }

  struct PP_Var r = idict->Create();

  struct PP_Var type_k = vlc_ppapi_mk_str(&type_key);
  struct PP_Var subtype_k = vlc_ppapi_mk_str(&subtype_key);
  idict->Set(r, type_k, type);
  idict->Set(r, subtype_k, subtype);

  return r;
}

static struct {
  vlc_meta_type_t meta;
  vlc_ppapi_static_str_t key;
} metadata_fields[] = {
  { vlc_meta_Title, VLC_PPAPI_STATIC_STR_INIT("title") },
  { vlc_meta_Artist, VLC_PPAPI_STATIC_STR_INIT("artist") },
  { vlc_meta_Genre, VLC_PPAPI_STATIC_STR_INIT("genre") },
  { vlc_meta_Copyright, VLC_PPAPI_STATIC_STR_INIT("copyright") },
  { vlc_meta_Album, VLC_PPAPI_STATIC_STR_INIT("album") },
  { vlc_meta_TrackNumber, VLC_PPAPI_STATIC_STR_INIT("track_number") },
  { vlc_meta_Description, VLC_PPAPI_STATIC_STR_INIT("description") },
  { vlc_meta_Rating, VLC_PPAPI_STATIC_STR_INIT("rating") },
  { vlc_meta_Date, VLC_PPAPI_STATIC_STR_INIT("date") },
  { vlc_meta_Setting, VLC_PPAPI_STATIC_STR_INIT("setting") },
  { vlc_meta_URL, VLC_PPAPI_STATIC_STR_INIT("url") },
  { vlc_meta_Language, VLC_PPAPI_STATIC_STR_INIT("language") },
  { vlc_meta_NowPlaying, VLC_PPAPI_STATIC_STR_INIT("now_playing") },
  { vlc_meta_Publisher,  VLC_PPAPI_STATIC_STR_INIT("publisher") },
  { vlc_meta_EncodedBy,  VLC_PPAPI_STATIC_STR_INIT("encoded_by") },
  { vlc_meta_ArtworkURL, VLC_PPAPI_STATIC_STR_INIT("artwork_url") },
  { vlc_meta_TrackID, VLC_PPAPI_STATIC_STR_INIT("track_id") },
  { vlc_meta_TrackTotal, VLC_PPAPI_STATIC_STR_INIT("track_total") },
  { vlc_meta_Director, VLC_PPAPI_STATIC_STR_INIT("director") },
  { vlc_meta_Season, VLC_PPAPI_STATIC_STR_INIT("season") },
  { vlc_meta_Episode, VLC_PPAPI_STATIC_STR_INIT("episode") },
  { vlc_meta_ShowName, VLC_PPAPI_STATIC_STR_INIT("show_name") },
  { vlc_meta_Actors, VLC_PPAPI_STATIC_STR_INIT("actors") },
  { vlc_meta_AlbumArtist, VLC_PPAPI_STATIC_STR_INIT("album_artist") },
  { vlc_meta_DiscNumber, VLC_PPAPI_STATIC_STR_INIT("disc_number") },
};

static PP_Var input_item_type_to_str(enum input_item_type_e type) {
  VLC_PPAPI_STATIC_STR(unknown, "unknown");
  VLC_PPAPI_STATIC_STR(file, "file");
  VLC_PPAPI_STATIC_STR(directory, "directory");
  VLC_PPAPI_STATIC_STR(disc, "disc");
  VLC_PPAPI_STATIC_STR(card, "card");
  VLC_PPAPI_STATIC_STR(stream, "stream");
  VLC_PPAPI_STATIC_STR(playlist, "playlist");
  VLC_PPAPI_STATIC_STR(node, "node");

  vlc_ppapi_static_str_t* s = NULL;

  switch(type) {
  case ITEM_TYPE_UNKNOWN:   s = &unknown;
  case ITEM_TYPE_FILE:      s = &file;
  case ITEM_TYPE_DIRECTORY: s = &directory;
  case ITEM_TYPE_DISC:      s = &disc;
  case ITEM_TYPE_CARD:      s = &card;
  case ITEM_TYPE_STREAM:    s = &stream;
  case ITEM_TYPE_PLAYLIST:  s = &playlist;
  case ITEM_TYPE_NODE:      s = &node;
  default: return PP_MakeUndefined();
  }

  return vlc_ppapi_mk_str(s);
}

static struct PP_Var media_to_var(playlist_item_t* pli) {
  const vlc_ppapi_var_dictionary_t* idict  = vlc_getPPAPI_VarDictionary();

  PP_Var r = media_to_var(pli->p_input);
  {
    struct PP_Var pli_id_k = vlc_ppapi_mk_str(&playlist_item_id_key);
    idict->Set(r, pli_id_k, PP_MakeInt32(pli->i_id));
  }
  return r;
}

static struct PP_Var media_to_var(input_item_t* media) {
  VLC_PPAPI_STATIC_STR(id_key, "input_item_id");

  const vlc_ppapi_var_t*            ivar   = vlc_getPPAPI_Var();
  const vlc_ppapi_var_dictionary_t* idict  = vlc_getPPAPI_VarDictionary();

  PP_Var subtype = input_item_type_to_str((input_item_type_e)media->i_type);

  struct PP_Var r = typed_obj_base(idict, vlc_ppapi_mk_str(&media_type_value), subtype);

  {
    struct PP_Var is_parsed_k = vlc_ppapi_mk_str(&is_parsed_key);
    struct PP_Var is_parsed_v;
    if(input_item_IsPreparsed(media)) {
      is_parsed_v = PP_MakeBool(PP_TRUE);
    } else {
      is_parsed_v = PP_MakeBool(PP_FALSE);
    }

    idict->Set(r, is_parsed_k, is_parsed_v);
  }

  {
    struct PP_Var mrl_k = vlc_ppapi_mk_str(&mrl_key);
    const char* mrl = input_item_GetURI(media);
    struct PP_Var mrl_v = cstr_to_var(mrl, ivar);
    idict->Set(r, mrl_k, mrl_v);
    deref_var(mrl_v);
  }

  {
    PP_Var id_k = vlc_ppapi_mk_str(&id_key);
    idict->Set(r, id_k, PP_MakeInt32(media->i_id));
  }
  {
    PP_Var duration_k = vlc_ppapi_mk_str(&duration_key);
    idict->Set(r, duration_k, split_mtime(media->i_duration));
  }

  {
    struct PP_Var meta_k = vlc_ppapi_mk_str(&meta_key);
    struct PP_Var meta_v;
    if(input_item_IsPreparsed(media)) {
      meta_v = idict->Create();
      for(uint32_t i = 0;
          i < sizeof(metadata_fields) / sizeof(metadata_fields[0]);
          i++) {

        struct PP_Var k = vlc_ppapi_mk_str(&metadata_fields[i].key);

        const char* v_str = input_item_GetMeta(media,
                                               metadata_fields[i].meta);
        struct PP_Var v = cstr_to_var(v_str, ivar);
        idict->Set(meta_v, k, v);
        deref_var(v);
      }
    } else {
      meta_v = PP_MakeUndefined();
    }
    idict->Set(r, meta_k, meta_v);
    deref_var(meta_v);
  }

  return r;
}

static void handle_message(intf_sys_t* sys,
                           const struct PP_Var& msg,
                           PP_Var& ret_val) {
  VLC_PPAPI_STATIC_STR(return_type, "return");

  const vlc_ppapi_console_t* iconsole = vlc_getPPAPI_Console();
  const vlc_ppapi_var_dictionary_t* idict  = vlc_getPPAPI_VarDictionary();

  const PP_Var req_id_k = vlc_ppapi_mk_str(&g_request_id_key);
  const PP_Var location_k = vlc_ppapi_mk_str(&g_location_key);

  int32_t result_code = 400;

  const char* location_cstr = nullptr;
  PP_Var arg_v = PP_MakeUndefined();
  PP_Var location_v = PP_MakeUndefined();
  PP_Var request_id_v = PP_MakeUndefined();
  PP_Var inner_ret_v = PP_MakeUndefined();

  js_object_desc_t::Match check = js_object_desc_t::Match::No;
  check = JSV_OBJECT_TYPE_DESC_REF(request)->check(msg, false);
  if(check == js_object_desc_t::Match::No) {
    result_code = 400;
    vlc_ppapi_log_warning(sys->instance, "unknown message format!");
    iconsole->Log(sys->instance, PP_LOGLEVEL_WARNING, msg);
    return;
  } else if(check == js_object_desc_t::Match::Partial) {
    result_code = 400;
    vlc_ppapi_log_warning(sys->instance, "malformed request message!");
    iconsole->Log(sys->instance, PP_LOGLEVEL_WARNING, msg);
    return;
  } else if(check == js_object_desc_t::Match::Yes) {
    const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();

    const PP_Var version_k = vlc_ppapi_mk_str(&g_api_version_key);
    const PP_Var arg_k = vlc_ppapi_mk_str(&g_arguments_key);
    const PP_Var request_id_k = vlc_ppapi_mk_str(&g_request_id_key);

    location_v = idict->Get(msg, location_k);
    const uint32_t version = get_var_uint32(idict->Get(msg, version_k));
    arg_v = idict->Get(msg, arg_k);
    request_id_v = idict->Get(msg, request_id_k);

    size_t location_len = 0;
    const char* location_str = ivar->VarToUtf8(location_v, &location_len);
    location_cstr = vlc_ppapi_null_terminate(location_str, location_len);
    if(location_cstr == nullptr) {
      result_code = JS_EGENERIC;
      goto done;
    }

    result_code = JS_ENOTFOUND;
    const auto& locations = get_methods();
    const auto locations_range = locations.equal_range(location_cstr);
    for(auto i = locations_range.first; i != locations_range.second; i++) {
      const auto& method = i->second;
      if(version < method.version_added ||
         method.version_removed < version) {
        continue;
      }

      if(!method.arg.check(arg_v, false)) {
        continue;
      }


      if(method.handler == nullptr) {
        result_code = JS_SUCCESS;
        break;
      }

      method.handler(sys, version, &method, arg_v, result_code, inner_ret_v);
      break;
    }

    goto done;
  }

 done:
  if(location_cstr != nullptr) { free((void*)location_cstr); }
  vlc_ppapi_deref_var(arg_v);

  ret_val = typed_obj_base(idict, vlc_ppapi_mk_str(&return_type), PP_MakeUndefined());
  idict->Set(ret_val, req_id_k, request_id_v);

  PP_Var ret_code_k = vlc_ppapi_mk_str(&g_return_code_key);
  PP_Var ret_val_k = vlc_ppapi_mk_str(&g_return_value_key);

  idict->Set(ret_val, ret_val_k, inner_ret_v);
  vlc_ppapi_deref_var(inner_ret_v);
  idict->Set(ret_val, ret_code_k, PP_MakeInt32(result_code));
  idict->Set(ret_val, location_k, location_v);
  vlc_ppapi_deref_var(location_v);
}

static void PPPHandleMessage(PP_Instance instance, void* sys_,
                             const struct PP_Var *message) {
  VLC_UNUSED(instance);
  assert(((intf_sys_t*)sys_)->instance == instance);

  intf_sys_t* sys = (intf_sys_t*)sys_;
  PP_Var ret = PP_MakeUndefined();
  handle_message(sys, *message, ret);

  vlc_getPPAPI_Messaging()->PostMessage(sys->instance, ret);
  vlc_ppapi_deref_var(ret);
}
static void PPPHandleBlockingMessage(PP_Instance instance, void* sys_,
                                     const struct PP_Var* message,
                                     struct PP_Var* response) {
  VLC_UNUSED(instance);
  assert(((intf_sys_t*)sys_)->instance == instance);

  intf_sys_t* sys = (intf_sys_t*)sys_;

  *response = PP_MakeUndefined();
  handle_message(sys, *message, *response);
}
static void PPPDestroy(PP_Instance instance, void* sys_) {
  VLC_UNUSED(instance);
  assert(((intf_sys_t*)sys_)->instance == instance);
  free(sys_);
}

static const struct PPP_MessageHandler_0_2 ppp_messaging = {
  PPPHandleMessage, PPPHandleBlockingMessage, PPPDestroy,
};

static void* ControlThreadEntry(void* ctxt) {
  intf_sys_t* sys = (intf_sys_t*)ctxt;

  const vlc_ppapi_message_loop_t* iloop = vlc_getPPAPI_MessageLoop();
  const vlc_ppapi_messaging_t* imsg = vlc_getPPAPI_Messaging();

  iloop->AttachToCurrentThread(sys->message_loop);

  imsg->RegisterMessageHandler(sys->instance,
                               sys,
                               &ppp_messaging,
                               sys->message_loop);

  vlc_mutex_lock(&sys->init_mutex);
  vlc_cond_signal(&sys->init_cond);
  vlc_mutex_unlock(&sys->init_mutex);

  iloop->Run(sys->message_loop);

  delete sys;

  return NULL;
}
