#include "RadioWebServer.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <memory>
#include <new>
#include <vector>

#include "../app/RadioController.h"

namespace {

constexpr const char* kJson = "application/json; charset=utf-8";

String normalizeLogo(String logo) {
  logo.trim();
  if (logo.isEmpty()) logo = "nologo";
  return logo;
}

bool collectRemoveTargets(const String& path, std::vector<String>& files,
                          std::vector<String>& dirs) {
  File entry = LittleFS.open(path, FILE_READ);
  if (!entry) return false;

  if (!entry.isDirectory()) {
    entry.close();
    files.push_back(path);
    return true;
  }

  File child = entry.openNextFile();
  while (child) {
    String childPath = child.path();
    if (childPath.isEmpty()) childPath = child.name();
    childPath.replace("\\", "/");
    if (!childPath.startsWith("/")) {
      childPath = path == "/" ? "/" + childPath : path + "/" + childPath;
    }
    while (childPath.indexOf("//") >= 0) childPath.replace("//", "/");
    const bool childIsDir = child.isDirectory();
    child.close();

    if (childIsDir) {
      if (!collectRemoveTargets(childPath, files, dirs)) return false;
    } else {
      files.push_back(childPath);
    }

    child = entry.openNextFile();
  }

  entry.close();
  dirs.push_back(path);
  return true;
}

bool removeRecursive(const String& path) {
  if (path == "/") return false;
  std::vector<String> files;
  std::vector<String> dirs;
  if (!collectRemoveTargets(path, files, dirs)) return false;

  bool ok = true;
  for (const String& filePath : files) {
    if (LittleFS.exists(filePath) && !LittleFS.remove(filePath)) ok = false;
    delay(0);
  }
  for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
    const String& dirPath = *it;
    if (dirPath != "/" && LittleFS.exists(dirPath) && !LittleFS.rmdir(dirPath)) {
      ok = false;
    }
    delay(0);
  }
  return ok;
}

size_t littleFsFreeBytes() {
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

void purgeCacheForSpace(size_t requiredBytes) {
  if (!LittleFS.exists("/cache")) LittleFS.mkdir("/cache");
  if (littleFsFreeBytes() >= requiredBytes) return;

  File root = LittleFS.open("/cache");
  if (!root || !root.isDirectory()) return;
  for (File file = root.openNextFile(); file && littleFsFreeBytes() < requiredBytes;
       file = root.openNextFile()) {
    String path = file.path();
    const size_t size = file.size();
    file.close();
    if (!path.startsWith("/cache/")) continue;
    if (LittleFS.remove(path)) {
      Serial.printf("[web] cache helyfelszabaditas: %s (%u byte)\n",
                    path.c_str(), static_cast<unsigned>(size));
    }
    delay(0);
  }
  root.close();
}

const char* visualizerModeName(uint8_t mode) {
  switch (mode) {
    case 1:
      return "vu";
    case 2:
      return "off";
    default:
      return "spectrum";
  }
}

}  // namespace

RadioWebServer::RadioWebServer(RadioController& radio) : radio_(radio) {}

void RadioWebServer::begin() {
  registerRoutes();
  if (radio_.wifi().accessPointMode()) {
    dnsRunning_ = dns_.start(53, "*", WiFi.softAPIP());
  }
  server_.begin();
  Serial.printf("[web] http://%s/\n", radio_.wifi().address().c_str());
}

void RadioWebServer::loop() {
  if (dnsRunning_) dns_.processNextRequest();
  server_.handleClient();
  if (backgroundUploadFinalizePending_) {
    backgroundUploadFinalizePending_ = false;
    radio_.endBackgroundUploadMode();
  }
  if (restartPending_ && static_cast<int32_t>(millis() - restartAt_) >= 0) {
    ESP.restart();
  }
}

