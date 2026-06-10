/// @file lz4lite.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_LZ4LITE_CXX
#define DAKTLIB_METHODS_LZ4LITE_CXX

#include <config>

#include <zip/methods/lz4.h>

DAKTLIB_BEGIN_NAMESPACE_ZIP

auto Lz4::name() const noexcept -> dakt::string_view {
  return "Lz4";
}

auto Lz4::method() const noexcept -> CompressionMethod {
  return CompressionMethod::Lz4;
}

auto Lz4::inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize {
  if (compressedData.empty()) { return 0U; }

  const uint8t*       src     = compressedData.data();
  const uint8t* const src_end = src + compressedData.size();

  // If the host application knows the exact uncompressed size, it should pre-resize outputBuffer.
  // If not, we will need to dynamically push_back (which is slower but safe).
  // For this raw block decompressor, we assume dynamic growth for safety.
  outputBuffer.clear();
  outputBuffer.reserve(compressedData.size() * 3); // Heuristic starting capacity

  while (src < src_end) {
    // 1. Read the Token
    uint8t token          = *src++;
    usize  literal_length = (token >> 4) & 0x0F;
    usize  match_length   = token & 0x0F;

    // 2. Decode Literal Length
    if (literal_length == 15) {
      uint8t step;
      while (step == 255) {
        if (src >= src_end) { return 0; }
        step            = *src++;
        literal_length += step;
      }
    }

    // 3. Copy Literals
    if (src + literal_length > src_end) {
      return 0; // Bounds check
    }

    usize current_out_size = outputBuffer.size();
    outputBuffer.resize(current_out_size + literal_length);
    dakt::memcpy(outputBuffer.data() + current_out_size, src, literal_length);
    src += literal_length;

    // LZ4 blocks end with exactly 5 literals and no match
    if (src >= src_end) { break; }

    // 4. Read Match Offset (Little Endian 16-bit)
    if (src + 2 > src_end) {
      return 0; // bounds check
    }
    uint16t offset  = static_cast<uint16t>(src[0]) | (static_cast<uint16t>(src[1]) << 8);
    src            += 2;

    if (offset == 0 || offset > outputBuffer.size()) {
      return 0; // Invalid offset
    }

    // 5. Decode Match Length
    if (match_length == 15) {
      uint8t step;
      while (step == 255) {
        if (src >= src_end) { return 0; }
        step          = *src++;
        match_length += step;
      }
    }
    match_length     += 4; // LZ4 implicit minimum match length is 4

                           // 6. Copy Match from previously decoded data
    current_out_size  = outputBuffer.size();
    outputBuffer.resize(current_out_size + match_length);

    // We must copy byte-by-byte (or use overlapping safe copy)
    // because match length can be greater than the match offset (RLE behavior)
    uint8t*       dest      = outputBuffer.data() + current_out_size;
    const uint8t* match_ptr = dest - offset;

    for (usize i = 0; i < match_length; ++i) { dest[i] = match_ptr[i]; }
  }

  return outputBuffer.size();
}

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_LZ4LITE_CXX