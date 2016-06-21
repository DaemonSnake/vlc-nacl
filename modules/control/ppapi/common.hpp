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

#pragma once

#include <assert.h>

#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <limits>
#include <string>

#include <vlc_common.h>
#include <vlc_ppapi.h>
#include <vlc_interface.h>
#include <vlc/libvlc.h>

typedef struct intf_sys_t {
  PP_Instance instance;
  intf_thread_t* parent = nullptr;

  vlc_thread_t handler_thread_id;
  PP_Resource message_loop;

  vlc_mutex_t init_mutex;
  vlc_cond_t  init_cond;

  vlc_mutex_t event_versions_mutex;
  std::unordered_map<uint32_t, std::unordered_map<std::string, size_t>> event_versions;
} intf_sys_t;

static inline std::string vlc_string_var_to_str(PP_Var v) {
  assert(v.type == PP_VARTYPE_STRING);
  uint32_t len = 0;
  auto* buf = vlc_getPPAPI_Var()->VarToUtf8(v, &len);
  return std::move(std::string(buf, len));
}

#define JS_API_VER_A 0
#define JS_API_VER_B 1

#define JS_API_FIRST_VERSION JS_API_VER_A

// Incrememt this if you change *ANY* part of the API, and only increment it by
// one.
#define JS_API_CURRENT_VERSION JS_API_VER_B

#define JS_SUCCESS 200
#define JS_EBADREQUEST 400
#define JS_EFORBIDDEN 403
#define JS_ENOTFOUND 404
#define JS_EGENERIC 500

#define JS_STATIC_PROPERTY_SET(string) string ".set()"
#define JS_STATIC_PROPERTY_GET(string) string ".get()"

#define JS_STATIC_METHOD_LOC(string) "/" PARENT_LOCATION "/" string "()"
#define JS_STATIC_PROPERTY_LOC(string) "/" PARENT_LOCATION "/" string

typedef struct js_object_desc_t        js_object_desc_t;
typedef struct js_static_method_decl_t js_static_method_decl_t;
typedef struct js_var_desc_t           js_var_desc_t;

// I could have done this with templates, but this code didn't start in a CXX file.

enum class VarType : typename std::underlying_type<PP_VarType>::type {
  Undefined = PP_VARTYPE_UNDEFINED,
  Null = PP_VARTYPE_NULL,
  Bool = PP_VARTYPE_BOOL,
  Int32 = PP_VARTYPE_INT32,
  Double = PP_VARTYPE_DOUBLE,
  String = PP_VARTYPE_STRING,
  Object = PP_VARTYPE_OBJECT,
  Array = PP_VARTYPE_ARRAY,
  Dictionary = PP_VARTYPE_DICTIONARY,
  ArrayBuffer = PP_VARTYPE_ARRAY_BUFFER,
  Resource = PP_VARTYPE_RESOURCE,

  Any = std::numeric_limits<typename std::underlying_type<PP_VarType>::type>::max() - 1,
  UInt32 = Any - 1,
};

typedef struct js_var_desc_t {
  bool nullable;
  VarType type;

  // The following lets you match on a var's value.
  union {
    const js_object_desc_t* dictionary_type;  // can be null.

    const char* string_value;           // can be null.

    bool double_allowed; // if .type == PP_VARTYPE_INT32, do we allow double type too?
    // if double_allowed == true, a var's value must be within the range of possible values of a int32_t.
    bool int_allowed;

    // if type is PP_VARTYPE_ARRAY:
    struct {
      size_t array_min_len;              // set to 0 if you don't care.
      size_t array_max_len;              // set to -1 if you don't care.
      const js_var_desc_t* array_element_type; // can be null.
    } array;

    // valid if only and only if var_type is a resource type. can be null.
    PP_Bool (*is_correct_res_type)(PP_Resource);
  };

  bool check(const PP_Var obj, const bool deref) const;
} js_var_desc_t;

