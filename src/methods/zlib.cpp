/// @file Zlib.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-27

#pragma once
#include <stdexcept>
#include <zlib.hpp>

namespace dakt::decrypt {

std::string_view  Zlib::name() const noexcept { return "Zlib/Deflate"; }

CompressionMethod Zlib::method() const noexcept { return CompressionMethod::Deflate; }

namespace {
using std::min;
using std::span;
using std::vector;

// Basic Least Significant Bit (LSB) reader for RFC 1951 DEFLATE streams
class BitReader {
public:
  explicit BitReader(std::span<const uint8_t> data) : m_data(data), m_bitPos(0) {}

  // Reads up to 32 bits LSB-first
  uint32_t readBits(size_t count) {
    uint32_t result   = 0;
    size_t   bitsRead = 0;

    while (count > 0) {
      size_t byteIndex = m_bitPos / 8;
      size_t bitOffset = m_bitPos % 8;

      if (byteIndex >= m_data.size()) {
        throw std::runtime_error("BitReader out of bound");
      }

      size_t   bitsAvailable  = 8 - bitOffset;
      size_t   bitsToRead     = min(count, bitsAvailable);

      uint8_t  byte           = m_data[byteIndex];
      uint32_t extracted      = (byte >> bitOffset) & ((1 << bitsToRead) - 1);

      result                 |= (extracted << bitsRead);

      m_bitPos               += bitsToRead;
      bitsRead               += bitsToRead;
      count                  -= bitsToRead;
    }

    return result;
  }

  // Aligns the read to the next exact byte boundary
  void alignToByte() {
    if (m_bitPos % 8 != 0) {
      m_bitPos = (m_bitPos / 8 + 1) * 8;
    }
  }

  // Bulk copy bytes into the output vector
  void readBytes(size_t byteCount, vector<uint8_t>& output) {
    alignToByte();
    size_t byteIndex = m_bitPos / 8;

    if (byteIndex + byteCount > m_data.size()) {
      throw std::runtime_error("BitReader byte read out of bounds");
    }

    output.insert(output.end(), m_data.begin() + byteIndex, m_data.begin() + byteIndex + byteCount);

    m_bitPos += byteCount * 8;
  }

  uint32_t peekBits(size_t count) {
    size_t   savedPos = m_bitPos;
    uint32_t result   = readBits(count);
    m_bitPos          = savedPos; // Restore position
    return result;
  }

  void consumeBits(size_t count) { m_bitPos += count; }

  bool empty() const { return (m_bitPos / 8) >= m_data.size(); }

private:
  span<const uint8_t> m_data;
  size_t              m_bitPos;
};

class DeflateHuffmanTree {
public:
  // Build the tree given an array of code lengths for each symbol
  bool build(const vector<uint8_t>& codeLengths) {
    vector<uint16_t> bl_count(MAX_BITS + 1, 0);
    vector<uint16_t> next_code(MAX_BITS + 1, 0);

    for (uint8_t len : codeLengths) {
      if (len > 0) {
        bl_count[len]++;
      }
    }

    uint16_t code = 0;
    bl_count[0]   = 0;
    for (size_t bits = 1; bits <= MAX_BITS; bits++) {
      code            = (code + bl_count[bits - 1]) << 1;
      next_code[bits] = code;
    }

    m_tree.assign(1 << MAX_BITS, 0xFFFF);
    m_lengths = codeLengths;

    for (size_t n = 0; n < codeLengths.size(); n++) {
      uint8_t len = codeLengths[n];
      if (len != 0) {
        uint16_t c         = next_code[len]++;
        // Reverse the bits of the code for lookup (Deflate reads LSB first)
        uint16_t rev_c     = reverseBits(c, len);
        int      fill_bits = MAX_BITS - len;
        for (int j = 0; j < (1 << fill_bits); j++) {
          m_tree[rev_c | (j << len)] = static_cast<uint16_t>(n);
        }
      }
    }
    return true;
  }

  // Decodes a single symbol using the BitReader
  uint32_t decode(BitReader& reader) const {
    // Peek up to MAX_BITS bits
    uint32_t bits   = reader.peekBits(MAX_BITS);
    uint16_t symbol = m_tree[bits];
    if (symbol == 0xFFFF) {
      return 0xFFFF; // Error
    }

    reader.consumeBits(m_lengths[symbol]);
    return symbol;
  }

private:
  static constexpr int MAX_BITS = 15;
  vector<uint16_t>     m_tree;
  vector<uint8_t>      m_lengths;

