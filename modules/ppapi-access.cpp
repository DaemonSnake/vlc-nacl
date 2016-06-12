/**
 * @file ppapi-access.cpp
 * @brief An access module for remote resources (ie playing a video/music from
 * an URL).
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

// XXX: Handle remote file updates.

#define MODULE_NAME ppapi_access
#define MODULE_STRING "ppapi_access"

#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#include <queue>
#include <map>
#include <string>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_ppapi.h>
#include <vlc_access.h>
#include <vlc_atomic.h>
#include <vlc_md5.h>
#include <vlc_input.h>
#include <vlc_messages.h>

#include <ppapi/c/pp_instance.h>
#include <ppapi/c/pp_completion_callback.h>
#include <ppapi/c/pp_directory_entry.h>

struct access_sys_t;
class LoaderBuffer;

static int Open(vlc_object_t* obj);
static void Close(vlc_object_t* obj);

static int32_t new_download(access_sys_t* sys, const size_t from_meta_index);
static int32_t open_cache_file(access_sys_t* sys, const size_t meta_index, PP_Resource* file);
static int32_t resize_meta(access_sys_t* sys, const size_t meta, const uint64_t new_size);
static void close_loader(access_sys_t* sys);

// whomever decided that HTTP headers should be case insensitive was soooo high.
static bool case_insensitive_strncmp(const char* left, const char* right, const size_t len);

vlc_module_begin()
	set_description("PPAPI HTTP/HTTPS Access Module")
	set_capability("access", 0)
	set_shortname("ppapi-remote-access")
	set_category(CAT_INPUT)
	set_subcategory(SUBCAT_INPUT_ACCESS)
        add_shortcut("http", "https", "ftp")
	set_callbacks(Open, Close)
vlc_module_end()

typedef struct cache_meta_t {
  uint64_t id;
  uint64_t start;
} cache_meta_t;

#define NO_PROGRESS_RESTART_DELAY 3*1000*1000 // three seconds
#define MAX_CACHE_PATH 512
#define NEED_DATA_UNTIL_BLOCK_SIZE 512

class Buffer {
public:
  Buffer() { }

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  Buffer(Buffer&& rhs)
    : buf_(rhs.base())
    , size_(rhs.size())
    , offset_(rhs.offset())
  {
    rhs.buf_ = rhs.offset_ = nullptr;
    rhs.size_ = 0;
  }
  Buffer& operator=(Buffer&& rhs) {
    if(buf_ != nullptr) {
      free(buf_);
    }
    buf_ = rhs.base();
    offset_ = rhs.offset();
    size_ = rhs.size();
    rhs.buf_ = rhs.offset_ = nullptr;
    rhs.size_ = 0;
    return *this;
  }

  ~Buffer() {
    if(buf_ != nullptr) {
      free(buf_);
    }
  }

  int copy(uint8_t* src, size_t size) {
    int ret = VLC_SUCCESS;
    if(src == nullptr) {
      assert(size == 0);
      return ret;
    } else if(size == 0) {
      return ret;
    }

    if((ret = need(size)) != VLC_SUCCESS) {
      return ret;
    }
    memmove(offset(), src, size);
    return ret;
  }

  int need(size_t size) {
    if(buf_ != nullptr) {
      assert(offset_ != nullptr);
      if(left() > size) {
        return VLC_SUCCESS;
      }

      const auto reserve = size - left();
      uint8_t* new_buf = nullptr;
      if(offset() != base()) {
        new_buf = (uint8_t*)malloc(reserve);
        if(new_buf == nullptr) {
          return VLC_ENOMEM;
        }

        memmove(new_buf, offset_, left());
      } else {
        new_buf = (uint8_t*)realloc(buf_, reserve);
        if(new_buf == nullptr) {
          return VLC_ENOMEM;
        }
      }
      free(buf_);
      buf_ = offset_ = new_buf;
      size_ = reserve;

    } else {
      auto* new_buf = (uint8_t*)malloc(size);
      if(new_buf == nullptr) {
        return VLC_ENOMEM;
      }
      buf_ = new_buf;
      size_ = size;
      offset_ = new_buf;
    }

    return VLC_SUCCESS;
  }

  size_t left() {
    if(buf_ == nullptr) { return 0; }

    return buf_ + size_ - offset_;
  }

  size_t size() { return size_; }

  uint8_t* read(size_t size) {
    assert(offset_ != nullptr);
    if(left() < size) { return nullptr; }

    auto* read = offset_;
    offset_ += size;
    return read;
  }

  uint8_t* base() { return buf_; }
  uint8_t* offset() { return offset_; }

  void debug_info_str(access_t* access, const char* msg, const size_t bytes) {
    static const char inputs[] = "\0\a\b\f\n\r\t\v";
    static const char outputs[] = "0abfnrtv";

    size_t size = __MIN(bytes, left());
    size_t escapes = 0;
    for(size_t i = 0; i < size; ++i) {
      char* pos = nullptr;
      if((pos = strchr(inputs, offset()[i])) != nullptr) {
        escapes += 1;
      }
    }
    char* buffer = (char*)alloca(size + escapes + 1);
    buffer[size + escapes] = '\0';
    for(size_t i = 0, j = 0; i < size; ++i) {
      if(char* pos = strchr(inputs, offset()[i])) {
        buffer[j++] = '\\';
        buffer[j++] = outputs[pos - inputs];
      } else {
        buffer[j++] = offset()[i];
      }
    }

    msg_Info(access, "buffer data at %s: `%s`", msg, buffer);
  }

private:
  uint8_t* buf_ = nullptr;
  size_t size_ = 0;

  uint8_t* offset_ = nullptr;
};

// TODO make this work with unsized streams.
class Loader {
public:
  Loader() = default;

  bool is_open() const { return loader_ != 0; }

  void close();

  void open(const PP_Resource loader);

  int32_t progress(uint64_t& recieved, uint64_t& total);

  uint64_t left();

  uint64_t unread();

  int32_t read(Buffer& into, size_t amount);
private:
  PP_Resource loader_ = 0;
  uint64_t bytes_downloaded_ = 0;
};

class LoaderBuffer {
public:
  LoaderBuffer() = delete;
  explicit LoaderBuffer(access_sys_t* sys);

  Loader& loader() { return *loader_; }

  int32_t load_recieved(const size_t max = 1024*1024);

  int32_t cache_rest_into_meta(bool& piece_end);
  int32_t cache_all_rest_into_meta();

  int32_t read_until_newline(Buffer& into);

  int32_t read(const size_t size, uint8_t*& dest);

  int32_t read_multi_part_headers();

private:
  Loader* loader_;
  access_sys_t* sys_;
  Buffer buf_;
};

struct access_sys_t {
  PP_Instance instance;
  PP_Resource message_loop;

  PP_Resource cache_meta_fref;

  Loader loader_;
  PP_Resource request_info = 0;
  PP_Resource response_info = 0;

  mtime_t last_progress_time = VLC_TS_INVALID;

  bool response_is_multipart = false;

  // all multiparts must have the same Content-Type header.
  std::string content_type_;
  std::string multi_part_boundary_;

  uint64_t next_mpb_end = 0;

  uint64_t data_cache_basename = 0;
  std::string location_str_;

  PP_Resource* data_caches = nullptr; // File resources
  cache_meta_t* cache_metas = nullptr;
  size_t cache_count = 0;

  uint64_t next_meta_id = 0;

  size_t ppapi_meta = 0;   // the meta which recieves the downloaded data.
  uint64_t ppapi_offset = 0;

  bool is_last_ppapi_meta() const {
    return this->ppapi_meta + 1 >= this->cache_count;
  }

  uint64_t ppapi_meta_end() const {
    return this->meta_end(this->ppapi_meta);
  }

  size_t vlc_meta = 0; // the meta where we read the data from.
  uint64_t vlc_offset = 0;

  uint64_t vlc_meta_end() const {
    return this->meta_end(this->vlc_meta);
  }

  uint64_t meta_end(size_t meta) const {
    uint64_t part_end = 0;
    if(meta + 1 < this->cache_count) {
      part_end = this->cache_metas[meta + 1].start;
    } else {
      part_end = this->file_size;
    }
    return part_end;
  }

  uint64_t vlc_last_seek_offset = 0;
  uint64_t vlc_read_since_last_seek = 0;

  uint64_t file_size = 0;

  access_t* access = nullptr;
};

void Loader::close() {
  if(loader_ != 0) {
    const vlc_ppapi_url_loader_t* iloader = vlc_getPPAPI_URLLoader();
    iloader->Close(loader_);
    vlc_subResReference(loader_);
    loader_ = 0;
    bytes_downloaded_ = 0;
  }
}

void Loader::open(const PP_Resource loader) {
  close();

  loader_ = loader;
  bytes_downloaded_ = 0;
}

int32_t Loader::progress(uint64_t& recieved, uint64_t& total) {
  const vlc_ppapi_url_loader_t* iloader = vlc_getPPAPI_URLLoader();

  int64_t bytes_recieved, bytes_total;
  if(iloader->GetDownloadProgress(loader_, &bytes_recieved, &bytes_total) != PP_TRUE) {
    return VLC_EGENERIC;
  }

  recieved = (uint64_t)bytes_recieved - bytes_downloaded_;
  total = (uint64_t)bytes_total - bytes_downloaded_;
  return VLC_SUCCESS;
}

uint64_t Loader::left() {
  uint64_t recieved; VLC_UNUSED(recieved);
  uint64_t total;
  int ret = VLC_SUCCESS;
  ret = progress(recieved, total);
  assert(ret == VLC_SUCCESS); VLC_UNUSED(ret);

  return total;
}

uint64_t Loader::unread() {
  uint64_t recieved;
  uint64_t total; VLC_UNUSED(total);
  int ret = VLC_SUCCESS;
  ret = progress(recieved, total);
  assert(ret == VLC_SUCCESS); VLC_UNUSED(ret);

  return recieved;
}

int32_t Loader::read(Buffer& into, size_t amount) {
  const vlc_ppapi_url_loader_t* iloader = vlc_getPPAPI_URLLoader();

  int ret = VLC_SUCCESS;
  if((ret = into.need(amount)) != VLC_SUCCESS) {
    return ret;
  }

  for(size_t total = 0; total < amount;) {
    const auto read = iloader->ReadResponseBody(loader_, into.base() + total,
                                                amount - total, PP_BlockUntilComplete());
    if(read < 0) {
      return VLC_EGENERIC;
    } else {
      total += read;
      bytes_downloaded_ += read;
    }
  }
  return VLC_SUCCESS;
}

LoaderBuffer::LoaderBuffer(access_sys_t* sys)
  : loader_(&sys->loader_)
  , sys_(sys)
{ }

int32_t LoaderBuffer::load_recieved(const size_t max) {
  const auto unread = __MIN(max, loader().unread());

  int32_t ret = VLC_SUCCESS;
  if((ret = loader().read(buf_, unread)) != VLC_SUCCESS) {
    return ret;
  }

  return ret;
}
int32_t LoaderBuffer::cache_rest_into_meta(bool& piece_end) {
  const vlc_ppapi_file_io_t*  ifile_io = vlc_getPPAPI_FileIO();

  const uint64_t part_start = sys_->cache_metas[sys_->ppapi_meta].start;
  const uint64_t part_end = sys_->ppapi_meta_end();

  uint64_t part_offset = sys_->ppapi_offset - part_start;
  const uint64_t part_left = part_end - sys_->ppapi_offset;

  int32_t code = PP_OK;

  // Open the file inside the loop; it may change between iterations if we're
  // processing a multipart reply.
  PP_Resource file;
  if((code = open_cache_file(sys_, sys_->ppapi_meta, &file)) != VLC_SUCCESS) {
    return code;
  }

  const uint64_t rest_size = __MIN(sys_->next_mpb_end, __MIN(buf_.left(), part_left));

  if((code = resize_meta(sys_, sys_->ppapi_meta, part_offset + rest_size)) != PP_OK) {
    return VLC_SUCCESS;
  }

  for(size_t written = 0; written < rest_size;) {
    const auto code = ifile_io->Write(file, part_offset,
                                      (const char*)buf_.offset(),
                                      rest_size - written,
                                      PP_BlockUntilComplete());
    assert(code != 0);
    if(code < 0) {
      msg_Err(sys_->access, "failed to write into the cache");
      return VLC_EGENERIC;
    }
    written += (size_t)code;
    buf_.read((size_t)code);
    part_offset += (uint64_t)code;
    sys_->ppapi_offset += (uint64_t)code;
  }

  piece_end = buf_.left() > 0;

  return VLC_SUCCESS;
}

int32_t LoaderBuffer::cache_all_rest_into_meta() {
  int32_t code = VLC_SUCCESS;
  for(bool piece_end = true; piece_end;) {
    assert(sys_->ppapi_meta < sys_->cache_count);

    cache_rest_into_meta(piece_end);

    if(!piece_end) {
      break;
    }

    msg_Info(sys_->access, "ppapi: finished part `%u`", sys_->ppapi_meta);

    if(sys_->is_last_ppapi_meta()) {
      close_loader(sys_);
      break;
    }

    sys_->ppapi_meta += 1;

    if(sys_->next_mpb_end >= sys_->ppapi_offset) {
      // we may have combined ranges together, so this branch means we've
      // finish this cache, but haven't finished the part overall.
      // Thus we need to write the rest of cache_dest into the next cache
      // file.

#ifndef NDEBUG
      // sanity check: check that the next block is indeed empty.
      PP_Resource file;
      if((code = open_cache_file(sys_, sys_->ppapi_meta, &file)) != VLC_SUCCESS) {
        return code;
      }

      struct PP_FileInfo info;
      code = vlc_getPPAPI_FileIO()->Query(file, &info, PP_BlockUntilComplete());
      if(code != PP_OK) {
        return VLC_EGENERIC;
      }

      assert(info.size == 0);
#endif
      continue;
    } else if(sys_->response_is_multipart) {
      code = read_multi_part_headers();
      if(code != VLC_SUCCESS) {
        msg_Err(sys_->access, "failed to parse multipart headers");
        return code;
      }
    } else if(!sys_->response_is_multipart) {
      // We requested multiple parts, but the server ignored all of them
      // except the first.
      // So, start a new download starting at the next missing piece.
      if((code = new_download(sys_, sys_->ppapi_meta)) != VLC_SUCCESS) {
        msg_Err(sys_->access, "unable to continue caching media; playback will stop "
                "sometime in the future");
        return code;
      }

      break;
    }
  }

  return code;
}

int32_t LoaderBuffer::read_until_newline(Buffer& into) {
  int32_t ret = VLC_SUCCESS;
  size_t len = 0;
  for(; *(char*)(buf_.offset() + len) != '\n'; len++) {
    if(buf_.left() <= len) {
      const size_t dl = __MIN(NEED_DATA_UNTIL_BLOCK_SIZE, loader().left());
      if(dl == 0) {
        len -= 1;
        break;
      }
      if((ret = loader_->read(buf_, dl)) != VLC_SUCCESS) {
        return ret;
      }
    }
  }

  auto* read = buf_.read(len + 1);
  assert(read);

  if(len != 0 && *(char*)(buf_.offset() + len - 1) == '\r') {
    len -= 1;
  }

  return into.copy(read, len);
}

int32_t LoaderBuffer::read(const size_t size, uint8_t*& dest) {
  int32_t ret = VLC_SUCCESS;
  if(buf_.left() < size && (ret = loader_->read(buf_, size)) != VLC_SUCCESS) {
    return ret;
  }

  dest = buf_.read(size);
  return ret;
}
int32_t LoaderBuffer::read_multi_part_headers() {

  uint8_t* buf;

  int32_t code = read(1, buf);
  buf_.debug_info_str(sys_->access, "1", 64);
  if(code != VLC_SUCCESS || (buf[0] != '\n' && buf[0] != '\r')) {
    return VLC_EGENERIC;
  } else if(buf[0] == '\r') {
    // skip the following '\n'
    if((code = read(1, buf)) != VLC_SUCCESS || buf[0] != '\n') {
      return VLC_EGENERIC;
    }
  }

  code = read(2, buf);
  buf_.debug_info_str(sys_->access, "2", 64);
  if(code != VLC_SUCCESS || buf[0] != '-' || buf[1] != '-') {
    return VLC_EGENERIC;
  }

  code = read(sys_->multi_part_boundary_.size(), buf);
  buf_.debug_info_str(sys_->access, "3", 64);
  if(code != VLC_SUCCESS) {
    return code;
  } else if(strncmp((const char*)buf, sys_->multi_part_boundary_.data(),
                    sys_->multi_part_boundary_.size()) != 0) {
    msg_Err(sys_->access, "unrecognized boundary field");
    return VLC_EGENERIC;
  }

  code = read(1, buf);
  buf_.debug_info_str(sys_->access, "4", 64);
  if(code != VLC_SUCCESS || (buf[0] != '\n' && buf[0] != '\r')) {
    return VLC_EGENERIC;
  } else if(buf[0] == '\r') {
    // skip the following '\n'
    if((code = read(1, buf)) != VLC_SUCCESS || buf[0] != '\n') {
      return VLC_EGENERIC;
    }
  }

  // read multipart headers
  for(bool recieved_content_range = false;;) {
    Buffer header;
    if((code = read_until_newline(header)) != VLC_SUCCESS) {
      return code;
    }

    if(header.size() == 0) {
      if(!recieved_content_range) {
        msg_Err(sys_->access, "missing \"Content-Range\" header");
        sys_->next_mpb_end = sys_->ppapi_meta_end();
      }
      return VLC_SUCCESS;
    }

    const char content_range[] = "Content-Range: ";
    const size_t content_range_len = sizeof(content_range) - 1;
    if(!recieved_content_range &&
       case_insensitive_strncmp((const char*)header.offset(), (const char*)content_range,
                                __MIN(header.left(), content_range_len)) == 0) {
      header.read(content_range_len);

      uint64_t start, end;
      if(!(sscanf((const char*)header.offset(), "bytes %llu-%llu/%*llu", &start, &end) == 2 ||
           sscanf((const char*)header.offset(), "bytes=%llu-%llu/%*llu", &start, &end) == 2)) {
        msg_Err(sys_->access, "malformed \"Content-Range\" header");
        return VLC_EGENERIC;
      } else if(start < sys_->cache_metas[sys_->ppapi_meta].start) {
        msg_Err(sys_->access, "malformed \"Content-Range\" header: `%llu` < `%llu`",
                start, sys_->cache_metas[sys_->ppapi_meta].start);
        return VLC_EGENERIC;
      }

      sys_->ppapi_offset = start;
      sys_->next_mpb_end = end;
      recieved_content_range = true;
    }

    // TODO emit warning about an unknown header.
  }
  return VLC_SUCCESS;
}

inline static PP_Resource get_temp_fs(PP_Instance instance) {
  return vlc_ppapi_get_temp_fs(instance);
}
static void save_metas(access_sys_t* sys) {
  assert(sys != NULL);
  assert(sys->cache_meta_fref != 0);

  const vlc_ppapi_file_io_t*  ifile_io = vlc_getPPAPI_FileIO();

  int64_t code = 0;

  PP_Resource cache_file_io = ifile_io->Create(sys->instance);
  if(cache_file_io == 0) {
    msg_Warn(sys->access, "failed to create file io interface resource");
    return;
  }

  for(;;) {
    if((code = ifile_io->Open(cache_file_io, sys->cache_meta_fref,
                              PP_FILEOPENFLAG_WRITE | PP_FILEOPENFLAG_CREATE,
                              PP_BlockUntilComplete())) != PP_OK) {
      msg_Warn(sys->access, "failed to open file io resource for writing");
      break;
    }

    const uint64_t total_size = sys->cache_count * sizeof(cache_meta_t) +
      sizeof(sys->cache_count) + sizeof(sys->next_meta_id);

    if((code = ifile_io->SetLength(cache_file_io, total_size, PP_BlockUntilComplete())) != PP_OK) {
      msg_Warn(sys->access, "failed to set cache meta file length");
      break;
    }

    uint64_t offset = 0;

    code = ifile_io->Write(cache_file_io, offset, (char*)(&sys->cache_count),
                           sizeof(sys->cache_count),
                           PP_BlockUntilComplete());
    if(code != sizeof(sys->cache_count)) { code = 1; break; }
    offset += sizeof(sys->cache_count);

    code = ifile_io->Write(cache_file_io, offset, (char*)(&sys->next_meta_id),
                           sizeof(sys->next_meta_id),
                           PP_BlockUntilComplete());
    if(code != sizeof(sys->next_meta_id)) { code = 1; break; }
    offset += sizeof(sys->next_meta_id);

    code = ifile_io->Write(cache_file_io, offset, (char*)(sys->cache_metas),
                           sizeof(cache_meta_t) * sys->cache_count,
                           PP_BlockUntilComplete());
    if(code != sizeof(cache_meta_t) * sys->cache_count) { code = 1; break; }
    offset += sizeof(cache_meta_t) * sys->cache_count;

    code = VLC_SUCCESS;

    break;
  }

  if(code != VLC_SUCCESS) {
    msg_Warn(sys->access, "failed to write the meta cache file");
  }

  vlc_subResReference(cache_file_io);
}
static void close_loader(access_sys_t* sys) {
  if(sys->loader_.is_open()) {
    msg_Info(sys->access, "closing PPB_URLLoader");
    sys->loader_.close();
  }

  if(sys->request_info != 0) {
    vlc_subResReference(sys->request_info);
    sys->request_info = 0;
  }
  if(sys->response_info != 0) {
    vlc_subResReference(sys->response_info);
    sys->response_info = 0;
  }

  sys->content_type_.clear();
  sys->multi_part_boundary_.clear();
  sys->response_is_multipart = false;

  sys->ppapi_meta = sys->ppapi_offset = 0;
}

static PP_Resource get_cache_file_ref(access_sys_t* sys, const size_t meta_index) {
  const vlc_ppapi_file_ref_t* ifile_ref = vlc_getPPAPI_FileRef();

  // open the file
  PP_Resource cache_fs = get_temp_fs(sys->instance);

  cache_meta_t meta = sys->cache_metas[meta_index];
  assert(meta.id < sys->next_meta_id);

  const char filename_fmt[] = "/vlc-cache/%llu.%llu.data";

  char* data_cache_path = NULL;
  asprintf(&data_cache_path, filename_fmt, sys->data_cache_basename, meta.id);

  PP_Resource cache_file_ref = ifile_ref->Create(cache_fs, data_cache_path);
  free(data_cache_path);
  return cache_file_ref;
}

static int32_t open_cache_file(access_sys_t* sys, const size_t meta_index, PP_Resource* file) {
  assert(file != NULL);
  *file = sys->data_caches[meta_index];
  if(*file == 0) {
    const vlc_ppapi_file_io_t*  ifile_io = vlc_getPPAPI_FileIO();
    PP_Resource cache_file_ref = get_cache_file_ref(sys, meta_index);
    if(cache_file_ref == 0) {
      return VLC_EGENERIC;
    }

    *file = ifile_io->Create(sys->instance);
    if(*file == 0) {
      vlc_subResReference(cache_file_ref);
      return VLC_EGENERIC;
    }

    int32_t flags = PP_FILEOPENFLAG_READ | PP_FILEOPENFLAG_WRITE | PP_FILEOPENFLAG_CREATE;
    //flags |= PP_FILEOPENFLAG_TRUNCATE;
    int32_t code = ifile_io->Open(*file, cache_file_ref,
                                  flags, PP_BlockUntilComplete());
    vlc_subResReference(cache_file_ref);
    if(code != PP_OK) {
      vlc_subResReference(*file);
      *file = 0;
      return VLC_EGENERIC;
    }

    sys->data_caches[meta_index] = *file;
  }

  return VLC_SUCCESS;
}

static int32_t add_meta(access_sys_t* sys, const cache_meta_t meta, const size_t where) {
  if(sys->cache_count - 1 > where) {
    assert(meta.start < sys->cache_metas[where + 1].start);
  } else {
    assert(meta.start > sys->cache_metas[where - 1].start);
  }
  msg_Info(sys->access, "add_meta: cache_meta_t { id: `%llu`, start: `%llu`, } @ index `%u`",
           meta.id, meta.start, where);

  sys->cache_count++;
  void* metas = realloc(sys->cache_metas, sizeof(cache_meta_t) * sys->cache_count);
  if(metas == NULL) {
    sys->cache_count--;
    return VLC_ENOMEM;
  }

  PP_Resource* files = (PP_Resource*)realloc(sys->data_caches, sizeof(PP_Resource) * sys->cache_count);
  if(files == NULL) {
    sys->cache_count--;
    sys->cache_metas = (cache_meta_t*)realloc(sys->cache_metas, sizeof(cache_meta_t) * sys->cache_count);
    assert(sys->cache_metas != NULL);
    return VLC_ENOMEM;
  }

  sys->cache_metas = (cache_meta_t*)metas;
  sys->data_caches = files;

  for(size_t i = where + 1; i < sys->cache_count; i++) {
    sys->cache_metas[i] = sys->cache_metas[i - 1];
    sys->data_caches[i] = sys->data_caches[i - 1];
  }
  sys->cache_metas[where] = meta;
  sys->data_caches[where] = 0;

  // check for and possibly delete a pre-existing data cache file:
  PP_Resource ref = 0;
  if((ref = get_cache_file_ref(sys, where)) != 0) {
    const vlc_ppapi_file_ref_t* iref = vlc_getPPAPI_FileRef();
    int32_t code = iref->Delete(ref, PP_BlockUntilComplete());
    if(code != PP_OK && code != PP_ERROR_FILENOTFOUND) {
      msg_Err(sys->access, "error delete pre-existing cache file: `%i`", code);
    }
  }

  save_metas(sys);

  return VLC_SUCCESS;
}

// Returns the number of bytes written into dest, or a number < 0 for an error.
static int64_t load_from_cache(access_sys_t* sys, uint8_t* dest,
                               size_t size) {
  assert(sys != NULL); assert(dest != NULL); assert(size != 0);

  const vlc_ppapi_file_io_t*  ifile_io = vlc_getPPAPI_FileIO();
  assert(sys->vlc_meta < sys->cache_count);

  const cache_meta_t meta = sys->cache_metas[sys->vlc_meta];

  int32_t code;

  PP_Resource file;
  if((code = open_cache_file(sys, sys->vlc_meta, &file)) != VLC_SUCCESS) {
    return code;
  }

  struct PP_FileInfo info;
  code = ifile_io->Query(file, &info, PP_BlockUntilComplete());
  if(code != PP_OK) {
    return VLC_EGENERIC;
  }

  const uint64_t cache_offset = sys->vlc_offset - meta.start;
  if(cache_offset >= (uint64_t)info.size) {
    return 0;
  }

  const bool is_last_meta = (sys->vlc_meta + 1 == sys->cache_count);
  const uint64_t end = is_last_meta ? sys->file_size : sys->cache_metas[sys->vlc_meta + 1].start;
  assert(info.size + meta.start <= end);

  const uint64_t to_read = __MIN((uint64_t)info.size - cache_offset, size);
  //printf("sys->vlc_meta = %u, meta = { %llu, %llu }, sys->vlc_offset = %llu, to_read = %llu\n",
  //       sys->vlc_meta, meta.id, meta.start, sys->vlc_offset, to_read);
  code = ifile_io->Read(file, cache_offset, (char*)(dest),
                        to_read, PP_BlockUntilComplete());
  if(code < 0) {
    return VLC_EGENERIC;
  } else if((uint64_t)code < to_read) {
    msg_Dbg(sys->access, "we've read less than intended: code = %i, to_read = %llu\n", code, to_read);
  }

  sys->vlc_offset += code;
  size -= code;

  sys->access->info.b_eof = sys->file_size < sys->vlc_offset;

  if(size > 0 && !is_last_meta &&
     // Check that this meta contains all the data possible between the
     // previous meta and the next.
     end == meta.start + info.size) {
    // start loading from the next file
    msg_Dbg(sys->access, "vlc: finished part `%u`", sys->vlc_meta);
    sys->vlc_meta += 1;
  }

  return (int64_t)code;

}

static int32_t delete_file_ref(access_t* access, const PP_Resource file_ref) {
  const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();
  const vlc_ppapi_file_ref_t* ifile_ref = vlc_getPPAPI_FileRef();

  const int32_t code = ifile_ref->Delete(file_ref, PP_BlockUntilComplete());
  if(code != PP_OK) {
    char* filename_buffer = (char*)alloca(MAX_CACHE_PATH);

    size_t name_len = 0;
    PP_Var name = ifile_ref->GetPath(file_ref);
    const char* name_str = ivar->VarToUtf8(name, &name_len);
    assert(name_len + 1 < MAX_CACHE_PATH);
    memset(filename_buffer, 0, MAX_CACHE_PATH);
    memmove((void*)filename_buffer, (void*)name_str, name_len);
    vlc_ppapi_deref_var(name);

    msg_Warn(access, "deletion failed for file `%s`: `%i`",
             filename_buffer, code);
  }

  return code;
}

static int32_t resize_meta(access_sys_t* sys, const size_t meta, const uint64_t new_size) {
  const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();
  const vlc_ppapi_file_io_t*  ifile_io = vlc_getPPAPI_FileIO();
  const vlc_ppapi_file_ref_t* ifile_ref = vlc_getPPAPI_FileRef();

  assert(sys->meta_end(meta) >= new_size);

  int32_t code = VLC_SUCCESS;
  PP_Resource file;
  if((code = open_cache_file(sys, meta, &file)) != VLC_SUCCESS) {
    return code;
  }

  uint64_t original_size = 0;
  {
    struct PP_FileInfo info;
    code = ifile_io->Query(file, &info, PP_BlockUntilComplete());
    assert(code == PP_OK);
    original_size = info.size;
  }

  code = ifile_io->SetLength(file, new_size, PP_BlockUntilComplete());
  if(code != PP_OK && code != PP_ERROR_NOQUOTA && code != PP_ERROR_NOSPACE) {
    return VLC_EGENERIC;
  }

  if(code == PP_OK) { return VLC_SUCCESS; }

  assert(original_size < new_size);

  if(code == PP_ERROR_NOQUOTA || code == PP_ERROR_NOSPACE) {
    msg_Warn(sys->access, "no more quota or space left; removing some old cached videos.");
    VLC_PPAPI_ARRAY_OUTPUT(entries_out, output);

    const PP_Resource cache_dir = ifile_ref->Create(get_temp_fs(sys->instance), "/vlc-cache");
    assert(cache_dir != 0);

    code = ifile_ref->ReadDirectoryEntries(cache_dir, output, PP_BlockUntilComplete());
    vlc_subResReference(cache_dir);
    if(code != PP_OK) {
      msg_Err(sys->access, "failed to read dir entries: `%d`", code);
      return VLC_EGENERIC;
    }

    PP_DirectoryEntry* enties = static_cast<PP_DirectoryEntry*>(entries_out.data);
    const size_t entry_len = entries_out.elements;

    char* filename_buffer = (char*)alloca(MAX_CACHE_PATH);

    struct file_mtime {
      PP_Resource file_ref;
      PP_Time mtime;
      uint64_t size;
    };
    struct comparer {
      constexpr bool operator()(const file_mtime& lhs, const file_mtime& rhs) const {
        return lhs.mtime < rhs.mtime;
      }
    };
    std::priority_queue<file_mtime, std::vector<file_mtime>, comparer> to_delete;

    for(size_t i = 0; i < entry_len; i++) {
      const PP_Resource file = enties[i].file_ref;
      if(enties[i].file_type != PP_FILETYPE_REGULAR) {
        vlc_subResReference(file);
        continue;
      }

      size_t name_len = 0;
      {
        PP_Var name = ifile_ref->GetName(file);
        const char* name_str = ivar->VarToUtf8(name, &name_len);
        assert(name_len + 1 < MAX_CACHE_PATH);
        memset(filename_buffer, 0, MAX_CACHE_PATH);
        memmove((void*)filename_buffer, (void*)name_str, name_len);
        vlc_ppapi_deref_var(name);
      }

      const char meta_ext[] = "meta";
      const size_t meta_ext_len = sizeof(meta_ext) - 1;
      if(strcmp(filename_buffer + name_len - meta_ext_len, meta_ext) == 0) {
        vlc_subResReference(file);
        continue;
      }

      uint64_t access_id = 0;
      sscanf(filename_buffer, "%llu.%*zu.data", &access_id);
      if(access_id == sys->data_cache_basename) {
        vlc_subResReference(file);
        continue;
      }

      struct PP_FileInfo info;
      code = ifile_ref->Query(file, &info, PP_BlockUntilComplete());
      assert(code == PP_OK);

      const file_mtime d = {
        .file_ref = file,
        .mtime = info.last_modified_time,
        .size = (uint64_t)info.size,
      };

      to_delete.emplace(d);
    }
    free(enties);

    int64_t delta = new_size - original_size;
    while(delta > 0 && !to_delete.empty()) {
      const auto t = to_delete.top();
      to_delete.pop();

      code = delete_file_ref(sys->access, t.file_ref);
      if(code == PP_OK) {
        delta -= t.size;
      }

      vlc_subResReference(t.file_ref);
    }

    if(delta > 0 && to_delete.empty()) {
      // not enough available to delete :(
      msg_Err(sys->access, "not enough old cache files to delete: still need `%lli` bytes!",
              delta);
      return VLC_EGENERIC;
    }

    for(; !to_delete.empty();) {
      const auto t = to_delete.top();
      to_delete.pop();

      vlc_subResReference(t.file_ref);
    }

    assert(delta <= 0);

    if(ifile_io->SetLength(file, new_size, PP_BlockUntilComplete()) != PP_OK) {
      return VLC_EGENERIC;
    } else {
      return VLC_SUCCESS;
    }
  }

  vlc_assert_unreachable();
}

// whoever decided that HTTP headers should be case insensitive was soooo high.
static bool case_insensitive_strncmp(const char* left, const char* right, const size_t len) {
  for(size_t i = 0; i < len; i++) {
    if(left[i] == '\0' || right[i] == '\0') { break; }

    if(tolower((int)left[i]) != tolower((int)right[i])) {
      return false;
    }
  }

  return true;
}

static int32_t download_and_cache(access_sys_t* sys) {
  if(!sys->loader_.is_open()) {
    return VLC_SUCCESS;
  }

  int32_t code = 0;

  LoaderBuffer loader(sys);

  if((code = loader.load_recieved()) != VLC_SUCCESS) {
    return code;
  }

  /*const mtime_t current = mdate();
  if(cache_dest_size == 0) {
    if(current - sys->last_progress_time > NO_PROGRESS_RESTART_DELAY && bytes_recieved != bytes_total) {
      msg_Warn(sys->access, "no progress has been make for three seconds; restarting download");
      close_loader(sys);
      if((code = new_download(sys, sys->ppapi_meta)) != VLC_SUCCESS) { return code; }
      sys->last_progress_time = current;
    } else if(bytes_recieved == bytes_total) {
      close_loader(sys);
    }
    return VLC_SUCCESS;
  } else {
    sys->last_progress_time = current;
    }*/

  return loader.cache_all_rest_into_meta();

}

