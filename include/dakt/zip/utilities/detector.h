/// @file detector.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-09
#ifndef DAKTLIB_UTILITIES_DETECTOR_H
#define DAKTLIB_UTILITIES_DETECTOR_H

#include <config>

#include <span>
#include <string_view>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

enum class CompressionMethod : uint16t {
  Store   = 0,
  Deflate = 8,
  XMem    = 14,
  Lzx     = 28,
  Lz4     = 64,
  Zstd    = 93,
  Unknown = 0xFFFF
};

[[nodiscard]] constexpr auto toString(CompressionMethod method) noexcept -> dakt::string_view {
  switch (method) {
    case CompressionMethod::Store  : return "Store";
    case CompressionMethod::Deflate: return "Deflate (Zlib)";
    case CompressionMethod::XMem   : return "XMem";
    case CompressionMethod::Lzx    : return "Lzx";
    case CompressionMethod::Lz4    : return "Lz4";
    case CompressionMethod::Zstd   : return "Zstd";
    default                        : return "Unknown";
  }
}

// Reads the first few bytes of a buffer to determine the compression algorithm.
[[nodiscard]] inline auto detectMethod(dakt::span<const uint8t> magicBytes) noexcept -> CompressionMethod {
  if (magicBytes.size() >= 4) {
    // Zstandard: FD 2F B5 28
    if (magicBytes[0] == 0xFD && magicBytes[1] == 0x2F && magicBytes[2] == 0xB5 && magicBytes[3] == 0x28) {
      return CompressionMethod::Zstd;
    }
    // LZ4 Frame: 04 22 4D 18
    if (magicBytes[0] == 0x04 && magicBytes[1] == 0x22 && magicBytes[2] == 0x4D && magicBytes[3] == 0x18) {
      return CompressionMethod::Lz4;
    }
  }

  if (magicBytes.size() >= 2) {
    // Zlib (Deflate): 78 01 (low), 78 5E (Default), 78 9C (Optimal), 78 DA (Max)
    if (magicBytes[0]
        == 0x78
        && (magicBytes[1] == 0x01 || magicBytes[1] == 0x5E || magicBytes[1] == 0x9C || magicBytes[1] == 0xDA)) {
      return CompressionMethod::Deflate;
    }
  }

  return CompressionMethod::Unknown;
}

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_UTILITIES_DETECTOR_H