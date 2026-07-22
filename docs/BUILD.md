# Building from source

## Requirements

- **Simplicity Studio v5**
- **Gecko SDK 4.4.6** (EmberZNet 7.4.x)
- **GNU ARM 12.2.1** toolchain

## Project setup

1. *File → Import* both projects into a Studio workspace:
   - `Zigbee-Remote-_TYZB01_7qf81wty` — the application
   - `bootloader-storage-internal-single-512k` — the Gecko bootloader
2. Build each project (hammer icon). If you change the `.slcp` (installed
   components) or the `.zap` (Zigbee clusters), run *Force Generation* /
   save first so `autogen/` regenerates.

**Important:** never hand-edit the `.slcp` file on disk while Studio is
open — Studio's `.pdm` cache silently reverts it. All component
(install/uninstall) changes must be made through the **Software Components**
GUI in Studio. Source files (`*.c` / `*.h` in the project root) and
everything in `config/*.h` are safe to edit directly on disk; they persist
and are picked up by the next build.

The application requires the **"OTA Cluster Platform Bootloader"** component
(`zigbee_ota_bootload`), which auto-installs **"Slot Manager"**. `app.c`
fails the build with a clear error if either is missing.

## Build artifacts (exact paths)

Application:

```
Zigbee-Remote-_TYZB01_7qf81wty/GNU ARM v12.2.1 - Default/Zigbee-Remote-_TYZB01_7qf81wty.s37
Zigbee-Remote-_TYZB01_7qf81wty/GNU ARM v12.2.1 - Default/Zigbee-Remote-_TYZB01_7qf81wty.hex
Zigbee-Remote-_TYZB01_7qf81wty/GNU ARM v12.2.1 - Default/Zigbee-Remote-_TYZB01_7qf81wty.bin
```

Bootloader (build once; the post-build step writes into `artifact/`):

```
bootloader-storage-internal-single-512k/artifact/bootloader-storage-internal-single-512k-combined.s37   <-- flash this one
bootloader-storage-internal-single-512k/artifact/bootloader-storage-internal-single-512k.s37             (main stage only — not flashable alone)
bootloader-storage-internal-single-512k/GNU ARM v12.2.1 - Default/bootloader-storage-internal-single-512k.{s37,hex}  (raw build output)
```

The `-combined.s37` contains the first stage plus the CRC'd main bootloader
for the Series-1 bootloader region; the plain `.s37`/`.hex` lacks the first
stage and won't boot on its own.

OTA artifacts (`.gbl` / `.ota`) are built separately — see
[`../ota/README.md`](../ota/README.md).

## Flash memory map (EFR32MG13P732F512GM48, 512 KB)

| Range | Size | Contents |
|---|---|---|
| `0x00000000 – 0x00044000` | 272 KB | Application (linker `ORIGIN=0x0`) |
| `0x00044000 – 0x00074000` | 192 KB | OTA storage slot 0 (internal storage bootloader) |
| `0x00077000 – 0x00080000` | 36 KB | NVM3 (network/keys/tokens — top of main flash) |
| `0x0FE00000` | 2 KB | User data page — manufacturing tokens. **Preserve — never erase** |
| `0x0FE10000` | 16 KB | Bootloader region (first stage + main bootloader; outside main flash) |

For how to flash these regions, see [`../FLASHING.md`](../FLASHING.md).

## `app_config.h` tuning reference

Every tunable of the firmware lives in
`Zigbee-Remote-_TYZB01_7qf81wty/app_config.h`. Compile-time checks in
`app.c` keep the Studio-owned mirrored copies in `config/*.h` in sync — if
you change a mirrored value without updating its counterpart, the build
tells you where.

