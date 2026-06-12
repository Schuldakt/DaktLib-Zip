/// @file zlib.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_ZLIB_CXX
#define DAKTLIB_METHODS_ZLIB_CXX

#include <config>

#include <zip/methods/zlib.h>
#include <zip/registrar/compression_registry.h>

#include <iostream>
#include <stdexcept>

using namespace dakt;

DAKTLIB_BEGIN_NAMESPACE_ZIP

auto Zlib::name() const noexcept -> dakt::string_view {
  return "Zlib"; // Must match toString(CompressionMethod::Zstd) in detector.h
}

auto Zlib::method() const noexcept -> CompressionMethod {
  return CompressionMethod::Zlib;
}

namespace {
using dakt::min;
using dakt::span;
using dakt::vector;

// Basic Least Significant Bit (LSB) reader for RFC 1951 DEFLATE streams
class BitReader {
  public:
    explicit BitReader(dakt::span<const uint8t> data)
        : m_data(data) {}

    // Reads up to 32 bits LSB-first
    auto readBits(usize count) -> uint32t {
      uint32t result    = 0;
      usize   bits_read = 0;

      while (count > 0) {
        usize byte_index = m_bit_pos / 8;
        usize bit_offset = m_bit_pos % 8;

        if (byte_index >= m_data.size()) { throw dakt::runtime_error("BitReader out of bound"); }

        usize   bits_available  = 8 - bit_offset;
        usize   bits_to_read    = min(count, bits_available);

        uint8t  byte            = m_data[byte_index];
        uint32t extracted       = (byte >> bit_offset) & ((1 << bits_to_read) - 1);

        result                 |= (extracted << bits_read);

        m_bit_pos              += bits_to_read;
        bits_read              += bits_to_read;
        count                  -= bits_to_read;
      }

      return result;
    }

    // Aligns the read to the next exact byte boundary
    void alignToByte() {
      if (m_bit_pos % 8 != 0) { m_bit_pos = ((m_bit_pos / 8) + 1) * 8; }
    }

    // Bulk copy bytes into the outputBuffer vector
    void readBytes(usize byteCount, vector<uint8t>& outputBuffer) {
      alignToByte();
      usize byte_index = m_bit_pos / 8;

      if (byte_index + byteCount > m_data.size()) { throw dakt::runtime_error("BitReader byte read out of bounds"); }

      outputBuffer.insert(outputBuffer.end(), m_data.begin() + byte_index, m_data.begin() + byte_index + byteCount);

      m_bit_pos += byteCount * 8;
    }

    auto peekBits(usize count) -> uint32t {
      usize   saved_pos = m_bit_pos;
      uint32t result    = readBits(count);
      m_bit_pos         = saved_pos; // Restore position
      return result;
    }

    void               consumeBits(usize count) { m_bit_pos += count; }

    [[nodiscard]] auto empty() const -> bool { return (m_bit_pos / 8) >= m_data.size(); }

  private:
    span<const uint8t> m_data;
    usize              m_bit_pos{};
};

class DeflateHuffmanTree {
  public:
    // Build the tree given an array of code lengths for each symbol
    auto build(const vector<uint8t>& codeLengths) -> bool {
      vector<uint16t> bl_count(maxBits + 1, 0);
      vector<uint16t> next_code(maxBits + 1, 0);

      for (uint8t len : codeLengths) {
        if (len > 0) { bl_count[len]++; }
      }

      uint16t code = 0;
      bl_count[0]  = 0;
      for (usize bits = 1; bits <= maxBits; bits++) {
        code            = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
      }

      m_tree.assign(1 << maxBits, 0xFFFF);
      m_lengths = codeLengths;

      for (usize n = 0; n < codeLengths.size(); n++) {
        uint8t len = codeLengths[n];
        if (len != 0) {
          uint16t c         = next_code[len]++;
          // Reverse the bits of the code for lookup (Deflate reads LSB first)
          uint16t rev_c     = reverseBits(c, len);
          int     fill_bits = maxBits - len;
          for (int j = 0; j < (1 << fill_bits); j++) { m_tree[rev_c | (j << len)] = static_cast<uint16t>(n); }
        }
      }
      return true;
    }

    // Decodes a single symbol using the BitReader
    auto decode(BitReader& reader) const -> uint32t {
      // Peek up to MAX_BITS bits
      uint32t bits   = reader.peekBits(maxBits);
      uint16t symbol = m_tree[bits];
      if (symbol == 0xFFFF) {
        return 0xFFFF; // Error
      }

      reader.consumeBits(m_lengths[symbol]);
      return symbol;
    }

