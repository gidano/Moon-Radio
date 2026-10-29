# Moon Radio screen capture component

This folder contains the reusable part of the web screenshot feature.

`ScreenCapture` keeps a logical 480x320 RGB565 frame in PSRAM. The display
manager updates it from each LVGL flush, including the native AXS visualizer
rectangle. `copyRow()` emits big-endian RGB565 rows so a web endpoint can
stream the frame without allocating a second full-size buffer.

To reuse it in another ESP32 project:

1. Copy this folder.
2. Create the component once after display/PSRAM initialization with
   `begin()`.
3. Call `captureRect()` from the display flush callback. Use
   `captureNativeRect()` for any drawing path that supplies byte-swapped
   RGB565 pixels outside LVGL.
4. Expose `copyRow()` from a web route with `X-W`, `X-H` and
   `application/octet-stream` headers.

The web client converts the returned RGB565 stream to PNG in the browser, so
the ESP32 does not need a PNG encoder.
