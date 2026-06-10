/// @file lz4.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief LZ4 raw block decompressor and compressor.
/// @version 0.2
/// @date 2026-06-10
#ifndef DAKTLIB_METHODS_LZ4_CXX
#define DAKTLIB_METHODS_LZ4_CXX

#include <algorithm>
#include <config>

#include <zip/methods/lz4.h>
#include <zip/registrar/compression_registry.h>

DAKTLIB_BEGIN_NAMESPACE_ZIP

auto Lz4::name() const noexcept -> dakt::string_view {
  return "Lz4"; // Must match toString(CompressionMethod::Lz4) in detector.h
}

auto Lz4::method() const noexcept -> CompressionMethod {
  return CompressionMethod::Lz4;
}

// ----------------------------------------------------------------------------
// inflateChunk
//
// Decodes a single LZ4 raw block (spec section 1.6).
// A raw block has no frame header — it's purely the sequence stream.
// The caller is responsible for stripping any LZ4 frame header (magic,
// FLG, BD, content size) before passing data here.
//
// Returns the number of bytes written to outputBuffer, or 0 on any error.
// ----------------------------------------------------------------------------
auto Lz4::inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize {
  if (compressedData.empty()) { return 0; }

  const uint8t*       src     = compressedData.data();
  const uint8t* const src_end = src + compressedData.size();

  outputBuffer.clear();
  outputBuffer.reserve(compressedData.size() * 4); // Heuristic: LZ4 typically achieves ~2-4x

  while (src < src_end) {

    // ------------------------------------------------------------------
    // Step 1: Read the token byte
    // High nibble = literal run length (0..14, or 15 = extended)
    // Low nibble  = match length      (0..14, or 15 = extended) — biased by +4 later
    // ------------------------------------------------------------------
    const uint8t token        = *src++;
    usize        lit_length   = (token >> 4) & 0x0F;
    usize        match_length = (token) & 0x0F;

    // ------------------------------------------------------------------
    // Step 2: Decode extended literal length
    // If the high nibble is 15, keep reading bytes and accumulating until
    // a byte less than 255 is read. Fix: do/while reads before checking.
    // ------------------------------------------------------------------
    if (lit_length == 15) {
      while (true) {
        if (src >= src_end) { return 0; }
        const uint8t step  = *src++;
        lit_length        += step;
        if (step < 255) { break; }
      }
    }

    // ------------------------------------------------------------------
    // Step 3: Copy literals
    // ------------------------------------------------------------------
    if (src + lit_length > src_end) { return 0; }

    const usize out_base = outputBuffer.size();
    outputBuffer.resize(out_base + lit_length);
    dakt::memcpy(outputBuffer.data() + out_base, src, lit_length);
    src += lit_length;

    // The last sequence in a block ends here — there is no match after the
    // final literal run. The spec guarantees the last 5 bytes are literals.
    if (src >= src_end) { break; }

    // ------------------------------------------------------------------
    // Step 4: Read match offset (little-endian 16-bit)
    // Offset is measured backwards from the current output position.
    // ------------------------------------------------------------------
    if (src + 2 > src_end) { return 0; }
    const uint16t offset  = static_cast<uint16t>(src[0]) | (static_cast<uint16t>(src[1]) << 8);
    src                  += 2;

    if (offset == 0 || offset > outputBuffer.size()) { return 0; } // Invalid back-reference

    // ------------------------------------------------------------------
    // Step 5: Decode extended match length (same extension scheme as literals)
    // Then add the implicit minimum of 4 (LZ4 spec §1.6.3).
    // ------------------------------------------------------------------
    if (match_length == 15) {
      while (true) {
        if (src >= src_end) { return 0; }
        const uint8t step  = *src++;
        match_length      += step;
        if (step < 255) { break; }
      }
    }
    match_length += 4; // Minimum match length per spec

    // ------------------------------------------------------------------
    // Step 6: Copy match — may overlap (offset < match_length is valid)
    //
    // We must NOT use memcpy here because overlapping copies are intentional
    // and produce run-length patterns (e.g. offset=1 repeats the last byte).
    //
    // We reserve first so that push_back cannot trigger a reallocation
    // mid-loop, which would invalidate the reference at match_pos + i.
    // ------------------------------------------------------------------
    outputBuffer.reserve(outputBuffer.size() + match_length);
    const usize match_pos = outputBuffer.size() - offset;

    for (usize i = 0; i < match_length; ++i) { outputBuffer.push_back(outputBuffer[match_pos + i]); }
  }

  return outputBuffer.size();
}

// ----------------------------------------------------------------------------
// deflateChunk
//
// Encodes raw bytes into a single LZ4 raw block using a literals-only strategy
// (no match finding). This produces valid LZ4 output at a 1:1 size ratio,
// which is correct for the Store-equivalent path and sufficient for round-trip
// testing. A proper hash-chain match finder can be layered in later.
//
// Raw block format for literals-only:
//   [token] [literal bytes...]  — repeated until all input is consumed
//
// The spec requires the last sequence to be at least 5 literal bytes with no
// trailing match. We enforce this by emitting all bytes as pure literal runs.
// ----------------------------------------------------------------------------
auto Lz4::deflateChunk(dakt::span<const uint8t> rawData, dakt::vector<uint8t>& outputBuffer) -> usize {
  if (rawData.empty()) { return 0; }

  outputBuffer.clear();

  const uint8t*   src         = rawData.data();
  const uint8t*   src_end     = src + rawData.size();
  constexpr usize MAX_LIT_RUN = 0xFF + 15; // 270 bytes per run before another token is needed

  while (src < src_end) {
    auto run                  = static_cast<usize>(src_end - src);
    run                       = dakt::min(run, MAX_LIT_RUN);

    // ------------------------------------------------------------------
    // Emit token + extended literal length
    // High nibble: min(run, 15). If run >= 15 we extend below.
    // Low nibble: 0 (no match follows).
    // ------------------------------------------------------------------
    const uint8t token_nibble = (run >= 15) ? 0xF0U : static_cast<uint8t>(run << 4);
    outputBuffer.push_back(token_nibble);

    if (run >= 15) {
      usize remaining = run - 15;
      while (remaining >= 255) {
        outputBuffer.push_back(255);
        remaining -= 255;
      }
      outputBuffer.push_back(static_cast<uint8t>(remaining));
    }

    // ------------------------------------------------------------------
    // Emit literal bytes
    // ------------------------------------------------------------------
    const usize base = outputBuffer.size();
    outputBuffer.resize(base + run);
    dakt::memcpy(outputBuffer.data() + base, src, run);
    src += run;
  }

  return outputBuffer.size();
}

// ----------------------------------------------------------------------------
// Self-registration
// ----------------------------------------------------------------------------
namespace {
[[maybe_unused]] const bool s_lz4_registered = [] -> bool {
  CompressionRegistry::instance().registerModule(dakt::make_unique<Lz4>());
  return true;
}();
} // namespace

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_LZ4_CXX