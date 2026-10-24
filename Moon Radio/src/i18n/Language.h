#pragma once
#include <Arduino.h>
namespace Language {
enum class Code : uint8_t { HU, EN, DE, PL };
Code current();
const char* defaultCode();
const char* code();
bool set(String value);
// Translate firmware-owned Hungarian UI text. Unknown strings pass through.
const char* text(const char* source);
}