void RadioWebServer::registerRoutes() {
  server_.on("/", HTTP_GET, [this]() { serveHome(); });
  server_.on("/search", HTTP_GET,
             [this]() { serveFile("/web/search_hu.html", "text/html"); });
  server_.on("/settings", HTTP_GET,
             [this]() { serveFile("/web/settings_hu.html", "text/html"); });
  server_.on("/style.css", HTTP_GET,
             [this]() { serveFile("/web/style.css", "text/css"); });
  server_.on("/theme.css", HTTP_GET,
             [this]() { serveFile("/web/theme.css", "text/css"); });
  server_.on("/favicon.ico", HTTP_GET,
             [this]() { serveFile("/web/favicon.svg", "image/svg+xml"); });
  server_.on("/wifi", HTTP_GET, [this]() { serveWifiForm(); });
  server_.on("/upload", HTTP_GET, [this]() { serveUploadForm(); });
  server_.on("/api/wifi", HTTP_POST, [this]() {
    const String ssid = argument("ssid");
    const String password = argument("password");
    if (!radio_.wifi().saveCredentials(ssid, password)) {
      server_.send(400, "text/plain; charset=utf-8",
                   "A Wi-Fi beállítás nem menthető.");
      return;
    }
    server_.send(200, "text/plain; charset=utf-8",
                 "Wi-Fi elmentve. Az eszköz újraindul.");
    restartPending_ = true;
    restartAt_ = millis() + 800;
  });

  server_.on("/api/stations", HTTP_GET, [this]() { sendStations(); });
  server_.on("/api/status", HTTP_GET, [this]() { sendStatus(); });
  server_.on("/api/buffer", HTTP_GET, [this]() { sendBuffer(); });
  server_.on("/api/logo/current.bmp", HTTP_GET,
             [this]() { sendCurrentLogo(); });
  server_.on("/api/backgrounds", HTTP_GET, [this]() { sendBackgrounds(); });
  server_.on("/api/background/delete", HTTP_POST,
             [this]() { deleteBackground(); });
  server_.on("/api/fs/ping", HTTP_GET, [this]() { fsPing(); });
  server_.on("/api/fs/info", HTTP_GET, [this]() { fsInfo(); });
  server_.on("/api/fs/list", HTTP_GET, [this]() { fsList(); });
  server_.on("/api/fs/read", HTTP_GET, [this]() { fsRead(); });
  server_.on("/api/fs/delete", HTTP_POST, [this]() { fsDelete(); });
  server_.on("/api/fs/mkdir", HTTP_POST, [this]() { fsMkdir(); });
  server_.on("/api/fs/rmdir", HTTP_POST, [this]() { fsRmdir(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { fsReboot(); });
  server_.on("/api/stations/add", HTTP_POST, [this]() { addStation(); });
  server_.on("/api/stations/update", HTTP_POST,
             [this]() { updateStation(); });
  server_.on("/api/stations/delete", HTTP_POST,
             [this]() { deleteStation(); });
  server_.on("/api/stations/reorder", HTTP_POST,
             [this]() { moveStation(); });
  server_.on("/api/stations/move", HTTP_POST,
             [this]() { moveStation(); });

  server_.on("/api/station", HTTP_POST, [this]() {
    const int index = argument("index").toInt();
    server_.send(radio_.selectStation(index) ? 200 : 400, kJson,
                 "{\"ok\":true}");
  });
  server_.on("/api/volume", HTTP_POST, [this]() {
    radio_.setVolume(argument("volume").toInt());
    server_.send(200, kJson, "{\"ok\":true}");
  });
  server_.on("/api/brightness", HTTP_GET, [this]() {
    server_.send(200, kJson,
                 "{\"value\":" + String(radio_.brightness()) + "}");
  });
  server_.on("/api/brightness", HTTP_POST, [this]() {
    radio_.setBrightness(argument("val").toInt());
    server_.send(200, kJson, "{\"ok\":true}");
  });
  server_.on("/api/settings", HTTP_GET, [this]() {
    const WeatherConfig weather = radio_.weatherConfig();
    const RadioController::ClockTtsConfig clockTts = radio_.clockTtsConfig();
    const String backgroundPath = radio_.backgroundPath();
    server_.send(200, kJson,
                 "{\"timezone\":\"" +
                     jsonEscape(radio_.timezone()) +
                     "\",\"background\":{\"enabled\":" +
                     String(radio_.backgroundEnabled() ? "true" : "false") +
                     ",\"path\":\"" + jsonEscape(backgroundPath) +
                     "\",\"opacity\":" + String(radio_.backgroundOpacity()) +
                     ",\"invertColors\":" +
                     String(radio_.colorInverted() ? "true" : "false") +
                     ",\"flipScreen\":" +
                     String(radio_.screenFlipped() ? "true" : "false") +
                     "},\"visualizer\":{\"mode\":\"" +
                     visualizerModeName(radio_.visualizerMode()) +
                     "\"},\"display\":{\"headerIp\":" +
                     String(radio_.headerIpVisible() ? "true" : "false") +
                     ",\"wifiDetails\":" +
                     String(radio_.wifiDetailsVisible() ? "true" : "false") +
                     ",\"volumeGraphic\":" +
                     String(radio_.volumeGraphicVisible() ? "true" : "false") +
                     ",\"bufferBar\":" +
                     String(radio_.bufferBarVisible() ? "true" : "false") +
                     ",\"diagnostics\":" +
                     String(radio_.diagnosticsVisible() ? "true" : "false") +
                     ",\"albumCovers\":" +
                     String(radio_.albumCoversEnabled() ? "true" : "false") +
                     "},\"weather\":{\"enabled\":" +
                     String(weather.enabled ? "true" : "false") +
                     ",\"latitude\":" + String(weather.latitude, 4) +
                     ",\"longitude\":" + String(weather.longitude, 4) +
                     ",\"intervalMinutes\":" +
                     String(weather.intervalMinutes) +
                     ",\"mode\":\"" +
                     String(weather.mode == WeatherDisplayMode::Today
                                ? "today"
                                : weather.mode == WeatherDisplayMode::Tomorrow
                                      ? "tomorrow"
                                      : "current") +
                     "\"},\"clockTts\":{\"enabled\":" +
                     String(clockTts.enabled ? "true" : "false") +
                     ",\"language\":\"" + jsonEscape(clockTts.language) +
                     "\",\"intervalMinutes\":" +
                     String(clockTts.intervalMinutes) +
                     ",\"onlyWhenNoStream\":" +
                     String(clockTts.onlyWhenNoStream ? "true" : "false") +
                     ",\"quietHoursEnabled\":" +
                     String(clockTts.quietHoursEnabled ? "true" : "false") +
                     ",\"quietFromMinutes\":" +
                     String(clockTts.quietFromMinutes) +
                     ",\"quietToMinutes\":" +
                     String(clockTts.quietToMinutes) + "}}");
  });
  server_.on("/api/settings", HTTP_POST, [this]() {
    const String body = server_.hasArg("plain") ? server_.arg("plain") : "";
    String timezone = argument("timezone");
    if (timezone.isEmpty() && !body.isEmpty())
      timezone = jsonStringField(body, "timezone");
    if (!radio_.setTimezone(timezone)) {
      server_.send(400, kJson,
                   "{\"ok\":false,\"error\":\"invalid timezone\"}");
      return;
    }
    bool backgroundEnabled = radio_.backgroundEnabled();
    String backgroundEnabledValue = argument("backgroundEnabled");
    if (backgroundEnabledValue.isEmpty() && !body.isEmpty())
      backgroundEnabledValue = jsonStringField(body, "backgroundEnabled");
    if (!backgroundEnabledValue.isEmpty()) {
      backgroundEnabled =
          backgroundEnabledValue == "true" || backgroundEnabledValue == "1";
    }
    String backgroundPath = argument("backgroundPath");
    if (backgroundPath.isEmpty() && !body.isEmpty())
      backgroundPath = jsonStringField(body, "backgroundPath");
    String backgroundOpacityValue = argument("backgroundOpacity");
    if (backgroundOpacityValue.isEmpty() && !body.isEmpty())
      backgroundOpacityValue = jsonStringField(body, "backgroundOpacity");
    uint8_t backgroundOpacity = radio_.backgroundOpacity();
    if (!backgroundOpacityValue.isEmpty()) {
      backgroundOpacity =
          static_cast<uint8_t>(constrain(backgroundOpacityValue.toInt(), 32, 255));
    }
    if (!radio_.setBackgroundConfig(backgroundEnabled, backgroundPath,
                                    backgroundOpacity)) {
      server_.send(400, kJson,
                   "{\"ok\":false,\"error\":\"invalid background config\"}");
      return;
    }
    bool invertColors = radio_.colorInverted();
    String invertColorsValue = argument("invertColors");
    if (invertColorsValue.isEmpty() && !body.isEmpty())
      invertColorsValue = jsonStringField(body, "invertColors");
    if (!invertColorsValue.isEmpty()) {
      invertColors =
          invertColorsValue == "true" || invertColorsValue == "1";
    }
    radio_.setColorInverted(invertColors);
    bool flipScreen = radio_.screenFlipped();
    String flipScreenValue = argument("flipScreen");
    if (flipScreenValue.isEmpty() && !body.isEmpty())
      flipScreenValue = jsonStringField(body, "flipScreen");
    if (!flipScreenValue.isEmpty()) {
      flipScreen =
          flipScreenValue == "true" || flipScreenValue == "1";
    }
    radio_.setScreenFlipped(flipScreen);
    String visualizerMode = argument("visualizerMode");
    if (visualizerMode.isEmpty() && !body.isEmpty())
      visualizerMode = jsonStringField(body, "visualizerMode");
    if (!visualizerMode.isEmpty()) {
      if (visualizerMode == "vu")
        radio_.setVisualizerMode(1);
      else if (visualizerMode == "off")
        radio_.setVisualizerMode(2);
      else
        radio_.setVisualizerMode(0);
    }
    String headerIp = argument("headerIp");
    if (headerIp.isEmpty() && !body.isEmpty())
      headerIp = jsonStringField(body, "headerIp");
    if (!headerIp.isEmpty()) {
      radio_.setHeaderIpVisible(headerIp == "true" || headerIp == "1");
    }
    String wifiDetails = argument("wifiDetails");
    if (wifiDetails.isEmpty() && !body.isEmpty())
      wifiDetails = jsonStringField(body, "wifiDetails");
    if (!wifiDetails.isEmpty()) {
      radio_.setWifiDetailsVisible(wifiDetails == "true" ||
                                   wifiDetails == "1");
    }
    String volumeGraphic = argument("volumeGraphic");
    if (volumeGraphic.isEmpty() && !body.isEmpty())
      volumeGraphic = jsonStringField(body, "volumeGraphic");
    if (!volumeGraphic.isEmpty()) {
      radio_.setVolumeGraphicVisible(volumeGraphic == "true" ||
                                     volumeGraphic == "1");
    }
    String bufferBar = argument("bufferBar");
    if (bufferBar.isEmpty() && !body.isEmpty())
      bufferBar = jsonStringField(body, "bufferBar");
    if (!bufferBar.isEmpty()) {
      radio_.setBufferBarVisible(bufferBar == "true" ||
                                 bufferBar == "1");
    }
    String diagnostics = argument("diagnostics");
    if (diagnostics.isEmpty() && !body.isEmpty())
      diagnostics = jsonStringField(body, "diagnostics");
    if (!diagnostics.isEmpty()) {
      radio_.setDiagnosticsVisible(diagnostics == "true" ||
                                   diagnostics == "1");
    }
    String albumCovers = argument("albumCovers");
    if (albumCovers.isEmpty() && !body.isEmpty())
      albumCovers = jsonStringField(body, "albumCovers");
    if (!albumCovers.isEmpty()) {
      radio_.setAlbumCoversEnabled(albumCovers == "true" ||
                                   albumCovers == "1");
    }
    WeatherConfig weather = radio_.weatherConfig();
    String weatherEnabled = argument("weatherEnabled");
    if (weatherEnabled.isEmpty() && !body.isEmpty())
      weatherEnabled = jsonStringField(body, "weatherEnabled");
    if (!weatherEnabled.isEmpty()) {
      weather.enabled = weatherEnabled == "true" || weatherEnabled == "1";
    }
    String latitude = argument("latitude");
    if (latitude.isEmpty() && !body.isEmpty())
      latitude = jsonStringField(body, "latitude");
    if (!latitude.isEmpty()) weather.latitude = latitude.toFloat();
    String longitude = argument("longitude");
    if (longitude.isEmpty() && !body.isEmpty())
      longitude = jsonStringField(body, "longitude");
    if (!longitude.isEmpty()) weather.longitude = longitude.toFloat();
    String intervalMinutes = argument("intervalMinutes");
    if (intervalMinutes.isEmpty() && !body.isEmpty())
      intervalMinutes = jsonStringField(body, "intervalMinutes");
    if (!intervalMinutes.isEmpty())
      weather.intervalMinutes = static_cast<uint16_t>(intervalMinutes.toInt());
    String weatherMode = argument("weatherMode");
    if (weatherMode.isEmpty() && !body.isEmpty())
      weatherMode = jsonStringField(body, "weatherMode");
    if (!weatherMode.isEmpty()) {
      if (weatherMode == "today")
        weather.mode = WeatherDisplayMode::Today;
      else if (weatherMode == "tomorrow")
        weather.mode = WeatherDisplayMode::Tomorrow;
      else
        weather.mode = WeatherDisplayMode::Current;
    }
    if (!radio_.setWeatherConfig(weather)) {
      server_.send(400, kJson,
                   "{\"ok\":false,\"error\":\"invalid weather config\"}");
      return;
    }
    RadioController::ClockTtsConfig clockTts = radio_.clockTtsConfig();
    String clockTtsEnabled = argument("clockTtsEnabled");
    if (clockTtsEnabled.isEmpty() && !body.isEmpty())
      clockTtsEnabled = jsonStringField(body, "clockTtsEnabled");
    if (!clockTtsEnabled.isEmpty()) {
      clockTts.enabled =
          clockTtsEnabled == "true" || clockTtsEnabled == "1";
    }
    String clockTtsLanguage = argument("clockTtsLanguage");
    if (clockTtsLanguage.isEmpty() && !body.isEmpty())
      clockTtsLanguage = jsonStringField(body, "clockTtsLanguage");
    if (!clockTtsLanguage.isEmpty()) clockTts.language = clockTtsLanguage;
    String clockTtsInterval = argument("clockTtsInterval");
    if (clockTtsInterval.isEmpty() && !body.isEmpty())
      clockTtsInterval = jsonStringField(body, "clockTtsInterval");
    if (!clockTtsInterval.isEmpty()) {
      const int interval = clockTtsInterval.toInt();
      clockTts.intervalMinutes =
          static_cast<uint16_t>(interval > 0 ? interval : 1);
    }
    String clockTtsOnlyNoStream = argument("clockTtsOnlyWhenNoStream");
    if (clockTtsOnlyNoStream.isEmpty() && !body.isEmpty())
      clockTtsOnlyNoStream =
          jsonStringField(body, "clockTtsOnlyWhenNoStream");
    if (!clockTtsOnlyNoStream.isEmpty()) {
      clockTts.onlyWhenNoStream =
          clockTtsOnlyNoStream == "true" || clockTtsOnlyNoStream == "1";
    }
    String clockTtsQuietEnabled = argument("clockTtsQuietHoursEnabled");
    if (clockTtsQuietEnabled.isEmpty() && !body.isEmpty())
      clockTtsQuietEnabled =
          jsonStringField(body, "clockTtsQuietHoursEnabled");
    if (!clockTtsQuietEnabled.isEmpty()) {
      clockTts.quietHoursEnabled =
          clockTtsQuietEnabled == "true" || clockTtsQuietEnabled == "1";
    }
    String clockTtsQuietFrom = argument("clockTtsQuietFromMinutes");
    if (clockTtsQuietFrom.isEmpty() && !body.isEmpty())
      clockTtsQuietFrom = jsonStringField(body, "clockTtsQuietFromMinutes");
    if (!clockTtsQuietFrom.isEmpty()) {
      clockTts.quietFromMinutes =
          static_cast<uint16_t>(clockTtsQuietFrom.toInt());
    }
    String clockTtsQuietTo = argument("clockTtsQuietToMinutes");
    if (clockTtsQuietTo.isEmpty() && !body.isEmpty())
      clockTtsQuietTo = jsonStringField(body, "clockTtsQuietToMinutes");
    if (!clockTtsQuietTo.isEmpty()) {
      clockTts.quietToMinutes =
          static_cast<uint16_t>(clockTtsQuietTo.toInt());
    }
    if (!radio_.setClockTtsConfig(clockTts)) {
      server_.send(400, kJson,
                   "{\"ok\":false,\"error\":\"invalid clock tts config\"}");
      return;
    }
    server_.send(200, kJson,
                 "{\"ok\":true,\"timezone\":\"" +
                     jsonEscape(radio_.timezone()) +
                     "\",\"background\":{\"enabled\":" +
                     String(radio_.backgroundEnabled() ? "true" : "false") +
                     ",\"path\":\"" + jsonEscape(radio_.backgroundPath()) +
                     "\",\"opacity\":" + String(radio_.backgroundOpacity()) +
                     ",\"invertColors\":" +
                     String(radio_.colorInverted() ? "true" : "false") +
                     ",\"flipScreen\":" +
                     String(radio_.screenFlipped() ? "true" : "false") +
                     "},\"visualizer\":{\"mode\":\"" +
                     visualizerModeName(radio_.visualizerMode()) +
                     "\"},\"display\":{\"headerIp\":" +
                     String(radio_.headerIpVisible() ? "true" : "false") +
                     ",\"wifiDetails\":" +
                     String(radio_.wifiDetailsVisible() ? "true" : "false") +
                     ",\"volumeGraphic\":" +
                     String(radio_.volumeGraphicVisible() ? "true" : "false") +
                     ",\"bufferBar\":" +
                     String(radio_.bufferBarVisible() ? "true" : "false") +
                     ",\"diagnostics\":" +
                     String(radio_.diagnosticsVisible() ? "true" : "false") +
                     ",\"albumCovers\":" +
                     String(radio_.albumCoversEnabled() ? "true" : "false") +
                     "},\"weather\":{\"enabled\":" +
                     String(weather.enabled ? "true" : "false") +
                     ",\"latitude\":" + String(weather.latitude, 4) +
                     ",\"longitude\":" + String(weather.longitude, 4) +
                     ",\"intervalMinutes\":" +
                     String(weather.intervalMinutes) +
                     ",\"mode\":\"" +
                     String(weather.mode == WeatherDisplayMode::Today
                                ? "today"
                                : weather.mode == WeatherDisplayMode::Tomorrow
                                      ? "tomorrow"
                                      : "current") +
                     "\"},\"clockTts\":{\"enabled\":" +
                     String(clockTts.enabled ? "true" : "false") +
                     ",\"language\":\"" + jsonEscape(clockTts.language) +
                     "\",\"intervalMinutes\":" +
                     String(clockTts.intervalMinutes) +
                     ",\"onlyWhenNoStream\":" +
                     String(clockTts.onlyWhenNoStream ? "true" : "false") +
                     ",\"quietHoursEnabled\":" +
                     String(clockTts.quietHoursEnabled ? "true" : "false") +
                     ",\"quietFromMinutes\":" +
                     String(clockTts.quietFromMinutes) +
                     ",\"quietToMinutes\":" +
                     String(clockTts.quietToMinutes) + "}}");
  });
  server_.on("/api/tts", HTTP_POST, [this]() {
    const String body = server_.hasArg("plain") ? server_.arg("plain") : "";
    String text = argument("text");
    if (text.isEmpty() && !body.isEmpty())
      text = jsonStringField(body, "text");
    String language = argument("language");
    if (language.isEmpty() && !body.isEmpty())
      language = jsonStringField(body, "language");
    const bool ok = radio_.forceClockTts(text, language);
    server_.send(ok ? 200 : 409, kJson,
                 ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server_.on("/api/toggle", HTTP_POST, [this]() {
    server_.send(radio_.togglePause() ? 200 : 409, kJson,
                 "{\"ok\":true}");
  });
  server_.on("/api/next", HTTP_POST, [this]() {
    server_.send(radio_.nextStation() ? 200 : 400, kJson,
                 "{\"ok\":true}");
  });
  server_.on("/api/prev", HTTP_POST, [this]() {
    server_.send(radio_.previousStation() ? 200 : 400, kJson,
                 "{\"ok\":true}");
  });
  server_.on("/api/track_next", HTTP_POST, [this]() {
    server_.send(radio_.nextTrack() ? 200 : 409, kJson,
                 "{\"ok\":true}");
  });
  server_.on("/api/track_prev", HTTP_POST, [this]() {
    server_.send(radio_.previousTrack() ? 200 : 409, kJson,
                 "{\"ok\":true}");
  });
  server_.on("/api/reset", HTTP_POST, [this]() {
    server_.send(200, kJson, "{\"ok\":true}");
    restartPending_ = true;
    restartAt_ = millis() + 500;
  });

  server_.on("/upload", HTTP_POST, [this]() { uploadFinished(); },
             [this]() { uploadChunk(); });
  server_.on("/api/background/upload", HTTP_POST,
             [this]() { backgroundUploadFinished(); },
             [this]() { backgroundUploadChunk(); });
  server_.on("/api/logo/upload", HTTP_POST,
             [this]() { logoUploadFinished(); },
             [this]() { logoUploadChunk(); });
  server_.on("/api/artwork/cache", HTTP_POST,
             [this]() { artworkCacheFinished(); },
             [this]() { artworkCacheChunk(); });
  server_.on("/api/artwork/proxy", HTTP_GET,
             [this]() { artworkProxy(); });
  server_.onNotFound([this]() { notFound(); });
}

void RadioWebServer::serveFile(const String& path,
                               const String& contentType) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    server_.send(404, "text/plain; charset=utf-8", "Nincs ilyen fájl.");
    return;
  }
  if (path.startsWith("/web/")) {
    server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server_.sendHeader("Pragma", "no-cache");
    server_.sendHeader("Expires", "0");
  }
  server_.streamFile(file, contentType);
  file.close();
}

void RadioWebServer::serveHome() {
  if (radio_.wifi().accessPointMode()) {
    serveWifiForm();
  } else {
    serveFile("/web/index_hu.html", "text/html; charset=utf-8");
  }
}

void RadioWebServer::serveWifiForm() {
  const String page =
      "<!doctype html><html lang='hu'><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Moon Radio Wi-Fi</title><style>"
      "body{font-family:sans-serif;background:#080b12;color:#f1f5f9;"
      "max-width:420px;margin:40px auto;padding:20px}"
      "input,button{box-sizing:border-box;width:100%;padding:12px;"
      "margin:8px 0;border-radius:8px;border:1px solid #334155}"
      "button{background:#e92b63;color:white;font-weight:bold}</style>"
      "<h1>Moon Radio</h1><p>Wi-Fi hálózat beállítása</p>"
      "<form method='post' action='/api/wifi'>"
      "<input name='ssid' placeholder='Hálózat neve' required>"
      "<input name='password' type='password' placeholder='Jelszó'>"
      "<button type='submit'>Mentés és újraindítás</button></form></html>";
  server_.send(200, "text/html; charset=utf-8", page);
}

void RadioWebServer::serveUploadForm() {
  const String page =
      "<!doctype html><html lang='hu'><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Moon Radio fájlfeltöltés</title><style>"
      "body{font-family:sans-serif;background:#080b12;color:#f1f5f9;"
      "max-width:520px;margin:40px auto;padding:20px}"
      "input,select,button{box-sizing:border-box;width:100%;padding:12px;"
      "margin:8px 0;border-radius:8px;border:1px solid #334155}"
      "button{background:#e92b63;color:white;font-weight:bold}"
      "a{color:#7dd3fc}</style><h1>LittleFS fájlfeltöltés</h1>"
      "<p>A Stations Editor a <code>/stations.txt</code> útvonalat "
      "ugyanezen a végponton használja.</p>"
      "<form method='post' action='/upload' enctype='multipart/form-data'>"
      "<input name='path' value='/stations.txt' "
      "placeholder='/web/index_hu.html' required>"
      "<input type='file' name='file' required>"
      "<button type='submit'>Feltöltés</button></form>"
      "<p><a href='/'>Vissza a rádióhoz</a></p></html>";
  server_.send(200, "text/html; charset=utf-8", page);
}

void RadioWebServer::sendStations() {
  String json;
  json.reserve(4096);
  json = "{\"stations\":[";
  const auto& stations = radio_.stations().stations();
  for (size_t index = 0; index < stations.size(); ++index) {
    if (index) json += ',';
    const Station& station = stations[index];
    json += "{\"name\":\"" + jsonEscape(station.name) +
            "\",\"url\":\"" + jsonEscape(station.url) +
            "\",\"logo\":\"" + jsonEscape(station.logoName) +
            "\",\"homepage\":\"" + jsonEscape(station.homepage) + "\"}";
  }
  const AudioSnapshot audio = radio_.audioSnapshot();
  json += "],\"currentIndex\":" + String(radio_.currentIndex());
  json += ",\"volume\":" + String(radio_.volume());
  json += ",\"paused\":" + String(audio.paused ? "true" : "false");
  json += ",\"stationName\":\"" + jsonEscape(audio.stationName) + "\"";
  json += ",\"title\":\"" + jsonEscape(audio.streamTitle) + "\"";
  json += ",\"artist\":\"\",\"codec\":\"" + jsonEscape(audio.codec) + "\"";
  json += ",\"bitrate\":" + String(audio.bitrateKbps) + "}";
  server_.send(200, kJson, json);
}

void RadioWebServer::sendStatus() {
  const AudioSnapshot audio = radio_.audioSnapshot();
  String json = "{\"currentIndex\":" + String(radio_.currentIndex());
  json += ",\"volume\":" + String(radio_.volume());
  json += ",\"paused\":" + String(audio.paused ? "true" : "false");
  json += ",\"stationName\":\"" + jsonEscape(audio.stationName) + "\"";
  json += ",\"artist\":\"\",\"title\":\"" +
          jsonEscape(audio.streamTitle) + "\"";
  json += ",\"codec\":\"" + jsonEscape(audio.codec) + "\"";
  json += ",\"bitrate\":" + String(audio.bitrateKbps);
  json += ",\"status\":\"" + jsonEscape(audio.statusText) + "\"";
  json += ",\"state\":\"" + jsonEscape(audio.stateCode) + "\"";
  json += ",\"initialized\":" +
          String(audio.initialized ? "true" : "false");
  json += ",\"commandQueued\":" +
          String(audio.commandQueued ? "true" : "false");
  json += ",\"connectAttempted\":" +
          String(audio.connectAttempted ? "true" : "false");
  json += ",\"tcpConnected\":" +
          String(audio.tcpConnected ? "true" : "false");
  json += ",\"running\":" + String(audio.running ? "true" : "false");
  json += ",\"connecting\":" +
          String(audio.connecting ? "true" : "false");
  json += ",\"bufferFilled\":" + String(audio.bufferFilledBytes);
  json += ",\"bufferPercent\":" + String(audio.bufferPercent);
  json += ",\"sampleRate\":" + String(audio.sampleRate);
  json += ",\"bitsPerSample\":" + String(audio.bitsPerSample);
  const DiagnosticsSnapshot diagnostics = radio_.diagnostics();
  json += ",\"cpu0\":" + String(diagnostics.cpu0Percent);
  json += ",\"cpu1\":" + String(diagnostics.cpu1Percent);
  json += ",\"cpuValid\":" +
          String(diagnostics.cpuValid ? "true" : "false");
  json += ",\"temperatureC\":" +
          String(diagnostics.temperatureValid ? diagnostics.temperatureC
                                              : -999.0f, 1);
  json += ",\"freeInternalHeap\":" +
          String(diagnostics.freeInternalHeap);
  json += ",\"freePsram\":" + String(diagnostics.freePsram);
  json += ",\"cpuMHz\":" + String(diagnostics.cpuFrequencyMhz);
  json += ",\"logoSource\":\"" +
          jsonEscape(radio_.logos().currentSource()) + "\"";
  json += ",\"logoPath\":\"" + jsonEscape(radio_.logos().currentPath()) +
          "\"";
  json += ",\"logoNeedsBrowserImport\":" +
          String(radio_.logos().needsBrowserImport() ? "true" : "false");
  json += ",\"url\":\"" + jsonEscape(radio_.currentPlayUrl()) + "\"";
  json += ",\"playlist\":" +
          String(radio_.playlistActive() ? "true" : "false");
  json += ",\"trackIndex\":" + String(radio_.playlistIndex());
  json += ",\"trackCount\":" + String(radio_.playlistCount()) + "}";
  server_.send(200, kJson, json);
}

void RadioWebServer::sendBuffer() {
  const size_t total = radio_.bufferSize();
  const size_t filled = radio_.bufferFilled();
  const unsigned percent =
      total ? static_cast<unsigned>((filled * 100U) / total) : 0;
  const String json = "{\"percent\":" + String(percent) +
                      ",\"filled\":" + String(filled) +
                      ",\"free\":" + String(radio_.bufferFree()) +
                      ",\"total\":" + String(total) + "}";
  server_.send(200, kJson, json);
}

void RadioWebServer::sendCurrentLogo() {
  String path = radio_.logos().currentPath();
  if (path.isEmpty() || !LittleFS.exists(path)) {
    path = "/logos/nologo.png";
  }
  if (!LittleFS.exists(path)) {
    server_.send(404, "text/plain; charset=utf-8", "Nincs logó.");
    return;
  }

  String lower = path;
  lower.toLowerCase();
  if (!lower.endsWith(".sr565")) {
    server_.sendHeader("Cache-Control", "no-cache");
    serveFile(path, contentTypeFor(path));
    return;
  }

  File file = LittleFS.open(path, FILE_READ);
  uint8_t sourceHeader[8]{};
  const bool valid =
      file && file.read(sourceHeader, sizeof(sourceHeader)) ==
                  sizeof(sourceHeader) &&
      sourceHeader[0] == 'S' && sourceHeader[1] == 'R' &&
      sourceHeader[2] == '5' && sourceHeader[3] == '7';
  const uint16_t width =
      valid ? static_cast<uint16_t>(sourceHeader[4] |
                                    (sourceHeader[5] << 8))
            : 0;
  const uint16_t height =
      valid ? static_cast<uint16_t>(sourceHeader[6] |
                                    (sourceHeader[7] << 8))
            : 0;
  const size_t sourceBytes = static_cast<size_t>(width) * height * 2;
  if (!valid || width == 0 || height == 0 ||
      file.size() != 8 + sourceBytes) {
    if (file) file.close();
    server_.send(404, "text/plain; charset=utf-8", "Hibás logó.");
    return;
  }

  const size_t rowBytes = (static_cast<size_t>(width) * 3 + 3) & ~3U;
  const size_t imageBytes = rowBytes * height;
  const size_t responseBytes = 54 + imageBytes;
  uint8_t bmpHeader[54]{};
  auto writeLe16 = [](uint8_t* target, uint16_t value) {
    target[0] = static_cast<uint8_t>(value);
    target[1] = static_cast<uint8_t>(value >> 8);
  };
  auto writeLe32 = [](uint8_t* target, uint32_t value) {
    target[0] = static_cast<uint8_t>(value);
    target[1] = static_cast<uint8_t>(value >> 8);
    target[2] = static_cast<uint8_t>(value >> 16);
    target[3] = static_cast<uint8_t>(value >> 24);
  };
  bmpHeader[0] = 'B';
  bmpHeader[1] = 'M';
  writeLe32(bmpHeader + 2, responseBytes);
  writeLe32(bmpHeader + 10, 54);
  writeLe32(bmpHeader + 14, 40);
  writeLe32(bmpHeader + 18, width);
  // Negatív magasság: a fájlban tárolt felső sor marad felül.
  writeLe32(bmpHeader + 22,
            static_cast<uint32_t>(-static_cast<int32_t>(height)));
  writeLe16(bmpHeader + 26, 1);
  writeLe16(bmpHeader + 28, 24);
  writeLe32(bmpHeader + 34, imageBytes);

  server_.sendHeader("Cache-Control", "no-cache");
  server_.setContentLength(responseBytes);
  server_.send(200, "image/bmp", "");
  WiFiClient client = server_.client();
  if (client.write(bmpHeader, sizeof(bmpHeader)) != sizeof(bmpHeader)) {
    file.close();
    return;
  }

  std::unique_ptr<uint8_t[]> sourceRow(new (std::nothrow)
                                           uint8_t[width * 2]);
  std::unique_ptr<uint8_t[]> bmpRow(
      new (std::nothrow) uint8_t[rowBytes]);
  if (!sourceRow || !bmpRow) {
    file.close();
    return;
  }
  for (uint16_t y = 0; y < height && client.connected(); ++y) {
    if (file.read(sourceRow.get(), width * 2) != width * 2) break;
    memset(bmpRow.get(), 0, rowBytes);
    for (uint16_t x = 0; x < width; ++x) {
      // Az SR565 bájtcserélt RGB565, ahogy az LVGL-kijelző is használja.
      const uint16_t pixel =
          static_cast<uint16_t>((sourceRow[x * 2] << 8) |
                                sourceRow[x * 2 + 1]);
      const uint8_t red =
          static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
      const uint8_t green =
          static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
      const uint8_t blue =
          static_cast<uint8_t>((pixel & 0x1F) * 255 / 31);
      bmpRow[x * 3] = blue;
      bmpRow[x * 3 + 1] = green;
      bmpRow[x * 3 + 2] = red;
    }
    if (client.write(bmpRow.get(), rowBytes) != rowBytes) break;
    delay(0);
  }
  file.close();
}

void RadioWebServer::sendBackgrounds() {
  String json;
  json.reserve(1024);
  json = "{\"items\":[";
  bool first = true;

  File dir = LittleFS.open("/backgrounds");
  if (dir && dir.isDirectory()) {
    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String path = entry.path();
        if (path.isEmpty()) path = entry.name();
        path.replace("\\", "/");
        String lower = path;
        lower.toLowerCase();
        if (lower.endsWith(".sr565")) {
          if (!first) json += ',';
          first = false;
          String name = path;
          const int slash = name.lastIndexOf('/');
          if (slash >= 0) name.remove(0, slash + 1);
          json += "{\"name\":\"" + jsonEscape(name) +
                  "\",\"path\":\"" + jsonEscape(path) + "\"}";
        }
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
  }

  json += "],\"current\":\"" + jsonEscape(radio_.backgroundPath()) + "\"}";
  server_.send(200, kJson, json);
}

void RadioWebServer::deleteBackground() {
  String path;
  if (!normalizeFsPath(argument("path"), path)) {
    server_.send(400, kJson,
                 "{\"ok\":false,\"error\":\"invalid path\"}");
    return;
  }

  String lower = path;
  lower.toLowerCase();
  if (!path.startsWith("/backgrounds/") || !lower.endsWith(".sr565")) {
    server_.send(403, kJson,
                 "{\"ok\":false,\"error\":\"background path only\"}");
    return;
  }

  if (radio_.backgroundEnabled() && path == radio_.backgroundPath()) {
    server_.send(409, kJson,
                 "{\"ok\":false,\"error\":\"active background\"}");
    return;
  }

  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    server_.send(404, kJson,
                 "{\"ok\":false,\"error\":\"not found\"}");
    return;
  }
  file.close();

  const bool ok = LittleFS.remove(path);
  Serial.printf("[bg-delete] %s: %s\n", ok ? "torolve" : "hiba",
                path.c_str());
  server_.send(ok ? 200 : 500, kJson,
               ok ? "{\"ok\":true}"
                  : "{\"ok\":false,\"error\":\"delete failed\"}");
}

void RadioWebServer::fsPing() {
  const bool ready = radio_.enterFsMaintenance();
  server_.send(
      ready ? 200 : 503, kJson,
      ready ? "{\"ok\":true,\"filesystem\":\"LittleFS\","
              "\"maintenance\":true}"
            : "{\"ok\":false,\"error\":\"filesystem busy\"}");
}

void RadioWebServer::fsInfo() {
  if (!radio_.enterFsMaintenance()) {
    server_.send(503, kJson,
                 "{\"ok\":false,\"error\":\"filesystem busy\"}");
    return;
  }
  const uint32_t total = static_cast<uint32_t>(LittleFS.totalBytes());
  const uint32_t used = static_cast<uint32_t>(LittleFS.usedBytes());
  const uint32_t free = total > used ? total - used : 0;
  server_.send(200, kJson,
               "{\"ok\":true,\"backend\":\"littlefs\",\"total\":" +
                   String(total) + ",\"used\":" + String(used) +
                   ",\"free\":" + String(free) + "}");
}

void RadioWebServer::fsList() {
  if (!radio_.enterFsMaintenance()) {
    server_.send(503, kJson,
                 "{\"ok\":false,\"error\":\"filesystem busy\"}");
    return;
  }
  // A lista akár sok cache-fájlt is tartalmazhat. Chunkolt válasszal nem kell
  // az egész JSON-t egyszerre a szűk belső heapben tartani.
  server_.sendHeader("Cache-Control", "no-store");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, kJson, "");
  String output;
  output.reserve(2304);
  output = "[";
  bool first = true;
  streamFsDirectory("/", first, 0, output);
  output += "]";
  server_.sendContent(output);
}

void RadioWebServer::streamFsDirectory(const String& directory, bool& first,
                                       uint8_t depth, String& output) {
  if (depth > 16) return;
  File root = LittleFS.open(directory);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  // Some Arduino-ESP32/LittleFS combinations can stall when a child
  // directory is opened while its parent iterator is still open. Collect the
  // child paths first, close the parent, and recurse only afterwards.
  std::vector<String> subdirectories;
  File entry = root.openNextFile();
  while (entry) {
    String path = entry.path();
    if (path.isEmpty()) path = entry.name();
    path.replace("\\", "/");
    if (!path.startsWith("/")) {
      String parent = directory;
      while (parent.endsWith("/") && parent.length() > 1)
        parent.remove(parent.length() - 1);
      path = parent == "/" ? "/" + path : parent + "/" + path;
    }
    while (path.indexOf("//") >= 0) path.replace("//", "/");

    const bool isDirectory = entry.isDirectory();
    const size_t size = isDirectory ? 0 : entry.size();
    const time_t lastWrite = entry.getLastWrite();
    const uint32_t mtime =
        static_cast<uint32_t>(lastWrite > 0 ? lastWrite : 0);
    entry.close();

    String item;
    item.reserve(path.length() * 2 + 64);
    item = first ? "{\"name\":\"" : ",{\"name\":\"";
    first = false;
    item += jsonEscape(path) +
           "\",\"path\":\"" + jsonEscape(path) +
           "\",\"size\":" + String(static_cast<uint32_t>(size)) +
           ",\"dir\":" + String(isDirectory ? "true" : "false") +
           ",\"mtime\":" + String(mtime) + "}";
    output += item;
    if (output.length() >= 2048) {
      server_.sendContent(output);
      output = "";
      delay(0);
    }

    if (isDirectory && path != directory) subdirectories.push_back(path);
    entry = root.openNextFile();
  }
  root.close();

  for (const String& path : subdirectories) {
    streamFsDirectory(path, first, depth + 1, output);
  }
}

void RadioWebServer::fsRead() {
  if (!radio_.enterFsMaintenance()) {
    server_.send(503, kJson,
                 "{\"ok\":false,\"error\":\"filesystem busy\"}");
    return;
  }
  String path;
  if (!normalizeFsPath(argument("path"), path)) {
    server_.send(400, kJson,
                 "{\"ok\":false,\"error\":\"invalid path\"}");
    return;
  }
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    server_.send(404, kJson,
                 "{\"ok\":false,\"error\":\"not found\"}");
    return;
  }
  server_.sendHeader("Content-Disposition",
                     "attachment; filename=\"" + String(file.name()) + "\"");
  const size_t total = file.size();
  server_.setContentLength(total);
  server_.send(200, "application/octet-stream", "");
  WiFiClient client = server_.client();
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[1024]);
  if (!buffer) {
    file.close();
    return;
  }
  size_t remaining = total;
  while (remaining > 0 && client.connected()) {
    const size_t wanted = min<size_t>(remaining, 1024);
    const size_t read = file.read(buffer.get(), wanted);
    if (read == 0) break;
    size_t sent = 0;
    while (sent < read && client.connected()) {
      const size_t written = client.write(buffer.get() + sent, read - sent);
      if (written == 0) {
        delay(1);
        continue;
      }
      sent += written;
    }
    remaining -= read;
    delay(0);
  }
  file.close();
}