static bool find_header(const char* headers, const size_t headers_len,
                        const char* to_find, const size_t to_find_len,
                        char const** found, size_t* found_len) {
  for(size_t found_start = 0; found_start < headers_len - to_find_len; found_start++) {
    if(case_insensitive_strncmp(&headers[found_start], to_find,
				__MIN(to_find_len, headers_len - found_start))) {
      if(found != NULL) {
        assert(found_len != NULL);
        *found = &headers[found_start];

        const char* i = *found;
        for(; *i != '\n' && *i != '\r' && (size_t)(i - *found) < headers_len; i++) { }
        *found_len = i - *found;
      }

      return true;
    }
  }
  return false;
}

static int32_t new_download(access_sys_t* sys, const size_t from_meta_index) {
  const vlc_ppapi_file_io_t*  ifile_io = vlc_getPPAPI_FileIO();
  const vlc_ppapi_url_loader_t* iloader = vlc_getPPAPI_URLLoader();
  const vlc_ppapi_url_request_info_t* irequest = vlc_getPPAPI_URLRequestInfo();
  const vlc_ppapi_url_response_info_t* iresponse = vlc_getPPAPI_URLResponseInfo();
  const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();

  VLC_PPAPI_STATIC_STR(http_method, "GET");

  assert(from_meta_index < sys->cache_count);

  size_t ranges_size = sys->cache_count - from_meta_index;
  uint64_t ranges[ranges_size][2];
  memset(&ranges[0][0], 0, sizeof(ranges));

  // Upon return, what meta will we be caching into? This will represent the
  // first meta after `from_meta_index` that does not have all the data between
  // it and the next meta (or the end of the file).
  size_t next_ppapi_meta = 0;
  bool next_ppapi_meta_found = false;

  for(size_t i = 0, j = 0; i < sys->cache_count - from_meta_index; i += 1) {
    const size_t index = i + from_meta_index;
    PP_Resource file;
    int32_t code;
    if((code = open_cache_file(sys, index, &file)) != VLC_SUCCESS) {
      return code;
    }

    struct PP_FileInfo info;
    code = ifile_io->Query(file, &info, PP_BlockUntilComplete());
    if(code != PP_OK) {
      return VLC_EGENERIC;
    }

    const bool is_last_meta = (index + 1 == sys->cache_count);
    const uint64_t end_offset = is_last_meta ? sys->file_size : sys->cache_metas[index + 1].start;

    ranges[j][0] = sys->cache_metas[index].start + info.size;

    if(ranges[j][0] > end_offset) {
      msg_Err(sys->access, "index = `%u`, j = `%u`: ranges[j][0] = `%llu`, end_offset = `%llu`",
             index, j, ranges[j][0], end_offset);
      assert(ranges[j][0] < end_offset);
    }

    ranges[j][1] = end_offset - 1;
    if(ranges[j][0] == end_offset) {
      // this cache block is already fully cached.
      ranges_size -= 1;
    } else {
      // this cache block has missing data
      if(info.size != 0 || j == 0 /* can't skip the first cache */) {
        j += 1;
      } else if(info.size == 0 && j != 0) {
        // we need to remove this meta from the requested ranges. HTTP says we
        // "SHOULD NOT" send range requests with back-to-back ranges.
        ranges[j - 1][1] = ranges[j][1];
        ranges_size -= 1;
      }

      if(!next_ppapi_meta_found) {
        next_ppapi_meta = index;
        next_ppapi_meta_found = true;
      }
    }
  }

  if(ranges_size == 0) {
    // We already have all the data from `from_meta_index` to the end of the
    // file.
    msg_Info(sys->access, "nothing to download; media cached");
    return VLC_SUCCESS;
  }

  // calc the size of the header:
  size_t headers_size = 0;

  headers_size += snprintf(NULL, 0, "Range: bytes=");

  for(size_t i = 0; i < ranges_size; i++) {
    headers_size += snprintf(NULL, 0, "%llu-%llu,", ranges[i][0], ranges[i][1]);
  }
  // TODO other headers

  struct PP_Var headers_var;

  {
    char* headers = (char*)alloca(headers_size);
    size_t written = snprintf(headers, headers_size, "Range: bytes=");
    for(size_t i = 0; i < ranges_size; i++) {
      written += snprintf(headers + written,
			  headers_size - written,
			  "%llu-%llu,", ranges[i][0], ranges[i][1]);
    }
    msg_Info(sys->access, "sending headers: `%s`", headers);
    headers_var = vlc_ppapi_cstr_to_var(headers, headers_size - 1);
  }

  PP_Resource request = irequest->Create(sys->instance);
  if(request == 0) {
    vlc_ppapi_deref_var(headers_var);
    return VLC_EGENERIC;
  }
  PP_Bool success;

  success = irequest->SetProperty(request, PP_URLREQUESTPROPERTY_METHOD,
                                  vlc_ppapi_mk_str(&http_method));
  success = success == PP_TRUE ? irequest->SetProperty(request,
						       PP_URLREQUESTPROPERTY_RECORDDOWNLOADPROGRESS,
						       PP_MakeBool(PP_TRUE))
    : success;

  PP_Var location = vlc_ppapi_cstr_to_var(sys->location_str_.data(), sys->location_str_.size());
  success = success == PP_TRUE ? irequest->SetProperty(request,
						       PP_URLREQUESTPROPERTY_URL,
						       location)
    : success;
  vlc_ppapi_deref_var(location);
  success = success == PP_TRUE ? irequest->SetProperty(request,
						       PP_URLREQUESTPROPERTY_HEADERS,
						       headers_var)
    : success;
  vlc_ppapi_deref_var(headers_var);

  if(success != PP_TRUE) {
    vlc_subResReference(request);
    return VLC_EGENERIC;
  }

  PP_Resource loader = iloader->Create(sys->instance);
  if(loader == 0) {
    vlc_subResReference(request);
    return VLC_EGENERIC;
  }

  int32_t code = iloader->Open(loader, request, PP_BlockUntilComplete());
  if(code != PP_OK) {
    vlc_subResReference(request);
    vlc_subResReference(loader);
    return VLC_EGENERIC;
  }

  PP_Resource response = iloader->GetResponseInfo(loader);
  if(response == 0) {
    vlc_subResReference(request);
    vlc_subResReference(loader);
    return VLC_EGENERIC;
  }

  struct PP_Var status_code = iresponse->GetProperty(response, PP_URLRESPONSEPROPERTY_STATUSCODE);
  struct PP_Var headers = iresponse->GetProperty(response, PP_URLRESPONSEPROPERTY_HEADERS);
  assert(status_code.type == PP_VARTYPE_INT32);
  assert(headers.type == PP_VARTYPE_STRING);
  code = status_code.value.as_int;

  size_t header_len = 0;
  const char* header_str = ivar->VarToUtf8(headers, &header_len);
  std::vector<std::string> vheaders;
  size_t i = 0;
  for(size_t j = i; j < header_len; j = i) {
    for(; header_str[i] != '\n' && i < header_len; i++) {
    }
    if(j == i) { i++; continue; }
    size_t sub = 0;
    if(header_str[i - 1] == '\r') {
      sub += 1;
    }
    const size_t len = i - j - sub;
    if(len == 0) {
      break;
    }
    char* dbg_headers = (char*)alloca(len + 1);
    memset(dbg_headers, 0, len + 1);
    memmove(dbg_headers, &header_str[j], len);
    msg_Info(sys->access, "received header: `%s`", dbg_headers);
    vheaders.emplace_back(std::move(std::string(dbg_headers, len)));
  }

  int32_t ret;

  do {
    if(code == 200) {
      msg_Err(sys->access, "the server doesn't support range requests");
      // TODO
      ret = VLC_EGENERIC;
      break;
    } else if(code == 206) {

      sys->loader_.open(loader);

      const char mpb_content_type[] = "Content-Type: multipart/byteranges;";
      const size_t mpb_content_type_len = sizeof(mpb_content_type) - 1;
      const char* boundary;
      size_t boundary_len;
      sys->response_is_multipart = find_header(header_str, header_len,
                                               mpb_content_type, mpb_content_type_len,
                                               &boundary, &boundary_len);

      if(sys->response_is_multipart) {
        msg_Info(sys->access, "recieved multipart response");
        // we have to read the first multipart, and cache whatever we pull after
        // the headers.

        boundary += mpb_content_type_len;
        boundary_len -= mpb_content_type_len;
        if(boundary[0] == ' ') {
          boundary += 1;
          boundary_len -= 1;
        }
        const char boundary_prefix[] = "boundary=";
        const size_t boundary_prefix_len = sizeof(boundary_prefix) - 1;
        if(boundary_len <= boundary_prefix_len ||
           !case_insensitive_strncmp(boundary, boundary_prefix, boundary_len)) {
          msg_Err(sys->access, "Response multipart headers don't have a correct boundary string");
          ret = VLC_EGENERIC;
          break;
        } else {
          boundary += boundary_prefix_len;
          boundary_len -= boundary_prefix_len;

          sys->multi_part_boundary_ = std::move(std::string(boundary, boundary_len));
          msg_Info(sys->access, "multipart boundary: `%s`",
                   sys->multi_part_boundary_.c_str());
        }

        LoaderBuffer loader(sys);
        ret = loader.read_multi_part_headers();
        if(ret != VLC_SUCCESS) {
          break;
        }

        ret = loader.cache_all_rest_into_meta();
        break;
      } else {
        msg_Info(sys->access, "recieved single-part response");
        const char content_type[] = "Content-Type: ";
        const char content_range[] = "Content-Range: bytes"; // there is either
                                                             // a space or '='
                                                             // after this.

        const char* ct = NULL; size_t ct_len;
        const char* range_str = NULL; size_t range_str_len;

        const bool found_type = find_header(header_str, header_len,
                                            content_type, sizeof(content_type) - 1,
                                            &ct, &ct_len);
        const bool found_range = find_header(header_str, header_len,
                                             content_range, sizeof(content_range) - 1,
                                             &range_str, &range_str_len);
        if(!found_range) {
          msg_Err(sys->access, "Content-Range header missing");
          ret = VLC_EGENERIC;
          break;
        } else if(!found_type) {
          msg_Warn(sys->access, "Content-Type header missing");
        }

	range_str += sizeof(content_range); // we treat the null byte as a
                                            // placeholder for a space or '='
	ct += sizeof(content_type) - 1;

        uint64_t range_start, range_end;
        if(sscanf(range_str, "%llu-%llu/%*llu", &range_start, &range_end) != 2) {
          msg_Err(sys->access, "unknown Content-Range syntax");
          ret = VLC_EGENERIC;
          break;
        }

	PP_Resource file;
	int32_t code;
	if((code = open_cache_file(sys, next_ppapi_meta, &file)) != VLC_SUCCESS) {
	  ret = VLC_EGENERIC;
          break;
	}

	struct PP_FileInfo info;
	code = ifile_io->Query(file, &info, PP_BlockUntilComplete());
	if(code != PP_OK) {
	  ret = VLC_EGENERIC;
          break;
	}
        if(range_start != sys->cache_metas[next_ppapi_meta].start + info.size) {
          msg_Err(sys->access, "Content-Range's start doesn't match the requested start");
          ret = VLC_EGENERIC;
          break;
        }

        if(found_type) {
          sys->content_type_ = std::move(std::string(ct, ct_len));
        } else {
          sys->content_type_.clear();
        }

        sys->next_mpb_end = range_end;
	sys->ppapi_offset = range_start;

        ret = VLC_SUCCESS;
      }
    } else if(code == 416) {
      msg_Err(sys->access, "server responded with a 416 status code");
      ret = VLC_EGENERIC;
    } else {
      ret = VLC_EGENERIC;
    }
    break;
  } while(false);

  if(ret != VLC_SUCCESS) {
    sys->ppapi_meta = 0;

    sys->loader_.close();

    vlc_subResReference(request);
    vlc_subResReference(loader);
    vlc_subResReference(response);
  } else {
    assert(sys->loader_.is_open());

    sys->last_progress_time = mdate();
    sys->ppapi_meta = next_ppapi_meta;

    sys->request_info = request;
    sys->response_info = response;
  }
  vlc_ppapi_deref_var(headers);
  return ret;
}

