/**
 * @file ppapi.c
 * @brief Provides definitions to the functions declared in vlc_ppapi.h
 */
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

#include <stdlib.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_arrays.h>
#include <vlc_threads.h>
#include <vlc_atomic.h>
#include <vlc_ppapi.h>

#define NULLABORT(msg, var) if (unlikely((var) == NULL)) {              \
    printf("`%s` is null! Aborting! (%s)",                              \
           msg, __func__);                                              \
    abort();                                                            \
  } true
#define CHECKNULL(msg, var, ret) if (unlikely((var) == NULL)) {         \
    printf("`%s` is null!", msg);                                       \
    return (ret);                                                       \
  } true

static const vlc_ppapi_audio_t*          g_audio          = NULL;
static const vlc_ppapi_audio_config_t*   g_audio_config   = NULL;
static const vlc_ppapi_console_t*        g_console        = NULL;
static const vlc_ppapi_core_t*           g_core           = NULL;
static const vlc_ppapi_file_io_t*        g_file_io        = NULL;
static const vlc_ppapi_file_ref_t*       g_file_ref       = NULL;
static const vlc_ppapi_file_system_t*    g_file_system    = NULL;
static const vlc_ppapi_graphics_3d_t*    g_graphics_3d    = NULL;
static const vlc_ppapi_instance_t*       g_instance       = NULL;
static const vlc_ppapi_mouse_cursor_t*   g_mouse_cursor   = NULL;
static const vlc_ppapi_message_loop_t*   g_message_loop   = NULL;
static const vlc_ppapi_messaging_t*      g_messaging      = NULL;
static const vlc_ppapi_url_loader_t*     g_url_loader     = NULL;
static const vlc_ppapi_url_request_info_t* g_url_request_info = NULL;
static const vlc_ppapi_url_response_info_t* g_url_response_info = NULL;
static const vlc_ppapi_var_t*            g_var            = NULL;
static const vlc_ppapi_var_array_t*      g_var_array      = NULL;
static const vlc_ppapi_var_dictionary_t* g_var_dictionary = NULL;
static const vlc_ppapi_view_t*           g_view           = NULL;

const vlc_ppapi_audio_t* vlc_getPPAPI_Audio(void) {
  NULLABORT("PPB_Audio interface", g_audio);
  return g_audio;
}
const vlc_ppapi_audio_config_t* vlc_getPPAPI_AudioConfig(void) {
  NULLABORT("PPB_AudioConfig interface", g_audio_config);
  return g_audio_config;
}
const vlc_ppapi_console_t* vlc_getPPAPI_Console(void) {
  NULLABORT("PPB_Console interface", g_console);
  return g_console;
}
const vlc_ppapi_core_t* vlc_getPPAPI_Core(void) {
  NULLABORT("PPB_Core interface", g_core);
  return g_core;
}
const vlc_ppapi_file_io_t* vlc_getPPAPI_FileIO(void) {
  NULLABORT("PPB_FileIO interface", g_file_io);
  return g_file_io;
}
const vlc_ppapi_file_ref_t* vlc_getPPAPI_FileRef(void) {
  NULLABORT("PPB_FileRef interface", g_file_ref);
  return g_file_ref;
}
const vlc_ppapi_file_system_t* vlc_getPPAPI_FileSystem(void) {
  NULLABORT("PPB_FileSystem interface", g_file_system);
  return g_file_system;
}
const vlc_ppapi_graphics_3d_t* vlc_getPPAPI_Graphics3D(void) {
  NULLABORT("PPB_Graphics3D interface", g_graphics_3d);
  return g_graphics_3d;
}
const vlc_ppapi_instance_t* vlc_getPPAPI_Instance(void) {
  NULLABORT("PPB_Instance interface", g_instance);
  return g_instance;
}
const vlc_ppapi_mouse_cursor_t* vlc_getPPAPI_MouseCursor(void) {
  NULLABORT("PPB_MouseCursor interface", g_mouse_cursor);
  return g_mouse_cursor;
}
const vlc_ppapi_message_loop_t* vlc_getPPAPI_MessageLoop(void) {
  NULLABORT("PPB_MessageLoop interface", g_message_loop);
  return g_message_loop;
}
const vlc_ppapi_messaging_t* vlc_getPPAPI_Messaging(void) {
  NULLABORT("PPB_Messaging interface", g_messaging);
  return g_messaging;
}
const vlc_ppapi_url_loader_t* vlc_getPPAPI_URLLoader(void) {
  NULLABORT("PPB_URLLoader interface", g_url_loader);
  return g_url_loader;
}
const vlc_ppapi_url_request_info_t* vlc_getPPAPI_URLRequestInfo(void) {
  NULLABORT("PPB_URLRequestInfo interface", g_url_request_info);
  return g_url_request_info;
}
const vlc_ppapi_url_response_info_t* vlc_getPPAPI_URLResponseInfo(void) {
  NULLABORT("PPB_URLResponseInfo interface", g_url_response_info);
  return g_url_response_info;
}
const vlc_ppapi_var_t* vlc_getPPAPI_Var(void) {
  NULLABORT("PPB_Var interface", g_var);
  return g_var;
}
const vlc_ppapi_var_array_t* vlc_getPPAPI_VarArray(void) {
  NULLABORT("PPB_VarArray interface", g_var_array);
  return g_var_array;
}
const vlc_ppapi_var_dictionary_t* vlc_getPPAPI_VarDictionary(void) {
  NULLABORT("PPB_VarDictionary interface", g_var_dictionary);
  return g_var_dictionary;
}
const vlc_ppapi_view_t* vlc_getPPAPI_View(void) {
  NULLABORT("PPB_View interface", g_view);
  return g_view;
}

