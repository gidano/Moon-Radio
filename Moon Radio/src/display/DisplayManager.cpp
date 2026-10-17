#include "DisplayManager.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "options.h"

#ifndef AXS_VU_PHYSICAL_FPS
#define AXS_VU_PHYSICAL_FPS 0
#endif

namespace {

constexpr uint32_t kStationSelectDelayMs = 3000;
constexpr int32_t kVerticalSwipeThreshold = 36;
constexpr int32_t kStationScrollStep = 30;
#if DISPLAY_PROFILE_AXS15231B
#ifndef AXS_LVGL_RUN_INTERVAL_MS
#define AXS_LVGL_RUN_INTERVAL_MS 50
#endif
constexpr uint32_t kLvglRunIntervalMs = AXS_LVGL_RUN_INTERVAL_MS;
#else
constexpr uint32_t kLvglRunIntervalMs = 25;
#endif

}  // namespace

DisplayManager* DisplayManager::instance_ = nullptr;

DisplayManager::DisplayManager() : presets_(LittleFS) {}

void DisplayManager::setStartupDisplayState(bool colorInverted,
                                            bool screenFlipped) {
  // Do not send panel commands here: RadioController calls this before
  // DisplayDevice::init().  begin() consumes these values before the AXS
  // panel leaves sleep/reset.
  colorInverted_ = colorInverted;
  screenFlipped_ = screenFlipped;
}