// These were originally `#define`s, but unfortunately the preprocessor is buggy. Sigh.
// Fortunately, because of CXX's `constexpr`, these should always be evaluated at compile-time.
static constexpr js_var_desc_t JSV_MATCH_ANY() {
  return { false, VarType::Any, { nullptr }, };
}
static constexpr js_var_desc_t JSV_MATCH_UNDEFINED(const bool nullable_ = false) {
  return { nullable_, (VarType)PP_VARTYPE_UNDEFINED, { NULL }, };
}
static constexpr js_var_desc_t JSV_MATCH_BOOL(const bool nullable_ = false) {
  return { nullable_, (VarType)PP_VARTYPE_BOOL, { NULL }, };
}
static constexpr js_var_desc_t JSV_MATCH_INT32(const bool nullable_ = false,
                                               const bool double_allowed_ = true) {
  return { nullable_, (VarType)PP_VARTYPE_INT32, { .double_allowed = double_allowed_ }, };
}
static constexpr js_var_desc_t JSV_MATCH_DOUBLE(const bool nullable_ = false, const bool int_allowed = true) {
  return { nullable_, VarType::Double, { .int_allowed = int_allowed }, };
}
static constexpr js_var_desc_t JSV_MATCH_STRING(const char* string_, const bool nullable_ = false) {
  return {
    nullable_,
    (VarType)PP_VARTYPE_STRING,
    { .string_value = string_ },
  };
}
static constexpr js_var_desc_t JSV_MATCH_ARRAY(const js_var_desc_t* inner_, const bool nullable_ = false,
                                               const size_t min_ = 0, const size_t max_ = (size_t)(-1)) {
  return {
    nullable_,
    (VarType)PP_VARTYPE_ARRAY,
    {
      .array = { (min_), (max_), (inner_) }
    }
  };
}
static constexpr js_var_desc_t JSV_MATCH_DICTIONARY(const js_object_desc_t* dict_,
                                                    const bool nullable_ = false) {
  return { nullable_, (VarType)PP_VARTYPE_DICTIONARY, { dict_ }, };
}

typedef struct js_object_member_desc_t {
  vlc_ppapi_static_str_t* key;
  js_var_desc_t desc;

  // can the key/value pair be missing?
  bool possibly_undefined;
} js_object_member_desc_t;

struct js_object_desc_t {
  const char* type;
  js_var_desc_t subtype;

  const size_t member_count;
  const js_object_member_desc_t* members;

  enum class Match {
    No, Partial, Yes,
  };

  Match check(const PP_Var obj, const bool deref) const;
};

static constexpr js_object_member_desc_t JSV_OBJECT_MEMBER_DESC(vlc_ppapi_static_str_t* name,
                                                                const js_var_desc_t desc,
                                                                const bool allow_undefined = false) {
  return { name, desc, allow_undefined };
}

