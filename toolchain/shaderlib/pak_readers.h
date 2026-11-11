// The container readers of libshaderlib.so (defined in xdroid360_shaderlib.cpp), for the shader
// library: Dragon Age's shaders.erf and State of Decay's shaders.pak.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace shaderlib {

// zlib (window_bits 15) or raw deflate (-15); empty on failure. `expected` sizes the buffer.
std::vector<uint8_t> Inflate(const uint8_t* src, size_t n, size_t expected, int window_bits);
// Xbox 360 XMemCompress (native LZX framing), as State of Decay's paks store entries (zip method 13);
// empty on failure.
std::vector<uint8_t> XMemDecompress(const uint8_t* src, size_t n, size_t usize);
// Okumura's LZSS as CryEngine's resource files use it.
std::vector<uint8_t> CryLzss(const uint8_t* src, size_t n);

}  // namespace shaderlib
