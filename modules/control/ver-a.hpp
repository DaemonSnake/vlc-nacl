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

namespace ver_a {
  namespace playlist {
  }

  namespace control {
#define PARENT_LOCATION "control"
    static js_static_method_decl_t* g_handle_play =
      add_static_method(JS_STATIC_METHOD_LOC("play"),
                        JS_API_VER_A,
                        JS_API_VER_B,
                        JSV_MATCH_UNDEFINED(),
                        ver_b::playlist::handle_play);


    static js_static_method_decl_t* g_handle_pause =
      add_static_method(JS_STATIC_METHOD_LOC("pause"),
                        JS_API_VER_A,
                        JS_API_VER_B,
                        JSV_MATCH_UNDEFINED(),
                        ver_b::playlist::handle_pause);

    static js_static_method_decl_t* g_handle_stop =
      add_static_method(JS_STATIC_METHOD_LOC("stop"),
                        JS_API_VER_A,
                        JS_API_VER_B,
                        JSV_MATCH_UNDEFINED(),
                        ver_b::playlist::handle_stop);

    static js_static_method_decl_t* g_handle_next_frame =
      add_static_method(JS_STATIC_METHOD_LOC("next"),
                        JS_API_VER_A,
                        JS_API_VER_B,
                        JSV_MATCH_UNDEFINED(),
                        ver_b::playlist::handle_next);
    static js_static_method_decl_t* g_handle_prev_frame =
      add_static_method(JS_STATIC_METHOD_LOC("prev"),
                        JS_API_VER_A,
                        JS_API_VER_B,
                        JSV_MATCH_UNDEFINED(),
                        ver_b::playlist::handle_prev);

    static const js_static_property_decl_t g_rate =
      add_static_property(JS_STATIC_PROPERTY_LOC("rate"),
                          JS_API_VER_A,
                          JS_API_VER_B,
                          JSV_MATCH_INT32(),
                          ver_b::input::get_rate,
                          ver_b::input::set_rate);
    static const js_static_property_decl_t g_item =
      add_static_property(JS_STATIC_PROPERTY_LOC("item"),
                          JS_API_VER_A,
                          JS_API_VER_B,
                          JSV_MATCH_INT32(),
                          ver_b::input::get_item,
                          ver_b::input::set_item);
    static const js_static_property_decl_t g_status =
      add_static_property(JS_STATIC_PROPERTY_LOC("status"),
                          JS_API_VER_A,
                          JS_API_VER_B,
                          JSV_MATCH_UNDEFINED(),
                          ver_b::playlist::get_status,
                          nullptr);
    static const js_static_property_decl_t g_length =
      add_static_property(JS_STATIC_PROPERTY_LOC("length"),
                          JS_API_VER_A,
                          JS_API_VER_B,
                          JSV_MATCH_UNDEFINED(),
                          ver_b::input::get_length,
                          nullptr);

    static const js_static_property_decl_t g_position =
      add_static_property(JS_STATIC_PROPERTY_LOC("position"),
                          JS_API_VER_A,
                          JS_API_VER_B,
                          JSV_MATCH_INT32(),
                          ver_b::input::get_position,
                          ver_b::input::set_position);
    static const js_static_property_decl_t g_time =
      add_static_property(JS_STATIC_PROPERTY_LOC("time"),
                          JS_API_VER_A,
                          JS_API_VER_B,
                          g_mtime_desc,
                          ver_b::input::get_time,
                          ver_b::input::set_time);
#undef PARENT_LOCATION

    namespace video {
#define PARENT_LOCATION "control/video"
      static js_static_method_decl_t* g_handle_next_frame =
        add_static_method(JS_STATIC_PROPERTY_LOC("next-frame"),
                          JS_API_VER_A,
                          JS_API_VER_B,
                          JSV_MATCH_UNDEFINED(),
                          ver_b::input::video::handle_next_frame);
      static js_static_method_decl_t* g_handle_prev_frame =
        add_static_method(JS_STATIC_PROPERTY_LOC("prev-frame"),
                          JS_API_VER_A,
                          JS_API_VER_B,
                          JSV_MATCH_UNDEFINED(),
                          ver_b::input::video::handle_prev_frame);
#undef PARENT_LOCATION
    }
    namespace audio {
#define PARENT_LOCATION "control/audio"
      static const js_static_property_decl_t g_muted =
        add_static_property(JS_STATIC_PROPERTY_LOC("muted"),
                            JS_API_VER_A,
                            JS_API_VER_B,
                            JSV_MATCH_BOOL(),
                            ver_b::playlist::audio::get_muted,
                            ver_b::playlist::audio::set_muted);
      static const js_static_property_decl_t g_volume =
        add_static_property(JS_STATIC_PROPERTY_LOC("volume"),
                            JS_API_VER_A,
                            JS_API_VER_B,
                            JSV_MATCH_INT32(),
                            ver_b::playlist::audio::get_volume,
                            ver_b::playlist::audio::set_volume);
#undef PARENT_LOCATION
    }
  }
}
