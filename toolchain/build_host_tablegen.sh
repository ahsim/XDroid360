#!/usr/bin/env bash
# Builds the two host tools the Android cross-build needs, llvm-tblgen and clang-tblgen, from
# the same LLVM sources with the host's compiler (CI, or a machine without DAORecomp's tools),
# into build-host/bin. Then: HOST_LLVM_BIN=$PWD/build-host/bin ./build_tools.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
SRC=$HERE/src/llvm-project-$LLVM_VERSION.src
BUILD=${HOST_TABLEGEN_BUILD_DIR:-$HERE/build-host}
"$CMAKE" -G Ninja -S "$SRC/llvm" -B "$BUILD" \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=AArch64 \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF
"$NINJA" -C "$BUILD" llvm-tblgen clang-tblgen
ls -la "$BUILD/bin/llvm-tblgen$EXE" "$BUILD/bin/clang-tblgen$EXE"