bool DisplayManager::begin(const NowPlayingActions& actions) {
  instance_ = this;
  actions_ = actions;
#if defined(ENC_BTNB)
  pinMode(ENC_BTNB, INPUT_PULLUP);
#endif
  ledcAttach(BRIGHTNESS_PIN, 5000, 8);
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  // Hide retained GRAM while the controller is reset and initialized.
  ledcWrite(BRIGHTNESS_PIN, 0);
  backlightDeferred_ = true;
#else
  setBrightness(brightness_);
#endif

  device_.prepareTouch();
  device_.init();
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  // The factory transport exclusively owns AXS controller state (MADCTL,
  // inversion, sleep and display-on).  Issuing LovyanGFX rotation/inversion
  // commands here wakes the panel in a second, incompatible state during
  // boot and causes the visible orientation flicker.
#elif DISPLAY_PROFILE_AXS15231B
  device_.setRotation(screenFlipped_ ? 2 : 0);
#elif DISPLAY_PROFILE_FNK0104S
  // The Freenove FNK0104S is 320x480 native and needs the opposite
  // landscape MADCTL direction from the generic ST7796 profile.
  device_.setRotation(screenFlipped_ ? 1 : 3);
#else
  device_.setRotation(screenFlipped_ ? 3 : 1);
#endif
#if !(DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL)
#if DISPLAY_PROFILE_FNK0104S
  // Freenove's reference setup uses TFT_INVERSION_ON.  The web option
  // remains user-facing: false means the panel's normal (inversion-on) mode.
  device_.invertDisplay(!colorInverted_);
#else
  device_.invertDisplay(colorInverted_);
#endif
#endif
  device_.setColorDepth(16);
  device_.setSwapBytes(true);
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  // Pass the saved visual state into the factory transport before its panel
  // init sequence.  Applying it afterwards briefly exposed the retained
  // image with the opposite (180 degree) MADCTL orientation.
  device_.setAxsFactoryScreenFlipped(screenFlipped_);
  device_.setAxsFactoryColorInverted(colorInverted_);
  if (!device_.beginAxsFactoryTransport()) {
    Serial.println("[display] AXS gyari kep-ut nem indult");
    return false;
  }
  if (!device_.primeAxsFactoryPanel()) {
    Serial.println("[display] AXS indulasi kep-ut elokeszitesi hiba");
    return false;
  }
  // Let the controller latch DISPON before the full-screen bootlogo arrives.
  delay(40);
#endif
#if !DISPLAY_PROFILE_AXS15231B || !AXS_FACTORY_DIRECT_LVGL
  device_.fillScreen(TFT_BLACK);
#endif

  lv_init();
  fonts_.begin();
  lvDisplay_ = lv_display_create(kWidth, kHeight);
  lv_display_set_flush_cb(lvDisplay_, flush);
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  // The factory CASET stream is validated with complete 480x320 LVGL frames.
  // The VU retains that same 320-row geometry in its own left-half frame.
  constexpr uint32_t requestedDrawBufferPixels = kWidth * kHeight;
  constexpr lv_display_render_mode_t renderMode = LV_DISPLAY_RENDER_MODE_FULL;
#else
  constexpr uint32_t requestedDrawBufferPixels = kDrawBufferPixels;
  constexpr lv_display_render_mode_t renderMode = LV_DISPLAY_RENDER_MODE_PARTIAL;
#endif
  drawBuffer_ = static_cast<uint16_t*>(heap_caps_malloc(
      requestedDrawBufferPixels * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (drawBuffer_) {
    drawBufferPixels_ = requestedDrawBufferPixels;
    Serial.printf("[display] LVGL puffer PSRAM-ban: %u px\n",
                  static_cast<unsigned>(drawBufferPixels_));
  } else {
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
    Serial.println("[display] AXS gyari teljes LVGL puffer foglalasi hiba");
    return false;
#else
    drawBuffer_ = static_cast<uint16_t*>(heap_caps_calloc(
        kFallbackDrawBufferPixels, sizeof(uint16_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    drawBufferPixels_ = kFallbackDrawBufferPixels;
    Serial.printf("[display] LVGL puffer belso RAM-ban: %u px\n",
                  static_cast<unsigned>(drawBufferPixels_));
#endif
  }
  if (!drawBuffer_) {
    Serial.println("[display] LVGL puffer foglalasi hiba");
    return false;
  }
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  axsVuFrame_ = static_cast<uint16_t*>(heap_caps_malloc(
      // The AXS controller's proven factory write shape is 320 rows high.
      // Keep a left-half native frame (240 x 320), not a second 480 x 62
      // lower-strip layer.
      static_cast<size_t>(kAxsVuWidth) * kHeight * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!axsVuFrame_) {
    Serial.println("[display] AXS VU kep puffer foglalasi hiba");
    return false;
  }
#endif
  lv_display_set_buffers(lvDisplay_, drawBuffer_, nullptr,
                         drawBufferPixels_ * sizeof(uint16_t),
                         renderMode);

#if TOUCH_ENABLED
  calibrateTouch();

  lv_indev_t* input = lv_indev_create();
  lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
  lv_indev_set_display(input, lvDisplay_);
  lv_indev_set_read_cb(input, readTouch);
#else
  Serial.println("[display] touch kikapcsolva, kalibracio kihagyva");
#endif

  presets_.begin();
  screen_.create(fonts_, actions);
  screen_.setBootOverlayActive(true);
  configureScreenNativeHooks();
  screen_.setVisualizerMode(visualizerMode_);
  screen_.setHeaderIpVisible(headerIpVisible_);
  screen_.setWifiDetailsVisible(wifiDetailsVisible_);
  screen_.setVolumeGraphicVisible(volumeGraphicVisible_);
  screen_.setBufferBarVisible(bufferBarVisible_);
  screen_.setDiagnosticsVisible(diagnosticsVisible_);
  // A teljes rádióképernyő már elkészült a bootlogo alatt. Így Wi-Fi után
  // az audio be tudja tölteni az induló tartalékot anélkül, hogy a panel
  // inicializálási vagy AXS QSPI útját módosítanánk.
  showBootLogo();
  previousTick_ = millis();
  return true;
}

void DisplayManager::finishStartupBootLogo() {
  releaseBootLogo();
  screen_.setBootOverlayActive(false);
}

void DisplayManager::loop() {
  const uint32_t now = millis();
  lv_tick_inc(now - previousTick_);
  previousTick_ = now;
  if (now - lastLvglRunAt_ >= kLvglRunIntervalMs) {
    lastLvglRunAt_ = now;
    lv_timer_handler();
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
    if (backlightDeferred_) {
      ledcWrite(BRIGHTNESS_PIN, brightness_);
      backlightDeferred_ = false;
    }
#endif
  }
  // While the bootlogo overlay is visible the dashboard below it is hidden;
  // skip its background/VU/UI work until the overlay is released.
  if (screenMode_ == ScreenMode::Normal && !bootLogoOverlay_) {
    screen_.loop(now);
  }

  if (openSelectorPending_) {
    openSelectorPending_ = false;
    showStationSelector();
  }
  updateStationSelector();
  updatePresets();
  delay(1);
}

void DisplayManager::configureScreenNativeHooks() {
#if DISPLAY_PROFILE_AXS15231B
  // The factory LVGL transport keeps normal widgets in its proven full-frame
  // mode, but the VU canvas has an independent 240x62 rotated QSPI blit.
  // Leaving this hook disconnected makes each of the 15 VU updates invalidate
  // and retransmit the complete 480x320 LVGL frame.
  screen_.setNativeVisualizerBlit(this, nativeVisualizerBlit);
#endif
}

void DisplayManager::showBootLogo() {
  constexpr const char* kBootLogoPath =
      "/backgrounds/moon_radio_bootlogo_480x320.sr565";
  constexpr size_t kPixelBytes = static_cast<size_t>(kWidth) * kHeight * 2;
  constexpr size_t kFileBytes = 8 + kPixelBytes;

  if (!lvDisplay_) return;
  lv_obj_t* screen = lv_screen_active();
  if (bootLogoOverlay_) releaseBootLogo();

  File file = LittleFS.open(kBootLogoPath, FILE_READ);
  uint8_t header[8]{};
  const bool valid =
      file && file.size() == kFileBytes &&
      file.read(header, sizeof(header)) == sizeof(header) &&
      header[0] == 'S' && header[1] == 'R' && header[2] == '5' &&
      header[3] == '7' && header[4] == 224 && header[5] == 1 &&
      header[6] == 64 && header[7] == 1;

  if (valid) {
    bootLogoPixels_ = static_cast<uint8_t*>(heap_caps_malloc(
        kPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!bootLogoPixels_) {
      bootLogoPixels_ = static_cast<uint8_t*>(malloc(kPixelBytes));
    }
  }

  if (valid && bootLogoPixels_ &&
      file.read(bootLogoPixels_, kPixelBytes) == kPixelBytes) {
    bootLogoDescriptor_ = {};
    bootLogoDescriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    bootLogoDescriptor_.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
    bootLogoDescriptor_.header.w = kWidth;
    bootLogoDescriptor_.header.h = kHeight;
    bootLogoDescriptor_.header.stride = kWidth * 2;
    bootLogoDescriptor_.data_size = kPixelBytes;
    bootLogoDescriptor_.data = bootLogoPixels_;

    lv_obj_t* bootImage = lv_image_create(screen);
    lv_image_set_src(bootImage, &bootLogoDescriptor_);
    lv_obj_set_pos(bootImage, 0, 0);
    lv_obj_clear_flag(bootImage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(bootImage);
    bootLogoOverlay_ = bootImage;
  } else {
    // A normál kijelző még akkor is használható marad, ha egy hiányos
    // fájlrendszer-feltöltésből kimarad a bootlogo.
    if (bootLogoPixels_) {
      free(bootLogoPixels_);
      bootLogoPixels_ = nullptr;
    }
    Serial.printf("[display] bootlogo nem toltheto: %s\n", kBootLogoPath);
  }
  if (file) file.close();

  lv_refr_now(lvDisplay_);
}

void DisplayManager::releaseBootLogo() {
  if (bootLogoOverlay_) {
    lv_obj_delete(bootLogoOverlay_);
    bootLogoOverlay_ = nullptr;
  }
  if (bootLogoPixels_) {
    free(bootLogoPixels_);
    bootLogoPixels_ = nullptr;
  }
  bootLogoDescriptor_ = {};
}

void DisplayManager::nativeVisualizerBlit(void* context, int32_t x, int32_t y,
                                          int32_t w, int32_t h,
                                          const uint16_t* pixels) {
#if DISPLAY_PROFILE_AXS15231B
  auto* manager = static_cast<DisplayManager*>(context);
  if (!manager) return;
  // RadioController is the sole VU clock (67 ms on AXS, i.e. 15 FPS).
  // Do not issue a 62-row QSPI sequence: it is a different controller
  // raster shape from the factory's correct 320-row LVGL sequence.
  if (x != 0 || y != kAxsVuTop || w != kAxsVuWidth ||
       h != kAxsVuHeight || !pixels) {
    return;
  }
  // A natív AXS VU-képküldés megkerüli az LVGL objektumrétegeit. Ezért
  // felugró ablaknál itt is meg kell állítani, különben a közvetlen QSPI
  // frissítés a presetablak lefedő rétege fölé rajzol.
  if (!manager->visualizerActive()) return;
  manager->nativeVuPixels_ = pixels;
  if (!manager->axsVuFrame_ || !manager->axsVuFrameReady_) return;

  // WaveVu is RGB565_SWAPPED.  The retained factory source is native RGB565
  // and is byte-swapped once by flushFactoryRotated, exactly like LVGL.
  for (int32_t row = 0; row < kAxsVuHeight; ++row) {
    uint16_t* target = manager->axsVuFrame_ +
        static_cast<size_t>(kAxsVuTop + row) * kAxsVuWidth;
    const uint16_t* source = pixels +
        static_cast<size_t>(row) * kAxsVuWidth;
    for (int32_t col = 0; col < kAxsVuWidth; ++col) {
      target[col] = __builtin_bswap16(source[col]);
    }
  }
  manager->device_.flushAxsVuFrame(manager->axsVuFrame_);
#else
  (void)context;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)pixels;
#endif
}

void DisplayManager::overlayNativeVuIntoLvglFlush(
    uint16_t* lvglPixels, int32_t x, int32_t y, int32_t w, int32_t h) {
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  // LVGL's full-frame render is retained independently from the direct VU.
  // Just before that full frame is sent, replace only its VU rectangle with
  // the exact pixels already supplied to the direct transport.  This keeps
  // the once-per-second UI refresh from erasing or ghosting the VU.
  if (!visualizerActive() || !nativeVuPixels_ || !lvglPixels || x != 0 ||
      y != 0 || w != kWidth ||
      h != kHeight) {
    return;
  }
  for (int32_t row = 0; row < kAxsVuHeight; ++row) {
    uint16_t* target = lvglPixels +
        static_cast<size_t>(kAxsVuTop + row) * kWidth;
    const uint16_t* source = nativeVuPixels_ +
        static_cast<size_t>(row) * kAxsVuWidth;
    for (int32_t col = 0; col < kAxsVuWidth; ++col) {
      // The direct source is RGB565_SWAPPED; the full LVGL source is native
      // RGB565 and will be swapped by the normal factory transport.
      target[col] = __builtin_bswap16(source[col]);
    }
  }
#else
  (void)lvglPixels;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
#endif
}

void DisplayManager::refreshAxsVuFrameFromLvgl(const uint16_t* lvglPixels,
                                                int32_t x, int32_t y,
                                                int32_t w, int32_t h) {
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  if (!axsVuFrame_ || !lvglPixels || x != 0 || y != 0 || w != kWidth ||
      h != kHeight) {
    return;
  }
  // Retain the exact left half of a successful normal LVGL source.  The VU
  // callback subsequently replaces only its 240 x 62 pixels in this buffer.
  // Keeping the full 320-row source shape is what makes the following QSPI
  // sequence identical to the undistorted factory render.
  for (int32_t row = 0; row < kHeight; ++row) {
    const uint16_t* source = lvglPixels + static_cast<size_t>(row) * kWidth;
    uint16_t* target = axsVuFrame_ +
        static_cast<size_t>(row) * kAxsVuWidth;
    memcpy(target, source, static_cast<size_t>(kAxsVuWidth) * sizeof(uint16_t));
  }
  axsVuFrameReady_ = true;
#else
  (void)lvglPixels;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
#endif
}

void DisplayManager::setColorInverted(bool inverted) {
  colorInverted_ = inverted;
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  device_.setAxsFactoryColorInverted(colorInverted_);
  if (lvDisplay_) lv_obj_invalidate(lv_screen_active());
#else
#if DISPLAY_PROFILE_FNK0104S
  device_.invertDisplay(!colorInverted_);
#else
  device_.invertDisplay(colorInverted_);
#endif
#endif
}

bool DisplayManager::colorInverted() const { return colorInverted_; }

void DisplayManager::setScreenFlipped(bool flipped) {
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  // The panel's factory init and its first complete LVGL frame can both
  // touch controller state.  Re-send the persisted orientation even when
  // the logical value did not change, so a web-selected rotation survives
  // an ESP reset reliably.
  const bool changed = screenFlipped_ != flipped;
  screenFlipped_ = flipped;
  device_.setAxsFactoryScreenFlipped(screenFlipped_);
  if (changed && lvDisplay_) lv_obj_invalidate(lv_screen_active());
  return;
#else
  if (screenFlipped_ == flipped) return;
  screenFlipped_ = flipped;
#if DISPLAY_PROFILE_AXS15231B
  device_.setRotation(screenFlipped_ ? 2 : 0);
#elif DISPLAY_PROFILE_FNK0104S
  device_.setRotation(screenFlipped_ ? 1 : 3);
#else
  device_.setRotation(screenFlipped_ ? 3 : 1);
#endif
#endif
#if !DISPLAY_PROFILE_AXS15231B || !AXS_FACTORY_DIRECT_LVGL
  device_.fillScreen(TFT_BLACK);
#endif
  if (lvDisplay_) lv_obj_invalidate(lv_screen_active());
}

bool DisplayManager::screenFlipped() const { return screenFlipped_; }

void DisplayManager::update(const AudioSnapshot& audio,
                              const String& logoName,
                              const String& wifiText, const String& ipText,
                              const WeatherSnapshot& weather,
                              const DiagnosticsSnapshot& diagnostics,
                              bool backgroundEnabled,
                              const String& backgroundPath,
                              uint8_t backgroundOpacity) {
    if (wifiText.startsWith("AP ")) {
      showWifiSetup(ipText);
      return;
    }

    if (screenMode_ == ScreenMode::WifiSetup) {
      lv_obj_clean(lv_screen_active());
      screen_.create(fonts_, actions_);
      configureScreenNativeHooks();
      screen_.setVisualizerMode(visualizerMode_);
      screen_.setHeaderIpVisible(headerIpVisible_);
      screen_.setWifiDetailsVisible(wifiDetailsVisible_);
      screen_.setVolumeGraphicVisible(volumeGraphicVisible_);
      screen_.setBufferBarVisible(bufferBarVisible_);
      screen_.setDiagnosticsVisible(diagnosticsVisible_);
      screenMode_ = ScreenMode::Normal;
    }

#if DISPLAY_PROFILE_AXS15231B
    device_.setAxsRuntimeDiagnostics(audio.bufferFilledBytes,
                                     audio.bufferPercent,
                                     diagnostics.cpu0Percent,
                                     diagnostics.cpu1Percent,
                                     diagnostics.cpuValid);
#endif
    screen_.update(audio, logoName, wifiText, ipText, weather, diagnostics,
                   backgroundEnabled, backgroundPath, backgroundOpacity);
  }

void DisplayManager::updateVu(const AudioLevels& levels) {
  if (screenMode_ == ScreenMode::Normal && !selectorActive_ && !presetActive_) {
    screen_.updateVu(levels);
  }
}


void DisplayManager::setVolumeIndicator(uint8_t value) {
  if (screenMode_ == ScreenMode::Normal) {
    screen_.setVolumeIndicator(value);
  }
}

void DisplayManager::setVisualizerMode(WaveVu::Mode mode) {
  visualizerMode_ = mode;
  screen_.setVisualizerMode(mode);
}

WaveVu::Mode DisplayManager::visualizerMode() const {
  return visualizerMode_;
}

void DisplayManager::setHeaderIpVisible(bool visible) {
  headerIpVisible_ = visible;
  screen_.setHeaderIpVisible(visible);
}

void DisplayManager::showStartupIp(uint32_t durationMs) {
  screen_.showStartupIp(durationMs);
}

bool DisplayManager::headerIpVisible() const { return headerIpVisible_; }

void DisplayManager::setWifiDetailsVisible(bool visible) {
  wifiDetailsVisible_ = visible;
  screen_.setWifiDetailsVisible(visible);
}

bool DisplayManager::wifiDetailsVisible() const { return wifiDetailsVisible_; }

void DisplayManager::setVolumeGraphicVisible(bool visible) {
  volumeGraphicVisible_ = visible;
  screen_.setVolumeGraphicVisible(visible);
}

bool DisplayManager::volumeGraphicVisible() const {
  return volumeGraphicVisible_;
}

void DisplayManager::setBufferBarVisible(bool visible) {
  bufferBarVisible_ = visible;
  screen_.setBufferBarVisible(visible);
}

bool DisplayManager::bufferBarVisible() const { return bufferBarVisible_; }

void DisplayManager::setDiagnosticsVisible(bool visible) {
  diagnosticsVisible_ = visible;
  screen_.setDiagnosticsVisible(visible);
}

bool DisplayManager::diagnosticsVisible() const { return diagnosticsVisible_; }

void DisplayManager::openStationSelector() {
  if (presetActive_) hidePresets();
  openSelectorPending_ = true;
}

bool DisplayManager::moveStationSelector(int8_t delta) {
  if (!selectorActive_ || !selectorRoller_ || delta == 0 ||
      !actions_.stationCount)
    return false;

  const size_t count = actions_.stationCount(actions_.context);
  if (count == 0) return false;

  const uint32_t now = millis();
  const int8_t direction = delta > 0 ? 1 : -1;
  if (selectorLastMoveAt_ != 0 && now - selectorLastMoveAt_ < 140 &&
      direction == selectorLastMoveDirection_ &&
      selectorArcNextDurationMs_ == 220) {
    selectorArcNextDurationMs_ = 90;
  }
  selectorLastMoveAt_ = now;
  selectorLastMoveDirection_ = direction;

  int32_t selected = static_cast<int32_t>(lv_roller_get_selected(selectorRoller_));
  selected += delta;
  if (selected < 0) selected = 0;
  if (selected >= static_cast<int32_t>(count)) {
    selected = static_cast<int32_t>(count) - 1;
  }

  if (selected == static_cast<int32_t>(lv_roller_get_selected(selectorRoller_))) {
    return true;
  }

  lv_roller_set_selected(selectorRoller_, static_cast<uint32_t>(selected),
                         LV_ANIM_OFF);

  const uint16_t animationDuration = selectorArcNextDurationMs_;
  selectorArcNextDurationMs_ = 220;
  animateStationSelectorArc(selected, animationDuration);
  selectorPendingIndex_ = selected;
  selectorSelectAt_ = millis() + kStationSelectDelayMs;
  selectorCountdown_ = 0;
  if (selectorStatusLabel_) {
    lv_label_set_text(selectorStatusLabel_, "Indítás 3 mp múlva");
  }
  return true;
}

bool DisplayManager::stationSelectorActive() const { return selectorActive_; }

void DisplayManager::showMaintenance() {
  resetStationSelector();
  resetPresets();
  screenMode_ = ScreenMode::Maintenance;
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  // AXS retains a full-height source for the native VU transport.  While the
  // maintenance screen is rendered, do not composite its last VU pixels into
  // that new screen; the VU widget has just been removed and must disappear.
  nativeVuPixels_ = nullptr;
  axsVuFrameReady_ = false;
#endif
  lv_obj_t* screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x080B12), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(0xF1F5F9), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* accent = lv_obj_create(screen);
  lv_obj_set_size(accent, 8, 210);
  lv_obj_set_pos(accent, 38, 55);
  lv_obj_set_style_bg_color(accent, lv_color_hex(0x22D3EE), 0);
  lv_obj_set_style_border_width(accent, 0, 0);
  lv_obj_set_style_radius(accent, 4, 0);
  lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "KARBANTARTÁSI MÓD");
  lv_obj_set_pos(title, 70, 68);
  lv_obj_set_width(title, 380);
  lv_obj_set_style_text_font(title, fonts_.large(), 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x22D3EE), 0);

  lv_obj_t* subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "LittleFS fájlkezelő csatlakozott");
  lv_obj_set_pos(subtitle, 72, 132);
  lv_obj_set_width(subtitle, 370);
  lv_obj_set_style_text_font(subtitle, fonts_.regular(), 0);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0xF1F5F9), 0);

  lv_obj_t* hint = lv_label_create(screen);
  lv_label_set_text(hint,
                    "Fájlművelet van folyamatban.\n"
                    "Ha végeztél, használd az ÚJRAINDÍTÁS gombot.");
  lv_obj_set_pos(hint, 72, 187);
  lv_obj_set_width(hint, 370);
  lv_obj_set_style_text_font(hint, fonts_.small(), 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_line_space(hint, 7, 0);

  // Azonnal kirajzoljuk, mert karbantartási módban az LVGL időzítője
  // ezután szándékosan nem fut tovább: így a fontfájlok is cserélhetők.
  lv_refr_now(lvDisplay_);
}

void DisplayManager::showWifiSetup(const String& address) {
  if (screenMode_ == ScreenMode::WifiSetup) return;
  resetStationSelector();
  resetPresets();
  screenMode_ = ScreenMode::WifiSetup;

  lv_obj_t* screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x080B12), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(0xF1F5F9), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* accent = lv_obj_create(screen);
  lv_obj_set_size(accent, 8, 210);
  lv_obj_set_pos(accent, 38, 55);
  lv_obj_set_style_bg_color(accent, lv_color_hex(0x22D3EE), 0);
  lv_obj_set_style_border_width(accent, 0, 0);
  lv_obj_set_style_radius(accent, 4, 0);
  lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "WI-FI BEÁLLÍTÁS");
  lv_obj_set_pos(title, 70, 68);
  lv_obj_set_width(title, 380);
  lv_obj_set_style_text_font(title, fonts_.large(), 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x22D3EE), 0);

  lv_obj_t* subtitle = lv_label_create(screen);
  lv_label_set_text_fmt(subtitle, "Kapcsolódj ehhez: Moon-Radio-Setup\nNyisd meg: http://%s/",
                        address.c_str());
  lv_obj_set_pos(subtitle, 72, 132);
  lv_obj_set_width(subtitle, 370);
  lv_obj_set_style_text_font(subtitle, fonts_.regular(), 0);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0xF1F5F9), 0);
  lv_obj_set_style_text_line_space(subtitle, 8, 0);

  lv_obj_t* hint = lv_label_create(screen);
  lv_label_set_text(hint,
                    "A wifi.txt hiányzik vagy hibás.\n"
                    "Nyisd meg a helyi beállító oldalt,\n"
                    "mentsd el a Wi-Fi nevét és jelszavát,\n"
                    "majd a rádió újraindul.");
  lv_obj_set_pos(hint, 72, 205);
  lv_obj_set_width(hint, 370);
  lv_obj_set_style_text_font(hint, fonts_.small(), 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_line_space(hint, 7, 0);
}

