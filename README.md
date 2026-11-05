# XDroid360

**XDroid360** is an unofficial, community-developed launcher that runs Xbox 360 games on Android
phones. Today it plays **Dragon Age: Origins** and **State of Decay**, with a gamepad or the
on-screen controls. The games' code runs natively on the phone's processor; the console's graphics
chip is emulated on Vulkan.

> [!NOTE]
> XDroid360 does not include any game. It installs **your own copy**: you add the game's disc image
> or a `.zip`, and the phone turns it into a game you can play. Nothing of the games comes with the
> app or is stored here.

> [!WARNING]
> **XDroid360 is in beta (0.1.0).** It is made for and tested on **Snapdragon flagships**. Other
> phones may work, but nobody has tried them yet. Back up your saves before you play (the side menu
> has **Export saves** for that).

## Download only from GitHub

Get XDroid360 only from the [Releases page](https://github.com/udarmolota/XDroid360/releases). It is
signed with my own key. Certificate fingerprint (SHA-256):

```
F8:5A:43:7A:50:90:FB:84:B9:EB:2A:4C:BF:F5:AB:C0:7E:4D:3B:A2:D5:C4:BE:02:59:63:6F:3A:62:4E:D2:C6
```

You can check it in apps like MT Manager or App Manager. The real XDroid360 also installs over itself
as an update: if Android refuses to update your copy with the one from GitHub, your copy is not the
original.

## How to play

1. Install XDroid360 from the [Releases page](https://github.com/udarmolota/XDroid360/releases).
2. Copy your game to the phone's own storage.
3. In the launcher, tap **+**, choose the game and pick its file.
4. Check the card (which game it is, the space it needs) and tap **Build**. Installing takes
   **5–15 minutes**. You can leave the launcher meanwhile: the notification shows the progress.
5. Tap **Play**.

## Supported games

| Game | What to add | State |
|---|---|---|
| **Dragon Age: Origins** | the Xbox 360 disc image (`.iso`, or a `.zip` with it): the English/Italian/Spanish edition, or disc 1 of the Ultimate Edition | playable: 50–60 fps in the first camp on a cool phone; 30 with the game's own frame cap for long sessions |
| **State of Decay** | a `.zip` of its folder from a console's storage (`584111E8`), with the game, the DLC **Breakdown** and **Lifeline**, and Title Update 6 | playable, 30 fps by default. The DLC get installed; playing them hasn't been checked yet |

Other editions of a game may use a different version of its code. The launcher checks this when you
add the game and tells you if your edition isn't supported yet.

## Features

- ✔️ **Native code**: each game's code is translated for the phone's ARM processor when you install
  it; the console's graphics are emulated on Vulkan (Xenia's backend)
- ✔️ **On-screen controls with an editor**: move the buttons and sticks; every installed game keeps its
  own layout. Physical gamepads (Bluetooth or USB) work too
- ✔️ **Vulkan drivers**: Turnip for Adreno 8xx is built in; each game can use it, the phone's own
  driver, or any other driver you install (a `.so`, or an Adreno Tools `.zip`)
- ✔️ **In-game display**: off, only FPS, or a full bar with GPU and CPU load, memory, power, temperature
  and an FPS graph
- ✔️ **Saves**: export and import
- ✔️ **Report a bug** from the menu: pick your mail app, write a few words, and the logs are attached
- ✔️ Vibration on the on-screen buttons

## Project status & what to expect

XDroid360 is built by **one person**, with the help of AI, and grew out of
[Zomdroid](https://github.com/udarmolota/zomdroid), [RimDroid](https://github.com/udarmolota/RimDroid)
and [ValDroid](https://github.com/udarmolota/ValDroid). A few honest notes:

- **Installing takes time and space.** The phone builds each game itself. *Dragon Age: Origins* needs
  about 8 GB (and about 8 GB more while a `.zip` is unpacked); *State of Decay* about 3 GB (and about
  2 GB more). The card tells you before anything starts.
- **Phones get hot.** At 60 fps the GPU slows down after some minutes. The 30 fps cap in the game's
  options, or a cooler, is much kinder to your hands and your battery.
- **The first minutes are slower.** The graphics' shaders are prepared once and cached; later sessions
  are smoother.
- **Text input** (a character's name) is Latin letters only: the games' fonts have no others.
- *Dragon Age: Origins*: no DLC or title update yet. The text and voices are what your disc has
  (English, Italian or Spanish).

## Device compatibility

| GPU | Driver | State |
|---|---|---|
| Adreno 8xx (Snapdragon 8 Elite) | the built-in Turnip | tested on a Galaxy S25 Ultra. The phone's own Adreno driver shows a black picture for now |
| Adreno 6xx/7xx | a current Turnip build for 6xx/7xx, e.g. StevenMXZ's `Turnip_v27.0.0-R1` ([releases](https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases)) | untested |
| Mali, Xclipse, PowerVR | the phone's own driver | untested |

If you try another phone, a report from **Report a bug** is very welcome.

## System requirements

- **Minimum:** Android 11+, ARM64, Vulkan, and the free storage above.
- **Recommended:** a recent Snapdragon flagship with 12 GB of RAM (what it is tested on).

## Getting the best performance

- **Turn on your phone's game booster** at its highest-performance profile: Samsung *Game Booster*,
  Xiaomi/POCO *Game Turbo*, Realme/OPPO/OnePlus *Game Space*, vivo/iQOO *Ultra Game Mode*.
- **Cap the frame rate at 30** for long sessions (*Dragon Age: Origins*: Options → Graphics).
- **Close background apps** so the game gets the memory to itself.

## How it works

The games were made for the Xbox 360's PowerPC processor. When you install one, the launcher
**statically recompiles** its code: the phone translates the machine code from your copy into C++
(XenonRecomp) and compiles it for ARM64 (clang and lld, built into the app), in about 5–15 minutes.
The result runs with the game's runtime: [DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp)
by 96dot for *Dragon Age: Origins*, and SoDRecomp, derived from it, for *State of Decay*. They stand
in for the console's system. The graphics are the heaviest part: Xenia's Vulkan backend emulates
the console's GPU, translating its commands and shaders for the phone's.

## Supporting development

This is an independent project. To help keep it going, contributions are welcome via
[Ko-Fi](https://ko-fi.com/udarmolota).

## Feedback

Please report issues via [GitHub Issues](https://github.com/udarmolota/XDroid360/issues). The quickest
way to report a problem is **Report a bug** in the side menu: choose the game, pick your email app,
add a short description of what happened, and send. The logs are attached for you.

## Credits & Third-Party Sources

- [DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp) by 96dot: the *Dragon Age: Origins*
  runtime, and the base of *State of Decay*'s
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) by hedge-dev: PowerPC → C++ translation
- [Xenia](https://github.com/xenia-project/xenia) / xenia-canary: the Vulkan graphics backend
- [LLVM](https://llvm.org) clang and lld: compiling the games on the phone
- [Mesa Turnip](https://gitlab.freedesktop.org/mesa/mesa), StevenMXZ's
  [Gen8 build](https://github.com/StevenMXZ/Adreno-Tools-Drivers): the Adreno Vulkan driver
- [SDL 3](https://github.com/libsdl-org/SDL), [FFmpeg](https://github.com/has207/FFmpeg) (XMA audio),
  [liblinkernsbypass](https://github.com/bylaws/liblinkernsbypass)
- [Zomdroid](https://github.com/udarmolota/zomdroid), [RimDroid](https://github.com/udarmolota/RimDroid)
  and [ValDroid](https://github.com/udarmolota/ValDroid): the launchers XDroid360 grew out of

All components and their licences: [NOTICE.md](NOTICE.md). XDroid360's own code is MIT (`LICENSE`).

### Source code

Everything in the APK is built from public sources: this repository (the launcher, and the
on-phone toolchain in [`toolchain/`](toolchain)), the
[DAORecomp fork](https://github.com/udarmolota/Dragon-Age-Origins-Recomp-Android) and
[SoDRecomp](https://github.com/udarmolota/SoDRecomp). The recompiler the launcher runs,
`libxenonrecomp.so`, is GPL-2.0-or-later as a whole (its disassembler comes from GNU binutils);
its exact source is [XenonRecomp](https://github.com/hedge-dev/XenonRecomp/tree/ddd128bcca99fe8bfbb99bea583c972351fa6ace)
at `ddd128b`, patched by
[`scripts/patch_recompiler.py`](https://github.com/udarmolota/Dragon-Age-Origins-Recomp-Android/blob/86b9d0b1a0af40a44439e9310f02f8c3f2489510/scripts/patch_recompiler.py)
at `86b9d0b`, built by [`toolchain/build_tools.sh`](toolchain/build_tools.sh).

*Dragon Age* is a trademark of Electronic Arts; *State of Decay* is a trademark of Undead Labs and
Microsoft. XDroid360 is not affiliated with or endorsed by them.
