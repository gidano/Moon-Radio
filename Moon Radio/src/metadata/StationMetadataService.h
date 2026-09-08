#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../stations/Station.h"

class StationMetadataService {
 public:
  StationMetadataService();
  ~StationMetadataService();

  void selectStation(const Station* station);
  void loop(bool wifiConnected, bool playbackRunning,
            size_t bufferFilledBytes);
  String title() const;

 private:
  static void taskEntry(void* parameter);
  void fetch();
  bool fetchRetroTitle(String& title);
  bool fetchMyOnlineRadioTitle(const char* pageUrl, const char* stationToken,
                               String& title);
  static bool isRetroRadio(const Station& station);
  static bool isRadio1(const Station& station);

  mutable SemaphoreHandle_t mutex_{nullptr};
  String title_;
  uint32_t generation_{0};
  uint32_t nextPollAt_{0};
  bool taskRunning_{false};
  bool active_{false};
  uint8_t sourceKind_{0};
};
