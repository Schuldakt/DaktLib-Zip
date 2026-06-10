/// @file compression_registry.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_CXX
#define DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_CXX

#include <config>

#include <zip/registrar/compression_registry.h>

DAKTLIB_BEGIN_NAMESPACE_ZIP

void CompressionRegistry::registerModule(dakt::unique_ptr<ICompressor> module) {
  if (module) {
    dakt::string name_key{module->name()};
    m_modules[dakt::move(name_key)] = dakt::move(module);
  }
}

auto CompressionRegistry::get(CompressionMethod name) -> ICompressor* {
  static CompressionRegistry instance;
  dakt::string               name_key{toString(name)};
  auto                       it = instance.m_modules.find(name_key);
  if (it != instance.m_modules.end()) { return it->second.get(); }
  return nullptr;
}

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_CXX