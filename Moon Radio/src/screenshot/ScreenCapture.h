#pragma once

#include <Arduino.h>

// Reusable logical-screen capture component.  It deliberately captures the
// rendered RGB565 image instead of reading pixels back from the panel: many
// TFT/QSPI controllers are write-only in their real hardware configuration.
class ScreenCapture {
 public:
  static constexpr uint16_t kWidth = 480;
  static constexpr uint16_t kHeight = 320;

  bool begin();
  void captureRect(const uint16_t* pixels, int32_t x, int32_t y,
                   int32_t width, int32_t height);
  void captureNativeRect(const uint16_t* pixels, int32_t x, int32_t y,
                         int32_t width, int32_t height);
  bool copyRow(uint16_t y, uint8_t* destination,
               size_t destinationBytes) const;
  bool ready() const { return frame_ != nullptr && ready_; }

 private:
  uint16_t* frame_{nullptr};
  bool ready_{false};
};
