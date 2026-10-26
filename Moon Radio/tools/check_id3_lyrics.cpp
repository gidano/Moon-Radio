// Compile-time regression tests; compile with C++20, no board required.
#include "../lib/MaleksmAudioI2S/src/id3_lyrics.hpp"

constexpr bool validUtf8() {
    const uint8_t frame[] = {3,'e','n','g',2,1,0,'a',0,0,0,0,10,'b',0,0,0,0,20};
    unsigned count = 0;
    const auto parsed = id3::synchronizedLyrics(frame, sizeof(frame),
        [&](id3::TextRange text, uint8_t encoding, bool, uint32_t timestamp) {
            if (text.size == 1 && encoding == 3 && timestamp == (count + 1) * 10) ++count;
        });
    return parsed == 2 && count == 2;
}
constexpr bool incompleteTimestamp() {
    const uint8_t frame[] = {3,'e','n','g',2,1,0,'a',0,0,0,0,10,'b',0,0,0};
    unsigned count = 0;
    return id3::synchronizedLyrics(frame, sizeof(frame), [&](auto, auto, auto, auto) { ++count; }) == 1 && count == 1;
}
constexpr bool invalidText() {
    const uint8_t missingTerminator[] = {3,'e','n','g',2,1,0,'a','b'};
    const uint8_t oddUtf16[] = {1,'e','n','g',2,1,0,0,0xFF};
    const uint8_t frameCountTiming[] = {0,'e','n','g',1,1,0,'a',0,0,0,0,1};
    auto emit = [](auto, auto, auto, auto) {};
    return id3::synchronizedLyrics(missingTerminator, sizeof(missingTerminator), emit) == 0 &&
           id3::synchronizedLyrics(oddUtf16, sizeof(oddUtf16), emit) == 0 &&
           id3::synchronizedLyrics(frameCountTiming, sizeof(frameCountTiming), emit) == 0 &&
           id3::synchronizedLyrics(nullptr, 0, emit) == 0;
}
constexpr bool validUtf16() {
    const uint8_t frame[] = {1,'e','n','g',2,1,0xFE,0xFF,0,0,0,'A',0,0,0,0,0,25};
    bool correct = false;
    const auto count = id3::synchronizedLyrics(frame, sizeof(frame),
        [&](id3::TextRange text, uint8_t encoding, bool bigEndian, uint32_t timestamp) {
            correct = text.size == 2 && encoding == 1 && bigEndian && timestamp == 25;
        });
    return count == 1 && correct;
}
static_assert(validUtf8());
static_assert(incompleteTimestamp());
static_assert(invalidText());
static_assert(validUtf16());
constexpr bool everyTruncatedPrefix() {
    const uint8_t frame[] = {1,'e','n','g',2,1,0xFF,0xFE,0,0,'A',0,0,0,0,0,0,25};
    for (size_t size = 0; size <= sizeof(frame); ++size) {
        if (id3::synchronizedLyrics(frame, size, [](auto, auto, auto, auto) {}) > 1) return false;
    }
    return true;
}
static_assert(everyTruncatedPrefix());
// The reported panic: a lyric line exists, but its timestamp vector is empty.
static_assert(id3::lyricPairCount(1, 0) == 0);
static_assert(id3::lyricPairCount(2, 1) == 1);
static_assert(id3::lyricPairCount(1, 2) == 1);
