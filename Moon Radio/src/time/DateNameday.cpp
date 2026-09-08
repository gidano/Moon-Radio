#include "DateNameday.h"

#include "namedays_HU.h"

namespace DateNameday {
namespace {

constexpr const char* kWeekdays[] = {
    "vasárnap", "hétfő", "kedd", "szerda",
    "csütörtök", "péntek", "szombat"};

constexpr const char* kMonths[] = {
    "január",   "február", "március",   "április",
    "május",    "június",  "július",    "augusztus",
    "szeptember", "október", "november", "december"};

constexpr uint16_t kMonthOffsets[] = {
    0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

const char* getNameday(uint8_t month, uint8_t day) {
  if (month < 1 || month > 12 || day < 1 || day > 31) return "";
  const uint16_t index = kMonthOffsets[month - 1] + (day - 1);
  const uint16_t count = sizeof(namedays) / sizeof(namedays[0]);
  if (index >= count) return "";
  return namedays[index] ? namedays[index] : "";
}

}  // namespace

String formatDateWithNameday(const tm& localTime) {
  String text = formatDatePart(localTime);
  const String nameday = namedayText(localTime);
  if (!nameday.isEmpty()) {
    text += "  #FFD54A ";
    text += nameday;
    text += "#";
  }
  return text;
}

String formatDatePart(const tm& localTime) {
  char datePart[64];
  snprintf(datePart, sizeof(datePart), "%04d.%02d. %d. %s",
           localTime.tm_year + 1900, localTime.tm_mon + 1,
           localTime.tm_mday, kWeekdays[localTime.tm_wday]);
  return String(datePart);
}

String namedayText(const tm& localTime) {
  const char* nameday = getNameday(static_cast<uint8_t>(localTime.tm_mon + 1),
                                   static_cast<uint8_t>(localTime.tm_mday));
  return (nameday && nameday[0]) ? String(nameday) : String();
}

}  // namespace DateNameday
