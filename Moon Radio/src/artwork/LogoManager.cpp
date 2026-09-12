#include "LogoManager.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <LovyanGFX.hpp>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <math.h>
#include <new>

#include "options.h"

void* stbiHeapMalloc(size_t size) {
  void* memory =
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory)
    memory = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return memory;
}

void* stbiHeapRealloc(void* pointer, size_t size) {
  void* memory =
      heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory)
    memory =
        heap_caps_realloc(pointer, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return memory;
}

#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_WEBP
#define STBI_ONLY_BMP
#define STBI_MALLOC(size) stbiHeapMalloc(size)
#define STBI_REALLOC(pointer, size) stbiHeapRealloc((pointer), (size))
#define STBI_FREE(pointer) heap_caps_free(pointer)
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

namespace {

constexpr size_t kMaximumArtworkBytes = 384 * 1024;
constexpr size_t kMaximumEmbeddedSegments = 8;
constexpr size_t kMaximumCacheBytes = 1024 * 1024;
constexpr size_t kTargetCacheBytes = 768 * 1024;
constexpr size_t kMinimumArtworkFreeBytes = 96 * 1024;
constexpr uint16_t kThumbnailSize = 128;
constexpr uint16_t kMaximumDecodeDimension = 512;
constexpr size_t kThumbnailBytes =
    8 + static_cast<size_t>(kThumbnailSize) * kThumbnailSize * 2;
#if DISPLAY_PROFILE_AXS15231B
constexpr uint32_t kArtworkDelayMs = 3000;
#else
constexpr uint32_t kArtworkDelayMs = 5000;
#endif
constexpr uint32_t kNoLogoSearchWindowMs = 20000;
constexpr uint32_t kAlbumStatusLogIntervalMs = 7000;
constexpr uint32_t kMaximumAlbumCoverWaitMs = 35000;
constexpr uint32_t kJobBusyLogMs = 7000;
#if DISPLAY_PROFILE_AXS15231B
constexpr uint32_t kArtworkTaskStackBytes = 16 * 1024;
#else
constexpr uint32_t kArtworkTaskStackBytes = 22 * 1024;
#endif
constexpr size_t kMinimumLogoNetworkBuffer = 192 * 1024;
constexpr size_t kMinimumConfiguredLogoBuffer = 128 * 1024;
constexpr size_t kMinimumAlbumCoverBuffer = 48 * 1024;
constexpr size_t kMinimumSecureAlbumCoverBuffer = 64 * 1024;
constexpr size_t kMinimumLosslessAlbumCoverBuffer = 256 * 1024;
constexpr size_t kMinimumAlbumCoverAbortBuffer = 32 * 1024;
constexpr size_t kMinimumSecureAlbumCoverAbortBuffer = 48 * 1024;
// Sok nagy bitrátájú, de kis hálózati előtöltést engedő adó 60-70 KiB körül
// egyensúlyoz. A kicsinyített borító első, rövid próbájához 56 KiB már ad
// valós indítási ablakot, a 32 KiB-os azonnali visszavonási korlát pedig
// megtartja az audio elsőbbségét.
constexpr size_t kMinimumOpportunisticSecureAlbumCoverBuffer = 56 * 1024;
constexpr size_t kMinimumOpportunisticAlbumCoverBuffer = 48 * 1024;
constexpr size_t kMinimumOpportunisticSecureAlbumAbortBuffer = 32 * 1024;
constexpr size_t kMinimumOpportunisticAlbumAbortBuffer = 24 * 1024;
// Sok 128 kbps-os HTTP adó nem épít 48 KiB fölé, de 36 KiB-on, hosszabb
// megfigyelés után már el lehet kezdeni a nagyon lassított letöltést. A
// 24 KiB-os alsó korlát kb. másfél másodpercnyi MP3 tartalékot hagy.
constexpr size_t kMinimumLowBitrateAlbumCoverBuffer = 36 * 1024;
constexpr size_t kMinimumLowBitrateAlbumAbortBuffer = 24 * 1024;
// HTTPS-nél a TLS kapcsolat rövid, de érezhető pluszterhelése miatt egy
// nagyobb, három másodpercnyi MP3-tartalék maradjon a letöltés indításakor.
constexpr size_t kMinimumSecureLowBitrateAlbumCoverBuffer = 48 * 1024;
constexpr size_t kMinimumSecureLowBitrateAlbumAbortBuffer = 32 * 1024;
constexpr uint8_t kMinimumLosslessAlbumContinuePercent = 12;
constexpr uint32_t kAlbumBufferStabilizationMs = 3000;
constexpr uint32_t kLowBitrateAlbumBufferStabilizationMs = 6000;
constexpr uint32_t kOpportunisticAlbumBufferStabilizationMs = 750;
constexpr uint32_t kAlbumNetworkAbortRetryMs = 15000;
constexpr uint32_t kMaximumAlbumNetworkRetryMs = 60000;
constexpr size_t kMinimumAlbumInternalHeap =
    kArtworkTaskStackBytes + 20 * 1024;
constexpr uint32_t kReadIdleTimeoutMs = 3500;
constexpr size_t kMinimumTextFetchInternalHeap = 20 * 1024;
constexpr size_t kMinimumTextFetchBlock = 7 * 1024;
constexpr size_t kMinimumTextReadInternalHeap = 12 * 1024;
// A teljes kép-letöltés (főleg TLS esetén) több belső munkaterületet használ,
// mint a rövid API-válasz. A valós tesztben azonban a 24/9 KiB kapu a
// 110--160 KiB-os, stabil audio puffer mellett is minden iTunes-borítót
// kizárt: az API-kérés már biztonsággal lefutott, a 100 px-es kép pedig még
// el sem indulhatott. Ezért a HTTPS-kép kapuja az ugyancsak bevált API-kapu
// szintjére kerül. Az audio pufferes, letöltés közbeni azonnali megszakítás
// változatlanul elsőbbséget élvez.
constexpr size_t kMinimumArtworkPlainFetchInternalHeap = 16 * 1024;
constexpr size_t kMinimumArtworkPlainFetchBlock = 6 * 1024;
constexpr size_t kMinimumArtworkSecureFetchInternalHeap = 20 * 1024;
constexpr size_t kMinimumArtworkSecureFetchBlock = 7 * 1024;
#if DISPLAY_PROFILE_AXS15231B
constexpr size_t kMinimumAxsPlainTextFetchInternalHeap = 14 * 1024;
constexpr size_t kMinimumAxsPlainTextFetchBlock = 6 * 1024;
constexpr size_t kMinimumAxsSecureFetchInternalHeap = 21 * 1024;
constexpr size_t kMinimumAxsSecureFetchBlock = 9 * 1024;
constexpr size_t kMinimumAxsLowBitrateAlbumCoverBuffer = 24 * 1024;
constexpr size_t kMinimumAxsLowBitrateAlbumAbortBuffer = 16 * 1024;
#endif
// Az audio dekoder dedikált feladata a 0. magon fut.  A borítókeresés és a
// képdekódolás háttérmunka, ezért kétmagos célokon a másik magra kerül: egy
// lassú HTTP/DNS/TLS művelet így nem késlelteti közvetlenül az audio feladat
// ütemezését. Egy magos ESP32-n természetesen marad a 0. mag.
#if CONFIG_FREERTOS_UNICORE
constexpr BaseType_t kArtworkTaskCore = 0;
#else
constexpr BaseType_t kArtworkTaskCore = 1;
#endif
constexpr UBaseType_t kArtworkTaskPriority = 0;
constexpr char kRadioBrowserBases[][35] = {
    "https://de1.api.radio-browser.info",
    "https://nl1.api.radio-browser.info",
};

volatile uint8_t gAlbumNetworkPoliteLevel = 0;
volatile bool gAlbumNetworkAborted = false;
volatile bool gAlbumResourceDeferred = false;
volatile uint8_t gAlbumProviderStartIndex = 0;
volatile bool gArtworkPlaybackRunning = false;
volatile size_t gArtworkBufferFilledBytes = 0;
volatile uint8_t gArtworkBufferPercent = 0;
volatile uint32_t gArtworkBitrateKbps = 0;
volatile size_t gArtworkAlbumAbortBufferBytes =
    kMinimumAlbumCoverAbortBuffer;
volatile uint8_t gArtworkAlbumAbortBufferPercent = 0;
// Egy állomásváltás közben a korábbi háttérfeladat már nem tölthet le adatot.
// A két érték csak 32 bites, atomi olvasás/írásra használt generációszám.
volatile uint32_t gArtworkSelectionId = 0;
volatile uint32_t gArtworkTaskSelectionId = 0;

struct ImageSize {
  uint16_t width{0};
  uint16_t height{0};
};

struct PsramText {
  char* data{nullptr};
  size_t length{0};
  size_t capacity{0};

  ~PsramText() { clearStorage(); }

  PsramText() = default;
  PsramText(const PsramText&) = delete;
  PsramText& operator=(const PsramText&) = delete;

  void clear() {
    length = 0;
    if (data) data[0] = '\0';
  }

  void clearStorage() {
    if (data) heap_caps_free(data);
    data = nullptr;
    length = 0;
    capacity = 0;
  }

  bool reserve(size_t requested) {
    if (requested + 1 <= capacity) return true;
    size_t nextCapacity = capacity ? capacity : 2048;
    while (nextCapacity < requested + 1) nextCapacity *= 2;
    char* next = static_cast<char*>(
        heap_caps_malloc(nextCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!next) {
      next = static_cast<char*>(
          heap_caps_malloc(nextCapacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!next) return false;
    if (data && length) memcpy(next, data, length);
    if (data) heap_caps_free(data);
    data = next;
    capacity = nextCapacity;
    data[length] = '\0';
    return true;
  }

  bool append(const uint8_t* bytes, size_t count) {
    if (!bytes || !count) return true;
    if (!reserve(length + count)) return false;
    memcpy(data + length, bytes, count);
    length += count;
    data[length] = '\0';
    return true;
  }

  bool empty() const { return length == 0; }
  const char* c_str() const { return data ? data : ""; }
};

struct PsramBytes {
  uint8_t* data{nullptr};
  size_t length{0};

  ~PsramBytes() { clear(); }

  PsramBytes() = default;
  PsramBytes(const PsramBytes&) = delete;
  PsramBytes& operator=(const PsramBytes&) = delete;

  void clear() {
    if (data) heap_caps_free(data);
    data = nullptr;
    length = 0;
  }

  bool resize(size_t requested) {
    clear();
    if (!requested) return true;
    data = static_cast<uint8_t*>(
        heap_caps_malloc(requested, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data && requested <= 64 * 1024) {
      data = static_cast<uint8_t*>(
          heap_caps_malloc(requested, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!data) return false;
    length = requested;
    return true;
  }

  size_t size() const { return length; }
};

struct ScopedAlbumNetworkPoliteMode {
  explicit ScopedAlbumNetworkPoliteMode(uint8_t level)
      : previous(gAlbumNetworkPoliteLevel) {
    if (level > gAlbumNetworkPoliteLevel) gAlbumNetworkPoliteLevel = level;
  }

  ~ScopedAlbumNetworkPoliteMode() { gAlbumNetworkPoliteLevel = previous; }

  uint8_t previous;
};

bool losslessOrOggStream(String codec) {
  codec.toUpperCase();
  return codec.indexOf("FLAC") >= 0 || codec.indexOf("OGG") >= 0 ||
         codec.indexOf("VORBIS") >= 0;
}

bool recognizedCompressedStream(String codec) {
  codec.toUpperCase();
  return codec.indexOf("MP3") >= 0 || codec.indexOf("MPEG") >= 0 ||
         codec.indexOf("AAC") >= 0 || codec.indexOf("M4A") >= 0 ||
         codec.indexOf("OPUS") >= 0;
}

size_t artworkNetworkChunkLimit(size_t normalBytes) {
  if (gAlbumNetworkPoliteLevel >= 2) return min<size_t>(normalBytes, 128);
  if (gAlbumNetworkPoliteLevel == 1) return min<size_t>(normalBytes, 192);
  return normalBytes;
}

uint32_t artworkNetworkDelayMs() {
  if (gAlbumNetworkPoliteLevel >= 2) {
    const uint8_t percent = gArtworkBufferPercent;
    if (!percent || percent >= 80) return 8;
    if (percent >= 60) return 15;
    if (percent >= 40) return 25;
    if (percent >= 20) return 45;
    return 60;
  }
  if (gAlbumNetworkPoliteLevel == 1) {
    const uint8_t percent = gArtworkBufferPercent;
    if (!percent || percent >= 80) return 6;
    if (percent >= 60) return 10;
    if (percent >= 40) return 18;
    if (percent >= 25) return 30;
    return 45;
  }
  return 1;
}

bool artworkNetworkShouldAbort() {
  if (gArtworkTaskSelectionId != 0 &&
      gArtworkTaskSelectionId != gArtworkSelectionId)
    return true;
  if (!gArtworkPlaybackRunning || !gAlbumNetworkPoliteLevel) return false;
  return gArtworkBufferFilledBytes > 0 &&
         ((gArtworkAlbumAbortBufferPercent > 0 &&
           gArtworkBufferPercent > 0 &&
           gArtworkBufferPercent < gArtworkAlbumAbortBufferPercent) ||
          (gArtworkAlbumAbortBufferPercent == 0 &&
           gArtworkBufferFilledBytes < gArtworkAlbumAbortBufferBytes));
}

uint32_t fnv1a(const String& value) {
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < value.length(); ++index) {
    hash ^= static_cast<uint8_t>(value[index]);
    hash *= 16777619UL;
  }
  return hash;
}

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

bool readBe32(File& file, uint32_t& value) {
  uint8_t bytes[4]{};
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) return false;
  value = (static_cast<uint32_t>(bytes[0]) << 24) |
          (static_cast<uint32_t>(bytes[1]) << 16) |
          (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
  return true;
}

uint16_t readBe16(File& file) {
  uint8_t bytes[2]{};
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) return 0;
  return (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
}

bool readPngSize(const String& path, ImageSize& size) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) return false;
  uint8_t header[24]{};
  const bool valid =
      file.read(header, sizeof(header)) == sizeof(header) &&
      header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' &&
      header[3] == 'G';
  file.close();
  if (!valid) return false;
  const uint32_t width = (static_cast<uint32_t>(header[16]) << 24) |
                         (static_cast<uint32_t>(header[17]) << 16) |
                         (static_cast<uint32_t>(header[18]) << 8) | header[19];
  const uint32_t height = (static_cast<uint32_t>(header[20]) << 24) |
                          (static_cast<uint32_t>(header[21]) << 16) |
                          (static_cast<uint32_t>(header[22]) << 8) | header[23];
  if (!width || !height || width > UINT16_MAX || height > UINT16_MAX)
    return false;
  size.width = width;
  size.height = height;
  return true;
}

bool readJpegSize(const String& path, ImageSize& size) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || readBe16(file) != 0xFFD8) {
    if (file) file.close();
    return false;
  }
  while (file.available()) {
    if (file.read() != 0xFF) continue;
    uint8_t marker = file.read();
    while (marker == 0xFF && file.available()) marker = file.read();
    if (marker == 0xD9 || marker == 0xDA) break;
    const uint16_t segmentLength = readBe16(file);
    if (segmentLength < 2) break;
    const bool startOfFrame =
        (marker >= 0xC0 && marker <= 0xC3) ||
        (marker >= 0xC5 && marker <= 0xC7) ||
        (marker >= 0xC9 && marker <= 0xCB) ||
        (marker >= 0xCD && marker <= 0xCF);
    if (startOfFrame) {
      file.read();
      size.height = readBe16(file);
      size.width = readBe16(file);
      file.close();
      return size.width && size.height;
    }
    file.seek(file.position() + segmentLength - 2);
  }
  file.close();
  return false;
}

bool readBmpSize(const String& path, ImageSize& size) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) return false;
  uint8_t header[26]{};
  const bool valid =
      file.read(header, sizeof(header)) == sizeof(header) &&
      header[0] == 'B' && header[1] == 'M';
  file.close();
  if (!valid) return false;
  int32_t width = static_cast<int32_t>(readLe32(header + 18));
  int32_t height = static_cast<int32_t>(readLe32(header + 22));
  if (height < 0) height = -height;
  if (width <= 0 || height <= 0 || width > UINT16_MAX ||
      height > UINT16_MAX)
    return false;
  size.width = width;
  size.height = height;
  return true;
}

