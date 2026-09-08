#include "DisplayDevice.h"

#include "options.h"

DisplayDevice::DisplayDevice() {
  {
    auto config = bus_.config();
#if DISPLAY_PROFILE_AXS15231B
    config.spi_host = SPI3_HOST;
    // The legacy Lovyan framebuffer transport remains mode 0.  The upcoming
    // dedicated ESP-LCD adapter will own the reference driver's mode 3 path.
    config.spi_mode = 0;
    config.freq_write = 40000000;
    config.freq_read = 16000000;
    config.spi_3wire = false;
    config.use_lock = true;
    config.dma_channel = SPI_DMA_CH_AUTO;
    config.pin_sclk = TFT_SCK;
    config.pin_mosi = -1;
    config.pin_miso = -1;
    config.pin_dc = -1;
    config.pin_io0 = TFT_D0;
    config.pin_io1 = TFT_D1;
    config.pin_io2 = TFT_D2;
    config.pin_io3 = TFT_D3;
#else
    config.spi_host = SPI2_HOST;
    config.spi_mode = 0;
    config.freq_write = 40000000;
    config.freq_read = 16000000;
    config.spi_3wire = true;
    config.use_lock = true;
    config.dma_channel = SPI_DMA_CH_AUTO;
    config.pin_sclk = TFT_SCK;
    config.pin_mosi = TFT_MOSI;
#if TOUCH_ENABLED && TS_MODEL == TS_MODEL_XPT2046
    // GPIO13 fizikailag az XPT2046 MISO-ja. Az ILI9488 olvasasa tiltott.
    config.pin_miso = TS_MISO;
#else
    config.pin_miso = -1;
#endif
    config.pin_dc = TFT_DC;
#endif
    bus_.config(config);
    panel_.setBus(&bus_);
  }

  {
    auto config = panel_.config();
    config.pin_cs = TFT_CS;
    config.pin_rst = TFT_RST;
    config.pin_busy = -1;
#if DISPLAY_PROFILE_AXS15231B
    config.panel_width = 480;
    config.panel_height = 320;
    config.memory_width = 480;
    config.memory_height = 320;
    config.offset_x = 0;
    config.offset_y = 0;
    config.offset_rotation = 0;
    config.readable = false;
    config.invert = false;
    config.rgb_order = false;
    config.dlen_16bit = false;
    config.bus_shared = false;
#else
    config.panel_width = 320;
    config.panel_height = 480;
    config.memory_width = 320;
    config.memory_height = 480;
    config.offset_x = 0;
    config.offset_y = 0;
    config.offset_rotation = 0;
    config.readable = false;
    config.invert = false;
    config.rgb_order = false;
    config.dlen_16bit = false;
    config.bus_shared = true;
#endif
    panel_.config(config);
  }

#if TOUCH_ENABLED
  {
    auto config = touch_.config();
#if TS_MODEL == TS_MODEL_AXS15231B
    config.x_min = 0;
    config.x_max = 479;
    config.y_min = 0;
    config.y_max = 319;
    config.pin_int = TS_INT;
    config.pin_rst = TS_RST;
    config.bus_shared = false;
    config.offset_rotation = 0;
    config.i2c_port = 1;
    config.i2c_addr = 0x3B;
    config.pin_sda = TS_SDA;
    config.pin_scl = TS_SCL;
    config.freq = 400000;
#elif TS_MODEL == TS_MODEL_FT6X36
    config.x_min = 0;
    config.x_max = 319;
    config.y_min = 0;
    config.y_max = 479;
    config.pin_int = TS_INT;
    config.pin_rst = TS_RST;
    config.bus_shared = false;
    config.offset_rotation = 0;
    config.i2c_port = 0;
    config.i2c_addr = 0x38;
    config.pin_sda = TS_SDA;
    config.pin_scl = TS_SCL;
    config.freq = 400000;
#else
    config.x_min = 0;
    config.x_max = 4095;
    config.y_min = 0;
    config.y_max = 4095;
    config.pin_int = -1;
    config.bus_shared = true;
    config.offset_rotation = 0;
    config.spi_host = SPI2_HOST;
    config.freq = 1000000;
    config.pin_sclk = TFT_SCK;
    config.pin_mosi = TFT_MOSI;
    config.pin_miso = TS_MISO;
    config.pin_cs = TS_CS;
#endif
    touch_.config(config);
    panel_.setTouch(&touch_);
  }
#endif

  setPanel(&panel_);
}

