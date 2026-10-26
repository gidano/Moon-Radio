#pragma once
#include <cstddef>
#include <cstdint>

namespace id3 {
struct TextRange {
    size_t begin;
    size_t size;
};

// ID3 text lengths are in bytes. Scan inside the frame before calling any
// null-terminated text converter, including for UTF-16 and missing terminators.
constexpr bool textRange(const uint8_t* data, size_t size, uint8_t encoding,
                         size_t& cursor, TextRange& range) {
    const size_t width = encoding == 1 || encoding == 2 ? 2 : 1;
    const size_t start = cursor;
    while (cursor < size && size - cursor >= width) {
        if (data[cursor] == 0 && (width == 1 || data[cursor + 1] == 0)) {
            range = {start, cursor - start};
            cursor += width;
            return true;
        }
        cursor += width;
    }
    return false;
}

template <class Emit>
constexpr size_t synchronizedLyrics(const uint8_t* data, size_t size, Emit emit) {
    if (!data || size < 6 || data[0] > 3 || data[4] != 2) return 0;
    const uint8_t encoding = data[0];
    size_t cursor = 6;
    TextRange descriptor{};
    if (!textRange(data, size, encoding, cursor, descriptor)) return 0;
    bool bigEndian = encoding == 2;
    if (encoding == 1 && descriptor.size >= 2) {
        if (data[descriptor.begin] == 0xFE && data[descriptor.begin + 1] == 0xFF) bigEndian = true;
        if (data[descriptor.begin] == 0xFF && data[descriptor.begin + 1] == 0xFE) bigEndian = false;
    }
    size_t count = 0;
    while (cursor < size) {
        TextRange line{};
        if (!textRange(data, size, encoding, cursor, line) || size - cursor < 4) break;
        const uint32_t timestamp = (uint32_t(data[cursor]) << 24) |
                                   (uint32_t(data[cursor + 1]) << 16) |
                                   (uint32_t(data[cursor + 2]) << 8) | data[cursor + 3];
        cursor += 4;
        emit(line, encoding, bigEndian, timestamp);
        ++count;
    }
    return count;
}

constexpr size_t lyricPairCount(size_t lines, size_t timestamps) {
    return lines < timestamps ? lines : timestamps;
}
}
