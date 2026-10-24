#include "DateNameday.h"

#include "namedays_HU.h"
#include "namedays_DE.h"
#include "namedays_PL.h"
#include "nameday_EN.h"
#include "../i18n/Language.h"

namespace DateNameday {
namespace {

constexpr const char* kWeekdays[4][7] = {
  {"vasárnap", "hétfő", "kedd", "szerda", "csütörtök", "péntek", "szombat"},
  {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"},
  {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"},
  {"niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota"}
};
static_assert(sizeof(namedaysHU) / sizeof(*namedaysHU) == 366);
static_assert(sizeof(namedaysEN) / sizeof(*namedaysEN) == 366);
static_assert(sizeof(namedaysDE) / sizeof(*namedaysDE) == 366);
static_assert(sizeof(namedaysPL) / sizeof(*namedaysPL) == 366);

constexpr uint16_t kMonthOffsets[] = {
    0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

const char* getNameday(uint8_t month, uint8_t day) {
  if (month < 1 || month > 12 || day < 1 || day > 31) return "";
  constexpr uint8_t daysInMonth[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > daysInMonth[month - 1]) return "";
  const uint16_t index = kMonthOffsets[month - 1] + (day - 1);
  const char* const* tables[] = {namedaysHU, namedaysEN, namedaysDE, namedaysPL};
  const auto namedays = tables[static_cast<uint8_t>(Language::current())];
  constexpr uint16_t count = 366;
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
  if (localTime.tm_wday < 0 || localTime.tm_wday > 6 ||
      localTime.tm_mon < 0 || localTime.tm_mon > 11) return String();
  char datePart[64];
  const auto lang = Language::current();
  const char* weekday = kWeekdays[static_cast<uint8_t>(lang)][localTime.tm_wday % 7];
  if (lang == Language::Code::HU)
    snprintf(datePart, sizeof(datePart), "%04d.%02d. %d. %s", localTime.tm_year + 1900,
             localTime.tm_mon + 1, localTime.tm_mday, weekday);
  else
    snprintf(datePart, sizeof(datePart), "%02d.%02d.%04d %s", localTime.tm_mday,
             localTime.tm_mon + 1, localTime.tm_year + 1900, weekday);
  return String(datePart);
}

String namedayText(const tm& localTime) {
  const char* nameday = getNameday(static_cast<uint8_t>(localTime.tm_mon + 1),
                                   static_cast<uint8_t>(localTime.tm_mday));
  return (nameday && nameday[0]) ? String(nameday) : String();
}

}  // namespace DateNameday
