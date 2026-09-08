#pragma once

#include <Arduino.h>
#include <climits>
#include <esp_heap_caps.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lgfx/v1/Bus.hpp>
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

#ifndef AXS_FLUSH_TASK_DELAY_MS
#define AXS_FLUSH_TASK_DELAY_MS 1
#endif

#ifndef AXS_SMOOTH_SCROLL
#define AXS_SMOOTH_SCROLL 1
#endif

// Very light driver-side coalescing: wait only one RTOS tick so that
// multiple sprite pushes from the same UI loop can collapse into one
// physical full-frame flush. No fixed 60 Hz pacing here, because that made
// scrolling visibly slow on JC3248W535C.
#ifndef AXS_FLUSH_COALESCE_MS
#if AXS_SMOOTH_SCROLL
#define AXS_FLUSH_COALESCE_MS 1
#else
#define AXS_FLUSH_COALESCE_MS 5
#endif
#endif

// Keep continuous full-frame updates within the cadence used by the stable
// JC3248W535 AXS implementation. This only paces physical panel transfers;
// framebuffer drawing and UI logic remain unrestricted.
#ifndef AXS_MIN_FRAME_INTERVAL_MS
#define AXS_MIN_FRAME_INTERVAL_MS 50
#endif

// The ESP32-S3 DMA path can very rarely lose/duplicate part of a long QSPI
// transfer without reporting an error. Keep the AXS panel transfer synchronous
// while retaining the same staging buffer and QSPI clock.
#ifndef AXS_FLUSH_USE_DMA
#define AXS_FLUSH_USE_DMA 0
#endif

#ifndef AXS_RAW_QIO_QUEUED_FLUSH
#define AXS_RAW_QIO_QUEUED_FLUSH 0
#endif

#ifndef AXS_SCAN_BUILD_YIELD_COLS
#define AXS_SCAN_BUILD_YIELD_COLS 8
#endif

#ifndef AXS_SCAN_MAP_ROW_MAJOR_READ
#define AXS_SCAN_MAP_ROW_MAJOR_READ 0
#endif

#ifndef AXS_INCREMENTAL_SCAN_MAP
#define AXS_INCREMENTAL_SCAN_MAP 0
#endif

#ifndef AXS_FLUSH_TASK_PRIORITY
#define AXS_FLUSH_TASK_PRIORITY 1
#endif

#ifndef AXS_FLUSH_TASK_CORE
#if AXS_SMOOTH_SCROLL
#define AXS_FLUSH_TASK_CORE 0
#else
#define AXS_FLUSH_TASK_CORE 1
#endif
#endif

#ifndef AXS_FLUSH_CHUNK_DELAY_EVERY
#if AXS_SMOOTH_SCROLL
#define AXS_FLUSH_CHUNK_DELAY_EVERY 0
#else
#define AXS_FLUSH_CHUNK_DELAY_EVERY 2
#endif
#endif

#ifndef AXS_FLUSH_CHUNK_DELAY_MS
#if AXS_SMOOTH_SCROLL
#define AXS_FLUSH_CHUNK_DELAY_MS 0
#else
#define AXS_FLUSH_CHUNK_DELAY_MS 1
#endif
#endif

#ifndef AXS_FLUSH_CHUNK_YIELD
#define AXS_FLUSH_CHUNK_YIELD 1
#endif

#ifndef AXS_STABLE_BOOT_FLUSHES
// One complete image establishes GRAM after boot/wake.  Keeping eighty
// full-frame writes here defeats the AXS factory rectangle path for seconds.
#define AXS_STABLE_BOOT_FLUSHES 1
#endif

#ifndef AXS_SCAN_SMALL_DIRTY_PIXELS
#define AXS_SCAN_SMALL_DIRTY_PIXELS 8000
#endif

#ifndef AXS_ENABLE_SCAN_FLUSH
#define AXS_ENABLE_SCAN_FLUSH 0
#endif

#ifndef AXS_FLUSH_PIXELS
#define AXS_FLUSH_PIXELS 4096
#endif

#ifndef AXS_FLUSH_TASK_STACK_WORDS
#define AXS_FLUSH_TASK_STACK_WORDS 3072
#endif

#ifndef AXS_RAW_FLUSH_YIELD_EVERY
#define AXS_RAW_FLUSH_YIELD_EVERY 8
#endif

// The raw-QSPI colour transfer is DMA driven.  polling_transmit() spins on
// the CPU for the entire DMA transfer, which needlessly competes with audio
// decoding and networking.  spi_device_transmit() waits for the very same
// transfer through the SPI driver's completion semaphore instead.  It does
// not change the panel protocol, buffers, or pixels on the wire.
#ifndef AXS_RAW_QIO_INTERRUPT_WAIT
#define AXS_RAW_QIO_INTERRUPT_WAIT 1
#endif

#ifndef AXS_USE_RAW_QIO_FLUSH
#define AXS_USE_RAW_QIO_FLUSH 1
#endif

#ifndef AXS_RAW_QIO_SPI_FREQ
#define AXS_RAW_QIO_SPI_FREQ 40000000
#endif

#ifndef AXS_FLUSH_DIAG
#define AXS_FLUSH_DIAG 0
#endif

#ifndef AXS_NATIVE_VISUALIZER_DIRECT_ROWS
#define AXS_NATIVE_VISUALIZER_DIRECT_ROWS 0
#endif

#ifndef AXS_NATIVE_VISUALIZER_PARTIAL_MODE
#define AXS_NATIVE_VISUALIZER_PARTIAL_MODE 0
#endif

#ifndef AXS_FLUSH_DIAG_INTERVAL_MS
#define AXS_FLUSH_DIAG_INTERVAL_MS 1000UL
#endif