#define JSV_OBJECT_TYPE_DESC(type, subtype, ...)                        \
  constexpr js_object_member_desc_t g__##type##_members[] = {           \
    __VA_ARGS__                                                         \
  };                                                                    \
  constexpr js_object_desc_t g_##type##_js_object_desc = {              \
    #type, subtype,                                                     \
                                                                        \
    (sizeof(g__##type##_members) / sizeof(g__##type##_members[0])),     \
    g__##type##_members                                                 \
  }

#define JSV_OBJECT_TYPE_DESC_REF(type) (&g_##type##_js_object_desc)

static constexpr js_var_desc_t JSV_MATCH_ANYURL(const bool nullable_ = false) {
  return JSV_MATCH_STRING(NULL, nullable_);
}

constexpr js_var_desc_t g_url_var_desc = JSV_MATCH_ANYURL();
constexpr js_var_desc_t g_enqueue_desc = JSV_MATCH_ARRAY(&g_url_var_desc);
constexpr js_var_desc_t g_playlist_item_id_desc = JSV_MATCH_INT32();
constexpr js_var_desc_t g_dequeue_desc = JSV_MATCH_ARRAY(&g_playlist_item_id_desc);
constexpr js_var_desc_t g_mtime_piece_desc = JSV_MATCH_INT32(false, true);
constexpr js_var_desc_t g_mtime_desc = JSV_MATCH_ARRAY(&g_mtime_piece_desc, 2, 2);

VLC_PPAPI_STATIC_STR(g_request_id_key, "request_id");
VLC_PPAPI_STATIC_STR(g_return_code_key, "return_code");
VLC_PPAPI_STATIC_STR(g_return_value_key, "return_value");
VLC_PPAPI_STATIC_STR(g_location_key, "location");
VLC_PPAPI_STATIC_STR(g_api_version_key, "version");
VLC_PPAPI_STATIC_STR(g_arguments_key, "args");

VLC_PPAPI_STATIC_STR(package_major, "package_major");
VLC_PPAPI_STATIC_STR(package_minor, "package_minor");
VLC_PPAPI_STATIC_STR(package_extra, "package_extra");

#include <config.h>

static struct {
  vlc_ppapi_static_str_t key;
  vlc_ppapi_static_str_t value;
} version_strings[] = {
  { VLC_PPAPI_STATIC_STR_INIT("package"), VLC_PPAPI_STATIC_STR_INIT(PACKAGE_NAME) },
  { VLC_PPAPI_STATIC_STR_INIT("version"), VLC_PPAPI_STATIC_STR_INIT(VERSION) },
  { VLC_PPAPI_STATIC_STR_INIT("version_message"), VLC_PPAPI_STATIC_STR_INIT(VERSION_MESSAGE) },
  { VLC_PPAPI_STATIC_STR_INIT("compiled_by"), VLC_PPAPI_STATIC_STR_INIT(VLC_COMPILE_BY) },
  { VLC_PPAPI_STATIC_STR_INIT("compile_host"), VLC_PPAPI_STATIC_STR_INIT(VLC_COMPILE_HOST) },
  { VLC_PPAPI_STATIC_STR_INIT("package_url"), VLC_PPAPI_STATIC_STR_INIT(PACKAGE_URL) },
  { VLC_PPAPI_STATIC_STR_INIT("package_bugreport"), VLC_PPAPI_STATIC_STR_INIT(PACKAGE_BUGREPORT) },
  { VLC_PPAPI_STATIC_STR_INIT("copyright_message"), VLC_PPAPI_STATIC_STR_INIT(COPYRIGHT_MESSAGE) },
  { VLC_PPAPI_STATIC_STR_INIT("copyright_years"), VLC_PPAPI_STATIC_STR_INIT(COPYRIGHT_YEARS) },
  { VLC_PPAPI_STATIC_STR_INIT("license_msg"), VLC_PPAPI_STATIC_STR_INIT(LICENSE_MSG), },
  { VLC_PPAPI_STATIC_STR_INIT("changeset"), VLC_PPAPI_STATIC_STR_INIT(libvlc_get_changeset()), },
};

JSV_OBJECT_TYPE_DESC(request, JSV_MATCH_UNDEFINED(),
                     JSV_OBJECT_MEMBER_DESC(&g_location_key, JSV_MATCH_STRING(NULL, false)),
                     JSV_OBJECT_MEMBER_DESC(&g_api_version_key, JSV_MATCH_INT32(false)),
                     JSV_OBJECT_MEMBER_DESC(&g_request_id_key, JSV_MATCH_INT32(false)),
                     JSV_OBJECT_MEMBER_DESC(&g_arguments_key, JSV_MATCH_ANY()));

VLC_PPAPI_STATIC_STR(type_key, "type");
VLC_PPAPI_STATIC_STR(subtype_key, "subtype");

js_object_desc_t::Match js_object_desc_t::check(const PP_Var obj, const bool deref) const {
  const js_object_desc_t* desc = this;

  Match result = Match::No;
  do {
    PP_Var keys = PP_MakeUndefined();
    if(obj.type != PP_VARTYPE_DICTIONARY) {
      result = Match::No;
      break;
    }

    const vlc_ppapi_var_dictionary_t* idict = vlc_getPPAPI_VarDictionary();
    const vlc_ppapi_var_array_t* iarray = vlc_getPPAPI_VarArray();

    js_var_desc_t type_desc = {
      .nullable = false,
      .type = VarType::String,
      .string_value = desc->type,
    };

    // check the required fields
    PP_Var type_k = vlc_ppapi_mk_str(&type_key);
    if(!type_desc.check(idict->Get(obj, type_k), true)) {
      result = Match::No;
      break;
    }

    PP_Var subtype_k = vlc_ppapi_mk_str(&subtype_key);
    if(!desc->subtype.check(idict->Get(obj, subtype_k), true)) {
      result = Match::No;
      break;
    }

    if(desc->members == NULL) {
      assert(desc->member_count == 0);
      result = Match::Yes;
      break;
    } else {
      assert(desc->member_count != 0);
    }

    const size_t extra_keys = idict->HasKey(obj, subtype_k) == PP_TRUE ? 2 : 1;

    keys = idict->GetKeys(obj);
    size_t expected_size = extra_keys;
    for(size_t i = 0; i < desc->member_count; i++) {
      js_object_member_desc_t member = desc->members[i];
      PP_Var key = vlc_ppapi_mk_str(member.key);

      const bool key_present = idict->HasKey(obj, key);

      if(member.possibly_undefined || !key_present) {
        result = Match::Partial;
        break;
      }

      PP_Var value = idict->Get(obj, key);
      if(!member.desc.check(value, true)) {
        result = Match::Partial;
        break;
      }

      if(key_present == PP_TRUE) {
        expected_size += 1;
      }
    }

    // check if there are any extra keys that `desc` did not reference
    const size_t key_len = iarray->GetLength(keys);
    if(key_len != expected_size) {
      // TODO msg_Warn errors.
      printf("key_len != expected_size (%u != %u)\n",
             key_len, expected_size);
      result = Match::Partial;
      break;
    }

    result = Match::Yes;
  } while(false);

  if(deref) { vlc_ppapi_deref_var(obj); }
  return result;
}

bool js_var_desc_t::check(const PP_Var obj, const bool deref) const {
  const js_var_desc_t* desc = this;
  bool result;

  if(desc->type == VarType::Any) {
    result = true;
    goto done;
  }

  if(desc->nullable && (obj.type == PP_VARTYPE_NULL ||
                        obj.type == PP_VARTYPE_UNDEFINED)) {
    result = true;
    goto done;
  } else if(obj.type == PP_VARTYPE_DOUBLE && desc->type == VarType::Int32 &&
            desc->double_allowed && INT_MIN <= obj.value.as_double &&
            INT_MAX >= obj.value.as_double) {
    result = true;
    goto done;
  } else if((obj.type == PP_VARTYPE_DOUBLE && desc->type == VarType::UInt32 &&
            desc->double_allowed && 0 <= obj.value.as_double &&
            UINT_MAX >= obj.value.as_double) ||
            (obj.type == PP_VARTYPE_INT32 && desc->type == VarType::UInt32 &&
             obj.value.as_int >= 0)) {
    result = true;
    goto done;
  } else if(obj.type == PP_VARTYPE_INT32 && desc->type == VarType::Double &&
            desc->int_allowed) {
    result = true;
    goto done;
  } else if((VarType)obj.type != desc->type) {
    result = false;
    goto done;
  } else if(obj.type == PP_VARTYPE_STRING) {
    if(desc->string_value == NULL) {
      result = true;
      goto done;
    }

    result = (vlc_ppapi_var_strcmp(obj, desc->string_value) == 0);
    goto done;
  } else if(obj.type == PP_VARTYPE_DICTIONARY) {
    if(desc->dictionary_type == NULL) {
      result = true;
      goto done;
    }
    result = (desc->dictionary_type->check(obj, deref) == js_object_desc_t::Match::Yes);
    goto done;
  } else if(obj.type == PP_VARTYPE_ARRAY) {
    const vlc_ppapi_var_array_t* iva = vlc_getPPAPI_VarArray();

    size_t len = iva->GetLength(obj);
    if(len < desc->array.array_min_len || len > desc->array.array_max_len) {
      result = false;
      goto done;
    }

    if(desc->array.array_element_type == NULL) {
      result = true;
      goto done;
    }

    for(size_t i = 0; i < len; i++) {
      const PP_Var v = iva->Get(obj, i);
      const bool match = desc->array.array_element_type->check(v, false);
      vlc_ppapi_deref_var(v);
      if(!match) { return false; }
    }

    result = true;
    goto done;
  } else if(obj.type == PP_VARTYPE_RESOURCE && desc->is_correct_res_type != nullptr) {
    result = (desc->is_correct_res_type(obj.value.as_id) != PP_FALSE);
    goto done;
  } else {
    result = true;
    goto done;
  }

 done:
  if(deref) { vlc_ppapi_deref_var(obj); }
  return result;
}

typedef void (*js_method_t)(intf_sys_t* sys,
                            const uint32_t version,
                            const js_static_method_decl_t* decl,
                            // you don't need to add/sub ref args, unless you
                            // make your own copy.
                            const PP_Var arg,
                            int32_t& return_code,
                            PP_Var& return_value);

typedef struct js_static_method_decl_t {
  uint32_t version_added;
  uint32_t version_removed;

  js_var_desc_t arg;

  js_method_t handler;
} js_static_method_decl_t;


struct cstr_hash {
  size_t operator()(const char* str) const {
    assert(str != nullptr);
    // Copied from DictHash in vlc_array.h
    size_t hash = 0;
    while(*str != '\0') {
      hash += *(str++);
      hash += hash << 10;
      hash ^= hash >> 8;
    }
    return hash;
  }
};
struct cstr_equal_to {
  bool operator()(const char* lhs, const char* rhs) const {
    return strcmp(lhs, rhs) == 0;
  }
};

// the global list of all possible methods
// after initialization, this is const!
// also note that C++ global initialization order is undefined.
typedef std::unordered_multimap<const char*,
                                js_static_method_decl_t,
                                cstr_hash,
                                cstr_equal_to> js_methods_t;
static js_methods_t* g_methods = NULL;

static js_methods_t& get_methods() {
  if(g_methods == NULL) {
    g_methods = new js_methods_t();
  }

  return *g_methods;
}

static void check_name(const char* full_name) {
  if(full_name[0] != '/') {
    fprintf(stderr, "`%s` does not start with a '/'!\n", full_name);
    assert(full_name[0] != '/');
  }
}

static js_static_method_decl_t* add_static_method(const char* full_name,
                                                  const uint32_t version_added,
                                                  const uint32_t version_removed,
                                                  const js_var_desc_t arg,
                                                  const js_method_t handler) {
  check_name(full_name);

  const js_static_method_decl_t decl = {
    .version_added = version_added,
    .version_removed = version_removed,
    .arg = arg,
    .handler = handler,
  };

  js_methods_t& methods = get_methods();

  return &methods.emplace(full_name, decl)->second;
}

static void forbidden(intf_sys_t* sys,
                      const uint32_t version,
                      const js_static_method_decl_t* decl,
                      const PP_Var args,
                      int32_t& return_code,
                      PP_Var& ret) {
  VLC_UNUSED(sys); VLC_UNUSED(version); VLC_UNUSED(decl);
  VLC_UNUSED(args); VLC_UNUSED(ret);
  return_code = JS_EFORBIDDEN;
}

// A static property.
typedef struct js_static_property_decl_t {
  js_var_desc_t type;

  js_static_method_decl_t* get;
  js_static_method_decl_t* set;
} js_property_decl_t;
static js_static_property_decl_t add_static_property(const char* full_name,
                                                     const uint32_t version_added,
                                                     const uint32_t version_removed,
                                                     const js_var_desc_t type,
                                                     const js_method_t getter,
                                                     js_method_t setter) {
  const size_t name_len = strlen(full_name);

  const size_t method_name_size = name_len + sizeof(".get()");

  js_static_method_decl_t* get = nullptr;
  js_static_method_decl_t* set = nullptr;
  char* method_name = nullptr;

  assert(getter != nullptr);
  method_name = (char*)malloc(method_name_size);
  if(method_name == NULL) abort();
  snprintf(method_name, method_name_size, "%s.get()", full_name);
  get = add_static_method(method_name,
                          version_added,
                          version_removed,
                          JSV_MATCH_UNDEFINED(),
                          getter);

  if(setter == nullptr) {
    setter = forbidden;
  }

  method_name = (char*)malloc(method_name_size);
  if(method_name == NULL) abort();
  snprintf(method_name, method_name_size, "%s.set()", full_name);

  set = add_static_method(method_name,
                          version_added,
                          version_removed,
                          type,
                          setter);

  const js_static_property_decl_t ret = {
    .type = type,
    .get = get,
    .set = set,
  };
  return ret;
}

static inline PP_Var split_mtime(const mtime_t v);
static inline mtime_t unsplit_mtime(const PP_Var v);

static inline int32_t get_var_int32(const PP_Var v) {
  int32_t iv = v.value.as_int;
  if(v.type == PP_VARTYPE_DOUBLE) {
    iv = (int32_t)v.value.as_double;
  }
  return iv;
}
static inline uint32_t get_var_uint32(const PP_Var v) {
  uint32_t iv = (uint32_t)v.value.as_int;
  if(v.type == PP_VARTYPE_DOUBLE) {
    iv = (uint32_t)v.value.as_double;
  }
  return iv;
}
static inline double get_var_double(const PP_Var v) {
  double dv = v.value.as_double;
  if(v.type == PP_VARTYPE_INT32) {
    dv = (double)v.value.as_int;
  }
  return dv;
}

static struct PP_Var media_to_var(playlist_item_t* media);
