/// @file zip_exports.h
/// @author Schuldakt (https://github.com/Schuldakt)
/// @brief
/// @version 0.1
/// @date 2026-06-08
#ifndef DAKTLIB_CAPI_ZIP_EXPORTS_H
#define DAKTLIB_CAPI_ZIP_EXPORTS_H

#include <capi/stream.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // Takes input stream (e.g., from VFS), and wraps it in a decompression stream
  DAKTLIB_API auto zipCreateDecompressionStream(DataStream* inStream, int compressionType, DataStream* outStream)
    -> int;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DAKTLIB_CAPI_ZIP_EXPORTS_H