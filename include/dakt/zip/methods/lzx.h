/// @file lzx.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_LZX_H
#define DAKTLIB_METHODS_LZX_H

#include <config>

#include <compressor>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

class Lzx : public ICompressor {
  public:
    [[nodiscard]] auto name() const noexcept -> dakt::string_view override;

    [[nodiscard]] auto method() const noexcept -> CompressionMethod override;

    auto inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize override;
};

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_LZX_H