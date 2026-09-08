#pragma once

#include <Arduino.h>
#include <array>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>

#include "AudioSnapshot.h"

class AudioSpectrum {
 public:
  static constexpr size_t kFftSize = 256;

  bool begin();
  void setEnabled(bool enabled);
  AudioLevels snapshot();
  void capture(const int32_t* samples, int16_t frameCount);
  static void captureActive(const int32_t* samples, int16_t frameCount);

 private:
  enum class BufferState : uint8_t { Free, Writing, Ready, Processing };

  static void taskEntry(void* parameter);
  void taskLoop();
  bool claimReadyBuffer(uint8_t& index);
  void processBuffer(uint8_t index);
  bool selectWriteBuffer();
  void publishPeaks(uint8_t left, uint8_t right);

  portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
  std::array<std::array<int16_t, kFftSize>, 2> samples_{};
  std::array<BufferState, 2> states_{
      BufferState::Free, BufferState::Free};
  std::array<float, kFftSize> window_{};
  std::array<float, kFftSize * 2> work_{};
  std::array<uint8_t, AudioLevels::kSpectrumBands> publishedBands_{};
  uint8_t publishedLeft_{0};
  uint8_t publishedRight_{0};
  uint8_t writeBuffer_{0};
  size_t writePosition_{0};
  uint32_t nextCaptureAt_{0};
  volatile bool enabled_{false};
  volatile bool initialized_{false};
  TaskHandle_t task_{nullptr};
  static AudioSpectrum* active_;
};
