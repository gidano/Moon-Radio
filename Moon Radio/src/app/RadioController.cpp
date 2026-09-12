#include "RadioController.h"

#include <algorithm>

#include <Preferences.h>
#include <esp_system.h>
#include <time.h>

#include "../network/TlsMemory.h"
#include "../web/RadioWebServer.h"
#include "options.h"

namespace {

#if DISPLAY_PROFILE_AXS15231B
// Keep the normal dashboard behaviour intact.  The AXS VU has its own direct
// transport and must not require slowing the clock or other UI widgets.
constexpr uint32_t kUiUpdateMs = 1000;
// This display has a dedicated, partial-row VU transfer.  Match its 15 FPS
// physical cadence without changing the update rate of any other display.
constexpr uint32_t kVuUpdateMs = 67;
#else
constexpr uint32_t kUiUpdateMs = 500;
constexpr uint32_t kVuUpdateMs = 80;
#endif
constexpr uint32_t kConnectRetryMs = 10000;
constexpr uint32_t kArtworkServiceMs = 250;
constexpr uint32_t kMetadataServiceMs = 1000;
constexpr uint32_t kWifiServiceMs = 100;
constexpr uint32_t kWebServiceMs = 25;
constexpr uint32_t kStreamInitialGraceMs = 20000;
constexpr uint32_t kStreamStallRecoverMs = 12000;
constexpr uint32_t kStreamRecoveryCooldownMs = 30000;
constexpr uint32_t kClockTtsMaxActiveMs = 12000;
constexpr uint32_t kClockTtsNoProgressMs = 3500;
constexpr uint32_t kClockTtsFadeDownStepMs = 180;
constexpr uint32_t kClockTtsFadeUpStepMs = 180;



String preferPlainHttpAudioUrl(String url) {
  url.trim();
  String lower = url;
  lower.toLowerCase();
  if (lower.startsWith("https://icast.connectmedia.hu/") ||
      lower.startsWith("https://mr-stream.connectmedia.hu/")) {
    return "http://" + url.substring(8);
  }
  return url;
}

bool isClockTtsSupportedLanguage(const String& language) {
  return language == "EN" || language == "HU" || language == "PL" ||
         language == "NL" || language == "DE" || language == "RU" ||
         language == "RO" || language == "FR" || language == "GR";
}

String normalizeClockTtsLanguage(String language) {
  language.trim();
  language.toUpperCase();
  if (language.length() > 2) language = language.substring(0, 2);
  return isClockTtsSupportedLanguage(language) ? language : String("HU");
}

bool isClockTtsQuietTime(const RadioController::ClockTtsConfig& config,
                         const tm& localTime) {
  if (!config.quietHoursEnabled ||
      config.quietFromMinutes == config.quietToMinutes) {
    return false;
  }
  const uint16_t nowMinutes =
      static_cast<uint16_t>(localTime.tm_hour * 60 + localTime.tm_min);
  if (config.quietFromMinutes < config.quietToMinutes) {
    return nowMinutes >= config.quietFromMinutes &&
           nowMinutes < config.quietToMinutes;
  }
  return nowMinutes >= config.quietFromMinutes ||
         nowMinutes < config.quietToMinutes;
}

String clockTtsAnnouncement(int hour, int minute, const String& language) {
  char buffer[64];
  if (language == "PL") {
    snprintf(buffer, sizeof(buffer), "Jest godzina %d:%02d.", hour, minute);
  } else if (language == "HU") {
    snprintf(buffer, sizeof(buffer), "Az idő %d:%02d.", hour, minute);
  } else if (language == "RU") {
    snprintf(buffer, sizeof(buffer), "Сейчас %d:%02d.", hour, minute);
  } else if (language == "DE") {
    snprintf(buffer, sizeof(buffer), "Es ist %d Uhr %02d.", hour, minute);
  } else if (language == "FR") {
    snprintf(buffer, sizeof(buffer), "Il est %d:%02d.", hour, minute);
  } else if (language == "GR") {
    snprintf(buffer, sizeof(buffer), "I ora einai %d:%02d.", hour, minute);
  } else if (language == "RO") {
    snprintf(buffer, sizeof(buffer), "Este ora %d:%02d.", hour, minute);
  } else if (language == "NL") {
    snprintf(buffer, sizeof(buffer), "De tijd %d:%02d.", hour, minute);
  } else {
    snprintf(buffer, sizeof(buffer), "The time is %d:%02d.", hour, minute);
  }
  return String(buffer);
}

}  // namespace

RadioController::RadioController()
    : stationStore_(LittleFS), wifiManager_(LittleFS) {}

RadioController::~RadioController() { delete web_; }

