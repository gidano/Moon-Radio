/**
 * Minimal LVGL 9.3 configuration for the ESP32-S3 demo.
 * Undefined options use LVGL's built-in defaults.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_LOG 0

#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_LODEPNG 1

#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 1
#define LV_FS_ARDUINO_ESP_LITTLEFS_LETTER 'S'
#define LV_FS_ARDUINO_ESP_LITTLEFS_PATH ""

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif
