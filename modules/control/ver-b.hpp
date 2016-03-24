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

namespace ver_b {
  namespace playlist {

#define PARENT_LOCATION "playlist"

    static void handle_play(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      playlist_Play(pl);
      return_code = JS_SUCCESS;
    }
    static void handle_pause(intf_sys_t* sys,
                             const uint32_t version,
                             const js_static_method_decl_t* decl,
                             const PP_Var arg,
                             int32_t& return_code,
                             PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      playlist_Pause(pl);
      return_code = JS_SUCCESS;
    }
    static void handle_stop(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      playlist_Stop(pl);
      return_code = JS_SUCCESS;
    }

    static void handle_next(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      playlist_Next(pl);
      return_code = JS_SUCCESS;
    }

    static void handle_prev(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      playlist_Prev(pl);
      return_code = JS_SUCCESS;
    }

    static void get_status(intf_sys_t* sys,
                           const uint32_t version,
                           const js_static_method_decl_t* decl,
                           const PP_Var arg,
                           int32_t& return_code,
                           PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);

      playlist_Lock(pl);
      ret = PP_MakeInt32(playlist_Status(pl));
      playlist_Unlock(pl);

      return_code = JS_SUCCESS;
    }

    static void handle_enqueue_multiple(intf_sys_t* sys,
                                        const uint32_t version,
                                        const js_static_method_decl_t* decl,
                                        const PP_Var arg,
                                        int32_t& return_code,
                                        PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      const vlc_ppapi_var_array_t* iarray = vlc_getPPAPI_VarArray();
      const vlc_ppapi_var_t*       ivar = vlc_getPPAPI_Var();
      ret = iarray->Create();
      const uint32_t len = iarray->GetLength(arg);
      iarray->SetLength(ret, len);

      return_code = JS_SUCCESS;

      playlist_t* pl = pl_Get(sys->parent);

      playlist_Lock(pl);
      for(uint32_t i = 0; i < len; i++) {
        const PP_Var mrl_var = iarray->Get(arg, i);
        uint32_t mrl_len;
        const char* mrl_str = ivar->VarToUtf8(mrl_var, &mrl_len);

        const char* mrl_str_null_term = strndup(mrl_str, mrl_len);
        input_item_t* input_item = input_item_New(mrl_str_null_term, mrl_str_null_term);
        const int result = playlist_AddInput(pl, input_item, 0, PLAYLIST_END, true, true);

        if(result != VLC_SUCCESS) {
          iarray->Set(ret, i, PP_MakeInt32(JS_SUCCESS));
        } else {
          iarray->Set(ret, i, PP_MakeInt32(JS_EGENERIC));
        }
      }

      playlist_Unlock(pl);
    }
    static void handle_enqueue_single(intf_sys_t* sys,
                                      const uint32_t version,
                                      const js_static_method_decl_t* decl,
                                      const PP_Var args,
                                      int32_t& return_code,
                                      PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      const vlc_ppapi_var_t*       ivar = vlc_getPPAPI_Var();

      return_code = JS_SUCCESS;

      playlist_t* pl = pl_Get(sys->parent);

      playlist_Lock(pl);

      uint32_t mrl_len;
      const char* mrl_str = ivar->VarToUtf8(args, &mrl_len);

      const char* mrl_str_null_term = strndup(mrl_str, mrl_len);
      input_item_t* input_item = input_item_New(mrl_str_null_term, mrl_str_null_term);
      const int result = playlist_AddInput(pl, input_item, 0, PLAYLIST_END, true, true);

      if(result != VLC_SUCCESS) {
        return_code = 400;
      } else {
        ret = PP_MakeInt32(result);
      }

      playlist_Unlock(pl);
    }
    static void handle_dequeue_multiple(intf_sys_t* sys,
                                        const uint32_t version,
                                        const js_static_method_decl_t* decl,
                                        const PP_Var args,
                                        int32_t& return_code,
                                        PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      const vlc_ppapi_var_array_t* iarray = vlc_getPPAPI_VarArray();
      ret = iarray->Create();
      const uint32_t len = iarray->GetLength(args);
      iarray->SetLength(ret, len);

      playlist_t* pl = pl_Get(sys->parent);

      playlist_Lock(pl);
      for(uint32_t i = 0; i < len; i++) {
        const PP_Var element = iarray->Get(args, i);
        playlist_item_t* item = playlist_ItemGetById(pl, get_var_int32(element));
        if(item == NULL) {
          iarray->Set(ret, i, PP_MakeInt32(JS_EBADREQUEST));
          continue;
        }
        const int result = playlist_NodeDelete(pl, item, true, false);
        if(result != VLC_SUCCESS) {
          iarray->Set(ret, i, PP_MakeInt32(JS_SUCCESS));
        } else {
          iarray->Set(ret, i, PP_MakeInt32(JS_EGENERIC));
        }
      }

      playlist_Unlock(pl);

      return_code = JS_SUCCESS;
    }
    static void handle_dequeue_single(intf_sys_t* sys,
                                      const uint32_t version,
                                      const js_static_method_decl_t* decl,
                                      const PP_Var args,
                                      int32_t& return_code,
                                      PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);