bool RadioController::begin() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("[radio] Moon Radio indul");
  Serial.printf("[boot] reset reason=%d\n",
                static_cast<int>(esp_reset_reason()));
  performanceMonitor_.begin();

  // Még a Wi-Fi/TLS objektumok első valódi memóriafoglalása előtt állítjuk be.
  // Enélkül az Arduino-ESP32 3.3.7 mbedTLS csak a szűk belső heapet használja.
  enableTlsPsramAllocator();

  // Soha ne formázzunk automatikusan. Egy watchdog vagy pillanatnyi
  // csatolási hiba nem járhat a teljes adatpartíció törlésével.
  if (!LittleFS.begin(false)) {
    Serial.println("[fs] LittleFS csatolasi hiba - NINCS automatikus format");
    return false;
  }
  Serial.printf("[fs] LittleFS total=%u used=%u\n",
                static_cast<unsigned>(LittleFS.totalBytes()),
                static_cast<unsigned>(LittleFS.usedBytes()));
  logoManager_.begin();

  // The AXS panel keeps its own powered GRAM through an ESP reset.  Read the
  // two visual controller settings before DisplayManager::begin(), so its
  // first command already uses the saved orientation instead of showing the
  // previous frame in the transient default orientation.
  {
    Preferences startupPreferences;
    if (startupPreferences.begin("lvgl-radio", true)) {
      colorInverted_ = startupPreferences.getBool("color_inv", false);
      screenFlipped_ = startupPreferences.getBool("screen_flip", false);
      startupPreferences.end();
    }
  }
  display_.setStartupDisplayState(colorInverted_, screenFlipped_);

  const NowPlayingActions actions{
      .context = this,
      .previous = previousAction,
      .toggle = toggleAction,
      .next = nextAction,
      .adjustVolume = adjustVolumeAction,
      .setVolume = setVolumeAction,
      .stationCount = stationCountAction,
      .stationName = stationNameAction,
      .stationUrl = stationUrlAction,
      .currentStation = currentStationAction,
      .selectStation = selectStationAction,
      .openPresets = openPresetsAction,
      .ipAddress = nullptr,
      .cycleWeatherMode = cycleWeatherModeAction,
      .visualizerModeChanged = visualizerModeChangedAction,
      .headerIpVisibleChanged = headerIpVisibleChangedAction,
      .wifiDetailsVisibleChanged = wifiDetailsVisibleChangedAction,
      .volumeGraphicVisibleChanged = volumeGraphicVisibleChangedAction,
      .diagnosticsVisibleChanged = diagnosticsVisibleChangedAction,
  };
  display_.begin(actions);
  const InputActions inputActions{
      .context = this,
#if defined(ENC2_BTNL) && defined(ENC2_BTNR) && defined(ENC2_BTNB)
      .stationPrevious = previousAction,
      .stationNext = nextAction,
#else
      .stationPrevious = primaryEncoderLeftAction,
      .stationNext = primaryEncoderRightAction,
#endif
      .primaryEncoderLeft = primaryEncoderLeftAction,
      .primaryEncoderRight = primaryEncoderRightAction,
      .togglePlayback = toggleAction,
      .stationLongPress = stationLongPressAction,
      .volumeDown = volumeDownAction,
      .volumeUp = volumeUpAction,
  };
  input_.begin(inputActions);
  stationStore_.load();

  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  volume_ = preferences.getUChar("volume", 8);
  currentIndex_ = preferences.getUShort("station", 0);
  timezone_ = preferences.getString(
      "timezone", "CET-1CEST,M3.5.0,M10.5.0/3");
  backgroundEnabled_ = preferences.getBool("bg_on", false);
  backgroundPath_ =
      preferences.getString("bg_path", "/backgrounds/music_background.sr565");
  backgroundOpacity_ = preferences.getUChar("bg_opa", 128);
  colorInverted_ = preferences.getBool("color_inv", false);
  screenFlipped_ = preferences.getBool("screen_flip", false);
  visualizerMode_ = preferences.getUChar("vu_mode", 0);
  if (visualizerMode_ > 2) visualizerMode_ = 0;
  headerIpVisible_ = preferences.getBool("ui_ip", false);
  wifiDetailsVisible_ = preferences.getBool("ui_wifi", false);
  volumeGraphicVisible_ = preferences.getBool("ui_vol", true);
  bufferBarVisible_ = preferences.getBool("ui_buf", true);
  diagnosticsVisible_ = preferences.getBool("ui_diag", false);
  albumCoversEnabled_ = preferences.getBool("cover_on", true);
  logoManager_.setAlbumCoversEnabled(albumCoversEnabled_);
  if (backgroundPath_.isEmpty()) {
    backgroundPath_ = "/backgrounds/music_background.sr565";
  }
  backgroundOpacity_ = constrain(backgroundOpacity_, 32, 255);
  WeatherConfig weatherConfig;
  weatherConfig.enabled = preferences.getBool("weather_on", false);
  weatherConfig.latitude = preferences.getFloat("weather_lat", 47.4979f);
  weatherConfig.longitude = preferences.getFloat("weather_lon", 19.0402f);
  weatherConfig.intervalMinutes =
      preferences.getUShort("weather_int", 60);
  weatherConfig.mode = static_cast<WeatherDisplayMode>(
      preferences.getUChar("weather_mode",
                           static_cast<uint8_t>(WeatherDisplayMode::Current)));
  clockTts_.enabled = preferences.getBool("ctts_on", false);
  clockTts_.language =
      normalizeClockTtsLanguage(preferences.getString("ctts_lang", "HU"));
  clockTts_.intervalMinutes = preferences.getUShort("ctts_int", 60);
  if (clockTts_.intervalMinutes < 1) clockTts_.intervalMinutes = 1;
  clockTts_.onlyWhenNoStream = preferences.getBool("ctts_no", false);
  clockTts_.quietHoursEnabled = preferences.getBool("ctts_qon", false);
  clockTts_.quietFromMinutes =
      preferences.getUShort("ctts_qfrom", 23 * 60) % (24 * 60);
  clockTts_.quietToMinutes =
      preferences.getUShort("ctts_qto", 7 * 60) % (24 * 60);
  display_.setBrightness(preferences.getUChar("brightness", 204));
  display_.setColorInverted(colorInverted_);
  display_.setScreenFlipped(screenFlipped_);
  display_.setVisualizerMode(static_cast<WaveVu::Mode>(visualizerMode_));
  display_.setHeaderIpVisible(headerIpVisible_);
  if (!headerIpVisible_) display_.showStartupIp(10000);
  display_.setWifiDetailsVisible(wifiDetailsVisible_);
  display_.setVolumeGraphicVisible(volumeGraphicVisible_);
  display_.setBufferBarVisible(bufferBarVisible_);
  display_.setDiagnosticsVisible(diagnosticsVisible_);
  preferences.end();

  audio_.begin(volume_);
  weather_.begin(weatherConfig);
  wifiManager_.begin();
  wifiWasConnected_ = wifiManager_.connected();
  applyTimezone();

  web_ = new RadioWebServer(*this);
  web_->begin();

  if (currentIndex_ >= stationStore_.count()) currentIndex_ = 0;
  refreshDisplay(true);
  display_.loop();
  if (wifiManager_.connected() && stationStore_.count() > 0) {
    connectCurrentStation();
  }
  refreshDisplay(true);
  initialized_ = true;
  return true;
}