static PPB_GetInterface g_get_interface = NULL;

PPB_GetInterface vlc_getPPBGetInstance(void) {
  return g_get_interface;
}

PP_Bool _internal_VLCInitializeGetInterface(PPB_GetInterface get_interface);
// The versions used here *MUST* match the versions on the structs found in vlc_ppapi.h
PP_Bool _internal_VLCInitializeGetInterface(PPB_GetInterface get_interface) {
  g_get_interface = get_interface;

  PPB_GetInterface gi = get_interface;

  g_audio = (const vlc_ppapi_audio_t*)vlc_getPPBGetInstance()(PPB_AUDIO_INTERFACE_1_1);
  CHECKNULL("get PPB_Audio interface", g_audio, PP_FALSE);

  g_audio_config = (const vlc_ppapi_audio_config_t*)vlc_getPPBGetInstance()(PPB_AUDIO_CONFIG_INTERFACE_1_1);
  CHECKNULL("get PPB_AudioConfig interface", g_audio_config, PP_FALSE);

  g_console = (const vlc_ppapi_console_t*)vlc_getPPBGetInstance()(PPB_CONSOLE_INTERFACE_1_0);
  CHECKNULL("get PPB_Console interface", g_console, PP_FALSE);

  g_core = (const vlc_ppapi_core_t*)vlc_getPPBGetInstance()(PPB_CORE_INTERFACE_1_0);
  CHECKNULL("get PPB_Core interface", g_core, PP_FALSE);

  g_file_io = (const vlc_ppapi_file_io_t*)gi(PPB_FILEIO_INTERFACE_1_1);
  CHECKNULL("get PPB_FileIO interface", g_file_io, PP_FALSE);
  g_file_ref = (const vlc_ppapi_file_ref_t*)gi(PPB_FILEREF_INTERFACE_1_2);
  CHECKNULL("get PPB_FileRef interface", g_file_ref, PP_FALSE);
  g_file_system = (const vlc_ppapi_file_system_t*)gi(PPB_FILESYSTEM_INTERFACE_1_0);
  CHECKNULL("get PPB_FileSystem interface", g_file_system, PP_FALSE);

  g_graphics_3d = (const vlc_ppapi_graphics_3d_t*)vlc_getPPBGetInstance()(PPB_GRAPHICS_3D_INTERFACE_1_0);
  CHECKNULL("get PPB_Graphics3D interface", g_graphics_3d, PP_FALSE);

  g_instance = (const vlc_ppapi_instance_t*)gi(PPB_INSTANCE_INTERFACE_1_0);
  CHECKNULL("get PPB_Instance interface", g_instance, PP_FALSE);

  g_mouse_cursor = (const vlc_ppapi_mouse_cursor_t*)vlc_getPPBGetInstance()(PPB_MOUSECURSOR_INTERFACE_1_0);
  CHECKNULL("get PPB_MouseInterface interface", g_mouse_cursor, PP_FALSE);

  g_message_loop = (const vlc_ppapi_message_loop_t*)gi(PPB_MESSAGELOOP_INTERFACE_1_0);
  CHECKNULL("get PPB_MessageLoop interface", g_mouse_cursor, PP_FALSE);

  g_messaging = (const vlc_ppapi_messaging_t*)gi(PPB_MESSAGING_INTERFACE_1_2);
  CHECKNULL("get PPB_Messaging interface", g_mouse_cursor, PP_FALSE);

  g_url_loader = (const vlc_ppapi_url_loader_t*)gi(PPB_URLLOADER_INTERFACE_1_0);
  CHECKNULL("get PPB_URLLoader interface", g_mouse_cursor, PP_FALSE);
  g_url_request_info = (const vlc_ppapi_url_request_info_t*)gi(PPB_URLREQUESTINFO_INTERFACE_1_0);
  CHECKNULL("get PPB_URLRequest interface", g_mouse_cursor, PP_FALSE);
  g_url_response_info = (const vlc_ppapi_url_response_info_t*)gi(PPB_URLRESPONSEINFO_INTERFACE_1_0);
  CHECKNULL("get PPB_URLResponse interface", g_mouse_cursor, PP_FALSE);

  g_var = (const vlc_ppapi_var_t*)gi(PPB_VAR_INTERFACE_1_2);
  CHECKNULL("get PPB_Var interface", g_mouse_cursor, PP_FALSE);
  g_var_array = (const vlc_ppapi_var_array_t*)gi(PPB_VAR_ARRAY_INTERFACE_1_0);
  CHECKNULL("get PPB_VarArray interface", g_mouse_cursor, PP_FALSE);
  g_var_dictionary = (const vlc_ppapi_var_dictionary_t*)gi(PPB_VAR_DICTIONARY_INTERFACE_1_0);
  CHECKNULL("get PPB_VarDictionary interface", g_mouse_cursor, PP_FALSE);

  g_view = (const vlc_ppapi_view_t*)vlc_getPPBGetInstance()(PPB_VIEW_INTERFACE_1_2);
  CHECKNULL("get PPB_View interface", g_view, PP_FALSE);

  return PP_TRUE;
}

