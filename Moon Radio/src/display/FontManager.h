#pragma once

#include <lvgl.h>

class FontManager {
 public:
  void begin();

  const lv_font_t* small() const;
  const lv_font_t* regular() const;
  const lv_font_t* large() const;
  const lv_font_t* symbol() const;
  const lv_font_t* compact() const;

 private:
  const lv_font_t* font14_{nullptr};
  const lv_font_t* font20_{nullptr};
  const lv_font_t* font28_{nullptr};
  const lv_font_t* symbol24_{nullptr};
  const lv_font_t* compact14_{nullptr};
};
