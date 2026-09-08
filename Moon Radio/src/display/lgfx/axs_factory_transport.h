#pragma once

#include <Arduino.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "options.h"

// AXS15231B transport copied from Guition's esp_lcd_axs15231b driver.
// It intentionally uses ESP-LCD's panel IO rather than the generic LovyanGFX
// SPI writer: the AXS QSPI command prefix is part of the controller protocol.
class AxsFactoryTransport {
 public:
  bool begin(uint16_t* staging, size_t stagingPixels) {
    if (ready_) return true;
    if (!staging || stagingPixels == 0) return false;

    esp_lcd_panel_io_spi_config_t config = {};
    config.cs_gpio_num = TFT_CS;
    config.dc_gpio_num = -1;
    config.spi_mode = 3;
    config.pclk_hz = 40000000;
    config.trans_queue_depth = 1;
    // Match Guition's documented transport ownership: a staging buffer is
    // reused only after ESP-LCD signals the preceding colour DMA completion.
    config.on_color_trans_done = onColorTransferDone;
    config.user_ctx = this;
    config.lcd_cmd_bits = 32;
    config.lcd_param_bits = 8;
    config.flags.quad_mode = 1;

    const esp_err_t err = esp_lcd_new_panel_io_spi(
        static_cast<spi_host_device_t>(SPI3_HOST), &config, &io_);
    if (err != ESP_OK) {
      Serial.printf("[display] AXS gyari panel-IO hiba: %d\n", err);
      return false;
    }

    colorDone_ = xSemaphoreCreateBinary();
    if (!colorDone_) {
      Serial.println("[display] AXS DMA szinkron memoria hiba");
      esp_lcd_panel_io_del(io_);
      io_ = nullptr;
      return false;
    }
    transportMutex_ = xSemaphoreCreateMutex();
    if (!transportMutex_) {
      Serial.println("[display] AXS atvitelzar memoria hiba");
      vSemaphoreDelete(colorDone_);
      colorDone_ = nullptr;
      esp_lcd_panel_io_del(io_);
      io_ = nullptr;
      return false;
    }

    // A panel-IO transport is not enough on its own.  The former attempts
    // kept Moon's Lovyan initialization and therefore addressed a different
    // AXS state.  Reproduce the reference driver lifecycle here, before any
    // LVGL buffer is allowed to reach the panel.
    if (!initializeFactoryPanel()) {
      Serial.println("[display] AXS gyari inicializalas hiba");
      vSemaphoreDelete(transportMutex_);
      transportMutex_ = nullptr;
      vSemaphoreDelete(colorDone_);
      colorDone_ = nullptr;
      esp_lcd_panel_io_del(io_);
      io_ = nullptr;
      return false;
    }

    staging_ = staging;
    stagingPixels_ = stagingPixels;
    // initializeFactoryPanel has already installed the persisted MADCTL and
    // inversion state while output was off.  Keep the panel dark until a
    // complete, correctly oriented LVGL frame is resident in GRAM.
    ready_ = true;
    Serial.println("[display] AXS gyari ESP-LCD QSPI kep-ut aktiv");
    return true;
  }

  void setColorInverted(bool inverted) {
    colorInverted_ = inverted;
    if (ready_ && !applyColorInversion()) fail("szininverzio");
  }

  void setScreenFlipped(bool flipped) {
    screenFlipped_ = flipped;
    if (ready_ && !applyOrientation()) fail("elforgatas");
  }

  bool flushFactoryRotated(int32_t x, int32_t y, int32_t w, int32_t h,
                           const uint16_t* source,
                           bool sourceIsWireRgb565 = false) {
    // ESP-LCD serializes individual DMA descriptors, but it does not make a
    // CASET + RAMWR/RAMWRC *sequence* atomic. LVGL and the retained VU frame
    // can arrive from different execution paths, so protect the whole stream.
    if (!transportMutex_ ||
        xSemaphoreTake(transportMutex_, pdMS_TO_TICKS(250)) != pdTRUE) {
      return false;
    }
    const bool result = flushFactoryRotatedLocked(
        x, y, w, h, source, sourceIsWireRgb565);
    xSemaphoreGive(transportMutex_);
    return result;
  }