static ssize_t Read(access_t* access, uint8_t* dest, size_t bytes) {
  access_sys_t* sys = (access_sys_t*)access->p_sys;

  download_and_cache(sys);

  const int64_t read = load_from_cache(sys, dest, bytes);

  sys->vlc_read_since_last_seek += (read > 0 ? read : 0);

  if(read == 0 || read < 0) {
    if(read < 0 && sys->loader_.is_open()) {
      msg_Err(access, "Read error: `%lli`", read);
      return (ssize_t)(-1);
    } else {
      return 0;
    }
  } else {
    return (ssize_t)read;
  }
}
static int Seek(access_t* access, uint64_t offset_from_start) {
  const vlc_ppapi_file_io_t*  ifile_io = vlc_getPPAPI_FileIO();

  access_sys_t* sys = (access_sys_t*)access->p_sys;
  msg_Info(access, "seeking to `%llu` of `%llu`\n", offset_from_start, sys->file_size);
  /*printf("sys->vlc_offset = `%llu`, sys->vlc_last_seek_offset = `%llu`, "
         "sys->vlc_read_since_last_seek = `%llu`\n",
         sys->vlc_offset, sys->vlc_last_seek_offset,
         sys->vlc_read_since_last_seek);*/

  download_and_cache(sys);

  if(sys->file_size <= offset_from_start) {
    // handle past EOF seeking.
    sys->vlc_meta = sys->cache_count - 1;
    goto success;
  }

  int32_t code;

  for(size_t i = 0; i <= sys->cache_count; i++) {
    if(i < sys->cache_count && sys->cache_metas[i].start == offset_from_start) {
      /*msg_Info(access, "(case 1) seeking to %llu bytes (cache_meta_t[%u] { id = `%llu`, start = `%llu` })",
        offset_from_start, i, sys->cache_metas[i].id, sys->cache_metas[i].start);*/

      if(sys->ppapi_meta != i || !sys->loader_.is_open()) {
	close_loader(sys);

	if((code = new_download(sys, i)) != VLC_SUCCESS) { goto error; }
      }

      sys->vlc_meta = i;

      goto success;
    } else if(i == sys->cache_count || sys->cache_metas[i].start > offset_from_start) {
      size_t previous, next;
      if(i == sys->cache_count) {
        // this may seem like an OBO error, but its not!
        next = i;
	i -= 1;
        previous = i;
      } else {
        next = i;
        previous = i - 1;
      }

      PP_Resource file;
      if((code = open_cache_file(sys, previous, &file)) != VLC_SUCCESS) {
        code = VLC_EGENERIC;
        goto error;
      }

      struct PP_FileInfo info;
      code = ifile_io->Query(file, &info, PP_BlockUntilComplete());
      if(code != PP_OK) { code = VLC_EGENERIC; goto error; }

      if(sys->cache_metas[previous].start + info.size < offset_from_start) {
	// the previous cache file ends before offset_from_start
        const cache_meta_t new_meta = {
          sys->next_meta_id++,
          offset_from_start
        };
        if((code = add_meta(sys, new_meta, next)) != VLC_SUCCESS) { goto error; }

	close_loader(sys);

	if((code = new_download(sys, next)) != VLC_SUCCESS) { goto error; }

        sys->vlc_meta = next;
      } else {
	// the cache file covers the point to which we seek.
	if(sys->ppapi_meta != previous || !sys->loader_.is_open()) {
	  close_loader(sys);

	  if((code = new_download(sys, previous)) != VLC_SUCCESS) { goto error; }
	}
        sys->vlc_meta = previous;
      }

      /*msg_Info(access, "(case 2) seeking to %llu bytes (cache_meta_t[%u] { id = `%llu`, start = `%llu` })",
	       offset_from_start, sys->ppapi_meta, sys->cache_metas[sys->ppapi_meta].id,
               sys->cache_metas[sys->ppapi_meta].start);
      msg_Info(access, "sys->vlc_meta = `%u`, sys->ppapi_meta = `%u`",
      sys->vlc_meta, sys->ppapi_meta);*/

      goto success;
    }
  }

  vlc_assert_unreachable();

 success:
  sys->vlc_last_seek_offset = sys->vlc_offset = offset_from_start;
  sys->vlc_read_since_last_seek = 0;

  msg_Info(sys->access, "vlc_meta = %u, vlc_offset = %llu\n",
           sys->vlc_meta, sys->vlc_offset);
  msg_Info(sys->access, "ppapi_meta = %u, ppapi_offset = %llu\n",
           sys->ppapi_meta, sys->ppapi_offset);

  sys->access->info.b_eof = offset_from_start < sys->file_size;

  return VLC_SUCCESS;

 error:
  msg_Err(sys->access, "error seeking: `%i`\n", code);
  /*printf("sys->vlc_meta = %u, sys->vlc_offset = %llu\n", sys->vlc_meta, sys->vlc_offset);
  printf("sys->ppapi_meta = %u, sys->ppapi_offset = %llu\n"
         "\tsys->ppapi_bytes_downloaded = %llu\n",
         sys->ppapi_meta, sys->ppapi_offset, sys->ppapi_bytes_downloaded);*/

  return code;
}
static int Control(access_t* access, int query, va_list args) {
  access_sys_t* sys = (access_sys_t*)access->p_sys;
  switch (query) {
  case ACCESS_CAN_SEEK:
    // XXX enabling this causes corruption:
    //case ACCESS_CAN_FASTSEEK:
  case ACCESS_CAN_CONTROL_PACE: // XXX required to be true.
  case ACCESS_CAN_PAUSE: {
    bool* ret = va_arg(args, bool*);
    *ret = true;
    break;
  }

  case ACCESS_CAN_FASTSEEK: {
    bool* ret = va_arg(args, bool*);
    *ret = false;
    break;
  }

  case ACCESS_GET_PTS_DELAY: {
    int64_t* delay = va_arg(args, int64_t*);
    *delay = INT64_C(1000) * var_InheritInteger(access, "network-caching");
    break;
  }

  case ACCESS_GET_SIZE: {
    int64_t* size = va_arg(args, int64_t*);
    *size = (int64_t)sys->file_size;
    break;
  }
  case ACCESS_GET_CONTENT_TYPE: {
    if(sys->content_type_.empty()) { return VLC_EGENERIC; }

    char** dest = va_arg(args, char**);
    *dest = (char*)malloc(sys->content_type_.size() + 1);
    memmove(*dest, sys->content_type_.data(), sys->content_type_.size());
    (*dest)[sys->content_type_.size()] = '\0';
    break;
  }
  case ACCESS_SET_PAUSE_STATE: { break; }
  default:
    return VLC_EGENERIC;
  }

  return VLC_SUCCESS;
}

