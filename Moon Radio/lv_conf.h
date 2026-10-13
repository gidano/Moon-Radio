/**
 * Minimal LVGL 9.3 configuration for the ESP32-S3 demo.
 * Undefined options use LVGL's built-in defaults.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* Az LVGL 64 KiB-os belső memóriakészlete alapból statikus tömb a szűkös
   belső DRAM-ban.  A PSRAM-ba tesszük: így a TLS-es audio (esp-aes DMA),
   a lwIP és a háttérfeladatok stackjei számára ~64 KiB belső RAM szabadul
   fel.  A rajzolási puffer (DisplayManager) továbbra is belső DMA RAM. */
#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(size) \
  heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

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
