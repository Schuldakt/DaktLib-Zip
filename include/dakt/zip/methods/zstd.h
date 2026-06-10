/// @file zstd.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_ZSTD_H
#define DAKTLIB_METHODS_ZSTD_H

#include <config>

#include <compressor>
#include <zip/registrar/compression_registry.h>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

class Zstd : public ICompressor {
  public:
    [[nodiscard]] auto name() const noexcept -> dakt::string_view override;

    [[nodiscard]] auto method() const noexcept -> CompressionMethod override;

    auto inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize override;

    auto deflateChunk(dakt::span<const uint8t> rawData, dakt::vector<uint8t>& outputBuffer) -> usize override;
};

namespace detail {

// --- 1. Bitstream Reader ---
// Zstd bitstreams are read backwards from the end of the sequence section.
class BitStreamReader {
  public:
    BitStreamReader(const uint8t* start, usize size);

    [[nodiscard]] auto getBits(uint8t bits) const -> uint64t;
    void               consumeBits(uint8t bits);

  private:
    void          fill();

    const uint8t* m_ptr;
    const uint8t* m_start;
    uint64t       m_bit_container;
    uint8t        m_bits_consumed;
};

// --- 2. Decoder State Tables ---
struct FSEDecodeState {
    uint8t   symbol;
    uint8t   num_bits;
    uint16_t base_value;
};

struct FSETable {
    uint8t                       accuracy_log = 0;
    dakt::vector<FSEDecodeState> states;

    uint16_t                     current_state = 0;

    auto                         buildFromWeights(const uint8t* data, usize size) -> usize;
    void                         buildPredefined(int tableType);
    void                         buildRle(uint8t symbol);

    void                         initializeState(BitStreamReader& bits);

    auto                         decodeSymbol(BitStreamReader& bits) -> uint32_t;
};

struct HuffmanTableEntry {
    uint8t symbol;
    uint8t num_bits;
};

struct HuffmanTree {
    uint32_t                        table_log = 0;
    dakt::vector<HuffmanTableEntry> table;

    auto                            buildFromWeights(const uint8t* data, usize size) -> usize;
    auto                            decodeSymbol(BitStreamReader& bits) -> uint8t;
};

// --- 3. Zstd Frame Header ---
struct ZstdFrameHeader {
    uint64t window_size        = 0;
    uint64t dict_id            = 0;
    uint64t frame_content_size = 0;
    bool    single_segment     = false;
};

// --- 4. Internal Functions ---
auto parseFrameHeader(const uint8t*& src, const uint8t* srcEnd, ZstdFrameHeader& header) -> bool;
auto decodeCompressedBlock(
  const uint8t*         src,
  usize                 blockSize,
  dakt::vector<uint8t>& output,
  HuffmanTree&          hTree,
  FSETable&             llTable,
  FSETable&             ofTable,
  FSETable&             mlTable
) -> bool;

} // namespace detail

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_ZSTD_H