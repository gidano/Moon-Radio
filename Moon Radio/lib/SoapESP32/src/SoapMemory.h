#pragma once

// Moon Radio: library-local, bounded PSRAM allocation. Never changes the
// Arduino String allocator or the allocation policy of other components.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace SoapMemory {
inline constexpr size_t budget = 512 * 1024;
inline std::atomic<size_t> used{0}, peak{0};
inline std::atomic<bool> failed{false};
inline std::atomic<uint32_t> failures{0};
inline void fail() { failed = true; failures.fetch_add(1); }
struct alignas(8) Header { size_t bytes; };
inline void* allocate(size_t bytes) {
  if (!bytes || bytes > budget) { fail(); return nullptr; }
  const size_t total = bytes + sizeof(Header);
  const size_t before = used.fetch_add(total);
  if (before + total > budget) {
    used.fetch_sub(total); fail(); return nullptr;
  }
  auto* h = static_cast<Header*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!h) { used.fetch_sub(total); fail(); return nullptr; }
  h->bytes = total;
  size_t old = peak.load();
  while (old < before + total && !peak.compare_exchange_weak(old, before + total)) {}
  return h + 1;
}
inline void release(void* p) {
  if (!p) return;
  auto* h = static_cast<Header*>(p) - 1;
  used.fetch_sub(h->bytes);
  heap_caps_free(h);
}
}

// Only the Arduino String operations used by SoapESP32/MiniXPath. A failed
// allocation is sticky for the whole operation, so truncated data is not used.
class SoapString {
 public:
  SoapString(const char* p = "") { assign(p, p ? strlen(p) : 0); }
  SoapString(const SoapString& s) { assign(s.c_str(), s.length()); }
  SoapString(SoapString&& s) noexcept { swap(s); }
  ~SoapString() { SoapMemory::release(data_); }
  SoapString& operator=(const SoapString& s) { if (this != &s) assign(s.c_str(), s.length()); return *this; }
  SoapString& operator=(SoapString&& s) noexcept { swap(s); return *this; }
  SoapString& operator=(const char* p) { assign(p, p ? strlen(p) : 0); return *this; }
  const char* c_str() const { return data_ ? data_ : ""; }
  size_t length() const { return length_; }
  bool isEmpty() const { return length_ == 0; }
  char operator[](size_t i) const { return i < length_ ? data_[i] : 0; }
  bool reserve(size_t n) {
    if (n <= capacity_) return true;
    if (n > 32768) { SoapMemory::fail(); return false; }
    size_t next = std::max(n, std::min<size_t>(32768, std::max<size_t>(64, capacity_ * 2)));
    auto* p = static_cast<char*>(SoapMemory::allocate(next + 1));
    if (!p) return false;
    memcpy(p, c_str(), length_ + 1);
    SoapMemory::release(data_); data_ = p; capacity_ = next;
    return true;
  }
  SoapString& operator+=(const char* p) {
    const size_t n = p ? strlen(p) : 0;
    const bool alias = data_ && reinterpret_cast<uintptr_t>(p) >= reinterpret_cast<uintptr_t>(data_) &&
                       reinterpret_cast<uintptr_t>(p) <= reinterpret_cast<uintptr_t>(data_ + length_);
    const size_t offset = alias ? p - data_ : 0;
    if (n && reserve(length_ + n)) { memmove(data_ + length_, alias ? data_ + offset : p, n + 1); length_ += n; }
    return *this;
  }
  SoapString& operator+=(const SoapString& s) { return *this += s.c_str(); }
  SoapString& operator+=(char c) { if (reserve(length_ + 1)) { data_[length_++] = c; data_[length_] = 0; } return *this; }
  friend SoapString operator+(SoapString s, const char* p) { s += p; return s; }
  friend SoapString operator+(SoapString s, const SoapString& p) { s += p; return s; }
  friend SoapString operator+(const char* p, const SoapString& s) { SoapString r(p); r += s; return r; }
  int indexOf(const char* p, size_t from = 0) const {
    if (!p || from > length_) return -1;
    const char* found = strstr(c_str() + from, p);
    return found ? static_cast<int>(found - c_str()) : -1;
  }
  int indexOf(char c, size_t from = 0) const { char p[2]{c, 0}; return indexOf(p, from); }
  bool startsWith(const char* p) const { return p && strncmp(c_str(), p, strlen(p)) == 0; }
  bool endsWith(const char* p) const { const size_t n = p ? strlen(p) : 0; return n <= length_ && strcmp(c_str() + length_ - n, p ? p : "") == 0; }
  SoapString substring(size_t from, size_t to = SIZE_MAX) const {
    SoapString out;
    if (from <= length_) out.assign(c_str() + from, std::min(to, length_) > from ? std::min(to, length_) - from : 0);
    return out;
  }
  void remove(size_t from) { if (from < length_) { length_ = from; data_[length_] = 0; } }
  void trim() {
    size_t from = 0, to = length_;
    while (from < to && isspace(static_cast<unsigned char>(data_[from]))) ++from;
    while (to > from && isspace(static_cast<unsigned char>(data_[to - 1]))) --to;
    if (data_) { memmove(data_, data_ + from, to - from); length_ = to - from; data_[length_] = 0; }
  }
  long toInt() const { return strtol(c_str(), nullptr, 10); }
  void replace(const char* from, const char* to) {
    const size_t n = from ? strlen(from) : 0;
    if (!n || indexOf(from) < 0) return;
    SoapString out; size_t at = 0;
    for (;;) {
      const int found = indexOf(from, at);
      if (found < 0) { out += c_str() + at; break; }
      out += substring(at, found); out += to; at = found + n;
    }
    swap(out);
  }
  void swap(SoapString& s) noexcept { std::swap(data_, s.data_); std::swap(length_, s.length_); std::swap(capacity_, s.capacity_); }
 private:
  void assign(const char* p, size_t n) {
    if (!n) { length_ = 0; if (data_) data_[0] = 0; return; }
    if (reserve(n)) { memmove(data_, p, n); length_ = n; data_[n] = 0; }
  }
  char* data_{nullptr}; size_t length_{0}, capacity_{0};
};

