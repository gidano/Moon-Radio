#include "TlsMemory.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

#include <limits>

namespace {

void* tlsPsramCalloc(size_t count, size_t size) {
  if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
    return nullptr;
  }

  // A TLS kézfogás 16 kB-os be- és kimeneti pufferei nem igényelnek
  // belső vagy DMA-képes memóriát, ezért biztonságosan kerülhetnek PSRAM-ba.
  void* memory =
      heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) {
    memory =
        heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  return memory;
}

void tlsPsramFree(void* memory) {
  if (memory) heap_caps_free(memory);
}

}  // namespace

bool enableTlsPsramAllocator() {
  if (!psramFound()) {
    Serial.println("[tls] PSRAM nem érhető el");
    return false;
  }

  const int result =
      mbedtls_platform_set_calloc_free(tlsPsramCalloc, tlsPsramFree);
  Serial.printf("[tls] PSRAM memóriafoglaló: %s\n",
                result == 0 ? "OK" : "HIBA");
  return result == 0;
}