[[maybe_unused]] bool readImageSize(const String& path, ImageSize& size) {
  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".png")) return readPngSize(path, size);
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg"))
    return readJpegSize(path, size);
  if (lower.endsWith(".bmp")) return readBmpSize(path, size);
  if (lower.endsWith(".webp")) {
    File file = LittleFS.open(path, FILE_READ);
    if (!file) return false;
    const size_t length = file.size();
    if (!length || length > kMaximumArtworkBytes) {
      file.close();
      return false;
    }
    PsramBytes bytes;
    if (!bytes.resize(length)) {
      file.close();
      return false;
    }
    const size_t received = file.read(bytes.data, length);
    file.close();
    if (received != length) return false;
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(bytes.data, static_cast<int>(bytes.size()),
                               &width, &height, &channels))
      return false;
    if (width <= 0 || height <= 0 || width > UINT16_MAX ||
        height > UINT16_MAX)
      return false;
    size.width = static_cast<uint16_t>(width);
    size.height = static_cast<uint16_t>(height);
    return true;
  }
  return false;
}

bool hasVisiblePixels(const uint8_t* pixels, size_t pixelCount) {
  if (!pixels || !pixelCount) return false;
  size_t visible = 0;
  for (size_t index = 0; index < pixelCount; ++index) {
    const size_t offset = index * 2;
    if (pixels[offset] != 0 || pixels[offset + 1] != 0) {
      ++visible;
      if (visible >= 64) return true;
    }
  }
  return false;
}

bool loadFileBytes(const String& path, PsramBytes& bytes) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) return false;
  const size_t length = file.size();
  if (!length || length > kMaximumArtworkBytes) {
    file.close();
    return false;
  }
  if (!bytes.resize(length)) {
    file.close();
    return false;
  }
  const size_t received = file.read(bytes.data, length);
  file.close();
  return received == length;
}

[[maybe_unused]] bool decodeStbSprite(const String& imagePath,
                                      lgfx::LGFX_Sprite& decoded,
                                      int32_t decodedWidth,
                                      int32_t decodedHeight) {
  PsramBytes bytes;
  if (!loadFileBytes(imagePath, bytes)) return false;

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* rgba = stbi_load_from_memory(bytes.data,
                                        static_cast<int>(bytes.size()), &width,
                                        &height, &channels, 4);
  if (!rgba || width <= 0 || height <= 0) {
    if (rgba) stbi_image_free(rgba);
    return false;
  }

  uint16_t* pixels = static_cast<uint16_t*>(decoded.getBuffer());
  if (!pixels) {
    stbi_image_free(rgba);
    return false;
  }

  for (int32_t y = 0; y < decodedHeight; ++y) {
    const int srcY = static_cast<int>((static_cast<int64_t>(y) * height) /
                                      decodedHeight);
    for (int32_t x = 0; x < decodedWidth; ++x) {
      const int srcX = static_cast<int>((static_cast<int64_t>(x) * width) /
                                        decodedWidth);
      const uint8_t* src =
          rgba + ((static_cast<size_t>(srcY) * width) + srcX) * 4;
      const uint32_t alpha = src[3];
      uint16_t color = 0x0000;
      if (alpha) {
        uint32_t r = src[0];
        uint32_t g = src[1];
        uint32_t b = src[2];
        if (alpha < 255) {
          r = (r * alpha) / 255;
          g = (g * alpha) / 255;
          b = (b * alpha) / 255;
        }
        color = lgfx::swap565(static_cast<uint8_t>(r),
                              static_cast<uint8_t>(g),
                              static_cast<uint8_t>(b));
      }
      pixels[static_cast<size_t>(y) * decodedWidth + x] = color;
    }
  }

  stbi_image_free(rgba);
  return true;
}