void RadioController::loop() {
  if (!initialized_) {
    delay(100);
    return;
  }
  const uint32_t loopStartedAt = millis();
  if (previousLoopAt_ != 0) {
    maximumLoopGap_ =
        max(maximumLoopGap_, loopStartedAt - previousLoopAt_);
  }
  previousLoopAt_ = loopStartedAt;

  // Do not let a slow audio service pass postpone the visual clock.  levels()
  // returns AudioSpectrum's already-published, lock-protected snapshot, so
  // drawing it here changes neither audio processing nor its data path.
  // It merely lets the AXS VU use the newest completed sample at the start of
  // every main-loop pass.
  if (!backgroundUploadMode_ && !fsMaintenance_) {
    const bool visualizerVisible = display_.visualizerActive();
    const bool playbackActive = audio_.running() && !audio_.paused();
    audio_.setVisualizationEnabled(visualizerVisible && playbackActive);
    if (visualizerVisible) {
      const uint32_t vuNow = millis();
      if (lastVuUpdate_ == 0) lastVuUpdate_ = vuNow;
      const uint32_t elapsed = vuNow - lastVuUpdate_;
      if (elapsed >= kVuUpdateMs) {
        const uint32_t slots = elapsed / kVuUpdateMs;
        lastVuUpdate_ += slots * kVuUpdateMs;
        display_.updateVu(playbackActive ? audio_.levels() : AudioLevels{});
      }
    }
  }

  audio_.loop();
  input_.loop();
  if (backgroundUploadMode_) {
    audio_.setVisualizationEnabled(false);
    performanceMonitor_.update();
    const uint32_t now = millis();
    if (now - lastWifiLoopAt_ >= kWifiServiceMs) {
      lastWifiLoopAt_ = now;
      wifiManager_.loop();
    }
    if (web_ && now - lastWebLoopAt_ >= kWebServiceMs) {
      lastWebLoopAt_ = now;
      web_->loop();
    }
    display_.loop();
    return;
  }
  if (fsMaintenance_) {
    audio_.setVisualizationEnabled(false);
    performanceMonitor_.update();
    const uint32_t now = millis();
    if (now - lastWifiLoopAt_ >= kWifiServiceMs) {
      lastWifiLoopAt_ = now;
      wifiManager_.loop();
    }
    if (web_ && now - lastWebLoopAt_ >= kWebServiceMs) {
      lastWebLoopAt_ = now;
      web_->loop();
    }
    return;
  }
  const uint32_t now = millis();
  const size_t bufferFilled = audio_.bufferFilled();
  const bool running = audio_.running();
  const bool wifiConnected = wifiManager_.connected();

  // A MyOnlineRadio HTTPS-címlekérés közben ne induljon egy második, szintén
  // hálózatot és belső RAM-ot használó borítófeladat.
  if (!metadata_.busy() && now - lastArtworkAt_ >= kArtworkServiceMs) {
    lastArtworkAt_ = now;
    AudioEngine::ArtworkEvent artworkEvent;
    while (audio_.takeArtworkEvent(artworkEvent)) {
      if (artworkEvent.kind == AudioEngine::ArtworkEventKind::IcyLogo) {
        logoManager_.setIcyLogo(artworkEvent.text, currentPlayUrl_);
      } else if (!playlist_.active()) {
        std::vector<uint32_t> segments;
        segments.reserve(artworkEvent.segmentValues);
        for (uint8_t index = 0; index < artworkEvent.segmentValues; ++index)
          segments.push_back(artworkEvent.segments[index]);
        logoManager_.setEmbeddedImage(currentPlayUrl_, segments);
      }
    }
    const AudioSnapshot snapshot = audioSnapshot();
    logoManager_.setAlbumTitle(snapshot.streamTitle);
    logoManager_.loop(running, bufferFilled, snapshot.codec,
                      snapshot.bitrateKbps, snapshot.bufferPercent);
  }

  if (!logoManager_.busy() && now - lastMetadataAt_ >= kMetadataServiceMs) {
    lastMetadataAt_ = now;
    metadata_.loop(wifiConnected, running, bufferFilled);
  }
  weather_.loop(wifiConnected);
  performanceMonitor_.update();
  if (now - lastWifiLoopAt_ >= kWifiServiceMs) {
    lastWifiLoopAt_ = now;
    wifiManager_.loop();
  }
  if (wifiConnected && !wifiWasConnected_ && !fsMaintenance_) {
    applyTimezone();
    if (stationStore_.count() > 0) connectCurrentStation();
  }
  wifiWasConnected_ = wifiConnected;
  if (web_ && now - lastWebLoopAt_ >= kWebServiceMs) {
    lastWebLoopAt_ = now;
    web_->loop();
  }

  processClockTts(now);

  if (!clockTtsActive_ && audio_.consumeEndOfFile()) {
    if (playlist_.active()) {
      stepTrack(1);
    } else if (wifiConnected && stationStore_.count() > 0) {
      Serial.println("[radio] Elo stream vege, ujracsatlakozas");
      connectCurrentStation();
    }
  }

  if (connectRetryPending_ && wifiConnected &&
      static_cast<int32_t>(now - nextConnectRetryAt_) >= 0) {
    connectRetryPending_ = false;
    Serial.println("[radio] M3U kapcsolat ujraprobalasa");
    connectCurrentStation();
  }
  recoverStalledStream(now, wifiConnected, bufferFilled);
  if (volumeSavePending_ && now - volumeChangedAt_ >= 1000) {
    Preferences preferences;
    preferences.begin("lvgl-radio", false);
    preferences.putUChar("volume", volume_);
    preferences.end();
    volumeSavePending_ = false;
  }
  if (now - lastUiUpdate_ >= kUiUpdateMs) {
    refreshDisplay(true);
  }
  if (now - lastDiagnosticsAt_ >= 2000) {
    lastDiagnosticsAt_ = now;
    maximumLoopGap_ = 0;
  }
  display_.loop();
}

StationStore& RadioController::stations() { return stationStore_; }

const StationStore& RadioController::stations() const { return stationStore_; }

WifiManager& RadioController::wifi() { return wifiManager_; }

size_t RadioController::currentIndex() const { return currentIndex_; }

const Station* RadioController::currentStation() const {
  return stationStore_.get(currentIndex_);
}

