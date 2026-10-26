#include "SdLibrary.h"
#include <SD.h>
#include <SD_MMC.h>
#include <SPI.h>
#include <driver/gpio.h>
#include "options.h"

#if !(defined(SD_MMC_ENABLED) && SD_MMC_ENABLED) && \
    defined(SDC_CS) && defined(SD_SPIPINS)
#ifndef SD_SPI_HOST
#define SD_SPI_HOST HSPI
#endif
namespace {
SPIClass sdSpi(SD_SPI_HOST);
}
#endif

#if defined(SD_MMC_ENABLED) && SD_MMC_ENABLED
namespace {
constexpr bool sdPin(int pin) {
  return pin == SD_MMC_CLK || pin == SD_MMC_CMD || pin == SD_MMC_D0 ||
         pin == SD_MMC_D1 || pin == SD_MMC_D2 || pin == SD_MMC_D3;
}
static_assert(!sdPin(I2S_DOUT) && !sdPin(I2S_BCLK) && !sdPin(I2S_LRC),
              "SD_MMC and I2S pins must not overlap");
}
#endif

bool SdLibrary::prepareSpiBus() {
#if defined(SD_MMC_ENABLED) && SD_MMC_ENABLED
  // FNK0104S uses the independent native 4-bit SD_MMC peripheral.
  return true;
#elif defined(SDC_CS) && defined(SD_SPIPINS)
  if (spiPrepared_) return true;
  const int pins[] = {SD_SPIPINS};  // SCK, MISO, MOSI, CS
  pinMode(SDC_CS, OUTPUT);
  digitalWrite(SDC_CS, HIGH);
  spiPrepared_ = sdSpi.begin(pins[0], pins[1], pins[2], pins[3]);
  Serial.printf("[sd] SPI bus prepare host=%d SCK=%d MISO=%d MOSI=%d CS=%d: %s\n",
                SD_SPI_HOST, pins[0], pins[1], pins[2], SDC_CS,
                spiPrepared_ ? "OK" : "FAILED");
  return spiPrepared_;
#else
  return true;
#endif
}

bool SdLibrary::normalize(String path, String& result) {
  if (path.isEmpty()) path = "/";
  if (!path.startsWith("/") || path.length() > 512 ||
      path.indexOf('\\') >= 0) return false;
  result = "";
  for (unsigned start = 1; start < path.length();) {
    int end = path.indexOf('/', start);
    if (end < 0) end = path.length();
    String part = path.substring(start, end);
    if (part == ".." || part == ".") return false;
    for (unsigned i = 0; i < part.length(); ++i)
      if (static_cast<uint8_t>(part[i]) < 32) return false;
    if (!part.isEmpty()) result += "/" + part;
    start = end + 1;
  }
  if (result.isEmpty()) result = "/";
  return true;
}

bool SdLibrary::supported(String path) {
  path.toLowerCase();
  const char* extensions[] = {".mp3", ".aac", ".aacp", ".m4a", ".wav",
                             ".flac", ".opus", ".ogg", ".oga", ".vor", ".vorbis"};
  for (const char* extension : extensions)
    if (path.endsWith(extension)) return true;
  return false;
}

bool SdLibrary::mount() {
#if defined(SD_MMC_ENABLED) && SD_MMC_ENABLED
  if (mounted_) return true;
  Serial.printf("[sd] SD_MMC mount CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d\n",
                SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0, SD_MMC_D1, SD_MMC_D2, SD_MMC_D3);
  if (!SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0,
                      SD_MMC_D1, SD_MMC_D2, SD_MMC_D3)) {
    Serial.println("[sd] SD_MMC setPins FAILED");
    return false;
  }
  mounted_ = SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT, 6);
  Serial.printf("[sd] SD_MMC mount=%s\n", mounted_ ? "OK" : "FAILED (card initialization or filesystem mount)");
  return mounted_;
#elif defined(SDC_CS) && defined(SD_SPIPINS)
  if (mounted_) return true;
  const int pins[] = {SD_SPIPINS}; // SCK, MISO, MOSI, CS
  Serial.printf("[sd] SPI mount host=%d SCK=%d MISO=%d MOSI=%d CS=%d target=%lu Hz\n",
                SD_SPI_HOST, pins[0], pins[1], pins[2], SDC_CS,
                static_cast<unsigned long>(SDSPISPEED));
  // Mount runs in the main loop, outside display/touch transactions. Keep
  // every other device on this shared bus deselected during card startup.