String urlEncode(String value) {
  String encoded;
  encoded.reserve(value.length() * 3);
  static const char hex[] = "0123456789ABCDEF";
  for (size_t index = 0; index < value.length(); ++index) {
    const uint8_t ch = static_cast<uint8_t>(value[index]);
    const bool unreserved =
        (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~';
    if (unreserved) {
      encoded += static_cast<char>(ch);
    } else if (ch == ' ') {
      encoded += '+';
    } else {
      encoded += '%';
      encoded += hex[ch >> 4];
      encoded += hex[ch & 0x0F];
    }
  }
  return encoded;
}

int findInText(const char* text, size_t length, const char* needle,
               size_t startAt = 0) {
  if (!text || !needle || startAt >= length) return -1;
  const size_t needleLength = strlen(needle);
  if (!needleLength || needleLength > length - startAt) return -1;
  for (size_t index = startAt; index + needleLength <= length; ++index) {
    if (memcmp(text + index, needle, needleLength) == 0)
      return static_cast<int>(index);
  }
  return -1;
}

bool fetchText(const String& fetchUrl, PsramText& body) {
  constexpr size_t kMaximumTextResponseBytes = 48 * 1024;
  constexpr uint32_t kTextReadIdleTimeoutMs = 4500;
  const bool secureFetch = fetchUrl.startsWith("https://");
#if DISPLAY_PROFILE_AXS15231B
  const size_t requiredHeap = secureFetch ? kMinimumAxsSecureFetchInternalHeap
                                          : kMinimumAxsPlainTextFetchInternalHeap;
  const size_t requiredBlock = secureFetch ? kMinimumAxsSecureFetchBlock
                                           : kMinimumAxsPlainTextFetchBlock;
#else
  const size_t requiredHeap = kMinimumTextFetchInternalHeap;
  const size_t requiredBlock = kMinimumTextFetchBlock;
#endif
  const size_t freeInternalHeap =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largestInternalBlock =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (freeInternalHeap < requiredHeap || largestInternalBlock < requiredBlock) {
    Serial.printf(
        "[cover] API keresés kihagyva: keves belso heap %u/%u byte, blokk=%u/%u%s\n",
                  static_cast<unsigned>(freeInternalHeap),
                  static_cast<unsigned>(requiredHeap),
                  static_cast<unsigned>(largestInternalBlock),
                  static_cast<unsigned>(requiredBlock),
                  secureFetch ? " HTTPS" : "");
    gAlbumResourceDeferred = true;
    return false;
  }
  // A kereső API-k válaszai is ugyanazon a Wi-Fi kapcsolaton érkeznek, mint
  // az adás. Ne csak a kép fájlának olvasásakor, hanem már a keresés előtt is
  // engedjük vissza azonnal a sávszélt az audiónak.
  if (artworkNetworkShouldAbort()) {
    gAlbumNetworkAborted = true;
    gAlbumResourceDeferred = true;
    Serial.printf("[cover] API keresés megszakítva: puffer %u%% %u byte\n",
                  static_cast<unsigned>(gArtworkBufferPercent),
                  static_cast<unsigned>(gArtworkBufferFilledBytes));
    return false;
  }
  std::unique_ptr<NetworkClientSecure> secureClient;
  NetworkClient plainClient;
  NetworkClient* client = &plainClient;
  if (secureFetch) {
    secureClient.reset(new (std::nothrow) NetworkClientSecure());
    if (!secureClient) return false;
    secureClient->setInsecure();
    client = secureClient.get();
  }
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  http.setUserAgent("LVGL-Radio/1.0 ESP32");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(*client, fetchUrl)) return false;
  Serial.printf("[cover] HTTP keresés: %s\n", fetchUrl.c_str());
  const int code = http.GET();
  if (artworkNetworkShouldAbort()) {
    gAlbumNetworkAborted = true;
    gAlbumResourceDeferred = true;
    Serial.printf("[cover] API keresés megszakítva: puffer %u%% %u byte\n",
                  static_cast<unsigned>(gArtworkBufferPercent),
                  static_cast<unsigned>(gArtworkBufferFilledBytes));
    http.end();
    return false;
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("[cover] HTTP hiba %d: %s\n", code, fetchUrl.c_str());
    http.end();
    return false;
  }
  const int declaredLength = http.getSize();
  if (declaredLength > 0 &&
      static_cast<size_t>(declaredLength) > kMaximumTextResponseBytes) {
    Serial.printf("[cover] tul nagy API valasz: %d byte\n", declaredLength);
    http.end();
    return false;
  }

  body.clear();
  const size_t reserveBytes =
      declaredLength > 0 ? min<size_t>(declaredLength, 4096) : 2048;
  if (!body.reserve(reserveBytes)) {
    Serial.println("[cover] API valasz buffer foglalasi hiba");
    http.end();
    return false;
  }
  NetworkClient* stream = http.getStreamPtr();
  if (!stream) {
    Serial.println("[cover] API valasz stream hiba");
    http.end();
    return false;
  }
  uint8_t buffer[512];
  size_t receivedTotal = 0;
  uint32_t lastReadAt = millis();
  while ((http.connected() || stream->available()) &&
         (declaredLength < 0 ||
          receivedTotal < static_cast<size_t>(declaredLength))) {
    if (artworkNetworkShouldAbort()) {
      gAlbumNetworkAborted = true;
      gAlbumResourceDeferred = true;
      Serial.printf("[cover] API keresés megszakítva: puffer %u%% %u byte\n",
                    static_cast<unsigned>(gArtworkBufferPercent),
                    static_cast<unsigned>(gArtworkBufferFilledBytes));
      body.clear();
      break;
    }
    const size_t available = stream->available();
    if (!available) {
      if (millis() - lastReadAt > kTextReadIdleTimeoutMs) {
        Serial.println("[cover] API valasz olvasasi timeout");
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(artworkNetworkDelayMs()));
      continue;
    }
    size_t requested = min(available, artworkNetworkChunkLimit(sizeof(buffer)));
    if (declaredLength > 0) {
      requested =
          min(requested,
              static_cast<size_t>(declaredLength) - receivedTotal);
    }
    const int received = stream->readBytes(buffer, requested);
    if (received <= 0) break;
    if (receivedTotal + static_cast<size_t>(received) >
        kMaximumTextResponseBytes) {
      Serial.println("[cover] API valasz limit tulcsordult");
      body.clear();
      break;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        kMinimumTextReadInternalHeap) {
      Serial.printf("[cover] API olvasas leallitva: keves belso heap %u/%u byte\n",
                    static_cast<unsigned>(heap_caps_get_free_size(
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                    static_cast<unsigned>(kMinimumTextReadInternalHeap));
      gAlbumResourceDeferred = true;
      body.clear();
      break;
    }
    if (!body.append(buffer, static_cast<size_t>(received))) {
      Serial.println("[cover] API valasz buffer bovitesi hiba");
      body.clear();
      break;
    }
    receivedTotal += static_cast<size_t>(received);
    lastReadAt = millis();
    vTaskDelay(pdMS_TO_TICKS(artworkNetworkDelayMs()));
  }
  http.end();
  if (declaredLength > 0 &&
      receivedTotal != static_cast<size_t>(declaredLength)) {
    Serial.printf("[cover] rovid API valasz: %u/%d byte\n",
                  static_cast<unsigned>(receivedTotal), declaredLength);
  }
  return !body.empty();
}

String jsonUnescape(String value) {
  value.replace("\\\\", "\\");
  value.replace("\\/", "/");
  value.replace("\\\"", "\"");
  value.replace("\\n", "\n");
  value.replace("\\r", "\r");
  value.replace("\\t", "\t");
  return value;
}

String extractJsonStringField(const char* json, size_t jsonLength,
                              const char* fieldName, size_t startAt = 0) {
  char needle[80];
  snprintf(needle, sizeof(needle), "\"%s\"", fieldName);
  size_t searchFrom = 0;
  searchFrom = startAt;
  while (true) {
    const int index = findInText(json, jsonLength, needle, searchFrom);
    if (index < 0) return "";
    size_t cursor = static_cast<size_t>(index) + strlen(needle);
    while (cursor < jsonLength &&
           isspace(static_cast<unsigned char>(json[cursor]))) {
      ++cursor;
    }
    if (cursor >= jsonLength || json[cursor] != ':') {
      searchFrom = static_cast<size_t>(index) + strlen(needle);
      continue;
    }
    ++cursor;
    while (cursor < jsonLength &&
           isspace(static_cast<unsigned char>(json[cursor]))) {
      ++cursor;
    }
    if (cursor >= jsonLength) return "";
    if (json[cursor] == 'n') {
      return "";
    }
    if (json[cursor] != '"') {
      searchFrom = cursor + 1;
      continue;
    }
    ++cursor;
    String value;
    bool escaped = false;
    while (cursor < jsonLength) {
      const char character = json[cursor++];
      if (escaped) {
        value += character;
        escaped = false;
        continue;
      }
      if (character == '\\') {
        escaped = true;
        continue;
      }
      if (character == '"') return jsonUnescape(value);
      value += character;
    }
    return "";
  }
}

void collectJsonStringFields(const char* json, size_t jsonLength,
                             const char* fieldName,
                             std::vector<String>& values,
                             uint8_t maximum = 8) {
  char needle[80];
  snprintf(needle, sizeof(needle), "\"%s\"", fieldName);
  size_t searchFrom = 0;
  while (values.size() < maximum) {
    const int index = findInText(json, jsonLength, needle, searchFrom);
    if (index < 0) return;
    const String value =
        extractJsonStringField(json, jsonLength, fieldName, index);
    if (!value.isEmpty()) {
      bool duplicate = false;
      for (const String& existing : values) {
        if (existing == value) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) values.push_back(value);
    }
    searchFrom = static_cast<size_t>(index) + strlen(needle);
  }
}

void appendUnique(std::vector<String>& values, const String& candidate) {
  if (candidate.isEmpty()) return;
  for (const String& existing : values) {
    if (existing == candidate) return;
  }
  values.push_back(candidate);
}

String upgradedItunesArtwork(String url) {
  url.trim();
  if (url.isEmpty()) return "";
  url.replace("100x100", "120x120");
  url.replace("60x60", "120x120");
  return url;
}

String lighterAlbumArtwork(String url) {
  url.trim();
  if (url.isEmpty()) return "";
  url.replace("/300x300/", "/64s/");
  url.replace("/174s/", "/64s/");
  url.replace("60x60", "120x120");
  url.replace("100x100", "120x120");
  url.replace("170x170", "120x120");
  url.replace("250x250", "120x120");
  url.replace("500x500", "120x120");
  url.replace("300x300", "170x170");
  url.replace("600x600", "170x170");
  // A 120 px-es iTunes-borító már közel van a 128 px-es rádió-bélyegképhez,
  // ezért sokkal tisztább marad a megjelenése. A nagyobb képeket viszont
  // továbbra is szűkítjük, hogy a hálózati feladat rövid maradjon.
  if (gAlbumNetworkPoliteLevel >= 2) {
    url.replace("170x170", "100x100");
  }
  return url;
}

String albumArtworkDownloadUrl(String url, bool conservativeMode) {
  url.trim();
  if (conservativeMode &&
      url.startsWith("https://lastfm-img.freetls.fastly.net/"))
    return "http://" + url.substring(8);
  return url;
}

String radioBrowserFaviconForStream(const String& streamUrl) {
  const std::vector<String> endpoints = {
      streamUrl,
  };

  for (const String& base : endpoints) {
    if (base.isEmpty()) continue;
    for (const char* host : kRadioBrowserBases) {
      const String url = String(host) + "/json/stations/byurl?url=" +
                         urlEncode(base);
      PsramText json;
      if (!fetchText(url, json)) continue;
      const String favicon =
          extractJsonStringField(json.c_str(), json.length, "favicon");
      if (!favicon.isEmpty()) return favicon;
    }
  }

  return "";
}

bool isValidMbid(const String& value) {
  if (value.length() != 36) return false;
  for (size_t index = 0; index < 36; ++index) {
    const bool separator =
        index == 8 || index == 13 || index == 18 || index == 23;
    const char ch = value[index];
    if (separator) {
      if (ch != '-') return false;
    } else if (!isxdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

String normalizedCoverPart(String value) {
  value.trim();
  value.toLowerCase();
  String normalized;
  normalized.reserve(value.length());
  bool pendingSpace = false;
  for (size_t index = 0; index < value.length(); ++index) {
    const char ch = value[index];
    if (isspace(static_cast<unsigned char>(ch))) {
      pendingSpace = normalized.length() > 0;
      continue;
    }
    if (pendingSpace) normalized += ' ';
    pendingSpace = false;
    normalized += ch;
  }
  return normalized;
}

bool splitCombinedTitle(const String& combined, String& artist, String& title) {
  const int separator = combined.indexOf(" - ");
  if (separator <= 0) return false;
  artist = combined.substring(0, separator);
  title = combined.substring(separator + 3);
  artist.trim();
  title.trim();
  String normalizedArtist = artist;
  normalizedArtist.replace(" ", "");
  normalizedArtist.toLowerCase();
  if (normalizedArtist == "acdc") artist = "AC/DC";
  if (artist.isEmpty() || title.isEmpty()) return false;

  const char* blocked[] = {
      "unknown", "not provided", "n/a", "untitled", "no title"};
  String lowerTitle = title;
  lowerTitle.toLowerCase();
  for (const char* value : blocked) {
    if (lowerTitle == value) return false;
  }
  return true;
}

String albumCoverKey(const String& artist, const String& title) {
  return "album:" + normalizedCoverPart(artist) + "\x1f" +
         normalizedCoverPart(title);
}

String extractFieldAfter(const PsramText& json, const char* marker,
                         const char* fieldName, size_t startAt = 0) {
  const int markerAt =
      !marker || !marker[0] ? static_cast<int>(startAt)
                            : findInText(json.c_str(), json.length, marker,
                                         startAt);
  if (markerAt < 0) return "";
  return extractJsonStringField(json.c_str(), json.length, fieldName,
                                static_cast<size_t>(markerAt));
}

bool extractFirstReleaseGroupMbid(const PsramText& json, String& mbid,
                                  size_t startAt) {
  size_t cursor = startAt;
  while (cursor < json.length) {
    const int rgAt =
        findInText(json.c_str(), json.length, "\"release-group\"", cursor);
    if (rgAt < 0) return false;
    const String candidate =
        extractFieldAfter(json, "", "id", static_cast<size_t>(rgAt));
    if (isValidMbid(candidate)) {
      mbid = candidate;
      return true;
    }
    cursor = static_cast<size_t>(rgAt) + 15;
  }
  return false;
}

bool findMusicBrainzReleaseGroupCovers(const String& artist,
                                       const String& title,
                                       std::vector<String>& coverUrls) {
  coverUrls.clear();
  static uint32_t lastMusicBrainzAt = 0;
  const uint32_t now = millis();
  if (lastMusicBrainzAt && now - lastMusicBrainzAt < 1100) {
    vTaskDelay(pdMS_TO_TICKS(1100 - (now - lastMusicBrainzAt)));
  }
  lastMusicBrainzAt = millis();

  String query = "recording:\"";
  query += title;
  query += "\" AND artist:\"";
  query += artist;
  query += "\"";

  PsramText json;
  const uint8_t resultLimit = gAlbumNetworkPoliteLevel >= 2 ? 1 : 5;
  String url = "http://musicbrainz.org/ws/2/recording/?fmt=json&limit=" +
               String(resultLimit) + "&query=" + urlEncode(query);
  if (!fetchText(url, json)) {
    Serial.println("[cover] MusicBrainz keresés HTTP hiba");
    return false;
  }

  size_t cursor = 0;
  for (uint8_t attempts = 0; attempts < 6; ++attempts) {
    String mbid;
    if (!extractFirstReleaseGroupMbid(json, mbid, cursor)) break;
    const int mbidAt = findInText(json.c_str(), json.length, mbid.c_str(),
                                  cursor);
    cursor = (mbidAt >= 0 ? static_cast<size_t>(mbidAt) : cursor) +
             mbid.length();
    bool duplicate = false;
    for (const String& coverUrl : coverUrls) {
      if (coverUrl.indexOf(mbid) >= 0) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      coverUrls.push_back("http://coverartarchive.org/release-group/" + mbid +
                          "/front-250");
    }
  }
  if (coverUrls.empty()) {
    Serial.println("[cover] MusicBrainz nem adott release-group jeloltet");
  }
  return !coverUrls.empty();
}

bool findLastFmReleaseCover(const String& artist, const String& title,
                            String& coverUrl) {
#if defined(USE_LASTFM_COVER) && defined(LASTFM_API_KEY)
  String url =
      "http://ws.audioscrobbler.com/2.0/?method=track.getInfo&autocorrect=1&format=json&api_key=";
  url += LASTFM_API_KEY;
  url += "&artist=";
  url += urlEncode(artist);
  url += "&track=";
  url += urlEncode(title);

  PsramText json;
  if (!fetchText(url, json)) return false;
  const int albumAt = findInText(json.c_str(), json.length, "\"album\"");
  const String mbid = extractJsonStringField(
      json.c_str(), json.length, "mbid", albumAt >= 0 ? albumAt : 0);
  if (!isValidMbid(mbid)) return false;
  coverUrl = "http://coverartarchive.org/release/" + mbid + "/front-250";
  return true;
#else
  (void)artist;
  (void)title;
  (void)coverUrl;
  return false;
#endif
}

bool findLastFmCoverCandidates(const String& artist, const String& title,
                               std::vector<String>& coverUrls) {
#if defined(USE_LASTFM_COVER) && defined(LASTFM_API_KEY)
  String url =
      "http://ws.audioscrobbler.com/2.0/?method=track.getInfo&autocorrect=1&format=json&api_key=";
  url += LASTFM_API_KEY;
  url += "&artist=";
  url += urlEncode(artist);
  url += "&track=";
  url += urlEncode(title);

  PsramText json;
  if (!fetchText(url, json)) {
    Serial.println("[cover] Last.fm HTTP hiba");
    return false;
  }

  std::vector<String> images;
  collectJsonStringFields(json.c_str(), json.length, "#text", images, 10);
  for (int index = static_cast<int>(images.size()) - 1; index >= 0; --index) {
    if (images[index].startsWith("http://") ||
        images[index].startsWith("https://")) {
      appendUnique(coverUrls, images[index]);
      break;
    }
  }

  const int albumAt = findInText(json.c_str(), json.length, "\"album\"");
  const String mbid = extractJsonStringField(
      json.c_str(), json.length, "mbid", albumAt >= 0 ? albumAt : 0);
  if (isValidMbid(mbid)) {
    appendUnique(coverUrls,
                 "http://coverartarchive.org/release/" + mbid + "/front-250");
    appendUnique(coverUrls,
                 "http://coverartarchive.org/release/" + mbid + "/front");
  }

  Serial.printf("[cover] Last.fm jeloltek: %u\n",
                static_cast<unsigned>(coverUrls.size()));
  return !coverUrls.empty();
#else
  (void)artist;
  (void)title;
  (void)coverUrls;
  return false;
#endif
}

bool findItunesCoverCandidates(const String& artist, const String& title,
                               std::vector<String>& coverUrls) {
  const String query = artist + " " + title;
  const bool compactSearch = gAlbumNetworkPoliteLevel >= 2;
  const String url =
      "https://itunes.apple.com/search?media=music&entity=song&limit=" +
      String(compactSearch ? 1 : 5) + "&term=" + urlEncode(query);
  PsramText json;
  if (!fetchText(url, json)) {
    Serial.println("[cover] iTunes HTTP hiba");
    return false;
  }
  std::vector<String> images;
  collectJsonStringFields(json.c_str(), json.length, "artworkUrl100", images,
                          compactSearch ? 1 : 8);
  const size_t before = coverUrls.size();
  for (const String& image : images) {
    appendUnique(coverUrls, upgradedItunesArtwork(image));
  }
  Serial.printf("[cover] iTunes jeloltek: %u\n",
                static_cast<unsigned>(coverUrls.size() - before));
  return coverUrls.size() > before;
}

bool findDeezerCoverCandidates(const String& artist, const String& title,
                               std::vector<String>& coverUrls) {
  String query = "artist:\"";
  query += artist;
  query += "\" track:\"";
  query += title;
  query += "\"";
  const bool compactSearch = gAlbumNetworkPoliteLevel >= 2;
  const String url = "https://api.deezer.com/search/track?limit=" +
                     String(compactSearch ? 1 : 5) + "&q=" +
                     urlEncode(query);
  PsramText json;
  if (!fetchText(url, json)) {
    Serial.println("[cover] Deezer HTTP hiba");
    return false;
  }
  const size_t before = coverUrls.size();
  std::vector<String> images;
  // Korlátozott módban a kisebb Deezer-borító az első jelölt; normál módban
  // marad a korábbi, részletesebb sorrend.
  if (compactSearch) {
    collectJsonStringFields(json.c_str(), json.length, "cover_small", images,
                            1);
  } else {
    collectJsonStringFields(json.c_str(), json.length, "cover_medium", images,
                            8);
    collectJsonStringFields(json.c_str(), json.length, "cover_small", images,
                            8);
  }
  for (const String& image : images) appendUnique(coverUrls, image);
  Serial.printf("[cover] Deezer jeloltek: %u\n",
                static_cast<unsigned>(coverUrls.size() - before));
  return coverUrls.size() > before;
}

bool copyFileRange(const String& sourcePath, size_t offset, size_t length,
                   const String& destinationPath) {
  File source = LittleFS.open(sourcePath, FILE_READ);
  if (!source || offset > source.size() || length > source.size() - offset) {
    if (source) source.close();
    return false;
  }
  if (LittleFS.exists(destinationPath)) LittleFS.remove(destinationPath);
  File output = LittleFS.open(destinationPath, FILE_WRITE);
  if (!output || !source.seek(offset)) {
    if (output) output.close();
    source.close();
    return false;
  }
  uint8_t buffer[1024];
  size_t remaining = length;
  while (remaining) {
    const size_t requested = min(remaining, sizeof(buffer));
    const int received = source.read(buffer, requested);
    if (received <= 0 ||
        output.write(buffer, received) != static_cast<size_t>(received))
      break;
    remaining -= received;
    vTaskDelay(1);
  }
  output.flush();
  output.close();
  source.close();
  if (remaining) LittleFS.remove(destinationPath);
  return remaining == 0;
}

size_t littleFsFreeBytes() {
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

void purgeCacheForSpace(size_t requiredBytes, const String& keepPath = "") {
  if (!LittleFS.exists("/cache")) LittleFS.mkdir("/cache");
  if (littleFsFreeBytes() >= requiredBytes) return;

  File root = LittleFS.open("/cache");
  if (!root || !root.isDirectory()) return;

  for (File file = root.openNextFile(); file && littleFsFreeBytes() < requiredBytes;
       file = root.openNextFile()) {
    String path = file.path();
    const size_t size = file.size();
    file.close();
    if (path == keepPath) continue;
    if (!path.startsWith("/cache/")) continue;
    if (LittleFS.remove(path)) {
      Serial.printf("[logo] cache helyfelszabaditas: %s (%u byte)\n",
                    path.c_str(), static_cast<unsigned>(size));
    }
    delay(0);
  }
  root.close();
}

}  // namespace

bool LogoManager::downloadRadioBrowserLogo(const String& streamUrl,
                                           const String& homepage,
                                           const String& key,
                                           String& imagePath) {
  const String favicon = radioBrowserFaviconForStream(streamUrl);
  if (favicon.isEmpty()) return false;

  String resolved = favicon;
  if (!isRemote(resolved)) {
    const String base = isRemote(homepage) ? homepage : streamUrl;
    resolved = resolveRelativeUrl(resolved, base);
  }
  if (!isRemote(resolved)) return false;

  Serial.printf("[logo] radio-browser: %s\n", resolved.c_str());
  return downloadRemote(resolved, key, imagePath);
}

bool LogoManager::resolveAlbumCoverUrl(const String& combinedTitle,
                                       String& coverUrl) {
  coverUrl = "";
  String artist;
  String title;
  if (!splitCombinedTitle(combinedTitle, artist, title)) return false;

  if (findLastFmReleaseCover(artist, title, coverUrl)) {
    Serial.printf("[cover] Last.fm release MBID: %s - %s\n",
                  artist.c_str(), title.c_str());
    return true;
  }
  std::vector<String> coverUrls;
  if (findMusicBrainzReleaseGroupCovers(artist, title, coverUrls) &&
      !coverUrls.empty()) {
    coverUrl = coverUrls.front();
    Serial.printf("[cover] MusicBrainz release-group: %s - %s\n",
                  artist.c_str(), title.c_str());
    return true;
  }
  return false;
}

bool LogoManager::downloadAlbumCover(const String& combinedTitle,
                                     const String& key,
                                     String& imagePath,
                                     String& thumbnail,
                                     bool conservativeMode) {
  ScopedAlbumNetworkPoliteMode politeNetwork(conservativeMode ? 2 : 1);
  gAlbumNetworkAborted = false;
  gAlbumResourceDeferred = false;
  String artist;
  String title;
  if (!splitCombinedTitle(combinedTitle, artist, title)) return false;

  auto tryCover = [&](const String& coverUrl) {
    if (coverUrl.isEmpty()) return false;
    const String effectiveUrl =
        albumArtworkDownloadUrl(
            lighterAlbumArtwork(coverUrl),
            conservativeMode);
    Serial.printf("[cover] jelolt proba: %s\n", effectiveUrl.c_str());
    const bool downloaded = conservativeMode
                                ? downloadAttempt(effectiveUrl, key, imagePath)
                                : downloadRemote(effectiveUrl, key, imagePath);
    const bool success =
        downloaded && makeThumbnail(imagePath, key, thumbnail);
    Serial.printf("[cover] jelolt %s\n", success ? "OK" : "nem jo");
    return success;
  };

  auto tryCandidates = [&](std::vector<String>& coverUrls) {
    size_t attempted = 0;
    for (const String& candidateUrl : coverUrls) {
      if (conservativeMode && attempted++ >= 1) break;
      if (tryCover(candidateUrl)) return true;
      if (gAlbumResourceDeferred || gAlbumNetworkAborted) return false;
    }
    return false;
  };

  std::vector<String> coverUrls;
  const uint8_t providerStart = gAlbumProviderStartIndex % 4;
  // Korlátozott módban először a rövid iTunes-választ és a kis, közvetlen
  // borítót kérjük. Így egy dalhoz nem kell előbb több, lassú kereső és
  // Cover Art Archive-hívás hálózati költségét megfizetni.
  constexpr uint8_t kCompactProviderOrder[] = {2, 3, 0, 1};
  for (uint8_t pass = 0; pass < 4; ++pass) {
    const uint8_t provider = conservativeMode
                                 ? kCompactProviderOrder[(providerStart + pass) % 4]
                                 : (providerStart + pass) % 4;
    coverUrls.clear();

    switch (provider) {
      case 0:
        Serial.printf("[cover] 1/5 Last.fm: %s - %s\n", artist.c_str(),
                      title.c_str());
        if (findLastFmCoverCandidates(artist, title, coverUrls) &&
            tryCandidates(coverUrls))
          return true;
        break;

      case 1:
        Serial.println("[cover] 2/5 MusicBrainz + Cover Art Archive");
        if (findMusicBrainzReleaseGroupCovers(artist, title, coverUrls)) {
          std::vector<String> expanded = coverUrls;
          for (const String& candidateUrl : coverUrls) {
            appendUnique(expanded,
                         candidateUrl.endsWith("/front-250")
                             ? candidateUrl.substring(0,
                                                      candidateUrl.length() - 4)
                             : candidateUrl);
          }
          coverUrls = expanded;
          if (tryCandidates(coverUrls)) return true;
        }
        break;

      case 2:
        Serial.println("[cover] 3/5 iTunes Search");
        if (findItunesCoverCandidates(artist, title, coverUrls) &&
            tryCandidates(coverUrls))
          return true;
        break;

      case 3:
        Serial.println("[cover] 4/5 Deezer Search");
        if (findDeezerCoverCandidates(artist, title, coverUrls) &&
            tryCandidates(coverUrls))
          return true;
        break;
    }

    if (gAlbumResourceDeferred || gAlbumNetworkAborted) return false;
  }

  Serial.println("[cover] 5/5 nincs tobb jelolt");
  return false;
}

void LogoManager::begin() {
  if (!LittleFS.exists("/cache")) LittleFS.mkdir("/cache");
  mutex_ = xSemaphoreCreateMutex();
  enforceCacheBudget();
  refreshSelection();
  Serial.println("[logo] cache kesz");
}

void LogoManager::selectStation(const String& configuredSource,
                                const String& streamUrl,
                                const String& homepage,
                                const String& stationName) {
  stationSource_ = configuredSource;
  stationSource_.trim();
  if (stationSource_.isEmpty()) stationSource_ = "nologo";
  streamUrl_ = streamUrl;
  homepage_ = homepage;
  homepage_.trim();
  stationName_ = stationName;
  stationName_.trim();
  Serial.printf("[logo] allomas: source=%s stream=%s\n",
                stationSource_.c_str(), streamUrl_.c_str());
  icySource_ = "";
  embeddedKey_ = "";
  embeddedUrl_ = "";
  embeddedSegments_.clear();
  albumTitle_ = "";
  albumKey_ = "";
  albumRequestedAt_ = 0;
  albumStatusLoggedAt_ = 0;
  albumRetryAfter_ = 0;
  albumBufferStableAt_ = 0;
  pendingAlbumPurgeKey_ = "";
  radioBrowserLoaded_ = false;
  radioBrowserKey_ = stationName_;
  if (radioBrowserKey_.isEmpty()) radioBrowserKey_ = stationSource_;
  if (!streamUrl_.isEmpty()) {
    if (!radioBrowserKey_.isEmpty()) radioBrowserKey_ += "|";
    radioBrowserKey_ += streamUrl_;
  }
  if (!homepage_.isEmpty()) {
    if (!radioBrowserKey_.isEmpty()) radioBrowserKey_ += "|";
    radioBrowserKey_ += homepage_;
  }
  failedSources_.clear();
  ++selectionId_;
  gArtworkSelectionId = selectionId_;
  searchStartedAt_ = millis();
  searchFinished_ = false;
  String noLogoThumbnail;
  currentPath_ = findCachedThumbnail("nologo", noLogoThumbnail)
                     ? noLogoThumbnail
                     : String("/logos/nologo.png");
  refreshSelection();
}

void LogoManager::setIcyLogo(const String& rawLogo,
                             const String& streamUrl) {
  const String resolved = resolveIcyUrl(rawLogo, streamUrl);
  if (resolved.isEmpty() || resolved == icySource_) return;
  icySource_ = resolved;
  refreshSelection();
  Serial.printf("[logo] icy-logo: %s\n", icySource_.c_str());
}

void LogoManager::setEmbeddedImage(
    const String& streamUrl, const std::vector<uint32_t>& segments) {
  if (streamUrl.isEmpty() || segments.size() < 2 ||
      segments.size() % 2 != 0 ||
      segments.size() / 2 > kMaximumEmbeddedSegments)
    return;
  size_t total = 0;
  for (size_t index = 0; index + 1 < segments.size(); index += 2) {
    if (!segments[index + 1]) return;
    total += segments[index + 1];
    if (total > kMaximumArtworkBytes) return;
  }
  String key = "embedded:" + streamUrl;
  for (size_t index = 0; index + 1 < segments.size(); index += 2) {
    key += ":" + String(segments[index]) + "+" + String(segments[index + 1]);
  }
  if (key == embeddedKey_) return;
  embeddedKey_ = key;
  embeddedUrl_ = streamUrl;
  embeddedSegments_ = segments;
  refreshSelection();
  Serial.printf("[logo] beagyazott kep: %u reszlet, %u byte\n",
                static_cast<unsigned>(segments.size() / 2),
                static_cast<unsigned>(total));
}

void LogoManager::setAlbumCoversEnabled(bool enabled) {
  if (albumCoversEnabled_ == enabled) return;
  albumCoversEnabled_ = enabled;
  if (albumCoversEnabled_) return;

  const String previousAlbumKey = albumKey_;
  if (selectedSource_ == previousAlbumKey) {
    selectedSource_ = "";
  }
  albumTitle_ = "";
  albumKey_ = "";
  albumRequestedAt_ = 0;
  albumStatusLoggedAt_ = 0;
  albumBufferStableAt_ = 0;
  if (!previousAlbumKey.isEmpty()) {
    if (task_)
      pendingAlbumPurgeKey_ = previousAlbumKey;
    else
      purgeCachedArtwork(previousAlbumKey);
  }
  refreshSelection();
  Serial.println("[cover] album borito kereses kikapcsolva");
}

bool LogoManager::albumCoversEnabled() const { return albumCoversEnabled_; }

void LogoManager::setAlbumTitle(const String& combinedTitle) {
#if defined(USE_LASTFM_COVER) && defined(LASTFM_API_KEY)
  if (!albumCoversEnabled_) {
    if (!albumKey_.isEmpty()) {
      const String previousAlbumKey = albumKey_;
      if (selectedSource_ == previousAlbumKey) selectedSource_ = "";
      albumTitle_ = "";
      albumKey_ = "";
      albumRequestedAt_ = 0;
      albumStatusLoggedAt_ = 0;
      albumRetryAfter_ = 0;
      albumBufferStableAt_ = 0;
      if (task_)
        pendingAlbumPurgeKey_ = previousAlbumKey;
      else
        purgeCachedArtwork(previousAlbumKey);
      refreshSelection();
    }
    return;
  }

  String artist;
  String title;
  if (!splitCombinedTitle(combinedTitle, artist, title)) {
    if (!albumKey_.isEmpty()) {
      const String previousAlbumKey = albumKey_;
      albumTitle_ = "";
      albumKey_ = "";
      albumRequestedAt_ = 0;
      albumStatusLoggedAt_ = 0;
      albumRetryAfter_ = 0;
      albumBufferStableAt_ = 0;
      if (task_ && previousAlbumKey.startsWith("album:"))
        pendingAlbumPurgeKey_ = previousAlbumKey;
      else
        purgeCachedArtwork(previousAlbumKey);
      refreshSelection();
    }
    return;
  }

  const String key = albumCoverKey(artist, title);
  if (key == albumKey_) return;
  const String previousAlbumKey = albumKey_;
  if (selectedSource_ == previousAlbumKey) {
    selectedSource_ = "";
  }
  if (!previousAlbumKey.isEmpty()) {
    if (task_)
      pendingAlbumPurgeKey_ = previousAlbumKey;
    else
      purgeCachedArtwork(previousAlbumKey);
  }
  albumTitle_ = artist + " - " + title;
  albumKey_ = key;
  albumRequestedAt_ = millis();
  albumStatusLoggedAt_ = 0;
  albumRetryAfter_ = 0;
  albumBufferStableAt_ = 0;
  albumDeferredRetries_ = 0;
  if (selectedSource_.isEmpty()) refreshSelection();
  Serial.printf("[cover] uj cim: %s\n", albumTitle_.c_str());
#else
  (void)combinedTitle;
#endif
}

void LogoManager::loop(bool playbackRunning, size_t bufferFilledBytes,
                       const String& codec, uint32_t bitrateKbps,
                       uint8_t bufferPercent) {
  gArtworkPlaybackRunning = playbackRunning;
  gArtworkBufferFilledBytes = bufferFilledBytes;
  gArtworkBufferPercent = bufferPercent;
  gArtworkBitrateKbps = bitrateKbps;
  processResult();

  if (currentPath_.isEmpty() || !LittleFS.exists(currentPath_)) {
    String noLogoThumbnail;
    currentPath_ = findCachedThumbnail("nologo", noLogoThumbnail)
                       ? noLogoThumbnail
                       : String("/logos/nologo.png");
  }

  if (maintenance_) return;
  if (task_) {
    if (!taskBusyLogged_ && taskStartedAt_ &&
        millis() - taskStartedAt_ >= kJobBusyLogMs) {
      Serial.printf("[logo] varakozas: %s meg fut (%u ms)\n",
                    taskLabel_.c_str(),
                    static_cast<unsigned>(millis() - taskStartedAt_));
      taskBusyLogged_ = true;
    }
    return;
  }
  if (browserPending_) {
    const String source = browserSource_;
    const String path = browserTemporaryPath_;
    browserPending_ = false;
    startJob(JobKind::BrowserImport, source, "", path);
    return;
  }
  if ((stationSource_ == "nologo" || stationSource_.isEmpty()) &&
      LittleFS.exists("/logos/nologo.png")) {
    String noLogoThumbnail;
    if (findCachedThumbnail("nologo", noLogoThumbnail)) {
      if (currentPath_ == "/logos/nologo.png") currentPath_ = noLogoThumbnail;
    } else {
      startJob(JobKind::Thumbnail, "nologo", "", "/logos/nologo.png");
      return;
    }
  }

  const bool configuredStationSource =
      stationSource_ != "nologo" && !stationSource_.isEmpty();
  const bool selectedConfiguredSource =
      configuredStationSource && selectedSource_ == stationSource_;
  const bool safeForNetwork =
      !playbackRunning ||
      bufferFilledBytes >= (configuredStationSource
                                ? kMinimumConfiguredLogoBuffer
                                : kMinimumLogoNetworkBuffer);
  const bool secureAudioStream = streamUrl_.startsWith("https://");
  const bool losslessOrOgg = losslessOrOggStream(codec);
  const bool conservativeAlbumDownload = true;
  albumConservativeDownload_ = conservativeAlbumDownload;
  size_t albumBufferTarget =
      losslessOrOgg ? kMinimumLosslessAlbumCoverBuffer
                    : (secureAudioStream ? kMinimumSecureAlbumCoverBuffer
                                         : kMinimumAlbumCoverBuffer);
  const bool highBitrateCompressed =
      !losslessOrOgg && bitrateKbps >= 192;
  const bool bitrateNotReportedCompressed =
      !losslessOrOgg && bitrateKbps == 0 &&
      recognizedCompressedStream(codec);
  const bool opportunisticCompressed =
      highBitrateCompressed || bitrateNotReportedCompressed;
  const bool lowBitrateCompressed =
      !losslessOrOgg && bitrateKbps > 0 && bitrateKbps <= 160;
  uint32_t albumBufferStabilizationMs = kAlbumBufferStabilizationMs;
  size_t albumAbortBuffer = secureAudioStream
                                ? kMinimumSecureAlbumCoverAbortBuffer
                                : kMinimumAlbumCoverAbortBuffer;
  uint8_t albumAbortPercent = 0;
  if (losslessOrOgg) {
    // A veszteségmentes streamek nagy frame-jeihez maradjon meg a korábbi,
    // százalékos biztonsági tartalék.
    albumAbortPercent = kMinimumLosslessAlbumContinuePercent;
  } else if (opportunisticCompressed) {
    // Egyes nagy bitrátájú vagy bitrate-fejléc nélküli streamek nem építik
    // fel a klasszikus 64 KiB-os előtöltést. A gyors, kis borítóút induljon a
    // ténylegesen elérhető 48/56 KiB-os ablakban, de 24/32 KiB alatt azonnal
    // engedje vissza a hálózatot az audiónak.
    albumBufferTarget = secureAudioStream
                            ? kMinimumOpportunisticSecureAlbumCoverBuffer
                            : kMinimumOpportunisticAlbumCoverBuffer;
    albumAbortBuffer = secureAudioStream
                           ? kMinimumOpportunisticSecureAlbumAbortBuffer
                           : kMinimumOpportunisticAlbumAbortBuffer;
    albumBufferStabilizationMs = kOpportunisticAlbumBufferStabilizationMs;
  }
  if (lowBitrateCompressed) {
    albumBufferTarget = secureAudioStream
                            ? kMinimumSecureLowBitrateAlbumCoverBuffer
                            : kMinimumLowBitrateAlbumCoverBuffer;
    albumAbortBuffer = secureAudioStream
                           ? kMinimumSecureLowBitrateAlbumAbortBuffer
                           : kMinimumLowBitrateAlbumAbortBuffer;
    albumBufferStabilizationMs = kLowBitrateAlbumBufferStabilizationMs;
  }
#if DISPLAY_PROFILE_AXS15231B
  if (lowBitrateCompressed) {
    albumBufferTarget = kMinimumAxsLowBitrateAlbumCoverBuffer;
    albumAbortBuffer = kMinimumAxsLowBitrateAlbumAbortBuffer;
  }
#endif
  gArtworkAlbumAbortBufferBytes = albumAbortBuffer;
  gArtworkAlbumAbortBufferPercent = albumAbortPercent;
  const bool safeForAlbumCover =
      !playbackRunning || bufferFilledBytes >= albumBufferTarget;
  if (!playbackRunning) {
    albumBufferStableAt_ = 0;
  } else if (safeForAlbumCover) {
    if (!albumBufferStableAt_) albumBufferStableAt_ = millis();
  } else {
    albumBufferStableAt_ = 0;
  }
  const bool albumBufferStable =
      !playbackRunning ||
      (albumBufferStableAt_ &&
       millis() - albumBufferStableAt_ >= albumBufferStabilizationMs);

  auto tryAlbumCoverJob = [&]() -> bool {
    if (!albumCoversEnabled_) return false;
    if (albumKey_.isEmpty() || sourceFailed(albumKey_)) return false;
    String cachedAlbum;
    if (findCachedThumbnail(albumKey_, cachedAlbum)) {
      selectedSource_ = albumKey_;
      currentPath_ = cachedAlbum;
      searchFinished_ = true;
      return true;
    }

    const uint32_t now = millis();
    if (albumRetryAfter_ &&
        static_cast<int32_t>(now - albumRetryAfter_) < 0) {
      if (!albumStatusLoggedAt_ ||
          now - albumStatusLoggedAt_ >= kAlbumStatusLogIntervalMs) {
        Serial.printf("[cover] varakozas ujraprobara: %u%% %u byte\n",
                      static_cast<unsigned>(bufferPercent),
                      static_cast<unsigned>(bufferFilledBytes));
        albumStatusLoggedAt_ = now;
      }
      return false;
    }
    albumRetryAfter_ = 0;
    if (now - albumRequestedAt_ < kArtworkDelayMs) {
      if (!albumStatusLoggedAt_ ||
          now - albumStatusLoggedAt_ >= kAlbumStatusLogIntervalMs) {
        Serial.printf("[cover] varakozas indulashoz: %s\n",
                      albumTitle_.c_str());
        albumStatusLoggedAt_ = now;
      }
      return false;
    } else if (!safeForAlbumCover || !albumBufferStable) {
      if (now - albumRequestedAt_ >= kMaximumAlbumCoverWaitMs) {
        Serial.printf(
            "[cover] kihagyva: nincs eleg stabil puffer %u/%u byte %u%%: %s\n",
                      static_cast<unsigned>(bufferFilledBytes),
                      static_cast<unsigned>(albumBufferTarget),
                      static_cast<unsigned>(bufferPercent),
                      albumTitle_.c_str());
        albumRequestedAt_ = now;
        albumStatusLoggedAt_ = now;
        return false;
      }
      if (!albumStatusLoggedAt_ ||
          now - albumStatusLoggedAt_ >= kAlbumStatusLogIntervalMs) {
        if (safeForAlbumCover) {
          Serial.printf("[cover] puffer stabilizalas: %u/%u byte %u%% (%u/%u ms)\n",
                        static_cast<unsigned>(bufferFilledBytes),
                        static_cast<unsigned>(albumBufferTarget),
                        static_cast<unsigned>(bufferPercent),
                        static_cast<unsigned>(now - albumBufferStableAt_),
                        static_cast<unsigned>(albumBufferStabilizationMs));
        } else {
          Serial.printf("[cover] varakozas pufferre: %u/%u byte %u%%\n",
                        static_cast<unsigned>(bufferFilledBytes),
                        static_cast<unsigned>(albumBufferTarget),
                        static_cast<unsigned>(bufferPercent));
        }
        albumStatusLoggedAt_ = now;
      }
      return false;
    } else if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                       MALLOC_CAP_8BIT) <
               kMinimumAlbumInternalHeap) {
      if (now - albumRequestedAt_ >= kMaximumAlbumCoverWaitMs) {
        Serial.printf("[cover] kihagyva: keves belso heap %u/%u byte: %s\n",
                      static_cast<unsigned>(heap_caps_get_free_size(
                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(kMinimumAlbumInternalHeap),
                      albumTitle_.c_str());
        albumRequestedAt_ = now;
        albumStatusLoggedAt_ = now;
        return false;
      }
      if (!albumStatusLoggedAt_ ||
          now - albumStatusLoggedAt_ >= kAlbumStatusLogIntervalMs) {
        Serial.printf("[cover] varakozas belso heapre: %u/%u byte\n",
                      static_cast<unsigned>(heap_caps_get_free_size(
                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(kMinimumAlbumInternalHeap));
        albumStatusLoggedAt_ = now;
      }
      return false;
    } else {
      return startJob(JobKind::AlbumCover, albumKey_, albumTitle_);
    }
  };

  // A valid local assignment is an immediately drawable station-logo base.
  const String configuredLocal = localLogoPath(stationSource_);
  if (selectedSource_ != albumKey_ && stationSource_ != "nologo" &&
      !configuredLocal.isEmpty() &&
      LittleFS.exists(configuredLocal)) {
    selectedSource_ = stationSource_;
    String configuredThumbnail;
    if (findCachedThumbnail(stationSource_, configuredThumbnail)) {
      currentPath_ = configuredThumbnail;
      if (tryAlbumCoverJob()) return;
      return;
    }
    if (currentPath_ != configuredLocal) currentPath_ = configuredLocal;
    startJob(JobKind::Thumbnail, stationSource_, "", configuredLocal);
    return;
  }

  String cached;
  if (!(stationSource_ == "nologo" && selectedSource_ != "nologo" &&
        millis() - selectedAt_ < kArtworkDelayMs) &&
      findCachedThumbnail(selectedSource_, cached)) {
    if (currentPath_ != cached) currentPath_ = cached;
    if (selectedSource_ != albumKey_ && tryAlbumCoverJob()) return;
    return;
  }

  if (!selectedConfiguredSource &&
      (searchFinished_ ||
       millis() - searchStartedAt_ >= kNoLogoSearchWindowMs)) {
    searchFinished_ = true;
    if (tryAlbumCoverJob()) return;
    return;
  }

  if (selectedSource_ == embeddedKey_ && !embeddedKey_.isEmpty()) {
    if (millis() - selectedAt_ < kArtworkDelayMs) return;
    if (safeForNetwork)
      startJob(JobKind::Embedded, embeddedKey_, embeddedUrl_, "",
               embeddedSegments_);
    return;
  }

  if (isRemote(selectedSource_)) {
    if (sourceFailed(selectedSource_)) return;
    if (!selectedConfiguredSource && millis() - selectedAt_ < kArtworkDelayMs)
      return;
    if (safeForNetwork)
      startJob(JobKind::Remote, selectedSource_, selectedSource_);
    return;
  }

  const String local = localLogoPath(selectedSource_);
  if (selectedSource_ != "nologo" && !local.isEmpty() &&
      LittleFS.exists(local)) {
    // Local logos need no network safety delay. Convert them immediately to
    // the RGB565/PSRAM thumbnail so LVGL does not keep decoding an ARGB PNG
    // while the radio is trying to establish the audio connection.
    startJob(JobKind::Thumbnail, selectedSource_, "", local);
    return;
  }

  if (!radioBrowserKey_.isEmpty() && !radioBrowserLoaded_ &&
      !sourceFailed(radioBrowserKey_)) {
    if (millis() - selectedAt_ < kArtworkDelayMs) return;
    if (safeForNetwork)
      startJob(JobKind::RadioBrowser, radioBrowserKey_, streamUrl_);
    return;
  } else if (selectedSource_ != "nologo") {
    markSourceFailed(selectedSource_);
    refreshSelection();
  }
}

String LogoManager::currentPath() const { return currentPath_; }

String LogoManager::currentSource() const { return selectedSource_; }

bool LogoManager::busy() const { return task_ != nullptr || browserPending_; }

bool LogoManager::needsBrowserImport() const {
  return isRemote(selectedSource_) && sourceFailed(selectedSource_);
}

bool LogoManager::queueBrowserPng(const String& sourceUrl,
                                  const String& temporaryPath) {
  if (maintenance_) return false;
  if (!isRemote(sourceUrl) || !temporaryPath.startsWith("/cache/") ||
      !LittleFS.exists(temporaryPath))
    return false;
  if (task_ || browserPending_) return false;
  browserSource_ = sourceUrl;
  browserTemporaryPath_ = temporaryPath;
  browserPending_ = true;
  return true;
}

bool LogoManager::enterMaintenance(uint32_t timeoutMs) {
  maintenance_ = true;
  browserPending_ = false;
  const uint32_t startedAt = millis();
  while (task_ && millis() - startedAt < timeoutMs) {
    processResult();
    delay(10);
  }
  processResult();
  return task_ == nullptr;
}

void LogoManager::refreshSelection() {
  // A valid local logo is the fastest station-logo base. If it is not present,
  // the normal stream/RadioBrowser lookup chain continues below.
  const String configuredLocal = localLogoPath(stationSource_);
  if (stationSource_ != "nologo" && !configuredLocal.isEmpty() &&
      LittleFS.exists(configuredLocal)) {
    selectedSource_ = stationSource_;
    selectedAt_ = millis();
    String configuredThumbnail;
    if (findCachedThumbnail(stationSource_, configuredThumbnail)) {
      currentPath_ = configuredThumbnail;
    } else {
      currentPath_ = configuredLocal;
    }
    return;
  }

  String next;
  if (!embeddedKey_.isEmpty() && !sourceFailed(embeddedKey_)) {
    next = embeddedKey_;
  } else if (!icySource_.isEmpty() && !sourceFailed(icySource_)) {
    next = icySource_;
  } else if (!stationSource_.isEmpty() && stationSource_ != "nologo" &&
             !sourceFailed(stationSource_)) {
    next = stationSource_;
  } else if (!radioBrowserKey_.isEmpty() && !sourceFailed(radioBrowserKey_)) {
    next = radioBrowserKey_;
  }
  if (next.isEmpty()) next = "nologo";
  selectedSource_ = next;
  selectedAt_ = millis();

  // nologo is a real, immediately drawable display state. Keep it visible
  // while the optional background lookup is running.
  if (stationSource_ == "nologo") {
    String noLogoThumbnail;
    currentPath_ = findCachedThumbnail("nologo", noLogoThumbnail)
                       ? noLogoThumbnail
                       : String("/logos/nologo.png");
  }

  String cached;
  if (stationSource_ != "nologo" &&
      findCachedThumbnail(selectedSource_, cached)) {
    currentPath_ = cached;
    return;
  }
  if (isRemote(selectedSource_)) {
    String noLogoThumbnail;
    currentPath_ = findCachedThumbnail("nologo", noLogoThumbnail)
                       ? noLogoThumbnail
                       : String("/logos/nologo.png");
    return;
  }
  if (!isRemote(selectedSource_) && selectedSource_ != embeddedKey_) {
    const String local = localLogoPath(selectedSource_);
    if (!local.isEmpty() && LittleFS.exists(local)) {
      currentPath_ = local;
      return;
    }
  }

  // Keep the currently displayed image while a background search is pending
  // or fails. selectStation() has already installed nologo for a new station.
  if (currentPath_.isEmpty() || !LittleFS.exists(currentPath_))
    currentPath_ = "/logos/nologo.png";
}

bool LogoManager::startJob(JobKind kind, const String& source,
                           const String& url, const String& localPath,
                           const std::vector<uint32_t>& segments) {
  if (task_ || resultReady_) return false;
  Job* job = new (std::nothrow) Job();
  if (!job) return false;
  job->owner = this;
  job->kind = kind;
  job->source = source;
  job->url = url;
  job->localPath = localPath;
  job->selectionId = selectionId_;
  job->conservativeAlbumDownload =
      kind == JobKind::AlbumCover && albumConservativeDownload_;
  job->homepage = homepage_;
  job->segments = segments;
  const char* kindName = "kepfeladat";
  switch (kind) {
    case JobKind::Remote:
      kindName = "tavoli logo";
      break;
    case JobKind::Embedded:
      kindName = "beagyazott kep";
      break;
    case JobKind::Thumbnail:
      kindName = "thumbnail";
      break;
    case JobKind::BrowserImport:
      kindName = "bongeszo import";
      break;
    case JobKind::RadioBrowser:
      kindName = "radio-browser";
      break;
    case JobKind::AlbumCover:
      kindName = "album borito";
      break;
  }
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "logo", kArtworkTaskStackBytes, job, kArtworkTaskPriority,
      &task_, kArtworkTaskCore);
  if (created != pdPASS) {
    task_ = nullptr;
    Serial.printf("[logo] task inditasi hiba: %s heap=%u psram=%u\n",
                  kindName,
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    delete job;
    return false;
  }
  taskStartedAt_ = millis();
  taskLabel_ = String(kindName) + ": " + (kind == JobKind::AlbumCover ? url : source);
  taskBusyLogged_ = false;
  Serial.printf("[logo] feladat indul: %s\n", taskLabel_.c_str());
  return true;
}

void LogoManager::taskEntry(void* parameter) {
  Job* job = static_cast<Job*>(parameter);
  if (job && job->owner) job->owner->executeJob(*job);
  delete job;
  vTaskDelete(nullptr);
}

void LogoManager::executeJob(Job& job) {
  String imagePath;
  String thumbnail;
  bool success = false;

  gArtworkTaskSelectionId = job.selectionId;
  if (!artworkNetworkShouldAbort()) {
    switch (job.kind) {
      case JobKind::Remote:
        success = downloadRemote(job.url, job.source, imagePath) &&
                  makeThumbnail(imagePath, job.source, thumbnail);
        if (success) compactCache(imagePath, thumbnail);
        break;
      case JobKind::Embedded:
        success =
            downloadEmbedded(job.url, job.segments, job.source, imagePath) &&
            makeThumbnail(imagePath, job.source, thumbnail);
        if (success) compactCache(imagePath, thumbnail);
        break;
      case JobKind::Thumbnail:
        success = makeThumbnail(job.localPath, job.source, thumbnail);
        break;
      case JobKind::BrowserImport:
        imagePath = job.localPath;
        success = makeThumbnail(imagePath, job.source, thumbnail);
        if (success) compactCache(imagePath, thumbnail);
        break;
      case JobKind::RadioBrowser:
        success = downloadRadioBrowserLogo(job.url, job.homepage, job.source,
                                           imagePath) &&
                  makeThumbnail(imagePath, job.source, thumbnail);
        if (success) compactCache(imagePath, thumbnail);
        break;
      case JobKind::AlbumCover:
        success = downloadAlbumCover(job.url, job.source, imagePath, thumbnail,
                                     job.conservativeAlbumDownload);
        if (success) compactCache(imagePath, thumbnail);
        break;
    }
  }
  gArtworkTaskSelectionId = 0;
  finishJob(job.source, success ? thumbnail : String(), success,
            job.selectionId);
}

void LogoManager::finishJob(const String& source, const String& path,
                            bool success, uint32_t selectionId) {
  if (mutex_ && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
    resultSource_ = source;
    resultPath_ = path;
    resultSuccess_ = success;
    resultSelectionId_ = selectionId;
    resultReady_ = true;
    xSemaphoreGive(mutex_);
  } else {
    Serial.println("[logo] feladat eredmeny atadasi hiba");
  }
}

void LogoManager::processResult() {
  if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE) return;
  if (!resultReady_) {
    xSemaphoreGive(mutex_);
    return;
  }
  const String source = resultSource_;
  const String path = resultPath_;
  const bool success = resultSuccess_;
  const uint32_t resultSelectionId = resultSelectionId_;
  resultReady_ = false;
  resultSource_ = "";
  resultPath_ = "";
  resultSelectionId_ = 0;
  task_ = nullptr;
  taskStartedAt_ = 0;
  taskLabel_ = "";
  taskBusyLogged_ = false;
  xSemaphoreGive(mutex_);

  if (!pendingAlbumPurgeKey_.isEmpty() && pendingAlbumPurgeKey_ != source) {
    purgeCachedArtwork(pendingAlbumPurgeKey_);
    pendingAlbumPurgeKey_ = "";
  }

  if (resultSelectionId != selectionId_) {
    if (!pendingAlbumPurgeKey_.isEmpty()) {
      purgeCachedArtwork(pendingAlbumPurgeKey_);
      pendingAlbumPurgeKey_ = "";
    }
    return;
  }
  const bool resultConfiguredSource =
      stationSource_ != "nologo" && !stationSource_.isEmpty() &&
      source == stationSource_;
  const bool resultAlbumCover = source.startsWith("album:");
  if (!resultAlbumCover && !resultConfiguredSource &&
      millis() - searchStartedAt_ >= kNoLogoSearchWindowMs) {
    searchFinished_ = true;
    return;
  }

  if (success) {
    Serial.printf("[logo] cache: %s\n", path.c_str());
    if (resultAlbumCover) gAlbumProviderStartIndex = 0;
    if (!path.isEmpty()) {
      if (source == "nologo" && currentPath_ == "/logos/nologo.png") {
        currentPath_ = path;
      } else if (selectedSource_ == "nologo" && source == radioBrowserKey_) {
        selectedSource_ = source;
        currentPath_ = path;
      } else if (source == selectedSource_) {
        currentPath_ = path;
      }
      if (source != "nologo") searchFinished_ = true;
    }
    if (source == radioBrowserKey_) radioBrowserLoaded_ = true;
  } else {
    const String failedLabel = resultAlbumCover && !albumTitle_.isEmpty()
                                   ? albumTitle_
                                   : source;
    Serial.printf("[logo] sikertelen: %s\n", failedLabel.c_str());
    if (resultAlbumCover) {
      albumRequestedAt_ = millis();
      albumStatusLoggedAt_ = albumRequestedAt_;
      if (gAlbumResourceDeferred || gAlbumNetworkAborted) {
        // Keep the feature available for weak stations, but back off after
        // every resource-limited attempt so one title cannot continuously
        // recreate artwork tasks while the system is under pressure.
        if (albumDeferredRetries_ < 3) ++albumDeferredRetries_;
        const uint32_t retryDelay = min(
            kMaximumAlbumNetworkRetryMs,
            kAlbumNetworkAbortRetryMs << albumDeferredRetries_);
        albumRetryAfter_ = millis() + retryDelay;
        gAlbumProviderStartIndex = (gAlbumProviderStartIndex + 1) % 4;
        Serial.printf("[cover] ujraproba %lu mp mulva: %s\n",
                      static_cast<unsigned long>(retryDelay / 1000),
                      albumTitle_.c_str());
        gAlbumNetworkAborted = false;
        gAlbumResourceDeferred = false;
      } else {
        markSourceFailed(source);
      }
    } else {
      markSourceFailed(source);
      const bool keepConfiguredRemoteForBrowser =
          resultConfiguredSource && isRemote(source);
      if (keepConfiguredRemoteForBrowser) {
        String noLogoThumbnail;
        currentPath_ = findCachedThumbnail("nologo", noLogoThumbnail)
                           ? noLogoThumbnail
                           : String("/logos/nologo.png");
        Serial.println("[logo] varakozas bongeszos importalasra");
      } else if (source == selectedSource_) {
        refreshSelection();
      }
    }
  }

  if (!pendingAlbumPurgeKey_.isEmpty()) {
    purgeCachedArtwork(pendingAlbumPurgeKey_);
    pendingAlbumPurgeKey_ = "";
  }
}

bool LogoManager::sourceFailed(const String& source) const {
  for (const String& failed : failedSources_) {
    if (failed == source) return true;
  }
  return false;
}

void LogoManager::markSourceFailed(const String& source) {
  if (!source.isEmpty() && !sourceFailed(source))
    failedSources_.push_back(source);
}

bool LogoManager::isRemote(const String& value) {
  return value.startsWith("http://") || value.startsWith("https://");
}

String LogoManager::resolveIcyUrl(String logo, const String& streamUrl) {
  logo.trim();
  if (logo.isEmpty() || isRemote(logo)) return logo;
  if (logo.startsWith("//"))
    return String(streamUrl.startsWith("https://") ? "https:" : "http:") +
           logo;
  const int schemeEnd = streamUrl.indexOf("://");
  if (schemeEnd < 0) return logo;
  const int hostStart = schemeEnd + 3;
  const int pathStart = streamUrl.indexOf('/', hostStart);
  const String origin =
      pathStart > 0 ? streamUrl.substring(0, pathStart) : streamUrl;
  if (logo.startsWith("/")) return origin + logo;
  const int lastSlash = streamUrl.lastIndexOf('/');
  const String base =
      lastSlash >= hostStart ? streamUrl.substring(0, lastSlash + 1)
                             : origin + "/";
  return base + logo;
}

String LogoManager::localLogoPath(String source) {
  source.trim();
  if (source.isEmpty() || source == "nologo") return "/logos/nologo.png";
  if (isRemote(source) || source.startsWith("embedded:")) return "";
  if (!source.startsWith("/")) source = "/logos/" + source;
  if (LittleFS.exists(source)) return source;
  const int slash = source.lastIndexOf('/');
  const int dot = source.lastIndexOf('.');
  if (dot > slash) return source;
  const char* extensions[] = {".png", ".bmp", ".jpg", ".jpeg"};
  for (const char* extension : extensions) {
    const String candidate = source + extension;
    if (LittleFS.exists(candidate)) return candidate;
  }
  return source;
}

String LogoManager::cacheStem(const String& key) {
  char path[36];
  snprintf(path, sizeof(path), "/cache/logo_%08lx",
           static_cast<unsigned long>(fnv1a(key)));
  return path;
}

String LogoManager::thumbnailPath(const String& key) {
  return cacheStem(key) + ".sr565";
}

bool LogoManager::validThumbnail(const String& path) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() != kThumbnailBytes) {
    if (file) file.close();
    return false;
  }
  uint8_t header[8]{};
  const bool valid =
      file.read(header, sizeof(header)) == sizeof(header) &&
      header[0] == 'S' && header[1] == 'R' && header[2] == '5' &&
      header[3] == '7' && readLe16(header + 4) == kThumbnailSize &&
      readLe16(header + 6) == kThumbnailSize;
  file.close();
  return valid;
}

bool thumbnailHasVisiblePixels(const String& path) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() != kThumbnailBytes) {
    if (file) file.close();
    return false;
  }
  uint8_t header[8]{};
  if (file.read(header, sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }
  std::vector<uint8_t> pixels(static_cast<size_t>(kThumbnailSize) *
                              static_cast<size_t>(kThumbnailSize) * 2);
  const size_t received = file.read(pixels.data(), pixels.size());
  file.close();
  if (received != pixels.size()) return false;
  return hasVisiblePixels(pixels.data(),
                          static_cast<size_t>(kThumbnailSize) *
                              static_cast<size_t>(kThumbnailSize));
}

bool LogoManager::findCachedThumbnail(const String& key, String& path) {
  if (key.isEmpty()) return false;
  const String candidate = thumbnailPath(key);
  if (!validThumbnail(candidate) || !thumbnailHasVisiblePixels(candidate)) {
    if (LittleFS.exists(candidate)) LittleFS.remove(candidate);
    return false;
  }
  path = candidate;
  return true;
}

bool LogoManager::downloadRemote(const String& url, const String& key,
                                 String& imagePath) {
  // Prefer the secure URL. Some image CDNs reject the HTTP endpoint or return
  // a redirect that the ESP32 client cannot reproduce reliably.
  if (url.startsWith("http://")) {
    const String secure = "https://" + url.substring(7);
    Serial.printf("[logo] HTTPS proba: %s\n", secure.c_str());
    if (downloadAttempt(secure, key, imagePath)) return true;
    if (gAlbumNetworkAborted) return false;
  }
  if (downloadAttempt(url, key, imagePath)) return true;
  if (gAlbumNetworkAborted) return false;
  if (url.startsWith("https://")) {
    const String plain = "http://" + url.substring(8);
    Serial.printf("[logo] HTTP tartalek: %s\n", plain.c_str());
    return downloadAttempt(plain, key, imagePath);
  }
  return false;
}

String LogoManager::resolveRelativeUrl(String value,
                                       const String& baseUrl) {
  value.trim();
  if (value.isEmpty() || isRemote(value)) return value;
  if (value.startsWith("//"))
    return String(baseUrl.startsWith("https://") ? "https:" : "http:") +
           value;
  const int schemeEnd = baseUrl.indexOf("://");
  if (schemeEnd < 0) return "";
  const int hostStart = schemeEnd + 3;
  const int pathStart = baseUrl.indexOf('/', hostStart);
  const String origin =
      pathStart > 0 ? baseUrl.substring(0, pathStart) : baseUrl;
  if (value.startsWith("/")) return origin + value;
  const int lastSlash = baseUrl.lastIndexOf('/');
  const String base =
      lastSlash >= hostStart ? baseUrl.substring(0, lastSlash + 1)
                             : origin + "/";
  return base + value;
}

bool LogoManager::downloadAttempt(const String& fetchUrl, const String& key,
                                  String& imagePath) {
  Serial.printf("[logo] letoltes: %s\n", fetchUrl.c_str());
  const bool secureFetch = fetchUrl.startsWith("https://");
  const size_t requiredHeap = secureFetch
                                  ? kMinimumArtworkSecureFetchInternalHeap
                                  : kMinimumArtworkPlainFetchInternalHeap;
  const size_t requiredBlock = secureFetch ? kMinimumArtworkSecureFetchBlock
                                           : kMinimumArtworkPlainFetchBlock;
  const size_t freeInternalHeap =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largestInternalBlock =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (freeInternalHeap < requiredHeap || largestInternalBlock < requiredBlock) {
    Serial.printf(
        "[logo] letoltes kihagyva: keves belso heap %u/%u byte, blokk=%u/%u%s\n",
        static_cast<unsigned>(freeInternalHeap),
        static_cast<unsigned>(requiredHeap),
        static_cast<unsigned>(largestInternalBlock),
        static_cast<unsigned>(requiredBlock), secureFetch ? " HTTPS" : "");
    if (gAlbumNetworkPoliteLevel) gAlbumResourceDeferred = true;
    return false;
  }
  if (artworkNetworkShouldAbort()) {
    gAlbumNetworkAborted = true;
    gAlbumResourceDeferred = true;
    Serial.printf("[cover] kep letoltes nem indul: puffer %u%% %u byte\n",
                  static_cast<unsigned>(gArtworkBufferPercent),
                  static_cast<unsigned>(gArtworkBufferFilledBytes));
    return false;
  }
  std::unique_ptr<NetworkClientSecure> secureClient;
  NetworkClient plainClient;
  NetworkClient* client = &plainClient;
  if (secureFetch) {
    secureClient.reset(new (std::nothrow) NetworkClientSecure());
    if (!secureClient) return false;
    secureClient->setInsecure();
    client = secureClient.get();
  }
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(6000);
  http.setUserAgent("LVGL-Radio/1.0 ESP32");
  http.addHeader("Accept", "image/jpeg,image/png,image/bmp,*/*;q=0.5");
  http.addHeader("Referer", "https://www.radio.pl/");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(*client, fetchUrl)) return false;
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[logo] HTTP hiba %d: %s\n", code, fetchUrl.c_str());
    http.end();
    return false;
  }
  const int declaredLength = http.getSize();
  if (declaredLength == 0 ||
      (declaredLength > 0 &&
       static_cast<size_t>(declaredLength) > kMaximumArtworkBytes)) {
    http.end();
    return false;
  }
  const size_t expectedBytes =
      declaredLength > 0 ? static_cast<size_t>(declaredLength)
                         : kMinimumArtworkFreeBytes;
  purgeCacheForSpace(expectedBytes + kMinimumArtworkFreeBytes);
  if (littleFsFreeBytes() < expectedBytes) {
    Serial.printf("[logo] nincs eleg LittleFS hely: szabad=%u kell=%u\n",
                  static_cast<unsigned>(littleFsFreeBytes()),
                  static_cast<unsigned>(expectedBytes));
    http.end();
    return false;
  }
  const String temporary = cacheStem(key) + ".tmp";
  if (LittleFS.exists(temporary)) LittleFS.remove(temporary);
  File output = LittleFS.open(temporary, FILE_WRITE);
  if (!output) {
    http.end();
    return false;
  }
  NetworkClient* stream = http.getStreamPtr();
  if (!stream) {
    Serial.printf("[logo] kep stream hiba: %s\n", fetchUrl.c_str());
    output.close();
    http.end();
    LittleFS.remove(temporary);
    return false;
  }
  uint8_t buffer[1024];
  size_t written = 0;
  uint32_t lastReadAt = millis();
  while ((http.connected() || stream->available()) &&
         (declaredLength < 0 ||
          written < static_cast<size_t>(declaredLength))) {
    if (artworkNetworkShouldAbort()) {
      gAlbumNetworkAborted = true;
      gAlbumResourceDeferred = true;
      Serial.printf("[cover] kep letoltes megszakitva: puffer %u%% %u byte\n",
                    static_cast<unsigned>(gArtworkBufferPercent),
                    static_cast<unsigned>(gArtworkBufferFilledBytes));
      break;
    }
    const size_t available = stream->available();
    if (!available) {
      if (millis() - lastReadAt > kReadIdleTimeoutMs) break;
      vTaskDelay(pdMS_TO_TICKS(artworkNetworkDelayMs()));
      continue;
    }
    size_t requested = min(available, artworkNetworkChunkLimit(sizeof(buffer)));
    if (declaredLength > 0)
      requested =
          min(requested, static_cast<size_t>(declaredLength) - written);
    const int received = stream->readBytes(buffer, requested);
    if (received <= 0 ||
        output.write(buffer, received) != static_cast<size_t>(received))
      break;
    written += received;
    lastReadAt = millis();
    if (written > kMaximumArtworkBytes) break;
    vTaskDelay(pdMS_TO_TICKS(artworkNetworkDelayMs()));
  }
  output.flush();
  output.close();
  http.end();
  if (!written || written > kMaximumArtworkBytes ||
      (declaredLength > 0 && written != static_cast<size_t>(declaredLength))) {
    Serial.printf("[logo] incomplet kep: %u byte, vart=%d: %s\n",
                  static_cast<unsigned>(written), declaredLength,
                  fetchUrl.c_str());
    LittleFS.remove(temporary);
    return false;
  }
  String extension = detectImageExtension(temporary);
  if (extension.isEmpty()) {
    Serial.printf("[logo] ismeretlen kepformatum: %s\n", fetchUrl.c_str());
    const bool ico = extractCompressedIco(temporary, key, imagePath);
    LittleFS.remove(temporary);
    return ico;
  }
  imagePath = cacheStem(key) + extension;
  if (LittleFS.exists(imagePath)) LittleFS.remove(imagePath);
  if (!LittleFS.rename(temporary, imagePath)) {
    LittleFS.remove(temporary);
    return false;
  }
  return true;
}

bool LogoManager::appendHttpRange(const String& url, uint32_t offset,
                                  uint32_t length, File& output) {
  if (!length || length > kMaximumArtworkBytes) return false;
  if (artworkNetworkShouldAbort()) return false;
#if DISPLAY_PROFILE_AXS15231B
  if (url.startsWith("https://")) {
    const size_t freeInternalHeap =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largestInternalBlock =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (freeInternalHeap < kMinimumAxsSecureFetchInternalHeap ||
        largestInternalBlock < kMinimumAxsSecureFetchBlock) {
      Serial.printf(
          "[cover] HTTPS range kihagyva: keves belso heap %u/%u byte, blokk=%u/%u\n",
          static_cast<unsigned>(freeInternalHeap),
          static_cast<unsigned>(kMinimumAxsSecureFetchInternalHeap),
          static_cast<unsigned>(largestInternalBlock),
          static_cast<unsigned>(kMinimumAxsSecureFetchBlock));
      gAlbumResourceDeferred = true;
      return false;
    }
  }
#endif
  const bool secureFetch = url.startsWith("https://");
  std::unique_ptr<NetworkClientSecure> secureClient;
  NetworkClient plainClient;
  NetworkClient* client = &plainClient;
  if (secureFetch) {
    secureClient.reset(new (std::nothrow) NetworkClientSecure());
    if (!secureClient) return false;
    secureClient->setInsecure();
    client = secureClient.get();
  }
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Range", "bytes=" + String(offset) + "-" +
                              String(offset + length - 1));
  const int code = http.GET();
  if (artworkNetworkShouldAbort()) {
    http.end();
    return false;
  }
  if (code != HTTP_CODE_PARTIAL_CONTENT) {
    http.end();
    return false;
  }
  NetworkClient* stream = http.getStreamPtr();
  if (!stream) {
    http.end();
    return false;
  }
  uint8_t buffer[1024];
  uint32_t remaining = length;
  uint32_t lastReadAt = millis();
  while ((http.connected() || stream->available()) && remaining) {
    if (artworkNetworkShouldAbort()) break;
    const size_t available = stream->available();
    if (!available) {
      if (millis() - lastReadAt > kReadIdleTimeoutMs) break;
      vTaskDelay(1);
      continue;
    }
    size_t requested = min(available, sizeof(buffer));
    requested = min(requested, static_cast<size_t>(remaining));
    const int received = stream->readBytes(buffer, requested);
    if (received <= 0 ||
        output.write(buffer, received) != static_cast<size_t>(received))
      break;
    remaining -= received;
    lastReadAt = millis();
    vTaskDelay(1);
  }
  http.end();
  return remaining == 0;
}

