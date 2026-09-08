#pragma once

#include <Audio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>

#include "AudioSnapshot.h"
#include "AudioSpectrum.h"

class AudioEngine {
 public:
  enum class ArtworkEventKind : uint8_t { IcyLogo, EmbeddedImage };

  struct ArtworkEvent {
    ArtworkEventKind kind{ArtworkEventKind::IcyLogo};
    char text[384]{};
    uint8_t segmentValues{0};
    uint32_t segments[16]{};
  };

  AudioEngine();
  ~AudioEngine();

  bool begin(uint8_t volume);
  void loop();

  bool connect(const String& url);
  bool speak(const String& text, const String& lang);
  void stop();
  bool togglePause();
  void setVolume(uint8_t volume);
  void setConnectionError(const char* message);

  AudioLevels levels();
  void setVisualizationEnabled(bool enabled);
  AudioSnapshot snapshot(const String& stationName);
  bool consumeEndOfFile();
  bool takeArtworkEvent(ArtworkEvent& event);

  size_t bufferFilled();
  size_t bufferFree();
 size_t bufferSize();
  bool running();
  bool paused() const;
  uint32_t audioCurrentTime();
  uint32_t audioStackFreeWords();

 private:
  struct DeferredLog {
    char text[256]{};
  };

  static void audioInfoCallback(Audio::msg_t message);
  void processDeferredLogs();
  void deferAudioLog(const char* text);
  void handleAudioInfo(Audio::msg_t message);
  void setOutputEnabled(bool enabled);
  static uint8_t toInternalVolume(uint8_t displayVolume);

  static AudioEngine* instance_;

  Audio audio_;
  AudioSpectrum spectrum_;
  QueueHandle_t artworkQueue_{nullptr};
  QueueHandle_t logQueue_{nullptr};
  mutable SemaphoreHandle_t metadataMutex_{nullptr};
  String streamTitle_;
  String artist_;
  String title_;
  String codec_;
  String statusText_;
  String stateCode_{"BOOT"};
  uint32_t bitrateKbps_{0};
  uint8_t volume_{8};
  volatile bool paused_{false};
  volatile bool connecting_{false};
  volatile bool endOfFile_{false};
  volatile bool suppressEndOfFile_{false};
  volatile bool initialized_{false};
  volatile bool commandQueued_{false};
  volatile bool connectAttempted_{false};
  volatile bool tcpConnected_{false};
  bool outputEnabled_{false};
  String currentUrl_;
  uint32_t connectStartedAt_{0};
};
