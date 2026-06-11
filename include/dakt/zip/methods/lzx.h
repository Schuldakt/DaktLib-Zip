/// @file lzx.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_LZX_H
#define DAKTLIB_METHODS_LZX_H

#include <config>

#include <zip/utilities/compressor.h>

#include <array>
#include <span>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

namespace detail {

// 1. LZX 16-bit Word-Aligned BitStream Reader
// LZX reads 16-bit words in Little-Endian, but consumes bits from MSB to LSB.
class BitReader {
    dakt::span<const uint8t> m_data;
    usize                    m_byte_cursor    = 0;
    uint32t                  m_bit_buffer     = 0;
    int32t                   m_bits_available = 0;

  public:
    explicit BitReader(dakt::span<const uint8t> data)
        : m_data(data) {
      // Read first two 16-bit words to prime the 32-bit buffer
      primeWord();
      primeWord();
    }

    auto readBits(int32t count) -> uint32t {
      while (m_bits_available < count) { primeWord(); }
      uint32t result    = (m_bit_buffer >> (m_bits_available - count)) & ((1U << count) - 1);
      m_bits_available -= count;
      return result;
    }

    auto peekBits(int32t count) -> uint32t {
      while (m_bits_available < count) { primeWord(); }
      return (m_bit_buffer >> (m_bits_available - count)) & ((1U << count) - 1);
    }

    void dropBits(int32t count) { m_bits_available -= count; }

    void alignTo16BitBoundary() {
      uint32t overflow = m_bits_available % 16;
      if (overflow > 0) { m_bits_available -= overflow; }
    }

  private:
    void primeWord() {
      if (m_byte_cursor + 1 < m_data.size()) {
        uint32t word =
          static_cast<uint32t>(m_data[m_byte_cursor]) | (static_cast<uint32t>(m_data[m_byte_cursor + 1]) << 8);
        m_bit_buffer      = (m_bit_buffer << 16) | word;
        m_bits_available += 16;
        m_byte_cursor    += 2;
      } else {
        // Pad with zeros at EOF to prevent crashes on slight over-reads
        m_bit_buffer     <<= 16;
        m_bits_available  += 16;
      }
    }
};

// 2. Canonical Huffman Decoder Table
// LZX uses maximum 16-bit code lengths. We use a fast lookup array.
template <usize MaxElements, usize MaxBits> class HuffmanTable {
    dakt::array<int16t, 1 << MaxBits> m_table;
    dakt::array<uint8t, MaxElements>  m_lengths;

  public:
    HuffmanTable() {
      m_table.fill(-1);
      m_lengths.fill(0);
    }

    auto getLengths() -> dakt::span<uint8t> { return m_lengths; }

    void build() {
      dakt::array<uint16t, 17> length_counts{0};
      dakt::array<uint16t, 17> next_code{0};

      for (usize i = 0; i < MaxElements; ++i) {
        if (*(m_lengths.data() + i) > 0) { (*(length_counts.data() + *(m_lengths.data() + i)))++; }
      }

      uint16t code = 0;
      for (usize i = 1; i <= MaxBits; ++i) {
        code                      = (code + *(length_counts.data() + i - 1)) << 1;
        (*(next_code.data() + i)) = code;
      }

      m_table.fill(-1);
      for (usize i = 0; i < MaxElements; ++i) {
        uint8t len = (*(m_lengths.data() + i));
        if (len > 0) {
          uint16t current_code = (*(next_code.data() + len))++;

          // Fill all variations of the remaining bits for the fast 0(1) lookup
          uint16t shift        = MaxBits - len;
          uint16t start        = current_code << shift;
          uint16t end          = start + (1 << shift);

          for (uint16t j = start; j < end; ++j) { (*(m_table.data() + j)) = static_cast<int16t>(i); }
        }
      }
    }

    auto decode(BitReader& reader) const -> int16t {
      uint32t peek   = reader.peekBits(MaxBits);
      int16t  symbol = (*(m_table.data() + peek));
      if (symbol != -1) { reader.dropBits(*(m_lengths.data() + symbol)); }
      return symbol;
    }
};

} // namespace detail

class Lzx : public ICompressor {
  public:
    [[nodiscard]] auto name() const noexcept -> dakt::string_view override;

    [[nodiscard]] auto method() const noexcept -> CompressionMethod override;

    auto inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize override;

    auto deflateChunk(dakt::span<const uint8t> rawData, dakt::vector<uint8t>& outputBuffer) -> usize override;
};

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_LZX_H