void RadioWebServer::fsDelete() {
  if (!radio_.enterFsMaintenance()) {
    server_.send(503, kJson,
                 "{\"ok\":false,\"error\":\"filesystem busy\"}");
    return;
  }
  String path;
  if (!normalizeFsPath(argument("path"), path)) {
    server_.send(400, kJson,
                 "{\"ok\":false,\"error\":\"invalid path\"}");
    return;
  }
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    if (file) file.close();
    server_.send(404, kJson,
                 "{\"ok\":false,\"error\":\"not found\"}");
    return;
  }
  const bool isDirectory = file.isDirectory();
  file.close();
  const bool ok = isDirectory ? removeRecursive(path) : LittleFS.remove(path);
  if (ok && path == "/stations.txt") radio_.reloadStations();
  server_.send(ok ? 200 : 500, kJson,
               ok ? "{\"ok\":true}"
                  : "{\"ok\":false,\"error\":\"delete failed\"}");
}

void RadioWebServer::fsMkdir() {
  if (!radio_.enterFsMaintenance()) {
    server_.send(503, kJson,
                 "{\"ok\":false,\"error\":\"filesystem busy\"}");
    return;
  }
  String path;
  if (!normalizeFsPath(argument("path"), path, true)) {
    server_.send(400, kJson,
                 "{\"ok\":false,\"error\":\"invalid path\"}");
    return;
  }
  const bool ok =
      path == "/" || LittleFS.exists(path) ||
      (ensureParentDirectories(path) && LittleFS.mkdir(path));
  server_.send(ok ? 200 : 500, kJson,
               ok ? "{\"ok\":true}"
                  : "{\"ok\":false,\"error\":\"mkdir failed\"}");
}