namespace lgfx {
inline namespace v1 {

struct Panel_AXS15231B : public Panel_FrameBufferBase {
    static constexpr uint8_t CMD_SWRESET = 0x01;
    static constexpr uint8_t CMD_SLPIN = 0x10;
    static constexpr uint8_t CMD_SLPOUT = 0x11;
    static constexpr uint8_t CMD_PTLON = 0x12;
    static constexpr uint8_t CMD_NORON = 0x13;
    static constexpr uint8_t CMD_INVOFF = 0x20;
    static constexpr uint8_t CMD_INVON = 0x21;
    static constexpr uint8_t CMD_PIXELS_OFF = 0x22;
    static constexpr uint8_t CMD_CASET = 0x2A;
    static constexpr uint8_t CMD_RASET = 0x2B;
    static constexpr uint8_t CMD_RAMWR = 0x2C;
    static constexpr uint8_t CMD_PTLAR_ROWS = 0x30;
    static constexpr uint8_t CMD_PTLAR_COLUMNS = 0x31;
    static constexpr uint8_t CMD_RAMWRC = 0x3C;
    static constexpr uint8_t SEND_PIXELS = 0x32;

    Panel_AXS15231B(void) {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 320;
        _cfg.readable = false;
        _cfg.bus_shared = false;
        _write_depth = rgb565_2Byte;
        _read_depth = rgb565_2Byte;
    }

    bool init(bool use_reset) override {
        if (!allocateFrameBuffer()) {
            Serial.println("##[AXS display driver]#: Out of PSRAM !");
            return false;
        }

#if AXS_USE_RAW_QIO_FLUSH && AXS_PREINIT_RAW_QIO_BUS && !AXS_USE_LOVYAN_FULL_FLUSH && !AXS_FACTORY_DIRECT_LVGL
        preinitRawQioBus();
#endif

        if (!Panel_FrameBufferBase::init(use_reset)) {
#if AXS_USE_RAW_QIO_FLUSH && AXS_PREINIT_RAW_QIO_BUS && !AXS_USE_LOVYAN_FULL_FLUSH && !AXS_FACTORY_DIRECT_LVGL
            releaseRawQioBusPreinit();
#endif
            return false;
        }

        
#if AXS_USE_RAW_QIO_FLUSH && !AXS_USE_LOVYAN_FULL_FLUSH
        initRawQioFlush();
#endif

// ESP32-S3 enables Panel_FrameBufferBase auto-display for cache
        // writeback. That would call display() synchronously at the end of
        // every LovyanGFX primitive, while this panel already owns a locked,
        // coalescing background flush task. Leaving both paths active causes
        // a full-frame flush storm under spectrum/VU updates and starves
        // IDLE0 until the task watchdog resets the device.
        _auto_display = false;
        _busReady = true;
        _busMutex = xSemaphoreCreateMutex();
        _frameMutex = xSemaphoreCreateRecursiveMutex();

#if !AXS_FACTORY_DIRECT_LVGL
        if (use_reset && _cfg.pin_rst < 0) {
            sendCommand(CMD_SWRESET);
        }
        delay(250);

        sendCommand(CMD_PIXELS_OFF);
        sendCommand(CMD_NORON);
        sendCommand(CMD_SLPOUT);
        delay(200);
        sendCommand(0x29);
        delay(200);
#endif

        memset(_framebuffer, 0, _bufferBytes);
#if AXS_FACTORY_DIRECT_LVGL
        // The factory ESP-LCD path owns both the controller lifecycle and
        // the pixels submitted by LVGL.  Do not let this auxiliary Lovyan
        // framebuffer wake/reconfigure the physical panel or replay frames.
        clearDirtyRange();
#else
        markFullDirty();
        display(0, 0, _width, _height);
        startFlushTask();
#endif
        return true;
    }

    void releaseBus(void) override {
        stopFlushTask();
        freeFrameBuffer();
        if (_busMutex) {
            vSemaphoreDelete(_busMutex);
            _busMutex = nullptr;
        }
        if (_frameMutex) {
            vSemaphoreDelete(_frameMutex);
            _frameMutex = nullptr;
        }
#if AXS_USE_RAW_QIO_FLUSH && !AXS_USE_LOVYAN_FULL_FLUSH
        releaseRawQioFlush();
#endif
        Panel_FrameBufferBase::releaseBus();
    }

    void beginTransaction(void) override {
        lockFrame();
    }

    void endTransaction(void) override {
        requestFlush();
        unlockFrame();
    }

    void display(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h) override {
        (void)x;
        (void)y;
        (void)w;
        (void)h;
#if AXS_FLUSH_DIAG
        const uint32_t diagStartUs = micros();
#endif

        if (_framebuffer == nullptr ||
            (_flushLine == nullptr && !_scanBufferDirectDma) ||
            _range_mod.empty()) {
            return;
        }

        lockBus();
#if AXS_USE_RAW_QIO_FLUSH
        const bool rawFlush = rawQioFlushReady() && !AXS_USE_LOVYAN_FULL_FLUSH;
#else
        const bool rawFlush = false;
#endif
        const bool ownTransaction = !rawFlush && !_inBusTransaction;
        if (ownTransaction) {
            _bus->beginTransaction();
            _inBusTransaction = true;
        }

        const bool useStablePath = shouldUseStablePath();
#if AXS_FLUSH_DIAG
        const uint16_t diagLeft = _range_mod.left;
        const uint16_t diagTop = _range_mod.top;
        const uint16_t diagRight = _range_mod.right;
        const uint16_t diagBottom = _range_mod.bottom;
        const uint32_t diagHeight = (_range_mod.empty()) ? 0UL :
            static_cast<uint32_t>(diagBottom - diagTop + 1);
        const uint32_t diagPixels = useStablePath ?
            static_cast<uint32_t>(_cfg.memory_width) * static_cast<uint32_t>(_cfg.memory_height) :
            static_cast<uint32_t>(_cfg.memory_width) * diagHeight;
        uint32_t diagMapUs = 0;
        uint32_t diagSendUs = 0;
#endif

        _flushInProgress = true;
        lockFrame();
#if AXS_ENABLE_SCAN_FLUSH
        if (_scanBuffer) {
#if AXS_FLUSH_DIAG
            uint32_t diagPartStartUs = micros();
#endif
#if AXS_INCREMENTAL_SCAN_MAP
            if (_scanBufferNeedsFullMap) {
                updateScanBufferFromFrame();
                _scanBufferNeedsFullMap = false;
            }
#else
            updateScanBufferFromFrame();
#endif
#if AXS_FLUSH_DIAG
            diagMapUs = micros() - diagPartStartUs;
            diagPartStartUs = micros();
#endif
            if (useStablePath) {
                sendScanBuffer();
            } else {
                sendScanBufferRect(_range_mod.left, _range_mod.top,
                                   _range_mod.right, _range_mod.bottom);
            }
#if AXS_FLUSH_DIAG
            diagSendUs = micros() - diagPartStartUs;
#endif
            if (_stableBootFlushes) {
                --_stableBootFlushes;
            }
        } else
#endif
        if (useStablePath) {
#if AXS_FLUSH_DIAG
            const uint32_t diagPartStartUs = micros();
#endif
#if AXS_USE_RAW_QIO_FLUSH && !AXS_USE_LOVYAN_FULL_FLUSH && !AXS_FACTORY_DIRECT_LVGL
            if (rawQioFlushReady()) {
                sendFrameRawMapped();
            } else
#endif
            {
                sendFrameDirect();
            }
#if AXS_FLUSH_DIAG
            diagSendUs = micros() - diagPartStartUs;
#endif
            if (_stableBootFlushes) {
                --_stableBootFlushes;
            }
        } else {
#if AXS_FLUSH_DIAG
            const uint32_t diagPartStartUs = micros();
#endif
            sendDirtyFrameDirect();
#if AXS_FLUSH_DIAG
            diagSendUs = micros() - diagPartStartUs;
#endif
        }
        unlockFrame();

#if AXS_FLUSH_DIAG
        const bool hadDirtyDuring = _dirtyDuringFlush;
#endif
        if (_dirtyDuringFlush) {
            if (_range_mod.empty()) {
                markFullDirty();
            }
            _flushRequested = true;
            _dirtyDuringFlush = false;
        } else {
            clearDirtyRange();
        }
        _flushInProgress = false;

        if (ownTransaction) {
            _bus->endTransaction();
            _inBusTransaction = false;
        }
        unlockBus();
        _lastPhysicalFlushTick = xTaskGetTickCount();
#if AXS_FLUSH_DIAG
        const uint32_t nowMs = millis();
        const uint32_t flushUs = micros() - diagStartUs;
        if (_diagFlushWindowMs == 0) {
            _diagFlushWindowMs = nowMs;
        }
        _diagFlushSamples++;
        _diagFlushSumUs += flushUs;
        _diagMapSumUs += diagMapUs;
        _diagSendSumUs += diagSendUs;
        _diagPrepSumUs += _diagLastPrepUs;
        _diagWriteSumUs += _diagLastWriteUs;
        if (_diagLastRawFlush) _diagRawFlushSamples++;
        if (flushUs > _diagFlushMaxUs) _diagFlushMaxUs = flushUs;
        if (hadDirtyDuring) _diagFlushDirtyDuring++;
        if (nowMs - _diagFlushWindowMs >= AXS_FLUSH_DIAG_INTERVAL_MS) {
            Serial.printf(
                "AXS_FLUSH core=%d samples=%lu avg_us=%lu max_us=%lu map_us=%lu send_us=%lu prep_us=%lu write_us=%lu raw=%lu dirty_during=%lu req=%u stable=%u scan_dma=%u chunks=%u native=%lu native_pix=%lu vu_direct=%lu rowpix=%lu row_us=%lu lvgl=%ld,%ld-%ld,%ld lvgl_pix=%lu area=%u,%u-%u,%u pix=%lu buf=%lu buf_pct=%u cpu=%u/%u cpu_ok=%u free_heap=%u stack_free=%u psram=%u\n",
                xPortGetCoreID(),
                (unsigned long)_diagFlushSamples,
                (unsigned long)(_diagFlushSamples ? _diagFlushSumUs / _diagFlushSamples : 0),
                (unsigned long)_diagFlushMaxUs,
                (unsigned long)(_diagFlushSamples ? _diagMapSumUs / _diagFlushSamples : 0),
                (unsigned long)(_diagFlushSamples ? _diagSendSumUs / _diagFlushSamples : 0),
                (unsigned long)(_diagFlushSamples ? _diagPrepSumUs / _diagFlushSamples : 0),
                (unsigned long)(_diagFlushSamples ? _diagWriteSumUs / _diagFlushSamples : 0),
                (unsigned long)_diagRawFlushSamples,
                (unsigned long)_diagFlushDirtyDuring,
                _flushRequested ? 1 : 0,
                _stableBootFlushes,
                _scanBufferDirectDma ? 1U : 0U,
                static_cast<unsigned>(_diagLastChunkCount),
                (unsigned long)_diagNativeBlitCount,
                (unsigned long)_diagNativeBlitPixels,
                (unsigned long)_diagRowDirectCount,
                (unsigned long)_diagRowDirectPixels,
                (unsigned long)_diagRowDirectWriteUs,
                static_cast<long>(_diagLvglX1),
                static_cast<long>(_diagLvglY1),
                static_cast<long>(_diagLvglX2),
                static_cast<long>(_diagLvglY2),
                (unsigned long)_diagLvglPixels,
                diagLeft,
                diagTop,
                diagRight,
                diagBottom,
                (unsigned long)diagPixels,
                (unsigned long)_diagBufferFilledBytes,
                static_cast<unsigned>(_diagBufferPercent),
                static_cast<unsigned>(_diagCpu0Percent),
                static_cast<unsigned>(_diagCpu1Percent),
                _diagCpuValid ? 1U : 0U,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)(_flushTaskHandle ? uxTaskGetStackHighWaterMark(_flushTaskHandle) : 0),
                (unsigned)ESP.getFreePsram()
            );
            _diagFlushWindowMs = nowMs;
            _diagFlushSamples = 0;
            _diagFlushSumUs = 0;
            _diagFlushMaxUs = 0;
            _diagMapSumUs = 0;
            _diagSendSumUs = 0;
            _diagPrepSumUs = 0;
            _diagWriteSumUs = 0;
            _diagRawFlushSamples = 0;
            _diagFlushDirtyDuring = 0;
            _diagNativeBlitCount = 0;
            _diagNativeBlitPixels = 0;
            _diagRowDirectCount = 0;
            _diagRowDirectPixels = 0;
            _diagRowDirectWriteUs = 0;
        }
#endif
    }

    void setInvert(bool invert) override {
        _invert = invert;
        _softwareInvert = invert;
        if (!_busReady) {
            return;
        }
        sendCommand(CMD_INVOFF);
        _stableBootFlushes = AXS_STABLE_BOOT_FLUSHES;
        markFullDirty();
        display(0, 0, _width, _height);
    }

    void setSleep(bool sleep) override {
        if (!_busReady) {
            return;
        }
        _sleeping = sleep;
        sendCommand(sleep ? CMD_SLPIN : CMD_SLPOUT);
        delay(200);
        if (!sleep) {
            _stableBootFlushes = AXS_STABLE_BOOT_FLUSHES;
            markFullDirty();
            _flushRequested = true;
        }
    }

    bool isFlushBusy() const {
        return _flushInProgress;
    }

    bool isFrameBusy() const {
        if (!_frameMutex) {
            return false;
        }
        if (xSemaphoreTakeRecursive(_frameMutex, 0) == pdTRUE) {
            xSemaphoreGiveRecursive(_frameMutex);
            return false;
        }
        return true;
    }

    bool tryBeginFrameAccess() {
        if (!_frameMutex) {
            return true;
        }
        return xSemaphoreTakeRecursive(_frameMutex, 0) == pdTRUE;
    }

    void endFrameAccess() {
        if (_frameMutex) {
            xSemaphoreGiveRecursive(_frameMutex);
        }
    }


    void setRuntimeDiagnostics(uint32_t bufferFilledBytes, uint8_t bufferPercent,
                               uint8_t cpu0Percent, uint8_t cpu1Percent,
                               bool cpuValid) {
        _diagBufferFilledBytes = bufferFilledBytes;
        _diagBufferPercent = bufferPercent;
        _diagCpu0Percent = cpu0Percent;
        _diagCpu1Percent = cpu1Percent;
        _diagCpuValid = cpuValid;
    }

    void noteLvglFlushArea(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                           uint32_t pixels) {
        _diagLvglX1 = x1;
        _diagLvglY1 = y1;
        _diagLvglX2 = x2;
        _diagLvglY2 = y2;
        _diagLvglPixels = pixels;
    }

    // The factory ESP-LCD transport borrows the existing internal-DMA line.
    // No second staging allocation is permitted: free internal heap is part
    // of the audio/cover-download budget.
    uint16_t* factoryTransportBuffer() { return _flushLine; }
    size_t factoryTransportBufferPixels() const { return AXS_FLUSH_PIXELS; }
    // Exact Guition LV_DISP_ROT_90 transport entry point.  The source is
    // the active LVGL rectangle, not the panel's deferred framebuffer.
    // This mirrors lv_port.c: split the logical X range, rotate each block
    // right-to-left, then let the AXS QSPI driver write that block.
    bool flushFactoryLvglRect(int32_t x, int32_t y, int32_t w, int32_t h,
                              const uint16_t* pixels) {
#if AXS_FACTORY_DIRECT_LVGL && AXS_USE_RAW_QIO_FLUSH
        if (!pixels || w <= 0 || h <= 0 || x < 0 || y < 0 ||
            x + w > _cfg.memory_width || y + h > _cfg.memory_height ||
            !_flushLine || !rawQioFlushReady()) {
            return false;
        }

        lockBus();
        const uint32_t startUs = micros();
        const bool sent = sendFactoryRot270Blocks(x, y, w, h, pixels);
        unlockBus();
#if AXS_FLUSH_DIAG
        if (sent) {
            const uint32_t elapsedUs = micros() - startUs;
            _diagFactoryRectCount++;
            _diagFactoryPixels += static_cast<uint32_t>(w) *
                                  static_cast<uint32_t>(h);
            _diagFactoryTotalUs += elapsedUs;
            if (elapsedUs > _diagFactoryMaxUs) _diagFactoryMaxUs = elapsedUs;
            if (y == 258 && h == 62) {
                _diagFactoryVuCount++;
                _diagFactoryVuPixels += static_cast<uint32_t>(w) *
                                        static_cast<uint32_t>(h);
            }
            const uint32_t nowMs = millis();
            if (_diagFactoryWindowMs == 0) _diagFactoryWindowMs = nowMs;
            if (nowMs - _diagFactoryWindowMs >= AXS_FLUSH_DIAG_INTERVAL_MS) {
                Serial.printf(
                    "AXS_DIRECT core=%d rects=%lu pix=%lu vu_rects=%lu vu_pix=%lu avg_us=%lu max_us=%lu free_heap=%u psram=%u\\n",
                    xPortGetCoreID(),
                    (unsigned long)_diagFactoryRectCount,
                    (unsigned long)_diagFactoryPixels,
                    (unsigned long)_diagFactoryVuCount,
                    (unsigned long)_diagFactoryVuPixels,
                    (unsigned long)(_diagFactoryRectCount ?
                        _diagFactoryTotalUs / _diagFactoryRectCount : 0),
                    (unsigned long)_diagFactoryMaxUs,
                    (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getFreePsram());
                _diagFactoryWindowMs = nowMs;
                _diagFactoryRectCount = 0;
                _diagFactoryPixels = 0;
                _diagFactoryVuCount = 0;
                _diagFactoryVuPixels = 0;
                _diagFactoryTotalUs = 0;
                _diagFactoryMaxUs = 0;
            }
        }
#endif
        return sent;
#else
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)pixels;
        return false;
#endif
    }

    void writePixels(pixelcopy_t* param, uint32_t length, bool use_dma) override {
#if AXS_ENABLE_SCAN_FLUSH && AXS_INCREMENTAL_SCAN_MAP
        const uint_fast16_t xs = _xs;
        const uint_fast16_t ys = _ys;
        const uint_fast16_t xe = _xe;
        const uint_fast16_t ye = _ye;
#endif
        Panel_FrameBufferBase::writePixels(param, length, use_dma);
#if AXS_ENABLE_SCAN_FLUSH && AXS_INCREMENTAL_SCAN_MAP
        if (_scanBuffer && _internal_rotation == 0) {
            updateScanBufferRect(xs, ys, xe, ye);
        } else {
            _scanBufferNeedsFullMap = true;
        }
#endif
    }
    bool blitFrameBlock(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
        return blitFrameBlockImpl(x, y, w, h, pixels, true);
    }

    // Native VU updates share the framebuffer with LVGL, but must not queue
    // another full-frame transfer while the current one is still running.
    bool blitNativeVisualizerBlock(int32_t x, int32_t y, int32_t w, int32_t h,
                                   const uint16_t* pixels) {
        return blitFrameBlockImpl(x, y, w, h, pixels, true, true);
    }

    // Factory-LVGL and the VU intentionally have no shared framebuffer.
    // Stream the WaveVu canvas directly into the controller's native
    // right-to-left QSPI column order.  This is equivalent to the proven
    // scan-buffer rectangle order, but avoids stale Lovyan dirty/scan state.
    bool blitNativeVisualizerPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                                    const uint16_t* pixels) {
#if AXS_NATIVE_VISUALIZER_DIRECT_ROWS && AXS_USE_RAW_QIO_FLUSH
        if (!pixels || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > _cfg.memory_width ||
            y + h > _cfg.memory_height || !rawQioFlushReady()) {
            return false;
        }
        if (_busMutex && xSemaphoreTake(_busMutex, 0) != pdTRUE) {
            return false;
        }
        const uint32_t writeStartUs = micros();
        const uint8_t chunkCount = sendNativeVisualizerPixels(
            static_cast<uint_fast16_t>(x), static_cast<uint_fast16_t>(y),
            static_cast<uint_fast16_t>(w), static_cast<uint_fast16_t>(h), pixels);
        if (_busMutex) xSemaphoreGive(_busMutex);
#if AXS_FLUSH_DIAG
        if (chunkCount != 0) {
            _diagLastChunkCount = chunkCount;
            _diagRowDirectCount++;
            _diagRowDirectPixels += static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
            _diagRowDirectWriteUs += micros() - writeStartUs;
        }
#endif
        return chunkCount != 0;
#else
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)pixels;
        return false;
#endif
    }

    bool blitFrameBlockDeferred(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
        return blitFrameBlockImpl(x, y, w, h, pixels, false);
    }

    bool fillFrameBlockDeferred(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
        return fillFrameBlockImpl(x, y, w, h, color, false);
    }

