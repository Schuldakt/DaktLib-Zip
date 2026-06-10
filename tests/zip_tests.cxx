/// @file zip_tests.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief Unit and integration tests for DaktLib-Zip compression methods.
/// @version 0.2
/// @date 2026-06-10
#ifndef DAKTLIB_TESTS_ZIP_TESTS_CXX
#define DAKTLIB_TESTS_ZIP_TESTS_CXX

#include <config>

#include <compressor>
#include <utility/memory_stream.h>
#include <zip/capi/zip_exports.h>
#include <zip/methods/lz4.h>
#include <zip/methods/lzx.h>
#include <zip/methods/store.h>
#include <zip/methods/xmem.h>
#include <zip/methods/zlib.h>
#include <zip/methods/zstd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string_view>

using namespace dakt;
using namespace dakt::zip;

// ============================================================================
// Helpers
// ============================================================================

// Runs the full C-API pipeline for a given method and returns the output bytes.
// Returns an empty vector on any failure along the pipeline.
auto runPipeline(dakt::vector<uint8t> inputBytes, CompressionMethod method) -> dakt::vector<uint8t> {
  auto       mock_stream = make_unique<MockMemoryStream>(dakt::move(inputBytes));
  DataStream c_input     = capi::makeCStream(dakt::move(mock_stream));

  DataStream c_output{};
  int        result = zipCreateDecompressionStream(&c_input, static_cast<int>(method), &c_output);
  if (result != 0) { return {}; }

  dakt::vector<uint8t> output_buffer(4096);
  usize                bytes_read = c_output.read(c_output.context, output_buffer.data(), output_buffer.size());
  output_buffer.resize(bytes_read);

  c_output.close(c_output.context);
  return output_buffer;
}

// ============================================================================
// Store
// ============================================================================

void testStoreDirectInflate() {
  Store                engine;
  string_view          input_text = "Asset Pipeline Test";

  dakt::vector<uint8t> input(input_text.begin(), input_text.end());
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(input, output);

  assert(bytes == input_text.size() && "Store: inflateChunk returned wrong byte count");
  assert(output.size() == input_text.size() && "Store: output buffer has wrong size");
  assert(memcmp(output.data(), input_text.data(), bytes) == 0 && "Store: data corruption");

  cout << "[PASS] Store: Direct inflateChunk\n";
}

void testStoreDirectDeflate() {
  Store                engine;
  string_view          input_text = "Asset Pipeline Test";

  dakt::vector<uint8t> input(input_text.begin(), input_text.end());
  dakt::vector<uint8t> output;

  usize                bytes = engine.deflateChunk(input, output);

  // Store deflate is identity — output must be bit-for-bit identical to input
  assert(bytes == input_text.size() && "Store: deflateChunk returned wrong byte count");
  assert(memcmp(output.data(), input_text.data(), bytes) == 0 && "Store: deflate data corruption");

  cout << "[PASS] Store: Direct deflateChunk (identity)\n";
}

void testStoreRoundTrip() {
  Store                engine;
  string_view          input_text = "Round-trip integrity check";

  dakt::vector<uint8t> original(input_text.begin(), input_text.end());
  dakt::vector<uint8t> deflated;
  dakt::vector<uint8t> inflated;

  engine.deflateChunk(original, deflated);
  engine.inflateChunk(deflated, inflated);

  assert(inflated.size() == original.size() && "Store: round-trip size mismatch");
  assert(memcmp(inflated.data(), original.data(), original.size()) == 0 && "Store: round-trip data corruption");

  cout << "[PASS] Store: Round-trip deflate -> inflate\n";
}

void testStoreEmptyInput() {
  Store                engine;
  dakt::vector<uint8t> empty_input;
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(empty_input, output);

  assert(bytes == 0 && "Store: empty input should return 0 bytes");
  assert(output.empty() && "Store: output should be empty for empty input");

  cout << "[PASS] Store: Empty input guard\n";
}

