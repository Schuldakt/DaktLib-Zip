/// @file lzx.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_LZX_CXX
#define DAKTLIB_METHODS_LZX_CXX

#include <config>

#include <zip/methods/lzx.h>
#include <zip/registrar/compression_registry.h>
#include <zip/utilities/bit_stream.h>

#include <cstring>

DAKTLIB_BEGIN_NAMESPACE_ZIP

// Helper to decode the tress using the pre-tree (LZX Spec)
static void
readTreeLengths(detail::BitReader& reader, detail::HuffmanTable<20, 16>& preTree, dakt::span<uint8t> targetLengths) {
  usize i = 0;
  while (i < targetLengths.size()) {
    int16t symbol = preTree.decode(reader);
    if (symbol < 0) {
      return; // Error
    }

    if (symbol <= 16) {
      targetLengths[i++] = static_cast<uint8t>((targetLengths[i] + 17 - symbol) % 17);
    } else if (symbol == 17) {
      usize run = 4 + reader.readBits(4);
      while (run-- > 0 && i < targetLengths.size()) { targetLengths[i++] = 0; }
    } else if (symbol == 18) {
      usize run = 20 + reader.readBits(5);
      while (run-- > 0 && i < targetLengths.size()) { targetLengths[i++] = 0; }
    } else if (symbol == 19) {
      usize  run        = 4 + reader.readBits(1);
      uint8t value      = targetLengths[i - 1]; // Repeat last decoded length
                                                // Decode the new value to repeat
      int16t new_symbol = preTree.decode(reader);
      value             = static_cast<uint8t>((targetLengths[i] + 17 - new_symbol) % 17);
      while (run-- > 0 && i < targetLengths.size()) { targetLengths[i++] = value; }
    }
  }
}

auto Lzx::name() const noexcept -> dakt::string_view {
  return "Lzx"; // Must match toString(CompressionMethod::Zstd) in detector.h
}

auto Lzx::method() const noexcept -> CompressionMethod {
  return CompressionMethod::Lzx;
}