static int discover_file_size(access_t* access, PP_Instance instance, const char* url, uint64_t *file_size) {
  const vlc_ppapi_url_loader_t* iloader = vlc_getPPAPI_URLLoader();
  const vlc_ppapi_url_request_info_t* irequest = vlc_getPPAPI_URLRequestInfo();
  const vlc_ppapi_url_response_info_t* iresponse = vlc_getPPAPI_URLResponseInfo();
  const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();

  VLC_PPAPI_STATIC_STR(head_method, "HEAD");
  PP_Resource request = 0, loader = 0, response = 0;
  PP_Var headers = PP_MakeUndefined();

  const char content_length[] = "content-length";

  const char* cl; size_t cl_len;

  bool found;
  const char* header_str; size_t header_len;

  PP_Var status_code; PP_Var location;
  PP_Bool success; int32_t code;

  request = irequest->Create(instance);
  if(request == 0) { goto error; }
  loader = iloader->Create(instance);
  if(loader == 0) { goto error; }


  success = irequest->SetProperty(request, PP_URLREQUESTPROPERTY_METHOD,
				  vlc_ppapi_mk_str(&head_method));
  location = vlc_ppapi_cstr_to_var(url, strlen(url));
  if(success == PP_TRUE) {
    success = irequest->SetProperty(request, PP_URLREQUESTPROPERTY_URL,
				    location);
  }
  vlc_ppapi_deref_var(location);

  if(success != PP_TRUE) { goto error; }

  code = iloader->Open(loader, request, PP_BlockUntilComplete());
  if(code != PP_OK) { goto error; }

  response = iloader->GetResponseInfo(loader);
  if(response == 0) { goto error; }

  status_code = iresponse->GetProperty(response, PP_URLRESPONSEPROPERTY_STATUSCODE);
  assert(status_code.type == PP_VARTYPE_INT32);
  code = status_code.value.as_int;

  if(code != 200) {
    msg_Err(access, "couldn't retrieve file size: server returned `%i`",
	    code);
    goto error;
  }

  headers = iresponse->GetProperty(response, PP_URLRESPONSEPROPERTY_HEADERS);
  assert(headers.type == PP_VARTYPE_STRING);

  header_str = ivar->VarToUtf8(headers, &header_len);


  found = find_header(header_str, header_len,
                      content_length, sizeof(content_length) - 1,
                      &cl, &cl_len);

  if(!found || sscanf(cl + sizeof(content_length) - 1, ": %llu", file_size) <= 0) {
    msg_Err(access, "server response to HEAD is missing the Content-Length header");
    goto error;
  }

  if(request != 0) {
    vlc_subResReference(request);
  }
  if(response != 0) {
    vlc_subResReference(response);
  }
  if(loader != 0) {
    vlc_subResReference(request);
  }
  vlc_ppapi_deref_var(headers);
  return VLC_SUCCESS;

 error:
  if(request != 0) {
    vlc_subResReference(request);
  }
  if(response != 0) {
    vlc_subResReference(response);
  }
  if(loader != 0) {
    vlc_subResReference(request);
  }
  vlc_ppapi_deref_var(headers);
  return VLC_EGENERIC;
}

