#include "FontManager.h"

#include <Arduino.h>

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_roboto_hu_20);
LV_FONT_DECLARE(lv_font_roboto_hu_28);

void FontManager::begin() {
  // Szabály: minden emberi olvasásra szánt UI-szöveg a magyar Roboto
  // készletből jön. A "small" is ezt aliasolja, hogy ne legyenek rejtett
  // ékezet-hiányok. Külön font csak ikonokhoz/szimbólumokhoz marad.
  font14_ = &lv_font_roboto_hu_20;
  font20_ = &lv_font_roboto_hu_20;
  font28_ = &lv_font_roboto_hu_28;
  symbol24_ = &lv_font_montserrat_24;
  compact14_ = &lv_font_montserrat_14;
  Serial.println("[display] Firmware fontok hasznalatban");
}

const lv_font_t* FontManager::small() const { return font14_; }

const lv_font_t* FontManager::regular() const { return font20_; }

const lv_font_t* FontManager::large() const { return font28_; }

const lv_font_t* FontManager::symbol() const { return symbol24_; }

const lv_font_t* FontManager::compact() const { return compact14_; }
