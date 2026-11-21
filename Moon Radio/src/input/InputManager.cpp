#include "InputManager.h"

#include <driver/gpio.h>
#include <esp_intr_alloc.h>
#include <soc/interrupts.h>
#include <freertos/FreeRTOS.h>

#include "options.h"
#include <Rotary.h>

namespace {

constexpr uint32_t kButtonDebounceMs = 35;
constexpr uint32_t kButtonLongPressMs = 850;
portMUX_TYPE kEncoderMux = portMUX_INITIALIZER_UNLOCKED;

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

}  // namespace

void IRAM_ATTR InputManager::encoderInterrupt(void* argument) {
  auto* encoder = static_cast<Encoder*>(argument);
  if (!encoder) return;
  portENTER_CRITICAL_ISR(&kEncoderMux);
  const unsigned char direction = encoder->rotary->process();
  // Map the physical encoder direction to Moon Radio's left/right actions.
  if (direction == DIR_CW) encoder->interruptAccumulator += 4;
  else if (direction == DIR_CCW) encoder->interruptAccumulator -= 4;
  portEXIT_CRITICAL_ISR(&kEncoderMux);
}

void InputManager::begin(const InputActions& actions) {
  actions_ = actions;
#if DISPLAY_PROFILE_AXS15231B && (defined(ENC_USE_INTERRUPT) || defined(ENC2_USE_INTERRUPT))
  // IDF registers the GPIO service on the small IPC stack. Preallocate the
  // selected vector descriptor on this task's stack; IDF retains it on free.
  intr_handle_t gpioVector = nullptr;
  esp_err_t vectorResult = esp_intr_alloc(ETS_GPIO_INTR_SOURCE,
      ARDUINO_ISR_FLAG | ESP_INTR_FLAG_INTRDISABLED, nullptr, nullptr, &gpioVector);
  if (vectorResult == ESP_OK) vectorResult = esp_intr_free(gpioVector);
  if (vectorResult != ESP_OK) {
    Serial.printf("[input] GPIO vector preparation failed: %d\n", vectorResult);
  }
#endif
#if defined(ENC_BTNL) && defined(ENC_BTNR) && defined(ENC_BTNB)
  stationEncoder_ = {ENC_BTNL, ENC_BTNR, ENC_BTNB};
  static Rotary stationRotary(ENC_BTNL, ENC_BTNR);
  stationEncoder_.rotary = &stationRotary;
#if defined(ENC_INTERNALPULLUP)
  stationRotary.begin(true);
#else
  stationRotary.begin(false);
#endif
  Serial.println("[input] brianlow Rotary encoder aktiv");
  configureEncoderPin(stationEncoder_.pinA);
  configureEncoderPin(stationEncoder_.pinB);
  configureEncoderPin(stationEncoder_.pinButton, true);
#if defined(ENC_USE_INTERRUPT)
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
  static Rotary volumeRotary(ENC2_BTNL, ENC2_BTNR);
  volumeEncoder_.rotary = &volumeRotary;
#if defined(ENC_INTERNALPULLUP)
  volumeRotary.begin(true);
#else
  volumeRotary.begin(false);
#endif
  configureEncoderPin(volumeEncoder_.pinA);
  configureEncoderPin(volumeEncoder_.pinB);
  configureEncoderPin(volumeEncoder_.pinButton, true);
#if defined(ENC2_USE_INTERRUPT)
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
    const unsigned char direction = encoder.rotary->process();
    if (direction == DIR_CCW && left) left(actions_.context);
    else if (direction == DIR_CW && right) right(actions_.context);
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
