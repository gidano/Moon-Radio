#include "AudioEngine.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClient.h>

#include "options.h"

AudioEngine* AudioEngine::instance_ = nullptr;

namespace {

constexpr uint8_t kDisplayVolumeMax = 21;
constexpr uint8_t kMaleksmVolumeMax = 254;
constexpr const char* kGoogleTtsTempPath = "/clock_tts.mp3";

#ifndef MUTE_PIN
#define MUTE_PIN 255
#endif

#ifndef MUTE_VAL
#define MUTE_VAL HIGH
#endif

String normalizeDisplayText(String value) {
  value.replace("\xE2\x80\x98", "'");
  value.replace("\xE2\x80\x99", "'");
  value.replace("\xE2\x80\x9A", "'");
  value.replace("\xE2\x80\x9B", "'");
  value.replace("\xE2\x80\xB2", "'");
  value.replace("\xE2\x80\xB5", "'");
  value.replace("\xCA\xBB", "'");
  value.replace("\xCA\xBC", "'");
  value.replace("\xC2\xB4", "'");
  value.replace("\xE2\x80\x9C", "\"");
  value.replace("\xE2\x80\x9D", "\"");
  value.replace("\xE2\x80\x9E", "\"");
  value.replace("\xE2\x80\x9F", "\"");
  value.replace("\xE2\x80\x93", "-");
  value.replace("\xE2\x80\x94", "-");
  value.replace("\xE2\x80\xA6", "...");

  bool validUtf8 = true;
  for (size_t index = 0; index < value.length();) {
    const uint8_t ch = static_cast<uint8_t>(value[index]);
    if (ch <= 0x7F) {
      ++index;
      continue;
    }

    size_t sequenceLength = 0;
    if ((ch & 0xE0) == 0xC0) {
      sequenceLength = 2;
      if (ch < 0xC2) validUtf8 = false;
    } else if ((ch & 0xF0) == 0xE0) {
      sequenceLength = 3;
    } else if ((ch & 0xF8) == 0xF0) {
      sequenceLength = 4;
      if (ch > 0xF4) validUtf8 = false;
    } else {
      validUtf8 = false;
    }

    if (!validUtf8 || index + sequenceLength > value.length()) {
      validUtf8 = false;
      break;
    }

    for (size_t offset = 1; offset < sequenceLength; ++offset) {
      const uint8_t continuation =
          static_cast<uint8_t>(value[index + offset]);
      if ((continuation & 0xC0) != 0x80) {
        validUtf8 = false;
        break;
      }
    }

    if (!validUtf8) break;
    index += sequenceLength;
  }

  if (validUtf8) {
    return value;
  }

  String normalized;
  normalized.reserve(value.length() + 8);
  for (size_t index = 0; index < value.length(); ++index) {
    const uint8_t ch = static_cast<uint8_t>(value[index]);
      switch (ch) {
        case 0x91:
        case 0x92:
        case 0xB4:
          normalized += '\'';
          break;
      case 0x93:
      case 0x94:
        normalized += '"';
        break;
      case 0x96:
      case 0x97:
        normalized += '-';
        break;
      case 0x85:
        normalized += "...";
        break;
      default:
        normalized += static_cast<char>(ch);
        break;
    }
  }
  return normalized;
}

String urlEncodeUtf8(const String& value) {
  const char* hex = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(value.length() * 3);
  for (size_t index = 0; index < value.length(); ++index) {
    const uint8_t ch = static_cast<uint8_t>(value[index]);
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
        ch == '.' || ch == '~') {
      encoded += static_cast<char>(ch);
    } else if (ch == ' ') {
      encoded += "%20";
    } else {
      encoded += '%';
      encoded += hex[ch >> 4];
      encoded += hex[ch & 0x0F];
    }
  }
  return encoded;
}

