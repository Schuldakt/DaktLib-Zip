/// @file Decompressor.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-27

#pragma once
#include <decompressor.hpp>

namespace dakt::decrypt {

std::expected<std::vector<uint8_t>> Decompressor::extract(std::span<const uint8_t> buffer,
                                                          CompressionMethod        method,
                                                          size_t uncompressed_size) noexcept {
  if (buffer.empty()) {
    return dakt::Error(100, "Decompressor aborted. Buffer span is entirely empty.");
  }

  switch (method) {
  case CompressionMethod::Store  : return extract_store(buffer);

  case CompressionMethod::Deflate: return extract_deflate(buffer, uncompressed_size);

  case CompressionMethod::Zstd   : return extract_zstd(buffer, uncompressed_size);

  default                        : return dakt::Error(101, "Unsupported Compression Method signature.");
  }
}

std::expected<std::vector<uint8_t>>
Decompressor::extract_store(std::span<const uint8_t> buffer) noexcept {
  // Stored is just contiguous raw data. Reallocate it cleanly into our Vector.
  std::vector<uint8_t> out_buffer;
  out_buffer.reserve(buffer.size());
  std::memcpy(out_buffer.data(), buffer.data(), buffer.size());

  // (A hack to bump the native Vector "size" tracker because reserve() just touches capacity)
  for (size_t i = 0; i < buffer.size(); ++i) {
    out_buffer.push_back(buffer[i]);
  }

  return std::move(out_buffer);
}

std::expected<std::vector<uint8_t>>
Decompressor::extract_deflate(std::span<const uint8_t> buffer, size_t uncompressed_size) noexcept {
  // TODO: Wire Vendor Zlib headers (inflateInit2 / inflate) here
  return dakt::Error(501,
                     "Deflate algorithm decompression is stubbed out and requires libz linkage!");
}

std::expected<std::vector<uint8_t>> Decompressor::extract_zstd(std::span<const uint8_t> buffer,
                                                               size_t uncompressed_size) noexcept {
  // TODO: Wire Vendor Zstd framework (ZSTD_decompress) here
  return dakt::Error(501,
                     "ZSTD algorithm decompression is stubbed out and requires libzstd linkage!");
}

} // namespace dakt::decrypt