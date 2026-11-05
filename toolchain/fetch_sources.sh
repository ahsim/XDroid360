#!/usr/bin/env bash
# Fetches the pinned sources (env.sh), so the tools build without an XDroid360 checkout (CI):
#   src/llvm-project-<LLVM_VERSION>.src   the LLVM release, sha256-checked
#   sources/XenonRecomp                   XR_COMMIT with its submodules, patched with the XDroid360
#                                         games' patch script at XR_PATCH_COMMIT (sha256-checked)
#   sources/SDL                           SDL_COMMIT (headers for the kit)
# Usage: fetch_sources.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
PYTHON=${PYTHON:-}
if [ -z "$PYTHON" ]; then for PYTHON in python3 python; do "$PYTHON" -c "" 2>/dev/null && break; done; fi

# LLVM: the source release.
SRC=$HERE/src/llvm-project-$LLVM_VERSION.src
if [ ! -d "$SRC/llvm" ]; then
  mkdir -p "$HERE/dl" "$HERE/src"
  tarball=$HERE/dl/llvm-project-$LLVM_VERSION.src.tar.xz
  [ -f "$tarball" ] || curl -fL --retry 3 -o "$tarball" \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/llvm-project-$LLVM_VERSION.src.tar.xz"
  echo "$LLVM_TARBALL_SHA256  $tarball" | sha256sum -c -
  # Only what clang and lld build from (cmake/, third-party/ and the docs dirs CMake asks for).
  tar -xJf "$tarball" -C "$HERE/src" \
    "llvm-project-$LLVM_VERSION.src/llvm" "llvm-project-$LLVM_VERSION.src/clang" \
    "llvm-project-$LLVM_VERSION.src/lld" "llvm-project-$LLVM_VERSION.src/cmake" \
    "llvm-project-$LLVM_VERSION.src/third-party" "llvm-project-$LLVM_VERSION.src/libunwind"
fi

clone() {  # clone <repo> <commit> <dir> [--recursive]
  if [ ! -d "$3/.git" ]; then
    git init -q "$3"
    git -C "$3" remote add origin "$1"
  fi
  git -C "$3" fetch -q --depth 1 origin "$2"
  git -C "$3" checkout -q --force "$2"
  if [ "${4:-}" = --recursive ]; then git -C "$3" submodule update -q --init --recursive --depth 1; fi
}

mkdir -p "$HERE/sources"
XR=$HERE/sources/XenonRecomp
clone "$XR_REPO" "$XR_COMMIT" "$XR" --recursive
# The patch script, pinned by commit and sha256, run on the checkout (it keeps .orig copies
# and re-patches from them, so running this again is a no-op).
script=$HERE/sources/patch_recompiler.py
curl -fL --retry 3 -o "$script" \
  "https://raw.githubusercontent.com/$XR_PATCH_REPO/$XR_PATCH_COMMIT/scripts/patch_recompiler.py"
echo "$XR_PATCH_SHA256  $script" | sha256sum -c -
"$PYTHON" -I "$script" "$XR/XenonRecomp/recompiler.cpp"

clone "$SDL_REPO" "$SDL_COMMIT" "$HERE/sources/SDL"
echo "sources ready"
