#!/usr/bin/env bash
# clang + lld 22.1.8 for Android aarch64, cross-built with the NDK. The host tablegens
# (llvm-tblgen, clang-tblgen of the same LLVM) come from HOST_LLVM_BIN: on the author's PC
# DAORecomp's tools (env.sh), in CI a native build of just those two
# (build_host_tablegen.sh). Sources: the llvm-project release in src/ (fetch_sources.sh).
# No debug info (-g0): the binaries are stripped anyway, and it keeps the build tree small.
# 16 KB pages (flexible page sizes): a 16 KB-page kernel refuses 4 KB-aligned segments.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
SRC=$HERE/src/llvm-project-$LLVM_VERSION.src
BUILD=${LLVM_BUILD_DIR:-$HERE/build-android}
HOST=$HOST_LLVM_BIN
[ -x "$HOST/llvm-tblgen$EXE" ] && [ -x "$HOST/clang-tblgen$EXE" ] ||
  { echo "no llvm-tblgen/clang-tblgen in $HOST (set HOST_LLVM_BIN, or run build_host_tablegen.sh)" >&2; exit 1; }

"$CMAKE" -G Ninja -S "$SRC/llvm" -B "$BUILD" \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=16384" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -g0" -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -g0" \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_TARGETS_TO_BUILD=AArch64 \
  -DLLVM_HOST_TRIPLE=aarch64-linux-android29 \
  -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-android29 \
  -DLLVM_TABLEGEN="$HOST/llvm-tblgen$EXE" \
  -DLLVM_HEADERS_TABLEGEN="$HOST/llvm-tblgen$EXE" \
  -DCLANG_TABLEGEN="$HOST/clang-tblgen$EXE" \
  -DLLVM_NATIVE_TOOL_DIR="$HOST" \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_INCLUDE_UTILS=OFF -DLLVM_BUILD_UTILS=OFF \
  -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF \
  -DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
  -DCLANG_BUILD_TOOLS=ON \
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_TERMINFO=OFF \
  -DLLVM_ENABLE_BINDINGS=OFF \
  -DLLVM_PARALLEL_LINK_JOBS=2 \
  -DLLVM_ENABLE_ASSERTIONS=OFF
