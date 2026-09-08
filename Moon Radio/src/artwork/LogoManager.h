#pragma once

#include <Arduino.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>

class LogoManager {
 public:
  void begin();
  void selectStation(const String& configuredSource,
                     const String& streamUrl,
                     const String& homepage = "",
                     const String& stationName = "");
  void setIcyLogo(const String& rawLogo, const String& streamUrl);
  void setEmbeddedImage(const String& streamUrl,
                        const std::vector<uint32_t>& segments);
  void setAlbumCoversEnabled(bool enabled);
  bool albumCoversEnabled() const;
  void setAlbumTitle(const String& combinedTitle);
  void loop(bool playbackRunning, size_t bufferFilledBytes,
            const String& codec = "", uint32_t bitrateKbps = 0,
            uint8_t bufferPercent = 0);

  String currentPath() const;
  String currentSource() const;
  bool busy() const;
  bool needsBrowserImport() const;
  bool queueBrowserPng(const String& sourceUrl, const String& temporaryPath);
  bool enterMaintenance(uint32_t timeoutMs = 3500);

 private:
  enum class JobKind : uint8_t {
    Remote,
    Embedded,
    Thumbnail,
    BrowserImport,
    RadioBrowser,
    AlbumCover
  };

  struct Job {
    LogoManager* owner{nullptr};
    JobKind kind{JobKind::Remote};
    String source;
    String url;
    String localPath;
    String homepage;
    uint32_t selectionId{0};
    bool conservativeAlbumDownload{false};
    std::vector<uint32_t> segments;
  };

  static void taskEntry(void* parameter);
  bool startJob(JobKind kind, const String& source, const String& url = "",
                const String& localPath = "",
                const std::vector<uint32_t>& segments = {});
  void executeJob(Job& job);
  void finishJob(const String& source, const String& path, bool success,
                 uint32_t selectionId);
  void processResult();
  void refreshSelection();

  static bool isRemote(const String& value);
  static String resolveIcyUrl(String logo, const String& streamUrl);
  static String localLogoPath(String source);
  static String cacheStem(const String& key);
  static String thumbnailPath(const String& key);
  static bool validThumbnail(const String& path);
  static bool findCachedThumbnail(const String& key, String& path);
  static bool downloadRemote(const String& url, const String& key,
                             String& imagePath);
  static bool downloadRadioBrowserLogo(const String& streamUrl,
                                       const String& homepage,
                                       const String& key,
                                       String& imagePath);
  static bool resolveAlbumCoverUrl(const String& combinedTitle,
                                   String& coverUrl);
  static bool downloadAlbumCover(const String& combinedTitle,
                                 const String& key,
                                 String& imagePath,
                                 String& thumbnail,
                                 bool conservativeMode = false);
  static String resolveRelativeUrl(String value, const String& baseUrl);
  static bool downloadAttempt(const String& fetchUrl, const String& key,
                              String& imagePath);
  static bool downloadEmbedded(const String& url,
                               const std::vector<uint32_t>& segments,
                               const String& key, String& imagePath);
  static bool appendHttpRange(const String& url, uint32_t offset,
                              uint32_t length, File& output);
  static bool normalizeEmbedded(const String& temporaryPath,
                                const String& key, String& imagePath);
  static bool normalizePayload(const String& sourcePath, size_t offset,
                               size_t length, const String& key,
                               String& imagePath);
  static bool extractCompressedIco(const String& sourcePath,
                                   const String& key, String& imagePath);
  static String detectImageExtension(const String& path);
  static bool makeThumbnail(const String& imagePath, const String& key,
                            String& thumbnail);
  static void purgeCachedArtwork(const String& key);
  static void compactCache(const String& imagePath,
                           const String& thumbnailPath);
  static void enforceCacheBudget(const String& keepPath = "");
  bool sourceFailed(const String& source) const;
  void markSourceFailed(const String& source);

  mutable SemaphoreHandle_t mutex_{nullptr};
  TaskHandle_t task_{nullptr};
  String stationSource_;
  String streamUrl_;
  String homepage_;
  String stationName_;
  String icySource_;
  String embeddedKey_;
  String embeddedUrl_;
  std::vector<uint32_t> embeddedSegments_;
  String albumTitle_;
  String albumKey_;
  bool albumCoversEnabled_{true};
  bool albumConservativeDownload_{false};
  uint32_t albumRequestedAt_{0};
  uint32_t albumStatusLoggedAt_{0};
  uint32_t albumRetryAfter_{0};
  String pendingAlbumPurgeKey_;
  String selectedSource_;
  String radioBrowserKey_;
  bool radioBrowserLoaded_{false};
  std::vector<String> failedSources_;
  String currentPath_{"/logos/nologo.png"};
  uint32_t selectedAt_{0};
  uint32_t selectionId_{0};
  uint32_t searchStartedAt_{0};
  bool searchFinished_{false};
  uint32_t taskStartedAt_{0};
  String taskLabel_;
  bool taskBusyLogged_{false};

  bool resultReady_{false};
  bool resultSuccess_{false};
  String resultSource_;
  String resultPath_;
  uint32_t resultSelectionId_{0};

  bool browserPending_{false};
  bool maintenance_{false};
  String browserSource_;
  String browserTemporaryPath_;
};