AudioSnapshot RadioController::audioSnapshot() {
  const Station* station = currentStation();
  AudioSnapshot snapshot =
      audio_.snapshot(station ? station->name : String("Nincs állomás"));
  if (snapshot.streamTitle.isEmpty() && !playlistTitle_.isEmpty()) {
    snapshot.streamTitle = playlistTitle_;
  }
  if (snapshot.streamTitle.isEmpty()) {
    const String externalTitle = metadata_.title();
    if (!externalTitle.isEmpty()) snapshot.streamTitle = externalTitle;
  }
  return snapshot;
}

AudioLevels RadioController::audioLevels() { return audio_.levels(); }

DiagnosticsSnapshot RadioController::diagnostics() const {
  return performanceMonitor_.snapshot();
}

LogoManager& RadioController::logos() { return logoManager_; }

const LogoManager& RadioController::logos() const { return logoManager_; }

bool RadioController::selectStation(size_t index) {
  const Station* station = stationStore_.get(index);
  if (!station) return false;
  resetClockTtsPlayback(true);
  currentIndex_ = index;

  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putUShort("station", static_cast<uint16_t>(currentIndex_));
  preferences.end();

  // Set the display state before any playlist/stream network operation. This
  // makes a local logo or nologo visible immediately on station changes.
  logoManager_.selectStation(station->logoName, "", station->homepage,
                             station->name);
  refreshDisplay(true);
  // Execute one LVGL cycle before the blocking playlist/network work below so
  // the new local logo or nologo is physically flushed to the panel now.
  display_.loop();

  const bool ok = connectCurrentStation();
  refreshDisplay(true);
  return ok;
}

bool RadioController::previousStation() {
  const size_t count = stationStore_.count();
  return count > 0 && selectStation((currentIndex_ + count - 1) % count);
}

bool RadioController::nextStation() {
  const size_t count = stationStore_.count();
  return count > 0 && selectStation((currentIndex_ + 1) % count);
}

bool RadioController::previousTrack() { return stepTrack(-1); }

bool RadioController::nextTrack() { return stepTrack(1); }

bool RadioController::togglePause() {
  if (fsMaintenance_) return false;
  // pauseResume() cannot restart an already closed stream. In STOP/error
  // state, Play must establish the current station again.
  if (!audio_.running()) {
    if (!wifiManager_.connected()) return false;
    return connectCurrentStation();
  }
  return audio_.togglePause();
}

void RadioController::resetStreamProgressWatchdog(uint32_t now) {
  lastAudioDataAt_ = now;
  lastObservedAudioTime_ = 0;
  lastObservedBufferFilled_ = 0;
  decoderProgressObserved_ = false;
}

void RadioController::recoverStalledStream(uint32_t now, bool wifiConnected,
                                           size_t bufferFilled) {
  if (!wifiConnected || audio_.paused() || clockTtsActive_ ||
      clockTtsFadingDown_ || clockTtsFadingUp_ || connectRetryPending_ ||
      currentPlayUrl_.isEmpty() || stationStore_.count() == 0) {
    return;
  }

  // A non-empty buffer alone is not a proof of playback: a broken HTTP/TLS
  // connection can leave bytes behind while the decoder has stopped.  Once a
  // stream has reported decoded play time, use that as the liveness signal.
  // Some codecs do not publish a play clock immediately, so before then a
  // changing buffer remains a conservative fallback for slow stations.
  const uint32_t audioTime = audio_.audioCurrentTime();
  if (audioTime > 0 && audioTime != lastObservedAudioTime_) {
    decoderProgressObserved_ = true;
    lastAudioDataAt_ = now;
  } else if (!decoderProgressObserved_ &&
             bufferFilled != lastObservedBufferFilled_) {
    lastAudioDataAt_ = now;
  }
  lastObservedAudioTime_ = audioTime;
  lastObservedBufferFilled_ = bufferFilled;

  if (!lastAudioDataAt_) resetStreamProgressWatchdog(now);

  const uint32_t stallLimit = decoderProgressObserved_
                                  ? kStreamStallRecoverMs
                                  : kStreamInitialGraceMs;
  if (now - lastAudioDataAt_ < stallLimit) return;
  if (lastStreamRecoveryAt_ &&
      now - lastStreamRecoveryAt_ < kStreamRecoveryCooldownMs) {
    return;
  }

  lastStreamRecoveryAt_ = now;
  resetStreamProgressWatchdog(now);
  Serial.printf("[radio] Stream lejatszasa nem halad %lu ms ota, ujrainditas: %s\n",
                static_cast<unsigned long>(stallLimit),
                currentPlayUrl_.c_str());
  audio_.stop();
  delay(150);
  connectCurrentStation();
}

void RadioController::setVolume(uint8_t value) {
  volume_ = constrain(value, 0, 21);
  audio_.setVolume(volume_);
  display_.setVolumeIndicator(volume_);
  volumeChangedAt_ = millis();
  volumeSavePending_ = true;
}

uint8_t RadioController::volume() const { return volume_; }

void RadioController::setBrightness(uint8_t value) {
  display_.setBrightness(value);
  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putUChar("brightness", value);
  preferences.end();
}

uint8_t RadioController::brightness() const {
  return display_.brightness();
}

const String& RadioController::timezone() const { return timezone_; }

bool RadioController::setTimezone(String value) {
  value.trim();
  if (value.isEmpty() || value.length() > 96) return false;
  for (const char character : value) {
    const bool valid =
        isalnum(static_cast<unsigned char>(character)) ||
        strchr("<>,.+-/:_", character);
    if (!valid) return false;
  }

  // The settings page posts every field together. Re-running configTzTime()
  // for an unchanged value repeatedly recreates the SNTP/timer state and can
  // destabilize the FreeRTOS timer task under Wi-Fi load.
  if (timezone_ == value) return true;

  timezone_ = value;
  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  const bool saved = preferences.putString("timezone", timezone_) > 0;
  preferences.end();
  applyTimezone();
  refreshDisplay(true);
  return saved;
}

bool RadioController::backgroundEnabled() const { return backgroundEnabled_; }

const String& RadioController::backgroundPath() const { return backgroundPath_; }

uint8_t RadioController::backgroundOpacity() const { return backgroundOpacity_; }

