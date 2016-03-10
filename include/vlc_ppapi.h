
#ifndef VLC_PPAPI_H
# define VLC_PPAPI_H 1

#  include <string.h>
#  include <stdbool.h>

#  include <vlc_atomic.h>

#  include <ppapi/c/pp_errors.h>
#  include <ppapi/c/pp_instance.h>
#  include <ppapi/c/pp_array_output.h>

#  include <ppapi/c/ppb.h>
#  include <ppapi/c/ppb_audio.h>
#  include <ppapi/c/ppb_audio_config.h>
#  include <ppapi/c/ppb_core.h>
#  include <ppapi/c/ppb_console.h>
#  include <ppapi/c/ppb_file_io.h>
#  include <ppapi/c/ppb_file_ref.h>
#  include <ppapi/c/ppb_file_system.h>
#  include <ppapi/c/ppb_graphics_3d.h>
#  include <ppapi/c/ppb_instance.h>
#  include <ppapi/c/ppb_mouse_cursor.h>
#  include <ppapi/c/ppb_message_loop.h>
#  include <ppapi/c/ppb_messaging.h>
#  include <ppapi/c/ppb_url_loader.h>
#  include <ppapi/c/ppb_url_request_info.h>
#  include <ppapi/c/ppb_url_response_info.h>
#  include <ppapi/c/ppb_var.h>
#  include <ppapi/c/ppb_var_array.h>
#  include <ppapi/c/ppb_var_dictionary.h>
#  include <ppapi/c/ppb_view.h>

/* typedefs to the versioned types in the PPAPI */
typedef struct PPB_Audio_1_1           vlc_ppapi_audio_t;
typedef struct PPB_AudioConfig_1_1     vlc_ppapi_audio_config_t;
typedef struct PPB_Console_1_0         vlc_ppapi_console_t;
typedef struct PPB_Core_1_0            vlc_ppapi_core_t;
typedef struct PPB_FileIO_1_1          vlc_ppapi_file_io_t;
typedef struct PPB_FileRef_1_2         vlc_ppapi_file_ref_t;
typedef struct PPB_FileSystem_1_0      vlc_ppapi_file_system_t;
typedef struct PPB_Graphics2D_1_1      vlc_ppapi_graphics_2d_t;
typedef struct PPB_Graphics3D_1_0      vlc_ppapi_graphics_3d_t;
typedef struct PPB_ImageData_1_0       vlc_ppapi_image_data_t;
typedef struct PPB_Instance_1_0        vlc_ppapi_instance_t;
typedef struct PPB_MouseCursor_1_0     vlc_ppapi_mouse_cursor_t;
typedef struct PPB_MessageLoop_1_0     vlc_ppapi_message_loop_t;
typedef struct PPB_Messaging_1_2       vlc_ppapi_messaging_t;
typedef struct PPB_URLLoader_1_0       vlc_ppapi_url_loader_t;
typedef struct PPB_URLRequestInfo_1_0  vlc_ppapi_url_request_info_t;
typedef struct PPB_URLResponseInfo_1_0 vlc_ppapi_url_response_info_t;
typedef struct PPB_Var_1_2             vlc_ppapi_var_t;
typedef struct PPB_VarArray_1_0        vlc_ppapi_var_array_t;
typedef struct PPB_VarDictionary_1_0   vlc_ppapi_var_dictionary_t;
typedef struct PPB_View_1_2            vlc_ppapi_view_t;

typedef struct PP_Var PP_Var;

typedef struct vlc_ppapi_array_output_t {
  void* data;
  size_t elements;
} vlc_ppapi_array_output_t;

