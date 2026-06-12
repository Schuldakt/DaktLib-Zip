/// @file zlib.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_ZLIB_H
#define DAKTLIB_METHODS_ZLIB_H

#include <config>

#include <zip/utilities/compressor.h>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

class Zlib : public ICompressor {
  public:
    [[nodiscard]] auto name() const noexcept -> dakt::string_view override;

    [[nodiscard]] auto method() const noexcept -> CompressionMethod override;

    auto inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize override;

    auto deflateChunk(dakt::span<const uint8t> rawData, dakt::vector<uint8t>& outputBuffer) -> usize override;

    // Raw path — skips magic validation entirely. Use when the caller has
    // already identified the stream as Zstd via an external mechanism
    // (e.g. P4K central directory method field) and the magic may be
    // absent, stripped, or non-standard.
    auto inflateChunkRaw(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize;
};

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_ZLIB_H