void DisplayDevice::prepareTouch() {
#if TOUCH_ENABLED
  // A touch vezerlo konfiguracioja a konstruktorban keszul el;
  // az inicializalast a kesobbi LGFX_Device::init() vegzi.
#else
  // Nem erintos build: nincs touch vezerlo es nincs kalibracio.
#endif
}
void DisplayDevice::setAxsRuntimeDiagnostics(uint32_t bufferFilledBytes,
                                             uint8_t bufferPercent,
                                             uint8_t cpu0Percent,
                                             uint8_t cpu1Percent,
                                             bool cpuValid) {
#if DISPLAY_PROFILE_AXS15231B
  panel_.setRuntimeDiagnostics(bufferFilledBytes, bufferPercent, cpu0Percent,
                               cpu1Percent, cpuValid);
#else
  (void)bufferFilledBytes;
  (void)bufferPercent;
  (void)cpu0Percent;
  (void)cpu1Percent;
  (void)cpuValid;
#endif
}

void DisplayDevice::noteAxsLvglFlushArea(int32_t x1, int32_t y1, int32_t x2,
                                         int32_t y2, uint32_t pixels) {
#if DISPLAY_PROFILE_AXS15231B
  panel_.noteLvglFlushArea(x1, y1, x2, y2, pixels);
#else
  (void)x1;
  (void)y1;
  (void)x2;
  (void)y2;
  (void)pixels;
#endif
}

bool DisplayDevice::beginAxsFactoryTransport() {
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  return axsFactoryTransport_.begin(panel_.factoryTransportBuffer(),
                                    panel_.factoryTransportBufferPixels());
#else
  return true;
#endif
}

void DisplayDevice::setAxsFactoryColorInverted(bool inverted) {
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  axsFactoryTransport_.setColorInverted(inverted);
#else
  (void)inverted;
#endif
}

void DisplayDevice::setAxsFactoryScreenFlipped(bool flipped) {
#if DISPLAY_PROFILE_AXS15231B && AXS_FACTORY_DIRECT_LVGL
  axsFactoryTransport_.setScreenFlipped(flipped);
#else
  (void)flipped;
#endif
}

bool DisplayDevice::flushAxsLvglRect(int32_t x, int32_t y, int32_t w,
                                      int32_t h, const uint16_t* pixels) {
#if DISPLAY_PROFILE_AXS15231B
#if AXS_FACTORY_DIRECT_LVGL
  // The full LVGL frame is native little-endian RGB565.  The factory panel
  // transport must convert it to the panel's wire order exactly once.
  return axsFactoryTransport_.flushFactoryRotated(x, y, w, h, pixels, false);
#else
  return panel_.flushFactoryLvglRect(x, y, w, h, pixels);
#endif
#else
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)pixels;
  return false;
#endif
}

bool DisplayDevice::flushAxsVuFrame(const uint16_t* pixels) {
#if DISPLAY_PROFILE_AXS15231B
#if AXS_FACTORY_DIRECT_LVGL
  if (!pixels) return false;
  // The AXS factory QSPI path is demonstrably correct only when its pixel
  // stream has the complete 320-row source height.  This contains the left
  // 240 logical columns; its 62 VU rows are updated by DisplayManager while
  // all other rows are retained from the last ordinary LVGL frame.
  return axsFactoryTransport_.flushFactoryRotated(0, 0, 240, 320, pixels,
                                                   false);
#else
  (void)pixels;
  return false;
#endif
#else
  (void)pixels;
  return false;
#endif
}
