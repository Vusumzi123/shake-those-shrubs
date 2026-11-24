# Shake Those Shrubs

A small SKSE plugin for Skyrim SE: every placed shrub that matches the configured
rules tilts as you brush past it, and plays a rustle whose volume follows how
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
| `TagEditorID` | `Shrub,Bush,Fern,Thistle,Brush` | Base-form model path substrings (comma separated) that count as a shrub. |
| `Harvestable` | `1` | Treat harvestable plants (FLOR, or any base form with a produce item) as shrubs. |
| `BaseForms` | empty | Extra base forms to shake, comma separated: `Plugin.esp\|0xFormID` or a bare FormID. |
| `DumpBaseForms` | `0` | Write the matched set to `SKSE/Plugins/ShakeThoseShrubs_bases.txt`. |
| `TargetRef` | `0` | Optional single reference to animate instead of scanning for matching shrubs. |
| `Radius` | `90` | Flat distance at which a shrub reacts. |
| `Sound` | `0x00000DD0` | Sound descriptor played once per pass; `0` disables. |
| `SoundEnabled` | `1` | Boolean; `0`/`false`/`no`/`off` disable playback. |
| `SoundVolume` | `1.0` | Multiplier on the velocity-scaled volume. |
| `MinImpact` | `0.3` | Shake strength at zero speed. |
| `MaxImpact` | `1.0` | Shake strength at full speed. |
| `VelocityInfluence` | `1.0` | `1` maps strength across the speed range, `0` always uses full-speed strength. |
| `ImpactSpeed` | `350` | Speed considered "full". |
| `RetriggerCooldown` | `0.5` | Seconds between repeats while you stay in contact and keep moving (clamped to 0.1 minimum). |
| `MoveThreshold` | `20` | Speed below which a held contact does not repeat; standing still stays silent. |
| `YawDegrees` / `PitchDegrees` | | Tilt limits at strength 1. |
| `Speed` / `Spring` | | Wobble frequency and decay. |
| `LeanFraction` / `LeanRate` | | How far the shrub leans and how quickly it reaches it. |
| `Debug` | `0` | Log measured speed and active shrub count once per second. |

## What shakes

A placed reference shakes when its base form meets any of these:

- it is `BaseForm`;
- it is harvestable — FLOR, or any base form with a produce item — and
  `Harvestable=1`;
- its model path contains a `TagEditorID` token, case-insensitive.

The rule set is evaluated once at load over every STAT, TREE, FLOR and ACTI base
form, and the result is a base-form set: per reference the scan does one set
lookup, no string work. Matching reads the base form the reference uses at
runtime, so a Base Object Swapper replacement is matched as that replacement and
only shakes if it meets a rule.

STAT and TREE records carry no keyword field in the engine, so there is no
keyword or KID step. To cover something the rules miss:

- add a model-path token, e.g. `Maple`;
- list the base form in `BaseForms=`;
- drop a community patch into `Data/SKSE/Plugins/ShakeThoseShrubsPatches/` with
  `BaseForms=Plugin.esp|0x800` (several forms comma separated).

With `Debug=1` the plugin logs `near base ... model ...` for the first 64
unmatched plant base forms near you, which names the token or form to add. With
`DumpBaseForms=1` it writes the whole matched set so you can audit it.

## Build

Cross-compiles on Linux with clang-cl against a Windows sysroot.

```sh
VCPKG_ROOT=/path/to/vcpkg XWIN_SYSROOT=~/.cache/xwin/out cmake --preset linux-clangcl
ninja -C build/linux-clangcl
```

Output: `build/linux-clangcl/ShakeThoseShrubs.dll`.

## Licence

MIT.
