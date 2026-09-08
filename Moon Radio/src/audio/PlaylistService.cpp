#include "PlaylistService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr size_t kInitialPlaylistCapacity = 4096;
constexpr size_t kMaximumPlaylistSize = 512 * 1024;
constexpr uint32_t kPlaylistReadTimeoutMs = 8000;

int hexValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

String stripTrackPrefix(String value) {
  value.trim();
  while (value.length() >= 3 && isDigit(value[0]) && isDigit(value[1]) &&
         (value[2] == ' ' || value[2] == '-' || value[2] == '.' ||
          value[2] == '_')) {
    value.remove(0, 3);
    value.trim();
  }
  return value;
}

}  // namespace

PlaylistService::~PlaylistService() { reset(); }

bool PlaylistService::ensureCapacity(size_t required) {
  if (required <= playlistCapacity_) return true;
  if (required > kMaximumPlaylistSize + 1) return false;

  size_t newCapacity =
      playlistCapacity_ == 0 ? kInitialPlaylistCapacity : playlistCapacity_;
  while (newCapacity < required) {
    const size_t grown = newCapacity * 2;
    newCapacity = std::min(grown, kMaximumPlaylistSize + 1);
    if (newCapacity < required && newCapacity == kMaximumPlaylistSize + 1) {
      return false;
    }
  }

  void* resized = playlistData_
                      ? heap_caps_realloc(playlistData_, newCapacity,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                      : heap_caps_malloc(newCapacity,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!resized) return false;

  playlistData_ = static_cast<char*>(resized);
  playlistCapacity_ = newCapacity;
  return true;
}

const char* PlaylistService::urlAt(size_t index) const {
  if (!playlistData_ || index >= urlOffsets_.size()) return "";
  return playlistData_ + urlOffsets_[index];
}

bool PlaylistService::isM3uUrl(const String& input) {
  String url = input;
  int cut = url.indexOf('?');
  if (cut >= 0) url.remove(cut);
  cut = url.indexOf('#');
  if (cut >= 0) url.remove(cut);
  url.toLowerCase();
  return url.endsWith(".m3u");
}

String PlaylistService::percentDecode(const String& value) {
  String decoded;
  decoded.reserve(value.length());
  for (size_t index = 0; index < value.length(); ++index) {
    const char current = value[index];
    if (current == '%' && index + 2 < value.length()) {
      const int high = hexValue(value[index + 1]);
      const int low = hexValue(value[index + 2]);
      if (high >= 0 && low >= 0) {
        decoded += static_cast<char>((high << 4) | low);
        index += 2;
        continue;
      }
    }
    decoded += current == '+' ? ' ' : current;
  }
  return decoded;
}

String PlaylistService::titleFromUrl(const String& input) {
  String url = input;
  const int query = url.indexOf('?');
  if (query >= 0) url.remove(query);

  const int scheme = url.indexOf("://");
  const int pathStart = scheme >= 0 ? url.indexOf('/', scheme + 3) : 0;
  String path = pathStart >= 0 ? url.substring(pathStart + 1) : url;
  path = percentDecode(path);

  const int slash = path.lastIndexOf('/');
  String file = slash >= 0 ? path.substring(slash + 1) : path;
  String parent;
  if (slash > 0) {
    const int previousSlash = path.lastIndexOf('/', slash - 1);
    parent = previousSlash >= 0
                 ? path.substring(previousSlash + 1, slash)
                 : path.substring(0, slash);
  }

  const int extension = file.lastIndexOf('.');
  if (extension > 0) file.remove(extension);
  file = stripTrackPrefix(file);
  file.replace('_', ' ');

  if (!parent.isEmpty() && file.indexOf(" - ") < 0) {
    parent.replace('_', ' ');
    return parent + " — " + file;
  }
  return file;
}

bool PlaylistService::load(const String& url) {
  reset();

  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(4000);
  http.setTimeout(8000);
  if (!http.begin(url)) return false;

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[playlist] HTTP hiba: %d\n", code);
    http.end();
    return false;
  }

  const int announcedLength = http.getSize();
  if (announcedLength > static_cast<int>(kMaximumPlaylistSize)) {
    Serial.printf("[playlist] M3U tul nagy: %d byte\n", announcedLength);
    http.end();
    return false;
  }

  const size_t wantedCapacity =
      announcedLength > 0 ? static_cast<size_t>(announcedLength) + 1
                          : kInitialPlaylistCapacity;
  if (!ensureCapacity(wantedCapacity)) {
    Serial.println("[playlist] PSRAM foglalasi hiba");
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint32_t lastDataAt = millis();
  while (http.connected() &&
         (announcedLength < 0 ||
          playlistLength_ < static_cast<size_t>(announcedLength))) {
    const int available = stream->available();
    if (available <= 0) {
      if (millis() - lastDataAt > kPlaylistReadTimeoutMs) {
        Serial.println("[playlist] M3U letoltesi idotullepes");
        http.end();
        reset();
        return false;
      }
      delay(1);
      continue;
    }

    size_t requested = static_cast<size_t>(available);
    if (announcedLength >= 0) {
      requested =
          std::min(requested,
                   static_cast<size_t>(announcedLength) - playlistLength_);
    }
    if (!ensureCapacity(playlistLength_ + requested + 1)) {
      Serial.println("[playlist] M3U PSRAM tarhely elfogyott");
      http.end();
      reset();
      return false;
    }

    const int received =
        stream->read(reinterpret_cast<uint8_t*>(playlistData_ + playlistLength_),
                     requested);
    if (received > 0) {
      playlistLength_ += static_cast<size_t>(received);
      lastDataAt = millis();
    }
  }
  http.end();

  if (playlistLength_ == 0) {
    reset();
    return false;
  }
  playlistData_[playlistLength_] = '\0';

  urlOffsets_.clear();
  urlOffsets_.reserve(256);
  size_t lineStart = 0;
  for (size_t pos = 0; pos <= playlistLength_; ++pos) {
    if (pos != playlistLength_ && playlistData_[pos] != '\n') continue;

    playlistData_[pos] = '\0';
    size_t begin = lineStart;
    while (begin < pos &&
           (playlistData_[begin] == ' ' || playlistData_[begin] == '\t' ||
            playlistData_[begin] == '\r')) {
      ++begin;
    }
    size_t end = pos;
    while (end > begin &&
           (playlistData_[end - 1] == ' ' ||
            playlistData_[end - 1] == '\t' ||
            playlistData_[end - 1] == '\r')) {
      playlistData_[--end] = '\0';
    }

    const char* line = playlistData_ + begin;
    if (strncmp(line, "http://", 7) == 0 ||
        strncmp(line, "https://", 8) == 0) {
      urlOffsets_.push_back(static_cast<uint32_t>(begin));
    }
    lineStart = pos + 1;
  }

  if (urlOffsets_.empty()) {
    reset();
    return false;
  }
  sourceUrl_ = url;
  index_ = 0;
  Serial.printf("[playlist] %u zeneszam, %u byte PSRAM-ban\n",
                static_cast<unsigned>(urlOffsets_.size()),
                static_cast<unsigned>(playlistLength_));
  return true;
}

bool PlaylistService::resolve(const String& stationUrl, String& playUrl,
                              String& title) {
  if (!isM3uUrl(stationUrl)) {
    reset();
    playUrl = stationUrl;
    title = "";
    return true;
  }

  if (sourceUrl_ != stationUrl || urlOffsets_.empty() || index_ < 0) {
    if (!load(stationUrl)) return false;
  }

  playUrl = urlAt(index_);
  title = titleFromUrl(playUrl);
  return true;
}

bool PlaylistService::step(int delta, String& playUrl, String& title) {
  if (urlOffsets_.empty()) return false;
  const int total = static_cast<int>(urlOffsets_.size());
  index_ = (index_ + delta) % total;
  if (index_ < 0) index_ += total;
  playUrl = urlAt(index_);
  title = titleFromUrl(playUrl);
  return true;
}

void PlaylistService::reset() {
  sourceUrl_ = "";
  urlOffsets_.clear();
  if (playlistData_) {
    heap_caps_free(playlistData_);
    playlistData_ = nullptr;
  }
  playlistLength_ = 0;
  playlistCapacity_ = 0;
  index_ = -1;
}

bool PlaylistService::active() const {
  return !urlOffsets_.empty() && index_ >= 0;
}

size_t PlaylistService::count() const { return urlOffsets_.size(); }

int PlaylistService::index() const { return index_; }
