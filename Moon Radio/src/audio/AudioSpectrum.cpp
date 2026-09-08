#include "AudioSpectrum.h"

#include <esp_dsp.h>
#include <math.h>

namespace {

constexpr uint32_t kCaptureIntervalMs = 60;
constexpr float kMinimumDb = -58.0f;
constexpr float kMaximumDb = -6.0f;


// 256 pontos FFT-nél ezek közel logaritmikus sávok a teljes hallható
// tartományban. Minden sáv legalább egy frekvenciabint tartalmaz.
constexpr uint8_t kBandEdges[AudioLevels::kSpectrumBands + 1] = {
    1, 2, 3, 4, 6, 9, 13, 19, 28, 41, 60, 88, 128};

uint8_t absoluteLevel(int32_t sample) {
  const int32_t shifted = sample >> 23;
  return static_cast<uint8_t>(
      constrain(shifted >= 0 ? shifted : -shifted, 0, 255));
}

}  // namespace

AudioSpectrum* AudioSpectrum::active_ = nullptr;

bool AudioSpectrum::begin() {
  if (initialized_) return true;
  for (size_t index = 0; index < kFftSize; ++index) {
    window_[index] =
        0.5f * (1.0f - cosf(2.0f * M_PI * index / (kFftSize - 1)));
  }
  active_ = this;
  if (xTaskCreatePinnedToCore(taskEntry, "spectrum", 4096, this, 0, &task_,
                              0) != pdPASS) {
    active_ = nullptr;
    task_ = nullptr;
    return false;
  }
  initialized_ = true;
  Serial.println("[spectrum] 12 savos analizator kesz");
  return true;
}

void AudioSpectrum::captureActive(const int32_t* samples,
                                  int16_t frameCount) {
  if (active_ && samples && frameCount > 0)
    active_->capture(samples, frameCount);
}

void AudioSpectrum::setEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (enabled) return;
  portENTER_CRITICAL(&stateMux_);
  for (BufferState& state : states_) {
    if (state != BufferState::Processing) state = BufferState::Free;
  }
  writePosition_ = 0;
  nextCaptureAt_ = 0;
  publishedBands_.fill(0);
  publishedLeft_ = 0;
  publishedRight_ = 0;
  portEXIT_CRITICAL(&stateMux_);
}

AudioLevels AudioSpectrum::snapshot() {
  AudioLevels result;
  portENTER_CRITICAL(&stateMux_);
  result.left = publishedLeft_;
  result.right = publishedRight_;
  result.bands = publishedBands_;
  result.spectrumValid = enabled_ && initialized_;
  portEXIT_CRITICAL(&stateMux_);
  return result;
}

void AudioSpectrum::capture(const int32_t* samples, int16_t frameCount) {
  if (!enabled_ || !initialized_ || !samples || frameCount <= 0) return;

  uint8_t leftPeak = 0;
  uint8_t rightPeak = 0;
  for (int16_t frame = 0; frame < frameCount; frame += 8) {
    leftPeak = max(leftPeak, absoluteLevel(samples[frame * 2]));
    rightPeak = max(rightPeak, absoluteLevel(samples[frame * 2 + 1]));
  }
  publishPeaks(leftPeak, rightPeak);

  if (static_cast<int32_t>(millis() - nextCaptureAt_) < 0) return;
  if (states_[writeBuffer_] != BufferState::Writing &&
      !selectWriteBuffer())
    return;

  for (int16_t frame = 0; frame < frameCount; ++frame) {
    const int32_t left = samples[frame * 2];
    const int32_t right = samples[frame * 2 + 1];
    const int32_t mono = (left >> 1) + (right >> 1);
    samples_[writeBuffer_][writePosition_++] =
        static_cast<int16_t>(mono >> 16);
    if (writePosition_ < kFftSize) continue;

    portENTER_CRITICAL(&stateMux_);
    states_[writeBuffer_] = BufferState::Ready;
    portEXIT_CRITICAL(&stateMux_);
    writePosition_ = 0;
    nextCaptureAt_ = millis() + kCaptureIntervalMs;
    break;
  }
}

