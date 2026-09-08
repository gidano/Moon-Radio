#include "StationMetadataService.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>

namespace {

constexpr char kRetroMetadataUrl[] =
    "https://myonlineradio.hu/get-radio-list-songs";
constexpr char kRetroPageUrl[] = "https://myonlineradio.hu/retro-radio";
constexpr char kRadio1PageUrl[] = "https://myonlineradio.hu/radio-1";
constexpr char kRetroStationToken[] = "\"94\"";
constexpr char kRadio1StationToken[] = "\"1\"";
constexpr uint32_t kInitialDelayMs = 10000;
constexpr uint32_t kPollIntervalMs = 15000;
constexpr uint32_t kRetryIntervalMs = 15000;
constexpr uint32_t kReadTimeoutMs = 7000;
constexpr size_t kHealthyBufferBytes = 64 * 1024;

bool readNext(HTTPClient& http, NetworkClient& stream, int& value,
              uint32_t deadline) {
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    if (stream.available()) {
      value = stream.read();
      return value >= 0;
    }
    if (!http.connected()) return false;
    vTaskDelay(1);
  }
  return false;
}

bool findToken(HTTPClient& http, NetworkClient& stream, const char* token,
               uint32_t deadline, size_t maximumBytes = SIZE_MAX) {
  const size_t length = strlen(token);
  size_t matched = 0;
  size_t scanned = 0;
  int value = 0;
  while (scanned++ < maximumBytes &&
         readNext(http, stream, value, deadline)) {
    const char character = static_cast<char>(value);
    if (character == token[matched]) {
      if (++matched == length) return true;
    } else {
      matched = character == token[0] ? 1 : 0;
    }
  }
  return false;
}

int hexValue(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

void appendUtf8(String& output, uint16_t codepoint) {
  if (codepoint <= 0x7F) {
    output += static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    output += static_cast<char>(0xC0 | (codepoint >> 6));
    output += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0xD800 || codepoint > 0xDFFF) {
    output += static_cast<char>(0xE0 | (codepoint >> 12));
    output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    output += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}

bool readJsonString(HTTPClient& http, NetworkClient& stream, String& output,
                    uint32_t deadline) {
  output = "";
  output.reserve(192);
  bool escaped = false;
  int value = 0;
  while (output.length() < 384 &&
         readNext(http, stream, value, deadline)) {
    const char character = static_cast<char>(value);
    if (!escaped) {
      if (character == '"') return true;
      if (character == '\\') {
        escaped = true;
      } else {
        output += character;
      }
      continue;
    }

    escaped = false;
    switch (character) {
      case '"':
      case '\\':
      case '/':
        output += character;
        break;
      case 'b':
        output += '\b';
        break;
      case 'f':
        output += '\f';
        break;
      case 'n':
        output += '\n';
        break;
      case 'r':
        output += '\r';
        break;
      case 't':
        output += '\t';
        break;
      case 'u': {
        uint16_t codepoint = 0;
        for (uint8_t index = 0; index < 4; ++index) {
          if (!readNext(http, stream, value, deadline)) return false;
          const int digit = hexValue(static_cast<char>(value));
          if (digit < 0) return false;
          codepoint = static_cast<uint16_t>((codepoint << 4) | digit);
        }
        appendUtf8(output, codepoint);
        break;
      }
      default:
        output += character;
        break;
    }
  }
  return false;
}


}  // namespace

StationMetadataService::StationMetadataService() {
  mutex_ = xSemaphoreCreateMutex();
}

StationMetadataService::~StationMetadataService() {
  if (mutex_) vSemaphoreDelete(mutex_);
}

void StationMetadataService::selectStation(const Station* station) {
  if (!mutex_ || !xSemaphoreTake(mutex_, pdMS_TO_TICKS(30))) return;
  sourceKind_ = 0;
  if (station) {
    if (isRetroRadio(*station)) sourceKind_ = 1;
    else if (isRadio1(*station)) sourceKind_ = 2;
  }
  active_ = sourceKind_ != 0;
  title_ = "";
  ++generation_;
  nextPollAt_ = millis() + kInitialDelayMs;
  xSemaphoreGive(mutex_);
}

void StationMetadataService::loop(bool wifiConnected, bool playbackRunning,
                                  size_t bufferFilledBytes) {
  if (!wifiConnected || !playbackRunning ||
      bufferFilledBytes < kHealthyBufferBytes || !mutex_)
    return;

  bool start = false;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5))) {
    start = active_ && !taskRunning_ &&
            static_cast<int32_t>(millis() - nextPollAt_) >= 0;
    if (start) {
      // Sikertelen feladatindításnál se próbálkozzunk minden főciklusban.
      nextPollAt_ = millis() + kRetryIntervalMs;
      taskRunning_ = true;
    }
    xSemaphoreGive(mutex_);
  }
  if (!start) return;

  if (xTaskCreatePinnedToCore(taskEntry, "station-meta", 8192, this, 0,
                              nullptr, 0) != pdPASS) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(30))) {
      taskRunning_ = false;
      xSemaphoreGive(mutex_);
    }
    return;
  }
}

