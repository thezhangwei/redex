/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define CHECK(cond, ...)                        \
  do {                                          \
    auto cond_eval = (cond);                    \
    if (!cond_eval) {                           \
      fprintf(stderr, "%s:%d CHECK(%s) failed.",\
              __FILE__,                         \
              __LINE__,                         \
              #cond);                           \
      fprintf(stderr, " " __VA_ARGS__);         \
      fprintf(stderr, "\n");                    \
    }                                           \
    assert(cond_eval);                          \
  } while (0)

#define UNCOPYABLE(klass)       \
  klass(const klass&) = delete; \
  klass& operator=(const klass&) = delete;

#define MOVABLE(klass)      \
  klass(klass&&) = default; \
  klass& operator=(klass&&) = default;

template <typename T1, typename T2, typename L>
static void foreach_pair(const T1& t1, const T2& t2, const L& fn) {
  CHECK(t1.size() == t2.size());
  for (typename T1::size_type i = 0; i < t1.size(); i++) {
    fn(t1[i], t2[i]);
  }
}

template <uint32_t Width>
uint32_t align(uint32_t in) {
  return (in + (Width - 1)) & -Width;
}

inline uint32_t align(uint32_t width, uint32_t in) {
  return (in + (width - 1)) & -width;
}

template <uint32_t Width>
bool is_aligned(uint32_t in) {
  return (in & (Width - 1)) == 0;
}

template <typename T>
inline T nextPowerOfTwo(T in) {
  // Turn off all but msb.
  while ((in & (in - 1u)) != 0) {
    in &= in - 1u;
  }
  return in << 1u;
}

template <typename T>
inline T countSetBits(T in) {
  // Turn off all but msb.
  if (in == 0) { return 0; }
  int count = 1;
  while ((in & (in - 1u)) != 0) {
    in &= in - 1u;
    count++;
  }
  return count;
}


struct ConstBuffer {
  const char* ptr;
  size_t len;

  ConstBuffer slice(size_t new_begin) const { return slice(new_begin, len); }

  ConstBuffer truncate(size_t new_len) const {
    CHECK(new_len <= len);
    return ConstBuffer{ptr, new_len};
  }

  ConstBuffer slice(size_t new_begin, size_t new_end) const {
    CHECK(new_end <= len);
    CHECK(new_begin <= new_end);
    auto new_len = new_end - new_begin;
    return ConstBuffer{ptr + new_begin, new_len};
  }

  const char& operator[](size_t n) const {
    CHECK(n < len);
    return ptr[n];
  }
};

class FileHandle {
public:
  explicit FileHandle(FILE* fh) : bytes_written_(0), seek_ref_(0), fh_(fh) {}
  UNCOPYABLE(FileHandle);

  FILE* get() const { return fh_; }

  virtual ~FileHandle() {
    if (fh_ != nullptr) {
      fclose(fh_);
      fh_ = nullptr;
    }
  }

  FileHandle& operator=(FileHandle&& other) {
    bytes_written_ = other.bytes_written_;
    seek_ref_ = other.seek_ref_;
    fh_ = other.fh_;
    other.fh_ = nullptr;
    return *this;
  }

  FileHandle(FileHandle&& other) {
    bytes_written_ = other.bytes_written_;
    seek_ref_ = other.seek_ref_;
    fh_ = other.fh_;
    other.fh_ = nullptr;
  }

  size_t bytes_written() const { return bytes_written_; }
  void reset_bytes_written() { bytes_written_ = 0; }

  virtual size_t fwrite(const void* p, size_t size, size_t count);
  size_t fread(void* ptr, size_t size, size_t count);

  template <typename T>
  std::unique_ptr<T> read_object() {
    auto ret = std::unique_ptr<T>(new T);
    if (this->fread(ret.get(), sizeof(T), 1) != 1) {
      return std::unique_ptr<T>(nullptr);
    } else {
      return ret;
    }
  }

  bool feof();
  bool ferror();

  bool seek_set(long offset);
  bool seek_begin() { return seek_set(0); }
  bool seek_end();

  // Adjust the offset from which seek_set(N) is computed. Keeps oat-writing
  // code much cleaner by hiding the elf file .rodata offset from the oat code.
  void set_seek_reference_to_fpos();
  void set_seek_reference(long offset);

protected:
  size_t fwrite_impl(const void* p, size_t size, size_t count);
  virtual void flush() {}
  size_t bytes_written_;

  // seek_set() operates relative to this point.
  long seek_ref_;

private:
  FILE* fh_;
};

class Adler32 {
public:
  Adler32() = default;
  UNCOPYABLE(Adler32);
  MOVABLE(Adler32);

  void update(const void* _data, size_t len) {
    auto data = reinterpret_cast<const uint8_t*>(_data);
    for (size_t i = 0; i < len; i++) {
      a = (a + data[i]) % kModAdler;
      b = (b + a) % kModAdler;
    }
  }

  void update(ConstBuffer buf) {
    update(buf.ptr, buf.len);
  }

  uint32_t get() const {
    return (b << 16) | a;
  }

  static uint32_t compute(ConstBuffer buf) {
    Adler32 adler;
    adler.update(buf.ptr, buf.len);
    return adler.get();
  }

private:
  static constexpr int kModAdler = 65521;

  uint32_t a = 1;
  uint32_t b = 0;
};

class ChecksummingFileHandle : public FileHandle {
public:
  static constexpr size_t kBufSize = 50 * 1024;

  ChecksummingFileHandle(FILE* fh, Adler32 checksum)
    : FileHandle(fh), cksum_(std::move(checksum)),
    buffer_(new char[kBufSize]), buf_pos_(0) {}

  ChecksummingFileHandle(FileHandle fh, Adler32 checksum)
    : FileHandle(std::move(fh)), cksum_(std::move(checksum)),
    buffer_(new char[kBufSize]), buf_pos_(0) {}

  virtual ~ChecksummingFileHandle() {
    if (buffer_.get() != nullptr) {
      flush();
    }
  }

  size_t fwrite(const void* p, size_t size, size_t count) override;

  const Adler32& cksum() {
    flush();
    return cksum_;
  }

private:
  void flush() override;
  size_t flush_write(const char* p, size_t size, size_t count);

  Adler32 cksum_;
  std::unique_ptr<char[]> buffer_;
  size_t buf_pos_;
};

void write_word(FileHandle& fh, uint32_t value);

void write_buf(FileHandle& fh, ConstBuffer buf);
void write_padding(FileHandle& fh, char byte, size_t num);

template <typename T>
void write_obj(FileHandle& fh, const T& obj) {
  write_buf(fh, ConstBuffer { reinterpret_cast<const char*>(&obj), sizeof(T) });
}

template <typename T>
void write_vec(FileHandle& fh, const std::vector<T>& obj) {
  write_buf(fh, ConstBuffer { reinterpret_cast<const char*>(obj.data()),
                              obj.size() * sizeof(T) });
}

void write_str_and_null(FileHandle& fh, const std::string& str);
void write_str(FileHandle& fh, const std::string& str);
void stream_file(FileHandle& in, FileHandle& out);

size_t get_filesize(FileHandle& fh);

inline uint32_t read_uleb128(char** _ptr) {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(*_ptr);
  uint32_t result = *(ptr++);

  if (result > 0x7f) {
    int cur = *(ptr++);
    result = (result & 0x7f) | ((cur & 0x7f) << 7);
    if (cur > 0x7f) {
      cur = *(ptr++);
      result |= (cur & 0x7f) << 14;
      if (cur > 0x7f) {
        cur = *(ptr++);
        result |= (cur & 0x7f) << 21;
        if (cur > 0x7f) {
          cur = *(ptr++);
          result |= cur << 28;
        }
      }
    }
  }
  *_ptr = reinterpret_cast<char*>(ptr);
  return result;
}
