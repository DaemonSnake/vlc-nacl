/**
 * @file context3d.c
 * @brief A video output module for PPAPI using its Graphics3D interface.
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

#define MODULE_NAME ppapi_vout_graphics3d
#define MODULE_STRING "ppapi_vout_graphics3d"

#include <stdlib.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>
#include <vlc_opengl.h>
#include <vlc_ppapi.h>

#define USE_OPENGL_ES 2
#include "../modules/video_output/opengl.h"

static int  PPAPIDisplayOpen (vlc_object_t *);
static void PPAPIDisplayClose (vlc_object_t *);

vlc_module_begin ()
    set_shortname("ppapi-vout-graphics3d")
    set_description("OpenGL ES 2 output via PPAPI.")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("vout display", 1)
    set_callbacks(PPAPIDisplayOpen, PPAPIDisplayClose)

    add_shortcut("ppapi", "opengl", "gles2")
vlc_module_end ()

struct vout_display_sys_t {
  PP_Instance instance;
  // To keep the copy in vout_window_t alive.
  PP_Resource  context3d;

  vlc_gl_t* gl;

  enum PP_MouseCursor_Type cursor_type;
  PP_Resource cursor_image;

  PP_Resource viewport;

  vout_display_opengl_t* vgl;
  picture_pool_t* pool;
};

static picture_pool_t* PPAPIDisplayPool(vout_display_t*, unsigned);
static void PPAPIDisplayPictureRender(vout_display_t*, picture_t*, subpicture_t*);
static void PPAPIDisplayPictureDisplay(vout_display_t*, picture_t*, subpicture_t*);
static int  PPAPIDisplayControl(vout_display_t*, int, va_list);
static void PPAPIDisplayManage(vout_display_t*);

static int PPAPIDisplayOpen(vlc_object_t* obj) {
  vout_display_t* vd = (vout_display_t*)obj;

  PP_Instance instance = 0;
  vlc_value_t val;
  if(var_GetChecked(vd->p_libvlc, "ppapi-instance", VLC_VAR_INTEGER, &val) != VLC_ENOVAR) {
    instance = (PP_Instance)val.i_int;
  }
  if(instance == 0) {
    msg_Err(vd, "couldn't get a reference to the PPAPI instance handle");
    return VLC_EGENERIC;
  }

  const vlc_ppapi_graphics_3d_t* g3d = vlc_getPPAPI_Graphics3D();
  int32_t max_width, max_height;

  int32_t result = PP_TRUE;

  result = g3d->GetAttribMaxValue(instance,
				  PP_GRAPHICS3DATTRIB_WIDTH,
				  &max_width);
  if(result != PP_TRUE) {
    msg_Warn(vd, "couldn't get max supported width: `%i`; defaulting to `%u`",
	     result, vd->fmt.i_width);
    max_width = (int32_t)vd->fmt.i_width;
  }
  result = g3d->GetAttribMaxValue(instance, PP_GRAPHICS3DATTRIB_HEIGHT,
                                  &max_height);
  if(result != PP_TRUE) {
    msg_Warn(vd, "couldn't get max supported height: `%i`; defaulting to `%u`",
	     result, vd->fmt.i_height);
    max_height = (int32_t)vd->fmt.i_height;
  }

  vd->fmt.i_width = __MIN(vd->fmt.i_width, (uint32_t) max_width);
  vd->fmt.i_height = __MIN(vd->fmt.i_height, (uint32_t) max_height);

  int32_t attrs[] = {
    PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 0,
    PP_GRAPHICS3DATTRIB_BLUE_SIZE, 8,
    PP_GRAPHICS3DATTRIB_GREEN_SIZE, 8,
    PP_GRAPHICS3DATTRIB_RED_SIZE, 8,
    PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 0,
    PP_GRAPHICS3DATTRIB_STENCIL_SIZE, 0,
    PP_GRAPHICS3DATTRIB_WIDTH, (int32_t)vd->fmt.i_width,
    PP_GRAPHICS3DATTRIB_HEIGHT, (int32_t)vd->fmt.i_height,
    PP_GRAPHICS3DATTRIB_GPU_PREFERENCE, PP_GRAPHICS3DATTRIB_GPU_PREFERENCE_LOW_POWER,
    PP_GRAPHICS3DATTRIB_NONE
  };

  PP_Resource context = g3d->Create(instance, 0, attrs);
  if(context == 0) {
    msg_Err(vd, "failed to create PPAPI 3d context");
    return VLC_EGENERIC;
  }

  if(vlc_getPPAPI_Instance()->BindGraphics(instance, context) != PP_TRUE) {
    msg_Err(vd, "failed to bind context to instance");
    vlc_subResReference(context);
    return VLC_EGENERIC;
  }

  vout_window_t* surface = vout_display_NewWindow(vd, VOUT_WINDOW_TYPE_PPAPI_G3D);
  if (surface == NULL) {
    msg_Err(vd, "window not available");
    vlc_getPPAPI_Instance()->BindGraphics(instance, 0);
    vlc_subResReference(context);
    return VLC_ENOMEM;
  }
  surface->handle.pp_context = context;
  surface->display.pp_instance = instance;

  vlc_gl_t* gl = vlc_gl_Create(surface, VLC_OPENGL_ES2, "ppapi_vout_gl");
  if (gl == NULL) {
    msg_Err(vd, "couldn't create gl");

    vout_display_DeleteWindow(vd, surface);
    vlc_getPPAPI_Instance()->BindGraphics(instance, 0);
    vlc_subResReference(context);

    return VLC_ENOMEM;
  }

  if (vlc_gl_MakeCurrent(gl)) {
    vlc_gl_Destroy(gl);
    vout_display_DeleteWindow(vd, surface);
    vlc_getPPAPI_Instance()->BindGraphics(instance, 0);
    vlc_subResReference(context);
    return VLC_EGENERIC;
  }

  video_format_t fmt = vd->fmt;
  const vlc_fourcc_t* spu_chromas = NULL;
  vout_display_opengl_t* vgl = vout_display_opengl_New(&fmt, &spu_chromas, gl);
  if (vgl == NULL) {
    vlc_gl_Destroy(gl);
    vout_display_DeleteWindow(vd, surface);
    vlc_getPPAPI_Instance()->BindGraphics(instance, 0);
    vlc_subResReference(context);
    return VLC_ENOMEM;
  }

  vout_display_sys_t* sys = malloc(sizeof(vout_display_sys_t));
  if (sys == NULL) {
    vlc_gl_Destroy(gl);
    vout_display_DeleteWindow(vd, surface);
    vlc_getPPAPI_Instance()->BindGraphics(instance, 0);
    vlc_subResReference(context);
    return VLC_ENOMEM;
  }
  sys->instance = instance;
  sys->context3d = context;
  sys->gl = gl;
  sys->vgl = vgl;
  sys->pool = NULL;

  sys->cursor_type = PP_MOUSECURSOR_TYPE_POINTER;
  sys->cursor_image = 0;

  sys->viewport = 0;

  /* Setup vout_display_t once everything is fine */
  vd->sys = sys;
  vd->info.is_slow = true;
  vd->info.has_pictures_invalid = false;
  vd->info.has_event_thread = false;
  vd->info.subpicture_chromas = spu_chromas;
  vd->pool    = PPAPIDisplayPool;
  vd->prepare = PPAPIDisplayPictureRender;
  vd->display = PPAPIDisplayPictureDisplay;
  vd->control = PPAPIDisplayControl;
  vd->manage  = PPAPIDisplayManage;

  // resize to the current viewport size
  PPAPIDisplayManage(vd);

  return VLC_SUCCESS;
}

