#pragma once

#include <Arduino.h>
#include <LittleFS.h>

#include "../audio/AudioEngine.h"
#include "../audio/PlaylistService.h"
#include "../artwork/LogoManager.h"
#include "../display/DisplayManager.h"
#include "../diagnostics/PerformanceMonitor.h"
#include "../input/InputManager.h"
#include "../metadata/StationMetadataService.h"
#include "../network/WifiManager.h"
#include "../stations/StationStore.h"
#include "../weather/WeatherService.h"

class RadioWebServer;

class RadioController {
 public:
  struct ClockTtsConfig {
    bool enabled{false};
    String language{"HU"};
    uint16_t intervalMinutes{60};
    bool onlyWhenNoStream{false};
    bool quietHoursEnabled{false};
    uint16_t quietFromMinutes{23 * 60};
    uint16_t quietToMinutes{7 * 60};
  };

  RadioController();
  ~RadioController();

  bool begin();
  void loop();

  StationStore& stations();
  const StationStore& stations() const;
  WifiManager& wifi();

  size_t currentIndex() const;
  const Station* currentStation() const;
  AudioSnapshot audioSnapshot();
  AudioLevels audioLevels();
  DiagnosticsSnapshot diagnostics() const;
  LogoManager& logos();
  const LogoManager& logos() const;

  bool selectStation(size_t index);
  bool previousStation();
  bool nextStation();
  bool previousTrack();
  bool nextTrack();
  bool togglePause();
  void setVolume(uint8_t value);
  uint8_t volume() const;
  void setBrightness(uint8_t value);
  uint8_t brightness() const;
  const String& timezone() const;
  bool setTimezone(String value);
  bool backgroundEnabled() const;
  const String& backgroundPath() const;
  uint8_t backgroundOpacity() const;
  bool setBackgroundConfig(bool enabled, String path, uint8_t opacity);
  bool colorInverted() const;
  void setColorInverted(bool inverted);
  bool screenFlipped() const;
  void setScreenFlipped(bool flipped);
  uint8_t visualizerMode() const;
  void setVisualizerMode(uint8_t mode);
  bool headerIpVisible() const;
  void setHeaderIpVisible(bool visible);
  bool wifiDetailsVisible() const;
  void setWifiDetailsVisible(bool visible);
  bool volumeGraphicVisible() const;
  void setVolumeGraphicVisible(bool visible);
  bool bufferBarVisible() const;
  void setBufferBarVisible(bool visible);
  bool diagnosticsVisible() const;
  void setDiagnosticsVisible(bool visible);
  bool albumCoversEnabled() const;
  void setAlbumCoversEnabled(bool enabled);
  WeatherConfig weatherConfig() const;
  WeatherSnapshot weatherSnapshot() const;
  bool setWeatherConfig(const WeatherConfig& config);
  ClockTtsConfig clockTtsConfig() const;
  bool setClockTtsConfig(ClockTtsConfig config);
  bool forceClockTts(const String& text, const String& language = String());
  void beginBackgroundUploadMode();
  void endBackgroundUploadMode();
  bool reloadStations();
  bool enterFsMaintenance();
  bool fsMaintenance() const;

  size_t bufferFilled();
  size_t bufferFree();
  size_t bufferSize();
  bool playlistActive() const;
  int playlistIndex() const;
  size_t playlistCount() const;
  const String& currentPlayUrl() const;

 private:
  static void previousAction(void* context);
  static void toggleAction(void* context);
  static void nextAction(void* context);
  static void primaryEncoderLeftAction(void* context);
  static void primaryEncoderRightAction(void* context);
  static void stationLongPressAction(void* context);
  static void volumeDownAction(void* context);
  static void volumeUpAction(void* context);
  static void adjustVolumeAction(void* context, int8_t delta);
  static void setVolumeAction(void* context, uint8_t value);
  static size_t stationCountAction(void* context);
  static String stationNameAction(void* context, size_t index);
  static String stationUrlAction(void* context, size_t index);
  static size_t currentStationAction(void* context);
  static bool selectStationAction(void* context, size_t index);
  static void openPresetsAction(void* context);
  static void cycleWeatherModeAction(void* context);
  static void visualizerModeChangedAction(void* context, uint8_t mode);
  static void headerIpVisibleChangedAction(void* context, bool visible);
  static void wifiDetailsVisibleChangedAction(void* context, bool visible);
  static void volumeGraphicVisibleChangedAction(void* context, bool visible);
  static void diagnosticsVisibleChangedAction(void* context, bool visible);

