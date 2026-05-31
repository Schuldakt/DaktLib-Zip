/// @file XMem.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-27

#pragma once
#include <xmem.hpp>

namespace dakt::decrypt {

std::string_view  XMem::name() const noexcept { return "XMem"; }

CompressionMethod XMem::method() const noexcept { return CompressionMethod::XMem; }

bool XMem::decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const {
  // Stubbed until a native LZX decompressor is integrated.
  return false;
}

} // namespace dakt::decrypt