bool RadioController::setBackgroundConfig(bool enabled, String path,
                                          uint8_t opacity) {
  path.trim();
  if (path.isEmpty()) path = "/backgrounds/music_background.sr565";
  if (!path.startsWith("/")) path = "/" + path;
  if (path.length() > 96) return false;
  if (!path.endsWith(".sr565")) return false;
  opacity = constrain(opacity, 32, 255);

  backgroundEnabled_ = enabled;
  backgroundPath_ = path;
  backgroundOpacity_ = opacity;

  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("bg_on", backgroundEnabled_);
  preferences.putUChar("bg_opa", backgroundOpacity_);
  const bool saved = preferences.putString("bg_path", backgroundPath_) > 0;
  preferences.end();

  refreshDisplay(true);
  return saved;
}

bool RadioController::colorInverted() const { return colorInverted_; }

void RadioController::setColorInverted(bool inverted) {
  colorInverted_ = inverted;
  display_.setColorInverted(colorInverted_);

  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("color_inv", colorInverted_);
  preferences.end();
}

bool RadioController::screenFlipped() const { return screenFlipped_; }

void RadioController::setScreenFlipped(bool flipped) {
  screenFlipped_ = flipped;
  display_.setScreenFlipped(screenFlipped_);

  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("screen_flip", screenFlipped_);
  preferences.end();
}

uint8_t RadioController::visualizerMode() const { return visualizerMode_; }

void RadioController::setVisualizerMode(uint8_t mode) {
  if (mode > 2) mode = 0;
  visualizerMode_ = mode;
  display_.setVisualizerMode(static_cast<WaveVu::Mode>(visualizerMode_));

  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putUChar("vu_mode", visualizerMode_);
  preferences.end();
}

bool RadioController::headerIpVisible() const { return headerIpVisible_; }

void RadioController::setHeaderIpVisible(bool visible) {
  headerIpVisible_ = visible;
  display_.setHeaderIpVisible(headerIpVisible_);
  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("ui_ip", headerIpVisible_);
  preferences.end();
}

bool RadioController::wifiDetailsVisible() const {
  return wifiDetailsVisible_;
}

void RadioController::setWifiDetailsVisible(bool visible) {
  wifiDetailsVisible_ = visible;
  display_.setWifiDetailsVisible(wifiDetailsVisible_);
  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("ui_wifi", wifiDetailsVisible_);
  preferences.end();
}

bool RadioController::volumeGraphicVisible() const {
  return volumeGraphicVisible_;
}

void RadioController::setVolumeGraphicVisible(bool visible) {
  volumeGraphicVisible_ = visible;
  display_.setVolumeGraphicVisible(volumeGraphicVisible_);
  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("ui_vol", volumeGraphicVisible_);
  preferences.end();
}

bool RadioController::bufferBarVisible() const { return bufferBarVisible_; }

void RadioController::setBufferBarVisible(bool visible) {
  bufferBarVisible_ = visible;
  display_.setBufferBarVisible(bufferBarVisible_);
  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("ui_buf", bufferBarVisible_);
  preferences.end();
}

bool RadioController::diagnosticsVisible() const { return diagnosticsVisible_; }

void RadioController::setDiagnosticsVisible(bool visible) {
  diagnosticsVisible_ = visible;
  display_.setDiagnosticsVisible(diagnosticsVisible_);
  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("ui_diag", diagnosticsVisible_);
  preferences.end();
}

bool RadioController::albumCoversEnabled() const {
  return albumCoversEnabled_;
}

void RadioController::setAlbumCoversEnabled(bool enabled) {
  albumCoversEnabled_ = enabled;
  logoManager_.setAlbumCoversEnabled(albumCoversEnabled_);
  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("cover_on", albumCoversEnabled_);
  preferences.end();
  refreshDisplay(true);
}

WeatherConfig RadioController::weatherConfig() const {
  return weather_.config();
}

WeatherSnapshot RadioController::weatherSnapshot() const {
  return weather_.snapshot();
}

bool RadioController::setWeatherConfig(const WeatherConfig& config) {
  if (config.intervalMinutes != 30 && config.intervalMinutes != 60) return false;
  if (config.latitude < -90.0f || config.latitude > 90.0f) return false;
  if (config.longitude < -180.0f || config.longitude > 180.0f) return false;
  if (config.mode != WeatherDisplayMode::Current &&
      config.mode != WeatherDisplayMode::Today &&
      config.mode != WeatherDisplayMode::Tomorrow) {
    return false;
  }

  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("weather_on", config.enabled);
  preferences.putFloat("weather_lat", config.latitude);
  preferences.putFloat("weather_lon", config.longitude);
  preferences.putUShort("weather_int", config.intervalMinutes);
  preferences.putUChar("weather_mode", static_cast<uint8_t>(config.mode));
  preferences.end();
  weather_.setConfig(config);
  refreshDisplay(true);
  return true;
}

RadioController::ClockTtsConfig RadioController::clockTtsConfig() const {
  return clockTts_;
}

bool RadioController::setClockTtsConfig(ClockTtsConfig config) {
  config.language = normalizeClockTtsLanguage(config.language);
  if (config.intervalMinutes < 1) config.intervalMinutes = 1;
  config.quietFromMinutes %= 24 * 60;
  config.quietToMinutes %= 24 * 60;

  clockTts_ = config;
  if (!clockTts_.enabled) resetClockTtsPlayback(true);

  Preferences preferences;
  preferences.begin("lvgl-radio", false);
  preferences.putBool("ctts_on", clockTts_.enabled);
  preferences.putString("ctts_lang", clockTts_.language);
  preferences.putUShort("ctts_int", clockTts_.intervalMinutes);
  preferences.putBool("ctts_no", clockTts_.onlyWhenNoStream);
  preferences.putBool("ctts_qon", clockTts_.quietHoursEnabled);
  preferences.putUShort("ctts_qfrom", clockTts_.quietFromMinutes);
  preferences.putUShort("ctts_qto", clockTts_.quietToMinutes);
  preferences.end();
  return true;
}

