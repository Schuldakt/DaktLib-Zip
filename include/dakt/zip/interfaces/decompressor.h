/// @file decompressor.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_INTERFACES_DECOMPRESSOR_H
#define DAKTLIB_INTERFACES_DECOMPRESSOR_H

#include <config>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

enum class CompressionMethod : uint16t {
  Store   = 0,   // Uncompressed, RAW
  Deflate = 8,   // Legacy Zip Standard (zlib-compatible)
  Zstd    = 93,  // Modern Star Citizen / P4K standard
  Oodle   = 100, // Advanced Sony/Epic codec (Not fully supported in P4K yet, but common)
  Unknown = 999
};

using DecompressResult = dakt::expected<dakt::vector<uint8t>, dakt::string>;

/// @class Decompressor
/// @brief High-speed, stateless utility engine for unpacking compressed memory streams into linear
/// buffer vectors.
class Decompressor {
  public:
    /// @brief Extracts a chunk of bytes and returns a dynamically allocated, flat byte vector
    /// containing the inflated data.
    /// @param buffer The compressed payload block mapped natively via Span.
    /// @param method The declared codec formula used to compress this specific payload.
    /// @param uncompressedSize The expected final size (often stored in the Zip or Cry headers) to
    /// pre-allocate correctly.
    /// @return A monad containing the fully unpacked raw bytes OR an Error trace describing the
    /// failure reason.
    static auto extract(
      dakt::span<const uint8t> buffer, CompressionMethod method, usize uncompressedSize
    ) noexcept -> DecompressResult;

  private:
    static auto extractStore(dakt::span<const uint8t> buffer) noexcept -> DecompressResult;
    static auto extractDeflate(dakt::span<const uint8t> buffer, usize uncompressedSize) noexcept
      -> DecompressResult;
    static auto extractZstd(dakt::span<const uint8t> buffer, usize uncompressedSize) noexcept
      -> DecompressResult;
};

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_INTERFACES_DECOMPRESSOR_H