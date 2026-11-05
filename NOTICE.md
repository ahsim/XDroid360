# Third-party components

This repository's own files are MIT-licensed (`LICENSE`). The launcher APK, and the game
profiles and toolchain it carries, include code from the following projects, each under its
own license. The full texts are in the `licenses/` folder of each piece the APK carries
(`assets/kit.zip`, `assets/games/dao.zip`, `assets/games/sod.zip`); the launcher's About screen
lists them too.

## The launcher

| Component | Where | License |
|---|---|---|
| SDL 3 (3.4.18) | `recomp/thirdparty/SDL`, built into the APK as `libSDL3.so` | zlib |
| Mesa Turnip (Vulkan driver for Adreno): StevenMXZ's Turnip Gen8 V38 build (Mesa 27.0), bundled in the APK as the built-in driver | `android/app/src/main/jniLibs/arm64-v8a/libvulkan_turnip_gen8.so`, unchanged from github.com/StevenMXZ/Adreno-Tools-Drivers release v27.0.0-R1 (`Turnip_Gen8_V38.zip`; SHA-256 `41da0373…fd013`); Mesa's source: gitlab.freedesktop.org/mesa/mesa | MIT |
| liblinkernsbypass (Billy Laws): linker namespaces for the private Vulkan loader | `android/app/src/main/cpp/linkernsbypass/` | BSD 2-Clause |
| AndroidX AppCompat, Material Components for Android (the launcher's screens) | Gradle dependencies | Apache-2.0 |
| Material Design icons (Google): the launcher's icons | `android/app/src/main/res/drawable/ic_*.xml` | Apache-2.0 |
| Khronos Vulkan validation layer (debugging, dev builds only) | `android/app/src/dev/jniLibs/` (not in the repository) | Apache-2.0 |
| Code reused from the author's own launchers (Zomdroid, RimDroid, ValDroid): the performance bar, the on-screen gamepad's layout, the private Vulkan loader, the bug report, the launcher's structure | `android/app/` | as in those repositories |

## The toolchain (building the games on the phone)

| Component | Where | License |
|---|---|---|
| LLVM: clang and lld 22.1.8, compiler-rt builtins, libc++, libc++abi, libunwind | the APK's `lib/arm64-v8a/libclang.so`, `liblld.so`; `assets/kit.zip` | Apache-2.0 with LLVM exception |
| Android NDK r27d (27.3.13750724): the sysroot's headers, C runtime objects and library stubs | `assets/kit.zip` | Apache-2.0, BSD and others per file (as in the NDK's `NOTICE`); used under the Android Software Development Kit License Agreement |
| XenonRecomp and XenonUtils (hedge-dev), with this project's patches (`recomp/scripts/patch_recompiler.py`) | the APK's `libxenonrecomp.so`; `recomp/tools/XenonRecomp` | MIT |
| SoD's STFS extractor (a port of SoDRecomp's `stfs.cpp`) | the APK's `libstfsextract.so` | MIT (the DAORecomp contributors) |

## The games' runtimes (the game profiles)

| Component | Where | License |
|---|---|---|
| DAORecomp (Dragon Age: Origins — Recompiled) by 96dot | `recomp/` (git submodule); `dao.zip` | MIT, except its `xenia/` directory |
| SoDRecomp (State of Decay's runtime, derived from DAORecomp) | `sod.zip` | MIT (the DAORecomp contributors) |
| Xenia / xenia-canary (GPU, UI and base code vendored by DAORecomp; the Vulkan backend and SPIR-V translator imported from the same pinned commit `1c81b867`) | `recomp/xenia/`, `xenia-vulkan/src/`; both profiles | BSD 3-Clause (`recomp/xenia/LICENSE`) |
| Vulkan-Headers, VulkanMemoryAllocator, glslang (SPIR-V builder), fmt, xxHash, Snappy, Dear ImGui, toml++ (Xenia's third-party libraries) | `recomp/tools/xenia-canary/third_party/` (fetched, not in the repository); both profiles | Apache-2.0 / MIT, MIT, BSD-3-Clause and others, MIT, BSD-2-Clause, BSD-3-Clause, MIT, MIT |
| The DXBC checksum (AMD, derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm) | `third_party/dxbc`; both profiles | as in its source's header |
| AMD FidelityFX CAS and FSR 1 (shader headers included by Xenia's output shaders) | `recomp/tools/xenia-canary/third_party/FidelityFX-*` (fetched) | MIT |
| FFmpeg 8.0 (the has207/FFmpeg fork, commit `d980192e`; avcodec and avutil for XMA audio), configured without GPL parts | statically in both profiles' `lib/*-deps.a`, whose objects allow linking the game library again with another build; source: https://github.com/has207/FFmpeg | LGPL 2.1 or later |
| SIMD Everywhere (SIMDe) | via XenonRecomp's `thirdparty/`; both profiles | MIT |
| libmspack (LZX decompression, in XenonUtils) | via XenonRecomp's `thirdparty/`; both profiles | LGPL 2.1 |
| tiny-AES-c, TinySHA1 (in XenonUtils) | via XenonRecomp's `thirdparty/`; both profiles | Unlicense; ISC-style (in its header) |

Design references that contributed no code: mw2-android / MW2 Recompiled (GPL-3.0),
Rayman Origins Recomp, Pinyon Shift.

No game code, game data or anything derived from the games' discs is part of this repository
or of what the APK carries: each game is built on the player's phone from the player's own
copy. *Dragon Age* is a trademark of Electronic Arts Inc.; *State of Decay* is a trademark of
Undead Labs and Microsoft.
