#pragma once

#include <Arduino.h>

// Az album-borító thumbnailek PSRAM-ban élnek, nem a LittleFS-en.  Dalonként
// cserélődnek, így a flash-írásuk (cache-tiltás, a 0. magon futó dekóder
// megállítása) csak akadást okozna, haszna pedig nincs.  Az elérési út
// ugyanaz a "/cache/....sr565" marad, a fogyasztók először itt keresik.
namespace memory_artwork {

constexpr uint16_t kThumbnailSize = 128;
constexpr size_t kPixelBytes =
    static_cast<size_t>(kThumbnailSize) * kThumbnailSize * 2;

// Átveszi a heap_caps_malloc()/malloc() által foglalt, kPixelBytes méretű
// pixelpuffer tulajdonjogát (hiba esetén is felszabadítja).
bool store(const String& path, uint8_t* pixels);
bool contains(const String& path);
// kPixelBytes méretű célpufferbe másol.
bool copyPixels(const String& path, uint8_t* out);
void remove(const String& path);

}  // namespace memory_artwork