PP_Var vlc_ppapi_mk_str_var(atomic_uintptr_t* var, const char* const init, const size_t len) {
  // do a cheap, non-atomic, read of var:
  if(*var != 0) { return *(PP_Var*) *var; }

  const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();

  struct PP_Var* new_var = malloc(sizeof(struct PP_Var));
  if(new_var == NULL) {
    return PP_MakeNull();
  }

  *new_var = ivar->VarFromUtf8(init, len);

  uintptr_t expected = 0;
  if(!atomic_compare_exchange_strong(var, &expected,
                                     (uintptr_t)new_var)) {
    // another thread beat us.
    free(new_var);
    return *(PP_Var*) atomic_load(var);
  } else {
    return *new_var;
  }
}
void vlc_ppapi_log_va(const PP_Instance instance, const PP_LogLevel level,
                      PP_Var module, const char* format, va_list args) {
  // sadly, and for no reason, chrome truncates messages longer than 256 characters.
  // nonetheless, we still write it all out for debugging with chrome's stdout/stderr.
  VLC_PPAPI_STATIC_STR(format_error_msg_var, "error while formating console message!");

  const vlc_ppapi_console_t* iconsole = vlc_getPPAPI_Console();

  // find out how much buffer we need
  const int written = vsnprintf(NULL, 0, format, args);
  if(written < 0) {
    PP_Var msg = vlc_ppapi_mk_str(&format_error_msg_var);
    iconsole->LogWithSource(instance, PP_LOGLEVEL_ERROR, module, msg);
    return;
  }

  char* buffer = alloca(written + 1);
  // the real format:
  vsnprintf(buffer, written + 1, format, args);

  PP_Var msg = vlc_ppapi_cstr_to_var(buffer, written);

  iconsole->LogWithSource(instance, level, module, msg);
  vlc_ppapi_deref_var(msg);
}

