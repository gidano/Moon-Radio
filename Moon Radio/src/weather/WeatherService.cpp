#include "WeatherService.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>

#include "options.h"

namespace {

constexpr char kForecastUrl[] = "https://api.open-meteo.com/v1/forecast";
constexpr uint32_t kInitialDelayMs = 12000;
constexpr uint32_t kRetryDelayMs = 5 * 60 * 1000;

String extractCurrentObject(const String& json) {
  const int markerPos = json.indexOf("\"current\":");
  if (markerPos < 0) return "";
  int start = json.indexOf('{', markerPos);
  if (start < 0) return "";

  int depth = 0;
  for (int i = start; i < static_cast<int>(json.length()); ++i) {
    const char ch = json[i];
    if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        return json.substring(start, i + 1);
      }
    }
  }
  return "";
}

String extractDailyObject(const String& json) {
  const int markerPos = json.indexOf("\"daily\":");
  if (markerPos < 0) return "";
  int start = json.indexOf('{', markerPos);
  if (start < 0) return "";

  int depth = 0;
  for (int i = start; i < static_cast<int>(json.length()); ++i) {
    const char ch = json[i];
    if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        return json.substring(start, i + 1);
      }
    }
  }
  return "";
}

float parseFloatField(const String& json, const String& key, float fallback) {
  const String marker = "\"" + key + "\":";
  const int keyPos = json.indexOf(marker);
  if (keyPos < 0) return fallback;
  int valuePos = keyPos + marker.length();
  while (valuePos < static_cast<int>(json.length()) &&
         (json[valuePos] == ' ' || json[valuePos] == '"')) {
    ++valuePos;
  }
  int endPos = valuePos;
  while (endPos < static_cast<int>(json.length())) {
    const char ch = json[endPos];
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
      ++endPos;
    } else {
      break;
    }
  }
  if (endPos <= valuePos) return fallback;
  return json.substring(valuePos, endPos).toFloat();
}

int parseIntField(const String& json, const String& key, int fallback) {
  return static_cast<int>(parseFloatField(json, key, static_cast<float>(fallback)));
}

String extractArrayValue(const String& json, const String& key, int index) {
  if (index < 0) return "";
  const String marker = "\"" + key + "\":[";
  const int keyPos = json.indexOf(marker);
  if (keyPos < 0) return "";
  int valuePos = keyPos + marker.length();
  int currentIndex = 0;
  while (valuePos < static_cast<int>(json.length())) {
    while (valuePos < static_cast<int>(json.length()) &&
           (json[valuePos] == ' ' || json[valuePos] == '"')) {
      ++valuePos;
    }
    int endPos = valuePos;
    while (endPos < static_cast<int>(json.length())) {
      const char ch = json[endPos];
      if (ch == ',' || ch == ']') break;
      ++endPos;
    }
    if (currentIndex == index) {
      String value = json.substring(valuePos, endPos);
      value.trim();
      value.replace("\"", "");
      return value;
    }
    valuePos = endPos + 1;
    ++currentIndex;
  }
  return "";
}

float parseArrayFloatField(const String& json, const String& key, int index,
                           float fallback) {
  const String value = extractArrayValue(json, key, index);
  return value.isEmpty() ? fallback : value.toFloat();
}

int parseArrayIntField(const String& json, const String& key, int index,
                       int fallback) {
  return static_cast<int>(
      parseArrayFloatField(json, key, index, static_cast<float>(fallback)));
}

}  // namespace

WeatherService::WeatherService() { mutex_ = xSemaphoreCreateMutex(); }

WeatherService::~WeatherService() {
  if (mutex_) vSemaphoreDelete(mutex_);
}

void WeatherService::begin(const WeatherConfig& config) {
  if (!mutex_) return;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(30))) {
    config_ = config;
    snapshot_.enabled = config.enabled;
    snapshot_.valid = false;
    snapshot_.summary = "";
    nextPollAt_ = millis() + kInitialDelayMs;
    ++generation_;
    xSemaphoreGive(mutex_);
  }
}

void WeatherService::setConfig(const WeatherConfig& config) {
  if (!mutex_) return;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50))) {
    config_ = config;
    snapshot_.enabled = config.enabled;
    if (!config.enabled) {
      snapshot_.valid = false;
      snapshot_.summary = "";
      snapshot_.icon = WeatherIconKind::Unknown;
    }
    nextPollAt_ = millis() + 1000;
    ++generation_;
    xSemaphoreGive(mutex_);
  }
}