bool RadioController::forceClockTts(const String& text,
                                    const String& language) {
  if (!wifiManager_.connected()) return false;
  String speech = text;
  speech.trim();
  if (speech.isEmpty()) return false;
  const String lang =
      language.isEmpty() ? clockTts_.language : normalizeClockTtsLanguage(language);
  const bool resumeAfter = audio_.running();
  if (resumeAfter) {
    clockTtsPendingText_ = speech;
    clockTtsPendingLanguage_ = lang;
    clockTtsPendingResumeAfter_ = true;
    clockTtsSavedStation_ = currentIndex_;
    clockTtsFadingDown_ = true;
    clockTtsFadingUp_ = false;
    clockTtsFadeVolume_ = -1;
    clockTtsFadeAt_ = millis();
    return true;
  }
  return beginClockTtsAnnouncement(speech, lang, false);
}

void RadioController::beginBackgroundUploadMode() {
  if (backgroundUploadMode_) return;
  const AudioSnapshot snapshot = audio_.snapshot(
      currentStation() ? currentStation()->name : String("Nincs állomás"));
  backgroundUploadResumePlayback_ =
      wifiManager_.connected() &&
      (snapshot.running || snapshot.paused || !currentPlayUrl_.isEmpty());
  backgroundUploadMode_ = true;
  connectRetryPending_ = false;
  audio_.setVisualizationEnabled(false);
  audio_.stop();
  refreshDisplay(true);
}

void RadioController::endBackgroundUploadMode() {
  if (!backgroundUploadMode_) return;
  backgroundUploadMode_ = false;
  const bool resumePlayback = backgroundUploadResumePlayback_;
  backgroundUploadResumePlayback_ = false;
  if (resumePlayback && wifiManager_.connected() && stationStore_.count() > 0) {
    connectCurrentStation();
  }
  refreshDisplay(true);
}

void RadioController::applyTimezone() {
  setenv("TZ", timezone_.c_str(), 1);
  tzset();
  if (wifiManager_.connected()) {
    // A configTime(0, 0, ...) UTC-re állíthatja vissza a TZ környezetet.
    // A configTzTime egyszerre indítja az NTP-t és tartja meg a mentett
    // POSIX időzóna-/nyáriidő-szabályt.
    configTzTime(timezone_.c_str(), "pool.ntp.org", "time.nist.gov");
  }
  Serial.printf("[time] TZ=%s\n", timezone_.c_str());
}

bool RadioController::reloadStations() {
  const bool ok = stationStore_.reload();
  if (currentIndex_ >= stationStore_.count()) currentIndex_ = 0;
  if (ok && wifiManager_.connected() && !fsMaintenance_)
    connectCurrentStation();
  refreshDisplay(true);
  return ok;
}

bool RadioController::enterFsMaintenance() {
  if (!fsMaintenance_) {
    fsMaintenance_ = true;
    audio_.setVisualizationEnabled(false);
    audio_.stop();
    display_.showMaintenance();
  }
  const bool artworkIdle = logoManager_.enterMaintenance();
  Serial.printf("[fs] Wi-Fi karbantartasi mod%s\n",
                artworkIdle ? "" : " (logo feladat meg fut)");
  return artworkIdle;
}

bool RadioController::fsMaintenance() const { return fsMaintenance_; }

size_t RadioController::bufferFilled() { return audio_.bufferFilled(); }

size_t RadioController::bufferFree() { return audio_.bufferFree(); }

size_t RadioController::bufferSize() { return audio_.bufferSize(); }

bool RadioController::playlistActive() const { return playlist_.active(); }

int RadioController::playlistIndex() const { return playlist_.index(); }

size_t RadioController::playlistCount() const { return playlist_.count(); }

const String& RadioController::currentPlayUrl() const {
  return currentPlayUrl_;
}

void RadioController::previousAction(void* context) {
  RadioController* radio = static_cast<RadioController*>(context);
  if (radio->display_.stationSelectorActive()) {
    radio->display_.moveStationSelector(-1);
    return;
  }
  radio->previousStation();
}

void RadioController::toggleAction(void* context) {
  static_cast<RadioController*>(context)->togglePause();
}

void RadioController::nextAction(void* context) {
  RadioController* radio = static_cast<RadioController*>(context);
  if (radio->display_.stationSelectorActive()) {
    radio->display_.moveStationSelector(1);
    return;
  }
  radio->nextStation();
}

void RadioController::primaryEncoderLeftAction(void* context) {
  RadioController* radio = static_cast<RadioController*>(context);
  if (radio->display_.stationSelectorActive()) {
    radio->display_.moveStationSelector(-1);
    return;
  }
  volumeDownAction(context);
}

void RadioController::primaryEncoderRightAction(void* context) {
  RadioController* radio = static_cast<RadioController*>(context);
  if (radio->display_.stationSelectorActive()) {
    radio->display_.moveStationSelector(1);
    return;
  }
  volumeUpAction(context);
}

void RadioController::stationLongPressAction(void* context) {
  static_cast<RadioController*>(context)->display_.openStationSelector();
}

void RadioController::volumeDownAction(void* context) {
  RadioController* radio = static_cast<RadioController*>(context);
  if (radio->volume() > 0) radio->setVolume(radio->volume() - 1);
}

void RadioController::volumeUpAction(void* context) {
  RadioController* radio = static_cast<RadioController*>(context);
  if (radio->volume() < 21) radio->setVolume(radio->volume() + 1);
}

void RadioController::adjustVolumeAction(void* context, int8_t delta) {
  RadioController* radio = static_cast<RadioController*>(context);
  const int value = constrain(static_cast<int>(radio->volume()) + delta, 0, 21);
  radio->setVolume(static_cast<uint8_t>(value));
}

void RadioController::setVolumeAction(void* context, uint8_t value) {
  static_cast<RadioController*>(context)->setVolume(value);
}

size_t RadioController::stationCountAction(void* context) {
  return static_cast<RadioController*>(context)->stations().count();
}

String RadioController::stationNameAction(void* context, size_t index) {
  const Station* station =
      static_cast<RadioController*>(context)->stations().get(index);
  return station ? station->name : String();
}

String RadioController::stationUrlAction(void* context, size_t index) {
  const Station* station =
      static_cast<RadioController*>(context)->stations().get(index);
  return station ? station->url : String();
}

size_t RadioController::currentStationAction(void* context) {
  return static_cast<RadioController*>(context)->currentIndex();
}

bool RadioController::selectStationAction(void* context, size_t index) {
  return static_cast<RadioController*>(context)->selectStation(index);
}

