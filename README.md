# Shake Those Shrubs

A small SKSE plugin for Skyrim SE: every placed shrub that matches the configured
base form tilts as you brush past it, and plays a rustle whose volume follows how
fast you were moving.

Each shrub animates independently, so walking through a patch rocks each of them
in turn rather than one at a time.

## Requirements

- Skyrim Special Edition 1.7.104 (SKSE64 2.3.1)
- [SKSE64](https://skse.silverlock.org/)
- Optional: [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3)
  for the in-game settings page. Without it the plugin still works; settings come
  from the INI only.

## Install

Copy `SKSE/Plugins/ShakeThoseShrubs.dll` and `ShakeThoseShrubs.ini` into
`Data/SKSE/Plugins/`, or install the archive with your mod manager.

## Settings

`Data/SKSE/Plugins/ShakeThoseShrubs.ini`. Changes apply on game start; the menu
page edits the live values.

| Key | Default | Meaning |
| --- | --- | --- |
| `BaseForm` | `0x0009DAA2` | Base form matched against every reference (`TreePineShrub01Snow`). |
| `TargetRef` | `0` | Optional single reference to animate instead of scanning by base form. |
| `Radius` | `90` | Flat distance at which a shrub reacts. |
| `Sound` | `0x00000DD0` | Sound descriptor played once per pass; `0` disables. |
| `SoundEnabled` | `1` | Boolean; `0`/`false`/`no`/`off` disable playback. |
| `SoundVolume` | `1.0` | Multiplier on the velocity-scaled volume. |
| `MinImpact` | `0.3` | Shake strength at zero speed. |
| `MaxImpact` | `1.0` | Shake strength at full speed. |
| `VelocityInfluence` | `1.0` | `1` maps strength across the speed range, `0` always uses full-speed strength. |
| `ImpactSpeed` | `350` | Speed considered "full". |
| `YawDegrees` / `PitchDegrees` | | Tilt limits at strength 1. |
| `Speed` / `Spring` | | Wobble frequency and decay. |
| `LeanFraction` / `LeanRate` | | How far the shrub leans and how quickly it reaches it. |
| `Debug` | `0` | Log measured speed and active shrub count once per second. |

## Build

Cross-compiles on Linux with clang-cl against a Windows sysroot.

```sh
VCPKG_ROOT=/path/to/vcpkg XWIN_SYSROOT=~/.cache/xwin/out cmake --preset linux-clangcl
ninja -C build/linux-clangcl
```

Output: `build/linux-clangcl/ShakeThoseShrubs.dll`.

## Licence

MIT.
