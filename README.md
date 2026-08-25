# XenomodsTAS

XenomodsTAS is an in-game toolset for tool-assisted speedrun development, route research, and debugging in *Xenoblade Chronicles 2* and *Torna ~ The Golden Country*.

The project provides a bunch of TAS-related features to work with, designed to make TASing this game much less time-consuming and way more convenient/user friendly

## Features

- Trigger visualization for tutorials, cutscenes, landmarks, and collection points
- Live player telemetry, including position, velocity, speed, facing angle, grounded state, and wall contact
- Frame counting and input buffering tools
- Route targeting with visualized waypoints
- Save reloading
- Freecam
- Warps
- Combat AI inspection
- Collision debugging
- Utility, telemetry, trigger, warp, and frame-counter windows
- Rendering control

And a lot more!

## Installation

> [!CAUTION]
> This is a game modification intended for consoles already set up to run Atmosphere. Back up important data before installing or updating mods.

1. Download the appropriate ZIP from the [latest release](https://github.com/mawmz/xenomodsTAS/releases/latest).
2. Extract it to the root of your SD card.
3. Allow the included `atmosphere` directory to merge with the one already on the card.
4. Start the game through Atmosphere.

| Game | Title ID | Supported game versions | Codename |
|---|---|---|---|
| Xenoblade Chronicles 2 | `0100E95004038000` (`0100F3400332C000` for JP) | 1.0.0–2.0.0, or Torna 1.0.0[^1] | `bf2` |
| Xenoblade Chronicles 2: Torna | `0100C9F009F7A000` | 1.0.0, or XC2 2.0.0 | `ira` |

If using a Yuzu-based emulator, please install this mod at `0100E95004039001` instead.

Xenomods patches version-specific game code. If an installed game update is v2.0.1+, the game will not load/crash entirely.

An older `main` executable can be placed at `/atmosphere/contents/<title-id>/exefs/main`. This also restores the older executable's original bugs and limitations, so use one whenever possible. Ideally, v2.0.0 would be the best. (But I've been using v1.5.1 just fine!)

## Usage

Press `L + R + ZL + ZR` to open the in-game menu. It supports both controller and touch input.

## Building from source

### Requirements

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with Nintendo Switch development packages
- CMake 3.19 or newer
- Ninja
- Python 3

Clone the repository and all submodules:

```bash
git clone --recursive https://github.com/mawmz/xenomodsTAS.git
cd xenomodsTAS
```

Configure and build the desired target. For the primary Xenoblade Chronicles 2 build:

```bash
cmake --preset release-bf2
cmake --build build-release-bf2
```

Other presets are available for the codenames in the compatibility table. `release-bf2_ira` produces a combined Xenoblade Chronicles 2 and Torna build.

After a successful build, the generated `exefs` directory is ready to copy into the matching Atmosphere title directory. Release archives can be assembled with `scripts/makePackage.py` from inside the build directory.

To deploy automatically to a console running sys-ftpd with anonymous login enabled, add `-DXENOMODS_SWITCH_IP=<console-ip>` while configuring the build.

## Project Credits

XenomodsTAS began as a fork of BlockBuilder's [BlockBuilder57/xenomods](https://github.com/BlockBuilder57/xenomods), but has since developed into a separately maintained project centered on TAS tooling and game research. Without him, this project wouldn't exist today! Thanks!

Special thanks to the original contributors of the Xenomods project:

- [BlockBuilder57](https://github.com/BlockBuilder57) — original XenoMods maintainer and contributor
- [modeco80](https://github.com/modeco80) — original XenoMods contributor
- [Frank/3096](https://github.com/3096) — Ether Flow, skyline-stuff, and the original event-specific freecam work
- [Shadów](https://github.com/shadowninja108) — exlaunch and its patching design
- [RoccoDev](https://github.com/RoccoDev) — imgui-xeno and debug-rendering research for Xenoblade Chronicles: Definitive Edition and Xenoblade Chronicles 3
- The contributors and maintainers of [Skyline](https://github.com/skyline-dev/skyline)

See [LICENSE](LICENSE) for licensing information