bool DisplayManager::visualizerActive() const {
  return !selectorActive_ && !presetActive_ && screen_.visualizerActive();
}

void DisplayManager::setBrightness(uint8_t value) {
  brightness_ = value;
  ledcWrite(BRIGHTNESS_PIN, brightness_);
}

uint8_t DisplayManager::brightness() const { return brightness_; }

void DisplayManager::flush(lv_display_t* display, const lv_area_t* area,
                           uint8_t* pixels) {
  if (!instance_) return;
  const uint32_t width = area->x2 - area->x1 + 1;
  const uint32_t height = area->y2 - area->y1 + 1;
  const uint32_t pixelCount = width * height;

#if DISPLAY_PROFILE_AXS15231B
  instance_->device_.noteAxsLvglFlushArea(area->x1, area->y1, area->x2,
                                          area->y2, pixelCount);
#if AXS_FACTORY_DIRECT_LVGL
  instance_->overlayNativeVuIntoLvglFlush(
      reinterpret_cast<uint16_t*>(pixels), area->x1, area->y1,
      static_cast<int32_t>(width), static_cast<int32_t>(height));
  instance_->refreshAxsVuFrameFromLvgl(
      reinterpret_cast<uint16_t*>(pixels), area->x1, area->y1,
      static_cast<int32_t>(width), static_cast<int32_t>(height));
  instance_->device_.flushAxsLvglRect(
      area->x1, area->y1, static_cast<int32_t>(width),
      static_cast<int32_t>(height), reinterpret_cast<uint16_t*>(pixels));
  lv_display_flush_ready(display);
  return;
#endif
#endif
  instance_->device_.startWrite();
  instance_->device_.setAddrWindow(area->x1, area->y1, width, height);
  instance_->device_.writePixels(reinterpret_cast<uint16_t*>(pixels),
                                pixelCount, true);
  instance_->device_.endWrite();
  lv_display_flush_ready(display);
}

