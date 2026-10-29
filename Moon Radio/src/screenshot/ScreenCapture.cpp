#include "ScreenCapture.h"

#include <cstring>

#include <esp_heap_caps.h>

bool ScreenCapture::begin() {
  if (frame_) return true;
  frame_ = static_cast<uint16_t*>(heap_caps_calloc(
      static_cast<size_t>(kWidth) * kHeight, sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return frame_ != nullptr;
}

void ScreenCapture::captureRect(const uint16_t* pixels, int32_t x, int32_t y,
                                int32_t width, int32_t height) {
  if (!frame_ || !pixels || x < 0 || y < 0 || width <= 0 || height <= 0 ||
      x + width > kWidth || y + height > kHeight) {
    return;
  }
  for (int32_t row = 0; row < height; ++row) {
    memcpy(frame_ + static_cast<size_t>(y + row) * kWidth + x,
           pixels + static_cast<size_t>(row) * width,
           static_cast<size_t>(width) * sizeof(uint16_t));
  }
  ready_ = true;
}

void ScreenCapture::captureNativeRect(const uint16_t* pixels, int32_t x,
                                      int32_t y, int32_t width,
                                      int32_t height) {
  if (!frame_ || !pixels || x < 0 || y < 0 || width <= 0 || height <= 0 ||
      x + width > kWidth || y + height > kHeight) {
    return;
  }
  for (int32_t row = 0; row < height; ++row) {
    uint16_t* target = frame_ + static_cast<size_t>(y + row) * kWidth + x;
    const uint16_t* source = pixels + static_cast<size_t>(row) * width;
    for (int32_t col = 0; col < width; ++col) {
      target[col] = __builtin_bswap16(source[col]);
    }
  }
}

bool ScreenCapture::copyRow(uint16_t y, uint8_t* destination,
                            size_t destinationBytes) const {
  if (!ready() || !destination || y >= kHeight ||
      destinationBytes < static_cast<size_t>(kWidth) * 2) {
    return false;
  }
  const uint16_t* source = frame_ + static_cast<size_t>(y) * kWidth;
  for (uint16_t x = 0; x < kWidth; ++x) {
    const uint16_t pixel = source[x];
    destination[static_cast<size_t>(x) * 2] = static_cast<uint8_t>(pixel >> 8);
    destination[static_cast<size_t>(x) * 2 + 1] = static_cast<uint8_t>(pixel);
  }
  return true;
}