// No exceptions/abort on OOM: the caller checks SoapMemory::failed and refuses
// the operation. Hard bound also protects against servers ignoring RequestedCount.
template<class T> class SoapVector {
 public:
  SoapVector() = default;
  SoapVector(const SoapVector&) = delete;
  SoapVector& operator=(const SoapVector&) = delete;
  ~SoapVector() { reset(); }
  size_t size() const { return size_; }
  T& operator[](size_t i) { return data_[i]; }
  const T& operator[](size_t i) const { return data_[i]; }
  T* begin() { return data_; }
  T* end() { return data_ ? data_ + size_ : data_; }
  const T* begin() const { return data_; }
  const T* end() const { return data_ ? data_ + size_ : data_; }
  bool push_back(const T& value) {
    if (size_ == capacity_) {
      const size_t next = capacity_ ? capacity_ * 2 : 8;
      if (next > 64) { SoapMemory::fail(); return false; }
      auto* p = static_cast<T*>(SoapMemory::allocate(next * sizeof(T)));
      if (!p) return false;
      for (size_t i = 0; i < size_; ++i) { new (p + i) T(std::move(data_[i])); data_[i].~T(); }
      SoapMemory::release(data_); data_ = p; capacity_ = next;
    }
    new (data_ + size_++) T(value); return !SoapMemory::failed.load();
  }
  void clear() { while (size_) data_[--size_].~T(); }
  void pop_back() { if (size_) data_[--size_].~T(); }
  void drop_front() {
    if (!size_) return;
    data_[0].~T();
    for (size_t i = 1; i < size_; ++i) { new (data_ + i - 1) T(std::move(data_[i])); data_[i].~T(); }
    --size_;
  }
  void reset() { clear(); SoapMemory::release(data_); data_ = nullptr; capacity_ = 0; }
  void swap(SoapVector& s) noexcept { std::swap(data_, s.data_); std::swap(size_, s.size_); std::swap(capacity_, s.capacity_); }
 private:
  T* data_{nullptr}; size_t size_{0}, capacity_{0};
};
