#!/usr/bin/env bash
# DXC's own host tablegens (llvm-tblgen and clang-tblgen of DXC's LLVM 3.7 fork), which the
# Android cross-build of libdxcompiler.so runs (build_tools.sh shaderlib); LLVM 22's can't read
# DXC's .td files. Into build-dxc-host/bin (DXC_HOST_BUILD_DIR to change it).
#   Linux (CI): the host compiler.
#   Windows (the author's PC): DAORecomp's clang-cl and xwin sysroot (DAO_TOOLS). That sysroot
#   has no ATL, which DXC's Windows support code includes; shaderlib/host_shims/atlbase.h stands
#   in for the little of it these two tools use.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
BUILD=${DXC_HOST_BUILD_DIR:-$HERE/build-dxc-host}
[ -f "$DXC_SRC/CMakeLists.txt" ] || { echo "no DXC sources at $DXC_SRC (run fetch_sources.sh)" >&2; exit 1; }
args=(-G Ninja -S "$DXC_SRC" -B "$BUILD" -C "$DXC_SRC/cmake/caches/PredefinedParams.cmake"
  -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_BUILD_TYPE=Release
  -DHLSL_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF
  -DHLSL_BUILD_DXILCONV=OFF -DLLVM_APPEND_VC_REV=OFF)
if [ "$EXE" = .exe ]; then
  L=$DAO_TOOLS/llvm/bin
  W=$(cygpath -m "$DAO_TOOLS/winsysroot")
  SDK="$W/Windows Kits/10/Include/10.0.26100"
  args+=(-DCMAKE_C_COMPILER="$L/clang-cl.exe" -DCMAKE_CXX_COMPILER="$L/clang-cl.exe"
    -DCMAKE_LINKER="$L/lld-link.exe" -DCMAKE_AR="$L/llvm-lib.exe"
    -DCMAKE_RC_COMPILER="$L/llvm-rc.exe" -DCMAKE_MT="$L/llvm-mt.exe"
    -DCMAKE_C_FLAGS="-winsysroot $W"
    -DCMAKE_CXX_FLAGS="-winsysroot $W /EHsc -imsvc $(cygpath -m "$HERE/shaderlib/host_shims")"
    -DCMAKE_EXE_LINKER_FLAGS="/winsysroot:$W /manifest:no"
    -DCMAKE_SHARED_LINKER_FLAGS="/winsysroot:$W /manifest:no"
    -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
    -DLLVM_ENABLE_DIA_SDK=OFF -DHLSL_ENABLE_DEBUG_ITERATORS=OFF
    -DD3D12_INCLUDE_DIR="$SDK/um" -DDXGI_INCLUDE_DIR="$SDK/shared")
fi
"$CMAKE" "${args[@]}"
"$NINJA" -C "$BUILD" llvm-tblgen clang-tblgen
ls -la "$BUILD/bin/llvm-tblgen$EXE" "$BUILD/bin/clang-tblgen$EXE"