bool downloadGoogleTtsMp3(const String& speech, const String& language) {
  File file = LittleFS.open(kGoogleTtsTempPath, FILE_WRITE);
  if (!file) {
    Serial.println("[tts] ideiglenes MP3 fajl nem nyithato");
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  String url = "http://translate.google.com.vn/translate_tts?ie=UTF-8&total=1&idx=0&textlen=";
  url += speech.length();
  url += "&tl=";
  url += language;
  url += "&client=tw-ob&q=";
  url += urlEncodeUtf8(speech);

  http.setReuse(false);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  const char* headerKeys[] = {"Content-Type"};
  http.collectHeaders(headerKeys, 1);
  if (!http.begin(client, url)) {
    file.close();
    LittleFS.remove(kGoogleTtsTempPath);
    Serial.println("[tts] HTTP inditas hiba");
    return false;
  }
  http.addHeader("User-Agent", "Mozilla/5.0");
  http.addHeader("Accept", "audio/mpeg,*/*;q=0.8");
  http.addHeader("Accept-Encoding", "identity");

  const int code = http.GET();
  const String contentType = http.header("Content-Type");
  const int written = code == HTTP_CODE_OK ? http.writeToStream(&file) : -1;
  file.close();
  http.end();

  File verify = LittleFS.open(kGoogleTtsTempPath, FILE_READ);
  const size_t size = verify ? verify.size() : 0;
  if (verify) verify.close();
  Serial.printf("[tts] download code=%d type=%s written=%d size=%u\n",
                code, contentType.c_str(), written, static_cast<unsigned>(size));

  if (code != HTTP_CODE_OK || size < 1024 ||
      (!contentType.isEmpty() && contentType.indexOf("audio") < 0)) {
    LittleFS.remove(kGoogleTtsTempPath);
    return false;
  }
  return true;
}

}  // namespace

AudioEngine::AudioEngine() {
  metadataMutex_ = xSemaphoreCreateMutex();
  instance_ = this;
}

AudioEngine::~AudioEngine() {
  if (instance_ == this) instance_ = nullptr;
  if (artworkQueue_) {
    vQueueDelete(artworkQueue_);
    artworkQueue_ = nullptr;
  }
  if (logQueue_) {
    vQueueDelete(logQueue_);
    logQueue_ = nullptr;
  }
  if (metadataMutex_) vSemaphoreDelete(metadataMutex_);
}

bool AudioEngine::begin(uint8_t volume) {
  Audio::audio_info_callback = audioInfoCallback;
  logQueue_ = xQueueCreate(8, sizeof(DeferredLog));
  if (!logQueue_) {
    Serial.println("[audio] Naplosor letrehozasi hiba");
  }
  stateCode_ = "I2S";
  if (MUTE_PIN != 255) {
    pinMode(MUTE_PIN, OUTPUT);
    setOutputEnabled(false);
  }
  // Stabil audio az elsődleges. A mintánkénti VU-számítás nagy
  // bitrátájú FLAC streameknél felesleges dekódolási többletterhelés.
  audio_.settings.VU_LEVEL = false;
  // Jelenleg nincs felhasználói hangszínszabályzás. A bekapcsolt IIR út
  // ennek ellenére minden egyes PCM mintán három lebegőpontos biquadot
  // számolna, ezért sík hangszínnél teljesen kihagyjuk.
  audio_.settings.IIR_FILTER = false;
#ifdef I2S_MCLK
  if (!audio_.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK)) {
#else
  if (!audio_.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT)) {
#endif
    Serial.println("[audio] I2S labkiosztasi hiba");
    statusText_ = "I2S indítási hiba";
    stateCode_ = "I2S!";
    return false;
  }
  // A jelen hardveren az eredeti, bevált felállás működik jobban: a külön
  // audio task maradjon a 0. magon. A kijelzőterhelést külön csökkentjük.
  audio_.setAudioTaskCore(0);
  // A yoRadio/Maleksm ugyanezt az 5700 ms-os értéket használja.
  // A könyvtár 250 ms-os HTTP alapértéke internetes rádiókhoz túl rövid.
  audio_.setConnectionTimeout(5700, 5700);
  // A Maleksm Player eredeti inicializálási sorrendje.
  audio_.setBalance(0.0f);
  audio_.setTone(0.0f, 0.0f, 0.0f);
  audio_.setVolume(0);
  volume_ = constrain(volume, 0, kDisplayVolumeMax);
  audio_.setVolume(toInternalVolume(volume_));
  if (!spectrum_.begin()) {
    Serial.println("[spectrum] analizator task hiba");
  }
  artworkQueue_ = xQueueCreate(8, sizeof(ArtworkEvent));
  if (!artworkQueue_) {
    Serial.println("[audio] Kep-esemenysor letrehozasi hiba");
    statusText_ = "Kép-eseménysor hiba";
    stateCode_ = "KQ!";
    return false;
  }
  initialized_ = true;
  stateCode_ = "KESZ";
  statusText_ = "Audio kesz";
  Serial.printf("[audio] ESP32-audioI2S %s\n", audio_.getVersion());
  return true;
}