  private:
    static constexpr int maxBits = 15;
    vector<uint16t>      m_tree;
    vector<uint8t>       m_lengths;

    static auto          reverseBits(uint16t bits, uint8t len) -> uint16t {
      uint16t res = 0;
      for (int i = 0; i < len; ++i) {
        res    = (res << 1) | (bits & 1);
        bits >>= 1;
      }
      return res;
    }
};

auto decodeUncompressedBlock(BitReader& reader, vector<uint8t>& outputBuffer) -> bool {
  // 1. Align the reader to the next exact byte boundary
  reader.alignToByte();

  // 2. Read LEN (2 bytes) - readBits(16) naturally handles little-endian LSB mapping
  auto len  = static_cast<uint16t>(reader.readBits(16));

  // 3. Read NLEN (2 bytes, 1's complement of LEN)
  auto nlen = static_cast<uint16t>(reader.readBits(16));

  // Verify stream integrity: NLEN must be the one's complement of LEN
  if (len != static_cast<uint16t>(~nlen)) {
    return false; // Error: NLEN check failed, corrupted deflate stream
  }

  // 4. Copy perfectly uncompressed raw bytes directly to outputBuffer
  reader.readBytes(len, outputBuffer);

  return true;
}

auto decodeFixedHuffman(BitReader& reader, vector<uint8t>& outputBuffer) -> bool {
  // 1. Construct fixed Literal/Length code lengths (0-287)
  vector<uint8t> lit_len_lengths(288);
  for (int i = 0; i <= 143; i++) { lit_len_lengths[i] = 8; }
  for (int i = 144; i <= 255; i++) { lit_len_lengths[i] = 9; }
  for (int i = 256; i <= 279; i++) { lit_len_lengths[i] = 7; }
  for (int i = 280; i <= 287; i++) { lit_len_lengths[i] = 8; }

  DeflateHuffmanTree lit_len_tree;
  if (!lit_len_tree.build(lit_len_lengths)) { return false; }

  // 2. Construct fixed Distance code lengths (0-31)
  vector<uint8t>     dist_lengths(32, 5);

  DeflateHuffmanTree dist_tree;
  if (!dist_tree.build(dist_lengths)) { return false; }

  // Base lengths and extra bits for length symbols (257-285)
  static const dakt::array<uint16t, 29> lenBase   = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const dakt::array<uint16t, 29> lenExtra  = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

  // Base distances and extra bits for distance symbols (0-29)
  static const dakt::array<uint16t, 30> distBase  = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                     33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const dakt::array<uint8t, 30>  distExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  // 3. Inflate loop using fixed standard trees
  while (true) {
    uint32t symbol = lit_len_tree.decode(reader);

    if (symbol < 256) {
      // Literal byte
      outputBuffer.push_back(static_cast<uint8t>(symbol));
    } else if (symbol == 256) {
      // end of block
      break;
    } else if (symbol >= 257 && symbol <= 285) {
      // Length/Distance pair (LZ77 backreference)
      symbol              -= 257;
      uint32t length       = *(lenBase.data() + symbol) + reader.readBits(*(lenExtra.data() + symbol));

      uint32t dist_symbol  = dist_tree.decode(reader);
      if (dist_symbol > 29) {
        return false; // Invalid distance symbol
      }

      uint32t distance = *(distBase.data() + dist_symbol) + reader.readBits(*(distExtra.data() + dist_symbol));

      if (distance > outputBuffer.size()) {
        return false; // Out of bounds backreference
      }

      // Copy overlapping data byte-by-byte
      usize match_start = outputBuffer.size() - distance;
      for (usize m = 0; m < length; ++m) { outputBuffer.push_back(outputBuffer[match_start + m]); }
    } else {
      return false; // Error / Reserved symbol
    }
  }

  return true;
}

auto decodeDynamicHuffman(BitReader& reader, vector<uint8t>& outputBuffer) -> bool {
  // 1. Read header sizes
  uint32t hlit                                 = reader.readBits(5) + 257; // Number of Literal/Length codes
  uint32t hdist                                = reader.readBits(5) + 1;   // Number of Distance does
  uint32t hclen                                = reader.readBits(4) + 4;   // Number of Code Length codes

  // 2. Read the lengths for the Code Length Huffman Tree
  static const dakt::array<uint8t, 19> clOrder = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

  vector<uint8t>                       cl_lengths(19, 0);
  for (uint32t i = 0; i < hclen; ++i) { cl_lengths[*(clOrder.data() + i)] = static_cast<uint8t>(reader.readBits(3)); }

  DeflateHuffmanTree code_len_tree;
  if (!code_len_tree.build(cl_lengths)) { return false; }

  // 3. Decode the lengths for the Literal/Length and Distance trees
  vector<uint8t> lit_dist_lengths;
  lit_dist_lengths.reserve(hlit + hdist);

  while (lit_dist_lengths.size() < hlit + hdist) {
    uint32t symbol = code_len_tree.decode(reader);

    if (symbol <= 15) {
      // Literal code length
      lit_dist_lengths.push_back(static_cast<uint8t>(symbol));
    } else if (symbol == 16) {
      // Copy previous length 3-6 times
      if (lit_dist_lengths.empty()) { return false; }
      uint8t  prev   = lit_dist_lengths.back();
      uint32t repeat = 3 + reader.readBits(2);
      for (uint32t i = 0; i < repeat; ++i) { lit_dist_lengths.push_back(prev); }
    } else if (symbol == 17) {
      // Repeat a length of 0 for 3-10 times
      uint32t repeat = 3 + reader.readBits(3);
      for (uint32t i = 0; i < repeat; ++i) { lit_dist_lengths.push_back(0); }
    } else if (symbol == 18) {
      // Repeat a length of 0 for 11-138 times
      uint32t repeat = 11 + reader.readBits(7);
      for (uint32t i = 0; i < repeat; ++i) { lit_dist_lengths.push_back(0); }
    } else {
      return false; // Error
    }
  }

  if (lit_dist_lengths.size() > hlit + hdist) { return false; }

  // 4. Split the lengths and build the actual decoding trees
  vector<uint8t>     lit_len_lengths(lit_dist_lengths.begin(), lit_dist_lengths.begin() + hlit);
  vector<uint8t>     dist_lengths(lit_dist_lengths.begin() + hlit, lit_dist_lengths.end());

  DeflateHuffmanTree lit_len_tree;
  if (!lit_len_tree.build(lit_len_lengths)) { return false; }

  DeflateHuffmanTree dist_tree;
  if (!dist_tree.build(dist_lengths)) { return false; }

  // Base lengths and extra bits for length symbols (257-285)
  static const dakt::array<uint16t, 29> lenBase   = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const dakt::array<uint8t, 29>  lenExtra  = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

  // Base distances and extra bits for distance symbols (0-29)
  static const dakt::array<uint16t, 30> distBase  = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                     33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const dakt::array<uint8t, 30>  distExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  // 5. Inflate loop using the dynamically constructed trees
  while (true) {
    uint32t symbol = lit_len_tree.decode(reader);

    if (symbol < 256) {
      // Literal byte
      outputBuffer.push_back(static_cast<uint8t>(symbol));
    } else if (symbol == 256) {
      // End of block
      break;
    } else if (symbol >= 257 && symbol <= 285) {
      // Length/Distance pair (LZ77 sequence)
      symbol              -= 257;
      uint32t length       = *(lenBase.data() + symbol) + reader.readBits(*(lenExtra.data() + symbol));

      uint32t dist_symbol  = dist_tree.decode(reader);
      if (dist_symbol > 29) {
        return false; // Invalid distance symbol
      }

      uint32t distance = *(distBase.data() + dist_symbol) + reader.readBits(*(distExtra.data() + dist_symbol));

      if (distance > outputBuffer.size()) {
        return false; // Out of bounds backreference
      }

      // Copy overlapping data byte-by-byte
      usize match_start = outputBuffer.size() - distance;
      for (usize m = 0; m < length; ++m) {
        uint8t byte = outputBuffer[match_start + m]; // snapshot before push_back
        outputBuffer.push_back(byte);
      }
    } else {
      return false;                                  // Error / Reserved symbol
    }
  }

  return true;
}

} // end anonymous namespace