      playlist_Lock(pl);
      playlist_item_t* item = playlist_ItemGetById(pl, get_var_int32(args));
      if(item == NULL || playlist_NodeDelete(pl, item, true, false) != VLC_SUCCESS) {
        return_code = JS_EGENERIC;
      } else {
        return_code = JS_SUCCESS;
      }
      playlist_Unlock(pl);
    }

    static void clear_items(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);

      return_code = JS_SUCCESS;

      playlist_Lock(pl);

      while(pl->items.i_size) {
        playlist_item_t* pli = pl->items.p_elems[0];
        if(playlist_NodeDelete(pl, pli, true, false) != VLC_SUCCESS) {
          return_code = JS_EGENERIC;
          break;
        }
      }

      playlist_Unlock(pl);
    }
    static void get_items(intf_sys_t* sys,
                          const uint32_t version,
                          const js_static_method_decl_t* decl,
                          const PP_Var arg,
                          int32_t& return_code,
                          PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg); VLC_UNUSED(ret);
      const vlc_ppapi_var_array_t* iarray = vlc_getPPAPI_VarArray();
      ret = iarray->Create();

      playlist_t* pl = pl_Get(sys->parent);

      playlist_Lock(pl);

      const int32_t count = pl->items.i_size;
      iarray->SetLength(ret, count);

      for(int32_t i = 0; i < count; i++) {
        playlist_item_t* pli = pl->items.p_elems[i];
        PP_Var v = media_to_var(pli);
        iarray->Set(ret, i, v);
        vlc_ppapi_deref_var(v);
      }
      playlist_Unlock(pl);

      return_code = JS_SUCCESS;
    }

    static void get_looping(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg);

      playlist_t* pl = pl_Get(sys->parent);
      if(var_GetBool(pl, "loop")) {
        ret = PP_MakeBool(PP_TRUE);
      } else {
        ret = PP_MakeBool(PP_FALSE);
      }
      return_code = JS_SUCCESS;
    }
    static void set_looping(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(ret);
      playlist_t* pl = pl_Get(sys->parent);
      var_SetBool(pl, "loop", arg.value.as_bool);
      return_code = JS_SUCCESS;
    }
    static void get_repeating(intf_sys_t* sys,
                              const uint32_t version,
                              const js_static_method_decl_t* decl,
                              const PP_Var arg,
                              int32_t& return_code,
                              PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg);

      playlist_t* pl = pl_Get(sys->parent);
      if(var_GetBool(pl, "repeat")) {
        ret = PP_MakeBool(PP_TRUE);
      } else {
        ret = PP_MakeBool(PP_FALSE);
      }
      return_code = JS_SUCCESS;
    }
    static void set_repeating(intf_sys_t* sys,
                              const uint32_t version,
                              const js_static_method_decl_t* decl,
                              const PP_Var arg,
                              int32_t& return_code,
                              PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(ret);
      playlist_t* pl = pl_Get(sys->parent);
      var_SetBool(pl, "repeat", arg.value.as_bool);
      return_code = JS_SUCCESS;
    }

    static js_static_method_decl_t* g_handle_play =
      add_static_method(JS_STATIC_METHOD_LOC("play"),
                        JS_API_VER_B,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_UNDEFINED(),
                        handle_play);
    static js_static_method_decl_t* g_handle_pause =
      add_static_method(JS_STATIC_METHOD_LOC("pause"),
                        JS_API_VER_B,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_UNDEFINED(),
                        handle_pause);

    static js_static_method_decl_t* g_handle_stop =
      add_static_method(JS_STATIC_METHOD_LOC("stop"),
                        JS_API_VER_B,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_UNDEFINED(),
                        handle_stop);

    static js_static_method_decl_t* g_handle_next_frame =
      add_static_method(JS_STATIC_METHOD_LOC("next"),
                        JS_API_VER_B,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_UNDEFINED(),
                        handle_next);
    static js_static_method_decl_t* g_handle_prev_frame =
      add_static_method(JS_STATIC_METHOD_LOC("prev"),
                        JS_API_VER_B,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_UNDEFINED(),
                        handle_prev);

    static js_static_method_decl_t* g_handle_enqueue_multiple =
      add_static_method(JS_STATIC_METHOD_LOC("enqueue"),
                        JS_API_VER_A,
                        JS_API_CURRENT_VERSION,
                        g_enqueue_desc,
                        handle_enqueue_multiple);
    static js_static_method_decl_t* g_handle_enqueue_single =
      add_static_method(JS_STATIC_METHOD_LOC("enqueue"),
                        JS_API_VER_A,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_ANYURL(),
                        handle_enqueue_single);
    static js_static_method_decl_t* g_handle_dequeue_multiple =
      add_static_method(JS_STATIC_METHOD_LOC("dequeue"),
                        JS_API_VER_A,
                        JS_API_CURRENT_VERSION,
                        g_dequeue_desc,
                        handle_dequeue_multiple);
    static js_static_method_decl_t* g_handle_dequeue_single =
      add_static_method(JS_STATIC_METHOD_LOC("dequeue"),
                        JS_API_VER_A,
                        JS_API_CURRENT_VERSION,
                        g_playlist_item_id_desc,
                        handle_dequeue_single);

    static const js_static_method_decl_t* g_handle_clear =
      add_static_method(JS_STATIC_METHOD_LOC("clear"),
                        JS_API_VER_A,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_UNDEFINED(),
                        clear_items);
    static const js_static_property_decl_t g_items =
      add_static_property(JS_STATIC_PROPERTY_LOC("items"),
                          JS_API_VER_A,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_UNDEFINED(),
                          get_items,
                          nullptr);

    static const js_static_property_decl_t g_looping =
      add_static_property(JS_STATIC_PROPERTY_LOC("looping"),
                          JS_API_VER_A,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_BOOL(),
                          get_looping,
                          set_looping);
    static const js_static_property_decl_t g_repeating =
      add_static_property(JS_STATIC_PROPERTY_LOC("repeating"),
                          JS_API_VER_A,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_BOOL(),
                          get_repeating,
                          set_repeating);
    static const js_static_property_decl_t g_status =
      add_static_property(JS_STATIC_PROPERTY_LOC("status"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_UNDEFINED(),
                          get_status,
                          nullptr);

#undef PARENT_LOCATION

    namespace audio {
#define PARENT_LOCATION "playlist/audio"

      static void get_muted(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
        VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(arg);

        playlist_t* pl = pl_Get(sys->parent);

        const int muted = playlist_MuteGet(pl);
        if(muted == -1) {
          return_code = JS_EFORBIDDEN;
          return;
        } else if(muted == 0) {
          return_code = JS_SUCCESS;
          ret = PP_MakeBool(PP_FALSE);
        } else if(muted == 1) {
          return_code = JS_SUCCESS;
          ret = PP_MakeBool(PP_TRUE);
        }
      }
      static void set_muted(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& ret) {
        VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(ret);

        playlist_t* pl = pl_Get(sys->parent);

        const bool mute = arg.value.as_bool == PP_TRUE ? true : false;
        if(playlist_MuteSet(pl, mute) == VLC_SUCCESS) {
          return_code = JS_SUCCESS;
        } else {
          return_code = JS_EGENERIC;
        }
      }
      static void get_volume(intf_sys_t* sys,
                             const uint32_t version,
                             const js_static_method_decl_t* decl,
                             const PP_Var arg,
                             int32_t& return_code,
                             PP_Var& ret) {
        VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(arg);

        playlist_t* pl = pl_Get(sys->parent);

        const float vol = playlist_VolumeGet(pl);
        if(vol == -1.0f) {
          return_code = JS_EFORBIDDEN;
          return;
        } else {
          return_code = JS_SUCCESS;
          ret = PP_MakeDouble((double)vol);
        }
      }
      static void set_volume(intf_sys_t* sys,
                             const uint32_t version,
                             const js_static_method_decl_t* decl,
                             const PP_Var arg,
                             int32_t& return_code,
                             PP_Var& ret) {
        VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(ret);

        playlist_t* pl = pl_Get(sys->parent);

        const float vol = (float)get_var_double(arg);
        if(playlist_VolumeSet(pl, vol) == VLC_SUCCESS) {
          return_code = JS_SUCCESS;
        } else {
          return_code = JS_EGENERIC;
        }
      }

      static const js_static_property_decl_t g_muted =
        add_static_property(JS_STATIC_PROPERTY_LOC("muted"),
                            JS_API_VER_B,
                            JS_API_CURRENT_VERSION,
                            JSV_MATCH_BOOL(),
                            get_muted,
                            set_muted);
      static const js_static_property_decl_t g_volume =
        add_static_property(JS_STATIC_PROPERTY_LOC("volume"),
                            JS_API_VER_B,
                            JS_API_CURRENT_VERSION,
                            JSV_MATCH_INT32(),
                            get_volume,
                            set_volume);
#undef PARENT_LOCATION
    } // audio
  } // playlist

  namespace input {

#define PARENT_LOCATION "input"

    static void get_position(intf_sys_t* sys,
                             const uint32_t version,
                             const js_static_method_decl_t* decl,
                             const PP_Var arg,
                             int32_t& return_code,
                             PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(arg);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      double position = 0;
      const int result = input_Control(input, INPUT_GET_POSITION, &position);
      if(result != VLC_SUCCESS) {
        return_code = JS_EGENERIC;
      } else {
        ret = PP_MakeDouble(position);
        return_code = JS_SUCCESS;
      }

      vlc_object_release(input);
    }
    static void set_position(intf_sys_t* sys,
                             const uint32_t version,
                             const js_static_method_decl_t* decl,
                             const PP_Var arg,
                             int32_t& return_code,
                             PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      if(input_Control(input, INPUT_SET_POSITION, get_var_double(arg)) == VLC_SUCCESS) {
        return_code = JS_SUCCESS;
      } else {
        return_code = JS_EGENERIC;
      }

      vlc_object_release(input);
    }
    static void get_time(intf_sys_t* sys,
                         const uint32_t version,
                         const js_static_method_decl_t* decl,
                         const PP_Var arg,
                         int32_t& return_code,
                         PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(arg);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      mtime_t time = VLC_TS_INVALID;
      const int result = input_Control(input, INPUT_GET_TIME, &time);
      if(result != VLC_SUCCESS) {
        return_code = JS_EGENERIC;
      } else {
        ret = split_mtime(time);
        return_code = JS_SUCCESS;
      }

      vlc_object_release(input);
    }
    static void set_time(intf_sys_t* sys,
                         const uint32_t version,
                         const js_static_method_decl_t* decl,
                         const PP_Var arg,
                         int32_t& return_code,
                         PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      const mtime_t time = unsplit_mtime(arg);
      if(input_Control(input, INPUT_SET_TIME, time) == VLC_SUCCESS) {
        return_code = JS_SUCCESS;
      } else {
        return_code = JS_EGENERIC;
      }

      vlc_object_release(input);
    }

    static const js_static_property_decl_t g_position =
      add_static_property(JS_STATIC_PROPERTY_LOC("position"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_INT32(),
                          get_position,
                          set_position);
    static const js_static_property_decl_t g_time =
      add_static_property(JS_STATIC_PROPERTY_LOC("time"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          g_mtime_desc,
                          get_time,
                          set_time);

    static void get_item(intf_sys_t* sys,
                         const uint32_t version,
                         const js_static_method_decl_t* decl,
                         const PP_Var arg,
                         int32_t& return_code,
                         PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(arg);

      playlist_t* pl = pl_Get(sys->parent);
      playlist_item_t* item = playlist_CurrentPlayingItem(pl);

      if(item == nullptr) {
        ret = PP_MakeUndefined();
      } else {
        ret = media_to_var(item);
      }

      return_code = JS_SUCCESS;
    }
    static void set_item(intf_sys_t* sys,
                         const uint32_t version,
                         const js_static_method_decl_t* decl,
                         const PP_Var arg,
                         int32_t& return_code,
                         PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      playlist_item_t* current = playlist_CurrentPlayingItem(pl);
      playlist_item_t* set     = playlist_ItemGetById(pl, get_var_int32(arg));

      if(set == nullptr) {
        return_code = JS_EBADREQUEST;
        return;
      }
      return_code = JS_SUCCESS;
      if(current == set) {
        return;
      }

      bool found_current = false;
      bool found_set     = false;
      int dir = 0;
      int offset = 0;

      playlist_Lock(pl);

      for(int32_t i = 0; i < pl->items.i_size; i++) {

        offset += dir;

        if(found_current && found_set) {
          break;
        }

        playlist_item_t* pli = pl->items.p_elems[i];
        if(pli == current) {
          found_current = true;
          if(!found_set) {
            dir = 1;
          }
        } else if(pli == set) {
          found_set = true;
          if(!found_current) {
            dir = -1;
          }
        }
      }

      assert(found_current && found_set);

      playlist_Control(pl, PLAYLIST_SKIP, pl_Locked, offset);

      playlist_Unlock(pl);

    }
    static void get_rate(intf_sys_t* sys,
                         const uint32_t version,
                         const js_static_method_decl_t* decl,
                         const PP_Var arg,
                         int32_t& return_code,
                         PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(arg);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      double rate = 0;
      const int result = input_Control(input, INPUT_GET_RATE, &rate);
      if(result != VLC_SUCCESS) {
        return_code = JS_EGENERIC;
      } else {
        ret = PP_MakeDouble(rate);
        return_code = JS_SUCCESS;
      }

      vlc_object_release(input);
    }
    static void set_rate(intf_sys_t* sys,
                         const uint32_t version,
                         const js_static_method_decl_t* decl,
                         const PP_Var arg,
                         int32_t& return_code,
                         PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      if(input_Control(input, INPUT_SET_RATE, get_var_double(arg)) == VLC_SUCCESS) {
        return_code = JS_SUCCESS;
      } else {
        return_code = JS_EGENERIC;
      }

      vlc_object_release(input);
    }
    static void get_state(intf_sys_t* sys,
                          const uint32_t version,
                          const js_static_method_decl_t* decl,
                          const PP_Var arg,
                          int32_t& return_code,
                          PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);
      VLC_UNUSED(arg);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      int state;
      if(input_Control(input, INPUT_GET_STATE, &state) == VLC_SUCCESS) {
        return_code = JS_SUCCESS;
        ret = PP_MakeInt32(state);
      } else {
        return_code = JS_EGENERIC;
      }

      vlc_object_release(input);
    }
    static void set_state(intf_sys_t* sys,
                          const uint32_t version,
                          const js_static_method_decl_t* decl,
                          const PP_Var arg,
                          int32_t& return_code,
                          PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      if(input_Control(input, INPUT_SET_STATE, arg.value.as_int) == VLC_SUCCESS) {
        return_code = JS_SUCCESS;
      } else {
        return_code = JS_EGENERIC;
      }

      vlc_object_release(input);
    }
    static void get_length(intf_sys_t* sys,
                         const uint32_t version,
                         const js_static_method_decl_t* decl,
                         const PP_Var arg,
                         int32_t& return_code,
                         PP_Var& ret) {
    VLC_UNUSED(version); VLC_UNUSED(decl); VLC_UNUSED(arg);

    playlist_t* pl = pl_Get(sys->parent);
    input_thread_t* input = playlist_CurrentInput(pl);

    if(input == nullptr) {
      return_code = JS_EFORBIDDEN;
      return;
    }

    mtime_t time = 0;
    if(input_Control(input, INPUT_GET_LENGTH, &time) == VLC_SUCCESS) {
      ret = split_mtime(time);
      return_code = JS_SUCCESS;
    } else {
      return_code = JS_EGENERIC;
    }
  }

    static void restart_es(intf_sys_t* sys,
                           const uint32_t version,
                           const js_static_method_decl_t* decl,
                           const PP_Var arg,
                           int32_t& return_code,
                           PP_Var& ret) {
      VLC_UNUSED(version); VLC_UNUSED(decl);  VLC_UNUSED(ret);

      playlist_t* pl = pl_Get(sys->parent);
      input_thread_t* input = playlist_CurrentInput(pl);

      if(input == nullptr) {
        return_code = JS_EFORBIDDEN;
        return;
      }

      const int i_arg = get_var_int32(arg);
      if(input_Control(input, INPUT_RESTART_ES, i_arg) == VLC_SUCCESS) {
        return_code = JS_SUCCESS;
      } else {
        return_code = JS_EGENERIC;
      }

      vlc_object_release(input);
    }


    static const js_static_property_decl_t g_rate =
      add_static_property(JS_STATIC_PROPERTY_LOC("rate"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_INT32(),
                          get_rate,
                          set_rate);
    static const js_static_property_decl_t g_item =
      add_static_property(JS_STATIC_PROPERTY_LOC("item"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_INT32(),
                          get_item, set_item);

    static const js_static_property_decl_t g_state =
      add_static_property(JS_STATIC_PROPERTY_LOC("state"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_INT32(),
                          get_state,
                          set_state);
    static const js_static_property_decl_t g_length =
      add_static_property(JS_STATIC_PROPERTY_LOC("length"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_UNDEFINED(),
                          get_length,
                          nullptr);

    static js_static_method_decl_t* g_handle_restart_es =
      add_static_method(JS_STATIC_METHOD_LOC("restart-es"),
                        JS_API_VER_B,
                        JS_API_CURRENT_VERSION,
                        JSV_MATCH_INT32(),
                        restart_es);

    namespace video {
      static double get_video_fps(input_thread_t* input) {
        double fps = 0.0;
        input_item_t* item = input_GetItem(input);
        vlc_mutex_lock(&item->lock);
        for(size_t i = 0; i < (size_t)item->i_es; ++i) {
          const es_format_t *fmt = item->es[i];

          if(fmt->i_cat == VIDEO_ES && fmt->video.i_frame_rate_base > 0) {
            fps = (double)fmt->video.i_frame_rate
              / (double)fmt->video.i_frame_rate_base;
          }
        }
        vlc_mutex_unlock(&item->lock);

        return fps;
      }
      static void handle_next_frame(intf_sys_t* sys,
                                    const uint32_t version,
                                    const js_static_method_decl_t* decl,
                                    const PP_Var arg,
                                    int32_t& return_code,
                                    PP_Var& ret) {
        VLC_UNUSED(version); VLC_UNUSED(decl);
        VLC_UNUSED(arg); VLC_UNUSED(ret);

        playlist_t* pl = pl_Get(sys->parent);
        input_thread_t* input = playlist_CurrentInput(pl);

        if(input == nullptr) {
          return_code = 403;
          return;
        }
        playlist_Pause(pl);
        var_TriggerCallback(input, "frame-next");

        mtime_t ti;
        input_Control(input, INPUT_GET_TIME, &ti);

        const double fps = get_video_fps(input);

        const double fpms = fps / 1000;
        const int64_t frames = (int64_t)(fpms * ti);

        ret = split_mtime(frames);
        return_code = JS_SUCCESS;

        vlc_object_release(input);
      }
      static void handle_prev_frame(intf_sys_t* sys,
                                    const uint32_t version,
                                    const js_static_method_decl_t* decl,
                                    const PP_Var arg,
                                    int32_t& return_code,
                                    PP_Var& ret) {
        VLC_UNUSED(version); VLC_UNUSED(decl);
        VLC_UNUSED(arg); VLC_UNUSED(ret);

        playlist_t* pl = pl_Get(sys->parent);
        input_thread_t* input = playlist_CurrentInput(pl);

        if(input == nullptr) {
          return_code = 403;
          return;
        }
        // THIS IS EXPENSIVE!
        playlist_Pause(pl);

        mtime_t ti;
        input_Control(input, INPUT_GET_TIME, &ti);

        const double fps = get_video_fps(input);
        const double fpms = fps / 1000;
        const int64_t current_frame = (int64_t)(fpms * ti);

        const int64_t prev_frame = current_frame - 1;
        const int64_t prev_frame_ti = (int64_t)(prev_frame / fpms);
        input_Control(input, INPUT_SET_TIME, prev_frame_ti);

        ret = split_mtime(prev_frame);
        return_code = JS_SUCCESS;

        vlc_object_release(input);
      }
    } // video

#undef PARENT_LOCATION

    namespace video {
#define PARENT_LOCATION "input/video"
      static js_static_method_decl_t* g_handle_next_frame =
        add_static_method(JS_STATIC_METHOD_LOC("next-frame"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_UNDEFINED(),
                          handle_next_frame);
      static js_static_method_decl_t* g_handle_prev_frame =
        add_static_method(JS_STATIC_METHOD_LOC("prev-frame"),
                          JS_API_VER_B,
                          JS_API_CURRENT_VERSION,
                          JSV_MATCH_UNDEFINED(),
                          handle_prev_frame);
#undef PARENT_LOCATION
    }
  } // input
}
