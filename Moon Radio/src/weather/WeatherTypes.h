#pragma once

#include <Arduino.h>

enum class WeatherIconKind : uint8_t {
  Unknown = 0,
  Sun = 1,
  Cloud = 2,
  Rain = 3,
  Storm = 4,
  WifiError = 5,
};

enum class WeatherDisplayMode : uint8_t {
  Current = 0,
  Today = 1,
  Tomorrow = 2,
};

struct WeatherConfig {
  bool enabled{false};
  float latitude{47.4979f};
  float longitude{19.0402f};
  uint16_t intervalMinutes{60};
  WeatherDisplayMode mode{WeatherDisplayMode::Current};
};

struct WeatherSnapshot {
  bool enabled{false};
  bool valid{false};
  float temperatureC{0.0f};
  float pressureHpa{0.0f};
  float humidityPercent{0.0f};
  float windSpeedKmh{0.0f};
  int windDirectionDeg{0};
  WeatherIconKind icon{WeatherIconKind::Unknown};
  String summary;
  uint32_t updatedAt{0};
};
