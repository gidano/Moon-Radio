#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

class RadioController;

class RadioWebServer {
 public:
  explicit RadioWebServer(RadioController& radio);

  void begin();
  void loop();

 private:
  void registerRoutes();
  void serveFile(const String& path, const String& contentType);
  void serveHome();
  void serveWifiForm();
  void serveUploadForm();
  void sendStations();
  void sendStatus();
  void sendBuffer();
  void sendCurrentLogo();
  void sendBackgrounds();
  void deleteBackground();
  void fsPing();
  void fsInfo();
  void fsList();
  void fsRead();
  void fsDelete();
  void fsMkdir();
  void fsRmdir();
  void fsReboot();
  void streamFsDirectory(const String& directory, bool& first,
                         uint8_t depth, String& output);
  void addStation();
  void updateStation();
  void deleteStation();
  void moveStation();
  void uploadFinished();
  void uploadChunk();
  void backgroundUploadFinished();
  void backgroundUploadChunk();
  void logoUploadFinished();
  void logoUploadChunk();
  void artworkCacheFinished();
  void artworkCacheChunk();
  void artworkProxy();
  void notFound();

  String argument(const String& name) const;
  static String jsonEscape(const String& value);
  static String jsonStringField(const String& body, const String& name);
  static int jsonIntField(const String& body, const String& name,
                          int fallback);
  static String contentTypeFor(const String& path);
  static bool allowedUploadPath(const String& path);
  static bool normalizeFsPath(String input, String& output,
                              bool allowRoot = false);
  static bool ensureParentDirectories(const String& path);
  static String safeLogoFilename(String filename);

  RadioController& radio_;
  DNSServer dns_;
  WebServer server_{80};
  File uploadFile_;
  File backgroundUploadFile_;
  File logoUploadFile_;
  File artworkUploadFile_;
  String uploadPath_;
  String backgroundUploadPath_;
  String backgroundUploadError_;
  String logoUploadPath_;
  String artworkUploadPath_;
  String artworkUploadUrl_;
  size_t backgroundUploadBytes_{0};
  size_t logoUploadBytes_{0};
  size_t artworkUploadBytes_{0};
  bool backgroundUploadFailed_{false};
  bool logoUploadFailed_{false};
  bool artworkUploadFailed_{false};
  bool uploadFailed_{false};
  bool stationsUploaded_{false};
  bool backgroundUploadFinalizePending_{false};
  bool restartPending_{false};
  bool dnsRunning_{false};
  uint32_t restartAt_{0};
};