private:
    bool fillFrameBlockImpl(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color, bool requestDisplayFlush) {
        if (!_framebuffer || !_lineTable || w <= 0 || h <= 0) {
            return false;
        }

        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > _cfg.memory_width) {
            w = _cfg.memory_width - x;
        }
        if (y + h > _cfg.memory_height) {
            h = _cfg.memory_height - y;
        }
        if (w <= 0 || h <= 0) {
            return false;
        }

        lockFrame();
        for (int32_t row = 0; row < h; ++row) {
            uint16_t* dst = reinterpret_cast<uint16_t*>(_lineTable[y + row] + (x * sizeof(uint16_t)));
            for (int32_t col = 0; col < w; ++col) {
                dst[col] = color;
            }
        }
        markDirtyRect(x, y, w, h);
#if AXS_FLUSH_DIAG
        _diagNativeBlitCount++;
        _diagNativeBlitPixels += static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
#endif
#if AXS_ENABLE_SCAN_FLUSH && AXS_INCREMENTAL_SCAN_MAP
        if (_scanBuffer && _internal_rotation == 0) {
            updateScanBufferRect(x, y, x + w - 1, y + h - 1);
        } else {
            _scanBufferNeedsFullMap = true;
        }
#endif
        if (requestDisplayFlush) {
            requestFlush();
        }
        unlockFrame();
        return true;
    }

    bool blitFrameBlockImpl(int32_t x, int32_t y, int32_t w, int32_t h,
                            const uint16_t* pixels, bool requestDisplayFlush,
                            bool coalesceNativeFlush = false) {
        if (!_framebuffer || !_lineTable || !pixels || w <= 0 || h <= 0) {
            return false;
        }

        const int32_t srcStride = w;
        int32_t srcX = 0;
        int32_t srcY = 0;
        if (x < 0) {
            srcX = -x;
            w += x;
            x = 0;
        }
        if (y < 0) {
            srcY = -y;
            h += y;
            y = 0;
        }
        if (x + w > _cfg.memory_width) {
            w = _cfg.memory_width - x;
        }
        if (y + h > _cfg.memory_height) {
            h = _cfg.memory_height - y;
        }
        if (w <= 0 || h <= 0) {
            return false;
        }

        lockFrame();
        for (int32_t row = 0; row < h; ++row) {
            const uint16_t* src = pixels + static_cast<size_t>(srcY + row) * static_cast<size_t>(srcStride) + srcX;
            uint8_t* dst = _lineTable[y + row] + (x * sizeof(uint16_t));
            memcpy(dst, src, static_cast<size_t>(w) * sizeof(uint16_t));
        }
#if AXS_FLUSH_DIAG
        _diagNativeBlitCount++;
        _diagNativeBlitPixels += static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
#endif
#if AXS_ENABLE_SCAN_FLUSH && AXS_INCREMENTAL_SCAN_MAP
        if (requestDisplayFlush) {
            if (_scanBuffer && _internal_rotation == 0) {
                updateScanBufferRect(x, y, x + w - 1, y + h - 1);
            } else {
                _scanBufferNeedsFullMap = true;
            }
        }
#endif
        // A VU row band that is written immediately must not be left in the
        // deferred dirty range.  Otherwise a later, unrelated LVGL update is
        // merged with it and turns into an almost full-screen transfer.
        bool directRowsFlushed = false;
#if AXS_NATIVE_VISUALIZER_DIRECT_ROWS && AXS_ENABLE_SCAN_FLUSH
        // In factory-LVGL mode the physical scene is sent by
        // AxsFactoryTransport, never by this Lovyan framebuffer.  Its dirty
        // range can therefore remain marked after colour-depth/swap setup
        // even though no Lovyan flush is pending.  Treating that stale marker
        // as a bus conflict made every native VU image stop in RAM and left
        // only the five-second factory UI frame visible.
        const bool legacyFlushIdle =
#if AXS_FACTORY_DIRECT_LVGL
            true;
#else
            _range_mod.empty() && !_flushRequested && !_flushInProgress;
#endif
        if (requestDisplayFlush && h <= 96 && legacyFlushIdle) {
            directRowsFlushed = flushScanRowsDirect(
                static_cast<uint_fast16_t>(x), static_cast<uint_fast16_t>(y),
                static_cast<uint_fast16_t>(x + w - 1),
                static_cast<uint_fast16_t>(y + h - 1));
        }
#endif
        // A busy QSPI bus must not turn a superseded VU frame into a deferred
        // full-screen refresh.  The next audio sample contains a newer VU
        // image, so dropping this one preserves both animation cadence and
        // the audio/network time budget.  Ordinary LVGL writes retain the
        // established dirty/full flush behaviour.
        const bool dropBusyNativeFrame = coalesceNativeFlush &&
                                         requestDisplayFlush &&
                                         !directRowsFlushed;
        if (!directRowsFlushed && !dropBusyNativeFrame &&
            (requestDisplayFlush || coalesceNativeFlush)) {
            markDirtyRect(x, y, w, h);
        }
        if (requestDisplayFlush && !directRowsFlushed &&
            !dropBusyNativeFrame) {
            if (!coalesceNativeFlush || (!_flushRequested && !_flushInProgress)) {
                requestFlush();
            }
        }
        unlockFrame();
        // The native VU caller uses this result as its physical-frame
        // accounting signal.  A coalesced VU copy that could not acquire the
        // raw QSPI path is intentionally dropped, not a displayed frame.
        if (coalesceNativeFlush) return directRowsFlushed;
        return true;
    }

    bool flushScanRowsDirect(uint_fast16_t left, uint_fast16_t top,
                             uint_fast16_t right, uint_fast16_t bottom) {
#if AXS_NATIVE_VISUALIZER_DIRECT_ROWS && AXS_ENABLE_SCAN_FLUSH && AXS_USE_RAW_QIO_FLUSH
        if (!_scanBuffer || !_flushLine || !rawQioFlushReady() ||
            left > right || top > bottom || right >= _cfg.memory_width ||
            bottom >= _cfg.memory_height) {
            return false;
        }
        if (_busMutex && xSemaphoreTake(_busMutex, 0) != pdTRUE) {
            return false;
        }
        // The VU source was just written to the logical framebuffer.  Map
        // this exact rectangle before using the native AXS column stream.
        // Previously only a deferred/full map refreshed _scanBuffer, which
        // made a direct VU transaction reuse pixels from an older frame at
        // a valid but wrong physical location.
        updateScanBufferRect(left, top, right, bottom);
        const uint32_t writeStartUs = micros();
        const uint8_t chunkCount = sendScanBufferRect(left, top, right, bottom);
        if (_busMutex) xSemaphoreGive(_busMutex);
#if AXS_FLUSH_DIAG
        if (chunkCount != 0) {
            _diagLastChunkCount = chunkCount;
            _diagRowDirectCount++;
            _diagRowDirectPixels += static_cast<uint32_t>(right - left + 1) *
                                    static_cast<uint32_t>(bottom - top + 1);
            _diagRowDirectWriteUs += micros() - writeStartUs;
        }
#endif
        return chunkCount != 0;
#else
        (void)left;
        (void)top;
        (void)right;
        (void)bottom;
        return false;
#endif
    }

    // Proven stable JC3248W535 transfer size from VTomRadio: 40 native
    // 320-pixel rows per DMA staging block.
    static constexpr uint16_t FLUSH_PIXELS = AXS_FLUSH_PIXELS;
    uint8_t** _lineTable = nullptr;
    uint8_t* _framebuffer = nullptr;
    uint16_t* _nativeFrame = nullptr;
    bool _nativeFrameValid = false;
    uint16_t* _scanBuffer = nullptr;
    uint16_t* _flushLine = nullptr;
    // A bounded, DMA-capable PSRAM staging buffer for native VU rectangles.
    // It deliberately does not consume internal heap needed by cover loading.
    uint16_t* _partialLine = nullptr;
    bool _scanBufferDirectDma = false;
    bool _scanBufferNeedsFullMap = true;
