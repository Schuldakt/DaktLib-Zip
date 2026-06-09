/// @file zip_exports.cxx
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_CAPI_ZIP_EXPORTS_CXX
#define DAKTLIB_CAPI_ZIP_EXPORTS_CXX

#include <capi/stream_adapter.h>
#include <zip/capi/zip_exports.h>
#include <zip/registrar/compression_registry.h>
#include <zip/streams/inflate_stream.h>

#include <memory>

#ifdef __cplusplus
extern "C"
{
#endif

  DAKTLIB_API auto zipCreateCompressionStream(DataStream* inStream, int compressionType, DataStream* outStream) {
    if ((inStream == nullptr) || (outStream == nullptr)) { return -1; }
  }

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DAKTLIB_CAPI_ZIP_EXPORTS_CXX