bool LogoManager::downloadEmbedded(
    const String& url, const std::vector<uint32_t>& segments,
    const String& key, String& imagePath) {
  const String temporary = cacheStem(key) + ".emb";
  if (LittleFS.exists(temporary)) LittleFS.remove(temporary);
  File output = LittleFS.open(temporary, FILE_WRITE);
  if (!output) return false;
  for (size_t index = 0; index + 1 < segments.size(); index += 2) {
    if (!appendHttpRange(url, segments[index], segments[index + 1], output)) {
      output.close();
      LittleFS.remove(temporary);
      return false;
    }
  }
  output.flush();
  output.close();
  const bool success = normalizeEmbedded(temporary, key, imagePath);
  LittleFS.remove(temporary);
  return success;
}

bool LogoManager::normalizeEmbedded(const String& temporaryPath,
                                    const String& key, String& imagePath) {
  File file = LittleFS.open(temporaryPath, FILE_READ);
  if (!file) return false;
  const size_t size = file.size();
  file.close();
  if (normalizePayload(temporaryPath, 0, size, key, imagePath)) return true;

  file = LittleFS.open(temporaryPath, FILE_READ);
  uint32_t pictureType = 0;
  uint32_t mimeLength = 0;
  if (file && readBe32(file, pictureType) && readBe32(file, mimeLength) &&
      pictureType <= 20 && mimeLength > 4 && mimeLength < 80 &&
      file.seek(file.position() + mimeLength)) {
    uint32_t descriptionLength = 0;
    if (readBe32(file, descriptionLength) && descriptionLength < 4096 &&
        file.seek(file.position() + descriptionLength + 16)) {
      uint32_t dataLength = 0;
      if (readBe32(file, dataLength)) {
        const size_t dataOffset = file.position();
        file.close();
        if (dataLength && dataLength <= kMaximumArtworkBytes &&
            dataOffset + dataLength <= size &&
            normalizePayload(temporaryPath, dataOffset, dataLength, key,
                             imagePath))
          return true;
      }
    }
  }
  if (file) file.close();

  file = LittleFS.open(temporaryPath, FILE_READ);
  const int encoding = file ? file.read() : -1;
  if (encoding >= 0 && size > 16) {
    String mime;
    while (file.available() && mime.length() < 80) {
      const int character = file.read();
      if (character <= 0) break;
      mime += static_cast<char>(character);
    }
    if (mime.startsWith("image/") && file.available()) {
      file.read();
      size_t guard = 0;
      if (encoding == 1 || encoding == 2) {
        int previous = -1;
        while (file.available() && guard++ < 1024) {
          const int character = file.read();
          if (previous == 0 && character == 0) break;
          previous = character;
        }
      } else {
        while (file.available() && guard++ < 1024) {
          if (file.read() == 0) break;
        }
      }
      const size_t dataOffset = file.position();
      file.close();
      if (dataOffset < size)
        return normalizePayload(temporaryPath, dataOffset, size - dataOffset,
                                key, imagePath);
    }
  }
  if (file) file.close();
  return false;
}