void RadioWebServer::fsRmdir() {
  if (!radio_.enterFsMaintenance()) {
    server_.send(503, kJson,
                 "{\"ok\":false,\"error\":\"filesystem busy\"}");
    return;
  }
  String path;
  if (!normalizeFsPath(argument("path"), path)) {
    server_.send(400, kJson,
                 "{\"ok\":false,\"error\":\"invalid path\"}");
    return;
  }
  File file = LittleFS.open(path, FILE_READ);
  if (!file || !file.isDirectory()) {
    if (file) file.close();
    server_.send(404, kJson,
                 "{\"ok\":false,\"error\":\"not found\"}");
    return;
  }
  file.close();
  const bool ok = removeRecursive(path);
  server_.send(ok ? 200 : 500, kJson,
               ok ? "{\"ok\":true}"
                  : "{\"ok\":false,\"error\":\"rmdir failed\"}");
}

void RadioWebServer::fsReboot() {
  server_.send(200, kJson, "{\"ok\":true,\"msg\":\"rebooting\"}");
  restartPending_ = true;
  restartAt_ = millis() + 600;
}

void RadioWebServer::addStation() {
  const String body = server_.arg("plain");
  Station station;
  station.name = argument("name");
  station.url = argument("url");
  station.logoName = normalizeLogo(argument("logo"));
  station.homepage = argument("homepage");
  if (station.name.isEmpty() && !body.isEmpty()) {
    station.name = jsonStringField(body, "name");
    station.url = jsonStringField(body, "url");
    station.logoName = normalizeLogo(jsonStringField(body, "logo"));
    station.homepage = jsonStringField(body, "homepage");
  }
  server_.send(radio_.stations().add(station) ? 200 : 400, kJson,
               "{\"ok\":true}");
}

