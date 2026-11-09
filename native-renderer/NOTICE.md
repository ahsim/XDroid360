# Native renderer: origin and licences

This directory holds XDroid360's native (EDRAM-free) Vulkan renderer for Xbox 360 games. Instead of emulating the
Xenos GPU, it reads the game's PM4 command ring and draws with Vulkan, using shaders translated ahead of time.

It is taken from **[nfsmw-android](https://github.com/codepdbh/nfsmw-android) by codepdbh**, at commit
`71b4c8056481fb8652eb0e7433017644e5df54f5` (2026-10-27). The renderer was written for the Need for Speed: Most
Wanted recompilation, and nfsmw-android ported it to Android. Its own lineage, from nfsmw-android's
`THIRD_PARTY_NOTICES.md`:

| Project | Author | Licence |
|---|---|---|
| [nfsmw-android](https://github.com/codepdbh/nfsmw-android) | codepdbh | GPL-3.0 |
| [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) (the Switch port, where the native renderer was written) | StevensND | GPL-3.0 |
| [NFSMW-Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled) | madelrandel-blip | GPL-3.0 |
| [NFSMW-Recompiled-Mobile](https://github.com/victorgbd/NFSMW-Recompiled-Mobile) | victorgbd and contributors | GPL-3.0 |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), with portions from [Xenia](https://xenia.jp) | Tom Clay; Ben Vanik and the Xenia contributors | BSD-3-Clause |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (in `shaders/`) | hedge-dev and contributors | MIT |

## What is here

- **`src/`**: the renderer's Vulkan files from nfsmw-android's `app/src/` (`nfsmw_nativo_*`,
  `nfsmw_shader_library`, `nfsmw_espera_anillo`, `nfsmw_ajustes_graficos`, `nfsmw_spirv_*`, `nfsmw_texturas_bc.h`,
  `nfsmw_gpu_compatibilidad.h`, `nfsmw_microcodigo_versiones.h`). GPL-3.0 (`LICENSE`). The OpenGL ES files are not
  taken.
- **`shaders/`**: nfsmw-android's `shaders/` (its XenosRecomp fork and its container and library tools), without
  the prebuilt WebAssembly binaries. MIT (`shaders/LICENSE.md`).
- The first commit of each file is nfsmw-android's file **unchanged**, so every XDroid360 change shows in the
  history against the original. Changed files say so in a header comment naming the original file and commit.
- **Not taken:** game-derived shader binaries (`nfsmw_nativo_resplandor_*_spirv.h`, "derived from a game shader and
  not distributed" upstream), and anything game-specific to Need for Speed beyond what the shared files contain.

## Licence of the result

The renderer is GPL-3.0. Linking it into a game's runtime makes that runtime, and the XDroid360 APK, a GPL-3.0
work. Our own code that it links with is MIT, which is compatible.

Thanks to codepdbh and to everyone in the chain above. The native renderer is their work; XDroid360 adapts it to
its own runtime and games.
