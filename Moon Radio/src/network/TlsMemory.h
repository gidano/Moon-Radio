#pragma once

// Az Arduino-ESP32 előre fordított mbedTLS könyvtára alapból kizárólag
// belső RAM-ból foglal. A nagy TLS puffereket PSRAM-ba irányítjuk.
bool enableTlsPsramAllocator();