void testStorePipeline() {
  string_view          expected = "Asset Pipeline Test";
  dakt::vector<uint8t> raw(expected.begin(), expected.end());

  auto                 output = runPipeline(dakt::move(raw), CompressionMethod::Store);

  assert(output.size() == expected.size() && "Store pipeline: size mismatch");
  assert(memcmp(output.data(), expected.data(), expected.size()) == 0 && "Store pipeline: data corruption");

  cout << "[PASS] Store: Full C-API pipeline\n";
}

// ============================================================================
// Lz4
// ============================================================================
//
// Note: Your current test payload (0x7F, 'D', 'a', 'k', 't', 'L', 'i', 'b', ' ', 0x08, 0x00, 0x0A)
// is not a valid LZ4 raw block. The token 0x7F decodes as 7 literals + 15-byte match-length
// extension, which then reads 'D' as the extension step byte. That causes the literal copy to
// overrun src_end and return 0. The correct minimal block for "DaktLib " with no match is below.

void testLz4DirectInflateLiteralsOnly() {
  // Encodes "DaktLib " (8 bytes) as a literals-only LZ4 block (no match sequence).
  // Token: 0x80 = (8 << 4) | 0  -> 8 literals, 0 match length (valid end-of-block with no match)
  dakt::vector<uint8t> lz4_block = {
   0x80, // Token: 8 literals, no match
   'D',
   'a',
   'k',
   't',
   'L',
   'i',
   'b',
   ' ' // Literal bytes
  };

  Lz4                  engine;
  dakt::vector<uint8t> output;
  usize                bytes = engine.inflateChunk(lz4_block, output);

  assert(bytes > 0 && "Lz4: inflateChunk returned 0 bytes");

  string_view expected = "DaktLib ";
  assert(output.size() == expected.size() && "Lz4: output size mismatch");
  assert(memcmp(output.data(), expected.data(), expected.size()) == 0 && "Lz4: data corruption");

  cout << "[PASS] Lz4: Direct inflateChunk (literals-only block)\n";
}

void testLz4DirectInflateWithMatch() {
  // Encodes "DaktLib DaktLib " using one literal run + one match sequence.
  //
  // Sequence layout:
  //   Token:       0x80 = (8 literals << 4) | (0 match nibble)
  //   Literals:    'D' 'a' 'k' 't' 'L' 'i' 'b' ' '   (8 bytes)
  //   Offset:      0x08 0x00  = offset 8 (little-endian, points back 8 bytes)
  //   match_length = 0 + 4 = 4  (minimum implicit match, gives us "Dakt")
  //
  // That decodes to: "DaktLib " + "Dakt" = "DaktLib Dakt" (12 bytes)
  // For the full "DaktLib DaktLib " we need match_length = 8, so nibble = 4:
  //   Token:  0x84 = (8 << 4) | 4   -> 8 literals, match nibble 4 -> length 4+4=8
  //   Result: "DaktLib " + "DaktLib " = "DaktLib DaktLib " (16 bytes)

  dakt::vector<uint8t> lz4_block = {
   0x84, // Token: 8 literals, match nibble=4
   'D',
   'a',
   'k',
   't',
   'L',
   'i',
   'b',
   ' ', // 8 literal bytes
   0x08,
   0x00 // Match offset = 8 (points back to start)
        // No end-of-block literals needed — match ends the sequence and src == src_end
  };

  Lz4                  engine;
  dakt::vector<uint8t> output;
  usize                bytes = engine.inflateChunk(lz4_block, output);

  assert(bytes > 0 && "Lz4: inflateChunk with match sequence returned 0 bytes");

  string_view expected = "DaktLib DaktLib ";
  assert(output.size() == expected.size() && "Lz4: match sequence output size mismatch");
  assert(memcmp(output.data(), expected.data(), expected.size()) == 0 && "Lz4: match sequence data corruption");

  cout << "[PASS] Lz4: Direct inflateChunk (literal + match sequence)\n";
}

void testLz4EmptyInput() {
  Lz4                  engine;
  dakt::vector<uint8t> empty;
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(empty, output);
  assert(bytes == 0 && "Lz4: empty input should return 0");

  cout << "[PASS] Lz4: Empty input guard\n";
}

