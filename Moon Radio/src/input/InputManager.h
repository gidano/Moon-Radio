#pragma once

#include <Arduino.h>

struct InputActions {
  void* context{nullptr};
  void (*stationPrevious)(void*){nullptr};
  void (*stationNext)(void*){nullptr};
  void (*primaryEncoderLeft)(void*){nullptr};
  void (*primaryEncoderRight)(void*){nullptr};
  void (*togglePlayback)(void*){nullptr};
  void (*stationLongPress)(void*){nullptr};
  void (*volumeDown)(void*){nullptr};
  void (*volumeUp)(void*){nullptr};
};

class InputManager {
 public:
  void begin(const InputActions& actions);
  void loop();

 private:
  struct Encoder {
    uint8_t pinA;
    uint8_t pinB;
    uint8_t pinButton;
    uint8_t previousAB{0};
    int8_t accumulator{0};
    bool rawButton{true};
    bool stableButton{true};
    bool longPressFired{false};
    uint32_t buttonChangedAt{0};
  };

  void updateEncoder(Encoder& encoder, void (*left)(void*),
                     void (*right)(void*), void (*pressed)(void*),
                     void (*longPressed)(void*) = nullptr);

  InputActions actions_;
  Encoder stationEncoder_{0, 0, 0};
  Encoder volumeEncoder_{0, 0, 0};
};
