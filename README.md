# XDroid360

Xbox 360 games on Android phones, built on the phone from your own copy of the game:
*Dragon Age: Origins* (on [DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp) by
96dot) and *State of Decay*. Each game's code is translated ahead of time and compiled for the
phone by the launcher itself, and the graphics run on Vulkan through Xenia's backend. The games
keep their console interface, played with a gamepad or the on-screen one.

Made with the help of AI.

**No game code or game data is shared here.** The launcher builds each game on your phone from
your own copy:

- *Dragon Age: Origins*: the Xbox 360 disc image (`.iso`, or a `.zip` with it), title ID
  `454108C0`, media ID `186A1115` (the English/Italian/Spanish edition, also disc 1 of its
  Ultimate Edition);
- *State of Decay*: its folder from a console's storage (`584111E8`, with the game, DLC and
  title update packages), as a `.zip`.

## How to play

1. **Install the XDroid360 launcher** APK from this repository's
   [Releases](https://github.com/udarmolota/XDroid360/releases).
2. **Copy your game to the phone**: a cable, a USB drive or a download, into the phone's own
   storage (a cloud drive's file can't be read in place).
3. In the launcher, tap **+**, pick the disc image or `.zip`, check the card (the game, its
   version, the space it needs) and tap **Build**. Installing includes the static
   recompilation of the game's code for the phone: 5–10 minutes, plus copying the game files.
   You can leave the launcher meanwhile; the notification shows the progress and can cancel
   it. A build that was stopped continues where it left off.
4. Tap **Play**.

The game is your own copy: nothing of it comes with this app.

The launcher keeps each game's settings (Vulkan driver, graphics, environment variables),
saves (export and import), shader cache and a bug report screen. To build everything yourself,
see [docs/BUILDING.md](docs/BUILDING.md).

Status: for Snapdragon flagships. *Dragon Age: Origins* is playable on a Galaxy S25 Ultra
(Snapdragon 8 Elite, Adreno 830): 50–60 fps in the first camp on a cool phone, 30 with the
game's frame cap for long sessions. See [docs/status.md](docs/status.md).

## Drivers

The launcher comes with Turnip Gen8 V38 (Mesa) for the Adreno 8xx; any other Vulkan driver in
Android's format (a `.so`, or an Adreno Tools `.zip`) can be installed per game from the side
menu, without rebuilding anything.

| GPU | Driver | State |
|---|---|---|
| Adreno 8xx (Snapdragon 8 Elite) | the built-in Turnip | tested on the S25 Ultra. The phone's own Adreno driver shows a black picture for now |
| Adreno 6xx/7xx | a current Turnip build for 6xx/7xx, e.g. StevenMXZ's `Turnip_v27.0.0-R1` ([releases](https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases)) | untested |

If you try another phone, a report from the launcher's **Report a bug** is very welcome.

## Playing

- **Controls**: any Bluetooth or USB gamepad, or the on-screen one (its layout: the side menu,
  or hold the switch at the top of the screen in the game).
- **Graphics**: *Dragon Age: Origins*' own Graphics tab (Options, in the title and pause menus)
  has the frame cap: 30 keeps a phone cool through long sessions. The launcher's settings have
  the texture detail, the Vulkan driver and, under Advanced, Turnip's mode and environment
  variables.
- **Text** (a character's name): Android's keyboard opens in a dialog; Latin letters only for
  now (the games' fonts have no others).

## Known issues

- Heat: the phone's GPU gets throttled after some minutes at 60 fps; the 30 fps cap or a
  cooler helps.
- *Dragon Age: Origins*: no DLC or title update yet (DAORecomp supports them; untested on
  Android). The text and voices are English, Italian or Spanish (what the disc has).
- Building a game needs free space for the game files and the build: the card says how much
  (about 7 GB for *Dragon Age: Origins*, and about 8 GB more while a `.zip` is unpacked).

## License

MIT (see `LICENSE`) for this repository's own files. Third-party components and their
licenses are listed in `NOTICE.md`. *Dragon Age* is a trademark of Electronic Arts; *State of
Decay* is a trademark of Undead Labs and Microsoft; this project is not affiliated with or
endorsed by them.