  static uint16_t      reverseBits(uint16_t bits, uint8_t len) {
    uint16_t res = 0;
    for (int i = 0; i < len; ++i) {
      res    = (res << 1) | (bits & 1);
      bits >>= 1;
    }
    return res;
  }
};

bool decode_uncompressed_block(BitReader& reader, vector<uint8_t>& output) {
  // 1. Align the reader to the next exact byte boundary
  reader.alignToByte();

  // 2. Read LEN (2 bytes) - readBits(16) naturally handles little-endian LSB mapping
  uint16_t len  = static_cast<uint16_t>(reader.readBits(16));

  // 3. Read NLEN (2 bytes, 1's complement of LEN)
  uint16_t nlen = static_cast<uint16_t>(reader.readBits(16));

  // Verify stream integrity: NLEN must be the one's complement of LEN
  if (len != static_cast<uint16_t>(~nlen)) {
    return false; // Error: NLEN check failed, corrupted deflate stream
  }

  // 4. Copy perfectly uncompressed raw bytes directly to output
  reader.readBytes(len, output);

  return true;
}

bool decode_fixed_huffman(BitReader& reader, vector<uint8_t>& output) {
  // 1. Construct fixed Literal/Length code lengths (0-287)
  vector<uint8_t> litLenLengths(288);
  for (int i = 0; i <= 143; i++) {
    litLenLengths[i] = 8;
  }
  for (int i = 144; i <= 255; i++) {
    litLenLengths[i] = 9;
  }
  for (int i = 256; i <= 279; i++) {
    litLenLengths[i] = 7;
  }
  for (int i = 280; i <= 287; i++) {
    litLenLengths[i] = 8;
  }

  DeflateHuffmanTree litLenTree;
  if (!litLenTree.build(litLenLengths)) {
    return false;
  }

  // 2. Construct fixed Distance code lengths (0-31)
  vector<uint8_t>    distLengths(32, 5);

  DeflateHuffmanTree distTree;
  if (!distTree.build(distLengths)) {
    return false;
  }

  // Base lengths and extra bits for length symbols (257-285)
  static const uint16_t LEN_BASE[]  = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                       15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                       67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint8_t  LEN_EXTRA[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

  // Base distances and extra bits for distance symbols (0-29)
  static const uint16_t DIST_BASE[] = {
      1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
      193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t DIST_EXTRA[] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  // 3. Inflate loop using fixed standard trees
  while (true) {
    uint32_t symbol = litLenTree.decode(reader);

    if (symbol < 256) {
      // Literal byte
      output.push_back(static_cast<uint8_t>(symbol));
    } else if (symbol == 256) {
      // end of block
      break;
    } else if (symbol >= 257 && symbol <= 285) {
      // Length/Distance pair (LZ77 backreference)
      symbol              -= 257;
      uint32_t length      = LEN_BASE[symbol] + reader.readBits(LEN_EXTRA[symbol]);

      uint32_t distSymbol  = distTree.decode(reader);
      if (distSymbol > 29) {
        return false; // Invalid distance symbol
      }

      uint32_t distance = DIST_BASE[distSymbol] + reader.readBits(DIST_EXTRA[distSymbol]);

      if (distance > output.size()) {
        return false; // Out of bounds backreference
      }

      // Copy overlapping data byte-by-byte
      size_t match_start = output.size() - distance;
      for (size_t m = 0; m < length; ++m) {
        output.push_back(output[match_start + m]);
      }
    } else {
      return false; // Error / Reserved symbol
    }
  }

  return true;
}

bool decode_dynamic_huffman(BitReader& reader, vector<uint8_t>& output) {
  // 1. Read header sizes
  uint32_t hlit                     = reader.readBits(5) + 257; // Number of Literal/Length codes
  uint32_t hdist                    = reader.readBits(5) + 1;   // Number of Distance does
  uint32_t hclen                    = reader.readBits(4) + 4;   // Number of Code Length codes

  // 2. Read the lengths for the Code Length Huffman Tree
  static const uint8_t cl_order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                       11, 4,  12, 3, 13, 2, 14, 1, 15};

  vector<uint8_t>      cl_lengths(19, 0);
  for (uint32_t i = 0; i < hclen; ++i) {
    cl_lengths[cl_order[i]] = static_cast<uint8_t>(reader.readBits(3));
  }

  DeflateHuffmanTree codeLenTree;
  if (!codeLenTree.build(cl_lengths)) {
    return false;
  }

  // 3. Decode the lengths for the Literal/Length and Distance trees
  vector<uint8_t> litDistLengths;
  litDistLengths.reserve(hlit + hdist);

  while (litDistLengths.size() < hlit + hdist) {
    uint32_t symbol = codeLenTree.decode(reader);

    if (symbol <= 15) {
      // Literal code length
      litDistLengths.push_back(static_cast<uint8_t>(symbol));
    } else if (symbol == 16) {
      // Copy previous length 3-6 times
      if (litDistLengths.empty()) {
        return false;
      }
      uint8_t  prev   = litDistLengths.back();
      uint32_t repeat = 3 + reader.readBits(2);
      for (uint32_t i = 0; i < repeat; ++i) {
        litDistLengths.push_back(prev);
      }
    } else if (symbol == 17) {
      // Repeat a length of 0 for 3-10 times
      uint32_t repeat = 3 + reader.readBits(3);
      for (uint32_t i = 0; i < repeat; ++i) {
        litDistLengths.push_back(0);
      }
    } else if (symbol == 18) {
      // Repeat a length of 0 for 11-138 times
      uint32_t repeat = 11 + reader.readBits(7);
      for (uint32_t i = 0; i < repeat; ++i) {
        litDistLengths.push_back(0);
      }
    } else {
      return false; // Error
    }
  }

  if (litDistLengths.size() > hlit + hdist) {
    return false;
  }

  // 4. Split the lengths and build the actual decoding trees
  vector<uint8_t>    litLenLengths(litDistLengths.begin(), litDistLengths.begin() + hlit);
  vector<uint8_t>    distLengths(litDistLengths.begin() + hlit, litDistLengths.end());

  DeflateHuffmanTree litLenTree;
  if (!litLenTree.build(litLenLengths)) {
    return false;
  }

  DeflateHuffmanTree distTree;
  if (!distTree.build(distLengths)) {
    return false;
  }

  // Base lengths and extra bits for length symbols (257-285)
  static const uint16_t LEN_BASE[] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  23,  27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint8_t  LEN_EXTRA[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

  // Base distances and extra bits for distance symbols (0-29)
  static const uint16_t DIST_BASE[] = {
      1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
      193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t DIST_EXTRA[] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  // 5. Inflate loop using the dynamically constructed trees
  while (true) {
    uint32_t symbol = litLenTree.decode(reader);

    if (symbol < 256) {
      // Literal byte
      output.push_back(static_cast<uint8_t>(symbol));
    } else if (symbol == 256) {
      // End of block
      break;
    } else if (symbol >= 257 && symbol <= 285) {
      // Length/Distance pair (LZ77 sequence)
      symbol              -= 257;
      uint32_t length      = LEN_BASE[symbol] + reader.readBits(LEN_EXTRA[symbol]);

      uint32_t distSymbol  = distTree.decode(reader);
      if (distSymbol > 29) {
        return false; // Invalid distance symbol
      }

      uint32_t distance = DIST_BASE[distSymbol] + reader.readBits(DIST_EXTRA[distSymbol]);

      if (distance > output.size()) {
        return false; // Out of bounds backreference
      }

      // Copy overlapping data byte-by-byte
      size_t match_start = output.size() - distance;
      for (size_t m = 0; m < length; ++m) {
        output.push_back(output[match_start + m]);
      }
    } else {
      return false; // Error / Reserved symbol
    }
  }

  return true;
}

} // end anonymous namespace

bool Zlib::decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const {
  if (input.size() < 2) {
    return false; // Not enough data for Zlib header
  }

  // Parse the 2-byte zlib header (RFC 1950)
  uint8_t cmf = input[0];
  uint8_t flg = input[1];

  // Check header checksum (CMF * 256 + FLG must be a multiple of 31)
  if (((cmf << 8) + flg) % 31 != 0) {
    return false;
  }

  // Check Compression Method (CM) - Expected Deflate (8)
  uint8_t cm = cmf & 0x0F;
  if (cm != 8) {
    return false;
  }

  bool   fdict      = (flg & 0x20) != 0;
  size_t headerSize = 2;

  // If preset dictionary is present, the next 4 bytes are the dictionary ID
  if (fdict) {
    if (input.size() < 6) {
      return false;
    }

    headerSize += 4;
  }

  // Isolate the actual deflate stream payload
  span<const uint8_t> deflateData = input.subspan(headerSize);

  try {
    BitReader reader(deflateData);
    bool      is_final = false;

    while (!is_final) {
      is_final       = reader.readBits(1);
      uint32_t btype = reader.readBits(2);

      bool     ok    = false;
      if (btype == 0) {
        ok = decode_uncompressed_block(reader, output);
      } else if (btype == 1) {
        ok = decode_fixed_huffman(reader, output);
      } else if (btype == 2) {
        ok = decode_dynamic_huffman(reader, output);
      }

      if (!ok) {
        return false; // Block decoding failed
      }
    }
  } catch (const std::runtime_error& e) {
    return false;     // Safely catch out-of-bounds reads
  }

  // Verify the 4-byte Adler-32 checksum at the very end of `input`
  if (input.size() < headerSize + 4) {
    return false; // Not enough data for the checksum
  }

  // Zlib stores the Adler-32 checksum in Big-Endian (Network Byte Order)
  uint32_t expected_adler = (static_cast<uint32_t>(input[input.size() - 4]) << 24) |
                            (static_cast<uint32_t>(input[input.size() - 3]) << 16) |
                            (static_cast<uint32_t>(input[input.size() - 2]) << 8) |
                            (static_cast<uint32_t>(input[input.size() - 1]));

  // Compute Adler-32 on our decompressed output
  uint32_t s1             = 1;
  uint32_t s2             = 0;
  for (uint8_t byte : output) {
    s1 = (s1 + byte) % 65521;
    s2 = (s2 + s1) % 65521;
  }
  uint32_t computed_adler = (s2 << 16) | s1;

  if (expected_adler != computed_adler) {
    // Warning: Checksum mismatch usually indicates subtle data corruption perfectly
    // passing through the Huffman tree, or trailing garbage bits in the span
    return false;
  }

  return true;
}

} // namespace dakt::decrypt