void RadioWebServer::updateStation() {
  const int index = argument("index").toInt();
  const Station* old = radio_.stations().get(index);
  if (!old) {
    server_.send(404, kJson, "{\"ok\":false}");
    return;
  }
  Station station = *old;
  if (!argument("name").isEmpty()) station.name = argument("name");
  if (!argument("url").isEmpty()) station.url = argument("url");
  if (!argument("logo").isEmpty())
    station.logoName = normalizeLogo(argument("logo"));
  if (server_.hasArg("homepage"))
    station.homepage = argument("homepage");
  server_.send(radio_.stations().update(index, station) ? 200 : 400, kJson,
               "{\"ok\":true}");
}

void RadioWebServer::deleteStation() {
  const int index = argument("index").toInt();
  server_.send(radio_.stations().remove(index) ? 200 : 400, kJson,
               "{\"ok\":true}");
}

void RadioWebServer::moveStation() {
  const String body = server_.arg("plain");
  int from = argument("from").toInt();
  int to = argument("to").toInt();
  if (!body.isEmpty()) {
    from = jsonIntField(body, "oldIndex", from);
    to = jsonIntField(body, "newIndex", to);
  }
  server_.send(radio_.stations().move(from, to) ? 200 : 400, kJson,
               "{\"ok\":true}");
}