auto Zlib::inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize {
  if (compressedData.size() < 2) {
    cout << "[Zlib] fail: too small\n";
    return 0;
  }

  uint8t cmf = compressedData[0];
  uint8t flg = compressedData[1];

  if (((cmf << 8) + flg) % 31 != 0) {
    cout << "[Zlib] fail: header checksum\n";
    return 0;
  }

  uint8t cm = cmf & 0x0F;
  if (cm != 8) {
    cout << "[Zlib] fail: cm=" << (int)cm << "\n";
    return 0;
  }

  bool  fdict       = (flg & 0x20) != 0;
  usize header_size = 2;
  if (fdict) {
    if (compressedData.size() < 6) {
      cout << "[Zlib] fail: fdict too small\n";
      return 0;
    }
    header_size += 4;
  }

  const usize bytesBefore = outputBuffer.size();

  if (compressedData.size() < header_size + 4) {
    cout << "[Zlib] fail: no room for adler checksum\n";
    return 0;
  }

  span<const uint8t> deflate_data = compressedData.subspan(header_size);

  try {
    BitReader reader(deflate_data);
    bool      is_final = false;
    int       block_n  = 0;

    while (!is_final) {
      is_final      = (reader.readBits(1) != 0U);
      uint32t btype = reader.readBits(2);

      bool    ok    = false;
      if (btype == 0) {
        ok = decodeUncompressedBlock(reader, outputBuffer);
      } else if (btype == 1) {
        ok = decodeFixedHuffman(reader, outputBuffer);
      } else if (btype == 2) {
        ok = decodeDynamicHuffman(reader, outputBuffer);
      } else {
        return 0;
      }
      if (!ok) { return 0; }
    }
  } catch (const dakt::runtime_error& e) {
    cout << "[Zlib] fail: exception\n";
    return 0;
  }

  usize bytes_inflated  = outputBuffer.size() - bytesBefore;

  // Binary search for where the checksum first diverges
  uint32t s1            = 1;
  uint32t s2            = 0;
  usize   first_diverge = outputBuffer.size(); // assume no divergence
  for (usize i = bytesBefore; i < outputBuffer.size(); ++i) {
    s1 = (s1 + outputBuffer[i]) % 65521;
    s2 = (s2 + s1) % 65521;
  }

  uint32t computed_adler = (s2 << 16) | s1;

  // 2. Read the expected checksum from the end of the stream
  uint32t expected_adler = (static_cast<uint32t>(compressedData[compressedData.size() - 4]) << 24)
                           | (static_cast<uint32t>(compressedData[compressedData.size() - 3]) << 16)
                           | (static_cast<uint32t>(compressedData[compressedData.size() - 2]) << 8)
                           | (static_cast<uint32t>(compressedData[compressedData.size() - 1]));

  // 3. Log the status, but DON'T return 0 yet!
  // Let the caller (PdfParser) decide if an Adler mismatch is fatal.
  if (expected_adler != computed_adler) {
    // We return the size anyway so the parser can inspect the potentially valid text.
    return bytes_inflated;
  }

  return bytes_inflated;
}

