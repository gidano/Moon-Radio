#pragma once

#include <Arduino.h>
#include <time.h>

namespace DateNameday {

String formatDateWithNameday(const tm& localTime);
String formatDatePart(const tm& localTime);
String namedayText(const tm& localTime);

}
