/// @file compression_registry.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_CXX
#define DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_CXX

#include <config>

#include <compression_registry.h>

DAKTLIB_BEGIN_NAMESPACE_ZIP

void CompressionRegistry::register_module(std::unique_ptr<ICompressor> module) {
  if (module) {
    std::string name_key{module->name()};
    m_modules[std::move(name_key)] = std::move(module);
  }
}

const ICompressor* CompressionRegistry::get(std::string_view name) const {
  std::string name_key{name};
  auto        it = m_modules.find(name_key);
  if (it != m_modules.end()) { return it->second.get(); }
  return nullptr;
}

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_CXX