void RadioWebServer::uploadFinished() {
  if (uploadFile_) uploadFile_.close();
  if (stationsUploaded_ && !uploadFailed_) {
    radio_.reloadStations();
  }
  const bool ok = !uploadFailed_ && !uploadPath_.isEmpty();
  server_.send(ok ? 200 : 500, kJson,
               ok ? "{\"ok\":true}"
                  : "{\"ok\":false,\"error\":\"write failed\"}");
  uploadPath_ = "";
  uploadFailed_ = false;
  stationsUploaded_ = false;
}

void RadioWebServer::uploadChunk() {
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadFailed_ = false;
    stationsUploaded_ = false;
    String requestedPath = server_.arg("path");
    if (requestedPath.isEmpty()) requestedPath = upload.filename;
    if (!normalizeFsPath(requestedPath, uploadPath_) ||
        !allowedUploadPath(uploadPath_)) {
      uploadPath_ = "";
      uploadFailed_ = true;
      return;
    }
    if (!ensureParentDirectories(uploadPath_)) {
      uploadFailed_ = true;
      return;
    }
    if (LittleFS.exists(uploadPath_)) LittleFS.remove(uploadPath_);
    uploadFile_ = LittleFS.open(uploadPath_, FILE_WRITE);
    uploadFailed_ = !uploadFile_;
    stationsUploaded_ = uploadPath_ == "/stations.txt";
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!uploadFile_ ||
        uploadFile_.write(upload.buf, upload.currentSize) !=
            upload.currentSize) {
      uploadFailed_ = true;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile_) uploadFile_.close();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile_) uploadFile_.close();
    if (!uploadPath_.isEmpty()) LittleFS.remove(uploadPath_);
    uploadFailed_ = true;
  }
}