void DisplayManager::readTouch(lv_indev_t*, lv_indev_data_t* data) {
#if !TOUCH_ENABLED
  data->state = LV_INDEV_STATE_RELEASED;
  return;
#else
  if (!instance_) return;
  uint16_t x = 0;
  uint16_t y = 0;
  if (instance_->device_.getTouch(&x, &y)) {
    if (!instance_->touchActive_) {
      instance_->touchActive_ = true;
      instance_->volumeSwipe_ = false;
      instance_->verticalSwipe_ = false;
      instance_->touchStartX_ = x;
      instance_->touchStartY_ = y;
      instance_->lastSwipeX_ = x;
      instance_->lastSelectorTouchY_ = y;
      instance_->selectorTouchStartAt_ = millis();
    }

    const int32_t totalX = static_cast<int32_t>(x) - instance_->touchStartX_;
    const int32_t totalY = static_cast<int32_t>(y) - instance_->touchStartY_;
    if (instance_->selectorActive_ && !instance_->presetActive_) {
      // A roller saját pixel-alapú görgetése kis mozdulatokra is könnyen
      // sort vált. A választót szándékosan egyetlen, nagyobb lépésű
      // érintésvezérléssel kezeljük.
      const int32_t movement = static_cast<int32_t>(y) -
                               instance_->lastSelectorTouchY_;
      const int8_t steps = static_cast<int8_t>(movement / kStationScrollStep);
      if (steps != 0) {
        const uint32_t elapsed = millis() - instance_->selectorTouchStartAt_;
        const int32_t totalDistance = abs(totalY);
        // A gyors, nagyobb seprésnél rövidebb animációt használunk. A
        // finom mozdulat továbbra is pontosan egy sort léptet.
        const bool fastSweep = elapsed > 0 &&
                               (totalDistance * 1000L) / elapsed >= 450;
        instance_->selectorArcNextDurationMs_ = fastSweep ? 90 : 220;
        instance_->moveStationSelector(static_cast<int8_t>(-steps));
        instance_->lastSelectorTouchY_ += steps * kStationScrollStep;
      }
    } else if (!instance_->selectorActive_ && !instance_->presetActive_) {
      if (!instance_->volumeSwipe_ && !instance_->verticalSwipe_ &&
          abs(totalY) >= kVerticalSwipeThreshold &&
          abs(totalY) > abs(totalX)) {
        instance_->verticalSwipe_ = true;
      }

      if (!instance_->volumeSwipe_ && !instance_->verticalSwipe_ &&
          abs(totalX) >= 18 && abs(totalX) > abs(totalY)) {
        instance_->volumeSwipe_ = true;
        instance_->lastSwipeX_ = instance_->touchStartX_;
      }

      if (instance_->volumeSwipe_) {
        const int32_t movement =
            static_cast<int32_t>(x) - instance_->lastSwipeX_;
        const int8_t steps = static_cast<int8_t>(movement / 22);
        if (steps != 0 && instance_->actions_.adjustVolume) {
          instance_->actions_.adjustVolume(instance_->actions_.context, steps);
          instance_->lastSwipeX_ += static_cast<int32_t>(steps) * 22;
        }
      }
    }

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    if (instance_->touchActive_ && instance_->verticalSwipe_ &&
        !instance_->selectorActive_ && !instance_->presetActive_) {
      instance_->openSelectorPending_ = true;
    }
    instance_->touchActive_ = false;
    instance_->volumeSwipe_ = false;
    instance_->verticalSwipe_ = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
#endif
}

void DisplayManager::showStationSelector() {
  if (selectorActive_ || !actions_.stationCount || !actions_.stationName ||
      !actions_.currentStation || !actions_.selectStation)
    return;

  const size_t count = actions_.stationCount(actions_.context);
  if (count == 0) return;

  selectorConstructing_ = true;
  selectorPendingIndex_ = -1;
  selectorSelectAt_ = 0;
  selectorCountdown_ = 0;

  selectorOverlay_ = lv_obj_create(lv_screen_active());
  lv_obj_set_size(selectorOverlay_, kWidth, kHeight);
  lv_obj_set_pos(selectorOverlay_, 0, 0);
  lv_obj_set_style_bg_color(selectorOverlay_, lv_color_hex(0x080B12), 0);
  lv_obj_set_style_bg_opa(selectorOverlay_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(selectorOverlay_, 0, 0);
  lv_obj_set_style_radius(selectorOverlay_, 0, 0);
  lv_obj_set_style_pad_all(selectorOverlay_, 0, 0);
  lv_obj_clear_flag(selectorOverlay_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* header = lv_obj_create(selectorOverlay_);
  lv_obj_set_size(header, kWidth, 48);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x111827), 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(header);
  lv_label_set_text(title, "ÁLLOMÁSOK");
  lv_obj_set_pos(title, 16, 12);
  lv_obj_set_style_text_font(title, fonts_.large(), 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF1F5F9), 0);

  selectorStatusLabel_ = lv_label_create(header);
  lv_label_set_text(selectorStatusLabel_, "Görgess a listában");
  lv_obj_set_pos(selectorStatusLabel_, 190, 13);
  lv_obj_set_width(selectorStatusLabel_, 231);
  lv_obj_set_style_text_font(selectorStatusLabel_, fonts_.regular(), 0);
  lv_obj_set_style_text_color(selectorStatusLabel_, lv_color_hex(0x7DD3FC), 0);
  lv_obj_set_style_text_align(selectorStatusLabel_, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(selectorStatusLabel_, LV_LABEL_LONG_DOT);

  lv_obj_t* close = lv_button_create(header);
  lv_obj_set_size(close, 48, 48);
  lv_obj_set_pos(close, 432, 0);
  lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(close, 0, 0);
  lv_obj_set_style_radius(close, 0, 0);
  lv_obj_add_event_cb(close, closeSelectorEvent, LV_EVENT_CLICKED, this);
  lv_obj_t* closeLabel = lv_label_create(close);
  lv_label_set_text(closeLabel, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(closeLabel, fonts_.symbol(), 0);
  lv_obj_set_style_text_color(closeLabel, lv_color_hex(0x94A3B8), 0);
  lv_obj_center(closeLabel);

  String options;
  options.reserve(count * 28);
  for (size_t index = 0; index < count; ++index) {
    if (index > 0) options += '\n';
    String name = actions_.stationName(actions_.context, index);
    name.replace("\n", " ");
    name.replace("\r", " ");
    options += name;
  }

  selectorRoller_ = lv_roller_create(selectorOverlay_);
  lv_obj_set_size(selectorRoller_, 448, 226);
  lv_obj_set_pos(selectorRoller_, 16, 56);
  lv_roller_set_options(selectorRoller_, options.c_str(),
                        LV_ROLLER_MODE_NORMAL);
  lv_obj_set_style_bg_color(selectorRoller_, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_color(selectorRoller_, lv_color_hex(0x263246), 0);
  lv_obj_set_style_border_width(selectorRoller_, 1, 0);
  lv_obj_set_style_radius(selectorRoller_, 12, 0);
  lv_obj_set_style_text_font(selectorRoller_, fonts_.regular(), 0);
  lv_obj_set_style_text_color(selectorRoller_, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_align(selectorRoller_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(selectorRoller_, 8, 0);
  lv_obj_set_style_bg_color(selectorRoller_, lv_color_hex(0x22D3EE),
                            LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(selectorRoller_, LV_OPA_COVER, LV_PART_SELECTED);
  lv_obj_set_style_text_color(selectorRoller_, lv_color_hex(0x071018),
                              LV_PART_SELECTED);
  lv_obj_set_style_text_font(selectorRoller_, fonts_.regular(),
                             LV_PART_SELECTED);
  lv_obj_add_event_cb(selectorRoller_, stationSelectorEvent, LV_EVENT_PRESSED,
                      this);
  lv_obj_add_event_cb(selectorRoller_, stationSelectorEvent,
                      LV_EVENT_VALUE_CHANGED, this);

  size_t selected = actions_.currentStation(actions_.context);
  if (selected >= count) selected = 0;
  lv_roller_set_selected(selectorRoller_, static_cast<uint32_t>(selected),
                         LV_ANIM_OFF);
  selectorArcPosition100_ = static_cast<int32_t>(selected) * 100;
  selectorArcTargetIndex_ = static_cast<int32_t>(selected);

  // A görgetés és az érintés kezelését továbbra is az LVGL roller végzi.
  // A saját szöveges réteg csak a látványt rajzolja körív-szerűen fölé.
  lv_obj_set_style_bg_opa(selectorRoller_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(selectorRoller_, LV_OPA_TRANSP, LV_PART_SELECTED);
  lv_obj_set_style_border_width(selectorRoller_, 0, 0);
  lv_obj_set_style_text_opa(selectorRoller_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_opa(selectorRoller_, LV_OPA_TRANSP, LV_PART_SELECTED);
  lv_obj_clear_flag(selectorRoller_, LV_OBJ_FLAG_SCROLLABLE);

  // Részleges, dekoratív körív a kijelző jobb oldalán. A középpont a
  // kijelző bal oldalán kívülre esik, ezért a jobb oldali, ')' alakú rész
  // marad látható, ahogy a látványterven.
  // Az ív végei is fokozatosan halványulnak, mint a fókuszvonalaké. A
  // szegmensek átfedés nélkül, egymás mellé illesztve adják a jobb oldali
  // ')' alakú ívet, középen erősebben, felül és alul finomabban.
  constexpr int16_t arcStart[kStationArcFadeSegments] = {
      300, 320, 340, 350, 5, 20, 40};
  constexpr int16_t arcEnd[kStationArcFadeSegments] = {
      320, 340, 350, 5, 20, 40, 60};
  constexpr lv_opa_t arcOpacity[kStationArcFadeSegments] = {
      LV_OPA_50, LV_OPA_50, LV_OPA_50, LV_OPA_50,
      LV_OPA_50, LV_OPA_50, LV_OPA_50};
  for (uint8_t segment = 0; segment < kStationArcFadeSegments; ++segment) {
    selectorArcGlow_[segment] = lv_arc_create(selectorOverlay_);
    lv_obj_set_size(selectorArcGlow_[segment], 840, 840);
    lv_obj_set_pos(selectorArcGlow_[segment], -420, -260);
    lv_arc_set_angles(selectorArcGlow_[segment], arcStart[segment],
                      arcEnd[segment]);
    lv_arc_set_bg_angles(selectorArcGlow_[segment], arcStart[segment],
                         arcEnd[segment]);
    lv_obj_set_style_arc_width(selectorArcGlow_[segment], 5,
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(selectorArcGlow_[segment],
                               lv_color_hex(0x22D3EE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(
        selectorArcGlow_[segment],
        LV_OPA_20,
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(selectorArcGlow_[segment], 0, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(selectorArcGlow_[segment], LV_OPA_TRANSP,
                             LV_PART_MAIN);
    lv_obj_set_style_opa(selectorArcGlow_[segment], LV_OPA_COVER, 0);
    lv_obj_set_style_opa(selectorArcGlow_[segment], LV_OPA_TRANSP,
                         LV_PART_KNOB);
    lv_obj_clear_flag(selectorArcGlow_[segment], LV_OBJ_FLAG_CLICKABLE);

    selectorArcGuide_[segment] = lv_arc_create(selectorOverlay_);
    lv_obj_set_size(selectorArcGuide_[segment], 840, 840);
    lv_obj_set_pos(selectorArcGuide_[segment], -420, -260);
    lv_arc_set_angles(selectorArcGuide_[segment], arcStart[segment],
                      arcEnd[segment]);
    lv_arc_set_bg_angles(selectorArcGuide_[segment], arcStart[segment],
                         arcEnd[segment]);
    lv_obj_set_style_arc_width(selectorArcGuide_[segment], 1,
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(selectorArcGuide_[segment],
                               lv_color_hex(0x22D3EE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(selectorArcGuide_[segment], arcOpacity[segment],
                             LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(selectorArcGuide_[segment], 0, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(selectorArcGuide_[segment], LV_OPA_TRANSP,
                             LV_PART_MAIN);
    lv_obj_set_style_opa(selectorArcGuide_[segment], LV_OPA_COVER, 0);
    lv_obj_set_style_opa(selectorArcGuide_[segment], LV_OPA_TRANSP,
                         LV_PART_KNOB);
    lv_obj_clear_flag(selectorArcGuide_[segment], LV_OBJ_FLAG_CLICKABLE);
  }

  // A korábbi szegmensek helyett a tényleges megjelenítéshez egyetlen,
  // folyamatos ívet használunk; a többi objektumot elrejtjük.
  for (uint8_t segment = 1; segment < kStationArcFadeSegments; ++segment) {
    lv_obj_add_flag(selectorArcGlow_[segment], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(selectorArcGuide_[segment], LV_OBJ_FLAG_HIDDEN);
  }
  lv_arc_set_angles(selectorArcGlow_[0], 300, 60);
  lv_arc_set_bg_angles(selectorArcGlow_[0], 300, 60);
  lv_arc_set_angles(selectorArcGuide_[0], 300, 60);
  lv_arc_set_bg_angles(selectorArcGuide_[0], 300, 60);

  // A két fókuszvonal közötti nagyon halvány, lekerekített fénymező. Ez
  // adja a terven látható köztes kékes-türkiz tónust, de nem takarja el az
  // aktív állomásnevet.
  selectorFocusBand_ = lv_obj_create(selectorOverlay_);
  lv_obj_set_size(selectorFocusBand_, 360, 52);
  lv_obj_set_style_bg_color(selectorFocusBand_, lv_color_hex(0x164E63), 0);
  lv_obj_set_style_bg_opa(selectorFocusBand_, LV_OPA_20, 0);
  lv_obj_set_style_border_width(selectorFocusBand_, 0, 0);
  lv_obj_set_style_radius(selectorFocusBand_, 18, 0);
  lv_obj_clear_flag(selectorFocusBand_, LV_OBJ_FLAG_CLICKABLE);

  // A látványterv szerinti két vékony fókuszvonal. Több, eltérő
  // áttetszőségű szegmens adja a finom fényelhalást a végeknél.
  constexpr int32_t focusSegmentWidth = 40;
  for (uint8_t segment = 0; segment < kStationFocusSegments; ++segment) {
    selectorFocusGlowTop_[segment] = lv_obj_create(selectorOverlay_);
    selectorFocusGlowBottom_[segment] = lv_obj_create(selectorOverlay_);
    selectorFocusTopSegments_[segment] = lv_obj_create(selectorOverlay_);
    selectorFocusBottomSegments_[segment] = lv_obj_create(selectorOverlay_);
    for (lv_obj_t* glow : {selectorFocusGlowTop_[segment],
                           selectorFocusGlowBottom_[segment]}) {
      lv_obj_set_size(glow, focusSegmentWidth, 6);
      lv_obj_set_style_bg_color(glow, lv_color_hex(0x22D3EE), 0);
      lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);
      lv_obj_set_style_border_width(glow, 0, 0);
      lv_obj_set_style_radius(glow, 3, 0);
      lv_obj_clear_flag(glow, LV_OBJ_FLAG_CLICKABLE);
    }
    for (lv_obj_t* line : {selectorFocusTopSegments_[segment],
                           selectorFocusBottomSegments_[segment]}) {
      lv_obj_set_size(line, focusSegmentWidth, 2);
      lv_obj_set_style_bg_color(line, lv_color_hex(0x22D3EE), 0);
      lv_obj_set_style_bg_opa(line, LV_OPA_60, 0);
      lv_obj_set_height(line, 1);
      lv_obj_set_style_border_width(line, 0, 0);
      lv_obj_set_style_radius(line, 1, 0);
      lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }
  }
  for (uint8_t segment = 1; segment < kStationFocusSegments; ++segment) {
    lv_obj_add_flag(selectorFocusGlowTop_[segment], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(selectorFocusGlowBottom_[segment], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(selectorFocusTopSegments_[segment], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(selectorFocusBottomSegments_[segment], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_size(selectorFocusGlowTop_[0], 360, 6);
  lv_obj_set_size(selectorFocusGlowBottom_[0], 360, 6);
  lv_obj_set_size(selectorFocusTopSegments_[0], 360, 1);
  lv_obj_set_size(selectorFocusBottomSegments_[0], 360, 1);

  for (uint8_t slot = 0; slot < kStationArcSlots; ++slot) {
    selectorArcLabels_[slot] = lv_label_create(selectorOverlay_);
    lv_obj_set_width(selectorArcLabels_[slot], 300);
    // Az állomásnév egy sorban maradjon; hossz esetén vágjuk le, ne tördeljük.
    lv_label_set_long_mode(selectorArcLabels_[slot], LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(selectorArcLabels_[slot], LV_TEXT_ALIGN_LEFT,
                                0);
    lv_obj_clear_flag(selectorArcLabels_[slot], LV_OBJ_FLAG_CLICKABLE);
  }
  refreshStationSelectorArc();

  lv_obj_t* hint = lv_label_create(selectorOverlay_);
  lv_label_set_text(hint, "A kijelölt adó 3 másodperc múlva elindul");
  lv_obj_set_pos(hint, 16, 294);
  lv_obj_set_width(hint, 448);
  lv_obj_set_style_text_font(hint, fonts_.regular(), 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), 0);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_move_foreground(selectorOverlay_);
  selectorActive_ = true;
  selectorConstructing_ = false;
}

void DisplayManager::refreshStationSelectorArc() {
  if (!selectorRoller_ || !actions_.stationCount || !actions_.stationName)
    return;

  const size_t count = actions_.stationCount(actions_.context);
  if (count == 0) return;

  const int32_t target = selectorArcTargetIndex_;
  const float visualPosition =
      static_cast<float>(selectorArcPosition100_) / 100.0f;
  constexpr int32_t centerY = 166;
  constexpr int32_t rowHeight = 34;
  constexpr int32_t rowGap = 6;
  const int32_t activeHeight = lv_font_get_line_height(fonts_.large());
  const int32_t neighbourHeight = lv_font_get_line_height(fonts_.small());
  const float activeVisualOffset = static_cast<float>(target) - visualPosition;
  const float activeCenterY = centerY + activeVisualOffset * rowHeight;
  // Nagyobb, jobbra tolt képzeletbeli kör: a hosszú neveknek több hely jut.
  constexpr int32_t leftMostX = 104;

  if (selectorFocusTopSegments_[0]) {
    const int32_t focusY = static_cast<int32_t>(activeCenterY);
    const int32_t activeHeight = lv_font_get_line_height(fonts_.large());
    const int32_t topY = focusY - activeHeight / 2 - 7;
    const int32_t bottomY = focusY + activeHeight / 2 + 5;
    if (selectorFocusBand_) {
      lv_obj_set_pos(selectorFocusBand_, 32, topY + 2);
      lv_obj_set_size(selectorFocusBand_, 360,
                      max<int32_t>(8, bottomY - topY - 2));
    }
    for (uint8_t segment = 0; segment < kStationFocusSegments; ++segment) {
      const int32_t x = 32 + static_cast<int32_t>(segment) * 40;
      lv_obj_set_pos(selectorFocusGlowTop_[segment], x, topY - 2);
      lv_obj_set_pos(selectorFocusGlowBottom_[segment], x, bottomY - 2);
      lv_obj_set_pos(selectorFocusTopSegments_[segment], x, topY);
      lv_obj_set_pos(selectorFocusBottomSegments_[segment], x, bottomY);
    }
  }

  for (uint8_t slot = 0; slot < kStationArcSlots; ++slot) {
    lv_obj_t* label = selectorArcLabels_[slot];
    if (!label) continue;

    const int32_t offset = static_cast<int32_t>(slot) -
                           static_cast<int32_t>(kStationArcSlots / 2);
    const int32_t index = target + offset;
    if (index < 0 || index >= static_cast<int32_t>(count)) {
      lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    String name = actions_.stationName(actions_.context,
                                       static_cast<size_t>(index));
    name.replace("\n", " ");
    name.replace("\r", " ");
    lv_label_set_text(label, name.c_str());

    const float visualOffset = static_cast<float>(index) - visualPosition;
    const float distance = fabsf(visualOffset);
    // Laposabb parabola-közelítés egy jobbra eltolt, nagyobb körívhez.
    const int32_t x = index == target
                          ? 32
                          : leftMostX +
                                static_cast<int32_t>(distance * distance * 6.0f);
    const int32_t relative = index - target;
    int32_t y = 0;
    if (relative == 0) {
      y = static_cast<int32_t>(activeCenterY) - activeHeight / 2;
    } else if (relative > 0) {
      y = static_cast<int32_t>(activeCenterY) + activeHeight / 2 + rowGap +
          (relative - 1) * (neighbourHeight + rowGap);
    } else {
      const int32_t distanceFromActive = -relative;
      y = static_cast<int32_t>(activeCenterY) - activeHeight / 2 - rowGap -
          neighbourHeight - (distanceFromActive - 1) *
                                  (neighbourHeight + rowGap);
    }
    lv_obj_set_pos(label, x, y);

    if (index == target) {
      lv_obj_set_style_text_font(label, fonts_.large(), 0);
      lv_obj_set_style_text_color(label, lv_color_hex(0x22D3EE), 0);
    } else {
      lv_obj_set_style_text_font(label, fonts_.small(), 0);
      lv_obj_set_style_text_color(label, lv_color_hex(0x94A3B8), 0);
    }
  }
}

void DisplayManager::animateStationSelectorArc(int32_t targetIndex,
                                               uint16_t durationMs) {
  if (!selectorRoller_) return;

  selectorArcTargetIndex_ = targetIndex;
  const int32_t targetPosition = targetIndex * 100;
  lv_anim_delete(this, stationArcAnimExec);
  // A listaváltás ne mozgassa el az egész állomáslistát. A kiválasztás
  // legyen diszkrét: csak az új állapot és a fókusz színezése változzon.
  // A duration paramétert megtartjuk az interfészben a későbbi finom
  // áttűnéshez, de jelenleg szándékosan nem indítunk pozícióanimációt.
  (void)durationMs;
  selectorArcPosition100_ = targetPosition;
  refreshStationSelectorArc();
}

void DisplayManager::stationArcAnimExec(void* var, int32_t value) {
  auto* manager = static_cast<DisplayManager*>(var);
  if (!manager) return;
  manager->selectorArcPosition100_ = value;
  manager->refreshStationSelectorArc();
}

void DisplayManager::hideStationSelector() {
  if (selectorOverlay_) lv_obj_delete(selectorOverlay_);
  resetStationSelector();
}

void DisplayManager::resetStationSelector() {
  selectorOverlay_ = nullptr;
  selectorRoller_ = nullptr;
  for (auto& label : selectorArcLabels_) label = nullptr;
  selectorStatusLabel_ = nullptr;
  selectorFocusBar_ = nullptr;
  selectorFocusBarBottom_ = nullptr;
  selectorFocusBand_ = nullptr;
  for (auto& line : selectorFocusTopSegments_) line = nullptr;
  for (auto& line : selectorFocusBottomSegments_) line = nullptr;
  for (auto& glow : selectorFocusGlowTop_) glow = nullptr;
  for (auto& glow : selectorFocusGlowBottom_) glow = nullptr;
  for (auto& arc : selectorArcGuide_) arc = nullptr;
  for (auto& glow : selectorArcGlow_) glow = nullptr;
  selectorArcPosition100_ = 0;
  selectorArcTargetIndex_ = 0;
  selectorArcNextDurationMs_ = 220;
  selectorLastMoveAt_ = 0;
  selectorLastMoveDirection_ = 0;
  selectorPendingIndex_ = -1;
  selectorSelectAt_ = 0;
  selectorCountdown_ = 0;
  selectorActive_ = false;
  selectorConstructing_ = false;
  openSelectorPending_ = false;
}

void DisplayManager::updateStationSelector() {
  if (!selectorActive_ || selectorPendingIndex_ < 0 ||
      selectorSelectAt_ == 0)
    return;

  const uint32_t now = millis();
  const int32_t remaining =
      static_cast<int32_t>(selectorSelectAt_ - now);
  if (remaining <= 0) {
    const size_t selected = static_cast<size_t>(selectorPendingIndex_);
    hideStationSelector();
    actions_.selectStation(actions_.context, selected);
    return;
  }

  const uint8_t seconds =
      static_cast<uint8_t>((static_cast<uint32_t>(remaining) + 999U) / 1000U);
  if (seconds != selectorCountdown_ && selectorStatusLabel_) {
    selectorCountdown_ = seconds;
    char text[32];
    snprintf(text, sizeof(text), "Indítás %u mp múlva", seconds);
    lv_label_set_text(selectorStatusLabel_, text);
  }
}

void DisplayManager::stationSelectorEvent(lv_event_t* event) {
  auto* manager =
      static_cast<DisplayManager*>(lv_event_get_user_data(event));
  if (!manager || manager->selectorConstructing_ ||
      !manager->selectorRoller_)
    return;

  if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
    manager->selectorPendingIndex_ = -1;
    manager->selectorSelectAt_ = 0;
    manager->selectorCountdown_ = 0;
    if (manager->selectorStatusLabel_)
      lv_label_set_text(manager->selectorStatusLabel_, "Görgess a listában");
    return;
  }

  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    manager->selectorPendingIndex_ = static_cast<int32_t>(
        lv_roller_get_selected(manager->selectorRoller_));
    manager->animateStationSelectorArc(manager->selectorPendingIndex_);
    manager->selectorSelectAt_ = millis() + kStationSelectDelayMs;
    manager->selectorCountdown_ = 0;
  }
}

void DisplayManager::closeSelectorEvent(lv_event_t* event) {
  auto* manager =
      static_cast<DisplayManager*>(lv_event_get_user_data(event));
  if (manager) manager->hideStationSelector();
}

void DisplayManager::showPresets() {
  if (presetActive_ || !actions_.stationCount || !actions_.stationName ||
      !actions_.stationUrl || !actions_.currentStation ||
      !actions_.selectStation)
    return;
  if (selectorActive_) hideStationSelector();
  // Ezt a jelzőt már az objektumok felépítése előtt állítsuk be: az AXS
  // közvetlen VU-útja nem az LVGL fedőrétegén keresztül rajzol.
  presetActive_ = true;

  presetOverlay_ = lv_obj_create(lv_screen_active());
  lv_obj_set_size(presetOverlay_, kWidth, kHeight);
  lv_obj_set_pos(presetOverlay_, 0, 0);
  lv_obj_set_style_bg_color(presetOverlay_, lv_color_hex(0x080B12), 0);
  lv_obj_set_style_bg_opa(presetOverlay_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(presetOverlay_, 0, 0);
  lv_obj_set_style_radius(presetOverlay_, 0, 0);
  lv_obj_set_style_pad_all(presetOverlay_, 0, 0);
  lv_obj_clear_flag(presetOverlay_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* header = lv_obj_create(presetOverlay_);
  lv_obj_set_size(header, kWidth, 46);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x111827), 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  constexpr const char* modeNames[3] = {"LEJÁTSZÁS", "MENTÉS", "TÖRLÉS"};
  for (uint8_t index = 0; index < 3; ++index) {
    presetModeButtons_[index] = lv_button_create(header);
    lv_obj_set_size(presetModeButtons_[index], 138, 46);
    lv_obj_set_pos(presetModeButtons_[index], index * 138, 0);
    lv_obj_set_style_shadow_width(presetModeButtons_[index], 0, 0);
    lv_obj_set_style_radius(presetModeButtons_[index], 0, 0);
    lv_obj_set_style_border_width(presetModeButtons_[index], 0, 0);
    lv_obj_add_event_cb(presetModeButtons_[index], presetModeEvent,
                        LV_EVENT_CLICKED, this);

    lv_obj_t* label = lv_label_create(presetModeButtons_[index]);
    lv_label_set_text(label, modeNames[index]);
    lv_obj_set_style_text_font(label, fonts_.regular(), 0);
    lv_obj_center(label);
  }

  lv_obj_t* close = lv_button_create(header);
  lv_obj_set_size(close, 66, 46);
  lv_obj_set_pos(close, 414, 0);
  lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(close, 0, 0);
  lv_obj_set_style_radius(close, 0, 0);
  lv_obj_add_event_cb(close, closePresetsEvent, LV_EVENT_CLICKED, this);
  lv_obj_t* closeLabel = lv_label_create(close);
  lv_label_set_text(closeLabel, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(closeLabel, fonts_.symbol(), 0);
  lv_obj_set_style_text_color(closeLabel, lv_color_hex(0x94A3B8), 0);
  lv_obj_center(closeLabel);

  presetStatusLabel_ = lv_label_create(presetOverlay_);
  lv_obj_set_pos(presetStatusLabel_, 10, 50);
  lv_obj_set_size(presetStatusLabel_, 460, 24);
  lv_obj_set_style_text_font(presetStatusLabel_, fonts_.regular(), 0);
  lv_obj_set_style_text_color(presetStatusLabel_, lv_color_hex(0x7DD3FC), 0);
  lv_obj_set_style_text_align(presetStatusLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(presetStatusLabel_, LV_LABEL_LONG_DOT);

  constexpr int16_t slotWidth = 224;
  constexpr int16_t slotHeight = 44;
  constexpr int16_t slotGapX = 8;
  constexpr int16_t slotGapY = 6;
  for (uint8_t slot = 0; slot < PresetStore::kSlotCount; ++slot) {
    const uint8_t column = slot % 2;
    const uint8_t row = slot / 2;
    presetSlotButtons_[slot] = lv_button_create(presetOverlay_);
    lv_obj_set_size(presetSlotButtons_[slot], slotWidth, slotHeight);
    lv_obj_set_pos(presetSlotButtons_[slot],
                   12 + column * (slotWidth + slotGapX),
                   78 + row * (slotHeight + slotGapY));
    lv_obj_set_style_shadow_width(presetSlotButtons_[slot], 0, 0);
    lv_obj_set_style_radius(presetSlotButtons_[slot], 9, 0);
    lv_obj_set_style_border_width(presetSlotButtons_[slot], 1, 0);
    lv_obj_add_event_cb(presetSlotButtons_[slot], presetSlotEvent,
                        LV_EVENT_CLICKED, this);

    presetSlotLabels_[slot] = lv_label_create(presetSlotButtons_[slot]);
    lv_obj_set_width(presetSlotLabels_[slot], slotWidth - 20);
    lv_obj_set_style_text_font(presetSlotLabels_[slot], fonts_.regular(), 0);
    lv_obj_set_style_text_align(presetSlotLabels_[slot], LV_TEXT_ALIGN_CENTER,
                                0);
    lv_label_set_long_mode(presetSlotLabels_[slot], LV_LABEL_LONG_DOT);
    lv_obj_center(presetSlotLabels_[slot]);
  }

  constexpr int16_t bankWidth = 89;
  constexpr int16_t bankGap = 6;
  for (uint8_t bank = 0; bank < PresetStore::kBankCount; ++bank) {
    presetBankButtons_[bank] = lv_button_create(presetOverlay_);
    lv_obj_set_size(presetBankButtons_[bank], bankWidth, 40);
    lv_obj_set_pos(presetBankButtons_[bank],
                   5 + bank * (bankWidth + bankGap), 280);
    lv_obj_set_style_shadow_width(presetBankButtons_[bank], 0, 0);
    lv_obj_set_style_radius(presetBankButtons_[bank], 0, 0);
    lv_obj_set_style_border_width(presetBankButtons_[bank], 0, 0);
    lv_obj_add_event_cb(presetBankButtons_[bank], presetBankEvent,
                        LV_EVENT_CLICKED, this);

    presetBankLabels_[bank] = lv_label_create(presetBankButtons_[bank]);
    lv_obj_set_width(presetBankLabels_[bank], bankWidth - 8);
    lv_obj_set_style_text_font(presetBankLabels_[bank], fonts_.regular(), 0);
    lv_obj_set_style_text_align(presetBankLabels_[bank], LV_TEXT_ALIGN_CENTER,
                                0);
    lv_label_set_long_mode(presetBankLabels_[bank], LV_LABEL_LONG_DOT);
    lv_obj_center(presetBankLabels_[bank]);
  }

  presetActive_ = true;
  presetLastActivity_ = millis();
  presetDeleteArmed_ = -1;
  setPresetMode(PresetMode::Play);
  refreshPresetBanks();
  refreshPresetSlots();
  setPresetStatus("Válassz egy kedvenc állomást");
  lv_obj_move_foreground(presetOverlay_);
}

void DisplayManager::hidePresets() {
  if (presetOverlay_) lv_obj_delete(presetOverlay_);
  resetPresets();
}

void DisplayManager::resetPresets() {
  presetOverlay_ = nullptr;
  presetStatusLabel_ = nullptr;
  for (auto& button : presetModeButtons_) button = nullptr;
  for (auto& button : presetSlotButtons_) button = nullptr;
  for (auto& label : presetSlotLabels_) label = nullptr;
  for (auto& button : presetBankButtons_) button = nullptr;
  for (auto& label : presetBankLabels_) label = nullptr;
  presetMode_ = PresetMode::Play;
  presetDeleteArmed_ = -1;
  presetDeleteArmedAt_ = 0;
  presetLastActivity_ = 0;
  presetActive_ = false;
}

void DisplayManager::updatePresets() {
  if (!presetActive_) return;
  const uint32_t now = millis();
  if (presetDeleteArmed_ >= 0 &&
      now - presetDeleteArmedAt_ >= 3000) {
    presetDeleteArmed_ = -1;
    refreshPresetSlots();
    setPresetStatus("A törlés megszakítva");
  }
  if (now - presetLastActivity_ >= 20000) hidePresets();
}

void DisplayManager::setPresetMode(PresetMode mode) {
  presetMode_ = mode;
  presetDeleteArmed_ = -1;
  presetDeleteArmedAt_ = 0;
  presetLastActivity_ = millis();

  for (uint8_t index = 0; index < 3; ++index) {
    if (!presetModeButtons_[index]) continue;
    const bool active = index == static_cast<uint8_t>(mode);
    uint32_t background = active ? 0x22D3EE : 0x111827;
    uint32_t text = active ? 0x071018 : 0x94A3B8;
    if (active && mode == PresetMode::Delete) {
      background = 0xEF4444;
      text = 0xFFFFFF;
    }
    lv_obj_set_style_bg_color(presetModeButtons_[index],
                              lv_color_hex(background), 0);
    lv_obj_set_style_text_color(presetModeButtons_[index],
                                lv_color_hex(text), 0);
  }
  refreshPresetSlots();

  if (mode == PresetMode::Play)
    setPresetStatus("Érints meg egy állomást a lejátszáshoz");
  else if (mode == PresetMode::Save)
    setPresetStatus("Válassz helyet az aktuális állomásnak");
  else
    setPresetStatus("A törléshez érintsd meg kétszer a helyet", 0xFCA5A5);
}

void DisplayManager::refreshPresetSlots() {
  if (!presetActive_) return;
  String currentUrl;
  const size_t current = actions_.currentStation(actions_.context);
  if (current < actions_.stationCount(actions_.context))
    currentUrl = actions_.stationUrl(actions_.context, current);

  for (uint8_t slot = 0; slot < PresetStore::kSlotCount; ++slot) {
    const RadioPreset* preset = presets_.slot(slot);
    if (!presetSlotButtons_[slot] || !presetSlotLabels_[slot] || !preset)
      continue;

    const bool empty = preset->empty();
    const bool currentStation = !empty && preset->url == currentUrl;
    const bool deleteArmed = presetDeleteArmed_ == static_cast<int8_t>(slot);
    String label = String(slot + 1) + ". ";
    label += empty ? "Üres hely" : preset->name;
    lv_label_set_text(presetSlotLabels_[slot], label.c_str());

    uint32_t background = empty ? 0x111827 : 0x10243A;
    uint32_t border = currentStation ? 0x22D3EE : 0x263246;
    uint32_t text = empty ? 0x64748B : 0xF1F5F9;
    uint8_t borderWidth = currentStation ? 2 : 1;
    if (deleteArmed) {
      background = 0x7F1D1D;
      border = 0xEF4444;
      text = 0xFFFFFF;
      borderWidth = 2;
    }
    lv_obj_set_style_bg_color(presetSlotButtons_[slot],
                              lv_color_hex(background), 0);
    lv_obj_set_style_border_color(presetSlotButtons_[slot],
                                  lv_color_hex(border), 0);
    lv_obj_set_style_border_width(presetSlotButtons_[slot], borderWidth, 0);
    lv_obj_set_style_text_color(presetSlotButtons_[slot], lv_color_hex(text),
                                0);
  }
}

void DisplayManager::refreshPresetBanks() {
  if (!presetActive_) return;
  for (uint8_t bank = 0; bank < PresetStore::kBankCount; ++bank) {
    if (!presetBankButtons_[bank] || !presetBankLabels_[bank]) continue;
    lv_label_set_text(presetBankLabels_[bank],
                      presets_.bankLabel(bank).c_str());
    const bool active = bank == presets_.bank();
    lv_obj_set_style_bg_color(
        presetBankButtons_[bank],
        lv_color_hex(active ? 0x164E63 : 0x0F172A), 0);
    lv_obj_set_style_text_color(
        presetBankButtons_[bank],
        lv_color_hex(active ? 0x67E8F9 : 0x94A3B8), 0);
    lv_obj_set_style_border_width(presetBankButtons_[bank],
                                  active ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(presetBankButtons_[bank],
                                 LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(presetBankButtons_[bank],
                                  lv_color_hex(0x22D3EE), LV_PART_MAIN);
  }
}

void DisplayManager::handlePresetSlot(uint8_t slot) {
  if (slot >= PresetStore::kSlotCount) return;
  presetLastActivity_ = millis();
  const RadioPreset* preset = presets_.slot(slot);
  if (!preset) return;

  if (presetMode_ == PresetMode::Save) {
    const size_t current = actions_.currentStation(actions_.context);
    if (current >= actions_.stationCount(actions_.context)) {
      setPresetStatus("Nincs menthető állomás", 0xFCA5A5);
      return;
    }
    const String name = actions_.stationName(actions_.context, current);
    const String url = actions_.stationUrl(actions_.context, current);
    if (presets_.saveSlot(slot, name, url)) {
      refreshPresetSlots();
      setPresetStatus("Elmentve: " + name);
    } else {
      setPresetStatus("A preset nem menthető", 0xFCA5A5);
    }
    return;
  }

  if (preset->empty()) {
    setPresetStatus("Ez a presethely üres", 0xFCA5A5);
    return;
  }

  if (presetMode_ == PresetMode::Delete) {
    if (presetDeleteArmed_ != static_cast<int8_t>(slot) ||
        millis() - presetDeleteArmedAt_ >= 3000) {
      presetDeleteArmed_ = static_cast<int8_t>(slot);
      presetDeleteArmedAt_ = millis();
      refreshPresetSlots();
      setPresetStatus("Érintsd meg újra a törléshez", 0xFCA5A5);
      return;
    }
    const String name = preset->name;
    if (presets_.clearSlot(slot)) {
      presetDeleteArmed_ = -1;
      refreshPresetSlots();
      setPresetStatus("Törölve: " + name);
    } else {
      setPresetStatus("A preset nem törölhető", 0xFCA5A5);
    }
    return;
  }

  const int32_t stationIndex = stationIndexForUrl(preset->url);
  if (stationIndex < 0) {
    setPresetStatus("Az állomás már nincs a listában", 0xFCA5A5);
    return;
  }
  hidePresets();
  actions_.selectStation(actions_.context,
                         static_cast<size_t>(stationIndex));
}

int32_t DisplayManager::stationIndexForUrl(const String& url) const {
  const size_t count = actions_.stationCount(actions_.context);
  for (size_t index = 0; index < count; ++index) {
    if (actions_.stationUrl(actions_.context, index) == url)
      return static_cast<int32_t>(index);
  }
  return -1;
}

void DisplayManager::setPresetStatus(const String& text, uint32_t color) {
  if (!presetStatusLabel_) return;
  lv_label_set_text(presetStatusLabel_, text.c_str());
  lv_obj_set_style_text_color(presetStatusLabel_, lv_color_hex(color), 0);
}

void DisplayManager::presetModeEvent(lv_event_t* event) {
  auto* manager =
      static_cast<DisplayManager*>(lv_event_get_user_data(event));
  if (!manager) return;
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  for (uint8_t index = 0; index < 3; ++index) {
    if (target == manager->presetModeButtons_[index]) {
      manager->setPresetMode(static_cast<PresetMode>(index));
      return;
    }
  }
}

void DisplayManager::presetSlotEvent(lv_event_t* event) {
  auto* manager =
      static_cast<DisplayManager*>(lv_event_get_user_data(event));
  if (!manager) return;
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  for (uint8_t slot = 0; slot < PresetStore::kSlotCount; ++slot) {
    if (target == manager->presetSlotButtons_[slot]) {
      manager->handlePresetSlot(slot);
      return;
    }
  }
}

void DisplayManager::presetBankEvent(lv_event_t* event) {
  auto* manager =
      static_cast<DisplayManager*>(lv_event_get_user_data(event));
  if (!manager) return;
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  for (uint8_t bank = 0; bank < PresetStore::kBankCount; ++bank) {
    if (target == manager->presetBankButtons_[bank]) {
      manager->presetLastActivity_ = millis();
      manager->presetDeleteArmed_ = -1;
      manager->presets_.selectBank(bank);
      manager->refreshPresetBanks();
      manager->refreshPresetSlots();
      manager->setPresetStatus(manager->presets_.bankLabel(bank));
      return;
    }
  }
}

void DisplayManager::closePresetsEvent(lv_event_t* event) {
  auto* manager =
      static_cast<DisplayManager*>(lv_event_get_user_data(event));
  if (manager) manager->hidePresets();
}

void DisplayManager::calibrateTouch() {
#if !TOUCH_ENABLED
  return;
#elif TS_MODEL == TS_MODEL_FT6X36 || TS_MODEL == TS_MODEL_AXS15231B
  // A kapacitiv touch driverek abszolut koordinatakat adnak. Az XPT2046-hoz
  // keszult negypontos kalibracio elrontana a koordinataikat.
  return;
#else
  uint16_t calibration[8]{};
  Preferences preferences;
  preferences.begin("lvgl-touch", false);

  bool force = false;
#if defined(ENC_BTNB)
  force = digitalRead(ENC_BTNB) == LOW;
#endif
  constexpr const char* kTouchCalKey = "cal_xpt2046_v2";
  const bool saved = preferences.isKey(kTouchCalKey) &&
                     preferences.getBytesLength(kTouchCalKey) ==
                         sizeof(calibration);

  if (saved && !force) {
    preferences.getBytes(kTouchCalKey, calibration, sizeof(calibration));
    device_.setTouchCalibrate(calibration);
  } else {
    lv_obj_t* active = lv_screen_active();
    lv_obj_set_style_bg_color(active, lv_color_hex(0x101827), 0);
    lv_obj_t* label = lv_label_create(active);
    lv_label_set_text(label, "Érintsd meg a nyilakat\na kalibráláshoz");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, fonts_.large(), 0);
    lv_obj_center(label);
    lv_refr_now(lvDisplay_);
    delay(100);

    device_.calibrateTouch(calibration, TFT_MAGENTA, TFT_BLACK, 24);
    preferences.putBytes(kTouchCalKey, calibration, sizeof(calibration));
  }
  preferences.end();
  lv_obj_clean(lv_screen_active());
  device_.fillScreen(TFT_BLACK);
#endif
}
