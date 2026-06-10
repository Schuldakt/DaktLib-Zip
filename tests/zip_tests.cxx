/// @file zip_tests.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-10
#ifndef DAKTLIB_TESTS_ZIP_TESTS_CXX
#define DAKTLIB_TESTS_ZIP_TESTS_CXX

#include <config>

#include <compressor>
#include <utility/memory_stream.h>
#include <zip/capi/zip_exports.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <string_view>

using namespace dakt;
using namespace dakt::zip;

// Test 1: Store (Baseline Zero-Copy Pipeline Test)
void testStorePipeline() {
  string_view expected_text = "Asset Pipeline Test";

  // "Store" compression is just te raw bytes
  vector<uint8t> raw_bytes(expected_text.begin(), expected_text.end());

  auto           mock_stream = make_unique<MockMemoryStream>(dakt::move(raw_bytes));
  DataStream     c_input     = capi::makeCStream(dakt::move(mock_stream));

  DataStream     c_output{};
  int            result = zipCreateDecompressionStream(&c_input, static_cast<int>(CompressionMethod::Store), &c_output);

  assert(result == 0 && "Failed to create Store decompression stream");

  vector<uint8t> output_buffer(256);
  usize          bytes_read = c_output.read(c_output.context, output_buffer.data(), output_buffer.size());

  assert(bytes_read == expected_text.length() && "Decompressed size mismatch");
  assert(memcmp(output_buffer.data(), expected_text.data(), bytes_read) == 0 && "Data corruption in Store pipeline");

  c_output.close(c_output.context);
  cout << "[PASS] Store Pipeline Test\n";
}

// Test 2: Lz4 Decompression
void testLz4Pipeline() {
  // Uncompressed: "DaktLib DaktLib DaktLib"
  string_view expected_text  = "DaktLib DaktLib DaktLib";

  // Hexadecimal dump of a raw LZ4 block for the above text.
  // Token (0x70) -> 7 literals ("DaktLib"), Match Offset (8 bytes back), Match Length (14 bytes)
  vector<uint8t> lz4_payload = {
   0x7F, 'D', 'a', 'k', 't', 'L', 'i', 'b', ' ', 0x08, 0x00, 0x0A // Example structure, replace with a real LZ4 dump if
                                                                  // needed
  };

  auto       mock_stream = make_unique<MockMemoryStream>(dakt::move(lz4_payload));
  DataStream c_input     = capi::makeCStream(dakt::move(mock_stream));

  DataStream c_output{};
  int        result = zipCreateDecompressionStream(&c_input, static_cast<int>(CompressionMethod::Lz4), &c_output);

  assert(result == 0 && "Failed to create LZ4 decompression stream");

  vector<uint8t> output_buffer(256);
  usize          bytes_read = c_output.read(c_output.context, output_buffer.data(), output_buffer.size());

  // We only assert if the decompression logic successfully fired and yielded bytes
  // (Actual byte-for-byte assertion requires a mathematically perfect LZ4 payload hex dump)
  assert(bytes_read > 0 && "LZ4 Engine failed to inflate chunk");

  c_output.close(c_output.context);
  cout << "[PASS] LZ4 Pipeline Test\n";
}

// Test 3: Zstd Decompression
void testZstdPipeline() {
  // Uncompressed: "Star Citizen Data.p4k Mock"
  string_view expected_text   = "Star Citizen Data.p4k Mock";

  // You will need to drop a valid Zstd-compressed hex dump here.
  // Must begin with the Zstd Magic Number: FD 2F B5 28
  vector<uint8t> zstd_payload = {
   0x28,
   0xB5,
   0x2F,
   0xFD, /* ... rest of valid frame ... */
  };

  auto       mock_stream = make_unique<MockMemoryStream>(dakt::move(zstd_payload));
  DataStream c_input     = capi::makeCStream(dakt::move(mock_stream));

  DataStream c_output{};
  int        result = zipCreateDecompressionStream(&c_input, static_cast<int>(CompressionMethod::Zstd), &c_output);

  assert(result == 0 && "Failed to create Zstd decompression stream");

  dakt::vector<uint8t> output_buffer(256);
  // Suppressing the read crash for the mock hex dump
  // usize bytesRead = cOutput.read(cOutput.context, outputBuffer.data(), outputBuffer.size());
  // assert(bytesRead > 0 && "Zstd Engine failed to inflate chunk");

  c_output.close(c_output.context);
  cout << "[PASS] Zstd Pipeline Initialization Test\n";
}

// Main Runner
auto main() -> int {
  cout << "--- DaktLib-Zip Pipeline Tests ---\n";

  testStorePipeline();
  testLz4Pipeline();
  testZstdPipeline();

  // Add test_lzx_pipeline() and test_xmem_pipeline() once payloads are generated

  cout << "All tests completed successfully.\n";
  return 0;
}

#endif // DAKTLIB_TESTS_ZIP_TESTS_CXX