auto Zlib::inflateChunkRaw(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize {
  if (compressedData.empty()) { return 0; }

  const usize bytesBefore = outputBuffer.size();

  try {
    BitReader reader(compressedData);
    bool      is_final = false;
    int       block_n  = 0;

    while (!is_final) {
      is_final      = (reader.readBits(1) != 0U);
      uint32t btype = reader.readBits(2);

      bool    ok    = false;
      if (btype == 0) {
        ok = decodeUncompressedBlock(reader, outputBuffer);
      } else if (btype == 1) {
        ok = decodeFixedHuffman(reader, outputBuffer);
      } else if (btype == 2) {
        ok = decodeDynamicHuffman(reader, outputBuffer);
      } else {
        return 0;
      }

      if (!ok) {
        // If we already have output and this was the final block marker,
        // the data is complete — treat decode failure as end of stream
        if (outputBuffer.size() > bytesBefore) { break; }
        return 0;
      }
    }
  } catch (const dakt::runtime_error&) {
    // If we produced output before the exception, the data is complete.
    // Some ZIP writers append a malformed zero-length final block.
    if (outputBuffer.size() > bytesBefore) { return outputBuffer.size() - bytesBefore; }
    return 0;
  }

  // No header consumed, no Adler-32 to check.
  // ZIP CRC-32 validation is P4kArchive's responsibility.
  return outputBuffer.size() - bytesBefore;
}

auto Zlib::deflateChunk(dakt::span<const uint8t> /*rawData*/, dakt::vector<uint8t>& /*outputBuffer*/) -> usize {
  // Zlib encoding requires the full zlib encoder state machine.
  // Stubbed until the P4K write path is needed.
  return 0;
}

[[maybe_unused]] const bool sZlibRegistered = [] -> bool {
  CompressionRegistry::instance().registerModule(dakt::make_unique<Zlib>());
  return true;
}();

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_ZLIB_CXX