void RadioController::openPresetsAction(void* context) {
  static_cast<RadioController*>(context)->display_.showPresets();
}

void RadioController::cycleWeatherModeAction(void* context) {
  static_cast<RadioController*>(context)->cycleWeatherMode();
}

void RadioController::visualizerModeChangedAction(void* context,
                                                  uint8_t mode) {
  static_cast<RadioController*>(context)->setVisualizerMode(mode);
}

void RadioController::headerIpVisibleChangedAction(void* context,
                                                   bool visible) {
  static_cast<RadioController*>(context)->setHeaderIpVisible(visible);
}

void RadioController::wifiDetailsVisibleChangedAction(void* context,
                                                      bool visible) {
  static_cast<RadioController*>(context)->setWifiDetailsVisible(visible);
}

void RadioController::volumeGraphicVisibleChangedAction(void* context,
                                                        bool visible) {
  static_cast<RadioController*>(context)->setVolumeGraphicVisible(visible);
}

void RadioController::diagnosticsVisibleChangedAction(void* context,
                                                      bool visible) {
  static_cast<RadioController*>(context)->setDiagnosticsVisible(visible);
}

void RadioController::cycleWeatherMode() {
  WeatherConfig config = weather_.config();
  switch (config.mode) {
    case WeatherDisplayMode::Current:
      config.mode = WeatherDisplayMode::Today;
      break;
    case WeatherDisplayMode::Today:
      config.mode = WeatherDisplayMode::Tomorrow;
      break;
    case WeatherDisplayMode::Tomorrow:
    default:
      config.mode = WeatherDisplayMode::Current;
      break;
  }
  setWeatherConfig(config);
}

void RadioController::processClockTts(uint32_t now) {
  now = millis();
  if ((!clockTts_.enabled && !clockTtsActive_ && !clockTtsFadingDown_ &&
       !clockTtsFadingUp_) ||
      fsMaintenance_ || backgroundUploadMode_ ||
      !wifiManager_.connected()) {
    return;
  }

  if (clockTtsFadingDown_) {
    if (clockTtsFadeVolume_ < 0) {
      clockTtsFadeVolume_ = volume_;
      clockTtsPreviousVolume_ = volume_;
    }
    if (now - clockTtsFadeAt_ > kClockTtsFadeDownStepMs &&
        clockTtsFadeVolume_ > 0) {
      --clockTtsFadeVolume_;
      audio_.setVolume(static_cast<uint8_t>(clockTtsFadeVolume_));
      clockTtsFadeAt_ = now;
    }
    if (clockTtsFadeVolume_ <= 0) {
      const bool hasPending = !clockTtsPendingText_.isEmpty();
      tm localTime{};
      if (!hasPending && !getLocalTime(&localTime, 5)) {
        resetClockTtsPlayback(true);
        return;
      }
      const String text = hasPending
                              ? clockTtsPendingText_
                              : clockTtsAnnouncement(localTime.tm_hour,
                                                     localTime.tm_min,
                                                     clockTts_.language);
      const String language =
          hasPending ? clockTtsPendingLanguage_ : clockTts_.language;
      const bool resumeAfter =
          hasPending ? clockTtsPendingResumeAfter_ : true;
      clockTtsPendingText_ = "";
      clockTtsPendingLanguage_ = "";
      clockTtsPendingResumeAfter_ = false;
      audio_.setVolume(volume_);
      if (beginClockTtsAnnouncement(text, language, resumeAfter)) {
        if (!hasPending) clockTtsLastMinute_ = localTime.tm_min;
      } else {
        audio_.setVolume(0);
        connectCurrentStation();
        clockTtsFadingUp_ = true;
        clockTtsFadeAt_ = now;
      }
      clockTtsFadingDown_ = false;
      clockTtsFadeVolume_ = -1;
    }
    return;
  }

  if (clockTtsFadingUp_) {
    if (clockTtsFadeVolume_ < 0) clockTtsFadeVolume_ = 0;
    if (now - clockTtsFadeAt_ > kClockTtsFadeUpStepMs &&
        clockTtsFadeVolume_ < clockTtsPreviousVolume_) {
      ++clockTtsFadeVolume_;
      audio_.setVolume(static_cast<uint8_t>(clockTtsFadeVolume_));
      clockTtsFadeAt_ = now;
    }
    if (clockTtsFadeVolume_ >= clockTtsPreviousVolume_) {
      audio_.setVolume(volume_);
      clockTtsFadingUp_ = false;
      clockTtsFadeVolume_ = -1;
    }
    return;
  }

  if (clockTtsActive_) {
    if (clockTtsResumeAfter_ && currentIndex_ != clockTtsSavedStation_) {
      clockTtsResumeAfter_ = false;
    }

    const uint32_t audioTime = audio_.audioCurrentTime();
    if (audioTime != clockTtsLastAudioTime_) {
      clockTtsLastAudioTime_ = audioTime;
      clockTtsLastProgressAt_ = now;
      clockTtsAudioProgressSeen_ = true;
    }

    bool shouldRecover = false;
    if (!audio_.running() || now - clockTtsStartedAt_ > kClockTtsMaxActiveMs) {
      shouldRecover = true;
    }
    if (!clockTtsAudioProgressSeen_ &&
        now - clockTtsStartedAt_ > kClockTtsNoProgressMs) {
      shouldRecover = true;
    }
    if (!shouldRecover) return;

    const bool resumeAfter = clockTtsResumeAfter_;
    Serial.printf("[tts] recover resume=%u running=%u elapsed=%lu progress=%u "
                  "audioTime=%lu buffer=%u\n",
                  resumeAfter ? 1U : 0U,
                  audio_.running() ? 1U : 0U,
                  static_cast<unsigned long>(now - clockTtsStartedAt_),
                  clockTtsAudioProgressSeen_ ? 1U : 0U,
                  static_cast<unsigned long>(clockTtsLastAudioTime_),
                  static_cast<unsigned>(audio_.bufferFilled()));
    resetClockTtsPlayback(false);
    if (resumeAfter && currentIndex_ == clockTtsSavedStation_) {
      audio_.setVolume(0);
      connectCurrentStation();
      clockTtsFadingUp_ = true;
      clockTtsFadeAt_ = now;
    }
    return;
  }

  tm localTime{};
  if (!clockTts_.enabled || !getLocalTime(&localTime, 5) ||
      localTime.tm_year + 1900 < 2020) {
    return;
  }

  if (clockTts_.intervalMinutes > 0 &&
      localTime.tm_min % clockTts_.intervalMinutes == 0 &&
      localTime.tm_min != clockTtsLastMinute_ && localTime.tm_sec < 2 &&
      !isClockTtsQuietTime(clockTts_, localTime)) {
    const String text = clockTtsAnnouncement(
        localTime.tm_hour, localTime.tm_min, clockTts_.language);
    if (clockTts_.onlyWhenNoStream) {
      if (!audio_.running() &&
          beginClockTtsAnnouncement(text, clockTts_.language, false)) {
        clockTtsLastMinute_ = localTime.tm_min;
      }
    } else if (audio_.running()) {
      clockTtsFadingDown_ = true;
      clockTtsFadeAt_ = now;
      return;
    }
  }
}

