/// @file icompressor.hpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-26

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace dakt::decrypt::compression {

enum class CompressionMethod : uint16_t {
  Store   = 0,
  Deflate = 8,
  XMem    = 14,
  Lzx     = 28,
  Lz4Lite = 64,
  Zstd    = 93,
  Unknown = 0xFFFF
};

class ICompressor {
public:
  virtual ~ICompressor()                                          = default;

  [[nodiscard]] virtual std::string_view  name() const noexcept   = 0;

  [[nodiscard]] virtual CompressionMethod method() const noexcept = 0;

  // Decompresses the input span into the output vector. Returns false on failure.
  virtual bool decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const = 0;
};

} // namespace dakt::decrypt::compression