void RadioWebServer::backgroundUploadFinished() {
  if (backgroundUploadFile_) backgroundUploadFile_.close();
  const bool ok = !backgroundUploadFailed_ && !backgroundUploadPath_.isEmpty();
  if (!ok && !backgroundUploadPath_.isEmpty()) {
    LittleFS.remove(backgroundUploadPath_);
  }
  if (ok) {
    Serial.printf("[bg-upload] kesz: %s (%u byte)\n",
                  backgroundUploadPath_.c_str(),
                  static_cast<unsigned>(backgroundUploadBytes_));
  } else {
    Serial.printf("[bg-upload] hiba: %s\n", backgroundUploadError_.c_str());
  }
  server_.send(ok ? 200 : 400, kJson,
               ok ? "{\"ok\":true,\"path\":\"" +
                        jsonEscape(backgroundUploadPath_) + "\"}"
                  : "{\"ok\":false,\"error\":\"" +
                        jsonEscape(backgroundUploadError_.isEmpty()
                                       ? String("background upload failed")
                                       : backgroundUploadError_) +
                        "\"}");
  backgroundUploadFinalizePending_ = true;
  backgroundUploadPath_ = "";
  backgroundUploadError_ = "";
  backgroundUploadBytes_ = 0;
  backgroundUploadFailed_ = false;
}

void RadioWebServer::backgroundUploadChunk() {
  constexpr size_t kMaximumBackgroundUpload = 480 * 320 * 2 + 8 + 4096;
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (backgroundUploadFile_) {
      backgroundUploadFile_.close();
    }
    backgroundUploadFailed_ = false;
    backgroundUploadError_ = "";
    backgroundUploadBytes_ = 0;
    backgroundUploadFinalizePending_ = false;
    backgroundUploadPath_ = "";
    radio_.beginBackgroundUploadMode();
    String filename = upload.filename;
    filename.replace('\\', '-');
    filename.replace('/', '-');
    if (filename.isEmpty()) filename = "background.sr565";
    if (!filename.endsWith(".sr565")) filename += ".sr565";
    if (!LittleFS.exists("/backgrounds")) LittleFS.mkdir("/backgrounds");
    backgroundUploadPath_ = "/backgrounds/" + filename;
    Serial.printf("[bg-upload] start: %s\n", backgroundUploadPath_.c_str());
    if (LittleFS.exists(backgroundUploadPath_))
      LittleFS.remove(backgroundUploadPath_);
    backgroundUploadFile_ = LittleFS.open(backgroundUploadPath_, FILE_WRITE);
    backgroundUploadFailed_ = !backgroundUploadFile_;
    if (backgroundUploadFailed_) {
      backgroundUploadError_ = "Nem sikerült megnyitni a célfájlt.";
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    backgroundUploadBytes_ += upload.currentSize;
    if (backgroundUploadBytes_ > kMaximumBackgroundUpload) {
      backgroundUploadFailed_ = true;
      backgroundUploadError_ = "A háttérfájl túl nagy.";
    } else if (!backgroundUploadFile_) {
      backgroundUploadFailed_ = true;
      backgroundUploadError_ = "A célfájl nem elérhető íráshoz.";
    } else if (backgroundUploadFile_.write(upload.buf, upload.currentSize) !=
               upload.currentSize) {
      backgroundUploadFailed_ = true;
      backgroundUploadError_ = "Írási hiba a háttérfájl mentése közben.";
    }
  } else if (upload.status == UPLOAD_FILE_END ||
             upload.status == UPLOAD_FILE_ABORTED) {
    if (backgroundUploadFile_) backgroundUploadFile_.close();
    if (upload.status == UPLOAD_FILE_ABORTED) {
      backgroundUploadFailed_ = true;
      if (backgroundUploadError_.isEmpty()) {
        backgroundUploadError_ = "A feltöltési kapcsolat megszakadt.";
      }
      if (!backgroundUploadPath_.isEmpty() && LittleFS.exists(backgroundUploadPath_)) {
        LittleFS.remove(backgroundUploadPath_);
      }
    }
  }
}

void RadioWebServer::logoUploadFinished() {
  if (logoUploadFile_) logoUploadFile_.close();
  if (logoUploadFailed_ || logoUploadPath_.isEmpty()) {
    if (!logoUploadPath_.isEmpty()) LittleFS.remove(logoUploadPath_);
    server_.send(400, kJson,
                 "{\"ok\":false,\"error\":\"Hibás logófájl.\"}");
  } else {
    server_.send(200, kJson,
                 "{\"ok\":true,\"path\":\"" +
                     jsonEscape(logoUploadPath_) + "\"}");
  }
  logoUploadPath_ = "";
  logoUploadBytes_ = 0;
  logoUploadFailed_ = false;
}

void RadioWebServer::logoUploadChunk() {
  constexpr size_t kMaximumLogoUpload = 160 * 1024;
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    logoUploadFailed_ = false;
    logoUploadBytes_ = 0;
    String filename = safeLogoFilename(upload.filename);
    if (filename.isEmpty()) filename = "radio-browser.png";
    if (!filename.endsWith(".png")) filename += ".png";
    if (!LittleFS.exists("/logos")) LittleFS.mkdir("/logos");
    logoUploadPath_ = "/logos/" + filename;
    if (LittleFS.exists(logoUploadPath_)) LittleFS.remove(logoUploadPath_);
    logoUploadFile_ = LittleFS.open(logoUploadPath_, FILE_WRITE);
    logoUploadFailed_ = !logoUploadFile_;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    logoUploadBytes_ += upload.currentSize;
    if (logoUploadBytes_ > kMaximumLogoUpload || !logoUploadFile_ ||
        logoUploadFile_.write(upload.buf, upload.currentSize) !=
            upload.currentSize) {
      logoUploadFailed_ = true;
    }
  } else if (upload.status == UPLOAD_FILE_END ||
             upload.status == UPLOAD_FILE_ABORTED) {
    if (logoUploadFile_) logoUploadFile_.close();
    if (upload.status == UPLOAD_FILE_ABORTED) logoUploadFailed_ = true;
  }
}

void RadioWebServer::artworkCacheFinished() {
  if (artworkUploadFile_) artworkUploadFile_.close();
  bool queued = false;
  if (!artworkUploadFailed_ && !artworkUploadPath_.isEmpty() &&
      !artworkUploadUrl_.isEmpty()) {
    queued =
        radio_.logos().queueBrowserPng(artworkUploadUrl_, artworkUploadPath_);
  }
  if (!queued) {
    if (!artworkUploadPath_.isEmpty()) LittleFS.remove(artworkUploadPath_);
    server_.send(503, kJson,
                 "{\"ok\":false,\"error\":\"A logófeldolgozó foglalt.\"}");
  } else {
    server_.send(202, kJson, "{\"ok\":true}");
  }
  artworkUploadPath_ = "";
  artworkUploadUrl_ = "";
  artworkUploadBytes_ = 0;
  artworkUploadFailed_ = false;
}