void AudioEngine::loop() {
  audio_.loop();
  processDeferredLogs();

  if (audio_.inBufferFilled() > 0) {
    connecting_ = false;
    stateCode_ = "PLAY";
  } else if (connectAttempted_ && tcpConnected_ && !audio_.isRunning()) {
    stateCode_ = "STOP";
  }
  if (!audio_.isRunning() && outputEnabled_) setOutputEnabled(false);
}

bool AudioEngine::connect(const String& url) {
  if (url.isEmpty()) return false;
  endOfFile_ = false;
  paused_ = false;
  connecting_ = true;
  commandQueued_ = false;
  connectAttempted_ = true;
  tcpConnected_ = false;
  stateCode_ = "SOR";
  if (metadataMutex_ && xSemaphoreTake(metadataMutex_, pdMS_TO_TICKS(30))) {
    streamTitle_ = "";
    artist_ = "";
    title_ = "";
    codec_ = "";
    bitrateKbps_ = 0;
    xSemaphoreGive(metadataMutex_);
  }
  Serial.printf("[audio] Kapcsolodas: %s\n", url.c_str());
  currentUrl_ = url;
  suppressEndOfFile_ = true;
  connectStartedAt_ = millis();
  statusText_ = "Kapcsolódás...";
  stateCode_ = "TCP";
  const bool connected = audio_.connecttohost(currentUrl_.c_str());
  tcpConnected_ = connected;
  connecting_ = connected;
  statusText_ = connected ? "HTTP kapcsolat létrejött"
                          : "Kapcsolódási hiba";
  stateCode_ = connected ? "FEJL" : "TCP!";
  setOutputEnabled(connected);
  suppressEndOfFile_ = false;
  Serial.printf("[audio] connecttohost=%s running=%u\n",
                connected ? "OK" : "HIBA",
                static_cast<unsigned>(audio_.isRunning()));
  return connected;
}

bool AudioEngine::speak(const String& text, const String& lang) {
  String speech = text;
  speech.trim();
  if (speech.isEmpty()) return false;

  String language = lang;
  language.trim();
  if (language.isEmpty()) language = "HU";
  language.toLowerCase();

  endOfFile_ = false;
  paused_ = false;
  connecting_ = true;
  commandQueued_ = false;
  connectAttempted_ = true;
  tcpConnected_ = false;
  stateCode_ = "TTS";
  statusText_ = "Google TTS...";
  suppressEndOfFile_ = true;
  const bool downloaded = downloadGoogleTtsMp3(speech, language);
  const bool connected =
      downloaded && audio_.connecttoFS(LittleFS, kGoogleTtsTempPath);
  tcpConnected_ = connected;
  connecting_ = connected;
  statusText_ = connected ? "TTS lejátszás" : "TTS hiba";
  stateCode_ = connected ? "TTS" : "TTS!";
  setOutputEnabled(connected);
  suppressEndOfFile_ = false;
  Serial.printf("[tts] file playback=%s lang=%s text='%s'\n",
                connected ? "OK" : "HIBA", language.c_str(), speech.c_str());
  return connected;
}

void AudioEngine::stop() {
  setOutputEnabled(false);
  suppressEndOfFile_ = true;
  audio_.setDefaults();
  suppressEndOfFile_ = false;
  paused_ = false;
  connecting_ = false;
  commandQueued_ = false;
  tcpConnected_ = false;
  currentUrl_ = "";
  if (metadataMutex_ && xSemaphoreTake(metadataMutex_, pdMS_TO_TICKS(30))) {
    streamTitle_ = "";
    artist_ = "";
    title_ = "";
    xSemaphoreGive(metadataMutex_);
  }
  statusText_ = "Leállítva";
  stateCode_ = "STOP";
}

bool AudioEngine::togglePause() {
  if (!audio_.pauseResume()) return false;
  paused_ = !paused_;
  setOutputEnabled(!paused_);
  stateCode_ = paused_ ? "STOP" : "PLAY";
  return true;
}