void RadioController::resetClockTtsPlayback(bool restoreVolume) {
  if (restoreVolume) audio_.setVolume(volume_);
  clockTtsFadingDown_ = false;
  clockTtsFadingUp_ = false;
  clockTtsFadeVolume_ = -1;
  clockTtsActive_ = false;
  clockTtsStartedAt_ = 0;
  clockTtsLastProgressAt_ = 0;
  clockTtsLastAudioTime_ = 0;
  clockTtsPendingText_ = "";
  clockTtsPendingLanguage_ = "";
  clockTtsAudioProgressSeen_ = false;
  clockTtsPendingResumeAfter_ = false;
  clockTtsResumeAfter_ = false;
}

bool RadioController::beginClockTtsAnnouncement(const String& text,
                                                const String& language,
                                                bool resumeAfter) {
  clockTtsSavedStation_ = currentIndex_;
  if (audio_.running()) {
    audio_.stop();
    delay(150);
  }
  const bool ok = audio_.speak(text, language);
  if (!ok) {
    resetClockTtsPlayback(true);
    if (resumeAfter && wifiManager_.connected() &&
        currentIndex_ == clockTtsSavedStation_) {
      connectCurrentStation();
    }
    return false;
  }
  const uint32_t now = millis();
  clockTtsStartedAt_ = now;
  clockTtsLastProgressAt_ = now;
  clockTtsLastAudioTime_ = audio_.audioCurrentTime();
  clockTtsAudioProgressSeen_ = false;
  clockTtsResumeAfter_ = resumeAfter;
  clockTtsActive_ = true;
  Serial.printf("[tts] started resume=%u station=%u lang=%s text='%s'\n",
                resumeAfter ? 1U : 0U,
                static_cast<unsigned>(clockTtsSavedStation_),
                language.c_str(), text.c_str());
  return true;
}

bool RadioController::connectCurrentStation() {
  const Station* station = currentStation();
  if (!station || !wifiManager_.connected()) return false;
  metadata_.selectStation(station);

  String resolvedUrl;
  String title;
  if (!playlist_.resolve(station->url, resolvedUrl, title)) {
    Serial.println("[radio] Az M3U lista nem toltheto be");
    audio_.setConnectionError("Az M3U lista nem érhető el");
    connectRetryPending_ = true;
    nextConnectRetryAt_ = millis() + kConnectRetryMs;
    return false;
  }
  currentPlayUrl_ = preferPlainHttpAudioUrl(resolvedUrl);
  if (currentPlayUrl_ != resolvedUrl) {
    Serial.printf("[radio] Audio TLS kihagyva ismert streamnel: %s\n",
                  currentPlayUrl_.c_str());
  }
  playlistTitle_ = title;
  resetStreamProgressWatchdog(millis());
  logoManager_.selectStation(station->logoName, currentPlayUrl_,
                             station->homepage, station->name);
  const bool queued = audio_.connect(currentPlayUrl_);
  connectRetryPending_ = !queued;
  if (!queued) nextConnectRetryAt_ = millis() + kConnectRetryMs;
  return queued;
}

bool RadioController::stepTrack(int delta) {
  String resolvedUrl;
  String title;
  if (!playlist_.step(delta, resolvedUrl, title)) return false;
  currentPlayUrl_ = preferPlainHttpAudioUrl(resolvedUrl);
  if (currentPlayUrl_ != resolvedUrl) {
    Serial.printf("[radio] Audio TLS kihagyva ismert streamnel: %s\n",
                  currentPlayUrl_.c_str());
  }
  playlistTitle_ = title;
  resetStreamProgressWatchdog(millis());
  const Station* station = currentStation();
  metadata_.selectStation(station);
  logoManager_.selectStation(station ? station->logoName : String("nologo"),
                             currentPlayUrl_,
                             station ? station->homepage : String(),
                             station ? station->name : String());
  const bool ok = audio_.connect(currentPlayUrl_);
  refreshDisplay(true);
  return ok;
}

String RadioController::wifiStatusText() const {
  if (wifiManager_.accessPointMode()) {
    return "AP " + wifiManager_.address();
  }
  if (!wifiManager_.connected()) return "Wi-Fi —";
  return "WiFi " + String(wifiManager_.rssi());
}

void RadioController::refreshDisplay(bool force) {
  // A karbantartási nézet létrehozásakor a normál rádióképernyő LVGL
  // objektumai megszűnnek. Fájlcsere (például stations.txt feltöltése)
  // ezért sem közvetlenül, sem közvetve nem frissítheti őket.
  if (fsMaintenance_) return;
  const uint32_t now = millis();
  if (!force && now - lastUiUpdate_ < kUiUpdateMs) return;
  if (force) lastUiUpdate_ = now;

  const String ipText =
      wifiManager_.connected() || wifiManager_.accessPointMode()
          ? wifiManager_.address()
          : String("--.--.--.--");
  display_.update(audioSnapshot(), logoManager_.currentPath(),
                  wifiStatusText(), ipText, weather_.snapshot(),
                  performanceMonitor_.snapshot(), backgroundEnabled_,
                  backgroundPath_, backgroundOpacity_);
}