static void PPAPIDisplayClose(vlc_object_t* obj) {
  vout_display_t* vd = (vout_display_t*)obj;
  vout_display_sys_t* sys = vd->sys;
  vlc_gl_t* gl = sys->gl;
  vout_window_t* surface = gl->surface;

  vlc_gl_MakeCurrent(gl);
  vout_display_opengl_Delete(sys->vgl);
  vlc_gl_ReleaseCurrent(gl);
  vlc_gl_Destroy(gl);

  vout_display_DeleteWindow(vd, surface);
  vlc_subResReference(sys->context3d);
  free (sys);
}

static picture_pool_t* PPAPIDisplayPool(vout_display_t* vd, unsigned requested_count) {
  vout_display_sys_t* sys = vd->sys;

  if(!sys->pool) {
    vlc_gl_MakeCurrent(sys->gl);
    sys->pool = vout_display_opengl_GetPool(sys->vgl, requested_count);
    vlc_gl_ReleaseCurrent(sys->gl);
  }
  return sys->pool;
}

static void PPAPIDisplayPictureRender(vout_display_t* vd, picture_t* pic, subpicture_t* subpicture) {
  vout_display_sys_t* sys = vd->sys;

  vlc_gl_MakeCurrent(sys->gl);
  vout_display_opengl_Prepare(sys->vgl, pic, subpicture);
  vlc_gl_ReleaseCurrent(sys->gl);
}