bool LogoManager::normalizePayload(const String& sourcePath, size_t offset,
                                   size_t length, const String& key,
                                   String& imagePath) {
  if (!length || length > kMaximumArtworkBytes) return false;
  const String payload = cacheStem(key) + ".payload";
  if (!copyFileRange(sourcePath, offset, length, payload)) return false;
  const String extension = detectImageExtension(payload);
  if (extension.isEmpty()) {
    const bool ico = extractCompressedIco(payload, key, imagePath);
    LittleFS.remove(payload);
    return ico;
  }
  imagePath = cacheStem(key) + extension;
  if (LittleFS.exists(imagePath)) LittleFS.remove(imagePath);
  if (!LittleFS.rename(payload, imagePath)) {
    LittleFS.remove(payload);
    return false;
  }
  return true;
}

bool LogoManager::extractCompressedIco(const String& sourcePath,
                                       const String& key, String& imagePath) {
  File file = LittleFS.open(sourcePath, FILE_READ);
  if (!file || file.size() < 22) {
    if (file) file.close();
    return false;
  }
  uint8_t header[6]{};
  if (file.read(header, sizeof(header)) != sizeof(header) ||
      readLe16(header) != 0 || readLe16(header + 2) != 1) {
    file.close();
    return false;
  }
  const uint16_t count = readLe16(header + 4);
  if (!count || count > 32 || file.size() < 6 + count * 16) {
    file.close();
    return false;
  }
  uint32_t bestOffset = 0;
  uint32_t bestLength = 0;
  uint16_t bestSize = 0;
  for (uint16_t index = 0; index < count; ++index) {
    uint8_t entry[16]{};
    if (!file.seek(6 + index * 16) ||
        file.read(entry, sizeof(entry)) != sizeof(entry))
      break;
    const uint16_t width = entry[0] ? entry[0] : 256;
    const uint16_t height = entry[1] ? entry[1] : 256;
    const uint32_t bytes = readLe32(entry + 8);
    const uint32_t offset = readLe32(entry + 12);
    uint8_t signature[4]{};
    if (!bytes || bytes > kMaximumArtworkBytes ||
        offset + bytes > file.size() || !file.seek(offset) ||
        file.read(signature, sizeof(signature)) != sizeof(signature))
      continue;
    const bool compressed =
        (signature[0] == 0x89 && signature[1] == 'P' &&
         signature[2] == 'N' && signature[3] == 'G') ||
        (signature[0] == 0xFF && signature[1] == 0xD8 &&
         signature[2] == 0xFF);
    const uint16_t dimension = max(width, height);
    if (compressed &&
        (!bestLength || abs(static_cast<int>(dimension) - 128) <
                            abs(static_cast<int>(bestSize) - 128))) {
      bestOffset = offset;
      bestLength = bytes;
      bestSize = dimension;
    }
  }
  file.close();
  if (!bestLength) return false;
  return normalizePayload(sourcePath, bestOffset, bestLength,
                          key + ":ico", imagePath);
}

