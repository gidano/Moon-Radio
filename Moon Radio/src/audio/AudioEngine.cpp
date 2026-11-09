#include "AudioEngine.h"
#include "../i18n/Language.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>

#include "options.h"

AudioEngine* AudioEngine::instance_ = nullptr;

namespace {

constexpr uint8_t kDisplayVolumeMax = 21;
constexpr uint8_t kMaleksmVolumeMax = 254;
constexpr const char* kGoogleTtsTempPath = "/clock_tts.mp3";

// A hálózati stream olvasása (Audio::loop) saját taskban fut, a fő loop
// (LVGL, PNG-dekódolás, HTTP/TLS borító-, metaadat- és időjáráskérések,
// webszerver) blokkolásától függetlenül.  Az az elv, hogy a puffer töltése soha nem vár a kijelzőre.
// A fő loopTask prioritása 1; ez fölötte fut, ugyanazon a magon, így a
// hosszú UI-munkát rövid időre megszakítja, majd vTaskDelay-jel visszaadja.
#ifndef AUDIO_STREAM_TASK_CORE
#define AUDIO_STREAM_TASK_CORE 1
#endif
#ifndef AUDIO_STREAM_TASK_PRIORITY
#define AUDIO_STREAM_TASK_PRIORITY 3
#endif
#ifndef AUDIO_STREAM_TASK_STACK_BYTES
// A stack a PSRAM-ban van, így a
// szűkös belső heapből (logó-, borítótaskok) semmit nem vesz el.  Ez a task
// nem nyúl a flash-hez: a helyi fájlos TTS-t a fő loop olvassa.
#define AUDIO_STREAM_TASK_STACK_BYTES 12288
#endif
// Eddig a töltöttségig korlátlan sebességgel töltjük a puffert.
constexpr size_t kStreamFastFillBytes = 128U * 1024U;

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
  audioMutex_ = xSemaphoreCreateMutex();
  instance_ = this;
}

AudioEngine::~AudioEngine() {
  if (streamTask_) {
    vTaskDeleteWithCaps(streamTask_);
    streamTask_ = nullptr;
  }
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
  if (audioMutex_) vSemaphoreDelete(audioMutex_);
}

