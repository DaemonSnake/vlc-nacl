/**
 * @file gl.c
 * @brief PPAPI Context3D manager extension module
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

#define MODULE_NAME ppapi_vout_gl
#define MODULE_STRING "ppapi_vout_gl"

#include <stdint.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_vout_window.h>
#include <vlc_opengl.h>
#include <vlc_ppapi.h>
#include <vlc_plugin.h>

#include <ppapi/gles2/gl2ext_ppapi.h>
#include <ppapi/c/pp_completion_callback.h>

typedef struct vlc_gl_sys_t {
  PP_Instance instance;
  PP_Resource context;

  vlc_thread_t swapper;
  PP_Resource swapper_loop;

  vlc_gl_t* gl;
} vlc_gl_sys_t;

static int MakeCurrent(vlc_gl_t* gl) {
  vlc_gl_sys_t* sys = gl->sys;
  glSetCurrentContextPPAPI(sys->context);
  return VLC_SUCCESS;
}
static void ReleaseCurrent(vlc_gl_t* gl) {
  // Nothing to do... (the context is per thread for us).
  // Must be provided though.
  VLC_UNUSED(gl);
}
static void* SwapperThread(void* sys_) {
  vlc_gl_sys_t* sys = (vlc_gl_sys_t*)sys_;
  const vlc_ppapi_message_loop_t* iloop = vlc_getPPAPI_MessageLoop();
  if(iloop->AttachToCurrentThread(sys->swapper_loop) != PP_OK) {
    vlc_assert_unreachable();
  }

  iloop->Run(sys->swapper_loop);

  vlc_subResReference(sys->swapper_loop);
  vlc_subResReference(sys->context);
  free(sys);
  return NULL;
}

static void SwapperThreadDoSwap(void* sys_, int32_t result) {
  VLC_UNUSED(result);

  vlc_gl_sys_t* sys = (vlc_gl_sys_t*)sys_;

  const vlc_ppapi_graphics_3d_t* g3d = vlc_getPPAPI_Graphics3D();
  g3d->SwapBuffers(sys->context, PP_BlockUntilComplete());
  // Block for crude rate limiting.
  // Ignore any errors. Nothing we can do about them anyway. TODO.
}
static void Swap(vlc_gl_t* gl) {
  vlc_gl_sys_t* sys = gl->sys;

  if(sys->swapper_loop == 0) { return; }

  struct PP_CompletionCallback cb = PP_MakeCompletionCallback(SwapperThreadDoSwap, (void*)sys);

  const vlc_ppapi_message_loop_t* iloop = vlc_getPPAPI_MessageLoop();
  int32_t code;
  if((code = iloop->PostWork(sys->swapper_loop, cb, 0)) != PP_OK) {
    msg_Err(gl, "failed to post work to the swapper thread: `%i`", code);
    vlc_assert_unreachable();
  }
}
static void* GetProcAddress(vlc_gl_t* gl, const char* ext) {
  VLC_UNUSED(gl);
  VLC_UNUSED(ext);
  return NULL;
}
static void Close(vlc_object_t* obj) {
  vlc_gl_t* gl = (vlc_gl_t*)obj;
  vlc_gl_sys_t* sys = gl->sys;

  vlc_thread_t swapper = sys->swapper;

  const vlc_ppapi_message_loop_t* iloop = vlc_getPPAPI_MessageLoop();
  if(sys->swapper_loop != 0 && iloop->PostQuit(sys->swapper_loop, true) != PP_OK) {
    msg_Err(gl, "the swapper thread's message loop failed to PostQuit");
    // clean up sys ourselves.
    vlc_subResReference(sys->context);
    vlc_subResReference(sys->swapper_loop);
    free(sys);
  }

  vlc_join(swapper, NULL);

  gl->sys = NULL;
  gl->makeCurrent = NULL;
  gl->releaseCurrent = NULL;
  gl->swap = NULL;
  gl->getProcAddress = NULL;
}
static int Open(vlc_object_t *obj) {
  vlc_gl_t *gl = (vlc_gl_t*)obj;
  if(gl->surface->type != VOUT_WINDOW_TYPE_PPAPI_G3D ||
     gl->surface->handle.pp_context == 0 ||
     gl->surface->display.pp_instance == 0) {
    return VLC_EGENERIC;
  }

  const PP_Instance instance = gl->surface->display.pp_instance;

  const vlc_ppapi_message_loop_t* iloop = vlc_getPPAPI_MessageLoop();
  const PP_Resource loop = iloop->Create(instance);

  vlc_gl_sys_t* sys = malloc(sizeof(vlc_gl_sys_t));
  if(sys == NULL) {
    return VLC_ENOMEM;
  }

  sys->context = vlc_addResReference(gl->surface->handle.pp_context);
  sys->instance = instance;
  sys->swapper_loop = loop;
  sys->gl = gl;

  if(vlc_clone(&sys->swapper, SwapperThread, sys, VLC_THREAD_PRIORITY_LOW)) {
    vlc_subResReference(sys->context);
    vlc_subResReference(sys->swapper_loop);
    free(sys);
    return VLC_EGENERIC;
  }

  gl->sys = sys;
  gl->makeCurrent = MakeCurrent;
  gl->releaseCurrent = ReleaseCurrent;
  gl->swap = Swap;
  gl->getProcAddress = GetProcAddress;

  return VLC_SUCCESS;
}

vlc_module_begin()
    set_shortname("ppapi-vout-gl")
    set_description("PPAPI interface for OpenGL")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("opengl es2", 50)
    set_callbacks(Open, Close)
vlc_module_end()