| Define | Unit | Default | Effect |
|---|---|---|---|
| `DEBOUNCE_MS` | ms | 20 | Button debounce window |
| `DOUBLE_PRESS_MS` | ms | 300 | Window for the second press of a double click/hold |
| `HOLD_MS` | ms | 400 | Press duration that becomes a hold |
| `STUCK_BUTTON_MS` | ms | 20000 | No level change ⇒ button stuck: drop action, allow sleep |
| `ONE_CLICK_DISPATCH_DELAYED` | bool | 1 | Delay single click by `DOUBLE_PRESS_MS` to disambiguate doubles |
| `LEVEL_STEP` | units | 32 | Brightness change per PLUS/MINUS click |
| `LEVEL_STEP_TIME_DS` | 0.1 s | 1 | Transition time of a brightness step |
| `LEVEL_MOVE_RATE` | units/s | 50 | Brightness ramp rate while held |
| `CT_STEP_MIREDS` | mired | 30 | Color-temp change per double-click |
| `CT_STEP_TIME_DS` | 0.1 s | 1 | Transition time of a CT step |
| `CT_MOVE_RATE` | mired/s | 20 | CT ramp rate while double-held |
| `CT_MIN_MIREDS` / `CT_MAX_MIREDS` | mired | 153 / 500 | Bounds sent with every CT command |
| `COLOR_TEMP_PLUS_DIR` | 0/1 | 0 | 0: PLUS = cooler (fewer mireds); 1: reversed |
| `LED_RAMP_FAST_MS` | ms | 400 | Brightness-feedback blink cycle |
| `LED_RAMP_SLOW_MS` | ms | 1000 | CT-feedback blink cycle |
| `LED_BLINK_MS` | ms | 100 | Feedback blink length (ON=1, OFF=2 blinks) |
| `LED_PAIR_BLINK_MS` | ms | 200 | Pairing blink half-period |
| `LED_OTA_BREATHE_MS` | ms | 2000 | OTA breathing full period |
| `SLEEP_IDLE_MS` | ms | 2000 | Awake time after activity before EM2 sleep (mirrored in `zigbee_sleep_config.h`) |
| `TX_GRACE_MS` | ms | 500 | Documented TX-drain bound (stack-enforced) |
| `LONG_POLL_S` | s | 300 | Parent poll interval when idle (mirrored in `end-device-support-config.h`) |
| `BATTERY_SETTLE_MS` | ms | 10 | Divider settle time before ADC sample |
| `BATTERY_V_100` / `BATTERY_V_0` | V | 3.10 / 2.30 | 100% / 0% clamps of the battery curve |
| `BATTERY_DIVIDER_NUM/DEN` | – | 1/3 | Divider ratio VADC = VBAT·NUM/DEN |
| `BATTERY_MEASURE_MIN_INTERVAL_S` | s | 3600 | Battery measurement throttle |
| `ACTION_CACHE_MAX` | – | 10 | Offline cache cap (classes coalesce anyway) |
| `PAIR_HOLD_MS` | ms | 15000 | ON+OFF hold time for reset+pair |
| `PAIR_WINDOW_MS` | ms | 30000 | Join window length |
| `OTA_QUERY_MIN_INTERVAL_S` | s | 86400 | Auto OTA check at most once per this interval (mirrored: `ota-client-config.h` query delay) |
| `OTA_TRIGGER_HOLD_MS` | ms | 10000 | PLUS+MINUS hold for manual OTA check |
| `OTA_SESSION_MAX_S` | s | 600 | Hard cap on one OTA session (partial download kept) |
| `OTA_QUERY_GRACE_S` | s | 30 | Idle-session early end (query answered "no image") |
| `OTA_SLOT0_START/END` | addr | 0x44000/0x74000 | Flash-map ground truth (change only with the bootloader!) |
| `TX_POWER_DBM` | dBm | 10 | Radio TX power (mirrored in RAIL/steering configs) |
| `FW_OTA_FILE_VERSION` | – | 0x01000000 | OTA image version — bump together with the `ota-client-policy-config.h` firmware version for every release |
| `DEBUG_UART_ENABLED` | bool | 0 | Reserved; UART debug on PA0/TXD |
| `DEBUG_LOGGING` | bool | 0 | Master switch for this firmware's own RTT/console logs. 0 = production (all log calls compiled out — silent, zero cost). Set to 1 (and keep `iostream_rtt` + `zigbee_debug_print`) to restore logs |

### Production vs. debug build

The shipping firmware is silent: `DEBUG_LOGGING` is 0, so the app prints
nothing. To reclaim flash and drop the console entirely, uninstall these
components in the Studio Software Components GUI (all leaf dependencies,
reversible): `CLI` (example instance), `Zigbee Core CLI`, `Zigbee ZCL CLI`,
`Debug Print`, `IO Stream: RTT`, `IO Stream: Recommended Stream`. To debug
again: re-add `IO Stream: RTT` + `Debug Print`, set `DEBUG_LOGGING` to 1,
and reconnect SWD — see [`../FLASHING.md`](../FLASHING.md) for the debug
console setup.

## Repository layout

```
Zigbee-Remote-_TYZB01_7qf81wty/   application project (app.c, buttons.c,
                                  led_effects.c, remote_zigbee.c, battery.c,
                                  action_cache.c, ota_trigger.c, app_config.h)
bootloader-storage-internal-single-512k/   Gecko bootloader project
tools/                             flash.sh / debug.sh / efr32.cfg (Pi CM4 + OpenOCD)
z2m/ts1001-tyzb01-enhanced.js                  Zigbee2MQTT external converter
ota/                                OTA image build guide + hosted index
docs/api-reference.md              verified SDK API signatures
docs/BUILD.md                      this file
```
