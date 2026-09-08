#pragma once

#include <Arduino.h>
#include <cstdint>
#include <vector>

class PlaylistService {
 public:
  ~PlaylistService();

  bool resolve(const String& stationUrl, String& playUrl, String& title);
  bool step(int delta, String& playUrl, String& title);
  void reset();

  bool active() const;
  size_t count() const;
  int index() const;

  static bool isM3uUrl(const String& url);
  static String titleFromUrl(const String& url);

 private:
  bool load(const String& url);
  bool ensureCapacity(size_t required);
  const char* urlAt(size_t index) const;
  static String percentDecode(const String& value);

  String sourceUrl_;
  char* playlistData_{nullptr};
  size_t playlistLength_{0};
  size_t playlistCapacity_{0};
  std::vector<uint32_t> urlOffsets_;
  int index_{-1};
};