static void PPAPIDisplayPictureDisplay(vout_display_t* vd, picture_t* pic, subpicture_t* subpicture) {
  vout_display_sys_t* sys = vd->sys;

  // Don't try to render if we're not visible. Chrome throttles SwapBuffers, so
  // without this, vlc will queue up multiple frames, which will then be
  // released all at once when we come back into view.
  if(vlc_getPPAPI_View()->IsVisible(sys->viewport) != PP_FALSE) {
    vlc_gl_MakeCurrent(sys->gl);
    vout_display_opengl_Display(sys->vgl, &vd->source);
    vlc_gl_ReleaseCurrent(sys->gl);
  }

  picture_Release(pic);
  if (subpicture)
    subpicture_Delete(subpicture);
}

static int PPAPIDisplayControl(vout_display_t* vd, int query, va_list ap) {
  vout_display_sys_t* sys = vd->sys;

  switch (query) {
  case VOUT_DISPLAY_CHANGE_DISPLAY_SIZE:
  case VOUT_DISPLAY_CHANGE_DISPLAY_FILLED:
  case VOUT_DISPLAY_CHANGE_ZOOM:
  case VOUT_DISPLAY_CHANGE_SOURCE_ASPECT:
  case VOUT_DISPLAY_CHANGE_SOURCE_CROP: {
    const vout_display_cfg_t* cfg;
    const video_format_t* source;

    if (query == VOUT_DISPLAY_CHANGE_SOURCE_ASPECT
        || query == VOUT_DISPLAY_CHANGE_SOURCE_CROP) {
      source = (const video_format_t*)va_arg (ap, const video_format_t*);
      cfg = vd->cfg;
    } else {
      source = &vd->source;
      cfg = (const vout_display_cfg_t*)va_arg (ap, const vout_display_cfg_t*);
    }

    vout_display_place_t place;
    vout_display_PlacePicture(&place, source, cfg, false);

    const vlc_ppapi_graphics_3d_t* g3d = vlc_getPPAPI_Graphics3D();

    vlc_gl_MakeCurrent(sys->gl);

    if(g3d->ResizeBuffers(sys->context3d, cfg->display.width, cfg->display.height) != PP_OK) {
      msg_Err(vd, "ResizeBuffers returned an error!");
      return VLC_EGENERIC;
    }

    vlc_gl_Resize (sys->gl, place.width, place.height);
    glViewport(place.x, place.y, place.width, place.height);

    vlc_gl_ReleaseCurrent(sys->gl);

    return VLC_SUCCESS;
  }

    /* Hide the mouse. It will be send when
     * vout_display_t::info.b_hide_mouse is false */
  case VOUT_DISPLAY_HIDE_MOUSE: {
    // TODO mouse input

    return VLC_SUCCESS;

    if(vlc_getPPAPI_MouseCursor()->SetCursor(sys->instance,
                                             PP_MOUSECURSOR_TYPE_NONE,
                                             0,
                                             NULL) == PP_TRUE) {
      return VLC_SUCCESS;
    } else {
      return VLC_EGENERIC;
    }
  }

  case VOUT_DISPLAY_RESET_PICTURES:
    vlc_assert_unreachable();
  default:
    msg_Err(vd, "Unknown request in PPAPI vout display: `%i`", query);
    //case VOUT_DISPLAY_CHANGE_DISPLAY_SIZE:
    return VLC_EGENERIC;
  }
}
static void PPAPIDisplayManage(vout_display_t* vd) {
  vout_display_sys_t *sys = vd->sys;

  PP_Resource deref = 0;
  PP_Resource possibly_new_viewport = vlc_getPPAPI_InstanceViewport(sys->instance);
  if(sys->viewport != possibly_new_viewport) {
    deref = sys->viewport;
    sys->viewport = possibly_new_viewport;
  } else {
    deref = possibly_new_viewport;
    possibly_new_viewport = 0;
  }

  // we only care about the size, so to avoid flickering, check that there
  // actually was a change in the viewport size.
  struct PP_Rect old_viewport = { { 0, 0 }, { 0, 0 } };
  if(possibly_new_viewport != 0 && deref != 0 &&
     vlc_getPPAPI_View()->GetRect(deref, &old_viewport) == PP_FALSE) {
    msg_Err(vd, "failed to get the old viewport size");
    possibly_new_viewport = 0;
  }

  vlc_subResReference(deref);
  if(possibly_new_viewport == 0) { return; }

  // there is a new viewport. get the new dimensions and update the output.
  struct PP_Rect viewport;
  if(vlc_getPPAPI_View()->GetRect(sys->viewport, &viewport) == PP_FALSE) {
    msg_Err(vd, "failed to get the new viewport size");
    vlc_subResReference(sys->viewport);
    sys->viewport = 0;
    return;
  }
  if(old_viewport.size.width == viewport.size.width &&
     old_viewport.size.height == viewport.size.height) {
    return;
  }

  const unsigned width = (unsigned)viewport.size.width;
  const unsigned height = (unsigned)viewport.size.height;
  vout_display_SendEventDisplaySize(vd, width, height);
}
