/// @file lz4lite.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-27

#pragma once
#include <lz4lite.hpp>

namespace dakt::decrypt {

std::string_view  Lz4Lite::name() const noexcept { return "Lz4Lite"; }

CompressionMethod Lz4Lite::method() const noexcept { return CompressionMethod::Lz4Lite; }

bool Lz4Lite::decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const {
  if (input.empty()) {
    return false;
  }

  const uint8_t*       src     = input.data();
  const uint8_t* const src_end = src + input.size();

  // Typically, game engines prepend the uncompressed size.
  // We'll assume standard LZ4 block format here, but output is dynamically sized if not
  // pre-allocated.

  while (src < src_end) {
    uint8_t token         = *src++;

    // 1. Literals
    size_t literal_length = token >> 4;
    if (literal_length == 15) {
      uint8_t s;
      do {
        if (src >= src_end) {
          return false;
        }
        s               = *src++;
        literal_length += s;
      } while (s == 255);
    }

    if (src + literal_length > src_end) {
      return false; // Bounds check
    }

    output.insert(output.end(), src, src + literal_length);
    src += literal_length;

    if (src >= src_end) {
      break; // End of block
    }

    // 2. Matches
    if (src + 2 > src_end) {
      return false; // bounds check
    }
    uint16_t offset  = src[0] | (src[1] << 8);
    src             += 2;

    if (offset == 0 || offset > output.size()) {
      return false; // Invalid offset
    }

    size_t match_length = (token & 0x0F);
    if (match_length == 15) {
      uint8_t s;
      do {
        if (src >= src_end) {
          return false;
        }
        s             = *src++;
        match_length += s;
      } while (s == 255);
    }
    match_length     += 4; // Min match length

    // Copy matched sequence
    size_t start_idx  = output.size() - offset;
    for (size_t i = 0; i < match_length; i++) {
      output.push_back(output[start_idx + i]);
    }
  }

  return true;
}

} // namespace dakt::decrypt