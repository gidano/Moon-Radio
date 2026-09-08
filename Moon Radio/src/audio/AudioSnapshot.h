#pragma once

#include <Arduino.h>
#include <array>

struct AudioSnapshot {
  String stationName;
  String streamTitle;
  String codec;
  String statusText;
  String stateCode;
  uint32_t bitrateKbps{0};
  uint32_t sampleRate{0};
  uint8_t bitsPerSample{0};
  uint8_t channels{0};
  uint8_t volume{8};
  uint8_t bufferPercent{0};
  uint32_t bufferFilledBytes{0};
  bool paused{false};
  bool initialized{false};
  bool running{false};
  bool connecting{false};
  bool commandQueued{false};
  bool connectAttempted{false};
  bool tcpConnected{false};
};

struct AudioLevels {
  static constexpr size_t kSpectrumBands = 12;

  uint8_t left{0};
  uint8_t right{0};
  std::array<uint8_t, kSpectrumBands> bands{};
  bool spectrumValid{false};
};
