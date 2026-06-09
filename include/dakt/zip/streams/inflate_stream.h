/// @file inflate_stream.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_STREAMS_INFLATE_STREAM_H
#define DAKTLIB_STREAMS_INFLATE_STREAM_H

#include <capi/stream.h>
#include <compressor>

#include <algorithm>
#include <cstring>
#include <vector>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

class InflateStream {
    DataStream           m_source;
    ICompressor*         m_algorithm;

    dakt::vector<uint8t> m_compressed_buffer;
    dakt::vector<uint8t> m_inflated_buffer;

    usize                m_inflated_cursor = 0;
    bool                 m_eof             = false;

  public:
    InflateStream(DataStream source, ICompressor* algo)
        : m_source(source)
        , m_algorithm(algo) {
      m_compressed_buffer.resize(16384); // 16KB Read Window
      m_inflated_buffer.reserve(65536);  // 64KB Inflate Window
    }

    ~InflateStream() {
      if (m_source.close != nullptr) { m_source.close(m_source.context); }
    }

    auto read(dakt::span<uint8t> outBuffer) -> usize {
      usize bytes_fulfilled = 0;

      while (bytes_fulfilled < outBuffer.size() && !m_eof) {
        usize available = m_inflated_buffer.size() - m_inflated_cursor;

        // 1. Drain any existing inflated ata
        if (available > 0) {
          usize to_copy = dakt::min(available, outBuffer.size() - bytes_fulfilled);
          dakt::memcpy(outBuffer.data() + bytes_fulfilled, m_inflated_buffer.data() + m_inflated_cursor, to_copy);

          m_inflated_cursor += to_copy;
          bytes_fulfilled   += to_copy;
          continue;
        }

        // 2. Need more data: Read raw bytes from the underlying VFS stream
        usize raw_read = m_source.read(m_source.context, m_compressed_buffer.data(), m_compressed_buffer.size());

        if (raw_read == 0) {
          m_eof = true;
          break;
        }

        // 3. Push the chunk through your production Zstd algorithm
        m_inflated_buffer.clear();
        m_inflated_cursor = 0;
        m_algorithm->inflateChunk(dakt::span<uint8t>(m_compressed_buffer.data(), raw_read), m_inflated_buffer);
      }

      return bytes_fulfilled;
    }

    auto seek(int64t offset, int origin) -> int64t {
      return -1; /* Compressed streams generally don't support raw seeking without resetting */
    }
    [[nodiscard]] auto isEof() const -> bool { return m_eof && (m_inflated_cursor >= m_inflated_buffer.size()); }
};

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_STREAMS_INFLATE_STREAM_H