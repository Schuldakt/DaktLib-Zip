/// @file compression_registry.hpp
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-04-26

#pragma once

#include <icompressor.hpp>
#include <memory>
#include <unordered_map>

namespace dakt::decrypt::registry {

class CompressionRegistry {
public:
  CompressionRegistry() = default;

  void register_module(std::unique_ptr<compression::ICompressor> module);

  [[nodiscard]] const compression::ICompressor* get(std::string_view name) const;

private:
  std::unordered_map<std::string, std::unique_ptr<compression::ICompressor>> m_modules;
};

} // namespace dakt::decrypt::registry