#  ifdef __cplusplus
extern "C" {
#  endif

  void* vlc_ppapi_array_output_get_buffer(void* user_data, uint32_t count, uint32_t size);
#  define VLC_PPAPI_ARRAY_OUTPUT(name, output) vlc_ppapi_array_output_t name; \
  PP_ArrayOutput output = { &vlc_ppapi_array_output_get_buffer, &name }

  PP_Instance vlc_getPPAPI_InitializingInstance(void);
  void vlc_setPPAPI_InitializingInstance(PP_Instance);

  PPB_GetInterface vlc_getPPBGetInstance(void);

  const vlc_ppapi_audio_t*          vlc_getPPAPI_Audio(void);
  const vlc_ppapi_audio_config_t*   vlc_getPPAPI_AudioConfig(void);
  const vlc_ppapi_console_t*        vlc_getPPAPI_Console(void);
  const vlc_ppapi_core_t*           vlc_getPPAPI_Core(void);
  const vlc_ppapi_file_io_t*        vlc_getPPAPI_FileIO(void);
  const vlc_ppapi_file_ref_t*       vlc_getPPAPI_FileRef(void);
  const vlc_ppapi_file_system_t*    vlc_getPPAPI_FileSystem(void);
  const vlc_ppapi_graphics_2d_t*    vlc_getPPAPI_Graphics2D(void);
  const vlc_ppapi_graphics_3d_t*    vlc_getPPAPI_Graphics3D(void);
  const vlc_ppapi_image_data_t*     vlc_getPPAPI_ImageData(void);
  const vlc_ppapi_instance_t*       vlc_getPPAPI_Instance(void);
  const vlc_ppapi_mouse_cursor_t*   vlc_getPPAPI_MouseCursor(void);
  const vlc_ppapi_message_loop_t*   vlc_getPPAPI_MessageLoop(void);
  const vlc_ppapi_messaging_t*      vlc_getPPAPI_Messaging(void);
  const vlc_ppapi_url_loader_t*     vlc_getPPAPI_URLLoader(void);
  const vlc_ppapi_url_request_info_t* vlc_getPPAPI_URLRequestInfo(void);
  const vlc_ppapi_url_response_info_t* vlc_getPPAPI_URLResponseInfo(void);
  const vlc_ppapi_var_t*            vlc_getPPAPI_Var(void);
  const vlc_ppapi_var_array_t*      vlc_getPPAPI_VarArray(void);
  const vlc_ppapi_var_dictionary_t* vlc_getPPAPI_VarDictionary(void);
  const vlc_ppapi_view_t*           vlc_getPPAPI_View(void);

  // Initializes or deinitializes module global per-instance data. Both of these
  // should only be called from bin/ppapi.c.
  int32_t vlc_PPAPI_InitializeInstance(PP_Instance instance);
  void vlc_PPAPI_DeinitializeInstance(PP_Instance instance);

  void vlc_setPPAPI_InstanceLogVerbosity(PP_Instance instance, const int level);
  int vlc_getPPAPI_InstanceLogVerbosity(PP_Instance instance);

  // Set the instance's viewport resource. This should really *only* be called
  // from ppapi.c in bin/.
  void vlc_setPPAPI_InstanceViewport(PP_Instance instance, PP_Resource viewport);
  // Gets the viewport resource associated with the instance. If the returned
  // value != 0, the resource will be add-ref-ed for you.
  PP_Resource vlc_getPPAPI_InstanceViewport(PP_Instance instance);

  void vlc_setPPAPI_InstanceFocus(PP_Instance instance, const bool focus);
  bool vlc_getPPAPI_InstanceFocus(PP_Instance instance);

  typedef struct vlc_ppapi_static_str_t {
#ifdef __cplusplus
    std::atomic_uintptr_t var; // ugh
#else
    atomic_uintptr_t var;
#endif
    const char* const init;
    const size_t len;
  } vlc_ppapi_static_str_t;

#  define VLC_PPAPI_STATIC_STR_INIT(value)              \
  { ATOMIC_VAR_INIT(0), value, sizeof(value) - 1 }
#  define VLC_PPAPI_STATIC_STR(name, value)             \
  static vlc_ppapi_static_str_t name =                  \
    VLC_PPAPI_STATIC_STR_INIT(value)

  // The returned PP_Var does *not* need to be dereferenced after use.
  PP_Var vlc_ppapi_mk_str_var(
#ifdef __cplusplus
                              std::atomic_uintptr_t* var,
#else
                              atomic_uintptr_t* var,
#endif
                              const char* const init, const size_t len);
  static inline struct PP_Var vlc_ppapi_mk_str(vlc_ppapi_static_str_t* global) {
    return vlc_ppapi_mk_str_var(&global->var, global->init, global->len);
  }

  static inline int vlc_ppapi_var_strcmp(const struct PP_Var lhs, const char* rhs) {
    if(lhs.type != PP_VARTYPE_STRING) { return false; }

    const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();
    uint32_t len;
    const char* str = ivar->VarToUtf8(lhs, &len);
    return strncmp(str, rhs, (size_t)len);
  }

  static inline struct PP_Var vlc_ppapi_cstr_to_var(const char* str, const size_t len) {
    if(str == NULL) {
      return PP_MakeNull();
    } else {
      return vlc_getPPAPI_Var()->VarFromUtf8(str, len);
    }
  }
  static inline char* vlc_ppapi_null_terminate(const char* src, const size_t len) {
    char* dest = (char*)malloc(len + 1);
    if(dest == NULL) { return NULL; }

    memmove(dest, src, len);
    dest[len] = '\0';
    return dest;
  }

  void vlc_ppapi_log_va(const PP_Instance instance, const PP_LogLevel level,
                        PP_Var module, const char* format, va_list args);

  static inline void vlc_ppapi_log(const PP_Instance instance, const PP_LogLevel level,
                                   PP_Var module, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlc_ppapi_log_va(instance, level, module, format, args);
    va_end(args);
  }
#  define VLC_PPAPI_MODULE_NAME(name_string)    \
  VLC_PPAPI_STATIC_STR(___SOURCE, name_string);

#  define vlc_ppapi_log_tip(instance, ...)      \
  vlc_ppapi_log(instance, PP_LOGLEVEL_TIP,      \
                vlc_ppapi_mk_str(&___SOURCE),   \
                __VA_ARGS__)
#  define vlc_ppapi_log_log(instance, ...)      \
  vlc_ppapi_log(instance, PP_LOGLEVEL_LOG,      \
                vlc_ppapi_mk_str(&___SOURCE),   \
                __VA_ARGS__)
#  define vlc_ppapi_log_warning(instance, ...)  \
  vlc_ppapi_log(instance, PP_LOGLEVEL_WARNING,  \
                vlc_ppapi_mk_str(&___SOURCE),   \
                __VA_ARGS__)
#  define vlc_ppapi_log_error(instance, ...)    \
  vlc_ppapi_log(instance, PP_LOGLEVEL_ERROR,    \
                vlc_ppapi_mk_str(&___SOURCE),   \
                __VA_ARGS__)

  static inline void vlc_ppapi_deref_var(struct PP_Var v) {
    const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();
    ivar->Release(v);
  }

  static inline PP_Resource vlc_addResReference(PP_Resource res) {
    const vlc_ppapi_core_t* core = vlc_getPPAPI_Core();
    core->AddRefResource(res);
    return res;
  }
  static inline void vlc_subResReference(PP_Resource res) {
    if(res == 0) { return; }
    const vlc_ppapi_core_t* core = vlc_getPPAPI_Core();
    core->ReleaseResource(res);
  }

  PP_Resource vlc_ppapi_get_temp_fs(PP_Instance instance);

#  ifdef __cplusplus
}
#  endif

#endif