bool AudioEngine::begin(uint8_t volume) {
  mainTask_ = xTaskGetCurrentTaskHandle();
  Audio::audio_info_callback = audioInfoCallback;
  // A stream-olvasó task minden szöveges eseményt ide ad át, ezért a sor
  // kicsit hosszabb, hogy egy címváltás ne szoruljon ki a naplósorok miatt.
  logQueue_ = xQueueCreate(16, sizeof(DeferredLog));
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
  // I2S DMA: 32 x 256 frame = ~186 ms (44,1 kHz).  A korábbi 48 a belső
  // (DMA-képes) RAM-ból 16 KiB-ot vett el, pedig ugyanez a RAM kell a Wi-Fi
  // vételi puffereinek is: TLS-es borítókeresés közben 9 KiB-ra esett a
  // belső heap, a Wi-Fi csomagokat dobott, és a stream elakadt.  A hálózati
  // ingadozást úgyis a nagy PSRAM-os bemeneti puffer hidalja át.
  // setPinout() előtt kell beállítani.
  audio_.settings.DMA_DESC_NUM = 32;
  // Kis induló tartalék: a puffert innentől a külön stream-olvasó task
  // folyamatosan tölti, így nem kell másodpercekig némán várni a 200 KiB-os
  // előtöltésre.  Ha a szerver nem küld kezdő löketet, legfeljebb 1,5 mp
  // után a minimális (két frame-es) tartalékkal is elindul.
  audio_.settings.BUFFER_TRESHOLD_WEBSTREAM = 32U * 1024U;
  audio_.settings.BUFFER_TRESHOLD_WEBSTREAM_TIMEOUT_MS = 1500U;
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
  if (!streamTask_ &&
      xTaskCreatePinnedToCoreWithCaps(
          streamTaskEntry, "audio-stream", AUDIO_STREAM_TASK_STACK_BYTES, this,
          AUDIO_STREAM_TASK_PRIORITY, &streamTask_, AUDIO_STREAM_TASK_CORE,
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    streamTask_ = nullptr;
    // Tartalék: a fő loop olvas tovább, mint korábban.
    Serial.println("[audio] Stream-olvaso task letrehozasi hiba");
  }
  initialized_ = true;
  stateCode_ = "KESZ";
  statusText_ = "Audio kesz";
  Serial.printf("[audio] ESP32-audioI2S %s\n", audio_.getVersion());
  return true;
}

void AudioEngine::loop() {
  if ((!streamTask_ || localFilePlayback_) && lockAudio(0)) {
    audio_.loop();
    unlockAudio();
  }
  processDeferredLogs();

  if (audio_.inBufferFilled() > 0) {
    connecting_ = false;
    stateCode_ = "PLAY";
  } else if (connectAttempted_ && tcpConnected_ && !audio_.isRunning()) {
    stateCode_ = "STOP";
  }
  if (!audio_.isRunning() && outputEnabled_) setOutputEnabled(false);

  // Diagnosztika: 5 mp-enként a puffer és a belső RAM állapota, hogy egy
  // akadás oka (hálózat vagy belső RAM-hiány) a soros naplóból látsszon.
  // be = a hálózatról beolvasott adat az elmúlt időszakban (kbit/s): ha egy
  // pufferzuhanásnál 0 körüli, a kapcsolat állt meg; ha a stream bitrátája
  // alatt csordogál, a vétel lassú.
  static uint32_t lastDiagAt = 0;
  static uint32_t lastDiagBytes = 0;
  static uint32_t lastDiagChunks = 0;
  const uint32_t now = millis();
  if (audio_.isRunning() && !localFilePlayback_ && now - lastDiagAt >= 5000) {
    const uint32_t elapsed = now - lastDiagAt;
    const uint32_t bytesRead = audio_.getStreamBytesRead();
    const uint32_t delta =
        bytesRead >= lastDiagBytes ? bytesRead - lastDiagBytes : bytesRead;
    lastDiagAt = now;
    lastDiagBytes = bytesRead;
    const size_t total = audio_.getInBufferSize();
    const size_t filled = audio_.inBufferFilled();
    // var = a socketben váró bájtok (átlag/max) az olvasás után: ha nagy,
    // a rádió olvas lassan; ha 0 körüli, a hálózat/TCP hoz keveset.
    // chunk = átlagos HTTP-chunk méret, kor = Audio::loop hívás/s.
    const uint32_t chunks = audio_.getChunkCount();
    const uint32_t chunkDelta = chunks - lastDiagChunks;
    lastDiagChunks = chunks;
    const uint32_t loops = diagLoops_;
    const uint32_t availMax = diagAvailMax_;
    const uint32_t availSamples = diagAvailSamples_;
    const uint32_t availAvg = availSamples ? diagAvailSum_ / availSamples : 0;
    diagLoops_ = 0;
    diagAvailMax_ = 0;
    diagAvailSum_ = 0;
    diagAvailSamples_ = 0;
    Serial.printf("[diag] var=%u/%u B chunk=%u B (%u db) kor=%u/s\n",
                  static_cast<unsigned>(availAvg),
                  static_cast<unsigned>(availMax),
                  static_cast<unsigned>(chunkDelta ? delta / chunkDelta : 0),
                  static_cast<unsigned>(chunkDelta),
                  static_cast<unsigned>(elapsed ? loops * 1000U / elapsed : 0));
    Serial.printf(
        "[diag] puffer=%u KB (%u%%) be=%u kbit/s belso heap=%u min=%u blokk=%u rssi=%d\n",
        static_cast<unsigned>(filled / 1024),
        static_cast<unsigned>(total ? filled * 100U / total : 0),
        static_cast<unsigned>(elapsed ? (uint64_t)delta * 8U / elapsed : 0),
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<int>(WiFi.RSSI()));
  }
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
  streamReadyAt_ = 0;
  statusText_ = "Kapcsolódás...";
  stateCode_ = "TCP";
  lockAudio();
  localFilePlayback_ = false;
  const bool connected = audio_.connecttohost(currentUrl_.c_str());
  unlockAudio();
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

bool AudioEngine::connectFile(fs::FS& fs, const String& path, int32_t seconds) {
  stop();
  if (logQueue_) xQueueReset(logQueue_);
  if (artworkQueue_) xQueueReset(artworkQueue_);
  endOfFile_ = false;
  connectAttempted_ = true;
  connectStartedAt_ = millis();
  suppressEndOfFile_ = true;
  if (metadataMutex_ && xSemaphoreTake(metadataMutex_, portMAX_DELAY)) {
    codec_ = "";
    bitrateKbps_ = 0;
    xSemaphoreGive(metadataMutex_);
  }
  lockAudio();
  // SD_MMC reads run independently of web requests and display updates.
  // Flash-backed speech keeps its separate main-loop path.
  localFilePlayback_ = false;
  const bool ok = audio_.connecttoFS(fs, path.c_str(), seconds);
  unlockAudio();
  tcpConnected_ = ok;
  currentUrl_ = path;
  streamReadyAt_ = ok ? max(1UL, millis()) : 0;
  stateCode_ = ok ? "SD" : "SD!";
  statusText_ = ok ? "SD" : "SD olvasási hiba";
  suppressEndOfFile_ = false;
  setOutputEnabled(ok);
  return ok;
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
  connectStartedAt_ = millis();
  streamReadyAt_ = 0;
  suppressEndOfFile_ = true;
  const bool downloaded = downloadGoogleTtsMp3(speech, language);
  bool connected = false;
  if (downloaded) {
    lockAudio();
    localFilePlayback_ = true;
    connected = audio_.connecttoFS(LittleFS, kGoogleTtsTempPath);
    unlockAudio();
  }
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
  lockAudio();
  audio_.setDefaults();
  // Discard diagnostics and artwork queued by the source that just stopped.
  if (logQueue_) xQueueReset(logQueue_);
  if (artworkQueue_) xQueueReset(artworkQueue_);
  localFilePlayback_ = false;
  unlockAudio();
  suppressEndOfFile_ = false;
  paused_ = false;
  connecting_ = false;
  commandQueued_ = false;
  tcpConnected_ = false;
  streamReadyAt_ = 0;
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
  lockAudio();
  const bool toggled = audio_.pauseResume();
  unlockAudio();
  if (!toggled) return false;
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
    result.statusText = Language::text(statusText_.c_str());
    result.stateCode = Language::text(stateCode_.c_str());
    result.bitrateKbps = bitrateKbps_;
    xSemaphoreGive(metadataMutex_);
  }
  // Nem minden élő adó küld icy-br fejlécet / bitrate eseményt. A dekóder
  // ilyenkor ismeri a feldolgozott MP3 frame-ek tényleges vagy becsült
  // bitrátáját, ezért ezt használjuk tartalékként a kijelzéshez és a
  // borítóletöltés pufferküszöbéhez.
  if (!result.bitrateKbps) {
    const uint32_t decoderBitrate = audio_.getBitRate();
    result.bitrateKbps = decoderBitrate >= 1000
                             ? (decoderBitrate + 500) / 1000
                             : decoderBitrate;
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

bool AudioEngine::consumeSlowStreamWarning() {
  if (!slowStreamWarning_) return false;
  slowStreamWarning_ = false;
  return true;
}

uint32_t AudioEngine::streamReadyForMs() const {
  const uint32_t readyAt = streamReadyAt_;
  return readyAt ? millis() - readyAt : 0;
}

uint32_t AudioEngine::msSinceConnect() const {
  return millis() - connectStartedAt_;
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

bool AudioEngine::lockAudio(TickType_t timeout) {
  return !audioMutex_ || xSemaphoreTake(audioMutex_, timeout) == pdTRUE;
}

void AudioEngine::unlockAudio() {
  if (audioMutex_) xSemaphoreGive(audioMutex_);
}

void AudioEngine::streamTaskEntry(void* context) {
  static_cast<AudioEngine*>(context)->streamTask();
}

void AudioEngine::streamTask() {
  for (;;) {
    // Rövid várakozás: ha a fő loop éppen kapcsolódik/leállít, a következő
    // körben újrapróbáljuk, a puffer addig is a dekóder rendelkezésére áll.
    if (!localFilePlayback_ && lockAudio(pdMS_TO_TICKS(20))) {
      // Chunked HTTP-nél egy Audio::loop() legfeljebb egy chunkot olvas be,
      // így apró chunkok mellett a hívások száma szabja meg a vételi
      // sebességet: 10 ms-os ütemnél ~1,4 KB-os chunkokkal ez csak ~1,1 Mbit/s,
      // épp a FLAC bitrátája, így a puffer nem tud nőni.  Ezért amíg a
      // socketben adat vár és a pufferben van hely, egy körben többször is
      // olvasunk (legfeljebb ~3 ms-ig), mielőtt elengedjük a CPU-t.
      const uint32_t burstStarted = millis();
      for (uint8_t i = 0; i < 16 && !localFilePlayback_; ++i) {
        audio_.loop();
        ++diagLoops_;
        const uint32_t avail = audio_.socketAvailable();
        if (i == 0) {
          if (avail > diagAvailMax_) diagAvailMax_ = avail;
          diagAvailSum_ += avail;
          ++diagAvailSamples_;
        }
        if (avail == 0 || millis() - burstStarted >= 3) break;
        const size_t bufTotal = audio_.getInBufferSize();
        if (!bufTotal || audio_.inBufferFilled() + UINT16_MAX >= bufTotal) break;
      }
      unlockAudio();
    }
    // Amíg kevés a tartalék, minden ms-ban olvasunk.  Utána ritkábban: a
    // korlátlan sebességű feltöltés (akár MB/s) a 0. magon futó Wi-Fi/lwIP
    // feladatokat terheli, és elveszi a CPU-t a dekódertől, ami a nehéz
    // HE-AAC-nál induláskori döcögést okoz, amíg a puffer meg nem telik.
    // A fenti többszörös olvasás miatt egy körben a socketben váró teljes
    // adat (legfeljebb a 32 KiB-os TCP-ablak) kiürül, chunked streamnél is,
    // így a vétel felső korlátja a TCP-ablak / RTT, nem ez az ütemezés.
    const size_t total = audio_.getInBufferSize();
    const size_t filled = audio_.inBufferFilled();
    TickType_t delayTicks = 1;
    if (total && filled >= kStreamFastFillBytes)
      delayTicks = pdMS_TO_TICKS(filled * 2 >= total ? 25 : 10);
    vTaskDelay(delayTicks);
  }
}

void AudioEngine::processDeferredLogs() {
  if (!logQueue_) return;
  DeferredLog log;
  while (xQueueReceive(logQueue_, &log, 0) == pdTRUE) {
    Audio::msg_t message;
    message.e = static_cast<Audio::event_t>(log.event);
    message.msg = log.text;
    handleAudioInfo(message);
  }
}

void AudioEngine::deferAudioLog(Audio::event_t event, const char* text) {
  if (!logQueue_) return;
  DeferredLog log;
  log.event = static_cast<uint8_t>(event);
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
  // The decoder PeriodicTask and the stream reader task must not run Serial,
  // String/UTF-8 work or mutex waits from a deep decode/read call chain;
  // copy every textual event to the Arduino loop task instead.  Artwork image
  // vectors retain a different payload and continue through their small,
  // copy-only queue path below.
  const bool fromAudioTask =
      !instance_->mainTask_ ||
      xTaskGetCurrentTaskHandle() != instance_->mainTask_;
  const bool deferTextEvent =
      message.e == Audio::evt_info || message.e == Audio::evt_log ||
      message.e == Audio::evt_streamtitle || message.e == Audio::evt_bitrate ||
      message.e == Audio::evt_id3data || message.e == Audio::evt_icylogo;
  if (message.e == Audio::evt_log || (fromAudioTask && deferTextEvent)) {
    instance_->deferAudioLog(message.e, message.msg);
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
    if (strstr(text, "slow stream") != nullptr) {
      slowStreamWarning_ = true;
    }
    if (strstr(text, "stream ready") != nullptr) {
      streamReadyAt_ = millis();
      if (!streamReadyAt_) streamReadyAt_ = 1;
    } else if (strstr(text, "reconnecting") != nullptr ||
               strstr(text, "Stream lost") != nullptr) {
      // A könyvtár saját újrakapcsolódása (pl. http->https átirányítás) új
      // TLS-kézfogást jelent: a háttérmunka újra várjon.
      streamReadyAt_ = 0;
      connectStartedAt_ = millis();
    }
    Serial.printf("[audio] %s\n", text);
    // Decoder lifecycle messages are diagnostics, not user-facing failures.
    // Deferred events from the previous source must not overwrite SD/STOP.
    if (strstr(text, "Decoder has been destroyed") ||
        strstr(text, "Decoder has been initialized")) return;
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
    int separator = value.indexOf(':');
    const int equals = value.indexOf('=');
    if (equals >= 0 && (separator < 0 || equals < separator)) separator = equals;
    String key = separator >= 0 ? value.substring(0, separator) : String();
    key.trim();
    key.toLowerCase();
    if (key == "artist") {
      artist_ = value.substring(separator + 1);
      artist_.trim();
    } else if (key == "title") {
      title_ = value.substring(separator + 1);
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
