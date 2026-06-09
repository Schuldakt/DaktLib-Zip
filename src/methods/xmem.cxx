/// @file xmem.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_XMEM_CXX
#define DAKTLIB_METHODS_XMEM_CXX

#include <config>

#include <xmem.h>

DAKTLIB_BEGIN_NAMESPACE_ZIP

std::string_view XMem::name() const noexcept {
  return "XMem";
}

CompressionMethod XMem::method() const noexcept {
  return CompressionMethod::XMem;
}

bool XMem::decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const {
  // Stubbed until a native LZX decompressor is integrated.
  return false;
}

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_XMEM_CXX