#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "../audio/AudioSnapshot.h"
#include "../diagnostics/PerformanceMonitor.h"
#include "../presets/PresetStore.h"
#include "../weather/WeatherTypes.h"
#include "DisplayDevice.h"
#include "FontManager.h"
#include "screens/NowPlayingScreen.h"

class DisplayManager {
 public:
  static constexpr uint16_t kWidth = 480;
  static constexpr uint16_t kHeight = 320;

  DisplayManager();

  // Must be called before begin().  It supplies the persisted panel state
  // before the AXS controller is allowed to turn its output on.
  void setStartupDisplayState(bool colorInverted, bool screenFlipped);
  bool begin(const NowPlayingActions& actions);
  void loop();
  void update(const AudioSnapshot& audio, const String& logoName,
              const String& wifiText, const String& ipText,
              const WeatherSnapshot& weather,
              const DiagnosticsSnapshot& diagnostics,
              bool backgroundEnabled, const String& backgroundPath,
              uint8_t backgroundOpacity);
  void updateVu(const AudioLevels& levels);
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
  void openStationSelector();
  bool moveStationSelector(int8_t delta);
  bool stationSelectorActive() const;
  void showMaintenance();
  void showWifiSetup(const String& address);
  void showPresets();
  bool visualizerActive() const;
  void setBrightness(uint8_t value);
  uint8_t brightness() const;
  void setColorInverted(bool inverted);
  bool colorInverted() const;
  void setScreenFlipped(bool flipped);
  bool screenFlipped() const;

 private:
  enum class PresetMode : uint8_t { Play = 0, Save = 1, Delete = 2 };
  enum class ScreenMode : uint8_t { Normal = 0, Maintenance = 1, WifiSetup = 2 };

  static void flush(lv_display_t* display, const lv_area_t* area,
                    uint8_t* pixels);
  static void readTouch(lv_indev_t* input, lv_indev_data_t* data);
  static void stationSelectorEvent(lv_event_t* event);
  static void closeSelectorEvent(lv_event_t* event);
  static void presetModeEvent(lv_event_t* event);
  static void presetSlotEvent(lv_event_t* event);
  static void presetBankEvent(lv_event_t* event);
  static void closePresetsEvent(lv_event_t* event);
  static void nativeVisualizerBlit(void* context, int32_t x, int32_t y,
                                   int32_t w, int32_t h,
                                   const uint16_t* pixels);
  void overlayNativeVuIntoLvglFlush(uint16_t* lvglPixels, int32_t x,
                                    int32_t y, int32_t w, int32_t h);
  void refreshAxsVuFrameFromLvgl(const uint16_t* lvglPixels, int32_t x,
                                 int32_t y, int32_t w, int32_t h);
  void showBootLogo();
  void releaseBootLogo();
  void calibrateTouch();
  void configureScreenNativeHooks();
  void showStationSelector();
  void hideStationSelector();
  void updateStationSelector();
  void resetStationSelector();
  void hidePresets();
  void resetPresets();
  void updatePresets();
  void setPresetMode(PresetMode mode);
  void refreshPresetSlots();
  void refreshPresetBanks();
  void handlePresetSlot(uint8_t slot);
  int32_t stationIndexForUrl(const String& url) const;
  void setPresetStatus(const String& text, uint32_t color = 0x7DD3FC);

  static DisplayManager* instance_;
  static constexpr uint32_t kDrawBufferPixels = kWidth * 30;
  static constexpr uint32_t kFallbackDrawBufferPixels = kWidth * 10;
  static constexpr int32_t kAxsVuTop = 258;
  static constexpr int32_t kAxsVuHeight = 62;
  static constexpr int32_t kAxsVuWidth = 240;

  DisplayDevice device_;
  FontManager fonts_;
  PresetStore presets_;
  NowPlayingScreen screen_;
  NowPlayingActions actions_;
  lv_display_t* lvDisplay_{nullptr};
  uint16_t* drawBuffer_{nullptr};
  // The left 240 columns of the last valid 480x320 LVGL frame.  The VU
  // callback replaces only rows 258..319 before this is sent to AXS.
  uint16_t* axsVuFrame_{nullptr};
  const uint16_t* nativeVuPixels_{nullptr};
  uint8_t* bootLogoPixels_{nullptr};
  lv_image_dsc_t bootLogoDescriptor_{};
  uint32_t drawBufferPixels_{0};
  uint32_t previousTick_{0};
  uint32_t lastLvglRunAt_{0};
  uint32_t lastAxsVisualizerPhysicalFlushAt_{0};
  uint8_t brightness_{204};
  bool backlightDeferred_{false};
  bool axsVuFrameReady_{false};
  bool colorInverted_{false};
  bool screenFlipped_{false};
  WaveVu::Mode visualizerMode_{WaveVu::Mode::Spectrum};
  bool headerIpVisible_{false};
  bool wifiDetailsVisible_{false};
  bool volumeGraphicVisible_{true};
  bool bufferBarVisible_{true};
  bool diagnosticsVisible_{false};
  bool touchActive_{false};
  bool volumeSwipe_{false};
  bool verticalSwipe_{false};
  bool openSelectorPending_{false};
  int32_t touchStartX_{0};
  int32_t touchStartY_{0};
  int32_t lastSwipeX_{0};
  lv_obj_t* selectorOverlay_{nullptr};
  lv_obj_t* selectorRoller_{nullptr};
  lv_obj_t* selectorStatusLabel_{nullptr};
  int32_t selectorPendingIndex_{-1};
  uint32_t selectorSelectAt_{0};
  uint8_t selectorCountdown_{0};
  bool selectorActive_{false};
  bool selectorConstructing_{false};
  lv_obj_t* presetOverlay_{nullptr};
  lv_obj_t* presetStatusLabel_{nullptr};
  lv_obj_t* presetModeButtons_[3]{};
  lv_obj_t* presetSlotButtons_[PresetStore::kSlotCount]{};
  lv_obj_t* presetSlotLabels_[PresetStore::kSlotCount]{};
  lv_obj_t* presetBankButtons_[PresetStore::kBankCount]{};
  lv_obj_t* presetBankLabels_[PresetStore::kBankCount]{};
  PresetMode presetMode_{PresetMode::Play};
  int8_t presetDeleteArmed_{-1};
  uint32_t presetDeleteArmedAt_{0};
  uint32_t presetLastActivity_{0};
  bool presetActive_{false};
  ScreenMode screenMode_{ScreenMode::Normal};
};
