#include "InputManager.h"

#include "options.h"

namespace {

constexpr int8_t kQuadratureTable[16] = {
    0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
};
constexpr uint32_t kButtonDebounceMs = 35;
constexpr uint32_t kButtonLongPressMs = 850;

uint8_t readAB(uint8_t pinA, uint8_t pinB) {
  return (digitalRead(pinA) ? 2U : 0U) | (digitalRead(pinB) ? 1U : 0U);
}

}  // namespace

void InputManager::begin(const InputActions& actions) {
  actions_ = actions;
#if defined(ENC_BTNL) && defined(ENC_BTNR) && defined(ENC_BTNB)
  stationEncoder_ = {ENC_BTNL, ENC_BTNR, ENC_BTNB};
  pinMode(stationEncoder_.pinA, INPUT_PULLUP);
  pinMode(stationEncoder_.pinB, INPUT_PULLUP);
  pinMode(stationEncoder_.pinButton, INPUT_PULLUP);
  stationEncoder_.previousAB = readAB(stationEncoder_.pinA, stationEncoder_.pinB);
  stationEncoder_.rawButton = digitalRead(stationEncoder_.pinButton);
  stationEncoder_.stableButton = stationEncoder_.rawButton;
  stationEncoder_.buttonChangedAt = millis();
#endif

#if defined(ENC2_BTNL) && defined(ENC2_BTNR) && defined(ENC2_BTNB)
  volumeEncoder_ = {ENC2_BTNL, ENC2_BTNR, ENC2_BTNB};
  pinMode(volumeEncoder_.pinA, INPUT_PULLUP);
  pinMode(volumeEncoder_.pinB, INPUT_PULLUP);
  pinMode(volumeEncoder_.pinButton, INPUT_PULLUP);
  volumeEncoder_.previousAB = readAB(volumeEncoder_.pinA, volumeEncoder_.pinB);
  volumeEncoder_.rawButton = digitalRead(volumeEncoder_.pinButton);
  volumeEncoder_.stableButton = volumeEncoder_.rawButton;
  volumeEncoder_.buttonChangedAt = millis();
#endif
}

void InputManager::loop() {
#if defined(ENC_BTNL) && defined(ENC_BTNR) && defined(ENC_BTNB) && \
    defined(ENC2_BTNL) && defined(ENC2_BTNR) && defined(ENC2_BTNB)
  updateEncoder(stationEncoder_, actions_.stationPrevious,
                actions_.stationNext, actions_.togglePlayback,
                actions_.stationLongPress);
  updateEncoder(volumeEncoder_, actions_.volumeDown, actions_.volumeUp,
                actions_.togglePlayback);
#elif defined(ENC_BTNL) && defined(ENC_BTNR) && defined(ENC_BTNB)
  updateEncoder(stationEncoder_, actions_.primaryEncoderLeft,
                actions_.primaryEncoderRight, actions_.togglePlayback,
                actions_.stationLongPress);
#elif defined(ENC2_BTNL) && defined(ENC2_BTNR) && defined(ENC2_BTNB)
  updateEncoder(volumeEncoder_, actions_.volumeDown, actions_.volumeUp,
                actions_.togglePlayback);
#endif
}

void InputManager::updateEncoder(Encoder& encoder, void (*left)(void*),
                                 void (*right)(void*),
                                 void (*pressed)(void*),
                                 void (*longPressed)(void*)) {
  const uint8_t currentAB = readAB(encoder.pinA, encoder.pinB);
  if (currentAB != encoder.previousAB) {
    const uint8_t transition = (encoder.previousAB << 2) | currentAB;
    encoder.accumulator += kQuadratureTable[transition & 0x0F];
    encoder.previousAB = currentAB;

    if (encoder.accumulator <= -4) {
      encoder.accumulator = 0;
      if (left) left(actions_.context);
    } else if (encoder.accumulator >= 4) {
      encoder.accumulator = 0;
      if (right) right(actions_.context);
    }
  }

  const bool rawButton = digitalRead(encoder.pinButton);
  const uint32_t now = millis();
  if (rawButton != encoder.rawButton) {
    encoder.rawButton = rawButton;
    encoder.buttonChangedAt = now;
  }
  if (rawButton != encoder.stableButton &&
      now - encoder.buttonChangedAt >= kButtonDebounceMs) {
    encoder.stableButton = rawButton;
    if (!encoder.stableButton) {
      encoder.longPressFired = false;
      if (!longPressed && pressed) pressed(actions_.context);
    } else if (longPressed && !encoder.longPressFired && pressed) {
      pressed(actions_.context);
    }
  }

  if (longPressed && !encoder.stableButton && !encoder.longPressFired &&
      now - encoder.buttonChangedAt >= kButtonLongPressMs) {
    encoder.longPressFired = true;
    longPressed(actions_.context);
  }
}
