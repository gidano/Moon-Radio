#include "MemoryArtwork.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>

namespace memory_artwork {
namespace {

constexpr size_t kSlotCount = 3;

struct Slot {
  String path;
  uint8_t* pixels = nullptr;
  uint32_t usedAt = 0;
};

Slot gSlots[kSlotCount];
uint32_t gUseCounter = 0;
SemaphoreHandle_t gMutex = nullptr;
portMUX_TYPE gInitLock = portMUX_INITIALIZER_UNLOCKED;

bool lock() {
  if (!gMutex) {
    SemaphoreHandle_t created = xSemaphoreCreateMutex();
    portENTER_CRITICAL(&gInitLock);
    if (!gMutex) {
      gMutex = created;
      created = nullptr;
    }
    portEXIT_CRITICAL(&gInitLock);
    if (created) vSemaphoreDelete(created);
  }
  return gMutex && xSemaphoreTake(gMutex, portMAX_DELAY) == pdTRUE;
}

void unlock() { xSemaphoreGive(gMutex); }

Slot* find(const String& path) {
  for (Slot& slot : gSlots) {
    if (slot.pixels && slot.path == path) return &slot;
  }
  return nullptr;
}

}  // namespace

bool store(const String& path, uint8_t* pixels) {
  if (!pixels) return false;
  if (path.isEmpty() || !lock()) {
    heap_caps_free(pixels);
    return false;
  }
  Slot* slot = find(path);
  if (!slot) {
    slot = &gSlots[0];
    for (Slot& candidate : gSlots) {
      if (!candidate.pixels) {
        slot = &candidate;
        break;
      }
      if (candidate.usedAt < slot->usedAt) slot = &candidate;
    }
  }
  if (slot->pixels) heap_caps_free(slot->pixels);
  slot->path = path;
  slot->pixels = pixels;
  slot->usedAt = ++gUseCounter;
  unlock();
  return true;
}

bool contains(const String& path) {
  if (path.isEmpty() || !lock()) return false;
  const bool found = find(path) != nullptr;
  unlock();
  return found;
}

bool copyPixels(const String& path, uint8_t* out) {
  if (!out || path.isEmpty() || !lock()) return false;
  Slot* slot = find(path);
  if (slot) {
    memcpy(out, slot->pixels, kPixelBytes);
    slot->usedAt = ++gUseCounter;
  }
  unlock();
  return slot != nullptr;
}

void remove(const String& path) {
  if (path.isEmpty() || !lock()) return;
  if (Slot* slot = find(path)) {
    heap_caps_free(slot->pixels);
    slot->pixels = nullptr;
    slot->path = "";
    slot->usedAt = 0;
  }
  unlock();
}

}  // namespace memory_artwork