  bool connectCurrentStation();
  bool stepTrack(int delta);
  void cycleWeatherMode();
  void recoverStalledStream(uint32_t now, bool wifiConnected, bool running,
                            size_t bufferFilled);
  void processClockTts(uint32_t now);
  void resetClockTtsPlayback(bool restoreVolume);
  bool beginClockTtsAnnouncement(const String& text, const String& language,
                                 bool resumeAfter);
  void applyTimezone();
  String wifiStatusText() const;
  void refreshDisplay(bool force = false);

  StationStore stationStore_;
  WifiManager wifiManager_;
  PlaylistService playlist_;
  AudioEngine audio_;
  StationMetadataService metadata_;
  LogoManager logoManager_;
  DisplayManager display_;
  InputManager input_;
  PerformanceMonitor performanceMonitor_;
  RadioWebServer* web_{nullptr};

  size_t currentIndex_{0};
  uint8_t volume_{8};
  String timezone_{"CET-1CEST,M3.5.0,M10.5.0/3"};
  bool backgroundEnabled_{false};
  String backgroundPath_{"/backgrounds/music_background.sr565"};
  uint8_t backgroundOpacity_{128};
  bool colorInverted_{false};
  bool screenFlipped_{false};
  uint8_t visualizerMode_{0};
  bool headerIpVisible_{false};
  bool wifiDetailsVisible_{false};
  bool volumeGraphicVisible_{true};
  bool bufferBarVisible_{true};
  bool diagnosticsVisible_{false};
  bool albumCoversEnabled_{true};
  WeatherService weather_;
  ClockTtsConfig clockTts_;
  String currentPlayUrl_;
  String playlistTitle_;
  uint32_t lastUiUpdate_{0};
  uint32_t lastVuUpdate_{0};
  uint32_t volumeChangedAt_{0};
  bool volumeSavePending_{false};
  bool fsMaintenance_{false};
  bool wifiWasConnected_{false};
  bool initialized_{false};
  bool connectRetryPending_{false};
  bool backgroundUploadMode_{false};
  bool backgroundUploadResumePlayback_{false};
  uint32_t nextConnectRetryAt_{0};
  uint32_t previousLoopAt_{0};
  uint32_t maximumLoopGap_{0};
  uint32_t lastDiagnosticsAt_{0};
  uint32_t lastArtworkAt_{0};
  uint32_t lastMetadataAt_{0};
  uint32_t lastWifiLoopAt_{0};
  uint32_t lastWebLoopAt_{0};
  uint32_t lastAudioDataAt_{0};
  uint32_t lastStreamRecoveryAt_{0};
  int clockTtsPreviousVolume_{0};
  int clockTtsFadeVolume_{-1};
  int clockTtsLastMinute_{-1};
  size_t clockTtsSavedStation_{0};
  uint32_t clockTtsFadeAt_{0};
  uint32_t clockTtsStartedAt_{0};
  uint32_t clockTtsLastProgressAt_{0};
  uint32_t clockTtsLastAudioTime_{0};
  String clockTtsPendingText_;
  String clockTtsPendingLanguage_;
  bool clockTtsFadingDown_{false};
  bool clockTtsFadingUp_{false};
  bool clockTtsActive_{false};
  bool clockTtsAudioProgressSeen_{false};
  bool clockTtsPendingResumeAfter_{false};
  bool clockTtsResumeAfter_{false};
};