#if AXS_USE_RAW_QIO_FLUSH
    spi_device_handle_t _rawSpi = nullptr;
    spi_transaction_ext_t _queuedTransactions[2]{};
    bool _rawBusPreinitOwned = false;
#endif
    size_t _bufferBytes = 0;
    bool _busReady = false;
    bool _inBusTransaction = false;
    bool _softwareInvert = false;
    volatile bool _flushRequested = false;
    volatile bool _flushInProgress = false;
    volatile bool _dirtyDuringFlush = false;
    volatile bool _flushTaskStop = false;
    volatile bool _sleeping = false;
    volatile TickType_t _flushDueTick = 0;
    volatile TickType_t _lastPhysicalFlushTick = 0;
    uint8_t _stableBootFlushes = AXS_STABLE_BOOT_FLUSHES;
    TaskHandle_t _flushTaskHandle = nullptr;
    SemaphoreHandle_t _busMutex = nullptr;
    SemaphoreHandle_t _frameMutex = nullptr;
    volatile uint32_t _diagBufferFilledBytes = 0;
    volatile uint32_t _diagLvglPixels = 0;
    uint32_t _diagNativeBlitCount = 0;
    uint32_t _diagNativeBlitPixels = 0;
    uint32_t _diagRowDirectCount = 0;
    uint32_t _diagRowDirectPixels = 0;
    uint32_t _diagRowDirectWriteUs = 0;
#if AXS_FLUSH_DIAG
    uint32_t _diagFactoryWindowMs = 0;
    uint32_t _diagFactoryRectCount = 0;
    uint32_t _diagFactoryPixels = 0;
    uint32_t _diagFactoryVuCount = 0;
    uint32_t _diagFactoryVuPixels = 0;
    uint32_t _diagFactoryTotalUs = 0;
    uint32_t _diagFactoryMaxUs = 0;
#endif
    volatile int32_t _diagLvglX1 = -1;
    volatile int32_t _diagLvglY1 = -1;
    volatile int32_t _diagLvglX2 = -1;
    volatile int32_t _diagLvglY2 = -1;
    volatile uint8_t _diagBufferPercent = 0;
    volatile uint8_t _diagCpu0Percent = 0;
    volatile uint8_t _diagCpu1Percent = 0;
    volatile bool _diagCpuValid = false;
    uint8_t _diagLastChunkCount = 0;
#if AXS_FLUSH_DIAG
    uint32_t _diagLastPrepUs = 0;
    uint32_t _diagLastWriteUs = 0;
    bool _diagLastRawFlush = false;
    uint32_t _diagFlushWindowMs = 0;
    uint32_t _diagFlushSamples = 0;
    uint32_t _diagFlushSumUs = 0;
    uint32_t _diagFlushMaxUs = 0;
    uint32_t _diagMapSumUs = 0;
    uint32_t _diagSendSumUs = 0;
    uint32_t _diagPrepSumUs = 0;
    uint32_t _diagWriteSumUs = 0;
    uint32_t _diagRawFlushSamples = 0;
    uint32_t _diagFlushDirtyDuring = 0;
