/// @file store.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_METHODS_STORE_CXX
#define DAKTLIB_METHODS_STORE_CXX

#include <config>

#include <store.h>

DAKTLIB_BEGIN_NAMESPACE_ZIP

std::string_view Store::name() const noexcept {
  return "None";
}

CompressionMethod Store::method() const noexcept {
  return CompressionMethod::Store;
}

bool Store::decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) const {
  output.assign(input.begin(), input.end());
  return true;
}

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_METHODS_STORE_CXX