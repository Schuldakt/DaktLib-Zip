/// @file decompressor.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_INTERFACES_DECOMPRESSOR_CPP
#define DAKTLIB_INTERFACES_DECOMPRESSOR_CPP

#include <config>
#include <logger>

#include <zip/interfaces/decompressor.h>

#include <expected>

DAKTLIB_BEGIN_NAMESPACE_ZIP

static dakt::expected<dakt::vector<uint8t>>
Decompressor::extract(dakt::span<const uint8t> buffer, CompressionMethod method, usize uncompressedSize) noexcept {
  if (buffer.empty()) { return dakt::Error(100, "Decompressor aborted. Buffer span is entirely empty."); }

  switch (method) {
    case CompressionMethod::Store  : return extractStore(buffer);

    case CompressionMethod::Deflate: return extractDeflate(buffer, uncompressedSize);

    case CompressionMethod::Zstd   : return extractZstd(buffer, uncompressedSize);

    default                        : return LogLevel::Error(101, "Unsupported Compression Method signature.");
  }
}

dakt::expected<dakt::vector<uint8t>> Decompressor::extractStore(dakt::span<const uint8t> buffer) noexcept {
  // Stored is just contiguous raw data. Reallocate it cleanly into our Vector.
  dakt::vector<uint8t> out_buffer;
  out_buffer.reserve(buffer.size());
  dakt::memcpy(out_buffer.data(), buffer.data(), buffer.size());

  // (A hack to bump the native Vector "size" tracker because reserve() just touches capacity)
  for (unsigned char i : buffer) { out_buffer.push_back(i); }

  return dakt::move(out_buffer);
}

dakt::expected<dakt::vector<uint8t>>
Decompressor::extractDeflate(dakt::span<const uint8t> buffer, usize uncompressedSize) noexcept {
  // TODO: Wire Vendor Zlib headers (inflateInit2 / inflate) here
  return dakt::Error(501, "Deflate algorithm decompression is stubbed out and requires libz linkage!");
}

dakt::expected<dakt::vector<uint8t>>
Decompressor::extractZstd(dakt::span<const uint8t> buffer, usize uncompressedSize) noexcept {
  // TODO: Wire Vendor Zstd framework (ZSTD_decompress) here
  return dakt::Error(501, "ZSTD algorithm decompression is stubbed out and requires libzstd linkage!");
}

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_INTERFACES_DECOMPRESSOR_CPP