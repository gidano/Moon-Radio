#include <Arduino.h>

#include "app/RadioController.h"

RadioController radio;

void setup() { radio.begin(); }

void loop() { radio.loop(); }
