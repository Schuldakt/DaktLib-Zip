/// @file zstd.hpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-26

#pragma once
#include <icompressor.hpp>

namespace dakt::decrypt {

namespace Detail {

// --- 1. Bitstream Reader ---
// Zstd bitstreams are read backwards from the end of the sequence section.
class BitStreamReader {
public:
  BitStreamReader(const uint8_t* start, size_t size);

  uint64_t getBits(uint8_t bits) const;
  void     consumeBits(uint8_t bits);

private:
  void           fill();

  const uint8_t* m_ptr;
  const uint8_t* m_start;
  uint64_t       m_bitContainer;
  uint8_t        m_bitsConsumed;
};

// --- 2. Decoder State Tables ---
struct FSEDecodeState {
  uint8_t  symbol;
  uint8_t  numBits;
  uint16_t baseValue;
};

struct FSETable {
  uint8_t                     accuracyLog;
  std::vector<FSEDecodeState> states;

  uint16_t                    currentState = 0;

  size_t                      build_from_weights(const uint8_t* data, size_t size);
  void                        build_predefined(int tableType);
  void                        build_rle(uint8_t symbol);

  void                        initialize_state(BitStreamReader& bits);

  uint32_t                    decode_symbol(BitStreamReader& bits);
};

struct HuffmanTableEntry {
  uint8_t symbol;
  uint8_t numBits;
};

struct HuffmanTree {
  uint32_t                       tableLog = 0;
  std::vector<HuffmanTableEntry> table;

  size_t                         build_from_weights(const uint8_t* data, size_t size);
  uint8_t                        decode_symbol(BitStreamReader& bits);
};

// --- 3. Zstd Frame Header ---
struct ZstdFrameHeader {
  uint64_t window_size        = 0;
  uint64_t dict_id            = 0;
  uint64_t frame_content_size = 0;
  bool     single_segment     = false;
};

// --- 4. Internal Functions ---
bool parse_frame_header(const uint8_t*& src, const uint8_t* src_end, ZstdFrameHeader& header);
bool decode_compressed_block(const uint8_t* src, size_t block_size, std::vector<uint8_t>& output,
                             HuffmanTree& hTree);

} // namespace Detail

class Zstd : public compression::ICompressor {
public:
  [[nodiscard]] std::string_view               name() const noexcept override;

  [[nodiscard]] compression::CompressionMethod method() const noexcept override;

  // Core decompression pipeline identifying Zstd frames, blocks, and triggering the decode
  // blocks.
  bool decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const override;
};

} // namespace dakt::decrypt