void testLz4Pipeline() {
  // Same literals-only block as above, run through the full C-API stream layer
  dakt::vector<uint8t> lz4_block = {0x80, 'D', 'a', 'k', 't', 'L', 'i', 'b', ' '};

  auto                 output    = runPipeline(lz4_block, CompressionMethod::Lz4);

  assert(output.size() > 0 && "Lz4 pipeline: zero bytes returned");

  string_view expected = "DaktLib ";
  assert(output.size() == expected.size() && "Lz4 pipeline: size mismatch");
  assert(memcmp(output.data(), expected.data(), expected.size()) == 0 && "Lz4 pipeline: data corruption");

  cout << "[PASS] Lz4: Full C-API pipeline\n";
}

// ============================================================================
// Zstd
// ============================================================================

void testZstdMagicNumberReject() {
  // A buffer with a wrong magic number should return 0 bytes, not crash.
  Zstd                 engine;
  dakt::vector<uint8t> bad_magic = {0x00, 0x00, 0x00, 0x00, 0xAA, 0xBB, 0xCC};
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(bad_magic, output);
  assert(bytes == 0 && "Zstd: wrong magic should return 0");

  cout << "[PASS] Zstd: Bad magic number rejected cleanly\n";
}

void testZstdTruncatedFrame() {
  // Correct magic but nothing after it — frame header parse should fail cleanly.
  Zstd                 engine;
  dakt::vector<uint8t> truncated = {0x28, 0xB5, 0x2F, 0xFD};
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(truncated, output);
  assert(bytes == 0 && "Zstd: truncated frame should return 0");

  cout << "[PASS] Zstd: Truncated frame rejected cleanly\n";
}

void testZstdRawBlock() {
  // Manually construct a minimal single-segment Zstd frame with one Raw block.
  //
  // Frame layout:
  //   Magic:        28 B5 2F FD
  //   FHD:          0x60 = (FCS=1, SS=1, no dict)  -> single segment, 1-byte FCS
  //   FCS:          0x05 (frame content size = 5)
  //   Block header: 0x0D 0x00 0x00 = last_block=1, type=Raw(0b00), size=1 (3-bit LSB-first)
  //                 Encoding: (size << 3) | (block_type << 1) | last_block
  //                         = (5 << 3) | (0 << 1) | 1 = 0x29 -> 3 bytes LE: 0x29 0x00 0x00
  //   Payload:      'H' 'e' 'l' 'l' 'o'
  dakt::vector<uint8t> zstd_raw_block_frame = {
   0x28,
   0xB5,
   0x2F,
   0xFD, // Magic
   0x20, // FHD: single_segment=1, fcs_field=1 (1-byte)
   0x05, // FCS: 5 bytes
   0x29,
   0x00,
   0x00, // Block header: last=1, type=Raw, size=5
   'H',
   'e',
   'l',
   'l',
   'o' // Raw payload
  };

  Zstd                 engine;
  dakt::vector<uint8t> output;
  usize                bytes = engine.inflateChunk(zstd_raw_block_frame, output);

  assert(bytes != 0 && "Zstd: raw block frame returned 0");

  string_view expected = "Hello";
  assert(output.size() == expected.size() && "Zstd: raw block output size mismatch");
  assert(memcmp(output.data(), expected.data(), expected.size()) == 0 && "Zstd: raw block data corruption");

  cout << "[PASS] Zstd: Single raw block frame\n";
}

void testZstdRleBlock() {
  // Minimal Zstd frame with one RLE block: repeats byte 'X' 8 times.
  //
  //   Block header: (8 << 3) | (1 << 1) | 1 = 0x43 0x00 0x00
  //   (block_type=1=RLE, last=1, size=8 -> decoded size; RLE reads 1 byte from stream)
  dakt::vector<uint8t> zstd_rle_frame = {
   0x28,
   0xB5,
   0x2F,
   0xFD, // Magic
   0x20, // FHD: single_segment, 1-byte FCS
   0x08, // FCS: 8 bytes
   0x43,
   0x00,
   0x00, // Block header: last=1, type=RLE(1), size=8
   'X'   // The single byte to repeat
  };

  Zstd                 engine;
  dakt::vector<uint8t> output;
  usize                bytes = engine.inflateChunk(zstd_rle_frame, output);

  assert(bytes != 0 && "Zstd: RLE block frame returned 0");

  dakt::vector<uint8t> expected(8, 'X');
  assert(output.size() == expected.size() && "Zstd: RLE output size mismatch");
  assert(memcmp(output.data(), expected.data(), expected.size()) == 0 && "Zstd: RLE data corruption");

  cout << "[PASS] Zstd: Single RLE block frame\n";
}