void* vlc_ppapi_array_output_get_buffer(void* user_data, uint32_t count, uint32_t size) {
  vlc_ppapi_array_output_t* output = (vlc_ppapi_array_output_t*)user_data;
  output->elements = count;
  if(size != 0) {
    output->data = malloc(count * size);
  } else {
    output->data = NULL;
  }
  return output->data;
}

PP_Resource vlc_ppapi_get_temp_fs(PP_Instance instance) {
  static atomic_uintptr_t temp_fs = ATOMIC_VAR_INIT(0);

  if(temp_fs == 0) {
    PP_Resource fs = atomic_load(&temp_fs);
    if(fs != 0) {
      return fs;
    }

    const vlc_ppapi_file_system_t* ifs = vlc_getPPAPI_FileSystem();
    fs = ifs->Create(instance, PP_FILESYSTEMTYPE_LOCALTEMPORARY);
    if(ifs->Open(fs, 0, PP_BlockUntilComplete()) != PP_OK) {
      vlc_subResReference(fs);
      fs = 0;
    }
    uintptr_t expected = 0;
    if(fs == 0) {
      return fs;
    } else if(!atomic_compare_exchange_strong(&temp_fs, &expected, (uintptr_t)fs)) {
      // another thread beat us
      vlc_subResReference(fs);
      return atomic_load(&temp_fs);
    }
  }

  return temp_fs;
}

// set from vlc_did_create in bin/ppapi.c so a few modules which are initialized
// in libvlc_new can get the instance handle. Only valid during vlc_did_create.
// Volatile so it won't be optimized away.
static volatile PP_Instance g_instance_handle = 0;
PP_Instance vlc_getPPAPI_InitializingInstance(void) {
  return g_instance_handle;
}
void vlc_setPPAPI_InitializingInstance(PP_Instance instance) {
  g_instance_handle = instance;
}


typedef struct instance_data_t {
  PP_Instance instance;

  //atomic_uint8_t refs;

  vlc_rwlock_t* mtx;
  PP_Resource viewport;

  int log_level;

  bool focus;
} instance_data_t;

static vlc_rwlock_t g_instance_data_mtx = VLC_STATIC_RWLOCK;
static DECL_ARRAY(instance_data_t) g_instance_data = { 0, 0, NULL };

/*static instance_data_t add_id_ref(instance_data_t* inst) {
  uint8_t prev = atomic_fetch_add(&inst->refs, 1);
  VLC_UNUSED(prev);
  assert(prev != 0)
}
static void sub_id_ref(instance_data_t inst) {
  vlc_rwlock_destroy(item.mtx);
  free(item.mtx);
  vlc_subResReference(item.viewport);
  }*/

int32_t vlc_PPAPI_InitializeInstance(PP_Instance instance) {
  assert(instance != 0);

  vlc_rwlock_t* mtx = malloc(sizeof(vlc_rwlock_t));
  if(mtx == NULL) { return VLC_ENOMEM; }
  vlc_rwlock_init(mtx);

  instance_data_t data = {
    instance,
    mtx,
    0,
    3, // LIBVLC_WARNING
    true,
  };

  vlc_rwlock_wrlock(&g_instance_data_mtx);
  ARRAY_APPEND(g_instance_data, data);
  vlc_rwlock_unlock(&g_instance_data_mtx);

  return VLC_SUCCESS;
}