void AudioSpectrum::taskEntry(void* parameter) {
  static_cast<AudioSpectrum*>(parameter)->taskLoop();
}

void AudioSpectrum::taskLoop() {
  while (true) {
    if (!enabled_) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    uint8_t index = 0;
    if (claimReadyBuffer(index)) {
      processBuffer(index);
      portENTER_CRITICAL(&stateMux_);
      states_[index] = BufferState::Free;
      portEXIT_CRITICAL(&stateMux_);
    }
    vTaskDelay(pdMS_TO_TICKS(8));
  }
}

bool AudioSpectrum::claimReadyBuffer(uint8_t& index) {
  bool found = false;
  portENTER_CRITICAL(&stateMux_);
  for (uint8_t candidate = 0; candidate < states_.size(); ++candidate) {
    if (states_[candidate] != BufferState::Ready) continue;
    states_[candidate] = BufferState::Processing;
    index = candidate;
    found = true;
    break;
  }
  portEXIT_CRITICAL(&stateMux_);
  return found;
}

void AudioSpectrum::processBuffer(uint8_t index) {
  float mean = 0.0f;
  for (int16_t sample : samples_[index]) mean += sample;
  mean /= kFftSize;

  for (size_t sample = 0; sample < kFftSize; ++sample) {
    work_[sample * 2] =
        ((samples_[index][sample] - mean) / 32768.0f) * window_[sample];
    work_[sample * 2 + 1] = 0.0f;
  }

  dsps_fft2r_fc32(work_.data(), kFftSize);
  dsps_bit_rev_fc32(work_.data(), kFftSize);
  dsps_cplx2reC_fc32(work_.data(), kFftSize);

  std::array<uint8_t, AudioLevels::kSpectrumBands> bands{};
  constexpr float normalization = 2.0f / kFftSize;
  for (size_t band = 0; band < bands.size(); ++band) {
    float power = 0.0f;
    uint16_t bins = 0;
    for (uint16_t bin = kBandEdges[band];
         bin < kBandEdges[band + 1]; ++bin) {
      const float real = work_[bin * 2];
      const float imaginary = work_[bin * 2 + 1];
      power += real * real + imaginary * imaginary;
      ++bins;
    }
    const float rms =
        bins ? sqrtf(power / bins) * normalization : 0.0f;
    const float db = 20.0f * log10f(rms + 1.0e-7f);
    const float normalized =
        constrain((db - kMinimumDb) / (kMaximumDb - kMinimumDb),
                  0.0f, 1.0f);
    bands[band] = static_cast<uint8_t>(lroundf(normalized * 255.0f));
  }

  portENTER_CRITICAL(&stateMux_);
  if (enabled_) publishedBands_ = bands;
  portEXIT_CRITICAL(&stateMux_);
}

bool AudioSpectrum::selectWriteBuffer() {
  bool selected = false;
  portENTER_CRITICAL(&stateMux_);
  for (uint8_t candidate = 0; candidate < states_.size(); ++candidate) {
    if (states_[candidate] != BufferState::Free) continue;
    states_[candidate] = BufferState::Writing;
    writeBuffer_ = candidate;
    writePosition_ = 0;
    selected = true;
    break;
  }
  portEXIT_CRITICAL(&stateMux_);
  return selected;
}

void AudioSpectrum::publishPeaks(uint8_t left, uint8_t right) {
  portENTER_CRITICAL(&stateMux_);
  // Az oszlop az aktuális audiocsomagot kövesse, ne az FFT 60 ms-os
  // ütemében, négyesével essen vissza. A külön kirajzolt peak jelző
  // továbbra is hosszú ideig megőrzi a valódi csúcsot.
  const auto follow = [](uint8_t value, uint8_t target) {
    if (target >= value) return target;
    const uint8_t difference = value - target;
    return static_cast<uint8_t>(
        max<int>(target, value - max<int>(20, difference / 2)));
  };
  publishedLeft_ = follow(publishedLeft_, left);
  publishedRight_ = follow(publishedRight_, right);
  portEXIT_CRITICAL(&stateMux_);
}