void testZstdPipelineInitOnly() {
  // Only validates the pipeline doesn't return -1 or -2 for a valid magic prefix.
  // Full decompression read is not asserted since the frame is incomplete.
  dakt::vector<uint8t> partial     = {0x28, 0xB5, 0x2F, 0xFD};

  auto                 mock_stream = make_unique<MockMemoryStream>(dakt::move(partial));
  DataStream           c_input     = capi::makeCStream(dakt::move(mock_stream));
  DataStream           c_output{};

  int result = zipCreateDecompressionStream(&c_input, static_cast<int>(CompressionMethod::Zstd), &c_output);
  assert(result == 0 && "Zstd pipeline: registry miss or null pointer");

  c_output.close(c_output.context);
  cout << "[PASS] Zstd: Pipeline initialization (registry + stream wrap)\n";
}

// ============================================================================
// Zlib / Deflate
// ============================================================================

void testZlibValidPayload() {
  // Standard Zlib frame for "Hello Deflate!" — same payload as the old Catch2 test.
  Zlib engine;
  // Standard Zlib-wrapped fixed Huffman block for "Hello Deflate!"
  // Adler-32 of "Hello Deflate!" = 0x257704EB
  //   -> big-endian bytes: 0x25, 0x77, 0x04, 0xEB
  dakt::vector<uint8t> payload = {
   0x78,
   0x9C, // Zlib header (CM=8, CINFO=7, optimal compression)
   0xF3, 0x48, 0xCD, 0xC9, 0xC9, 0x57, 0x70, 0x49, 0x4D, 0xCB, 0x49, 0x2C, //  Fixed Huffman deflate stream
   0x49, 0x55, 0x04, 0x00, 0x25, 0x77, 0x04,
   0xEB                                                                    // Adler-32 of "Hello Deflate!" (big-endian)
  };

  dakt::vector<uint8t> output;
  usize                bytes = engine.inflateChunk(payload, output);

  assert(bytes > 0 && "Zlib: valid payload returned 0 bytes");

  string_view expected = "Hello Deflate!";
  assert(output.size() == expected.size() && "Zlib: output size mismatch");
  assert(memcmp(output.data(), expected.data(), expected.size()) == 0 && "Zlib: data corruption");

  cout << "[PASS] Zlib: Valid Deflate payload\n";
}

void testZlibCorruptHeader() {
  // Breaking the CMF/FLG checksum — the engine must reject this cleanly.
  Zlib                 engine;
  dakt::vector<uint8t> corrupt = {0x78, 0x00, 0xF3, 0x48, 0xCD, 0xC9, 0xC9};
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(corrupt, output);
  assert(bytes == 0 && "Zlib: corrupt header should return 0 bytes");

  cout << "[PASS] Zlib: Corrupt header rejected cleanly\n";
}

void testZlibEmptyInput() {
  Zlib                 engine;
  dakt::vector<uint8t> empty;
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(empty, output);
  assert(bytes == 0 && "Zlib: empty input should return 0");

  cout << "[PASS] Zlib: Empty input guard\n";
}

// ============================================================================
// Lzx (Partial Implementation — Stub-Safe)
// ============================================================================

void testLzxEmptyInput() {
  Lzx                  engine;
  dakt::vector<uint8t> empty;
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(empty, output);
  assert(bytes == 0 && "Lzx: empty input should return 0");

  cout << "[PASS] Lzx: Empty input guard\n";
}

