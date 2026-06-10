/// @file zstd.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_ZSTD_CXX
#define DAKTLIB_METHODS_ZSTD_CXX

#include <config>

#include <zip/methods/zstd.h>
#include <zip/registrar/compression_registry.h>

DAKTLIB_BEGIN_NAMESPACE_ZIP

// --- 1. Bitstream Reader ---
namespace detail {

BitStreamReader::BitStreamReader(const uint8t* start, usize size)
    : m_ptr(start + size)
    , m_start(start)
    , m_bit_container(0)
    , m_bits_consumed(64) { // Start fully consumed so first getBits triggers a fill
  if (size > 0) {
    // The sentinel byte (last byte of stream) encodes how many padding bits
    // are at the top. Find the highest set bit to locate the actual stream start.
    uint8t last_byte = *(m_ptr - 1);
    int    padding   = 7 - (63 - __builtin_clzll(static_cast<uint64t>(last_byte)));
    m_bits_consumed  = static_cast<uint8t>(padding);
    fill();
  }
}

auto BitStreamReader::getBits(uint8t bits) const -> uint64t {
  return (m_bit_container >> (64 - m_bits_consumed)) & ((1ULL << bits) - 1);
}

void BitStreamReader::consumeBits(uint8t bits) {
  m_bits_consumed += bits;
  if (m_bits_consumed >= 32) { fill(); }
}

void BitStreamReader::fill() {
  while (m_bits_consumed >= 8 && m_ptr > m_start) {
    --m_ptr;
    m_bit_container  = (m_bit_container >> 8) | (static_cast<uint64t>(*m_ptr) << 56);
    m_bits_consumed -= 8;
  }
}

// --- 2. Decoder State Tables ---
auto detail::FSETable::buildFromWeights(const uint8t* data, usize size) -> usize {
  if (size == 0) { return 0; }

  BitStreamReader bits(data, size);

  uint64_t        bit_container  = 0;
  uint32t         bits_available = 0;
  usize           byte_idx       = 0;

  auto            fill_bits      = [&]() -> void {
    while (bits_available <= 56 && byte_idx < size) {
      bit_container  |= static_cast<uint64_t>(data[byte_idx++]) << bits_available;
      bits_available += 8;
    }
  };

  fill_bits();

  // Read AccuracyLog (min 5, max 9 for literal lengths, max 8 for match
  // lengths/offsets)
  accuracy_log         = (bit_container & 15) + 5;
  bit_container      >>= 4;
  bits_available      -= 4;

  uint32t table_size   = 1 << accuracy_log;
  states.resize(table_size);

  dakt::vector<int16t> norm(256, 0); // Normalized frequencies
  uint32t              remaining = table_size;
  uint32t              sym_count = 0;

  // Read normalized frequencies (probabilities)
  while (remaining > 0 && sym_count < 256) {
    // Calculate max_bits for current remaining value
    uint8t  max_bits = 0;
    uint32t temp     = remaining + 1;
    while (temp > 1) {
      max_bits++;
      temp >>= 1;
    }

    fill_bits();
    if (bits_available < max_bits) {
      break; // End of properly formed stream
    }

    // Fetch value
    uint16t val      = bit_container & ((1 << max_bits) - 1);
    bit_container  >>= max_bits;
    bits_available  -= max_bits;

    if (val < 15) {
      int16t proba      = val - 1;
      norm[sym_count++] = proba;

      if (proba != -1) {
        remaining -= proba;
      } else {
        remaining -= 1; // -1 represents probability of 1 in the remaining pool
      }
    } else {
      // val >= 15 indicates a sequence of (val - 14) symbols with probability 0
      // The dakt::vector norm defaults to 0, so we just advance the symCount
      // index
      int repeat  = val - 14;
      sym_count  += repeat;
    }
  }

  // Distribute states across the table
  // (Translates probabilities into FSE decoding states step-by-step)
  uint32t position = 0;
  uint32t step     = (table_size >> 1) + (table_size >> 3) + 3;
  uint32t mask     = table_size - 1;

  for (uint32t s = 0; s < sym_count; s++) {
    int16t proba = norm[s];
    if (proba == -1) {
      states[position].symbol = static_cast<uint8t>(s);
      position                = (position + step) & mask;
      proba                   = 1;
    } else if (proba > 0) {
      for (int16t i = 0; i < proba; i++) {
        states[position].symbol = static_cast<uint8t>(s);
        position                = (position + step) & mask;
      }
    }
  }

  // Calculate num_bits and base_value for decoding speed
  dakt::vector<uint32t> next_state(sym_count, 0);
  for (uint32t s = 0; s < sym_count; s++) {
    if (norm[s] > 0) {
      next_state[s] = norm[s];
    } else if (norm[s] == -1) {
      next_state[s] = 1;
    }
  }

  for (uint32t i = 0; i < table_size; i++) {
    uint8t  s            = states[i].symbol;
    uint32t next         = next_state[s]++;

    auto    nb_bits      = static_cast<uint8t>(accuracy_log - (31 - dakt::countl_zero(next)));
    states[i].num_bits   = nb_bits;
    states[i].base_value = static_cast<uint16t>((next << nb_bits) - table_size);
  }

  // Total bits consumed from the stream
  usize bits_consumed = (byte_idx * 8) - bits_available;
  return (bits_consumed + 7) / 8; // Round up to nearest byte
}

void FSETable::buildPredefined(int tableType) {
  dakt::vector<int16t> norm;
  if (tableType == 0) { // LL
    static constexpr dakt::array<int16t, 36> LL_DEFAULT_WEIGHTS = {4, 3, 2, 2, 2, 2, 2, 2, 2,  2,  2,  2,
                                                                   2, 1, 1, 1, 2, 2, 2, 2, 2,  2,  2,  2,
                                                                   2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
    norm.assign(dakt::begin(LL_DEFAULT_WEIGHTS), dakt::end(LL_DEFAULT_WEIGHTS));
    accuracy_log = 6;
  } else if (tableType == 1) { // OF
    static constexpr dakt::array<int16t, 29> OF_DEFAULT_WEIGHTS = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1,  1,  1,  1,  1, 1,
                                                                   1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
    norm.assign(dakt::begin(OF_DEFAULT_WEIGHTS), dakt::end(OF_DEFAULT_WEIGHTS));
    accuracy_log = 5;
  } else if (tableType == 2) { // ML
    static constexpr dakt::array<int16t, 53> ML_DEFAULT_WEIGHTS = {1, 4, 3, 2, 2,  2,  2,  2,  2,  1,  1, 1, 1, 1,
                                                                   1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1, 1, 1,
                                                                   1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1, 1, 1,
                                                                   1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
    norm.assign(dakt::begin(ML_DEFAULT_WEIGHTS), dakt::end(ML_DEFAULT_WEIGHTS));
    accuracy_log = 6;
  } else {
    return;
  }

  uint32t table_size = 1 << accuracy_log;
  states.resize(table_size);

  uint32t sym_count = norm.size();

  // Distribute states across the table
  // (Translates probabilities into FSE decoding states step-by-step)
  uint32t position  = 0;
  uint32t step      = (table_size >> 1) + (table_size >> 3) + 3;
  uint32t mask      = table_size - 1;

  for (uint32t s = 0; s < sym_count; s++) {
    int16t proba = norm[s];
    if (proba == -1) {
      states[position].symbol = static_cast<uint8t>(s);
      position                = (position + step) & mask;
      proba                   = 1;
    } else if (proba > 0) {
      for (int16t i = 0; i < proba; i++) {
        states[position].symbol = static_cast<uint8t>(s);
        position                = (position + step) & mask;
      }
    }
  }

  // Calculate num_bits and base_value for decoding speed
  dakt::vector<uint32t> next_state(sym_count, 0);
  for (uint32t s = 0; s < sym_count; s++) {
    if (norm[s] > 0) {
      next_state[s] = norm[s];
    } else if (norm[s] == -1) {
      next_state[s] = 1;
    }
  }

  for (uint32t i = 0; i < table_size; i++) {
    uint8t  s            = states[i].symbol;
    uint32t next         = next_state[s]++;

    auto    nb_bits      = static_cast<uint8t>(accuracy_log - (31 - __builtin_clz(next)));
    states[i].num_bits   = nb_bits;
    states[i].base_value = static_cast<uint16t>((next << nb_bits) - table_size);
  }
}

void FSETable::buildRle(uint8t symbol) {
  accuracy_log = 0;
  states.resize(1);
  states[0].symbol     = symbol;
  states[0].num_bits   = 0;
  states[0].base_value = 0;
}

void FSETable::initializeState(BitStreamReader& bits) {
  // The initial state is read from the bitstream using the accuracy_log number
  // of bits.
  if (accuracy_log > 0) {
    current_state = static_cast<uint16t>(bits.getBits(accuracy_log));
    bits.consumeBits(accuracy_log);
  } else {
    current_state = 0;
  }
}

auto FSETable::decodeSymbol(BitStreamReader& bits) -> uint32t {
  if (states.empty() || current_state >= states.size()) { return 0; }

  // 1. Retrieve the symbol mapped to the current state
  const FSEDecodeState& d_state = states[current_state];
  uint8t                symbol  = d_state.symbol;

  // 2. Fetch the required bits from the stream to calculate the next state
  uint16t rest                  = 0;
  if (d_state.num_bits > 0) {
    rest = static_cast<uint16t>(bits.getBits(d_state.num_bits));
    bits.consumeBits(d_state.num_bits);
  }

  // 3. Transition the state machine
  current_state = d_state.base_value + rest;

  // 4. Return the decoded symbol to the sequence execution loop
  return symbol;
}

auto HuffmanTree::buildFromWeights(const uint8t* data, usize size) -> usize {
  // Generate decoding table based on RFC 8478 weights extraction
  dakt::vector<uint8t> weights(data, data + size);

  uint32t              weight_total = 0;
  for (uint8t w : weights) {
    if (w > 0) { weight_total += (1 << (w - 1)); }
  }

  table_log = 0;
  while ((1ULL << table_log) < weight_total) { table_log++; }

  if (table_log > 11) { // RFC 8478 specifies max Huffman bits is 11
    return 0;
  }

  dakt::array<uint32t, 12> rank_count = {0};
  for (uint8t w : weights) {
    if (w > 0) { *(rank_count.data() + (table_log + 1 - w)) += 1; }
  }

  dakt::array<uint32t, 12> rank_idx        = {0};
  uint32t                  next_rank_start = 0;
  for (uint32t i = 1; i <= table_log; i++) {
    *(rank_idx.data() + i)  = next_rank_start;
    next_rank_start        += (*(rank_count.data() + i) << (table_log - i));
  }

  table.resize(1 << table_log);
  for (usize sym = 0; sym < weights.size(); sym++) {
    uint8t w = weights[sym];
    if (w == 0) { continue; }

    uint8t  num_bits = table_log + 1 - w;
    uint32t base     = *(rank_idx.data() + num_bits);
    uint32t step     = 1 << (table_log - num_bits);

    for (uint32t i = 0; i < step; ++i) {
      table[base + i].symbol   = static_cast<uint8t>(sym);
      table[base + i].num_bits = num_bits;
    }
    *(rank_idx.data() + num_bits) += step;
  }

  return size;
}

auto HuffmanTree::decodeSymbol(BitStreamReader& bits) -> uint8t {
  if (table.empty()) { return 0; }

  // Huffman symbols in Zstd are peeked up to the max TableLog, then advanced
  // exactly num_bits
  auto state_val = static_cast<uint16t>(bits.getBits(table_log));

  auto entry     = table[state_val];
  bits.consumeBits(entry.num_bits);

  return entry.symbol;
}

auto decodeCompressedBlock(
  const uint8t*         src,
  usize                 blockSize,
  dakt::vector<uint8t>& outputBuffer,
  HuffmanTree&          hTree,
  FSETable&             llTable,
  FSETable&             ofTable,
  FSETable&             mlTable
) -> bool {
  const uint8t* src_end = src + blockSize;

  // --- Phase A: Decode Literals Section ---
  if (src >= src_end) { return false; }
  uint8t               lit_header     = *src++;
  uint8t               lit_block_type = lit_header & 3;

  uint32t              lit_size       = 0;
  dakt::vector<uint8t> literals;

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
    lit_size                = lit_header >> 4;
    uint32t compressed_size = *src++;

    if (src + compressed_size > src_end) { return false; }

    // Build the Huffman Tree from the first few bytes (the weight distribution)
    if (lit_block_type == 2) { hTree.buildFromWeights(src, compressed_size); }

    // Decode the literals
    BitStreamReader h_bits(src, compressed_size);
    for (uint32t i = 0; i < lit_size; ++i) { literals.push_back(hTree.decodeSymbol(h_bits)); }
    src += compressed_size;
  } else {
    return false;
  }

  // --- Phase B: Decode Sequences Section ---
  if (src >= src_end) { return false; }

  uint32t num_sequences = *src++;
  if (num_sequences == 0) {
    outputBuffer.insert(outputBuffer.end(), literals.begin(), literals.end());
    return true;
  }
  if (num_sequences >= 128 && num_sequences < 255) { num_sequences = ((num_sequences - 128) << 8) + *src++; }
  if (num_sequences == 255) {
    uint32t low   = *src++;
    uint32t high  = *src++;
    num_sequences = low + (high << 8) + 0x7F00;
  }

  // Sequence FSE Tables
  uint8t symbol_compression_modes = *src++;

  uint8t ll_mode                  = (symbol_compression_modes >> 6) & 3;
  uint8t of_mode                  = (symbol_compression_modes >> 4) & 3;
  uint8t ml_mode                  = (symbol_compression_modes >> 2) & 3;

  auto   build_table              = [&](uint8t mode, int tableType, FSETable& table) -> bool {
    if (mode == 0) {
      // Predefined_Mode: use hardcoded RFC probability distributions
      table.buildPredefined(tableType);
    } else if (mode == 1) {
      // RLE_Mode: Single repeating sequence layout
      if (src >= src_end) { return false; }
      uint8t rle_symbol = *src++;
      table.buildRle(rle_symbol);
    } else if (mode == 2) {
      // FSE_Compressed_Mode: Read customized table from stream
      // build_from_weights now properly returns exactly how many bytes were
      // consumed!
      usize bytes_consumed  = table.buildFromWeights(src, src_end - src);

      // Advance src by bytes consumed
      src                  += bytes_consumed;
    } else if (mode == 3) {
      // Repeat_Mode: Reuses the FSE table from the previous block.
      // Since ll_table, of_table, ml_table are now passed by reference from
      // Zdakt::decompress(), they automatically persist across blocks!
    }
    return true;
  };

  if (!build_table(ll_mode, 0, llTable)) { return false; }
  if (!build_table(of_mode, 1, ofTable)) { return false; }
  if (!build_table(ml_mode, 2, mlTable)) { return false; }

  // C. Sequence Execution Loop
  BitStreamReader bits(src, src_end - src);

  llTable.initializeState(bits);
  ofTable.initializeState(bits);
  mlTable.initializeState(bits);

  usize lit_ptr = 0;

  for (uint32t i = 0; i < num_sequences; i++) {
    // 1. Decode FSE symbols to get states
    uint32t ll_code                                  = llTable.decodeSymbol(bits);
    uint32t ml_code                                  = mlTable.decodeSymbol(bits);
    uint32t of_code                                  = ofTable.decodeSymbol(bits);

    // 2. Map FSE symbols to base values and read extra bits

    // --- Literal Length ---
    static const dakt::array<uint8t, 36>  ll_bits    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  1,  1,
                                                        1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static const dakt::array<uint32t, 36> ll_base    = {0,   1,   2,    3,    4,    5,    6,     7,     8,
                                                        9,   10,  11,   12,   13,   14,   15,    16,    18,
                                                        20,  22,  24,   28,   32,   40,   48,    64,    128,
                                                        256, 512, 1024, 2048, 4096, 9192, 16384, 32768, 65536};

    uint32t                               lit_length = *(ll_base.data() + ll_code);
    if (*(ll_bits.data() + ll_code) > 0) {
      lit_length += bits.getBits(*(ll_bits.data() + ll_code));
      bits.consumeBits(*(ll_bits.data() + ll_code));
    }

    // --- Match Length ---
    static const dakt::array<uint8t, 53>  ml_bits      = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0,
                                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  1,  1,  1, 1,
                                                          2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static const dakt::array<uint32t, 53> ml_base      = {3,   4,   5,    6,    7,    8,    9,     10,    11,   12, 13,
                                                          14,  15,  16,   17,   18,   19,   20,    21,    22,   23, 24,
                                                          25,  26,  27,   28,   29,   30,   31,    32,    33,   34, 35,
                                                          37,  39,  41,   43,   47,   51,   59,    67,    83,   99, 131,
                                                          259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};

    uint32t                               match_length = *(ml_base.data() + ml_code);
    if (*(ml_bits.data() + ml_code) > 0) {
      match_length += bits.getBits(*(ml_bits.data() + ml_code));
      bits.consumeBits(*(ml_bits.data() + ml_code));
    }

    // --- Offset ---
    uint32t offset = 1;
    if (of_code > 0) {
      // For offset, the symbol code represents the exact number of extra bits!
      // The base value is `1 << of_code`, and we read `of_code` extra bits.
      offset = (1 << of_code) + bits.getBits(of_code);
      bits.consumeBits(of_code);
    }

    // 3. Execute Literal Copy
    if (lit_ptr + lit_length > literals.size()) { return false; }
    outputBuffer.insert(outputBuffer.end(), literals.begin() + lit_ptr, literals.begin() + lit_ptr + lit_length);
    lit_ptr += lit_length;

    // 4. Execute Match Copy (sliding window)
    if (offset == 0 || offset > outputBuffer.size()) { return false; }

    outputBuffer.reserve(outputBuffer.size() + match_length);
    const usize match_start = outputBuffer.size() - offset;
    for (usize m = 0; m < match_length; ++m) { outputBuffer.push_back(outputBuffer[match_start + m]); }
  }

  // Copy any remaining literals
  if (lit_ptr < literals.size()) {
    outputBuffer.insert(outputBuffer.end(), literals.begin() + lit_ptr, literals.end());
  }

  return true;
}

// Parses the RFC 8478 Frame Header
auto parseFrameHeader(const uint8t*& src, const uint8t* srcEnd, ZstdFrameHeader& header) -> bool {
  if (src + 1 > srcEnd) { return false; }

  uint8t fhd               = *src++;
  header.single_segment    = (((fhd >> 5) & 1) != 0);
  uint8t fcs_field_size    = fhd >> 6;
  uint8t window_descriptor = 0;

  if (!header.single_segment) {
    if (src + 1 > srcEnd) { return false; }

    window_descriptor  = *src++;
    uint64_t mantissa  = window_descriptor & 7;
    uint64_t exponent  = window_descriptor >> 3;
    header.window_size = (1ULL << (10 + exponent)) + (mantissa << (7 + exponent));
  }

  // Dictionary ID (Skipped for brevity, assume 0 for standard game chunks)
  if ((fhd & 3) != 0) {
    uint8t dict_size  = (fhd & 3) == 1 ? 1 : ((fhd & 3) == 2 ? 2 : 4);
    src              += dict_size;
  }

  // Frame Content Size
  if (fcs_field_size == 0) {
    if (header.single_segment) {
      if (src + 1 > srcEnd) { return false; }

      header.frame_content_size = *src++;
    }
  } else if (fcs_field_size == 1) {
    if (src + 2 > srcEnd) { return false; }

    header.frame_content_size  = src[0] | (src[1] << 8);
    header.frame_content_size += 256;
    src                       += 2;
  } else if (fcs_field_size == 2) {
    if (src + 4 > srcEnd) { return false; }

    header.frame_content_size  = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    src                       += 4;
  } else { // fcs_field_size == 3 -> 8-byte FCS
    if (src + 8 > srcEnd) { return false; }
    // Read lower 4 bytes; upper 4 are only relevant for >4GB frames
    header.frame_content_size  = static_cast<uint64t>(src[0])
                                 | (static_cast<uint64t>(src[1]) << 8)
                                 | (static_cast<uint64t>(src[2]) << 16)
                                 | (static_cast<uint64t>(src[3]) << 24)
                                 | (static_cast<uint64t>(src[4]) << 32)
                                 | (static_cast<uint64t>(src[5]) << 40)
                                 | (static_cast<uint64t>(src[6]) << 48)
                                 | (static_cast<uint64t>(src[7]) << 56);
    src                       += 8;
  }

  return true;
}

} // namespace detail

using namespace detail;

auto Zstd::name() const noexcept -> dakt::string_view {
  return "Zstd"; // Must match toString(CompressionMethod::Zstd) in detector.h
}

auto Zstd::method() const noexcept -> CompressionMethod {
  return CompressionMethod::Zstd;
}

// --- 4. Main Decompression Entry ---
DAKTLIB_API inline auto Zstd::inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer)
  -> usize {
  if (compressedData.empty()) { return 0; }

  const uint8t* src     = compressedData.data();
  const uint8t* src_end = src + compressedData.size();

  // 1. Magic Number & Frame Header Check
  if (src + 4 > src_end) { return 0; }
  uint32t magic = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
  if (magic != 0xFD2FB528) {
    return 0; // Not a standard Zstd frame (might be a skippable frame, handle if necessary)
  }
  src += 4;

  // 2. Decode Frame Header
  ZstdFrameHeader frame;
  if (!parseFrameHeader(src, src_end, frame)) { return 0; }

  if (frame.frame_content_size > 0 && frame.frame_content_size < 1024 * 1024 * 500) {
    outputBuffer.reserve(outputBuffer.size() + frame.frame_content_size);
  }

  // 3. Block Parsing Loop
  bool        last_block = false;
  HuffmanTree h_tree;
  FSETable    ll_table;
  FSETable    of_table;
  FSETable    ml_table;
  while (!last_block && src < src_end) {
    if (src + 3 > src_end) { return 0; }

    uint32t block_header  = src[0] | (src[1] << 8) | (src[2] << 16);
    src                  += 3;

    last_block            = ((block_header & 1) != 0);
    uint8t  block_type    = (block_header >> 1) & 3;
    uint32t block_size    = block_header >> 3;

    if (src + (block_type == 1 ? 1 : block_size) > src_end) { return 0; }

    switch (block_type) {
      case 0: // Raw Block
        outputBuffer.insert(outputBuffer.end(), src, src + block_size);
        src += block_size;
        break;

      case 1: // RLE Block (Run-Length Encoding)
        {
          uint8t byte = *src++;
          outputBuffer.insert(outputBuffer.end(), block_size, byte);
          break;
        }

      case 2: // Compressed Block
        {
          if (!decodeCompressedBlock(src, block_size, outputBuffer, h_tree, ll_table, of_table, ml_table)) { return 0; }
          src += block_size;
          break;
        }

      case 3: // Reserved (Invalid)
        return 0;
    }
  }

  // Optional Checksum (last 4 bytes) ignored for simplicity
  return outputBuffer.size();
}

auto Zstd::deflateChunk(dakt::span<const uint8t> /*rawData*/, dakt::vector<uint8t>& /*outputBuffer*/) -> usize {
  // Zstd encoding requires the full zstd encoder state machine.
  // Stubbed until the P4K write path is needed.
  return 0;
}

[[maybe_unused]] const bool s_zstd_registered = [] -> bool {
  CompressionRegistry::instance().registerModule(dakt::make_unique<Zstd>());
  return true;
}();

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_ZSTD_CXX