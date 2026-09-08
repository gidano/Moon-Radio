#pragma once

#ifndef DSP_ILI9488
#define DSP_ILI9488 21
#endif
#ifndef DSP_ST7796
#define DSP_ST7796 22
#endif
#ifndef DSP_AXS15231B
#define DSP_AXS15231B 29
#endif

#ifndef TS_MODEL_XPT2046
#define TS_MODEL_XPT2046 1
#endif
#ifndef TS_MODEL_GT911
#define TS_MODEL_GT911 2
#endif
#ifndef TS_MODEL_FT6X36
#define TS_MODEL_FT6X36 3
#endif
#ifndef TS_MODEL_AXS15231B
#define TS_MODEL_AXS15231B 4
#endif

#ifndef DSP_MODEL
#error "Select one DSP_MODEL line in options.h."
#endif

#if DSP_MODEL != DSP_ILI9488 && DSP_MODEL != DSP_ST7796 && DSP_MODEL != DSP_AXS15231B
#error "Unsupported DSP_MODEL selected in options.h."
#endif

#ifndef TOUCH_ENABLED
#define TOUCH_ENABLED 1
#endif

#if TOUCH_ENABLED && !defined(TS_MODEL)
#error "TOUCH_ENABLED is 1, but TS_MODEL is not set in options.h."
#endif

#ifndef DISPLAY_PROFILE_ST7796
#define DISPLAY_PROFILE_ST7796 (DSP_MODEL == DSP_ST7796)
#endif
#ifndef DISPLAY_PROFILE_AXS15231B
#define DISPLAY_PROFILE_AXS15231B (DSP_MODEL == DSP_AXS15231B)
#endif

#ifndef TOUCH_PROFILE_FT6X36
#if TOUCH_ENABLED && defined(TS_MODEL) && TS_MODEL == TS_MODEL_FT6X36
#define TOUCH_PROFILE_FT6X36 1
#else
#define TOUCH_PROFILE_FT6X36 0
#endif
#endif

#if DSP_MODEL == DSP_AXS15231B && (!TOUCH_ENABLED || TS_MODEL != TS_MODEL_AXS15231B)
#error "The JC3248W535C / AXS15231B profile must use TS_MODEL_AXS15231B touch."
#endif

