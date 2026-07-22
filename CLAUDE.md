# CLAUDE.md

Guidance for Claude Code (and other agents) working in this repository.

## What this is

TS1001-CUS is open-source EmberZNet (Zigbee 3.0) firmware for the Immax NEO
Smart Remote v2, a four-button handheld remote built on a Tuya TYZS3 module
(Silicon Labs EFR32MG13). It replaces the stock Tuya firmware with a fully
local implementation that drives lights and groups through the Zigbee
binding table, designed for use with Zigbee2MQTT. The firmware is complete
and hardware-verified; most work in this repository is documentation,
tooling, and incremental firmware changes rather than a from-scratch build.

## Repository map

```
Zigbee-Remote-_TYZB01_7qf81wty/          Simplicity Studio application project
bootloader-storage-internal-single-512k/ Simplicity Studio bootloader project
tools/                                   flash.sh, debug.sh, efr32.cfg — Pi CM4 + OpenOCD workflow
docs/                                    BUILD.md, api-reference.md, images/
ota/                                     OTA image build guide + hosted index.json
z2m/                                     Zigbee2MQTT external converter (ts1001-cus.js)
.github/                                 CI: rebuilds ota/index.json when a .ota is pushed
```

The two Simplicity Studio project directories are managed by Studio itself —
see the build workflow below before editing anything inside them.

## Build workflow

The project builds in **Simplicity Studio v5** against **Gecko SDK 4.4.6**
(EmberZNet 7.4.x) with the **GNU ARM 12.2.1** toolchain. There is no
command-line build — an agent working in this repository cannot build the
firmware; changes need to be verified by building in Studio.

Two workspace rules matter here:

- **Component changes go through Studio's GUI.** Installing or uninstalling
  a software component (`.slcp`) or editing Zigbee clusters (`.zap`) must be
  done through Studio's Software Components UI, not by hand-editing the
  files on disk. Studio's `.pdm` cache regenerates the `.slcp` from its own
  state and will silently revert on-disk edits.
- **Source and config files are safe to edit directly.** `*.c` / `*.h` in
  the project root, and everything under `config/*.h`, persist normally and
  are picked up by the next Studio build.

Build artifacts, the flash memory map, and the full `app_config.h` tuning
reference live in [docs/BUILD.md](docs/BUILD.md). Flashing and debugging
over SWD from a Raspberry Pi are covered in [FLASHING.md](FLASHING.md).

## Architecture

The application is an event-driven EmberZNet SoC app. Each source file owns
one concern:

- **`buttons.c`** — debounced button state machine; classifies raw GPIO
  edges into click / hold / double-click / double-hold gestures per button.
- **`led_effects.c`** — non-blocking timer-driven LED engine (blinks, ramps,
  pairing flash, OTA breathing) so feedback never blocks the event loop.
- **`remote_zigbee.c`** — translates gestures into ZCL commands and sends
  them across the binding table (On/Off, Level Control, Color Control).
- **`battery.c`** — ADC-based battery voltage measurement and percentage
  curve, reported over the Power Configuration cluster.
- **`action_cache.c`** — coalesces button actions taken while the
  coordinator is unreachable and replays them once the network is back.
- **`ota_trigger.c`** — manages OTA session lifecycle: manual trigger,
  daily auto-check, session time bounds, resume of partial downloads.
- **`app.c`** — wires the above together: event dispatch, sleep policy, and
  the pairing/factory-reset combo (hold ON+OFF).

`app_config.h` is the single source of truth for tunable constants —
timings, thresholds, rates, and protocol identifiers all live there. Several
values are mirrored into Studio-generated `config/*.h` files; `app.c`
contains compile-time checks that fail the build if a mirrored pair drifts
out of sync.
