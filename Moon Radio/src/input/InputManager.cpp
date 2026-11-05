#include "InputManager.h"

#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <freertos/FreeRTOS.h>

#include "options.h"

namespace {

DRAM_ATTR const int8_t kQuadratureTable[16] = {
    0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
};
constexpr uint32_t kButtonDebounceMs = 35;
constexpr uint32_t kButtonLongPressMs = 850;
// A jelenlegi encoder mechanikai sorrendje a készülékben fordított a
// dekóder által feltételezetthez képest. A pinekhez nem kell nyúlni: a
// quadrature-jel előjelét fordítjuk meg egyetlen helyen.
constexpr bool kReverseEncoderDirection = true;
portMUX_TYPE kEncoderMux = portMUX_INITIALIZER_UNLOCKED;

uint8_t readAB(uint8_t pinA, uint8_t pinB) {
  return (digitalRead(pinA) ? 2U : 0U) | (digitalRead(pinB) ? 1U : 0U);
}

void configureEncoderPin(uint8_t pin, bool button = false) {
#if defined(ENC_INTERNALPULLUP)
  pinMode(pin, INPUT_PULLUP);
#else
  // The encoder button is active-low and must not be left floating.  The
  // rotation pins may use an external bias, but both encoder buttons need a
  // stable idle HIGH level.
  pinMode(pin, button ? INPUT_PULLUP : INPUT);
#endif
}

static inline uint8_t IRAM_ATTR readGpioInIsr(uint8_t pin) {
  if (pin < 32) return (GPIO.in & (1UL << pin)) ? 1U : 0U;
  return (GPIO.in1.val & (1UL << (pin - 32))) ? 1U : 0U;
}

}  // namespace

void IRAM_ATTR InputManager::encoderInterrupt(void* argument) {
  auto* encoder = static_cast<Encoder*>(argument);
  if (!encoder) return;
  portENTER_CRITICAL_ISR(&kEncoderMux);
  const uint8_t currentAB =
      (readGpioInIsr(encoder->pinA) ? 2U : 0U) |
      (readGpioInIsr(encoder->pinB) ? 1U : 0U);
  if (currentAB != encoder->interruptPreviousAB) {
    const uint8_t transition =
        (encoder->interruptPreviousAB << 2) | currentAB;
    const int8_t delta = kQuadratureTable[transition & 0x0F];
    encoder->interruptAccumulator += -delta;
    encoder->interruptPreviousAB = currentAB;
  }
  portEXIT_CRITICAL_ISR(&kEncoderMux);
}

void InputManager::begin(const InputActions& actions) {
  actions_ = actions;
#if defined(ENC_BTNL) && defined(ENC_BTNR) && defined(ENC_BTNB)
  stationEncoder_ = {ENC_BTNL, ENC_BTNR, ENC_BTNB};
  configureEncoderPin(stationEncoder_.pinA);
  configureEncoderPin(stationEncoder_.pinB);
  configureEncoderPin(stationEncoder_.pinButton, true);
  stationEncoder_.previousAB = readAB(stationEncoder_.pinA, stationEncoder_.pinB);
#if defined(ENC_USE_INTERRUPT)
  stationEncoder_.interruptPreviousAB = stationEncoder_.previousAB;
  stationEncoder_.interruptDriven = true;
  attachInterruptArg(stationEncoder_.pinA, encoderInterrupt,
                     &stationEncoder_, CHANGE);
  attachInterruptArg(stationEncoder_.pinB, encoderInterrupt,
                     &stationEncoder_, CHANGE);
#else
  stationEncoder_.interruptDriven = false;
#endif
  stationEncoder_.rawButton = digitalRead(stationEncoder_.pinButton);
  stationEncoder_.stableButton = stationEncoder_.rawButton;
  stationEncoder_.buttonChangedAt = millis();
#endif

#if defined(ENC2_BTNL) && defined(ENC2_BTNR) && defined(ENC2_BTNB)
  volumeEncoder_ = {ENC2_BTNL, ENC2_BTNR, ENC2_BTNB};
  configureEncoderPin(volumeEncoder_.pinA);
  configureEncoderPin(volumeEncoder_.pinB);
  configureEncoderPin(volumeEncoder_.pinButton, true);
  volumeEncoder_.previousAB = readAB(volumeEncoder_.pinA, volumeEncoder_.pinB);
#if defined(ENC2_USE_INTERRUPT)
  volumeEncoder_.interruptPreviousAB = volumeEncoder_.previousAB;
  volumeEncoder_.interruptDriven = true;
  attachInterruptArg(volumeEncoder_.pinA, encoderInterrupt,
                     &volumeEncoder_, CHANGE);
  attachInterruptArg(volumeEncoder_.pinB, encoderInterrupt,
                     &volumeEncoder_, CHANGE);
#else
  volumeEncoder_.interruptDriven = false;
#endif
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
                actions_.togglePlayback, actions_.stationLongPress);
#elif defined(ENC_BTNL) && defined(ENC_BTNR) && defined(ENC_BTNB)
  updateEncoder(stationEncoder_, actions_.primaryEncoderLeft,
                actions_.primaryEncoderRight, actions_.togglePlayback,
                actions_.stationLongPress);
#elif defined(ENC2_BTNL) && defined(ENC2_BTNR) && defined(ENC2_BTNB)
  updateEncoder(volumeEncoder_, actions_.volumeDown, actions_.volumeUp,
                actions_.togglePlayback, actions_.stationLongPress);
#endif
}

void InputManager::updateEncoder(Encoder& encoder, void (*left)(void*),
                                 void (*right)(void*),
                                 void (*pressed)(void*),
                                 void (*longPressed)(void*)) {
  if (encoder.interruptDriven) {
    int16_t accumulated = 0;
    portENTER_CRITICAL(&kEncoderMux);
    accumulated = encoder.interruptAccumulator;
    encoder.interruptAccumulator = 0;
    portEXIT_CRITICAL(&kEncoderMux);
    while (accumulated <= -4) {
      accumulated += 4;
      if (left) left(actions_.context);
    }
    while (accumulated >= 4) {
      accumulated -= 4;
      if (right) right(actions_.context);
    }
  } else {
    const uint8_t currentAB = readAB(encoder.pinA, encoder.pinB);
    if (currentAB != encoder.previousAB) {
      const uint8_t transition = (encoder.previousAB << 2) | currentAB;
      const int8_t transitionDelta = kQuadratureTable[transition & 0x0F];
      encoder.accumulator += kReverseEncoderDirection ? -transitionDelta
                                                      : transitionDelta;
      encoder.previousAB = currentAB;

      if (encoder.accumulator <= -4) {
        encoder.accumulator = 0;
        if (left) left(actions_.context);
      } else if (encoder.accumulator >= 4) {
        encoder.accumulator = 0;
        if (right) right(actions_.context);
      }
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
