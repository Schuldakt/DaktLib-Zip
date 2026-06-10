/// @file compression_registry.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_H
#define DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_H

#include <config>

#include <compressor>

#include <memory>
#include <string>
#include <unordered_map>

#ifndef DAKTLIB_HAS_NO_PRAGMA_SYSTEM_HEADER
  #pragma GCC system_header
#endif

DAKTLIB_BEGIN_NAMESPACE_ZIP

class CompressionRegistry {
  public:
    CompressionRegistry() = default;

    static auto               instance() -> CompressionRegistry&;
    void                      registerModule(dakt::unique_ptr<ICompressor> module);
    [[nodiscard]] static auto get(CompressionMethod method) -> ICompressor*;

  private:
    dakt::unordered_map<dakt::string, dakt::unique_ptr<ICompressor>> m_modules;
};

DAKTLIB_END_NAMESPACE_ZIP

#endif // DAKTLIB_REGISTRAR_COMPRESSION_REGISTRY_H