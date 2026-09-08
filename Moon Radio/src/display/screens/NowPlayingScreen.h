#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "../../audio/AudioSnapshot.h"
#include "../../diagnostics/PerformanceMonitor.h"
#include "../../weather/WeatherTypes.h"
#include "../FontManager.h"
#include "../widgets/WaveVu.h"

struct NowPlayingActions {
  void* context{nullptr};
  void (*previous)(void*){nullptr};
  void (*toggle)(void*){nullptr};
  void (*next)(void*){nullptr};
  void (*adjustVolume)(void*, int8_t){nullptr};
  void (*setVolume)(void*, uint8_t){nullptr};
  size_t (*stationCount)(void*){nullptr};
  String (*stationName)(void*, size_t){nullptr};
  String (*stationUrl)(void*, size_t){nullptr};
  size_t (*currentStation)(void*){nullptr};
  bool (*selectStation)(void*, size_t){nullptr};
  void (*openPresets)(void*){nullptr};
  String (*ipAddress)(void*){nullptr};
  void (*cycleWeatherMode)(void*){nullptr};
  void (*visualizerModeChanged)(void*, uint8_t){nullptr};
  void (*headerIpVisibleChanged)(void*, bool){nullptr};
  void (*wifiDetailsVisibleChanged)(void*, bool){nullptr};
  void (*volumeGraphicVisibleChanged)(void*, bool){nullptr};
  void (*diagnosticsVisibleChanged)(void*, bool){nullptr};
};

class NowPlayingScreen {
 public:
  void create(FontManager& fonts, const NowPlayingActions& actions);
  void update(const AudioSnapshot& audio, const String& logoName,
              const String& wifiText, const String& ipText,
              const WeatherSnapshot& weather,
              const DiagnosticsSnapshot& diagnostics,
              bool backgroundEnabled, const String& backgroundPath,
              uint8_t backgroundOpacity);
  void updateVu(const AudioLevels& levels);
  void setNativeVisualizerBlit(void* context,
                               WaveVu::NativeBlitCallback callback);
  void setVolumeIndicator(uint8_t value);
  void setVisualizerMode(WaveVu::Mode mode);
  WaveVu::Mode visualizerMode() const;
  void setHeaderIpVisible(bool visible);
  void showStartupIp(uint32_t durationMs);
  bool headerIpVisible() const;
  void setWifiDetailsVisible(bool visible);
  bool wifiDetailsVisible() const;
  void setVolumeGraphicVisible(bool visible);
  bool volumeGraphicVisible() const;
  void setBufferBarVisible(bool visible);
  bool bufferBarVisible() const;
  void setDiagnosticsVisible(bool visible);
  bool diagnosticsVisible() const;
  void loop(uint32_t now);
  bool visualizerActive() const;

 private:
  static void previousEvent(lv_event_t* event);
  static void toggleEvent(lv_event_t* event);
  static void nextEvent(lv_event_t* event);
  static void logoPanelEvent(lv_event_t* event);
  static void stationEvent(lv_event_t* event);
  static void wifiEvent(lv_event_t* event);
  static void headerVolumeEvent(lv_event_t* event);
  static void clockAreaEvent(lv_event_t* event);
  static void weatherEvent(lv_event_t* event);
  static void screenTapEvent(lv_event_t* event);
  static void volumeSliderEvent(lv_event_t* event);
  static lv_obj_t* makeButton(lv_obj_t* parent, const char* text, int x,
                              const lv_font_t* font, lv_event_cb_t callback,
                              NowPlayingScreen* screen);
  void updateWifiIndicator(const String& wifiText);
  void updateWifiHeader(const String& wifiText);
  void updateHeaderVolumeDisplay();
  void updateClockIpVisibility(uint32_t now);
  void updateWeather(const WeatherSnapshot& weather);
  void updateBackground(bool enabled, const String& path, uint8_t opacity);
  void updateBufferBar(const AudioSnapshot& audio);
  void updateMoonPhase(const tm& localTime);
  void showVolumePopup();
  void updateLogo(const String& logoName);
  bool loadRgb565Thumbnail(const String& path);
  bool loadWeatherIconRgb565(const String& path);
  bool loadMoonPhasePng(int phase);
  bool loadBackgroundRgb565(const String& path);
  void updateDiagnostics(const DiagnosticsSnapshot& diagnostics,
                         const char* bufferText);
  void showDiagnostics(bool show);

