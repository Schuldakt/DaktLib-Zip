/// @file store.hpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-26

#pragma once
#include <icompressor.hpp>

namespace dakt::decrypt {

class Store : public ICompressor {
public:
  [[nodiscard]] std::string_view  name() const noexcept override;

  [[nodiscard]] CompressionMethod method() const noexcept override;

  bool decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const override;
};

} // namespace dakt::decrypt