String StationMetadataService::title() const {
  String result;
  if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(20))) {
    result = title_;
    xSemaphoreGive(mutex_);
  }
  return result;
}

void StationMetadataService::taskEntry(void* parameter) {
  static_cast<StationMetadataService*>(parameter)->fetch();
  vTaskDelete(nullptr);
}

void StationMetadataService::fetch() {
  uint32_t generation = 0;
  if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(30))) {
    generation = generation_;
    xSemaphoreGive(mutex_);
  }

  String nextTitle;
  bool success = false;
  uint8_t sourceKind = 0;
  if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(30))) {
    sourceKind = sourceKind_;
    xSemaphoreGive(mutex_);
  }

  if (sourceKind == 1) {
    success = fetchRetroTitle(nextTitle);
  } else if (sourceKind == 2) {
    success =
        fetchMyOnlineRadioTitle(kRadio1PageUrl, kRadio1StationToken, nextTitle);
  }

  if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100))) {
    if (success && active_ && generation == generation_) {
      nextTitle.trim();
      title_ = nextTitle;
      Serial.printf("[metadata] MyOnlineRadio #%u: %s\n", sourceKind_,
                    title_.c_str());
    }
    nextPollAt_ =
        millis() + (success ? kPollIntervalMs : kRetryIntervalMs);
    taskRunning_ = false;
    xSemaphoreGive(mutex_);
  }
}

bool StationMetadataService::fetchRetroTitle(String& title) {
  return fetchMyOnlineRadioTitle(kRetroPageUrl, kRetroStationToken, title);
}

bool StationMetadataService::fetchMyOnlineRadioTitle(const char* pageUrl,
                                                     const char* stationToken,
                                                     String& title) {
  NetworkClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(7);

  // A weboldal AJAX-kérése is minden lekéréshez egyedi paramétert használ.
  // Így nem kaphatjuk vissza a szerver/CDN korábban gyorsítótárazott válaszát.
  String requestUrl = kRetroMetadataUrl;
  requestUrl += "?_=";
  requestUrl += String(millis());

  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(kReadTimeoutMs);
  http.setUserAgent("LVGL-Radio/1.0 ESP32");
  if (!http.begin(client, requestUrl)) return false;
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  // A végpont Referer nélkül 200 OK mellett üres választ küld.
  http.addHeader("Referer", kRetroPageUrl);
  http.addHeader("X-Requested-With", "XMLHttpRequest");

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[metadata] MyOnlineRadio HTTP %d\n", code);
    http.end();
    return false;
  }

  NetworkClient* stream = http.getStreamPtr();
  const uint32_t deadline = millis() + kReadTimeoutMs;
  bool ok = findToken(http, *stream, stationToken, deadline);
  if (ok) ok = findToken(http, *stream, "\"title\"", deadline, 256);
  if (ok) ok = findToken(http, *stream, ":", deadline, 32);
  if (ok) ok = findToken(http, *stream, "\"", deadline, 32);
  if (ok) ok = readJsonString(http, *stream, title, deadline);
  http.end();
  return ok;
}

bool StationMetadataService::isRetroRadio(const Station& station) {
  String logo = station.logoName;
  String url = station.url;
  String homepage = station.homepage;
  String name = station.name;
  logo.toLowerCase();
  url.toLowerCase();
  homepage.toLowerCase();
  name.toLowerCase();
  return logo == "retro_radio" || url.indexOf("/5001/live.mp3") >= 0 ||
         url.indexOf("/5002/live.mp3") >= 0 ||
         url.indexOf("retro-radio.mp3") >= 0 ||
         homepage.indexOf("retroradio.hu") >= 0 ||
         name.indexOf("retro rádió") >= 0;
}

bool StationMetadataService::isRadio1(const Station& station) {
  String logo = station.logoName;
  String url = station.url;
  String homepage = station.homepage;
  String name = station.name;
  logo.toLowerCase();
  url.toLowerCase();
  homepage.toLowerCase();
  name.toLowerCase();
  return logo == "radio_1" || logo == "radio1" ||
         homepage.indexOf("radio1.hu") >= 0 ||
         homepage.indexOf("myonlineradio.hu/radio-1") >= 0 ||
         url.indexOf("radio1") >= 0 || name.indexOf("rádió 1") >= 0 ||
         name.indexOf("radio 1") >= 0;
}