String LogoManager::detectImageExtension(const String& path) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) return "";
  uint8_t header[12]{};
  const size_t read = file.read(header, sizeof(header));
  file.close();
  if (read >= 8 && header[0] == 0x89 && header[1] == 'P' &&
      header[2] == 'N' && header[3] == 'G')
    return ".png";
  if (read >= 3 && header[0] == 0xFF && header[1] == 0xD8 &&
      header[2] == 0xFF)
    return ".jpg";
  if (read >= 2 && header[0] == 'B' && header[1] == 'M') return ".bmp";
  if (read >= 12 && header[0] == 'R' && header[1] == 'I' &&
      header[2] == 'F' && header[3] == 'F' && header[8] == 'W' &&
      header[9] == 'E' && header[10] == 'B' && header[11] == 'P')
    return ".webp";
  return "";
}

bool LogoManager::makeThumbnail(const String& imagePath, const String& key,
                                String& thumbnail) {
  thumbnail = thumbnailPath(key);
  if (validThumbnail(thumbnail)) return true;

  PsramBytes bytes;
  if (!loadFileBytes(imagePath, bytes)) return false;
  int sourceWidth = 0;
  int sourceHeight = 0;
  int channels = 0;
  stbi_uc* rgba = stbi_load_from_memory(bytes.data,
                                        static_cast<int>(bytes.size()),
                                        &sourceWidth, &sourceHeight,
                                        &channels, 4);
  if (!rgba || sourceWidth <= 0 || sourceHeight <= 0) {
    if (rgba) stbi_image_free(rgba);
    Serial.printf("[logo] kepdekodolas sikertelen: %s (%s)\n",
                  imagePath.c_str(),
                  stbi_failure_reason() ? stbi_failure_reason() : "?");
    return false;
  }

  const size_t pixelBytes =
      static_cast<size_t>(kThumbnailSize) * kThumbnailSize * 2;
  uint8_t* pixels = static_cast<uint8_t*>(
      heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) pixels = static_cast<uint8_t*>(malloc(pixelBytes));
  if (!pixels) {
    stbi_image_free(rgba);
    return false;
  }
  memset(pixels, 0, pixelBytes);

  const float scale =
      min(static_cast<float>(kThumbnailSize) / sourceWidth,
          static_cast<float>(kThumbnailSize) / sourceHeight);
  const int targetWidth = max<int>(1, lroundf(sourceWidth * scale));
  const int targetHeight = max<int>(1, lroundf(sourceHeight * scale));
  const int offsetX = (kThumbnailSize - targetWidth) / 2;
  const int offsetY = (kThumbnailSize - targetHeight) / 2;

  for (int y = 0; y < targetHeight; ++y) {
    const int srcY = min(sourceHeight - 1,
                         static_cast<int>((static_cast<int64_t>(y) *
                                           sourceHeight) /
                                          targetHeight));
    for (int x = 0; x < targetWidth; ++x) {
      const int srcX = min(sourceWidth - 1,
                           static_cast<int>((static_cast<int64_t>(x) *
                                             sourceWidth) /
                                            targetWidth));
      const uint8_t* src =
          rgba + (static_cast<size_t>(srcY) * sourceWidth + srcX) * 4;
      uint32_t red = src[0];
      uint32_t green = src[1];
      uint32_t blue = src[2];
      const uint32_t alpha = src[3];
      if (alpha < 255) {
        red = (red * alpha) / 255;
        green = (green * alpha) / 255;
        blue = (blue * alpha) / 255;
      }
      const uint16_t color =
          static_cast<uint16_t>(((red & 0xF8) << 8) |
                                ((green & 0xFC) << 3) |
                                (blue >> 3));
      const size_t dst =
          (static_cast<size_t>(offsetY + y) * kThumbnailSize + offsetX + x) *
          2;
      pixels[dst] = static_cast<uint8_t>(color >> 8);
      pixels[dst + 1] = static_cast<uint8_t>(color);
    }
    if ((y & 0x0F) == 0x0F) vTaskDelay(1);
  }
  stbi_image_free(rgba);

  if (!hasVisiblePixels(pixels,
                        static_cast<size_t>(kThumbnailSize) * kThumbnailSize)) {
    heap_caps_free(pixels);
    return false;
  }

  const String temporary = thumbnail + ".tmp";
  if (LittleFS.exists(temporary)) LittleFS.remove(temporary);
  File output = LittleFS.open(temporary, FILE_WRITE);
  if (!output) {
    heap_caps_free(pixels);
    return false;
  }
  const uint8_t header[8] = {
      'S', 'R', '5', '7',
      static_cast<uint8_t>(kThumbnailSize & 0xFF),
      static_cast<uint8_t>(kThumbnailSize >> 8),
      static_cast<uint8_t>(kThumbnailSize & 0xFF),
      static_cast<uint8_t>(kThumbnailSize >> 8),
  };
  bool success = output.write(header, sizeof(header)) == sizeof(header);
  size_t written = 0;
  while (success && written < pixelBytes) {
    const size_t chunk = min<size_t>(4096, pixelBytes - written);
    success = output.write(pixels + written, chunk) == chunk;
    written += chunk;
    vTaskDelay(1);
  }
  output.flush();
  output.close();
  heap_caps_free(pixels);
  if (!success) {
    LittleFS.remove(temporary);
    return false;
  }
  if (LittleFS.exists(thumbnail)) LittleFS.remove(thumbnail);
  if (!LittleFS.rename(temporary, thumbnail)) {
    LittleFS.remove(temporary);
    return false;
  }
  enforceCacheBudget(thumbnail);
  return true;
}