// lifted from vlc_arrays.h, modified for our use.
/* This function is not intended to be crypto-secure, we only want it to be
 * fast and not suck too much. This one is pretty fast and did 0 collisions
 * in wenglish's dictionary.
 */
static inline uint64_t DictHash2(const char *psz_string)
{
    uint64_t i_hash = 0;
    if( psz_string )
    {
        while( *psz_string )
        {
            i_hash += *psz_string++;
            i_hash += i_hash << 10;
            i_hash ^= i_hash >> 8;
        }
    }
    return i_hash;
}

static int Open(vlc_object_t* obj) {
  access_t* access = (access_t*)obj;

  // we can be openned at any time, so we must search up
  PP_Instance instance = 0;
  vlc_value_t val;
  if(var_GetChecked(access->p_libvlc, "ppapi-instance", VLC_VAR_INTEGER, &val) != VLC_ENOVAR) {
    instance = (PP_Instance)val.i_int;
  }
  if(instance == 0) {
    msg_Err(access, "couldn't get a reference to the PPAPI instance");
    return VLC_EGENERIC;
  }

  std::unique_ptr<access_sys_t> sys(new (std::nothrow) access_sys_t);
  if(sys == nullptr) {
    return VLC_ENOMEM;
  }

  // We need a message loop so we can call the PPAPI functions.
  const vlc_ppapi_message_loop_t* imsg_loop = vlc_getPPAPI_MessageLoop();
  PP_Resource msg_loop = imsg_loop->Create(instance);
  if(msg_loop == 0) {
    msg_Err(access, "failed to create message loop");
    return VLC_EGENERIC;
  }
  {
    const int32_t code = imsg_loop->AttachToCurrentThread(msg_loop);
    if(code != PP_OK) {
      msg_Err(access, "error attaching thread to message loop: `%d`", code);
      return VLC_EGENERIC;
    }
  }

  msg_Dbg(access, "attempting to open PPB_URLLoader");

  const PP_Resource cache_fs = get_temp_fs(instance);
  if(cache_fs == 0) {
    msg_Err(access, "couldn't open root file system");
    vlc_subResReference(msg_loop);
    return VLC_EGENERIC;
  }
  const vlc_ppapi_file_io_t*  ifile_io = vlc_getPPAPI_FileIO();
  const vlc_ppapi_file_ref_t* ifile_ref = vlc_getPPAPI_FileRef();

  {
    // ensure /vlc-cache exists:
    const PP_Resource cache_dir = ifile_ref->Create(cache_fs, "/vlc-cache");
    if(cache_dir == 0) {
      msg_Err(access, "error while create cache dir file_ref");
      vlc_subResReference(msg_loop);
      return VLC_EGENERIC;
    }

    const int32_t create_flags = PP_MAKEDIRECTORYFLAG_WITH_ANCESTORS;
    int32_t code = ifile_ref->MakeDirectory(cache_dir, create_flags, PP_BlockUntilComplete());
    vlc_subResReference(cache_dir);
    if(code != PP_OK) {
      msg_Err(access, "error while creating the `/vlc-cache` directory");
      vlc_subResReference(msg_loop);
      return VLC_EGENERIC;
    }
  }

  std::string location_str;
  {
    char* c_str = NULL;
    asprintf(&c_str, "%s://%s",
             access->psz_access,
             access->psz_location);
    location_str = std::move(std::string(c_str));
    free(c_str);
  }

  const uint64_t cache_basename = DictHash2(location_str.c_str());

  char* meta_cache_path = NULL;
  asprintf(&meta_cache_path, "/vlc-cache/%llu.meta", cache_basename);
  msg_Info(access, "opening ppapi-access: %s", location_str.c_str());
  msg_Info(access, "ppapi-access cache file: %s", meta_cache_path);
  const PP_Resource cache_file_ref = ifile_ref->Create(cache_fs, meta_cache_path);
  free(meta_cache_path);

  if(cache_file_ref == 0) {
    vlc_subResReference(msg_loop);
    return VLC_EGENERIC;
  }

  PP_Resource cache_file_io = ifile_io->Create(instance);
  if(cache_file_io == 0) {
    vlc_subResReference(msg_loop);
    return VLC_EGENERIC;
  }

  int32_t code = ifile_io->Open(cache_file_io, cache_file_ref,
                                PP_FILEOPENFLAG_READ,
                                PP_BlockUntilComplete());
  if(code != PP_OK && code != PP_ERROR_FILENOTFOUND) {
    vlc_subResReference(cache_file_io);
    vlc_subResReference(msg_loop);
    return VLC_EGENERIC;
  }

  size_t data_cache_count = 0;
  uint64_t next_meta_id = 0;
  PP_Resource*  cache_files = NULL;
  cache_meta_t* cache_metas = NULL;

  bool do_create_meta_file = code == PP_ERROR_FILENOTFOUND;

  if(!do_create_meta_file) {
    struct PP_FileInfo info;
    code = ifile_io->Query(cache_file_io, &info, PP_BlockUntilComplete());
    if(code != PP_OK) {
      vlc_subResReference(cache_file_io);
      vlc_subResReference(cache_file_ref);
      vlc_subResReference(msg_loop);
      return VLC_EGENERIC;
    }

    code = ifile_io->Read(cache_file_io, 0, (char*)&data_cache_count,
                          sizeof(data_cache_count), PP_BlockUntilComplete());
    const uint64_t total_size = data_cache_count * sizeof(cache_meta_t) +
      sizeof(data_cache_count) + sizeof(next_meta_id);

    if((size_t)code < sizeof(data_cache_count) || total_size != (uint64_t)info.size) {
      do_create_meta_file = true;
    }
  }

  if(!do_create_meta_file) {
    code = ifile_io->Read(cache_file_io, sizeof(data_cache_count), (char*)&next_meta_id,
                          sizeof(next_meta_id), PP_BlockUntilComplete());
    if(code != sizeof(next_meta_id)) {
      msg_Warn(access, "malformed cache meta file, ignoring it");
      do_create_meta_file = true;
    }
  }
  if(!do_create_meta_file) {
    if(data_cache_count != 0) {
      cache_metas = (cache_meta_t*)calloc(data_cache_count, sizeof(cache_meta_t));
      cache_files = (PP_Resource*)calloc(data_cache_count, sizeof(PP_Resource));
      if(cache_files == NULL || cache_metas == NULL) {
        if(cache_files != NULL) {
          free(cache_files);
        }
        if(cache_metas != NULL) {
          free(cache_metas);
        }
	vlc_subResReference(cache_file_io);
	vlc_subResReference(cache_file_ref);
	vlc_subResReference(msg_loop);
        return VLC_ENOMEM;
      }
    }

    code = ifile_io->Read(cache_file_io, sizeof(data_cache_count) + sizeof(next_meta_id),
                          (char*)cache_metas, sizeof(cache_meta_t) * data_cache_count,
                          PP_BlockUntilComplete());
    if((size_t)code != sizeof(cache_meta_t) * data_cache_count) {
      msg_Warn(access, "malformed cache meta file, ignoring it");
      do_create_meta_file = true;
    }
  }

  // get the file size:
  uint64_t file_size;
  if(discover_file_size(access, instance, location_str.c_str(), &file_size) != VLC_SUCCESS) {
    msg_Err(access, "failed to get the remote file size");
    if(cache_files != NULL) free(cache_files);
    if(cache_metas != NULL) free(cache_metas);
    vlc_subResReference(cache_file_ref);
    vlc_subResReference(msg_loop);
    return VLC_EGENERIC;
  }

  if(!do_create_meta_file) {
    const vlc_ppapi_var_t* ivar = vlc_getPPAPI_Var();
    // sanity check.
    VLC_PPAPI_ARRAY_OUTPUT(entries_out, output);

    const PP_Resource cache_dir = ifile_ref->Create(get_temp_fs(instance), "/vlc-cache");
    assert(cache_dir != 0);

    code = ifile_ref->ReadDirectoryEntries(cache_dir, output, PP_BlockUntilComplete());
    vlc_subResReference(cache_dir);
    if(code != PP_OK) {
      return VLC_EGENERIC;
    }

    PP_DirectoryEntry* enties = static_cast<PP_DirectoryEntry*>(entries_out.data);
    const size_t entry_len = entries_out.elements;

    assert(entry_len != 0);

    char* filename_buffer = (char*)alloca(MAX_CACHE_PATH);

    std::map<size_t, PP_Resource> files;

    for(size_t i = 0; i < entry_len; i++) {
      const PP_Resource file = enties[i].file_ref;
      if(enties[i].file_type != PP_FILETYPE_REGULAR || do_create_meta_file) {
        vlc_subResReference(file);
        continue;
      }

      size_t name_len = 0;
      {
        PP_Var name = ifile_ref->GetName(file);
        const char* name_str = ivar->VarToUtf8(name, &name_len);
        assert(name_len + 1 < MAX_CACHE_PATH);
        memset(filename_buffer, 0, MAX_CACHE_PATH);
        memmove((void*)filename_buffer, (void*)name_str, name_len);
        vlc_ppapi_deref_var(name);
      }

      const char meta_ext[] = "meta";
      const size_t meta_ext_len = sizeof(meta_ext) - 1;
      if(strcmp(filename_buffer + name_len - meta_ext_len, meta_ext) == 0) {
        vlc_subResReference(file);
        continue;
      }

      const char data_ext[] = "data";
      const size_t data_ext_len = sizeof(data_ext) - 1;
      if(strcmp(filename_buffer + name_len - data_ext_len, data_ext) != 0) {
        vlc_subResReference(file);
        continue;
      }

      uint64_t access_id = 0;
      size_t meta_id = 0;
      const auto scanned = sscanf(filename_buffer, "%llu.%zu.data", &access_id, &meta_id);
      if(access_id != cache_basename || scanned != 2) {
        vlc_subResReference(file);
        continue;
      }

      if(files[meta_id] != 0) {
        do_create_meta_file = true;
      }

      files[meta_id] = file;
    }
    free(enties);

    size_t i = 0;
    for(const auto& file : files) {
      if(file.first != i++ || i > data_cache_count) {
        do_create_meta_file = true;
        break;
      }

      uint64_t max_size;
      if(i == data_cache_count) {
        max_size = file_size;
      } else {
        if(cache_metas[i].start > file_size) {
          do_create_meta_file = true;
          break;
        }
        max_size = cache_metas[i].start;
      }
      max_size -= cache_metas[i - 1].start;

      struct PP_FileInfo info;
      code = ifile_ref->Query(file.second, &info, PP_BlockUntilComplete());
      assert(code == PP_OK);

      if(info.size > (int64_t)max_size) {
        do_create_meta_file = true;
        break;
      }
    }

    if(do_create_meta_file) {
      msg_Warn(access, "detected invalid cache data; deleting");
    }

    for(const auto& file : files) {
      if(do_create_meta_file) {
        // delete the existing file
        delete_file_ref(access, file.second);
      }

      vlc_subResReference(file.second);
    }
  }

  vlc_subResReference(cache_file_io);

  if(do_create_meta_file) {
    if(cache_files != NULL) { free(cache_files); }
    if(cache_metas != NULL) { free(cache_metas); }

    cache_files = (PP_Resource*)calloc(1, sizeof(PP_Resource));
    if(cache_files == NULL) {
      vlc_subResReference(cache_file_ref);
      vlc_subResReference(msg_loop);
      return VLC_ENOMEM;
    }
    cache_metas = (cache_meta_t*)calloc(1, sizeof(cache_meta_t));
    if(cache_metas == NULL) {
      free(cache_files);
      vlc_subResReference(cache_file_ref);
      vlc_subResReference(msg_loop);
      return VLC_ENOMEM;
    }

    next_meta_id = 1;
    data_cache_count = 1;
  }

  ;
  if(sys == NULL) {
    free(cache_files);
    free(cache_metas);
    vlc_subResReference(cache_file_ref);
    vlc_subResReference(msg_loop);
    return VLC_ENOMEM;
  }

  sys->instance = instance;
  sys->message_loop = msg_loop;
  sys->cache_meta_fref = cache_file_ref;

  sys->request_info = sys->response_info = 0;

  sys->data_cache_basename = cache_basename;
  sys->data_caches = cache_files;
  sys->cache_metas = cache_metas;
  sys->cache_count = data_cache_count;

  sys->next_meta_id = next_meta_id;

  sys->ppapi_meta = 0;
  sys->ppapi_offset = 0;
  sys->vlc_meta = 0;
  sys->vlc_offset = 0;

  sys->file_size = file_size;

  sys->access = access;
  sys->location_str_ = std::move(location_str);

  code = new_download(sys.get(), 0);
  if(code != VLC_SUCCESS) {
    free(cache_files);
    free(cache_metas);
    vlc_subResReference(cache_file_ref);
    vlc_subResReference(msg_loop);
    return code;
  }

  access_InitFields(access);
  {
    access_t* p_access = access;
    ACCESS_SET_CALLBACKS( Read, NULL, Control, Seek );
  }
  access->p_sys = sys.release();

  return VLC_SUCCESS;
}
static void Close(vlc_object_t* obj) {
  access_t* access = (access_t*)obj;
  access_sys_t* sys = access->p_sys;

  if(sys->message_loop != 0) {
    vlc_getPPAPI_MessageLoop()->PostQuit(sys->message_loop, PP_TRUE);
    vlc_subResReference(sys->message_loop);
  }

  download_and_cache(sys);
  close_loader(sys);

  save_metas(sys);
  vlc_subResReference(sys->cache_meta_fref);

  if(sys->cache_metas != NULL) {
    free(sys->cache_metas);
  }
  if(sys->data_caches != NULL) {
    for(size_t i = 0; i < sys->cache_count; i++) {
      vlc_subResReference(sys->data_caches[i]);
    }
    free(sys->data_caches);
  }

  delete sys;

  return;
}
