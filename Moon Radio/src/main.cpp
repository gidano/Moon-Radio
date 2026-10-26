#include <Arduino.h>

#include "app/RadioController.h"
#include "options.h"

RadioController radio;

void setup() {
#if defined(SDC_CS) && defined(SD_SPIPINS)
  // The ILI9488 SD socket has no guaranteed external CS pull-up.  Deselect
  // the card before filesystem, display and Serial startup can leave it
  // floating for several seconds.  Keep the shared return line defined too.
  const int sdPins[] = {SD_SPIPINS};  // SCK, MISO, MOSI, CS
  pinMode(SDC_CS, OUTPUT);
  digitalWrite(SDC_CS, HIGH);
  pinMode(sdPins[1], INPUT_PULLUP);
#endif
  radio.begin();
}

void loop() { radio.loop(); }