void RadioWebServer::artworkCacheChunk() {
  constexpr size_t kMaximumBrowserArtwork = 240 * 1024;
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    purgeCacheForSpace(kMaximumBrowserArtwork + 96 * 1024);
    artworkUploadFailed_ = false;
    artworkUploadBytes_ = 0;
    artworkUploadUrl_ = argument("url");
    Serial.printf("[web] artwork cache upload: %s\n",
                  artworkUploadUrl_.c_str());
    if (!LittleFS.exists("/cache")) LittleFS.mkdir("/cache");
    artworkUploadPath_ =
        "/cache/browser-" + String(static_cast<unsigned long>(esp_random()),
                                   HEX) +
        ".png";
    artworkUploadFile_ = LittleFS.open(artworkUploadPath_, FILE_WRITE);
    artworkUploadFailed_ =
        !artworkUploadFile_ ||
        (!artworkUploadUrl_.startsWith("http://") &&
         !artworkUploadUrl_.startsWith("https://"));
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    artworkUploadBytes_ += upload.currentSize;
    if (artworkUploadBytes_ > kMaximumBrowserArtwork ||
        !artworkUploadFile_ ||
        artworkUploadFile_.write(upload.buf, upload.currentSize) !=
            upload.currentSize) {
      artworkUploadFailed_ = true;
    }
  } else if (upload.status == UPLOAD_FILE_END ||
             upload.status == UPLOAD_FILE_ABORTED) {
    if (artworkUploadFile_) artworkUploadFile_.close();
    if (upload.status == UPLOAD_FILE_ABORTED) artworkUploadFailed_ = true;
  }
}

void RadioWebServer::artworkProxy() {
  const String url = argument("url");
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    server_.send(400, kJson, "{\"ok\":false,\"error\":\"bad url\"}");
    return;
  }

  HTTPClient http;
  NetworkClient plainClient;
  NetworkClientSecure secureClient;
  NetworkClient* client = &plainClient;
  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    client = &secureClient;
  }
  http.setConnectTimeout(4000);
  http.setTimeout(6000);
  http.setUserAgent("LVGL-Radio/1.0 ESP32");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(*client, url)) {
    server_.send(502, kJson, "{\"ok\":false,\"error\":\"begin failed\"}");
    return;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    server_.send(502, kJson, "{\"ok\":false,\"error\":\"fetch failed\"}");
    return;
  }

  constexpr size_t kMaximumProxyArtwork = 240 * 1024;
  const int declaredLength = http.getSize();
  if (declaredLength > 0 &&
      static_cast<size_t>(declaredLength) > kMaximumProxyArtwork) {
    http.end();
    server_.send(413, kJson, "{\"ok\":false,\"error\":\"too large\"}");
    return;
  }

  server_.sendHeader("Cache-Control", "no-store");
  server_.setContentLength(declaredLength > 0
                               ? static_cast<size_t>(declaredLength)
                               : CONTENT_LENGTH_UNKNOWN);
  server_.send(200, contentTypeFor(url), "");

  WiFiClient output = server_.client();
  NetworkClient* input = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t written = 0;
  uint32_t lastDataAt = millis();
  while (output.connected() &&
         (http.connected() || input->available()) &&
         written < kMaximumProxyArtwork) {
    const size_t available = input->available();
    if (!available) {
      if (millis() - lastDataAt > 5000) break;
      delay(1);
      continue;
    }
    const size_t requested =
        min(min(available, sizeof(buffer)), kMaximumProxyArtwork - written);
    const int received = input->readBytes(buffer, requested);
    if (received <= 0) break;
    if (output.write(buffer, received) != static_cast<size_t>(received)) break;
    written += received;
    lastDataAt = millis();
    delay(0);
  }
  http.end();
  Serial.printf("[web] artwork proxy: %u byte, %s\n",
                static_cast<unsigned>(written), url.c_str());
}

void RadioWebServer::notFound() {
  String path = server_.uri();
  if (path.indexOf("..") >= 0) {
    server_.send(400, "text/plain", "Hibás útvonal.");
    return;
  }
  if (LittleFS.exists(path)) {
    serveFile(path, contentTypeFor(path));
  } else if (radio_.wifi().accessPointMode()) {
    server_.sendHeader("Location", "http://" + radio_.wifi().address() + "/",
                       true);
    server_.send(302, "text/plain", "");
  } else {
    server_.send(404, "text/plain; charset=utf-8", "Nincs ilyen oldal.");
  }
}

String RadioWebServer::argument(const String& name) const {
  return server_.hasArg(name) ? server_.arg(name) : String();
}

String RadioWebServer::jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (const char character : value) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<uint8_t>(character) >= 0x20) escaped += character;
    }
  }
  return escaped;
}

String RadioWebServer::jsonStringField(const String& body,
                                       const String& name) {
  const String key = "\"" + name + "\"";
  int start = body.indexOf(key);
  if (start < 0) return "";
  start = body.indexOf(':', start + key.length());
  start = body.indexOf('"', start + 1);
  if (start < 0) return "";
  ++start;
  String value;
  bool escaped = false;
  for (int index = start; index < static_cast<int>(body.length()); ++index) {
    const char character = body[index];
    if (escaped) {
      value += character;
      escaped = false;
    } else if (character == '\\') {
      escaped = true;
    } else if (character == '"') {
      break;
    } else {
      value += character;
    }
  }
  return value;
}

int RadioWebServer::jsonIntField(const String& body, const String& name,
                                 int fallback) {
  const String key = "\"" + name + "\"";
  int start = body.indexOf(key);
  if (start < 0) return fallback;
  start = body.indexOf(':', start + key.length());
  if (start < 0) return fallback;
  return body.substring(start + 1).toInt();
}

String RadioWebServer::contentTypeFor(const String& path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".webp")) return "image/webp";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".webmanifest")) return "application/manifest+json";
  if (path.endsWith(".json")) return kJson;
  return "application/octet-stream";
}

bool RadioWebServer::allowedUploadPath(const String& path) {
  String normalized;
  return normalizeFsPath(path, normalized) && normalized == path;
}

bool RadioWebServer::normalizeFsPath(String input, String& output,
                                     bool allowRoot) {
  input.trim();
  input.replace('\\', '/');
  if (input.isEmpty()) return false;
  if (!input.startsWith("/")) input = "/" + input;
  while (input.indexOf("//") >= 0) input.replace("//", "/");
  while (input.endsWith("/") && input.length() > 1)
    input.remove(input.length() - 1);
  if (input.length() > 255) return false;

  int start = 1;
  while (start <= static_cast<int>(input.length())) {
    int end = input.indexOf('/', start);
    if (end < 0) end = input.length();
    const String part = input.substring(start, end);
    if (part == "." || part == "..") return false;
    for (size_t index = 0; index < part.length(); ++index) {
      if (static_cast<uint8_t>(part[index]) < 0x20) return false;
    }
    start = end + 1;
  }
  if (!allowRoot && input == "/") return false;
  output = input;
  return true;
}

bool RadioWebServer::ensureParentDirectories(const String& path) {
  const int lastSlash = path.lastIndexOf('/');
  if (lastSlash <= 0) return true;
  const String parent = path.substring(0, lastSlash);
  String current;
  int start = 1;
  while (start <= static_cast<int>(parent.length())) {
    int end = parent.indexOf('/', start);
    if (end < 0) end = parent.length();
    const String part = parent.substring(start, end);
    if (!part.isEmpty()) {
      current += "/" + part;
      if (!LittleFS.exists(current) && !LittleFS.mkdir(current))
        return false;
    }
    start = end + 1;
  }
  return true;
}

String RadioWebServer::safeLogoFilename(String filename) {
  filename.replace('\\', '-');
  filename.replace('/', '-');
  String safe;
  safe.reserve(min<size_t>(filename.length(), 64));
  for (size_t index = 0; index < filename.length() && safe.length() < 64;
       ++index) {
    const char character = filename[index];
    if (isalnum(static_cast<unsigned char>(character)) ||
        character == '-' || character == '_' || character == '.') {
      safe += character;
    }
  }
  if (safe == "." || safe == "..") safe = "";
  return safe;
}
