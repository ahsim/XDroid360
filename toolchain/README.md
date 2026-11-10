# On-device toolchain (0.1.0, P2 item 1)

This folder holds the tools and the game-agnostic kit that let the launcher build a game's
`libgame.so` on the phone, from the player's own disc, with no PC. The recipe format they
serve is `RECIPE_FORMAT_DRAFT.md` (frozen v2). The P0 measurements are in
`docs/P0_RESULTS.md`.

## Output (`out/`, what DAO's Gradle copies)

| Path | Size | In the APK as | Contents |
|---|---|---|---|
| `out/jniLibs/arm64-v8a/libclang.so` | 98 MB | jniLibs → `nativeLibraryDir` | clang 22.1.8 for aarch64-android29, stripped |
| `out/jniLibs/arm64-v8a/liblld.so` | 57 MB | jniLibs | lld 22.1.8. Call it with `-flavor gnu`, because its argv[0] is `liblld.so` |
| `out/jniLibs/arm64-v8a/libxenonrecomp.so` | 1.3 MB | jniLibs | the patched XenonRecomp; one binary for both games |
| `out/jniLibs/arm64-v8a/libstfsextract.so` | 444 KB | jniLibs, **loaded** (`System.loadLibrary("stfsextract")`) | SoD's STFS extraction for `StfsExtractor` (`stfsextract/`) |
| `out/jniLibs/arm64-v8a/libshaderlib.so` | 256 KB | jniLibs | the native renderer's shader library tool (`shaderlib/`, see below) |
| `out/jniLibs/arm64-v8a/libdxcompiler.so` | 27.6 MB | jniLibs, loaded by `libshaderlib.so` (RUNPATH `$ORIGIN`) | DXC 1.9.2609, stripped |
| `out/kit.zip` (`out/kit/`) | 6.8 MB (46 MB unpacked) | asset → `${KIT}` | see below |

The jniLibs must be extracted at install (`useLegacyPackaging = true`, i.e.
`extractNativeLibs`). Only `nativeLibraryDir` lets an app exec its files (P0).

Kit layout:

```
kit.json                      versions: kit, ndk, clang, sdl3, tools{clang, lld, xenonrecomp}
sysroot/usr/include/          NDK 27.3 headers, libc++ (c++/v1) included
sysroot/usr/lib/aarch64-linux-android/29/     crt*.o, stub .so (libc, libm, libdl, liblog, libandroid, ...), libc++.a (linker script)
sysroot/usr/lib/aarch64-linux-android/        libc++_static.a, libc++abi.a, libunwind.a, libatomic.a
clang/22/include/             clang 22's resource headers (arm_neon.h, ...): -resource-dir ${KIT}/clang/22
clang/lib/libclang_rt.builtins-aarch64-android.a
sdl3/include/SDL3/            SDL 3.4.18 headers (the profiles are built against these)
```

The NDK driver finds `libunwind.a` and `libatomic.a` in its own resource dir. The recipe's
link line has only the sysroot's `-L` paths, so the kit puts them next to libc++.

The XenonRecomp version is `<commit>-<sha256 of git diff, 8 chars>`. Today that is
`ddd128b-1a56f3d0`: DAO's patched copy, which SoD's copy matches in
everything but the `ppc_context.h` template. That template comes from each game's profile.

## Game profiles (`out/games/`)

| Path | Size | Made by |
|---|---|---|
| `out/games/sod.zip` (`out/games/sod/`) | 12 MB (50 MB unpacked) | SoDRecomp's `scripts/make_profile.sh` |
| `out/games/dao.zip` | — | DAO |

SoD's profile holds:

- `recipe.json`, filled in from SoDRecomp's `android/profile/recipe.json.in`;
- `lib/libsod-game.a`: the runtime and the front end, linked `--whole-archive`;
- `lib/libsod-deps.a`: the 15 other archives of the dev link line, merged;
- `include/`: `mmio_hook.h`, the build descriptor and SIMDe;
- `xr/`: `SoD.toml.in`, the switch tables and the `ppc_context.h` template.

Everything is built with NDK 27.3 and `llvm-strip -g`.

`SoD.toml.in` uses absolute paths: `file_path = "${GAME}/game/default.xex"` and
`out_directory_path = "${WORK}/ppc"`. Stock XenonRecomp prefixes the config's folder to every
path in the config. The patched one takes an absolute path as is: DAO's
`patch_recompiler.py`, marker `(XDroid360)`, in `recompiler_config.cpp`.

Checked on the PC (host clang/lld 22.1.8, kit + profile only, the recipe expanded as the
launcher would):

- `pch`, `ppc_recomp.165` and `build-info` compile with the recipe's flags;
- the `link` step's exact line links with `--no-undefined`, over 380 game units;
- the result exports the same 203,964 symbols as the dev build's `libgame.so`;
- it has the same `NEEDED` libraries and the `XDroid360` note with ABI 2.