 private:
  bool flushFactoryRotatedLocked(int32_t x, int32_t y, int32_t w, int32_t h,
                                 const uint16_t* source,
                                 bool sourceIsWireRgb565) {
    // This is the LV_DISP_ROT_90 branch from Guition's lv_port.c, including
    // the matching esp_lcd_panel_draw_bitmap() coordinates.  The vendor QSPI
    // driver uses a CASET-only sequential source stream.  Both normal LVGL
    // and the retained VU frame preserve that validated geometry.
    if (!ready_ || faulted_ || !source || x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > kLogicalWidth || y + h > kLogicalHeight) {
      return false;
    }

    const int32_t maxColumns = static_cast<int32_t>(stagingPixels_ / h);
    if (maxColumns <= 0) return false;

    bool hasQueuedColor = false;
    for (int32_t blockStart = 0; blockStart < w;
         blockStart += maxColumns) {
      if (hasQueuedColor && !waitForQueuedColor()) {
        fail("DMA szinkron");
        return false;
      }
      const int32_t columnsInBlock = min(maxColumns, w - blockStart);
      const int32_t sourceX = x + blockStart;
      // Exact LV_DISP_ROT_90 packing from Guition's lv_port.c.
      for (int32_t yy = 0; yy < h; ++yy) {
        const uint16_t* row = source + static_cast<size_t>(yy) * w + blockStart;
        for (int32_t xx = 0; xx < columnsInBlock; ++xx) {
          // ESP-LCD transmits the color buffer byte-for-byte.  Guition's
          // LVGL configuration supplies RGB565 in wire (big-endian) order;
          // Moon's normal LVGL framebuffer is native little-endian RGB565.
          // Convert only the temporary DMA block so every normal screen and
          // the web-controlled colour inversion remain untouched.
          const uint16_t color = row[xx];
          staging_[xx * h + (h - yy - 1)] =
              sourceIsWireRgb565 ? color : __builtin_bswap16(color);
        }
      }
      const int32_t xDrawStart = kLogicalHeight - y - h;
      const int32_t xDrawEnd = kLogicalHeight - y - 1;
      const uint8_t columns[] = {
          static_cast<uint8_t>(xDrawStart >> 8),
          static_cast<uint8_t>(xDrawStart),
          static_cast<uint8_t>(xDrawEnd >> 8),
          static_cast<uint8_t>(xDrawEnd),
      };
      // This is the factory QSPI stream: each internally DMA-safe block has
      // its own CASET and RAMWR/RAMWRC command.  Commandless continuation
      // was rejected by this panel after the first 66 columns.
      if (!sendParam(kCmdCaseT, columns, sizeof(columns))) {
        fail("CASET");
        return false;
      }
      // This condition is exactly panel_axs15231b_draw_bitmap(): y_start is
      // the rotated logical-X start, not the screen's vertical coordinate.
      if (!sendColor(sourceX == 0 ? kCmdRamWrite : kCmdRamWriteContinue,
                     staging_,
                     static_cast<size_t>(columnsInBlock) * h * 2)) {
        fail("kuldes");
        return false;
      }
      hasQueuedColor = true;
    }
    if (hasQueuedColor && !waitForQueuedColor()) {
      fail("DMA vegso szinkron");
      return false;
    }

    // The AXS GRAM is undefined after a software reset.  Showing it before
    // the first complete frame produces the coloured noise seen at boot.
    // A partial update is never sufficient to reveal the panel safely.
    if (!panelVisible_ && x == 0 && y == 0 && w == kLogicalWidth &&
        h == kLogicalHeight) {
      if (!showPanel()) {
        fail("bekapcsolas");
        return false;
      }
    }

    return true;
  }

 public:
  bool ready() const { return ready_; }

 private:
  static constexpr int32_t kLogicalWidth = 480;
  static constexpr int32_t kLogicalHeight = 320;
  static constexpr int32_t kNativeHeight = 320;
  static constexpr uint8_t kCmdCaseT = 0x2A;
  static constexpr uint8_t kCmdRamWrite = 0x2C;
  static constexpr uint8_t kCmdRamWriteContinue = 0x3C;
  static constexpr uint32_t kOpcodeWriteCmd = 0x02;
  static constexpr uint32_t kOpcodeWriteColor = 0x32;

  struct InitCommand {
    uint8_t command;
    uint8_t size;
    uint16_t delayMs;
    uint8_t data[31];
  };

  static int encodedCommand(uint8_t command, uint32_t opcode) {
    return static_cast<int>((opcode << 24) | (static_cast<uint32_t>(command) << 8));
  }

  bool sendParam(uint8_t command, const void* data, size_t bytes) {
    return esp_lcd_panel_io_tx_param(io_, encodedCommand(command, kOpcodeWriteCmd),
                                     data, bytes) == ESP_OK;
  }

  bool sendColor(uint8_t command, const void* data, size_t bytes) {
    return esp_lcd_panel_io_tx_color(io_,
                                     encodedCommand(command, kOpcodeWriteColor),
                                     data, bytes) == ESP_OK;
  }

  static bool onColorTransferDone(esp_lcd_panel_io_handle_t,
                                  esp_lcd_panel_io_event_data_t*,
                                  void* userContext) {
    auto* transport = static_cast<AxsFactoryTransport*>(userContext);
    if (!transport || !transport->colorDone_) return false;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(transport->colorDone_, &higherPriorityTaskWoken);
    return higherPriorityTaskWoken == pdTRUE;
  }

  bool waitForQueuedColor() {
    return colorDone_ &&
           xSemaphoreTake(colorDone_, pdMS_TO_TICKS(250)) == pdTRUE;
  }