#if !DISPLAY_PROFILE_AXS15231B
  if (pins[0] == TFT_SCK && pins[2] == TFT_MOSI) {
    // LovyanGFX configures these outputs through ESP-IDF, so Arduino's
    // peripheral manager does not register them as digitalWrite GPIOs.
    // Use the same GPIO layer; do not reclaim or reinitialize the SPI bus.
    gpio_set_level(static_cast<gpio_num_t>(TFT_CS), 1);
#if TOUCH_ENABLED && TS_MODEL == TS_MODEL_XPT2046
    gpio_set_level(static_cast<gpio_num_t>(TS_CS), 1);
    Serial.printf("[sd] shared CS levels: display=%d touch=%d (both must be 1)\n",
                  gpio_get_level(static_cast<gpio_num_t>(TFT_CS)),
                  gpio_get_level(static_cast<gpio_num_t>(TS_CS)));
#else
    Serial.printf("[sd] shared CS level: display=%d (must be 1)\n",
                  gpio_get_level(static_cast<gpio_num_t>(TFT_CS)));
#endif
  }
#endif
  pinMode(SDC_CS, OUTPUT);
  digitalWrite(SDC_CS, HIGH);
  // The persistent, profile-selected SPI object is prepared before display
  // initialization so LovyanGFX and SD share the same controller and lock.
  if (!prepareSpiBus()) {
    Serial.println("[sd] configured SPI bus is unavailable");
    return false;
  }
  mounted_ = SD.begin(SDC_CS, sdSpi, SDSPISPEED, "/sd", 6, false);
  if (!mounted_ && SDSPISPEED > 1000000) {
    Serial.println("[sd] mount FAILED; retry at 1000000 Hz");
    SD.end();
    mounted_ = SD.begin(SDC_CS, sdSpi, 1000000, "/sd", 6, false);
  }
  if (mounted_) {
    Serial.printf("[sd] mount=OK type=%u size=%llu MiB\n",
                  static_cast<unsigned>(SD.cardType()), SD.cardSize() / (1024ULL * 1024ULL));
  } else {
    Serial.println("[sd] mount=FAILED (card initialization or filesystem mount); radio source unchanged");
  }
  return mounted_;
#else
  Serial.println("[sd] mount FAILED: SD is not configured in options.h");
  return false;
#endif
}

fs::FS& SdLibrary::filesystem() {
#if defined(SD_MMC_ENABLED) && SD_MMC_ENABLED
  return SD_MMC;
#else
  return SD;
#endif
}

bool SdLibrary::list(const String& path, size_t offset,
                     std::vector<Entry>& entries, bool& more) {
  entries.clear();
  more = false;
  String normalized;
  if (!normalize(path, normalized) || !mount()) return false;
  File directory = filesystem().open(normalized);
  if (!directory || !directory.isDirectory()) return false;
  size_t skipped = 0;
  while (File file = directory.openNextFile()) {
    String name = file.name();
    name = name.substring(name.lastIndexOf('/') + 1);
    const bool folder = file.isDirectory();
    if (name.startsWith(".") || (!folder && !supported(name))) continue;
    if (skipped++ < offset) continue;
    if (entries.size() == 64) { more = true; break; }
    entries.push_back({name, (normalized == "/" ? "" : normalized) + "/" + name, folder});
  }
  return true;
}

bool SdLibrary::playable(const String& path) {
  String normalized;
  if (!normalize(path, normalized) || !supported(normalized) || !mount()) return false;
  File file = filesystem().open(normalized);
  return file && !file.isDirectory();
}

String SdLibrary::adjacent(const String& path, int delta) {
  String directory = path.substring(0, path.lastIndexOf('/'));
  if (directory.isEmpty()) directory = "/";
  String candidate, wrap;
  if (!mount()) return "";
  File folder = filesystem().open(directory);
  if (!folder || !folder.isDirectory()) return "";
  while (File file = folder.openNextFile()) {
    if (file.isDirectory()) continue;
    String name = file.name();
    name = name.substring(name.lastIndexOf('/') + 1);
    if (name.startsWith(".") || !supported(name)) continue;
    const String item = (directory == "/" ? String() : directory) + "/" + name;
    if (wrap.isEmpty() || (delta > 0 ? item < wrap : item > wrap)) wrap = item;
    if ((delta > 0 ? item > path : item < path) &&
        (candidate.isEmpty() || (delta > 0 ? item < candidate : item > candidate)))
      candidate = item;
    yield();
  }
  return candidate.isEmpty() ? wrap : candidate;
}