void LogoManager::compactCache(const String& imagePath,
                               const String& thumbnail) {
  if (imagePath.startsWith("/cache/") && imagePath != thumbnail)
    LittleFS.remove(imagePath);
}

void LogoManager::purgeCachedArtwork(const String& key) {
  if (key.isEmpty() || !key.startsWith("album:")) return;
  const String stem = cacheStem(key);
  const char* extensions[] = {
      ".sr565", ".tmp", ".png", ".jpg", ".jpeg", ".bmp", ".webp", ".payload",
      ".emb"};
  for (const char* extension : extensions) {
    const String path = stem + extension;
    if (LittleFS.exists(path) && LittleFS.remove(path)) {
      Serial.printf("[cover] regi album cache torolve: %s\n", path.c_str());
    }
  }
}

void LogoManager::enforceCacheBudget(const String& keepPath) {
  size_t total = 0;
  File root = LittleFS.open("/cache");
  if (!root || !root.isDirectory()) return;
  for (File file = root.openNextFile(); file; file = root.openNextFile())
    total += file.size();
  root.close();
  if (total <= kMaximumCacheBytes) return;

  root = LittleFS.open("/cache");
  for (File file = root.openNextFile();
       file && total > kTargetCacheBytes; file = root.openNextFile()) {
    const String path = file.path();
    const size_t size = file.size();
    file.close();
    if (path == keepPath || path.endsWith(".tmp")) continue;
    if (LittleFS.remove(path)) total = total > size ? total - size : 0;
  }
  root.close();
}
