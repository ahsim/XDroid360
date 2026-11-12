cmake_minimum_required(VERSION 3.20)
# libshaderlib.so: the native renderer's shader library tool (xdroid360_shaderlib.cpp), for
# Android arm64 with the NDK (build_tools.sh shaderlib). Inputs (cache variables):
#   NR_DIR    the native renderer (../../native-renderer: src/ and shaders/XenosRecomp/)
#   FMT_DIR   fmt, XXH_DIR xxHash: XenonRecomp's thirdparty copies (the same commits as XenosRecomp's)
#   DXC_SRC   DirectXShaderCompiler's sources at DXC_COMMIT (include/dxc)
#   DXC_LIB   the libdxcompiler.so built from them for the same target
#   MSPACK_DIR  libmspack's mspack/ (XenonRecomp's thirdparty): lzxd.c, for State of Decay's
#               XMemCompress (LZX) pak entries
project(xdroid360_shaderlib CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(XR ${NR_DIR}/shaders/XenosRecomp)

# shader_common.h, pasted into every translated shader, goes into the binary.
file(READ ${XR}/shader_common.h COMMON_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," COMMON_BYTES "${COMMON_HEX}")
file(WRITE ${CMAKE_BINARY_DIR}/gen/shader_common_embedded.h
  "// Generated from XenosRecomp/shader_common.h by CMake.\n#pragma once\n"
  "static const unsigned char kShaderCommon[] = {${COMMON_BYTES}};\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${XR}/shader_common.h)

add_executable(xdroid360_shaderlib
  xdroid360_shaderlib.cpp
  sod_russian.cpp
  ${XR}/shader_recompiler.cpp
  ${NR_DIR}/src/nfsmw_shader_library.cpp
  ${FMT_DIR}/src/format.cc
  ${XXH_DIR}/xxhash.c
  ${MSPACK_DIR}/lzxd.c)
target_compile_definitions(xdroid360_shaderlib PRIVATE NFSMW_RECOMP)
target_include_directories(xdroid360_shaderlib PRIVATE
  ${CMAKE_BINARY_DIR}/gen ${XR} ${NR_DIR}/src ${FMT_DIR}/include ${XXH_DIR} ${DXC_SRC}/include ${MSPACK_DIR})
set_source_files_properties(${MSPACK_DIR}/lzxd.c PROPERTIES COMPILE_OPTIONS "-w")
# XenosRecomp's sources expect its precompiled header (std, fmt, xxhash) before anything else.
set_source_files_properties(${XR}/shader_recompiler.cpp PROPERTIES COMPILE_OPTIONS
  "-include;${XR}/pch_min.h;-Wno-switch;-Wno-unused-variable;-Wno-null-arithmetic")
target_compile_options(xdroid360_shaderlib PRIVATE -fms-extensions -Wno-unknown-pragmas)

find_package(ZLIB REQUIRED)
target_link_libraries(xdroid360_shaderlib PRIVATE ${DXC_LIB} ZLIB::ZLIB)
# libdxcompiler.so sits next to this binary in the app's nativeLibraryDir; 16 KB pages.
target_link_options(xdroid360_shaderlib PRIVATE "-Wl,-rpath,$ORIGIN" "-Wl,-z,max-page-size=16384")
