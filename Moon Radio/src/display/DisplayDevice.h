#pragma once

#include "options.h"

#if DISPLAY_PROFILE_AXS15231B
#ifndef LGFX_USE_QSPI
#define LGFX_USE_QSPI
#endif
#endif

#include <LovyanGFX.hpp>

#if DISPLAY_PROFILE_AXS15231B
#include "lgfx/axs_factory_transport.h"
#include "lgfx/panel_axs15231b.h"
#include "lgfx/touch_axs15231b.h"
#endif

class DisplayDevice : public lgfx::LGFX_Device {
 public:
  DisplayDevice();
  void prepareTouch();
  void setAxsRuntimeDiagnostics(uint32_t bufferFilledBytes,
                                uint8_t bufferPercent,
                                uint8_t cpu0Percent,
                                uint8_t cpu1Percent,
                                bool cpuValid);
  void noteAxsLvglFlushArea(int32_t x1, int32_t y1, int32_t x2,
                            int32_t y2, uint32_t pixels);
  bool beginAxsFactoryTransport();
  void setAxsFactoryColorInverted(bool inverted);
  void setAxsFactoryScreenFlipped(bool flipped);
  bool flushAxsLvglRect(int32_t x, int32_t y, int32_t w, int32_t h,
                        const uint16_t* pixels);
  // Sends the left 240 logical columns using the factory's complete 320-row
  // raster. The supplied image contains the retained LVGL pixels plus VU.
  bool flushAxsVuFrame(const uint16_t* pixels);

 private:
#if DISPLAY_PROFILE_AXS15231B
  lgfx::Panel_AXS15231B panel_;
  AxsFactoryTransport axsFactoryTransport_;
#elif DISPLAY_PROFILE_ST7796
  lgfx::Panel_ST7796 panel_;
#else
  lgfx::Panel_ILI9488 panel_;
#endif
  lgfx::Bus_SPI bus_;
#if TOUCH_ENABLED && TS_MODEL == TS_MODEL_AXS15231B
  lgfx::Touch_AXS15231B touch_;
#elif TOUCH_ENABLED && TS_MODEL == TS_MODEL_FT6X36
  lgfx::Touch_FT5x06 touch_;
#elif TOUCH_ENABLED
  lgfx::Touch_XPT2046 touch_;
#endif
};
