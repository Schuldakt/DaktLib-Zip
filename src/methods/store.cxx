/// @file store.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_STORE_CXX
#define DAKTLIB_METHODS_STORE_CXX

#include <config>

#include <zip/methods/store.h>
#include <zip/registrar/compression_registry.h>

DAKTLIB_BEGIN_NAMESPACE_ZIP

auto Store::name() const noexcept -> dakt::string_view {
  return "Store"; // Must match toString(CompressionMethod::Zstd) in detector.h
}

auto Store::method() const noexcept -> CompressionMethod {
  return CompressionMethod::Store;
}

auto Store::inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize {
  if (compressedData.empty()) { return 0; }

  // For "Store", the uncompressed size is exactly the compressed size.
  usize data_size = compressedData.size();

  // We reserve and resize the buffer to perfectly fit the raw data
  outputBuffer.resize(data_size);

  // Fast memory copy
  dakt::memcpy(outputBuffer.data(), compressedData.data(), data_size);

  return data_size;
}

auto Store::deflateChunk(dakt::span<const uint8t> rawData, dakt::vector<uint8t>& outputBuffer) -> usize {
  // Deflating "Store" is identically just copying the bytes
  if (rawData.empty()) { return 0; }

  usize data_size = rawData.size();
  outputBuffer.resize(data_size);
  dakt::memcpy(outputBuffer.data(), rawData.data(), data_size);

  return data_size;
}

[[maybe_unused]] const bool s_store_registered = [] -> bool {
  CompressionRegistry::instance().registerModule(dakt::make_unique<Store>());
  return true;
}();

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_STORE_CXX