void AudioEngine::setVolume(uint8_t volume) {
  volume_ = constrain(volume, 0, kDisplayVolumeMax);
  audio_.setVolume(toInternalVolume(volume_));
}

void AudioEngine::setConnectionError(const char* message) {
  connecting_ = false;
  commandQueued_ = false;
  if (metadataMutex_ &&
      xSemaphoreTake(metadataMutex_, pdMS_TO_TICKS(30)) == pdTRUE) {
    statusText_ = message ? message : "Kapcsolódási hiba";
    stateCode_ = "M3U!";
    xSemaphoreGive(metadataMutex_);
  }
}

AudioLevels AudioEngine::levels() {
  return spectrum_.snapshot();
}

void AudioEngine::setVisualizationEnabled(bool enabled) {
  spectrum_.setEnabled(enabled);
}

AudioSnapshot AudioEngine::snapshot(const String& stationName) {
  AudioSnapshot result;
  result.stationName = stationName;
  result.volume = volume_;
  result.paused = paused_;
  result.connecting = connecting_;
  result.running = audio_.isRunning();
  result.sampleRate = audio_.getSampleRate();
  result.bitsPerSample = audio_.getBitsPerSample();
  result.channels = audio_.getChannels();
  const size_t total = audio_.getInBufferSize();
  const size_t filled = audio_.inBufferFilled();
  result.bufferFilledBytes = filled;
  result.bufferPercent =
      total ? static_cast<uint8_t>((filled * 100U) / total) : 0;

  if (metadataMutex_ && xSemaphoreTake(metadataMutex_, pdMS_TO_TICKS(30))) {
  result.streamTitle = streamTitle_;
  result.codec = codec_;
  result.statusText = statusText_;
    result.stateCode = stateCode_;
    result.bitrateKbps = bitrateKbps_;
    xSemaphoreGive(metadataMutex_);
  }
  result.initialized = initialized_;
  result.commandQueued = commandQueued_;
  result.connectAttempted = connectAttempted_;
  result.tcpConnected = tcpConnected_;
  if (result.codec.isEmpty() && audio_.getCodec() > 0) {
    result.codec = audio_.getCodecname();
  }
  return result;
}

bool AudioEngine::consumeEndOfFile() {
  if (!endOfFile_) return false;
  endOfFile_ = false;
  return true;
}

bool AudioEngine::takeArtworkEvent(ArtworkEvent& event) {
  return artworkQueue_ &&
         xQueueReceive(artworkQueue_, &event, 0) == pdTRUE;
}

size_t AudioEngine::bufferFilled() { return audio_.inBufferFilled(); }

size_t AudioEngine::bufferFree() { return audio_.inBufferFree(); }

size_t AudioEngine::bufferSize() { return audio_.getInBufferSize(); }

bool AudioEngine::running() { return audio_.isRunning(); }

bool AudioEngine::paused() const { return paused_; }

uint32_t AudioEngine::audioCurrentTime() { return audio_.getAudioCurrentTime(); }

uint32_t AudioEngine::audioStackFreeWords() {
  return initialized_ ? audio_.getHighWatermark() : 0;
}

void AudioEngine::processDeferredLogs() {
  if (!logQueue_) return;
  DeferredLog log;
  while (xQueueReceive(logQueue_, &log, 0) == pdTRUE) {
    Serial.printf("[audio] %s\n", log.text);
    if (metadataMutex_ &&
        xSemaphoreTake(metadataMutex_, pdMS_TO_TICKS(30))) {
      statusText_ = log.text;
      stateCode_ = "HIBA";
      xSemaphoreGive(metadataMutex_);
    }
  }
}

void AudioEngine::deferAudioLog(const char* text) {
  if (!logQueue_) return;
  DeferredLog log;
  strlcpy(log.text, text ? text : "", sizeof(log.text));
  if (xQueueSend(logQueue_, &log, 0) == pdTRUE) return;

  DeferredLog discarded;
  xQueueReceive(logQueue_, &discarded, 0);
  xQueueSend(logQueue_, &log, 0);
}

void AudioEngine::setOutputEnabled(bool enabled) {
  outputEnabled_ = enabled;
  if (MUTE_PIN != 255) {
    digitalWrite(MUTE_PIN, enabled ? !MUTE_VAL : MUTE_VAL);
  }
}