  bool initializeFactoryPanel() {
    // esp_lcd_panel_reset() followed by panel_axs15231b_init().  Values are
    // the JC3248W535C Guition lcd_init_cmds table, not guessed settings.
    if (!sendParam(0x01, nullptr, 0)) return false;  // SWRESET
    delay(120);
    if (!sendParam(0x11, nullptr, 0)) return false;  // SLPOUT
    delay(100);
    // The factory LVGL rotation path is already fixed at 90 degrees.  Its
    // matching physical, non-flipped AXS scan direction is MADCTL 0x00;
    // 0xC0 is the deliberate 180 degree web alternative.
    const uint8_t madctl = screenFlipped_ ? 0xC0 : 0x00;
    const uint8_t colmod = 0x55;  // RGB565
    if (!sendParam(0x36, &madctl, 1) || !sendParam(0x3A, &colmod, 1)) {
      return false;
    }

    static const InitCommand kGuitionInit[] = {
        {0xBB,8,0,{0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5}},
        {0xA0,17,0,{0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00}},
        {0xA2,31,0,{0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,0x80,0x80,0x80,0x20,0xF9,0x10,0x02,0xFF,0xFF,0xF0,0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,0x5A}},
        {0xD0,30,0,{0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,0x42,0xC2,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12}},
        {0xA3,22,0,{0xA0,0x06,0xAA,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55}},
        {0xC1,30,0,{0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0D,0x00,0xFF,0x40}},
        {0xC3,11,0,{0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01}},
        {0xC4,29,0,{0x00,0x24,0x33,0x80,0x00,0xEA,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50}},
        {0xC5,23,0,{0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,0x10,0x10,0x00}},
        {0xC6,20,0,{0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22}},
        {0xC7,20,0,{0x50,0x32,0x28,0x00,0xA2,0x80,0x8F,0x00,0x80,0xFF,0x07,0x11,0x9C,0x67,0xFF,0x24,0x0C,0x0D,0x0E,0x0F}},
        {0xC9,4,0,{0x33,0x44,0x44,0x01}},
        {0xCF,27,0,{0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x08,0x08,0x12,0xA0,0x08}},
        {0xD5,30,0,{0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00}},
        {0xD6,30,0,{0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00}},
        {0xD7,19,0,{0x03,0x01,0x0B,0x09,0x0F,0x0D,0x1E,0x1F,0x18,0x1D,0x1F,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F}},
        {0xD8,12,0,{0x02,0x00,0x0A,0x08,0x0E,0x0C,0x1E,0x1F,0x18,0x1D,0x1F,0x19}},
        {0xD9,12,0,{0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}},
        {0xDD,12,0,{0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}},
        {0xDF,8,0,{0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90}},
        {0xE0,17,0,{0x3B,0x28,0x10,0x16,0x0C,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D}},
        {0xE1,17,0,{0x37,0x28,0x10,0x16,0x0B,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F}},
        {0xE2,17,0,{0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D}},
        {0xE3,17,0,{0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F}},
        {0xE4,17,0,{0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D}},
        {0xE5,17,0,{0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F}},
        {0xA4,16,0,{0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,0x40,0x40,0x20,0xFF,0x60,0x30}},
        {0xA4,4,0,{0x85,0x85,0x95,0x85}},
        {0xBB,8,0,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
        {0x13,0,0,{}}, {0x11,0,120,{}}, {0x2C,4,0,{0x00,0x00,0x00,0x00}},
    };
    for (const InitCommand& command : kGuitionInit) {
      if (!sendParam(command.command, command.data, command.size)) return false;
      if (command.delayMs) delay(command.delayMs);
    }
    // Panel_FrameBufferBase may have left the controller output enabled while
    // it prepared its auxiliary framebuffer.  GRAM has no valid image yet,
    // so explicitly blank the panel until the first complete factory frame.
    if (!sendParam(0x28, nullptr, 0)) return false;  // DISPOFF
    // These controller settings must be issued after the vendor table too:
    // it may overwrite the initial MADCTL state while the panel is blank.
    // display-on itself remains deferred until the first full frame.
    return applyOrientation() && applyColorInversion();
  }

  bool showPanel() {
    if (panelVisible_) return true;
    // DISPOFF/SLPOUT sequences on this AXS revision do not reliably retain
    // MADCTL. Re-apply the saved orientation immediately before first light.
    if (!applyOrientation() || !applyColorInversion()) return false;
    if (!sendParam(0x29, nullptr, 0)) return false;  // display on
    panelVisible_ = true;
    return true;
  }

  bool applyOrientation() {
    // Keep the same mapping as the factory boot sequence.
    const uint8_t madctl = screenFlipped_ ? 0xC0 : 0x00;
    return sendParam(0x36, &madctl, 1);
  }

  bool applyColorInversion() {
    // Reference-driver esp_lcd_panel_invert_color().
    return sendParam(colorInverted_ ? 0x21 : 0x20, nullptr, 0);
  }

  void fail(const char* reason) {
    if (!faulted_) {
      Serial.printf("[display] AXS gyari kep-ut leallitva: %s\n", reason);
    }
    faulted_ = true;
  }

  esp_lcd_panel_io_handle_t io_ = nullptr;
  uint16_t* staging_ = nullptr;
  size_t stagingPixels_ = 0;
  bool colorInverted_ = false;
  bool screenFlipped_ = false;
  bool ready_ = false;
  bool panelVisible_ = false;
  bool faulted_ = false;
  SemaphoreHandle_t colorDone_ = nullptr;
  SemaphoreHandle_t transportMutex_ = nullptr;
};
