/// @file Zstd.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-27

#pragma once
#include <zlib.hpp>

namespace dakt::decrypt {

std::string_view name() const noexcept { return "Zstd"; }

// --- 1. Bitstream Reader ---
namespace Detail {

BitStreamReader::BitStreamReader(const uint8_t* start, size_t size)
    : m_ptr(start + size), m_start(start), m_bitContainer(0), m_bitsConsumed(0) {
  if (size > 0) {
    uint8_t last_byte = *(m_ptr - 1);
    m_ptr--;
    m_bitsConsumed = 8 - dakt::Bit::countr_zero(static_cast<uint32_t>(last_byte));
    fill();
  }
}

uint64_t BitStreamReader::getBits(uint8_t bits) const {
  return (m_bitContainer >> (64 - m_bitsConsumed - bits)) & ((1ULL << bits) - 1);
}

void BitStreamReader::consumeBits(uint8_t bits) {
  m_bitsConsumed += bits;
  if (m_bitsConsumed >= 32) {
    fill();
  }
}

void BitStreamReader::fill() {
  while (m_bitsConsumed >= 8 && m_ptr > m_start) {
    m_ptr--;
    m_bitContainer  = (m_bitContainer << 8) | *m_ptr;
    m_bitsConsumed -= 8;
  }
}

// --- 2. Decoder State Tables ---
size_t FSETable::build_from_weights(const uint8_t* data, size_t size) {
  if (size == 0) {
    return 0;
  }

  BitStreamReader bits(data, size);

  // FSE headers are read forward (unlike sequences which are read backward)
  // but in Zstd implementation, FSE headers for sequences are often
  // byte-aligned. For raw weights data, we simulate reading the AccuracyLog and
  // probabilities.

  // --- Forward Bitstream Reader State ---
  uint64_t bitContainer  = 0;
  uint32_t bitsAvailable = 0;
  size_t   byteIdx       = 0;

  auto     fillBits      = [&]() {
    while (bitsAvailable <= 56 && byteIdx < size) {
      bitContainer  |= static_cast<uint64_t>(data[byteIdx++]) << bitsAvailable;
      bitsAvailable += 8;
    }
  };

  fillBits();

  // Read AccuracyLog (min 5, max 9 for literal lengths, max 8 for match
  // lengths/offsets)
  accuracyLog          = (bitContainer & 15) + 5;
  bitContainer       >>= 4;
  bitsAvailable       -= 4;

  uint32_t tableSize   = 1 << accuracyLog;
  states.resize(tableSize);

  std::vector<i16> norm(256, 0); // Normalized frequencies
  uint32_t         remaining = tableSize;
  uint32_t         symCount  = 0;

  // Read normalized frequencies (probabilities)
  while (remaining > 0 && symCount < 256) {
    // Calculate max_bits for current remaining value
    uint8_t  maxBits = 0;
    uint32_t temp    = remaining + 1;
    while (temp > 1) {
      maxBits++;
      temp >>= 1;
    }

    fillBits();
    if (bitsAvailable < maxBits) {
      break; // End of properly formed stream
    }

    // Fetch value
    uint16_t val    = bitContainer & ((1 << maxBits) - 1);
    bitContainer  >>= maxBits;
    bitsAvailable  -= maxBits;

    if (val < 15) {
      i16 proba        = val - 1;
      norm[symCount++] = proba;

      if (proba != -1) {
        remaining -= proba;
      } else {
        remaining -= 1; // -1 represents probability of 1 in the remaining pool
      }
    } else {
      // val >= 15 indicates a sequence of (val - 14) symbols with probability 0
      // The std::vector norm defaults to 0, so we just advance the symCount
      // index
      int repeat  = val - 14;
      symCount   += repeat;
    }
  }

  // Distribute states across the table
  // (Translates probabilities into FSE decoding states step-by-step)
  uint32_t position = 0;
  uint32_t step     = (tableSize >> 1) + (tableSize >> 3) + 3;
  uint32_t mask     = tableSize - 1;

  for (uint32_t s = 0; s < symCount; s++) {
    i16 proba = norm[s];
    if (proba == -1) {
      states[position].symbol = static_cast<uint8_t>(s);
      position                = (position + step) & mask;
      proba                   = 1;
    } else if (proba > 0) {
      for (i16 i = 0; i < proba; i++) {
        states[position].symbol = static_cast<uint8_t>(s);
        position                = (position + step) & mask;
      }
    }
  }

  // Calculate numBits and baseValue for decoding speed
  std::vector<uint32_t> nextState(symCount, 0);
  for (uint32_t s = 0; s < symCount; s++) {
    if (norm[s] > 0) {
      nextState[s] = norm[s];
    } else if (norm[s] == -1) {
      nextState[s] = 1;
    }
  }

  for (uint32_t i = 0; i < tableSize; i++) {
    uint8_t  s          = states[i].symbol;
    uint32_t next       = nextState[s]++;

    uint8_t  nbBits     = static_cast<uint8_t>(accuracyLog - (31 - dakt::Bit::countl_zero(next)));
    states[i].numBits   = nbBits;
    states[i].baseValue = static_cast<uint16_t>((next << nbBits) - tableSize);
  }

  // Total bits consumed from the stream
  size_t bitsConsumed = (byteIdx * 8) - bitsAvailable;
  return (bitsConsumed + 7) / 8; // Round up to nearest byte
}

void FSETable::build_predefined(int tableType) {
  std::vector<i16> norm;
  if (tableType == 0) {          // LL
    static constexpr i16 LL_DEFAULT_WEIGHTS[] = {4, 3, 2, 2, 2, 2, 2, 2, 2,  2,  2,  2,
                                                 2, 1, 1, 1, 2, 2, 2, 2, 2,  2,  2,  2,
                                                 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
    norm.assign(dakt::begin(LL_DEFAULT_WEIGHTS), dakt::end(LL_DEFAULT_WEIGHTS));
    accuracyLog = 6;
  } else if (tableType == 1) { // OF
    static constexpr i16 OF_DEFAULT_WEIGHTS[] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1,  1,  1,  1,  1, 1,
                                                 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
    norm.assign(dakt::begin(OF_DEFAULT_WEIGHTS), dakt::end(OF_DEFAULT_WEIGHTS));
    accuracyLog = 5;
  } else if (tableType == 2) { // ML
    static constexpr i16 ML_DEFAULT_WEIGHTS[] = {
        1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
    norm.assign(dakt::begin(ML_DEFAULT_WEIGHTS), dakt::end(ML_DEFAULT_WEIGHTS));
    accuracyLog = 6;
  } else {
    return;
  }

  uint32_t tableSize = 1 << accuracyLog;
  states.resize(tableSize);

  uint32_t symCount = norm.size();

  // Distribute states across the table
  // (Translates probabilities into FSE decoding states step-by-step)
  uint32_t position = 0;
  uint32_t step     = (tableSize >> 1) + (tableSize >> 3) + 3;
  uint32_t mask     = tableSize - 1;

  for (uint32_t s = 0; s < symCount; s++) {
    i16 proba = norm[s];
    if (proba == -1) {
      states[position].symbol = static_cast<uint8_t>(s);
      position                = (position + step) & mask;
      proba                   = 1;
    } else if (proba > 0) {
      for (i16 i = 0; i < proba; i++) {
        states[position].symbol = static_cast<uint8_t>(s);
        position                = (position + step) & mask;
      }
    }
  }

  // Calculate numBits and baseValue for decoding speed
  std::vector<uint32_t> nextState(symCount, 0);
  for (uint32_t s = 0; s < symCount; s++) {
    if (norm[s] > 0) {
      nextState[s] = norm[s];
    } else if (norm[s] == -1) {
      nextState[s] = 1;
    }
  }

  for (uint32_t i = 0; i < tableSize; i++) {
    uint8_t  s          = states[i].symbol;
    uint32_t next       = nextState[s]++;

    uint8_t  nbBits     = static_cast<uint8_t>(accuracyLog - (31 - __builtin_clz(next)));
    states[i].numBits   = nbBits;
    states[i].baseValue = static_cast<uint16_t>((next << nbBits) - tableSize);
  }
}

void FSETable::build_rle(uint8_t symbol) {
  accuracyLog = 0;
  states.resize(1);
  states[0].symbol    = symbol;
  states[0].numBits   = 0;
  states[0].baseValue = 0;
}

void FSETable::initialize_state(BitStreamReader& bits) {
  // The initial state is read from the bitstream using the accuracyLog number
  // of bits.
  if (accuracyLog > 0) {
    currentState = static_cast<uint16_t>(bits.getBits(accuracyLog));
    bits.consumeBits(accuracyLog);
  } else {
    currentState = 0;
  }
}

uint32_t FSETable::decode_symbol(BitStreamReader& bits) {
  if (states.empty() || currentState >= states.size()) {
    return 0;
  }

  // 1. Retrieve the symbol mapped to the current state
  const FSEDecodeState& dState = states[currentState];
  uint8_t               symbol = dState.symbol;

  // 2. Fetch the required bits from the stream to calculate the next state
  uint16_t rest                = 0;
  if (dState.numBits > 0) {
    rest = static_cast<uint16_t>(bits.getBits(dState.numBits));
    bits.consumeBits(dState.numBits);
  }

  // 3. Transition the state machine
  currentState = dState.baseValue + rest;

  // 4. Return the decoded symbol to the sequence execution loop
  return symbol;
}

size_t HuffmanTree::build_from_weights(const uint8_t* data, size_t size) {
  // Generate decoding table based on RFC 8478 weights extraction
  std::vector<uint8_t> weights(data, data + size);

  uint32_t             weightTotal = 0;
  for (uint8_t w : weights) {
    if (w > 0) {
      weightTotal += (1 << (w - 1));
    }
  }

  tableLog = 0;
  while ((1ULL << tableLog) < weightTotal) {
    tableLog++;
  }

  if (tableLog > 11) { // RFC 8478 specifies max Huffman bits is 11
    return 0;
  }

  uint32_t rankCount[12] = {0};
  for (uint8_t w : weights) {
    if (w > 0) {
      rankCount[tableLog + 1 - w]++;
    }
  }

  uint32_t rankIdx[12]   = {0};
  uint32_t nextRankStart = 0;
  for (uint32_t i = 1; i <= tableLog; i++) {
    rankIdx[i]     = nextRankStart;
    nextRankStart += (rankCount[i] << (tableLog - i));
  }

  table.resize(1 << tableLog);
  for (size_t sym = 0; sym < weights.size(); sym++) {
    uint8_t w = weights[sym];
    if (w == 0) {
      continue;
    }

    uint8_t  numBits = tableLog + 1 - w;
    uint32_t base    = rankIdx[numBits];
    uint32_t step    = 1 << (tableLog - numBits);

    for (uint32_t i = 0; i < step; ++i) {
      table[base + i].symbol  = static_cast<uint8_t>(sym);
      table[base + i].numBits = numBits;
    }
    rankIdx[numBits] += step;
  }

  return size;
}

uint8_t HuffmanTree::decode_symbol(BitStreamReader& bits) {
  if (table.empty()) {
    return 0;
  }

  // Huffman symbols in Zstd are peeked up to the max TableLog, then advanced
  // exactly numBits
  uint16_t stateVal = static_cast<uint16_t>(bits.getBits(tableLog));

  auto     entry    = table[stateVal];
  bits.consumeBits(entry.numBits);

  return entry.symbol;
}

bool decode_compressed_block(const uint8_t* src, size_t block_size, std::vector<uint8_t>& output,
                             HuffmanTree& hTree, FSETable& ll_table, FSETable& of_table,
                             FSETable& ml_table) {
  const uint8_t* src_end = src + block_size;

  // --- Phase A: Decode Literals Section ---
  if (src >= src_end) {
    return false;
  }
  uint8_t              lit_header     = *src++;
  uint8_t              lit_block_type = lit_header & 3;

  uint32_t             lit_size       = 0;
  std::vector<uint8_t> literals;

  if (lit_block_type == 0 || lit_block_type == 1) {
    // Raw or RLE
    // Simplify sizes
    lit_size = lit_header >> 3;
    if (lit_block_type == 0) {
      literals.assign(src, src + lit_size);
      src += lit_size;
    } else {
      literals.assign(lit_size, *src++);
    }
  } else if (lit_block_type == 2 || lit_block_type == 3) {
    // Huffman Compressed (Type 2) or Treeless (Type 3)
    // Read header size (simplified 1-byte header for structure)
    lit_size                 = lit_header >> 4;
    uint32_t compressed_size = *src++;

    if (src + compressed_size > src_end) {
      return false;
    }

    // Build the Huffman Tree from the first few bytes (the weight distribution)
    if (lit_block_type == 2) {
      hTree.build_from_weights(src, compressed_size);
    }

    // Decode the literals
    BitStreamReader hBits(src, compressed_size);
    for (uint32_t i = 0; i < lit_size; ++i) {
      literals.push_back(hTree.decode_symbol(hBits));
    }
    src += compressed_size;
  } else {
    return false;
  }

  // --- Phase B: Decode Sequences Section ---
  if (src >= src_end) {
    return false;
  }

  uint32_t num_sequences = *src++;
  if (num_sequences == 0) {
    output.insert(output.end(), literals.begin(), literals.end());
    return true;
  }
  if (num_sequences >= 128 && num_sequences < 255) {
    num_sequences = ((num_sequences - 128) << 8) + *src++;
  }
  if (num_sequences == 255) {
    uint32_t low  = *src++;
    uint32_t high = *src++;
    num_sequences = low + (high << 8) + 0x7F00;
  }

  // Sequence FSE Tables
  uint8_t symbol_compression_modes = *src++;

  uint8_t ll_mode                  = (symbol_compression_modes >> 6) & 3;
  uint8_t of_mode                  = (symbol_compression_modes >> 4) & 3;
  uint8_t ml_mode                  = (symbol_compression_modes >> 2) & 3;

  auto    build_table              = [&](uint8_t mode, int tableType, FSETable& table) -> bool {
    if (mode == 0) {
      // Predefined_Mode: use hardcoded RFC probability distributions
      table.build_predefined(tableType);
    } else if (mode == 1) {
      // RLE_Mode: Single repeating sequence layout
      if (src >= src_end) {
        return false;
      }
      uint8_t rle_symbol = *src++;
      table.build_rle(rle_symbol);
    } else if (mode == 2) {
      // FSE_Compressed_Mode: Read customized table from stream
      // build_from_weights now properly returns exactly how many bytes were
      // consumed!
      size_t bytes_consumed  = table.build_from_weights(src, src_end - src);

      // Advance src by bytes consumed
      src                   += bytes_consumed;
    } else if (mode == 3) {
      // Repeat_Mode: Reuses the FSE table from the previous block.
      // Since ll_table, of_table, ml_table are now passed by reference from
      // Zdakt::decompress(), they automatically persist across blocks!
    }
    return true;
  };

  if (!build_table(ll_mode, 0, ll_table)) {
    return false;
  }
  if (!build_table(of_mode, 1, of_table)) {
    return false;
  }
  if (!build_table(ml_mode, 2, ml_table)) {
    return false;
  }

  // C. Sequence Execution Loop
  BitStreamReader bits(src, src_end - src);

  ll_table.initialize_state(bits);
  of_table.initialize_state(bits);
  ml_table.initialize_state(bits);

  size_t lit_ptr = 0;

  for (uint32_t i = 0; i < num_sequences; i++) {
    // 1. Decode FSE symbols to get states
    uint32_t ll_code                  = ll_table.decode_symbol(bits);
    uint32_t ml_code                  = ml_table.decode_symbol(bits);
    uint32_t of_code                  = of_table.decode_symbol(bits);

    // 2. Map FSE symbols to base values and read extra bits

    // --- Literal Length ---
    static const uint8_t  LL_BITS[36] = {0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,
                                         0, 0, 0, 0, 1, 1,  1,  1,  2,  2,  3,  3,
                                         4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static const uint32_t LL_BASE[36] = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,   9,   10,  11,   12,   13,   14,   15,    16,    18,
        20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096, 9192, 16384, 32768, 65536};

    uint32_t lit_length = LL_BASE[ll_code];
    if (LL_BITS[ll_code] > 0) {
      lit_length += bits.getBits(LL_BITS[ll_code]);
      bits.consumeBits(LL_BITS[ll_code]);
    }

    // --- Match Length ---
    static const uint8_t  ML_BITS[53] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  1,  1,  1, 1,
                                         2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static const uint32_t ML_BASE[53] = {
        3,  4,  5,  6,  7,  8,  9,  10,  11,  12,  13,   14,   15,   16,   17,    18,    19,   20,
        21, 22, 23, 24, 25, 26, 27, 28,  29,  30,  31,   32,   33,   34,   35,    37,    39,   41,
        43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};

    uint32_t match_length = ML_BASE[ml_code];
    if (ML_BITS[ml_code] > 0) {
      match_length += bits.getBits(ML_BITS[ml_code]);
      bits.consumeBits(ML_BITS[ml_code]);
    }

    // --- Offset ---
    uint32_t offset = 1;
    if (of_code > 0) {
      // For offset, the symbol code represents the exact number of extra bits!
      // The base value is `1 << of_code`, and we read `of_code` extra bits.
      offset = (1 << of_code) + bits.getBits(of_code);
      bits.consumeBits(of_code);
    }

    // 3. Execute Literal Copy
    if (lit_ptr + lit_length > literals.size()) {
      return false;
    }
    output.insert(output.end(), literals.begin() + lit_ptr,
                  literals.begin() + lit_ptr + lit_length);
    lit_ptr += lit_length;

    // 4. Execute Match Copy (sliding window)
    if (offset == 0 || offset > output.size()) {
      return false;
    }

    size_t match_start = output.size() - offset;
    for (size_t m = 0; m < match_length; ++m) {
      output.push_back(output[match_start + m]); // byte-by-byte for overlapping matches
    }
  }

  // Copy any remaining literals
  if (lit_ptr < literals.size()) {
    output.insert(output.end(), literals.begin() + lit_ptr, literals.end());
  }

  return true;
}

// Parses the RFC 8478 Frame Header
bool parse_frame_header(const uint8_t*& src, const uint8_t* src_end, ZstdFrameHeader& header) {
  if (src + 1 > src_end) {
    return false;
  }

  uint8_t fhd               = *src++;
  header.single_segment     = (fhd >> 5) & 1;
  uint8_t fcs_field_size    = fhd >> 6;
  uint8_t window_descriptor = 0;

  if (!header.single_segment) {
    if (src + 1 > src_end) {
      return false;
    }

    window_descriptor  = *src++;
    uint64_t mantissa  = window_descriptor & 7;
    uint64_t exponent  = window_descriptor >> 3;
    header.window_size = (1ULL << (10 + exponent)) + (mantissa << (7 + exponent));
  }

  // Dictionary ID (Skipped for brevity, assume 0 for standard game chunks)
  if ((fhd & 3) != 0) {
    uint8_t dict_size  = (fhd & 3) == 1 ? 1 : ((fhd & 3) == 2 ? 2 : 4);
    src               += dict_size;
  }

  // Frame Content Size
  if (fcs_field_size == 0) {
    if (header.single_segment) {
      if (src + 1 > src_end) {
        return false;
      }

      header.frame_content_size = *src++;
    }
  } else if (fcs_field_size == 1) {
    if (src + 2 > src_end) {
      return false;
    }

    header.frame_content_size  = src[0] | (src[1] << 8);
    header.frame_content_size += 256;
    src                       += 2;
  } else if (fcs_field_size == 2) {
    if (src + 4 > src_end) {
      return false;
    }

    header.frame_content_size  = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    src                       += 4;
  } else {
    if (src + 8 > src_end) {
      return false;
    }
    // 8-byte size parsing
    src += 8;
  }

  return true;
}

} // namespace Detail

using namespace Detail;

CompressionMethod Zdakt::method() const noexcept { return CompressionMethod::Zstd; }

// --- 4. Main Decompression Entry ---
bool Zdakt::decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const {
  if (input.size() < 4) {
    return false;
  }

  const uint8_t*       src      = input.data();
  const uint8_t* const src_end  = src + input.size();

  // 1. Magic Number Check
  uint32_t magic                = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
  src                          += 4;

  // Check if ti's a skippable frame (0x184D2A50 to 0x184D2A5F)
  if ((magic & 0xFFFFFFF0) == 0x184D2A50) {
    if (src + 4 > src_end) {
      return false;
    }
    uint32_t skip_size  = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    src                += 4;
    if (src + skip_size > src_end) {
      return false;
    }
    src += skip_size;
    return true;
  }

  if (magic != 0xFD2FB528) {
    return false; // Standard Zstd Magic
  }

  // 2. Decode Frame Header
  ZstdFrameHeader frame;
  if (!parse_frame_header(src, src_end, frame)) {
    return false;
  }

  if (frame.frame_content_size > 0 && frame.frame_content_size < 1024 * 1024 * 500) {
    output.reserve(output.size() + frame.frame_content_size);
  }

  // 3. Block Parsing Loop
  bool        last_block = false;
  HuffmanTree hTree;
  FSETable    ll_table;
  FSETable    of_table;
  FSETable    ml_table;
  while (!last_block && src < src_end) {
    if (src + 3 > src_end) {
      return false;
    }

    uint32_t block_header  = src[0] | (src[1] << 8) | (src[2] << 16);
    src                   += 3;

    last_block             = block_header & 1;
    uint8_t  block_type    = (block_header >> 1) & 3;
    uint32_t block_size    = block_header >> 3;

    if (src + (block_type == 1 ? 1 : block_size) > src_end) {
      return false;
    }

    switch (block_type) {
    case 0: // Raw Block
      output.insert(output.end(), src, src + block_size);
      src += block_size;
      break;

    case 1: // RLE Block (Run-Length Encoding)
    {
      uint8_t byte = *src++;
      output.insert(output.end(), block_size, byte);
      break;
    }

    case 2: // Compressed Block
    {
      if (!decode_compressed_block(src, block_size, output, hTree, ll_table, of_table, ml_table)) {
        return false;
      }
      src += block_size;
      break;
    }

    case 3: // Reserved (Invalid)
      return false;
    }
  }

  // Optional Checksum (last 4 bytes) ignored for simplicity
  return true;
}

} // namespace dakt::decrypt