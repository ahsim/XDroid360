# Paths and pinned sources for build_tools.sh, package.sh, configure_android_llvm.sh and
# fetch_sources.sh. Sourced, not run. Each path can be set in the environment.
#
# Two ways to have the sources:
#   - fetch_sources.sh (CI, or any machine): the LLVM release into src/, XenonRecomp at
#     XR_COMMIT patched with the pinned patch script, SDL at SDL_COMMIT, into sources/;
#   - the XDroid360 checkout this folder is in, with its recomp submodule and recomp's
#     installer tools (scripts/fetch_build_sources.py, or the author's PC): XDROID360_ROOT
#     (default ..), DAO_TOOLS (its recomp/tools).
#
#   ANDROID_NDK_HOME  NDK 27.3.13750724 (the games' prebuilt runtimes use the same one)
#   XR_SRC            the patched XenonRecomp checkout
#   SDL_SRC           SDL 3.4.18 (the launcher's libSDL3.so is built from the same commit)
#   HOST_LLVM_BIN     host llvm-tblgen and clang-tblgen 22.1.8 (configure_android_llvm.sh)
#   CMAKE, NINJA      if not the ones in DAO_TOOLS
_here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# The pinned sources.
LLVM_VERSION=22.1.8
LLVM_TARBALL_SHA256=922f1817a0df7b1489272d18134ee0087a8b068828f87ac63b9861b1a9965888
XR_REPO=https://github.com/hedge-dev/XenonRecomp.git
XR_COMMIT=ddd128bcca99fe8bfbb99bea583c972351fa6ace
# The XDroid360 games' one recompiler patch script (DAORecomp fork, recomp/scripts).
XR_PATCH_REPO=udarmolota/Dragon-Age-Origins-Recomp-Android
XR_PATCH_COMMIT=86b9d0b1a0af40a44439e9310f02f8c3f2489510
XR_PATCH_SHA256=f007dcf14c2bc8579bebe404d8d466351e6858f2104cbcb5b5aad6793726d936
SDL_REPO=https://github.com/libsdl-org/SDL.git
SDL_COMMIT=829a65d769d935c4852f8159e964312c0957260a
NDK_VERSION=27.3.13750724

ANDROID_SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/AppData/Local/Android/Sdk}}
NDK=${ANDROID_NDK_HOME:-$ANDROID_SDK/ndk/$NDK_VERSION}
XDROID360_ROOT=${XDROID360_ROOT:-$_here/..}
# Absolute, without ../ (they end up in CMake caches and messages).
[ -d "$XDROID360_ROOT" ] && XDROID360_ROOT=$(cd "$XDROID360_ROOT" && pwd)
DAO_TOOLS=${DAO_TOOLS:-$XDROID360_ROOT/recomp/tools}

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=.exe; HOST_TAG=windows-x86_64 ;;
  Darwin*) EXE=; HOST_TAG=darwin-x86_64 ;;
  *) EXE=; HOST_TAG=linux-x86_64 ;;
esac
CMAKE=${CMAKE:-$DAO_TOOLS/cmake/bin/cmake$EXE}
NINJA=${NINJA:-$DAO_TOOLS/ninja/ninja$EXE}
NDK_LLVM=$NDK/toolchains/llvm/prebuilt/$HOST_TAG
STRIP=$NDK_LLVM/bin/llvm-strip$EXE
# fetch_sources.sh's checkouts when they're there, else the XDroid360 checkout's.
if [ -z "${XR_SRC:-}" ]; then
  if [ -d "$_here/sources/XenonRecomp" ]; then XR_SRC=$_here/sources/XenonRecomp; else XR_SRC=$DAO_TOOLS/XenonRecomp; fi
fi
if [ -z "${SDL_SRC:-}" ]; then
  if [ -d "$_here/sources/SDL" ]; then SDL_SRC=$_here/sources/SDL; else SDL_SRC=$XDROID360_ROOT/recomp/thirdparty/SDL; fi
fi
HOST_LLVM_BIN=${HOST_LLVM_BIN:-$DAO_TOOLS/llvm/bin}
# xenia-canary's third_party (for a game profile's build and licences, not the tools).
XENIA_CANARY=${XENIA_CANARY:-$DAO_TOOLS/xenia-canary}
# The XenonRecomp version the recipes pin (requires.tools.xenonrecomp): its commit and the
# patch script's.
XR_VERSION=${XR_COMMIT:0:7}-p${XR_PATCH_COMMIT:0:7}

[ -d "$NDK" ] || { echo "env.sh: no NDK at $NDK (set ANDROID_NDK_HOME)" >&2; return 1 2>/dev/null || exit 1; }
unset _here
