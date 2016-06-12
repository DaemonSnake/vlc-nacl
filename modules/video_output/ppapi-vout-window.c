/**
 * @file ppapi-vout-window.c
 * @brief A "window" provider for PPAPI
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

#define MODULE_NAME ppapi_vout_window
#define MODULE_STRING "ppapi_vout_window"

#include <stdlib.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_window.h>
#include <vlc_ppapi.h>

static int  PPAPIWindowOpen(vout_window_t*, const vout_window_cfg_t*);
static void PPAPIWindowClose(vout_window_t*);
static int  PPAPIWindowControl(vout_window_t*, int, va_list);

vlc_module_begin ()
    set_shortname("ppapi-vout-window")
    set_description("A PPAPI \"window\".")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("vout window", 1)
    set_callbacks(PPAPIWindowOpen, PPAPIWindowClose)
vlc_module_end ()

struct vout_window_sys_t {
  PP_Instance instance;
};

static int PPAPIWindowOpen(vout_window_t* wnd, const vout_window_cfg_t* cfg) {
  VLC_UNUSED(cfg);
  if(wnd->type != VOUT_WINDOW_TYPE_INVALID &&
     wnd->type != VOUT_WINDOW_TYPE_PPAPI_G3D) {
    return VLC_EGENERIC;
  }

  // we can be openned at any time, so we can't use the global provided in ppapi.c
  PP_Instance instance = 0;
  vlc_value_t val;
  if(var_GetChecked(wnd->p_libvlc, "ppapi-instance", VLC_VAR_INTEGER, &val) != VLC_ENOVAR) {
    instance = (PP_Instance)val.i_int;
  }
  if(instance == 0) {
    msg_Err(wnd, "couldn't get the PPAPI instance handle");
    return VLC_EGENERIC;
  }

  vout_window_sys_t *sys = malloc(sizeof(vout_window_sys_t));
  if (sys == NULL) {
    return VLC_ENOMEM;
  }
  sys->instance = instance;

  wnd->sys = sys;
  wnd->control = PPAPIWindowControl;

  wnd->type = VOUT_WINDOW_TYPE_PPAPI_G3D;
  wnd->handle.pp_context = 0;

  return VLC_SUCCESS;
}

static void PPAPIWindowClose(vout_window_t* wnd) {
  free(wnd->sys);
  wnd->sys = NULL;
}

static int PPAPIWindowControl(vout_window_t* wnd, int query, va_list args) {
  VLC_UNUSED(wnd); VLC_UNUSED(query); VLC_UNUSED(args);
  // TODO fullscreen etc
  return VLC_EGENERIC;
}
