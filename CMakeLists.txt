cmake_minimum_required(VERSION 4.3)

project(DaktLib-Zip LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(DaktLib-Zip STATIC)
add_library(dakt::Zip ALIAS DaktLib-Zip)

target_sources(DaktLib-Zip
    PRIVATE
        src/methods/lz4.cxx
        src/methods/lzx.cxx
        src/methods/store.cxx
        src/methods/xmem.cxx
        src/methods/zlib.cxx
        src/methods/zstd.cxx

        src/registrar/compression_registry.cxx

        src/capi/zip_exports.cxx

    PUBLIC
        FILE_SET exposed_headers
        TYPE HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include/dakt/zip
        FILES
            include/dakt/zip/compressor

    PUBLIC
        FILE_SET headers
        TYPE HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include/dakt
        FILES
            include/dakt/zip/capi/zip_exports.h

            include/dakt/zip/methods/lz4.h
            include/dakt/zip/methods/lzx.h
            include/dakt/zip/methods/store.h
            include/dakt/zip/methods/xmem.h
            include/dakt/zip/methods/zlib.h
            include/dakt/zip/methods/zstd.h

            include/dakt/zip/registrar/compression_registry.h
)

target_link_libraries(DaktLib-Zip PUBLIC
    dakt::Core
    dakt::Log
)

set_target_properties(DaktLib-Zip PROPERTIES
    VERIFY_INTERFACE_HEADER_SETS ON
)

enable_testing()

add_executable(DaktLib-Zip-Tests
    tests/zip_tests.cxx
)

target_link_libraries(DaktLib-Zip-Tests PRIVATE DaktLib-Zip)

add_test(NAME ZipPipelineTests COMMAND 
    DaktLib-Zip-Tests
    dakt::Core
)

