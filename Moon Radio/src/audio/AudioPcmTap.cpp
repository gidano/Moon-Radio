#include "AudioSpectrum.h"

// Szándékosan nem include-oljuk itt az Audio.h fájlt. Abban a callback
// deklarációja weak attribútumú; ez a külön fordítási egység biztosítja,
// hogy a projekt megvalósítása erős szimbólumként felülírja a könyvtár
// üres alapértelmezett függvényét.
void audio_process_raw_samples(int32_t* outBuffer, int16_t validSamples) {
  AudioSpectrum::captureActive(outBuffer, validSamples);
}