Not checked yet: the full XenonRecomp + compile chain from the recipe on the phone, and running
the result in XDroid360.

## STFS extraction (`libstfsextract.so`)

The JNI exports are `Java_com_udarmolota_xdroid360_StfsExtractor_nativeProbe(int fd)` and
`..._nativeExtract(int fd, String outDir, Progress)`, as frozen in the recipe format. The
reader is `content/stfs.cpp` from SoD's runtime (the PC launcher's reader), ported from the
Windows API to `pread` on the launcher's fd. Its checks are the same, because the packages
are untrusted. Each package goes where SoD's runtime reads its content type, under `outDir`
(= `${GAME}`):

| Content type | Folder |
|---|---|
| game `0x000D0000` | `game/` |
| DLC `0x00000002` | `userdata/content/<title ID>/00000002/<content ID>/`, plus `<content ID>.name` (the display name, 256 bytes UTF-16BE) |
| title update `0x000B0000` | `title_update/` |
| anything else (saves, ...) | passed over: returns 0, writes nothing |

Checked on the PC with `stfs_cli` (`stfsextract/`, the same reader) on all four
of SoD's packages:

- the game: 59 files, byte-identical to `install/game`;
- Breakdown and Lifeline: 3 and 4 files, identical to `install/dlc`;
- the title update: 3 files, identical to `tools/stfs.py`'s extraction and to `extracted/tu`;
- `game/default.xex` matches the recipe's sha256;
- 2.7 GB took 42 s;
- a truncated package returns 3 (`extract_failed`), and a non-package returns `null` from
  probe and 3 from extract.

## Shader library (`libshaderlib.so`)

The native renderer (`native-renderer/`) draws with SPIR-V shaders translated ahead of time from
the game's Xbox 360 shaders, kept in `nfsmw_shaders.nfsp` in the instance's `userdata/`. That
library is made from the game's files, so it can't ship with the APK: each player's phone builds
it from their own game. `shaderlib/xdroid360_shaderlib.cpp` does, in one process, what the PC
did with four tools:

1. read Dragon Age: Origins' `packages/core/data/shaders.erf` (ERF V2.1, zlib entries) and find
   the Xbox 360 shader containers in it;
2. translate each one to HLSL with XenosRecomp (`native-renderer/shaders/XenosRecomp`, with
   `NFSMW_RECOMP`);
3. compile the HLSL to SPIR-V with DXC, linked in, with the PC's exact `dxc` arguments;
4. pack them with the renderer's own `EmpaquetarShaders`, load the result back and look every
   container up in it.

```
libshaderlib.so <shaders.erf> <out.nfsp> [-j N] [--hlsl <dir>]
```

| Output line | When |
|---|---|
| `containers <n>` | after reading the file |
| `progress <done>/<total>` | every 10 shaders, and at the end |
| `left out <name>: <reason>` | for each container XenosRecomp or DXC rejects (the renderer skips their draws) |
| `shaders: <n> of <m> translated, <k> left out` | at the end |
| `library <bytes> bytes, <ms> ms, peak memory <KB> KB` | at the end |

Exit code: 0 when the library is written, even with some containers left out; 1 on failure
(message on stderr); 2 when the input isn't a `shaders.erf`. It writes `<out.nfsp>.tmp` and
renames it at the end, so a stopped run leaves no half library, and needs no other scratch space.

DXC is pinned to the commit of the `dxc.exe` the PC library was built with, so the result is
byte-identical. `kit.json` names the tool as `<SHADERLIB_VERSION>-dxc<DXC_VERSION>` (`env.sh`);
bump `SHADERLIB_VERSION` when the translator, `shader_common.h` or DXC changes the output.

Building DXC needs its own host `llvm-tblgen` and `clang-tblgen` (its LLVM 3.7 fork): see
`build_dxc_host_tablegen.sh`. On Windows, DXC's support code includes ATL, which DAORecomp's xwin
sysroot doesn't have; `shaderlib/host_shims/atlbase.h` stands in for the little those two tools
use. It is never part of what ships.

Checked on the S25 with an instance's own `shaders.erf` (534 KB):

- 458 of 460 shaders translated, the same 2 left out as on the PC: `primitive.vsh` (XenosRecomp:
  a vertex export with no declared interpolator) and `static2.psh` (DXC: `r254` undeclared);
- 14,562,532 bytes, sha256 `c3d339fb…`: **byte-identical** to the PC-built library;
- 11.4 s on all 8 cores, peak memory 164 MB; 13.7 s and 122 MB with `-j 4`;
- a missing file exits 1, a file that isn't an ERF exits 2.

## Releases and rebuilding

The releases are built by GitHub Actions (`.github/workflows/build.yml`) on Linux. A tag
`v<kit>.<minor>.<patch>` publishes these files:

- `xdroid360-jnilibs-arm64-v8a-<tag>.zip`;
- `xdroid360-kit-<tag>.zip`;
- `kit.json`;
- `xenonrecomp-source-<tag>.tar.gz`;
- `SHA256SUMS`.

The XDroid360 launcher pins them by tag and sha256.

Every source is pinned in `env.sh`:

| Source | Pin |
|---|---|
| LLVM | 22.1.8: the release tarball and its sha256 |
| XenonRecomp | commit `ddd128b` |
| Patch script | the XDroid360 games' one patch script (DAORecomp fork, `scripts/patch_recompiler.py`), commit `86b9d0b` and its sha256 |
| SDL | commit `829a65d` (3.4.18) |
| DXC | commit `01b62ad` (v1.9.2609), with its submodules |
| NDK | 27.3.13750724 |

The kit's `kit.json` names the XenonRecomp version as `ddd128b-p86b9d0b`: its commit and the
patch script's.

On any machine:

```bash
./fetch_sources.sh        # LLVM into src/, XenonRecomp (patched), SDL and DXC into sources/
./build_host_tablegen.sh  # host llvm-tblgen + clang-tblgen into build-host/bin (or set HOST_LLVM_BIN)
./build_dxc_host_tablegen.sh  # DXC's own host tablegens into build-dxc-host/bin
HOST_LLVM_BIN=$PWD/build-host/bin ./build_tools.sh   # clang + lld (one to two hours), XenonRecomp, libstfsextract.so, shaderlib + DXC -> stage/
RELEASE=dev ./package.sh  # stage/ + NDK + SDL3 headers + licenses -> out/
```

The paths come from `env.sh`; each can be set in the environment:

| Variable | Default | What |
|---|---|---|
| `ANDROID_NDK_HOME` | `$ANDROID_HOME/ndk/27.3.13750724` (or the usual Windows SDK folder) | NDK 27.3.13750724. The games' prebuilt runtimes must use the same NDK |
| `XR_SRC`, `SDL_SRC` | `sources/XenonRecomp`, `sources/SDL` when present, else the XDroid360 checkout's | the patched XenonRecomp and SDL |
| `HOST_LLVM_BIN` | `$DAO_TOOLS/llvm/bin` | host `llvm-tblgen` and `clang-tblgen` 22.1.8 |
| `DXC_SRC`, `DXC_HOST_BIN` | `sources/DirectXShaderCompiler`, `build-dxc-host/bin` | DXC's sources and its own host tablegens |
| `DXC_BUILD_DIR`, `DXC_LIB` | `build-dxc-android`, none | where DXC is cross-built; or a `libdxcompiler.so` already built from `DXC_COMMIT` (CI's cache) |
| `XDROID360_ROOT`, `DAO_TOOLS` | `../../DAO`, `$XDROID360_ROOT/recomp/tools` | the author's PC: an XDroid360 checkout with recomp's installer tools (CMake, Ninja, LLVM) |
| `CMAKE`, `NINJA` | from `DAO_TOOLS` | e.g. `cmake`, `ninja` from the PATH |

## Licenses

`kit.zip` carries `licenses/` with an `INDEX.md`: component, where it is, its license, the
text file and its source. It covers the jniLibs too. Note that **`libxenonrecomp.so` is
GPL-2.0-or-later as a whole**: its PowerPC disassembler (`thirdparty/disasm`) comes from GNU
binutils. It is a separate program the launcher runs, so the launcher itself isn't affected.
Its source is XenonRecomp at the recorded commit plus the published patch script.
**`libshaderlib.so` is GPL-3.0 as a whole**: it links the native renderer's shader library code
(from nfsmw-android, GPL-3.0); its source is this repository. `libdxcompiler.so` is DXC under
the University of Illinois/NCSA licence, with SPIRV-Tools (Apache-2.0) and the SPIR-V and
DirectX headers (MIT-style).

## Checks done

- **P0 on the S25:** clang and lld from `nativeLibraryDir` compile all of DAO in 176 s,
  and the result loads.
- **Kit completeness (PC, host clang/lld 22.1.8, only the kit as sysroot):**
  - The setup: a C++ test using `std::string`, exceptions, `std::atomic`, libm, NEON and the
    SDL3 headers, linked with the recipe's exact lld line plus `libSDL3.so`.
  - The compile and the link succeed with `--no-undefined`.
  - All headers resolve from the kit.
  - The `XDroid360` ABI note survives `--gc-sections` (`llvm-readelf -n`).

Not checked yet: the stripped `libxenonrecomp.so` on the phone. The P0 run used the unstripped
build of the same source.

## Other files here

- `configure_android_llvm.sh`: the LLVM configure, called by `build_tools.sh`.
- `build_dxc_host_tablegen.sh`: DXC's host tablegens, for `build_tools.sh shaderlib`.
- `shaderlib/`: the shader library tool's source and CMake project.
- `phone/p0/`: the P0 adb kit and scripts.
- `p0test/`: the P0 in-app test APK project.
