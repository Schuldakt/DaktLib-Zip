/// @file compression_registry.cpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-26

#pragma once
#include <compression_registry.hpp>

namespace dakt::decrypt {

void CompressionRegistry::register_module(std::unique_ptr<ICompressor> module) {
  if (module) {
    std::string name_key{module->name()};
    m_modules[std::move(name_key)] = std::move(module);
  }
}

const ICompressor* CompressionRegistry::get(std::string_view name) const {
  std::string name_key{name};
  auto        it = m_modules.find(name_key);
  if (it != m_modules.end()) {
    return it->second.get();
  }
  return nullptr;
}

} // namespace dakt::decrypt