WeatherConfig WeatherService::config() const {
  WeatherConfig result;
  if (!mutex_) return result;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20))) {
    result = config_;
    xSemaphoreGive(mutex_);
  }
  return result;
}

WeatherSnapshot WeatherService::snapshot() const {
  WeatherSnapshot result;
  if (!mutex_) return result;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20))) {
    result = snapshot_;
    xSemaphoreGive(mutex_);
  }
  return result;
}

void WeatherService::loop(bool wifiConnected) {
  if (!wifiConnected || !mutex_) return;
  bool start = false;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5))) {
    start = config_.enabled && !taskRunning_ &&
            static_cast<int32_t>(millis() - nextPollAt_) >= 0;
    if (start) {
      taskRunning_ = true;
      nextPollAt_ = millis() + kRetryDelayMs;
    }
    xSemaphoreGive(mutex_);
  }
  if (!start) return;

  const BaseType_t created =
      xTaskCreatePinnedToCore(taskEntry, "weather-fetch", 8192, this, 0,
                              nullptr, 0);
  if (created != pdPASS) {
    Serial.printf("[weather] task inditasi hiba: heap=%u psram=%u\n",
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(30))) {
      taskRunning_ = false;
      xSemaphoreGive(mutex_);
    }
  }
}

void WeatherService::taskEntry(void* parameter) {
  static_cast<WeatherService*>(parameter)->fetch();
  vTaskDelete(nullptr);
}

void WeatherService::fetch() {
  uint32_t generation = 0;
  WeatherConfig config;
  if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(30))) {
    generation = generation_;
    config = config_;
    xSemaphoreGive(mutex_);
  }

  WeatherSnapshot next;
  next.enabled = config.enabled;
  const bool success = config.enabled && fetchOpenMeteo(next);

  if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100))) {
    if (generation == generation_) {
      if (success) snapshot_ = next;
      snapshot_.enabled = config_.enabled;
      nextPollAt_ = millis() +
                    (success ? static_cast<uint32_t>(config_.intervalMinutes) *
                                   60UL * 1000UL
                             : kRetryDelayMs);
    }
    taskRunning_ = false;
    xSemaphoreGive(mutex_);
  }
}

bool WeatherService::fetchOpenMeteo(WeatherSnapshot& snapshot) {
  WeatherConfig cfg = config();

  String url = String(kForecastUrl) + "?latitude=" + String(cfg.latitude, 4) +
               "&longitude=" + String(cfg.longitude, 4) +
               "&current=temperature_2m,relative_humidity_2m,pressure_msl,wind_speed_10m,wind_direction_10m,weather_code"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max,wind_direction_10m_dominant"
               "&forecast_days=2"
               "&wind_speed_unit=kmh";

  NetworkClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(7);

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(7000);
  http.setUserAgent("LVGL-Radio/1.0 ESP32");
  if (!http.begin(client, url)) return false;

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();
  if (body.isEmpty()) return false;

  const String current = extractCurrentObject(body);
  const String daily = extractDailyObject(body);
  if (current.isEmpty() && daily.isEmpty()) return false;

  if (!current.isEmpty()) {
    snapshot.temperatureC = parseFloatField(current, "temperature_2m", 0.0f);
    snapshot.humidityPercent =
        parseFloatField(current, "relative_humidity_2m", 0.0f);
    snapshot.pressureHpa = parseFloatField(current, "pressure_msl", 0.0f);
    snapshot.windSpeedKmh = parseFloatField(current, "wind_speed_10m", 0.0f);
    snapshot.windDirectionDeg = parseIntField(current, "wind_direction_10m", 0);
  }

  const int selectedIndex =
      cfg.mode == WeatherDisplayMode::Tomorrow ? 1 : 0;
  const int currentWeatherCode = !current.isEmpty()
                                     ? parseIntField(current, "weather_code", -1)
                                     : -1;
  const int dailyWeatherCode = !daily.isEmpty()
                                   ? parseArrayIntField(daily, "weather_code",
                                                        selectedIndex, -1)
                                   : -1;

  snapshot.updatedAt = millis();

  switch (cfg.mode) {
    case WeatherDisplayMode::Today: {
      if (daily.isEmpty()) return false;
      const float maxTemp =
          parseArrayFloatField(daily, "temperature_2m_max", 0, 0.0f);
      const float minTemp =
          parseArrayFloatField(daily, "temperature_2m_min", 0, 0.0f);
      const int precipitationProbability = parseArrayIntField(
          daily, "precipitation_probability_max", 0, 0);
      const float windSpeed =
          parseArrayFloatField(daily, "wind_speed_10m_max", 0, 0.0f);
      const int windDirection = parseArrayIntField(
          daily, "wind_direction_10m_dominant", 0, 0);
      snapshot.icon = iconFromWeatherCode(dailyWeatherCode);
      snapshot.valid = dailyWeatherCode >= 0;
      snapshot.summary = buildDailySummary("Ma", maxTemp, minTemp,
                                           precipitationProbability, windSpeed,
                                           windDirection);
      break;
    }
    case WeatherDisplayMode::Tomorrow: {
      if (daily.isEmpty()) return false;
      const float maxTemp =
          parseArrayFloatField(daily, "temperature_2m_max", 1, 0.0f);
      const float minTemp =
          parseArrayFloatField(daily, "temperature_2m_min", 1, 0.0f);
      const int precipitationProbability = parseArrayIntField(
          daily, "precipitation_probability_max", 1, 0);
      const float windSpeed =
          parseArrayFloatField(daily, "wind_speed_10m_max", 1, 0.0f);
      const int windDirection = parseArrayIntField(
          daily, "wind_direction_10m_dominant", 1, 0);
      snapshot.icon = iconFromWeatherCode(dailyWeatherCode);
      snapshot.valid = dailyWeatherCode >= 0;
      snapshot.summary = buildDailySummary("Holnap", maxTemp, minTemp,
                                           precipitationProbability, windSpeed,
                                           windDirection);
      break;
    }
    case WeatherDisplayMode::Current:
    default:
      snapshot.icon = iconFromWeatherCode(currentWeatherCode);
      snapshot.valid = currentWeatherCode >= 0;
      snapshot.summary = buildCurrentSummary(snapshot);
      break;
  }

  return snapshot.valid && !snapshot.summary.isEmpty();
}