uint8_t AudioEngine::toInternalVolume(uint8_t displayVolume) {
  const uint16_t value =
      static_cast<uint16_t>(min(displayVolume, kDisplayVolumeMax)) *
      kMaleksmVolumeMax;
  return static_cast<uint8_t>(
      (value + kDisplayVolumeMax / 2) / kDisplayVolumeMax);
}

void AudioEngine::audioInfoCallback(Audio::msg_t message) {
  if (!instance_) return;
  // Az AUDIO_LOG_IMPL a dekóder PeriodicTask feladatából is hívhatja ezt
  // a callbacket. Ott a Serial.printf/String feldolgozás stackigénye egy
  // hibás MP3 frame mély Huffman hívási láncán stack-canary rebootot okozott.
  // A dekóderfeladat ezért csak másol, a nehéz feldolgozás a loopTaskon fut.
  if (message.e == Audio::evt_log) {
    instance_->deferAudioLog(message.msg);
    return;
  }
  instance_->handleAudioInfo(message);
}

void AudioEngine::handleAudioInfo(Audio::msg_t message) {
  const char* text = message.msg ? message.msg : "";

  if (message.e == Audio::evt_icylogo || message.e == Audio::evt_image) {
    if (!artworkQueue_) return;
    ArtworkEvent event;
    if (message.e == Audio::evt_icylogo) {
      event.kind = ArtworkEventKind::IcyLogo;
      strlcpy(event.text, text, sizeof(event.text));
    } else {
      event.kind = ArtworkEventKind::EmbeddedImage;
      event.segmentValues =
          min<size_t>(message.vec.size(), sizeof(event.segments) /
                                              sizeof(event.segments[0]));
      for (uint8_t index = 0; index < event.segmentValues; ++index)
        event.segments[index] = message.vec[index];
    }
    if (xQueueSend(artworkQueue_, &event, 0) != pdTRUE) {
      ArtworkEvent discarded;
      xQueueReceive(artworkQueue_, &discarded, 0);
      xQueueSend(artworkQueue_, &event, 0);
    }
    return;
  }

  if (message.e == Audio::evt_eof) {
    if (!suppressEndOfFile_) endOfFile_ = true;
    return;
  }

  if (message.e == Audio::evt_info || message.e == Audio::evt_log) {
    Serial.printf("[audio] %s\n", text);
    if (metadataMutex_ &&
        xSemaphoreTake(metadataMutex_, pdMS_TO_TICKS(30))) {
      statusText_ = text;
      if (message.e == Audio::evt_log) stateCode_ = "HIBA";
      xSemaphoreGive(metadataMutex_);
    }
    return;
  }

  if (message.e != Audio::evt_streamtitle &&
      message.e != Audio::evt_bitrate &&
      message.e != Audio::evt_id3data) {
    return;
  }

  if (!metadataMutex_ ||
      !xSemaphoreTake(metadataMutex_, pdMS_TO_TICKS(30))) {
    return;
  }

  if (message.e == Audio::evt_streamtitle) {
    streamTitle_ = normalizeDisplayText(String(text));
    streamTitle_.trim();
  } else if (message.e == Audio::evt_bitrate) {
    uint32_t value = String(text).toInt();
    bitrateKbps_ = value >= 1000 ? (value + 500) / 1000 : value;
    statusText_ = "";
    stateCode_ = "PLAY";
  } else if (message.e == Audio::evt_id3data) {
    String value = normalizeDisplayText(String(text));
    value.trim();
    if (value.startsWith("Artist:")) {
      artist_ = value.substring(7);
      artist_.trim();
    } else if (value.startsWith("Title:")) {
      title_ = value.substring(6);
      title_.trim();
    }
    if (!artist_.isEmpty() && !title_.isEmpty()) {
      streamTitle_ = artist_ + " - " + title_;
    } else if (!title_.isEmpty()) {
      streamTitle_ = title_;
    } else if (!artist_.isEmpty()) {
      streamTitle_ = artist_;
    }
  }

  if (codec_.isEmpty() && audio_.getCodec() > 0) {
    codec_ = audio_.getCodecname();
  }
  xSemaphoreGive(metadataMutex_);
}