  FontManager* fonts_{nullptr};
  NowPlayingActions actions_;
  lv_obj_t* backgroundImage_{nullptr};
  lv_obj_t* wifiLabel_{nullptr};
  lv_obj_t* wifiSignalBars_[4]{};
  lv_obj_t* wifiTouchArea_{nullptr};
  lv_obj_t* appLabel_{nullptr};
  lv_obj_t* appTouchArea_{nullptr};
  lv_obj_t* bufferLabel_{nullptr};
  lv_obj_t* clockLabel_{nullptr};
  lv_obj_t* ipLabel_{nullptr};
  lv_obj_t* clockSecondsLabel_{nullptr};
  lv_obj_t* clockSecondsUnderline_{nullptr};
  lv_obj_t* clockTouchArea_{nullptr};
  lv_obj_t* headerVolumeTouchArea_{nullptr};
  lv_obj_t* headerVolumeIcon_{nullptr};
  lv_obj_t* volumeLabel_{nullptr};
  lv_obj_t* volumeBars_[5]{};
  lv_obj_t* stationLabel_{nullptr};
  lv_obj_t* bufferBarTrack_{nullptr};
  lv_obj_t* bufferBarFill_{nullptr};
  lv_obj_t* titleLabel_{nullptr};
  lv_obj_t* infoLabel_{nullptr};
  lv_obj_t* dateLabel_{nullptr};
  lv_obj_t* namedayLabel_{nullptr};
  lv_obj_t* weatherLabel_{nullptr};
  lv_obj_t* weatherIconHost_{nullptr};
  lv_obj_t* weatherIconImage_{nullptr};
  lv_obj_t* weatherTouchArea_{nullptr};
  lv_obj_t* weatherSunCenter_{nullptr};
  lv_obj_t* weatherSunRays_[4]{};
  lv_obj_t* weatherCloudBase_{nullptr};
  lv_obj_t* weatherCloudPuffs_[2]{};
  lv_obj_t* weatherRainDrops_[2]{};
  lv_obj_t* moonHost_{nullptr};
  lv_obj_t* moonImage_{nullptr};
  lv_obj_t* volumePopup_{nullptr};
  lv_obj_t* volumePopupIcon_{nullptr};
  lv_obj_t* volumePopupValueLabel_{nullptr};
  lv_obj_t* volumeSlider_{nullptr};
  lv_obj_t* playButtonLabel_{nullptr};
  lv_obj_t* logoPanel_{nullptr};
  lv_obj_t* logoTouchArea_{nullptr};
  lv_obj_t* logoImage_{nullptr};
  lv_obj_t* diagnosticsLabel_{nullptr};
  String logoPath_;
  String currentLogo_;
  uint8_t* logoPixels_{nullptr};
  lv_image_dsc_t logoDescriptor_{};
  uint8_t* weatherIconPixels_{nullptr};
  lv_image_dsc_t weatherIconDescriptor_{};
  uint8_t* backgroundPixels_{nullptr};
  lv_image_dsc_t backgroundDescriptor_{};
  uint8_t* moonPixels_{nullptr};
  lv_image_dsc_t moonDescriptor_{};
  bool diagnosticsVisible_{false};
  bool sliderSyncing_{false};
  bool wifiDetailsVisible_{false};
  bool bufferBarVisible_{true};
  bool volumeGraphicVisible_{false};
  bool ipVisible_{true};
  String lastWifiText_;
  String ipText_{"IP --.--.--.--"};
  String weatherIconPath_;
  String backgroundPath_;
  String currentMoon_;
  String moonPath_;
  String lastDateText_;
  String lastNamedayText_;
  String lastWeatherSummary_;
  bool backgroundEnabled_{false};
  uint8_t backgroundOpacity_{255};
  uint8_t currentVolume_{8};
  uint8_t currentBufferPercent_{255};
  int8_t lastWifiSignalBars_{-1};
  uint32_t lastBufferBarUpdateAt_{0};
  bool lastWeatherVisible_{false};
  WeatherIconKind lastWeatherIcon_{WeatherIconKind::Unknown};
  int lastMoonPhase_{-1};
  uint32_t volumePopupVisibleUntil_{0};
  uint32_t ipVisibleUntil_{0};
  lv_point_t screenPressedAt_{};
  WaveVu vu_;
};