auto Lzx::inflateChunk(dakt::span<const uint8t> compressedData, dakt::vector<uint8t>& outputBuffer) -> usize {
  if (compressedData.empty()) { return 0; }

  // LZX requires a sliding window history (usually 32KB to 2MB depending on the window bits)
  // For standalone chunks, we assume the host has pre-sized the buffer or we grow dynamically.
  detail::BitReader reader(compressedData);
  usize             initial_size = outputBuffer.size();

  // LZX LRU Offset Cache (MUST be initialized to 1, 1, 1 per spec)
  uint32t r0                     = 1;
  uint32t r1                     = 1;
  uint32t r2                     = 1;

  // Huffman Trees
  detail::HuffmanTable<20, 16>  pre_tree;
  detail::HuffmanTable<656, 16> main_tree; // 256 literals + (50 * 8) slots
  detail::HuffmanTable<249, 16> length_tree;
  detail::HuffmanTable<8, 16>   aligned_tree;

  // Extra bits required for position slots (0-50)
  constexpr dakt::array<uint8t, 51>  EXTRA_BITS = {0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,
                                                   7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
                                                   16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17};
  constexpr dakt::array<uint32t, 51> BASE_OFFSETS = {
   0,      1,      2,      3,       4,       6,       8,       12,      16,      24,      32,      48,     64,
   96,     128,    192,    256,     384,     512,     768,     1024,    1536,    2048,    3072,    4096,   6144,
   8192,   12288,  16384,  24576,   32768,   49152,   65536,   98304,   131072,  196608,  262144,  393216, 524288,
   655360, 786432, 917504, 1048576, 1179648, 1310720, 1441792, 1572864, 1703936, 1835008, 1966080, 2097152
  };

  while (true) {
    uint32t block_type = reader.readBits(3);
    uint32t block_size = reader.readBits(24);
    if (block_size == 0) {
      break;                                  // EOF
    }

    if (block_type == 1 || block_type == 2) { // Verbatim or Aligned
                                              // 1. Read Aligned Tree (if applicable)
      if (block_type == 2) {
        auto lengths = aligned_tree.getLengths();
        for (usize i = 0; i < 8; ++i) { lengths[i] = static_cast<uint8t>(reader.readBits(3)); }
        aligned_tree.build();
      }

      // 2. Read Pre-Tree
      auto pre_lengths = pre_tree.getLengths();
      for (usize i = 0; i < 20; ++i) { pre_lengths[i] = static_cast<uint8t>(reader.readBits(4)); }
      pre_tree.build();

      // 3. Read Main Tree & Length Tree
      readTreeLengths(reader, pre_tree, main_tree.getLengths());
      main_tree.build();

      readTreeLengths(reader, pre_tree, length_tree.getLengths());
      length_tree.build();

      // 4. Decode the LZ77 Block Payload
      usize decoded = 0;
      while (decoded < block_size) {
        int16t symbol = main_tree.decode(reader);
        if (symbol < 0) {
          return 0; // Error
        }

        if (symbol < 256) {
          // It's a Literal Byte
          outputBuffer.push_back(static_cast<uint8t>(symbol));
          decoded++;
        } else {
          // IT's a Match (Length + Offset)
          uint32t match_header = symbol - 256;
          uint32t pos_slot     = match_header / 8;
          uint32t match_len    = match_header % 8;

          if (match_len == 7) {
            int16t extra_len = length_tree.decode(reader);
            if (extra_len < 0) { return 0; }
            match_len += extra_len;
          }
          match_len            += 2; // Implicit minimum match length in LZX is 2

                                     // Calculate Offset using LRU Cache (r0, r1, r2)
          uint32t match_offset  = 0;
          if (pos_slot == 0) {
            match_offset = r0;
          } else if (pos_slot == 1) {
            match_offset = r1;
            r1           = r0;
            r0           = match_offset;
          } else if (pos_slot == 2) {
            match_offset = r2;
            r2           = r0;
            r0           = match_offset;
          } else {
            uint32t extra = *(EXTRA_BITS.data() + pos_slot);
            match_offset  = *(BASE_OFFSETS.data() + pos_slot);

            if (block_type == 2 && extra >= 3) { // Aligned Offset Mode
              match_offset   += reader.readBits(extra - 3) << 3;
              int16t aligned  = aligned_tree.decode(reader);
              if (aligned < 0) { return 0; }
              match_offset += aligned;
            } else {
              match_offset += reader.readBits(extra);
            }

            // Update LRU cache
            match_offset -= 2;
            r2            = r1;
            r1            = r0;
            r0            = match_offset;
          }

          // Copy the match from the output buffer history
          usize current_size = outputBuffer.size();
          if (match_offset == 0 || match_offset > current_size) {
            return 0; // Invalid offset
          }

          for (usize i = 0; i < match_len; ++i) {
            outputBuffer.push_back(outputBuffer[current_size - match_offset + i]);
          }
          decoded += match_len;
        }
      }
    } else if (block_type == 3) { // Uncompressed Block
      reader.alignTo16BitBoundary();

      // r0, r1, r2 are preserved, but we update them with actual byte offsets if needed
      // r0 = reader.readbits(32); r1 = reader.readBits(32); r2 = reader.readBits(32); (Depends on specific CAB
      // implementation)

      for (usize i = 0; i < block_size; ++i) {
        // To keep it dependency fre without reading raw memory pointers, we use readBits(8)
        outputBuffer.push_back(static_cast<uint8t>(reader.readBits(8)));
      }
    } else {
      return 0; // Unkown block type
    }
  }

  return outputBuffer.size() - initial_size;
}

auto Lzx::deflateChunk(dakt::span<const uint8t> /*rawData*/, dakt::vector<uint8t>& /*outputBuffer*/) -> usize {
  // Lzx encoding requires the full lzx encoder state machine.
  // Stubbed until the P4K write path is needed.
  return 0;
}

[[maybe_unused]] const bool s_lzx_registered = [] -> bool {
  CompressionRegistry::instance().registerModule(dakt::make_unique<Lzx>());
  return true;
}();

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_LZX_CXX