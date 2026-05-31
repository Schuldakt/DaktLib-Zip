/// @file decompressor.hpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-26

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace dakt::decrypt {

enum class CompressionMethod : uint16_t {
  Store   = 0,   // Uncompressed, RAW
  Deflate = 8,   // Legacy Zip Standard (zlib-compatible)
  Zstd    = 93,  // Modern Star Citizen / P4K standard
  Oodle   = 100, // Advanced Sony/Epic codec (Not fully supported in P4K yet, but common)
  Unknown = 999
};

using DecompressResult = std::expected<std::vector<uint8_t>, std::string>;

/// @class Decompressor
/// @brief High-speed, stateless utility engine for unpacking compressed memory streams into linear
/// buffer vectors.
class Decompressor {
public:
  /// @brief Extracts a chunk of bytes and returns a dynamically allocated, flat byte vector
  /// containing the inflated data.
  /// @param buffer The compressed payload block mapped natively via Span.
  /// @param method The declared codec formula used to compress this specific payload.
  /// @param uncompressed_size The expected final size (often stored in the Zip or Cry headers) to
  /// pre-allocate correctly.
  /// @return A monad containing the fully unpacked raw bytes OR an Error trace describing the
  /// failure reason.
  static DecompressResult extract(std::span<const uint8_t> buffer, CompressionMethod method,
                                  size_t uncompressed_size) noexcept;

private:
  static DecompressResult extract_store(std::span<const uint8_t> buffer) noexcept;
  static DecompressResult extract_deflate(std::span<const uint8_t> buffer,
                                          size_t                   uncompressed_size) noexcept;
  static DecompressResult extract_zstd(std::span<const uint8_t> buffer,
                                       size_t                   uncompressed_size) noexcept;
};

} // namespace dakt::decrypt