#endif

    bool allocateFrameBuffer() {
        if (_framebuffer) {
            return true;
        }

        const size_t lineBytes = _cfg.memory_width * sizeof(uint16_t);
        _bufferBytes = lineBytes * _cfg.memory_height;
        _framebuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, _bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#if !AXS_USE_LOVYAN_FULL_FLUSH
        _nativeFrame = static_cast<uint16_t*>(heap_caps_aligned_alloc(16, _bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
#if AXS_ENABLE_SCAN_FLUSH
#if AXS_USE_RAW_QIO_FLUSH && AXS_RAW_DIRECT_SCANBUFFER
        _scanBuffer = static_cast<uint16_t*>(heap_caps_aligned_alloc(
            16, _bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        _scanBufferDirectDma = _scanBuffer != nullptr;
#endif
        if (!_scanBuffer) {
            _scanBuffer = static_cast<uint16_t*>(heap_caps_aligned_alloc(
                16, _bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            _scanBufferDirectDma = false;
        }
#if AXS_NATIVE_VISUALIZER_DIRECT_ROWS && AXS_USE_RAW_QIO_FLUSH && AXS_RAW_DIRECT_SCANBUFFER
        _partialLine = static_cast<uint16_t*>(heap_caps_aligned_alloc(
            16, FLUSH_PIXELS * sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (!_partialLine) {
            Serial.println("[display] AXS native VU window unavailable; using stable full frames");
        }
#endif
#endif
        // The framebuffer itself resides in PSRAM, so its 320-entry pointer
        // table need not permanently consume scarce internal RAM either.
        _lineTable = static_cast<uint8_t**>(heap_caps_calloc(
            _cfg.memory_height, sizeof(uint8_t*), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#if AXS_USE_RAW_QIO_FLUSH && AXS_RAW_DIRECT_SCANBUFFER
        // With a DMA-capable PSRAM scan buffer the raw-QSPI path streams it
        // directly.  Do not reserve another 25.6 KiB from internal RAM.
        if (!_scanBufferDirectDma)
#endif
        {
#if AXS_FLUSH_USE_DMA
        _flushLine = static_cast<uint16_t*>(heap_caps_aligned_alloc(16, FLUSH_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
#else
        // The synchronous Lovyan path still needs an internal staging buffer:
        // reading the transfer source from PSRAM more than doubles send time.
        _flushLine = static_cast<uint16_t*>(heap_caps_aligned_alloc(16, FLUSH_PIXELS * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
        }

#if AXS_USE_LOVYAN_FULL_FLUSH
        if (!_framebuffer || !_lineTable || !_flushLine) {
#else
        if (!_framebuffer || !_nativeFrame || !_lineTable ||
            (!_flushLine && !_scanBufferDirectDma)) {
#endif
            freeFrameBuffer();
            return false;
        }
#if AXS_ENABLE_SCAN_FLUSH
        if (!_scanBuffer) {
            freeFrameBuffer();
            return false;
        }
#endif

        for (uint_fast16_t y = 0; y < _cfg.memory_height; ++y) {
            _lineTable[y] = _framebuffer + (y * lineBytes);
        }
        _lines_buffer = _lineTable;
#if AXS_ENABLE_SCAN_FLUSH
        memset(_scanBuffer, 0, _bufferBytes);
#if AXS_USE_RAW_QIO_FLUSH && AXS_RAW_DIRECT_SCANBUFFER
        Serial.printf("[display] AXS scanbuffer direct DMA: %s\n", _scanBufferDirectDma ? "yes" : "no");
#endif
#endif
        return true;
    }

    void freeFrameBuffer() {
        if (_framebuffer) {
            heap_caps_free(_framebuffer);
            _framebuffer = nullptr;
        }
        if (_nativeFrame) {
            heap_caps_free(_nativeFrame);
            _nativeFrame = nullptr;
            _nativeFrameValid = false;
        }
        if (_lineTable) {
            heap_caps_free(_lineTable);
            _lineTable = nullptr;
        }
        if (_scanBuffer) {
            heap_caps_free(_scanBuffer);
            _scanBuffer = nullptr;
            _scanBufferDirectDma = false;
        }
        if (_flushLine) {
            heap_caps_free(_flushLine);
            _flushLine = nullptr;
        }
        if (_partialLine) {
            heap_caps_free(_partialLine);
            _partialLine = nullptr;
        }
        _lines_buffer = nullptr;
    }

    void markFullDirty() {
        _range_mod.top = 0;
        _range_mod.left = 0;
        _range_mod.right = _cfg.memory_width - 1;
        _range_mod.bottom = _cfg.memory_height - 1;
    }

    void markDirtyRect(int32_t x, int32_t y, int32_t w, int32_t h) {
        if (w <= 0 || h <= 0) {
            return;
        }
        const int32_t right = x + w - 1;
        const int32_t bottom = y + h - 1;
        if (_range_mod.empty()) {
            _range_mod.left = x;
            _range_mod.top = y;
            _range_mod.right = right;
            _range_mod.bottom = bottom;
            return;
        }
        if (x < _range_mod.left) _range_mod.left = x;
        if (y < _range_mod.top) _range_mod.top = y;
        if (right > _range_mod.right) _range_mod.right = right;
        if (bottom > _range_mod.bottom) _range_mod.bottom = bottom;
    }

    void clearDirtyRange() {
        _range_mod.top = INT16_MAX;
        _range_mod.left = INT16_MAX;
        _range_mod.right = 0;
        _range_mod.bottom = 0;
    }

    bool shouldUseStablePath() const {
        if (!AXS_ENABLE_PARTIAL_FLUSH) {
            return true;
        }
        if (_stableBootFlushes) {
            return true;
        }
        if (_range_mod.empty()) {
            return true;
        }
        const uint32_t dirtyHeight = static_cast<uint32_t>(_range_mod.bottom - _range_mod.top + 1);
        const uint32_t dirtyWidth = static_cast<uint32_t>(_range_mod.right - _range_mod.left + 1);
        // The vendor QSPI rectangle path is valid for every incomplete
        // logical rectangle.  Only a real complete-screen invalidation needs
        // the column-major full-frame stream.
        return dirtyWidth >= _cfg.memory_width &&
               dirtyHeight >= _cfg.memory_height;
    }

    void updateNativeFrameFromDirty() {
        if (_nativeFrame == nullptr) return;
        const uint16_t width = _cfg.memory_width;
        const uint16_t height = _cfg.memory_height;
        const bool full = !_nativeFrameValid ||
            (_range_mod.left == 0 && _range_mod.top == 0 &&
             _range_mod.right >= width - 1 && _range_mod.bottom >= height - 1);
        if (full) {
            size_t out = 0;
            for (int_fast16_t sx = width - 1; sx >= 0; --sx) {
                for (uint_fast16_t sy = 0; sy < height; ++sy) {
                    const uint16_t* row = reinterpret_cast<const uint16_t*>(_lines_buffer[sy]);
                    uint16_t raw = row[sx];
                    if (_softwareInvert) raw ^= 0xFFFF;
                    _nativeFrame[out++] = raw;
                }
            }
            _nativeFrameValid = true;
            return;
        }
        uint16_t left = _range_mod.left;
        uint16_t top = _range_mod.top;
        uint16_t right = (_range_mod.right < width) ? _range_mod.right : width - 1;
        uint16_t bottom = (_range_mod.bottom < height) ? _range_mod.bottom : height - 1;
        if (left > right || top > bottom) return;
        for (uint16_t x = left; x <= right; ++x) {
            const uint16_t* rowBase = reinterpret_cast<const uint16_t*>(_lines_buffer[0]);
            (void)rowBase;
            const size_t nativeBase = static_cast<size_t>(width - 1 - x) * height;
            for (uint16_t y = top; y <= bottom; ++y) {
                const uint16_t* row = reinterpret_cast<const uint16_t*>(_lines_buffer[y]);
                uint16_t raw = row[x];
                if (_softwareInvert) raw ^= 0xFFFF;
                _nativeFrame[nativeBase + y] = raw;
            }
        }
    }

    void sendFrameRawMapped() {
        updateNativeFrameFromDirty();
        if (_nativeFrame == nullptr) return;
        startQspiMemoryWrite();
        cs_control(false);
        uint8_t chunkCount = 0;
        const size_t totalPixels = static_cast<size_t>(_cfg.memory_width) * _cfg.memory_height;
        for (size_t offset = 0; offset < totalPixels; offset += FLUSH_PIXELS) {
            const size_t count = ((totalPixels - offset) > FLUSH_PIXELS) ? FLUSH_PIXELS : (totalPixels - offset);
            memcpy(_flushLine, &_nativeFrame[offset], count * sizeof(uint16_t));
            if (offset == 0) rawWriteFirstColorChunk(CMD_RAMWR, _flushLine, count);
            else rawWriteNextColorChunk(_flushLine, count);
            yieldAfterRawFlushChunk(++chunkCount);
        }
        rawEndColorWrite();
#if AXS_FLUSH_DIAG
        _diagLastChunkCount = chunkCount;
        _diagLastRawFlush = true;
#endif
    }
    void sendFrameDirect() {
        startQspiMemoryWrite();
        uint_fast16_t outCount = 0;
        uint8_t chunkCount = 0;

        // Match the proven Fusion/VTom path: build the panel's native
        // right-to-left column stream directly from the LVGL line buffers.
        for (int_fast16_t sx = _cfg.memory_width - 1; sx >= 0; --sx) {
            for (uint_fast16_t sy = 0; sy < _cfg.memory_height; ++sy) {
                uint16_t raw;
                memcpy(&raw, &_lines_buffer[sy][sx * sizeof(uint16_t)], sizeof(raw));
                if (_softwareInvert) {
                    raw ^= 0xFFFF;
                }
                _flushLine[outCount++] = raw;

                if (outCount == FLUSH_PIXELS) {
                    _bus->writeBytes(
                        reinterpret_cast<const uint8_t*>(_flushLine),
                        FLUSH_PIXELS * sizeof(uint16_t),
                        true,
                        AXS_FLUSH_USE_DMA != 0
                    );
                    _bus->wait();
                    outCount = 0;
                    yieldAfterFlushChunk(++chunkCount);
                }
            }
        }

        if (outCount) {
            _bus->writeBytes(
                reinterpret_cast<const uint8_t*>(_flushLine),
                outCount * sizeof(uint16_t),
                true,
                AXS_FLUSH_USE_DMA != 0
            );
        }

        _bus->wait();
        cs_control(true);
#if AXS_FLUSH_DIAG
        _diagLastChunkCount = chunkCount;
        _diagLastRawFlush = false;
#endif
    }    void sendDirtyFrameDirect() {
        if (_range_mod.empty()) {
            return;
        }
        uint_fast16_t top = _range_mod.top;
        uint_fast16_t bottom = _range_mod.bottom;
        if (bottom >= _cfg.memory_height) bottom = _cfg.memory_height - 1;
        if (top > bottom) return;
        startQspiMemoryWrite(top, bottom);
        uint_fast16_t outCount = 0;
        uint8_t chunkCount = 0;

        for (int_fast16_t sx = _cfg.memory_width - 1; sx >= 0; --sx) {
            for (uint_fast16_t sy = top; sy <= bottom; ++sy) {
                const uint16_t* row = reinterpret_cast<const uint16_t*>(_lines_buffer[sy]);
                uint16_t raw = row[sx];
                if (_softwareInvert) {
                    raw ^= 0xFFFF;
                }
                _flushLine[outCount++] = raw;

                if (outCount == FLUSH_PIXELS) {
                    _bus->writeBytes(
                        reinterpret_cast<const uint8_t*>(_flushLine),
                        FLUSH_PIXELS * sizeof(uint16_t),
                        true,
                        AXS_FLUSH_USE_DMA != 0
                    );
                    yieldAfterFlushChunk(++chunkCount);
                    outCount = 0;
                }
            }
        }

        if (outCount) {
            _bus->writeBytes(
                reinterpret_cast<const uint8_t*>(_flushLine),
                outCount * sizeof(uint16_t),
                true,
                AXS_FLUSH_USE_DMA != 0
            );
        }
        _bus->wait();
        cs_control(true);
    }

    void updateScanBufferRect(uint_fast16_t left, uint_fast16_t top,
                              uint_fast16_t right, uint_fast16_t bottom) {
        if (_scanBuffer == nullptr) {
            return;
        }
        if (right >= _cfg.memory_width) right = _cfg.memory_width - 1;
        if (bottom >= _cfg.memory_height) bottom = _cfg.memory_height - 1;
        if (left > right || top > bottom) {
            return;
        }

#if AXS_SCAN_MAP_ROW_MAJOR_READ
#if AXS_SCAN_BUILD_YIELD_COLS > 0
        uint8_t yieldCount = 0;
#endif
        for (uint_fast16_t y = top; y <= bottom; ++y) {
            const uint16_t* src = reinterpret_cast<const uint16_t*>(_lines_buffer[y]) + left;
            uint16_t* out = &_scanBuffer[static_cast<size_t>(_cfg.memory_width - 1 - left) * _cfg.memory_height + y];
            for (uint_fast16_t x = left; x <= right; ++x) {
                *out = *src++;
                out -= _cfg.memory_height;
            }
#if AXS_SCAN_BUILD_YIELD_COLS > 0
            if ((++yieldCount % AXS_SCAN_BUILD_YIELD_COLS) == 0) {
                taskYIELD();
            }
#endif
        }
#else
#if AXS_SCAN_BUILD_YIELD_COLS > 0
        uint8_t yieldCount = 0;
#endif
        for (uint_fast16_t x = left; x <= right; ++x) {
            const uint_fast16_t panelX = _cfg.memory_width - 1 - x;
            uint16_t* out = &_scanBuffer[static_cast<size_t>(panelX) * _cfg.memory_height + top];
            for (uint_fast16_t y = top; y <= bottom; ++y) {
                const uint16_t* row = reinterpret_cast<const uint16_t*>(_lines_buffer[y]);
                uint16_t raw = row[x];
                *out++ = raw;
            }
#if AXS_SCAN_BUILD_YIELD_COLS > 0
            if ((++yieldCount % AXS_SCAN_BUILD_YIELD_COLS) == 0) {
                taskYIELD();
            }
#endif
        }
#endif
    }

    void updateScanBufferFromFrame() {
        if (_scanBuffer == nullptr || _range_mod.empty()) {
            return;
        }

        uint_fast16_t left = _range_mod.left;
        uint_fast16_t top = _range_mod.top;
        uint_fast16_t right = _range_mod.right;
        uint_fast16_t bottom = _range_mod.bottom;
        if (right >= _cfg.memory_width) right = _cfg.memory_width - 1;
        if (bottom >= _cfg.memory_height) bottom = _cfg.memory_height - 1;
        if (left > right || top > bottom) {
            return;
        }

#if AXS_SCAN_MAP_ROW_MAJOR_READ
#if AXS_SCAN_BUILD_YIELD_COLS > 0
        uint8_t yieldCount = 0;
#endif
        for (uint_fast16_t y = top; y <= bottom; ++y) {
            const uint16_t* src = reinterpret_cast<const uint16_t*>(_lines_buffer[y]) + left;
            uint16_t* out = &_scanBuffer[static_cast<size_t>(_cfg.memory_width - 1 - left) * _cfg.memory_height + y];
            for (uint_fast16_t x = left; x <= right; ++x) {
                *out = *src++;
                out -= _cfg.memory_height;
            }
#if AXS_SCAN_BUILD_YIELD_COLS > 0
            if ((++yieldCount % AXS_SCAN_BUILD_YIELD_COLS) == 0) {
                taskYIELD();
            }
#endif
        }
#else
#if AXS_SCAN_BUILD_YIELD_COLS > 0
        uint8_t yieldCount = 0;
#endif
        for (uint_fast16_t x = left; x <= right; ++x) {
            const uint_fast16_t panelX = _cfg.memory_width - 1 - x;
            uint16_t* out = &_scanBuffer[static_cast<size_t>(panelX) * _cfg.memory_height + top];
            for (uint_fast16_t y = top; y <= bottom; ++y) {
                const uint16_t* row = reinterpret_cast<const uint16_t*>(_lines_buffer[y]);
                uint16_t raw = row[x];
                *out++ = raw;
            }
#if AXS_SCAN_BUILD_YIELD_COLS > 0
            if ((++yieldCount % AXS_SCAN_BUILD_YIELD_COLS) == 0) {
                taskYIELD();
            }
#endif
        }
#endif
    }

    void sendScanBuffer() {
        if (_scanBuffer == nullptr) {
            return;
        }

        startQspiMemoryWrite();
        const size_t totalPixels = static_cast<size_t>(_cfg.memory_width) * _cfg.memory_height;
        uint8_t chunkCount = 0;
#if AXS_USE_RAW_QIO_FLUSH
        bool firstChunk = true;
#endif
#if AXS_FLUSH_DIAG
        uint32_t diagPrepUs = 0;
        uint32_t diagWriteUs = 0;
        bool diagRawUsed = false;
#endif

        for (size_t offset = 0; offset < totalPixels; offset += FLUSH_PIXELS) {
            const size_t count = ((totalPixels - offset) > FLUSH_PIXELS) ? FLUSH_PIXELS : (totalPixels - offset);
            const uint16_t* chunkPixels = _flushLine;

#if AXS_USE_RAW_QIO_FLUSH && AXS_RAW_DIRECT_SCANBUFFER
            if (rawQioFlushReady() && _scanBufferDirectDma && !_softwareInvert) {
                chunkPixels = &_scanBuffer[offset];
            } else
#endif
            if (chunkPixels == _flushLine) {
#if AXS_FLUSH_DIAG
                const uint32_t prepStartUs = micros();
#endif
                if (_softwareInvert) {
                    for (size_t i = 0; i < count; ++i) {
                        _flushLine[i] = _scanBuffer[offset + i] ^ 0xFFFF;
                    }
                } else {
                    memcpy(_flushLine, &_scanBuffer[offset], count * sizeof(uint16_t));
                }
#if AXS_FLUSH_DIAG
                diagPrepUs += micros() - prepStartUs;
#endif
            }

#if AXS_USE_RAW_QIO_FLUSH
            if (rawQioFlushReady()) {
#if AXS_FLUSH_DIAG
                const uint32_t writeStartUs = micros();
                diagRawUsed = true;
#endif
#if AXS_RAW_QIO_QUEUED_FLUSH
                bool queued = false;
                if (firstChunk) {
                    cs_control(false);
                    queued = rawQueueFirstColorChunk(CMD_RAMWR, chunkPixels,
                                                       count, 0);
                    firstChunk = false;
                } else {
                    queued = rawQueueNextColorChunk(chunkPixels, count, 0);
                }
                if (queued) {
                    if (!rawWaitQueuedChunk()) {
                        Serial.println("[display] AXS raw queued chunk failed");
                    }
                } else {
                    // Preserve a complete frame if the queue is temporarily
                    // unavailable; the next frame can return to queued DMA.
                    if (offset == 0) {
                        rawWriteFirstColorChunk(CMD_RAMWR, chunkPixels, count);
                    } else {
                        rawWriteNextColorChunk(chunkPixels, count);
                    }
                }
#else
                if (firstChunk) {
                    rawWriteFirstColorChunk(CMD_RAMWR, chunkPixels, count);
                    firstChunk = false;
                } else {
                    rawWriteNextColorChunk(chunkPixels, count);
                }
#endif
#if AXS_FLUSH_DIAG
                diagWriteUs += micros() - writeStartUs;
#endif
                yieldAfterRawFlushChunk(++chunkCount);
                continue;
            }
#endif
#if AXS_FLUSH_DIAG
            const uint32_t writeStartUs = micros();
#endif
            _bus->writeBytes(
                reinterpret_cast<const uint8_t*>(_flushLine),
                count * sizeof(uint16_t),
                true,
                true
            );
#if AXS_FLUSH_DIAG
            diagWriteUs += micros() - writeStartUs;
#endif
            yieldAfterFlushChunk(++chunkCount);
        }

#if AXS_USE_RAW_QIO_FLUSH
        if (rawQioFlushReady()) {
            _diagLastChunkCount = chunkCount;
#if AXS_FLUSH_DIAG
            _diagLastPrepUs = diagPrepUs;
            _diagLastWriteUs = diagWriteUs;
            _diagLastRawFlush = diagRawUsed;
#endif
            rawEndColorWrite();
            return;
        }
#endif
        _bus->wait();
        _diagLastChunkCount = chunkCount;
#if AXS_FLUSH_DIAG
        _diagLastPrepUs = diagPrepUs;
        _diagLastWriteUs = diagWriteUs;
        _diagLastRawFlush = diagRawUsed;
#endif
        cs_control(true);
    }

    // Exact Guition ESP-LCD LV_DISP_ROT_270 QSPI transform.  The reference
    // flush maps a logical (left,top)-(right,bottom) rectangle to
    // CASET(top,bottom), stores pixels right-to-left by column, and chooses
    // RAMWR only when the transformed Y origin (width-1-right) is zero.
    // QSPI deliberately omits RASET in the vendor driver.
    uint8_t sendScanBufferRect(uint_fast16_t left, uint_fast16_t top,
                               uint_fast16_t right, uint_fast16_t bottom) {
#if AXS_USE_RAW_QIO_FLUSH
        if (_scanBuffer == nullptr || _flushLine == nullptr ||
            !rawQioFlushReady() || left > right || top > bottom ||
            right >= _cfg.memory_width || bottom >= _cfg.memory_height) {
            return 0;
        }
        startQspiMemoryWrite(top, bottom);
        const uint_fast16_t transformedYStart =
            _cfg.memory_width - 1 - right;
        const size_t rowCount = static_cast<size_t>(bottom - top + 1);
        size_t outCount = 0;
        uint8_t chunkCount = 0;
        bool firstChunk = true;
        for (int_fast16_t x = right; x >= static_cast<int_fast16_t>(left); --x) {
            const uint_fast16_t panelX =
                _cfg.memory_width - 1 - static_cast<uint_fast16_t>(x);
            const uint16_t* src =
                &_scanBuffer[static_cast<size_t>(panelX) * _cfg.memory_height + top];
            for (size_t row = 0; row < rowCount; ++row) {
                uint16_t value = src[row];
                if (_softwareInvert) value ^= 0xFFFF;
                _flushLine[outCount++] = value;
                if (outCount == FLUSH_PIXELS) {
                    if (firstChunk) {
                        rawWriteFirstColorChunk(transformedYStart == 0 ? CMD_RAMWR : CMD_RAMWRC,
                                                _flushLine, outCount);
                        firstChunk = false;
                    } else {
                        rawWriteNextColorChunk(_flushLine, outCount);
                    }
                    outCount = 0;
                    yieldAfterRawFlushChunk(++chunkCount);
                }
            }
        }
        if (outCount) {
            if (firstChunk) {
                rawWriteFirstColorChunk(transformedYStart == 0 ? CMD_RAMWR : CMD_RAMWRC,
                                        _flushLine, outCount);
            } else {
                rawWriteNextColorChunk(_flushLine, outCount);
            }
            yieldAfterFlushChunk(++chunkCount);
        }
        rawEndColorWrite();
        _diagLastChunkCount = chunkCount;
#if AXS_FLUSH_DIAG
        _diagLastRawFlush = true;
#endif
        return chunkCount;
#else
        (void)left;
        (void)top;
        (void)right;
        (void)bottom;
        return 0;
#endif
    }

    uint8_t sendNativeVisualizerPixels(uint_fast16_t left, uint_fast16_t top,
                                       uint_fast16_t width, uint_fast16_t height,
                                       const uint16_t* pixels) {
#if AXS_USE_RAW_QIO_FLUSH
        if (_flushLine == nullptr || !pixels || !rawQioFlushReady() ||
            width == 0 || height == 0) {
            return 0;
        }
        const uint_fast16_t bottom = top + height - 1;
        startQspiMemoryWrite(top, bottom);
        size_t outCount = 0;
        uint8_t chunkCount = 0;
        bool firstChunk = true;
        // Match the proven factory LV_DISP_ROT_90 packing used by the normal
        // full UI: logical columns advance left-to-right, while each column
        // is sent bottom-to-top.  The former reverse/raw ordering addressed
        // the VU outside its visible logical half.
        for (uint_fast16_t col = 0; col < width; ++col) {
            for (int_fast16_t row = static_cast<int_fast16_t>(height) - 1;
                 row >= 0; --row) {
                uint16_t value = pixels[static_cast<size_t>(row) * width + col];
                if (_softwareInvert) value ^= 0xFFFF;
                _flushLine[outCount++] = value;
                if (outCount == FLUSH_PIXELS) {
                    if (firstChunk) {
                        // An isolated VU CASET stream must reset the write
                        // cursor; RAMWRC inherits the preceding UI cursor.
                        rawWriteFirstColorChunk(CMD_RAMWR, _flushLine, outCount);
                        firstChunk = false;
                    } else {
                        rawWriteNextColorChunk(_flushLine, outCount);
                    }
                    outCount = 0;
                    yieldAfterRawFlushChunk(++chunkCount);
                }
            }
        }
        if (outCount) {
            if (firstChunk) {
                rawWriteFirstColorChunk(CMD_RAMWR, _flushLine, outCount);
            } else {
                rawWriteNextColorChunk(_flushLine, outCount);
            }
            ++chunkCount;
        }
        rawEndColorWrite();
        return chunkCount;
#else
        (void)left;
        (void)top;
        (void)width;
        (void)height;
        (void)pixels;
        return 0;
#endif
    }

    // Ported line-for-line in behaviour from Guition's lv_port.c
    // LV_DISP_ROT_90 branch.  The Guition demo declares a 320x480 native
    // panel and rotates it 90 degrees to LVGL's 480x320 landscape space.
    // Each transport block has its own colour
    // command, exactly as esp_lcd_panel_draw_bitmap() does; it is not a
    // continuation of a previous raw SPI transaction.
    bool sendFactoryRot270Blocks(int32_t x, int32_t y, int32_t width,
                                 int32_t height, const uint16_t* source) {
#if AXS_USE_RAW_QIO_FLUSH
        if (!source || width <= 0 || height <= 0 || !_flushLine ||
            !rawQioFlushReady()) {
            return false;
        }

        const int32_t maxWidth = static_cast<int32_t>(FLUSH_PIXELS) / height;
        if (maxWidth <= 0) {
            return false;
        }

        bool sent = false;
        int32_t xStart = x;
        const int32_t logicalXEnd = x + width - 1;
        while (xStart <= logicalXEnd) {
            const int32_t transWidth = (logicalXEnd - xStart + 1 > maxWidth)
                ? maxWidth
                : (logicalXEnd - xStart + 1);
            const int32_t xEnd = xStart + transWidth - 1;
            const size_t pixelCount =
                static_cast<size_t>(transWidth) * static_cast<size_t>(height);

            // Factory: to[localX * height + (height - localY - 1)]
            //        = from[localY * width + (xStart - x) + localX]
            size_t out = 0;
            for (int32_t logicalX = xStart; logicalX <= xEnd; ++logicalX) {
                const int32_t sourceX = logicalX - x;
                for (int32_t logicalY = height - 1; logicalY >= 0; --logicalY) {
                    _flushLine[out++] = source[
                        static_cast<size_t>(logicalY) * static_cast<size_t>(width) +
                        static_cast<size_t>(sourceX)];
                }
            }

            const uint8_t column[] = {
                static_cast<uint8_t>((_cfg.memory_height - y - height) >> 8),
                static_cast<uint8_t>((_cfg.memory_height - y - height) & 0xFF),
                static_cast<uint8_t>((_cfg.memory_height - y - 1) >> 8),
                static_cast<uint8_t>((_cfg.memory_height - y - 1) & 0xFF),
            };
            rawSendCmd(CMD_CASET, column, sizeof(column));

            // Factory driver: y_draw_start = x_start.
            const uint8_t memoryWrite =
                (xStart == 0) ? CMD_RAMWR : CMD_RAMWRC;
            rawWriteFirstColorChunk(memoryWrite, _flushLine, pixelCount);
            rawEndColorWrite();
            sent = true;
            xStart = xEnd + 1;
        }
        return sent;
#else
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        (void)source;
        return false;
#endif
    }

    void scheduleFlush() {
        const TickType_t due = xTaskGetTickCount() + pdMS_TO_TICKS(AXS_FLUSH_COALESCE_MS);
        if (!_flushRequested || _flushDueTick == 0 || due < _flushDueTick) {
            _flushDueTick = due;
        }
        _flushRequested = true;
    }

    void requestFlush() {
        if (_flushInProgress) {
            _dirtyDuringFlush = true;
            scheduleFlush();
            return;
        }
        if (!_range_mod.empty()) {
            scheduleFlush();
        }
    }

    void startFlushTask() {
        if (_flushTaskHandle == nullptr) {
            _flushTaskStop = false;
            xTaskCreatePinnedToCore(flushTaskEntry, "AXSFlush", AXS_FLUSH_TASK_STACK_WORDS, this, AXS_FLUSH_TASK_PRIORITY, &_flushTaskHandle, AXS_FLUSH_TASK_CORE);
        }
    }

    void stopFlushTask() {
        _flushTaskStop = true;
        if (_flushTaskHandle != nullptr) {
            vTaskDelay(pdMS_TO_TICKS(20));
            _flushTaskHandle = nullptr;
        }
    }

    static void flushTaskEntry(void* arg) {
        static_cast<Panel_AXS15231B*>(arg)->flushTaskLoop();
    }

    void flushTaskLoop() {
        while (!_flushTaskStop) {
            if (_flushRequested && !_sleeping) {
                const TickType_t now = xTaskGetTickCount();
                if (_flushDueTick && now < _flushDueTick) {
                    vTaskDelay(_flushDueTick - now);
                    continue;
                }

#if AXS_MIN_FRAME_INTERVAL_MS > 0
                const TickType_t minFrameTicks = pdMS_TO_TICKS(AXS_MIN_FRAME_INTERVAL_MS);
                if (_lastPhysicalFlushTick != 0 && minFrameTicks > 0) {
                    const TickType_t elapsed = now - _lastPhysicalFlushTick;
                    if (elapsed < minFrameTicks) {
                        vTaskDelay(minFrameTicks - elapsed);
                        continue;
                    }
                }
#endif

                _flushRequested = false;
                _dirtyDuringFlush = false;
                display(0, 0, _width, _height);
            }
            vTaskDelay(pdMS_TO_TICKS(AXS_FLUSH_TASK_DELAY_MS));
        }
        vTaskDelete(nullptr);
    }

    void lockBus() {
        if (_busMutex) {
            xSemaphoreTake(_busMutex, portMAX_DELAY);
        }
    }

    void unlockBus() {
        if (_busMutex) {
            xSemaphoreGive(_busMutex);
        }
    }

    void lockFrame() {
        if (_frameMutex) {
            xSemaphoreTakeRecursive(_frameMutex, portMAX_DELAY);
        }
    }

    void unlockFrame() {
        if (_frameMutex) {
            xSemaphoreGiveRecursive(_frameMutex);
        }
    }

    // Exact lcd_init_cmds table from Guition's JC3248W535C ESP-LCD demo.
    // It is sent before DISPON, just as esp_lcd_panel_init() does there.
    void applyGuitionQspiInit() {
        struct InitCommand {
            uint8_t cmd;
            uint8_t len;
            uint16_t delayMs;
            uint8_t data[31];
        };
        static const InitCommand kInit[] = {
            {0xBB, 8, 0, {0x00,0x00,0x00,0x00,0x00,0x00,0x5A,0xA5}},
            {0xA0,17, 0, {0xC0,0x10,0x00,0x02,0x00,0x00,0x04,0x3F,0x20,0x05,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00}},
            {0xA2,31, 0, {0x30,0x3C,0x24,0x14,0xD0,0x20,0xFF,0xE0,0x40,0x19,0x80,0x80,0x80,0x20,0xF9,0x10,0x02,0xFF,0xFF,0xF0,0x90,0x01,0x32,0xA0,0x91,0xE0,0x20,0x7F,0xFF,0x00,0x5A}},
            {0xD0,30, 0, {0xE0,0x40,0x51,0x24,0x08,0x05,0x10,0x01,0x20,0x15,0x42,0xC2,0x22,0x22,0xAA,0x03,0x10,0x12,0x60,0x14,0x1E,0x51,0x15,0x00,0x8A,0x20,0x00,0x03,0x3A,0x12}},
            {0xA3,22, 0, {0xA0,0x06,0xAA,0x00,0x08,0x02,0x0A,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x55,0x55}},
            {0xC1,30, 0, {0x31,0x04,0x02,0x02,0x71,0x05,0x24,0x55,0x02,0x00,0x41,0x00,0x53,0xFF,0xFF,0xFF,0x4F,0x52,0x00,0x4F,0x52,0x00,0x45,0x3B,0x0B,0x02,0x0D,0x00,0xFF,0x40}},
            {0xC3,11, 0, {0x00,0x00,0x00,0x50,0x03,0x00,0x00,0x00,0x01,0x80,0x01}},
            {0xC4,29, 0, {0x00,0x24,0x33,0x80,0x00,0xEA,0x64,0x32,0xC8,0x64,0xC8,0x32,0x90,0x90,0x11,0x06,0xDC,0xFA,0x00,0x00,0x80,0xFE,0x10,0x10,0x00,0x0A,0x0A,0x44,0x50}},
            {0xC5,23, 0, {0x18,0x00,0x00,0x03,0xFE,0x3A,0x4A,0x20,0x30,0x10,0x88,0xDE,0x0D,0x08,0x0F,0x0F,0x01,0x3A,0x4A,0x20,0x10,0x10,0x00}},
            {0xC6,20, 0, {0x05,0x0A,0x05,0x0A,0x00,0xE0,0x2E,0x0B,0x12,0x22,0x12,0x22,0x01,0x03,0x00,0x3F,0x6A,0x18,0xC8,0x22}},
            {0xC7,20, 0, {0x50,0x32,0x28,0x00,0xA2,0x80,0x8F,0x00,0x80,0xFF,0x07,0x11,0x9C,0x67,0xFF,0x24,0x0C,0x0D,0x0E,0x0F}},
            {0xC9, 4, 0, {0x33,0x44,0x44,0x01}},
            {0xCF,27, 0, {0x2C,0x1E,0x88,0x58,0x13,0x18,0x56,0x18,0x1E,0x68,0x88,0x00,0x65,0x09,0x22,0xC4,0x0C,0x77,0x22,0x44,0xAA,0x55,0x08,0x08,0x12,0xA0,0x08}},
            {0xD5,30, 0, {0x40,0x8E,0x8D,0x01,0x35,0x04,0x92,0x74,0x04,0x92,0x74,0x04,0x08,0x6A,0x04,0x46,0x03,0x03,0x03,0x03,0x82,0x01,0x03,0x00,0xE0,0x51,0xA1,0x00,0x00,0x00}},
            {0xD6,30, 0, {0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x93,0x00,0x01,0x83,0x07,0x07,0x00,0x07,0x07,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x84,0x00,0x20,0x01,0x00}},
            {0xD7,19, 0, {0x03,0x01,0x0B,0x09,0x0F,0x0D,0x1E,0x1F,0x18,0x1D,0x1F,0x19,0x40,0x8E,0x04,0x00,0x20,0xA0,0x1F}},
            {0xD8,12, 0, {0x02,0x00,0x0A,0x08,0x0E,0x0C,0x1E,0x1F,0x18,0x1D,0x1F,0x19}},
            {0xD9,12, 0, {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}},
            {0xDD,12, 0, {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}},
            {0xDF, 8, 0, {0x44,0x73,0x4B,0x69,0x00,0x0A,0x02,0x90}},
            {0xE0,17, 0, {0x3B,0x28,0x10,0x16,0x0C,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x13,0x2C,0x33,0x28,0x0D}},
            {0xE1,17, 0, {0x37,0x28,0x10,0x16,0x0B,0x06,0x11,0x28,0x5C,0x21,0x0D,0x35,0x14,0x2C,0x33,0x28,0x0F}},
            {0xE2,17, 0, {0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D}},
            {0xE3,17, 0, {0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x35,0x44,0x32,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F}},
            {0xE4,17, 0, {0x3B,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x3A,0x2F,0x0D}},
            {0xE5,17, 0, {0x37,0x07,0x12,0x18,0x0E,0x0D,0x17,0x39,0x44,0x2E,0x0C,0x14,0x14,0x36,0x32,0x2F,0x0F}},
            {0xA4,16, 0, {0x85,0x85,0x95,0x82,0xAF,0xAA,0xAA,0x80,0x10,0x30,0x40,0x40,0x20,0xFF,0x60,0x30}},
            {0xA4, 4, 0, {0x85,0x85,0x95,0x85}},
            {0xBB, 8, 0, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
            {0x13, 0, 0, {}},
            {0x11, 0,120, {}},
            {0x2C, 4, 0, {0x00,0x00,0x00,0x00}},
        };
        for (const auto& command : kInit) {
            if (command.len) {
                sendCommandData(command.cmd, command.data, command.len);
            } else {
                sendCommand(command.cmd);
            }
            if (command.delayMs) delay(command.delayMs);
        }
    }

    void writeQspiCommandPrefix(uint8_t cmd) {
        _bus->writeCommand(0x02, 8);
        _bus->writeCommand(0x00, 8);
        _bus->writeCommand(cmd, 8);
        _bus->writeCommand(0x00, 8);
    }

    void yieldAfterFlushChunk(uint8_t chunkCount) {
#if AXS_FLUSH_CHUNK_YIELD
#if AXS_FLUSH_CHUNK_DELAY_EVERY > 0
        if ((chunkCount % AXS_FLUSH_CHUNK_DELAY_EVERY) == 0) {
            vTaskDelay(pdMS_TO_TICKS(AXS_FLUSH_CHUNK_DELAY_MS));
        } else {
            taskYIELD();
        }
#else
        (void)chunkCount;
        taskYIELD();
#endif
#else
        (void)chunkCount;
#endif
    }

    void yieldAfterRawFlushChunk(uint8_t chunkCount) {
#if AXS_RAW_FLUSH_YIELD_EVERY > 0
        if ((chunkCount % AXS_RAW_FLUSH_YIELD_EVERY) == 0) {
            taskYIELD();
        }
#else
        (void)chunkCount;
#endif
    }

    void sendCommand(uint8_t cmd) {
        lockBus();
#if AXS_USE_RAW_QIO_FLUSH
        if (rawQioFlushReady()) {
            rawSendCmd(cmd);
            unlockBus();
            return;
        }
#endif
        const bool ownTransaction = !_inBusTransaction;
        if (ownTransaction) {
            _bus->beginTransaction();
            _inBusTransaction = true;
        }
        cs_control(false);
        writeQspiCommandPrefix(cmd);
        _bus->wait();
        cs_control(true);
        if (ownTransaction) {
            _bus->endTransaction();
            _inBusTransaction = false;
        }
        unlockBus();
    }

    void sendCommandData(uint8_t cmd, const uint8_t* data, size_t len) {
        lockBus();
#if AXS_USE_RAW_QIO_FLUSH
        if (rawQioFlushReady()) {
            rawSendCmd(cmd, data, len);
            unlockBus();
            return;
        }
#endif
        const bool ownTransaction = !_inBusTransaction;
        if (ownTransaction) {
            _bus->beginTransaction();
            _inBusTransaction = true;
        }
        cs_control(false);
        writeQspiCommandPrefix(cmd);
        for (size_t i = 0; i < len; ++i) {
            _bus->writeCommand(data[i], 8);
        }
        _bus->wait();
        cs_control(true);
        if (ownTransaction) {
            _bus->endTransaction();
            _inBusTransaction = false;
        }
        unlockBus();
    }

#if AXS_USE_RAW_QIO_FLUSH
    bool rawQioFlushReady() const {
        return _rawSpi != nullptr;
    }

    esp_err_t rawTransmit(spi_transaction_t* transaction) {
        if (!_rawSpi || transaction == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
#if AXS_RAW_QIO_INTERRUPT_WAIT
        return spi_device_transmit(_rawSpi, transaction);
#else
        return spi_device_polling_transmit(_rawSpi, transaction);
#endif
    }

    void preinitRawQioBus() {
        if (_rawBusPreinitOwned) {
            return;
        }

        spi_bus_config_t buscfg{};
        buscfg.data0_io_num = TFT_D0;
        buscfg.data1_io_num = TFT_D1;
        buscfg.sclk_io_num = TFT_SCK;
        buscfg.data2_io_num = TFT_D2;
        buscfg.data3_io_num = TFT_D3;
        buscfg.max_transfer_sz = AXS_FLUSH_PIXELS * sizeof(uint16_t) + 16;
        buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;

        const esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (ret == ESP_OK) {
            _rawBusPreinitOwned = true;
            Serial.printf("[display] AXS raw QIO bus preinit: max_transfer=%u\n",
                          static_cast<unsigned>(buscfg.max_transfer_sz));
        } else if (ret != ESP_ERR_INVALID_STATE) {
            Serial.printf("[display] AXS raw QIO bus preinit failed: %d\n", ret);
        }
    }

    void releaseRawQioBusPreinit() {
        if (_rawBusPreinitOwned) {
            spi_bus_free(SPI3_HOST);
            _rawBusPreinitOwned = false;
        }
    }

    void initRawQioFlush() {
        if (_rawSpi) {
            return;
        }

        spi_device_interface_config_t devcfg{};
        devcfg.command_bits = 8;
        devcfg.address_bits = 24;
        // The factory AXS15231B ESP-LCD transport uses SPI mode 3.  The
        // native VU device shares that physical QSPI bus and must use the
        // same clock phase; mode 0 could report a completed transfer while
        // the panel sampled an unstable VU stream.
        devcfg.mode = 3;
        devcfg.clock_speed_hz = AXS_RAW_QIO_SPI_FREQ;
        devcfg.spics_io_num = -1;
        devcfg.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
        devcfg.queue_size = 2;

        const esp_err_t ret = spi_bus_add_device(SPI3_HOST, &devcfg, &_rawSpi);
        if (ret != ESP_OK) {
            _rawSpi = nullptr;
            Serial.printf("[display] AXS raw QIO flush unavailable: %d\n", ret);
        } else {
            Serial.printf("[display] AXS raw QIO flush: %lu Hz\n", static_cast<unsigned long>(AXS_RAW_QIO_SPI_FREQ));
        }
    }

    void releaseRawQioFlush() {
        if (_rawSpi) {
            spi_bus_remove_device(_rawSpi);
            _rawSpi = nullptr;
        }
    }

    void rawSendCmd(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0) {
        if (!_rawSpi) {
            return;
        }
        spi_transaction_t t{};
        t.cmd = 0x02;
        t.addr = static_cast<uint32_t>(cmd) << 8;
        t.tx_buffer = data;
        t.length = len * 8;
        cs_control(false);
        const esp_err_t ret = rawTransmit(&t);
        cs_control(true);
        if (ret != ESP_OK) {
            Serial.printf("[display] AXS raw cmd 0x%02X failed: %d\n", cmd, ret);
        }
    }

    bool rawQueueFirstColorChunk(uint8_t cmd, const uint16_t* pixels, size_t count, uint8_t slot) {
        if (!_rawSpi) return false;
        auto& t = _queuedTransactions[slot & 1];
        memset(&t, 0, sizeof(t));
        t.base.flags = SPI_TRANS_MODE_QIO;
        t.base.cmd = SEND_PIXELS;
        t.base.addr = static_cast<uint32_t>(cmd) << 8;
        t.base.tx_buffer = pixels;
        t.base.length = count * 16;
        return spi_device_queue_trans(_rawSpi, reinterpret_cast<spi_transaction_t*>(&t), portMAX_DELAY) == ESP_OK;
    }

    bool rawQueueNextColorChunk(const uint16_t* pixels, size_t count, uint8_t slot) {
        if (!_rawSpi) return false;
        auto& t = _queuedTransactions[slot & 1];
        memset(&t, 0, sizeof(t));
        t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                       SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        t.command_bits = 0;
        t.address_bits = 0;
        t.dummy_bits = 0;
        t.base.tx_buffer = pixels;
        t.base.length = count * 16;
        return spi_device_queue_trans(_rawSpi, reinterpret_cast<spi_transaction_t*>(&t), portMAX_DELAY) == ESP_OK;
    }

    bool rawWaitQueuedChunk() {
        spi_transaction_t* result = nullptr;
        return _rawSpi && spi_device_get_trans_result(_rawSpi, &result, portMAX_DELAY) == ESP_OK;
    }
    void rawWriteFirstColorChunk(uint8_t cmd, const uint16_t* pixels, size_t count) {
        if (!_rawSpi) {
            return;
        }
        spi_transaction_t t{};
        t.flags = SPI_TRANS_MODE_QIO;
        t.cmd = SEND_PIXELS;
        t.addr = static_cast<uint32_t>(cmd) << 8;
        t.tx_buffer = pixels;
        t.length = count * 16;
        cs_control(false);
        const esp_err_t ret = rawTransmit(&t);
        if (ret != ESP_OK) {
            Serial.printf("[display] AXS raw first chunk failed: %d\n", ret);
        }
    }

    void rawWriteNextColorChunk(const uint16_t* pixels, size_t count) {
        if (!_rawSpi) {
            return;
        }
        spi_transaction_ext_t t{};
        t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                       SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        t.command_bits = 0;
        t.address_bits = 0;
        t.dummy_bits = 0;
        t.base.tx_buffer = pixels;
        t.base.length = count * 16;
        const esp_err_t ret = rawTransmit(reinterpret_cast<spi_transaction_t*>(&t));
        if (ret != ESP_OK) {
            Serial.printf("[display] AXS raw next chunk failed: %d\n", ret);
        }
    }

    void rawEndColorWrite() {
        cs_control(true);
    }
#endif
    void startQspiMemoryWrite(uint_fast16_t scanStart = 0,
                              uint_fast16_t scanEnd = UINT_FAST16_MAX) {
        // The AXS15231B QSPI memory stream is a 320-pixel-high column stream,
        // right edge to left edge.  Per the factory ESP-LCD driver, QSPI uses
        // CASET alone; RASET is for the non-QSPI transport.
        if (scanEnd == UINT_FAST16_MAX || scanEnd >= _cfg.panel_height) {
            scanEnd = _cfg.panel_height - 1;
        }
        if (scanStart > scanEnd) scanStart = 0;
        const uint8_t col[] = {
            static_cast<uint8_t>(scanStart >> 8),
            static_cast<uint8_t>(scanStart & 0xFF),
            static_cast<uint8_t>(scanEnd >> 8),
            static_cast<uint8_t>(scanEnd & 0xFF)
        };

#if AXS_USE_RAW_QIO_FLUSH && !AXS_USE_LOVYAN_FULL_FLUSH
        if (rawQioFlushReady()) {
            rawSendCmd(CMD_CASET, col, sizeof(col));
            return;
        }
#endif

        cs_control(false);
        writeQspiCommandPrefix(CMD_CASET);
        for (size_t i = 0; i < sizeof(col); ++i) {
            _bus->writeCommand(col[i], 8);
        }
        _bus->wait();
        cs_control(true);

        const uint8_t writeCmd = (scanStart == 0) ? CMD_RAMWR : CMD_RAMWRC;
        cs_control(false);
        _bus->writeCommand(SEND_PIXELS, 8);
        _bus->writeCommand(0x00, 8);
        _bus->writeCommand(writeCmd, 8);
        _bus->writeCommand(0x00, 8);
        _bus->wait();
    }

    // Program an actual portrait GRAM rectangle for the native AXS15231B
    // QSPI interface.  The stock vendor driver omits RASET in QSPI mode and
    // therefore only works for a top-to-bottom full-frame stream.  Supplying
    // both axes is required for an isolated landscape VU rectangle.
    void startQspiRectWrite(uint_fast16_t columnStart, uint_fast16_t columnEnd,
                            uint_fast16_t rowStart, uint_fast16_t rowEnd) {
        const uint8_t column[] = {
            static_cast<uint8_t>(columnStart >> 8),
            static_cast<uint8_t>(columnStart & 0xFF),
            static_cast<uint8_t>(columnEnd >> 8),
            static_cast<uint8_t>(columnEnd & 0xFF)
        };
        const uint8_t row[] = {
            static_cast<uint8_t>(rowStart >> 8),
            static_cast<uint8_t>(rowStart & 0xFF),
            static_cast<uint8_t>(rowEnd >> 8),
            static_cast<uint8_t>(rowEnd & 0xFF)
        };
#if AXS_USE_RAW_QIO_FLUSH && !AXS_USE_LOVYAN_FULL_FLUSH && !AXS_FACTORY_DIRECT_LVGL
        if (rawQioFlushReady()) {
            rawSendCmd(CMD_CASET, column, sizeof(column));
            rawSendCmd(CMD_RASET, row, sizeof(row));
            return;
        }
#endif
        cs_control(false);
        writeQspiCommandPrefix(CMD_CASET);
        for (uint8_t value : column) {
            _bus->writeCommand(value, 8);
        }
        _bus->wait();
        cs_control(true);

        cs_control(false);
        writeQspiCommandPrefix(CMD_RASET);
        for (uint8_t value : row) {
            _bus->writeCommand(value, 8);
        }
        _bus->wait();
        cs_control(true);
    }

    void startQspiPartialDisplay(uint_fast16_t rowStart, uint_fast16_t rowEnd,
                                 uint_fast16_t columnStart, uint_fast16_t columnEnd) {
        const uint8_t rows[] = {
            static_cast<uint8_t>(rowStart >> 8), static_cast<uint8_t>(rowStart & 0xFF),
            static_cast<uint8_t>(rowEnd >> 8), static_cast<uint8_t>(rowEnd & 0xFF)
        };
        const uint8_t columns[] = {
            static_cast<uint8_t>(columnStart >> 8), static_cast<uint8_t>(columnStart & 0xFF),
            static_cast<uint8_t>(columnEnd >> 8), static_cast<uint8_t>(columnEnd & 0xFF)
        };
#if AXS_USE_RAW_QIO_FLUSH && !AXS_USE_LOVYAN_FULL_FLUSH
        if (rawQioFlushReady()) {
            rawSendCmd(CMD_PTLAR_ROWS, rows, sizeof(rows));
            rawSendCmd(CMD_PTLAR_COLUMNS, columns, sizeof(columns));
            rawSendCmd(CMD_PTLON);
            return;
        }
#endif
        sendCommandData(CMD_PTLAR_ROWS, rows, sizeof(rows));
        sendCommandData(CMD_PTLAR_COLUMNS, columns, sizeof(columns));
        sendCommand(CMD_PTLON);
    }
};

} // namespace v1
} // namespace lgfx