void testLzxRandomBytesNoCrash() {
  // Feeds garbage bytes — the implementation must not crash or corrupt memory.
  // We do not assert on the return value since partial LZX is not guaranteed to succeed.
  Lzx                  engine;
  dakt::vector<uint8t> garbage = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03};
  dakt::vector<uint8t> output;

  engine.inflateChunk(garbage, output); // No assert — just must not crash

  cout << "[PASS] Lzx: Garbage input does not crash (stub-safe)\n";
}

// TODO: Replace testLzx_RandomBytes_NoCrash with a byte-exact assertion once
// a known-good LZX compressed payload is available for the target window size.

// ============================================================================
// XMem (Delegates to Lzx — Stub-Safe)
// ============================================================================

void testXMemEmptyInput() {
  XMem                 engine;
  dakt::vector<uint8t> empty;
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(empty, output);
  assert(bytes == 0 && "XMem: empty input should return 0");

  cout << "[PASS] XMem: Empty input guard\n";
}

void testXMemTruncatedHeaderNoCrash() {
  // Only 2 bytes — the 4-byte XMem frame header read must bounds-check cleanly.
  XMem                 engine;
  dakt::vector<uint8t> truncated = {0x00, 0x80};
  dakt::vector<uint8t> output;

  usize                bytes = engine.inflateChunk(truncated, output);
  assert(bytes == 0 && "XMem: truncated header should return 0");

  cout << "[PASS] XMem: Truncated header guard\n";
}

// TODO: Replace with a full round-trip test once an XMem frame payload is
// available (requires a real XMem-compressed P4K chunk or a custom encoder).

// ============================================================================
// Registry
// ============================================================================

void testRegistryAllMethodsRegistered() {
  // Verifies that self-registration fired for every live method.
  // Lzx and XMem are included because their statics register even if inflate is partial.
  const dakt::array<CompressionMethod, 6> methods = {CompressionMethod::Store,
                                                     CompressionMethod::Zlib,
                                                     CompressionMethod::XMem,
                                                     CompressionMethod::Lzx,
                                                     CompressionMethod::Lz4,
                                                     CompressionMethod::Zstd};

  for (auto m : methods) {
    auto* algo = CompressionRegistry::get(m);
    assert(algo != nullptr && "Registry: a method is missing from the registry");
    assert(algo->method() == m && "Registry: method() enum mismatch after lookup");
  }

  cout << "[PASS] Registry: All methods registered and enum-stable\n";
}

void testRegistryUnknownMethodReturnsNull() {
  auto* algo = CompressionRegistry::get(CompressionMethod::Unknown);
  assert(algo == nullptr && "Registry: Unknown should return nullptr");

  cout << "[PASS] Registry: Unknown method returns nullptr\n";
}

// ============================================================================
// Main Runner
// ============================================================================

auto main() -> int {
  cout << "--- DaktLib-Zip Tests ---\n\n";

  cout << "[Store]\n";
  testStoreDirectInflate();
  testStoreDirectDeflate();
  testStoreRoundTrip();
  testStoreEmptyInput();
  testStorePipeline();

  cout << "\n[Lz4]\n";
  testLz4EmptyInput();
  testLz4DirectInflateLiteralsOnly();
  testLz4DirectInflateWithMatch();
  testLz4Pipeline();

  cout << "\n[Zstd]\n";
  testZstdMagicNumberReject();
  testZstdTruncatedFrame();
  testZstdRawBlock();
  testZstdRleBlock();
  testZstdPipelineInitOnly();

  cout << "\n[Zlib]\n";
  testZlibValidPayload();
  testZlibCorruptHeader();
  testZlibEmptyInput();

  cout << "\n[Lzx]\n";
  testLzxEmptyInput();
  testLzxRandomBytesNoCrash();

  cout << "\n[XMem]\n";
  testXMemEmptyInput();
  testXMemTruncatedHeaderNoCrash();

  cout << "\n[Registry]\n";
  testRegistryAllMethodsRegistered();
  testRegistryUnknownMethodReturnsNull();

  cout << "\nAll tests completed successfully.\n";
  return 0;
}

#endif // DAKTLIB_TESTS_ZIP_TESTS_CXX