#if DSP_MODEL == DSP_AXS15231B
#ifndef AXS_FLUSH_TASK_PRIORITY
#define AXS_FLUSH_TASK_PRIORITY 1
#endif
#ifndef AXS_FLUSH_TASK_STACK_WORDS
// Playback measurement peaks around 9.1 KiB.  Keep roughly 2.9 KiB headroom
// while returning 4 KiB of internal RAM to the radio.
#define AXS_FLUSH_TASK_STACK_WORDS 3072
#endif
#ifndef AXS_FLUSH_TASK_CORE
// The audio decoder owns core 0.  Keep QSPI transfers off that core so a
// display update cannot drain the network/audio buffer.
#define AXS_FLUSH_TASK_CORE 1
#endif
#ifndef AXS_SCAN_BUILD_YIELD_COLS
#define AXS_SCAN_BUILD_YIELD_COLS 0
#endif
#ifndef AXS_SCAN_MAP_ROW_MAJOR_READ
#define AXS_SCAN_MAP_ROW_MAJOR_READ 0
#endif
#ifndef AXS_INCREMENTAL_SCAN_MAP
#define AXS_INCREMENTAL_SCAN_MAP 1
#endif
#ifndef AXS_MIN_FRAME_INTERVAL_MS
#define AXS_MIN_FRAME_INTERVAL_MS 50
#endif
#ifndef AXS_FLUSH_CHUNK_DELAY_EVERY
#define AXS_FLUSH_CHUNK_DELAY_EVERY 0
#endif
#ifndef AXS_FLUSH_CHUNK_DELAY_MS
#define AXS_FLUSH_CHUNK_DELAY_MS 0
#endif
#ifndef AXS_FLUSH_CHUNK_YIELD
#define AXS_FLUSH_CHUNK_YIELD 1
#endif
#ifndef AXS_USE_RAW_QIO_FLUSH
#define AXS_USE_RAW_QIO_FLUSH 1
#endif
#ifndef AXS_RAW_QIO_QUEUED_FLUSH
// The controller is stable with synchronous raw-QSPI blocks.  The queued
// variant can lengthen a frame and occasionally disturb the panel stream.
#define AXS_RAW_QIO_QUEUED_FLUSH 0
#endif
#ifndef AXS_RAW_QIO_SPI_FREQ
// 80 MHz produces bit errors on the JC3248W535C's QSPI wiring.  Keep the
// verified 40 MHz setting; performance work must not compromise pixels.
#define AXS_RAW_QIO_SPI_FREQ 40000000
#endif
#ifndef AXS_PREINIT_RAW_QIO_BUS
// Reserve the QSPI bus with the Guition reference transfer size before
// LovyanGFX attaches to it.  This permits the larger DMA transaction safely.
#define AXS_PREINIT_RAW_QIO_BUS 1
#endif
#ifndef AXS_FLUSH_USE_DMA
// Keep the QSPI source in internal DMA RAM.  Direct PSRAM transactions can
// occasionally corrupt a complete AXS frame under Wi-Fi/audio pressure.
#define AXS_FLUSH_USE_DMA 1
#endif
#ifndef AXS_RAW_DIRECT_SCANBUFFER
// The controller still receives raw QSPI, but every 4 KiB block is copied to
// the explicitly DMA-capable staging line first.  This avoids rare displaced
// screen strips caused by direct PSRAM transactions.
#define AXS_RAW_DIRECT_SCANBUFFER 0
#endif
#ifndef AXS_FLUSH_PIXELS
// Bound every SPI DMA transaction to 8 KiB.  This leaves enough internal RAM
// for Moon's artwork/network functions while still using raw QSPI.
#define AXS_FLUSH_PIXELS 4096
#endif
#ifndef AXS_RAW_FLUSH_YIELD_EVERY
// A 4 KiB QSPI transfer is short.  Yielding all 38 blocks of a full frame
// adds more scheduler latency than it saves; yielding every eighth still
// leaves time for the rest of the AXS core.
#define AXS_RAW_FLUSH_YIELD_EVERY 8
#endif
#ifndef AXS_ENABLE_PARTIAL_FLUSH
// The existing Lovyan framebuffer transport cannot safely consume the
// factory panel's rectangle protocol: it owns a deferred frame image rather
// than the current LVGL rectangle.  Keep this legacy transport whole-frame
// only until the dedicated factory-driver adapter replaces it.
#define AXS_ENABLE_PARTIAL_FLUSH 0
#endif
#ifndef AXS_FACTORY_DIRECT_LVGL
// The Guition reference sends the current LVGL rectangle directly to the
// panel.  It does not maintain or replay a second, full-screen framebuffer.
// Keep this AXS-only path separate from the legacy Lovyan framebuffer flush.
// The AXS branch owns the complete Guition panel-IO initialization and
// bitmap transport.  Other display profiles do not compile this path.
#define AXS_FACTORY_DIRECT_LVGL 1
#endif
#ifndef AXS_ENABLE_SCAN_FLUSH
#define AXS_ENABLE_SCAN_FLUSH 1
#endif
#ifndef AXS_PARTIAL_MAX_DIRTY_PIXELS
#define AXS_PARTIAL_MAX_DIRTY_PIXELS (480 * 320)
#endif
#ifndef AXS_LVGL_RUN_INTERVAL_MS
#define AXS_LVGL_RUN_INTERVAL_MS 50
#endif
#ifndef AXS_VU_PHYSICAL_FPS
// VU cadence is independent from LVGL. The AXS transfer preserves the
// factory's 320-row QSPI raster while changing only the 240x62 VU pixels.
#define AXS_VU_PHYSICAL_FPS 15
#endif
// The Guition reference implementation uses the controller's QSPI path.
// LovyanGFX's generic full-frame path was sending every VU update as a
// 480x320 transfer; the raw QSPI path also enables the safe row blit above.
#ifndef AXS_USE_LOVYAN_FULL_FLUSH
#define AXS_USE_LOVYAN_FULL_FLUSH 0
#endif
#ifndef AXS_FLUSH_DIAG
#define AXS_FLUSH_DIAG 1
#endif
#endif
#if DSP_MODEL == DSP_AXS15231B && !defined(SD_SPI_HOST)
#define SD_SPI_HOST FSPI
#endif
