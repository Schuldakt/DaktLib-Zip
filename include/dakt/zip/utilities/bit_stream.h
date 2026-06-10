/// @file bit_stream.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-09
#ifndef DAKTLIB_UTILITIES_BIT_STREAM_H
#define DAKTLIB_UTILITIES_BIT_STREAM_H

#include <config>

#include <span>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

class BitStreamReader {
    dakt::span<const uint8t> m_data;
    usize                    m_byte_cursor    = 0;
    uint32t                  m_bit_buffer     = 0;
    uint32t                  m_bits_available = 0;

  public:
    explicit BitStreamReader(dakt::span<const uint8t> data)
        : m_data(data) {}

    // Pull N bits from the stream
    [[nodiscard]] auto readBits(uint32t count) -> uint32t {
      while (m_bits_available < count) {
        if (m_byte_cursor >= m_data.size()) {
          return 0; // EOF
        }
        m_bit_buffer     |= (static_cast<uint32t>(m_data[m_byte_cursor++]) << m_bits_available);
        m_bits_available += 8;
      }

      uint32t result     = m_bit_buffer & ((1U << count) - 1);
      m_bit_buffer     >>= count;
      m_bits_available  -= count;
      return result;
    }
};

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_UTILITIES_BIT_STREAM_H