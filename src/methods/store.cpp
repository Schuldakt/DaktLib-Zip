/// @file store.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-27

#pragma once
#include <store.hpp>

namespace dakt::decrypt {

std::string_view  Store::name() const noexcept { return "None"; }

CompressionMethod Store::method() const noexcept { return CompressionMethod::Store; }

bool Store::decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const {
  output.assign(input.begin(), input.end());
  return true;
}

} // namespace dakt::decrypt