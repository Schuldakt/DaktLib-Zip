/// @file xmem.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_XMEM_CXX
#define DAKTLIB_METHODS_XMEM_CXX

#include <config>

#include <zip/methods/lzx.h>
#include <zip/methods/xmem.h>

#include <cstring>

DAKTLIB_BEGIN_NAMESPACE_ZIP

auto XMem::method() const noexcept -> CompressionMethod {
  return CompressionMethod::XMem;
}

auto XMem::inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize {
  if (compressedData.size() < 4) { return 0; }

  const uint8t* src          = compressedData.data();
  const uint8t* src_end      = src + compressedData.size();
  usize         initial_size = outputBuffer.size();

  // 1. read XMem Frame Header
  // XMem blocks usually begin with a chunk size header, often 0x8000 (32KB) or 0x10000 (64KB)
  // The exact header format depends on it it's Xbox 360 (Big Endian) or Xbox One (Little Endian).
  // Assuming standard Little Endian PC-ported XMem:
  if (src + 4 > src_end) { return 0; }
  uint32t uncompressed_chunk_size  = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
  src                             += 4;

  // Optional: Read compressed chunk size if present in your specific engine's XMem wrapper
  // uint32t compressedChunkSize = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
  // src += 4;

  // Ensure output buffer has space for this chunk
  outputBuffer.reserve(initial_size + uncompressed_chunk_size);

  // 2. Hand off the payload to the LZX Core
  // We instantiate the LzxCompressor locally to process the interior payload.
  // Because LZX is stateless between standalone chunks, this is perfectly safe and thread-safe.
  Lzx lzx_engine;

  while (src < src_end) {
    // Read the size of the NEXT compressed chunk.
    // Some engines use 16-bit sizes, some use 32-bit. We assume standard 32-bit here.
    if (src + 4 > src_end) {
      break; // Reached end or padding
    }

    uint32t compressed_chunk_size  = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    src                           += 4;

    // XMem sometimes flags uncompressed chunks by setting the MSB (Most Significant Bit)
    // e.g., if (compressed_chunk_size & 0x80000000) -> it's uncompressed!
    bool is_uncompressed           = (compressed_chunk_size & 0x80000000) != 0;
    compressed_chunk_size         &= 0x7FFFFFFF; // Clear the flag bit to get the real size

    if (compressed_chunk_size == 0 || src + compressed_chunk_size > src_end) {
      break;                                     // Safety bounds check
    }

                                                 // 3. Slice the payload and route it
    dakt::span<const uint8t> payload_slice(src, compressed_chunk_size);

    if (is_uncompressed) {
      // Bypass LZX entirely and write directly to output
      usize current_out_size = outputBuffer.size();
      outputBuffer.resize(current_out_size + compressed_chunk_size);
      dakt::memcpy(outputBuffer.data() + current_out_size, payload_slice.data(), compressed_chunk_size);
    } else {
      // Route the exact chunk slice to your raw LZX engine
      usize decoded_bytes = lzx_engine.inflateChunk(payload_slice, outputBuffer);

      if (decoded_bytes == 0) {
        // TODO: Implement DaktLib-Log Warning message.
        // Warning: LZX Core failed to decode this specific chunk.
        break;
      }
    }

    // Advance the cursor past the chunk we just processed
    src += compressed_chunk_size;

    // Safety break if we'v fulfilled the expected size (some XMem streams pad the end with zeros)
    if (outputBuffer.size() - initial_size >= uncompressed_chunk_size) { break; }
  }

  // Return exactly how many bytes we added to the buffer across all chunks
  return outputBuffer.size() - initial_size;
}

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_XMEM_CXX