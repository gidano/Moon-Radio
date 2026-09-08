#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "WeatherTypes.h"

class WeatherService {
 public:
  WeatherService();
  ~WeatherService();

  void begin(const WeatherConfig& config);
  void setConfig(const WeatherConfig& config);
  WeatherConfig config() const;
  WeatherSnapshot snapshot() const;
  void loop(bool wifiConnected);

 private:
  static void taskEntry(void* parameter);
  void fetch();
  bool fetchOpenMeteo(WeatherSnapshot& snapshot);
  static String buildCurrentSummary(const WeatherSnapshot& snapshot);
  static String buildDailySummary(const char* label, float maxTemp, float minTemp,
                                  int precipitationProbability, float windSpeed,
                                  int windDirectionDeg);
  static String windDirectionText(int degrees);
  static WeatherIconKind iconFromWeatherCode(int weatherCode);

  mutable SemaphoreHandle_t mutex_{nullptr};
  WeatherConfig config_;
  WeatherSnapshot snapshot_;
  bool taskRunning_{false};
  uint32_t nextPollAt_{0};
  uint32_t generation_{0};
};