void vlc_PPAPI_DeinitializeInstance(PP_Instance instance) {
  assert(instance != 0);
  vlc_rwlock_wrlock(&g_instance_data_mtx);
  for(size_t i = 0; i < (size_t)g_instance_data.i_size; i++) {
    instance_data_t item = ARRAY_VAL(g_instance_data, i);
    if(item.instance != instance) {
      continue;
    }

    ARRAY_REMOVE(g_instance_data, i);
    vlc_rwlock_unlock(&g_instance_data_mtx);

    vlc_rwlock_wrlock(item.mtx);
    vlc_subResReference(item.viewport);
    vlc_rwlock_unlock(item.mtx);
    vlc_rwlock_destroy(item.mtx);
    free(item.mtx);

    return;
  }
  vlc_rwlock_unlock(&g_instance_data_mtx);
}

// this function takes ownership of viewport.
void vlc_setPPAPI_InstanceViewport(PP_Instance instance, PP_Resource viewport) {
  assert(instance != 0);
  vlc_rwlock_rdlock(&g_instance_data_mtx);
  for(size_t i = 0; i < (size_t)g_instance_data.i_size; i++) {
    instance_data_t* item = &ARRAY_VAL(g_instance_data, i);
    if(item->instance != instance) {
      continue;
    }

    vlc_rwlock_wrlock(item->mtx);
    vlc_subResReference(item->viewport);
    item->viewport = vlc_addResReference(viewport);
    vlc_rwlock_unlock(item->mtx);
    break;
  }
  vlc_rwlock_unlock(&g_instance_data_mtx);
}
PP_Resource vlc_getPPAPI_InstanceViewport(PP_Instance instance) {
  assert(instance != 0);
  vlc_rwlock_rdlock(&g_instance_data_mtx);
  PP_Resource viewport = 0;
  for(size_t i = 0; i < (size_t)g_instance_data.i_size; i++) {
    instance_data_t* item = &ARRAY_VAL(g_instance_data, i);
    if(item->instance != instance) {
      continue;
    }

    vlc_rwlock_rdlock(item->mtx);
    viewport = vlc_addResReference(item->viewport);
    vlc_rwlock_unlock(item->mtx);
    break;
  }
  vlc_rwlock_unlock(&g_instance_data_mtx);
  return viewport;
}

void vlc_setPPAPI_InstanceLogVerbosity(PP_Instance instance, const int level) {
  assert(instance != 0);
  vlc_rwlock_rdlock(&g_instance_data_mtx);
  for(size_t i = 0; i < (size_t)g_instance_data.i_size; i++) {
    instance_data_t* item = &ARRAY_VAL(g_instance_data, i);
    if(item->instance != instance) {
      continue;
    }

    item->log_level = level;
    break;
  }
  vlc_rwlock_unlock(&g_instance_data_mtx);
}
int vlc_getPPAPI_InstanceLogVerbosity(PP_Instance instance) {
  assert(instance != 0);
  vlc_rwlock_rdlock(&g_instance_data_mtx);
  int result = 0;
  for(size_t i = 0; i < (size_t)g_instance_data.i_size; i++) {
    instance_data_t* item = &ARRAY_VAL(g_instance_data, i);
    if(item->instance != instance) {
      continue;
    }

    result = item->log_level;
    break;
  }
  vlc_rwlock_unlock(&g_instance_data_mtx);
  return result;
}

void vlc_setPPAPI_InstanceFocus(PP_Instance instance, const bool focus) {
  assert(instance != 0);
  vlc_rwlock_rdlock(&g_instance_data_mtx);
  for(size_t i = 0; i < (size_t)g_instance_data.i_size; i++) {
    instance_data_t* item = &ARRAY_VAL(g_instance_data, i);
    if(item->instance != instance) {
      continue;
    }

    item->focus = focus;
    break;
  }
  vlc_rwlock_unlock(&g_instance_data_mtx);
}
bool vlc_getPPAPI_InstanceFocus(PP_Instance instance) {
  assert(instance != 0);
  vlc_rwlock_rdlock(&g_instance_data_mtx);
  bool result = false;
  for(size_t i = 0; i < (size_t)g_instance_data.i_size; i++) {
    instance_data_t* item = &ARRAY_VAL(g_instance_data, i);
    if(item->instance != instance) {
      continue;
    }

    result = item->focus;
    break;
  }
  vlc_rwlock_unlock(&g_instance_data_mtx);
  return result;
}