String WeatherService::buildCurrentSummary(const WeatherSnapshot& snapshot) {
  if (!snapshot.valid) return "";
  String text;
  text.reserve(128);
  text += String(static_cast<int>(snapshot.temperatureC + (snapshot.temperatureC >= 0 ? 0.5f : -0.5f)));
  text += "°C • ";
  text += String(static_cast<int>(snapshot.pressureHpa + 0.5f));
  text += " hPa • ";
  text += String(static_cast<int>(snapshot.humidityPercent + 0.5f));
  text += "% RH • ";
  text += String(static_cast<int>(snapshot.windSpeedKmh + 0.5f));
  text += " km/h (";
  text += windDirectionText(snapshot.windDirectionDeg);
  text += ")";
  return text;
}

String WeatherService::buildDailySummary(const char* label, float maxTemp,
                                         float minTemp,
                                         int precipitationProbability,
                                         float windSpeed,
                                         int windDirectionDeg) {
  String text;
  text.reserve(96);
  text += label;
  text += ": ";
  text += String(static_cast<int>(maxTemp + (maxTemp >= 0 ? 0.5f : -0.5f)));
  text += "/";
  text += String(static_cast<int>(minTemp + (minTemp >= 0 ? 0.5f : -0.5f)));
  text += "°C • ";
  text += String(precipitationProbability);
  text += "% eső • ";
  text += String(static_cast<int>(windSpeed + 0.5f));
  text += " km/h (";
  text += windDirectionText(windDirectionDeg);
  text += ")";
  return text;
}

String WeatherService::windDirectionText(int degrees) {
  static constexpr const char* kDirections[] = {
      "É",   "É-ÉK", "ÉK", "K-ÉK",
      "K",   "K-DK", "DK", "D-DK",
      "D",   "D-DNy","DNy","Ny-DNy",
      "Ny",  "Ny-ÉNy", "ÉNy", "É-ÉNy"};
  degrees %= 360;
  if (degrees < 0) degrees += 360;
  const int sector = ((degrees + 11) / 22) % 16;
  return String(kDirections[sector]);
}

WeatherIconKind WeatherService::iconFromWeatherCode(int weatherCode) {
  if (weatherCode == 0 || weatherCode == 1) return WeatherIconKind::Sun;
  if (weatherCode >= 95 && weatherCode <= 99) return WeatherIconKind::Storm;
  if ((weatherCode >= 51 && weatherCode <= 67) ||
      (weatherCode >= 71 && weatherCode <= 94)) {
    return WeatherIconKind::Rain;
  }
  if (weatherCode >= 2) return WeatherIconKind::Cloud;
  return WeatherIconKind::Unknown;
}
