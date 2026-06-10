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
{ // NOLINTBEGIN(modernize-use-trailing-return-type)
#endif

  DAKTLIB_API int zipCreateCompressionStream(DataStream* inStream, int compressionType, DataStream* outStream) {
    if ((inStream == nullptr) || (outStream == nullptr)) { return -1; }

    // Safely cast the raw C integer to your modern enum
    auto method = static_cast<dakt::zip::CompressionMethod>(compressionType);

    // Fetch from registry using the strongly-type enum
    auto* algo  = dakt::zip::CompressionRegistry::get(method);
    if (algo == nullptr) {
      return -2; // Unsupported compression method
    }

                 // Create the lens using the found algorithm
    auto lens  = dakt::make_unique<dakt::zip::InflateStream>(*inStream, algo);

    // Wrap it back into the universal C-API stream struct
    *outStream = dakt::capi::makeCStream(dakt::move(lens));

    return 0;
  }

#ifdef __cplusplus
} // extern "C"
#endif // NOLINTEND(modernize-use-trailing-return-type)

#endif // DAKTLIB_CAPI_ZIP_EXPORTS_CXX