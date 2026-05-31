/// @file lzx.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-27

#pragma once
#include <lzx.hpp>

namespace dakt::decrypt {

std::string_view  Lzx::name() const noexcept { return "LZX"; }

CompressionMethod Lzx::method() const noexcept { return CompressionMethod::Lzx; }

bool Lzx::decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const {
  if (input.empty()) {
    return false;
  }

  // STUB: Microsoft LZX decompression requires bitstream parsing and multiple
  // aligned/pre-tree/main Huffman tables.
  // We fail gracefully. This ensures the Inspector safely captures the hexdump preview instead of
  // crashing.

  return false;
}

} // namespace dakt::decrypt