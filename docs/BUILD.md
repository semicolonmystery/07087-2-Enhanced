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
| `SLEEP_IDLE_MS` | ms | **0** | Sleep backoff. **Must stay 0** — see [Sleep backoff](#sleep-backoff-must-stay-0) (mirrored in `zigbee_sleep_config.h`) |
| `TX_GRACE_MS` | ms | 500 | Documented TX-drain bound (stack-enforced); not referenced by code |
| `LONG_POLL_S` | s | 1800 | Parent poll interval when idle (mirrored in `end-device-support-config.h`, and in the Poll Control `LongPollInterval` attribute once that cluster exists) |
| `POLL_CTRL_CHECK_IN_QS` | qs | 14400 | Poll Control `CheckInInterval` (1 h) — ZAP attribute default |
| `POLL_CTRL_LONG_POLL_QS` | qs | 7200 | Poll Control `LongPollInterval` — ZAP attribute default, kept `= LONG_POLL_S * 4` |
| `POLL_CTRL_SHORT_POLL_QS` | qs | 2 | Poll Control `ShortPollInterval` (0.5 s) — ZAP attribute default |
| `POLL_CTRL_FAST_POLL_QS` | qs | 40 | Poll Control `FastPollTimeout` (10 s) — ZAP attribute default |
| `POLL_CTRL_LONG_POLL_MIN_QS` | qs | 20 | Poll Control `LongPollIntervalMin` (5 s floor) — ZAP attribute default |
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

## Sleep and power

### Sleep backoff (must stay 0)

`SLEEP_IDLE_MS` / `SL_ZIGBEE_APP_FRAMEWORK_BACKOFF_SLEEP_MS` reads like a
"how long to linger before sleeping" knob. It is not. Any non-zero value is a
severe battery leak, and the effect grows with `LONG_POLL_S`.

In `zigbee_app_framework_sleep.c:190-247`, a wake that lands inside the backoff
window takes the EM1 requirement and then arms the wake timer for the time to
the **next event** — not for the remainder of the backoff window. Nothing is
scheduled to re-evaluate when the backoff expires. So the sequence for an idle
remote is:

1. Long-poll timer fires, MCU leaves EM2, `lastWakeupMs` is stamped.
2. The poll completes in ~15 ms. Still inside the backoff window, so the EM1
   requirement is taken and the wake timer is armed for the next event — the
   next long poll, `LONG_POLL_S` away.
3. The MCU sits in **EM1 (~1.5 mA) for the entire poll interval**.
4. That wake is not an EM2 exit, so `lastWakeupMs` is unchanged, the backoff
   now reads as expired, and the next interval is spent in EM2 (~2 µA).
5. Repeat — a steady **~50 % EM1 duty cycle**, independent of `LONG_POLL_S`.

~0.75 mA average against ~1000 mAh of 2×AAA is a flat battery in roughly one
to two months, which is exactly the drain this firmware showed through v1.0.9.
With the backoff at 0 the device drops to EM2 as soon as it is idle and the
idle draw is dominated by EM2 retention plus the long poll — a few µA.

`app.c` fails the build if either constant drifts off 0.

### Poll Control server cluster (0x0020)

Lets the coordinator manage the poll rate and, via device-initiated check-ins,
keeps Z2M availability working for a sleepy device — a Z2M ping can never reach
a sleeping remote, because the parent only holds indirect messages for ~7.68 s.

This needs **both** a component and a ZCL cluster; either half alone is
silently useless, so `app.c` fails the build if only one is present.

1. **Software Components** → install **Poll Control Server Cluster**
   (`zigbee_poll_control_server`).
2. **ZCL editor** (`config/zcl/zcl_config.zap`) → endpoint 1 → enable
   **Poll Control (0x0020) server**, and enable the optional
   `LongPollIntervalMin` (0x0005) attribute.
3. Set the attribute defaults from `app_config.h` (all in quarter-seconds):

   The ZCL editor shows and expects **hex**, zero-padded to the attribute's
   width (8 digits for INT32U, 4 for INT16U). Type the `Default` column:

   | Attribute | ID | Type | Default (hex) | = decimal qs | Meaning | `app_config.h` |
   |---|---|---|---|---|---|---|
   | `CheckInInterval` | 0x0000 | INT32U | `0x00003840` | 14400 | 1 h | `POLL_CTRL_CHECK_IN_QS` |
   | `LongPollInterval` | 0x0001 | INT32U | `0x00001C20` | 7200 | 30 min | `POLL_CTRL_LONG_POLL_QS` |
   | `ShortPollInterval` | 0x0002 | INT16U | `0x0002` | 2 | 0.5 s | `POLL_CTRL_SHORT_POLL_QS` |
   | `FastPollTimeout` | 0x0003 | INT16U | `0x0028` | 40 | 10 s | `POLL_CTRL_FAST_POLL_QS` |
   | `LongPollIntervalMin` | 0x0005 | INT32U | `0x00000014` | 20 | 5 s floor | `POLL_CTRL_LONG_POLL_MIN_QS` |

   All five are inside the ZCL-defined min/max for their attribute, and satisfy
   the spec's ordering rules (`CheckInInterval >= LongPollInterval >=
   ShortPollInterval`, and `LongPollInterval >= LongPollIntervalMin`).

   **Leave `cluster revision` (0xFFFD) alone.** It is a mandatory global
   attribute that ZAP enables and fills in automatically from the cluster's
   `<globalAttribute>` in the SDK's ZCL definition — for Poll Control that is
   **3** (`0x0003`, int16u). It is not a tunable: it states which revision of
   the cluster spec the implementation follows, so hand-editing it only
   misdeclares the device. The same is true of every other cluster in this
   project, which is why their revisions differ (Level Control 1, Power
   Configuration / Identify / On-Off 2, Color Control / Poll Control 3).

Note that from then on these attributes are authoritative: at startup the
plugin pushes `LongPollInterval` / `ShortPollInterval` into the
end-device-support plugin (`poll-control-server.c:441-461`), overriding
`end-device-support-config.h`. Keep `POLL_CTRL_LONG_POLL_QS` equal to
`LONG_POLL_S * 4`; `app_config.h` enforces the plugin's constraint that the
long poll interval must not exceed the check-in interval.

### Device type

`SLI_ZIGBEE_PRIMARY_NETWORK_DEVICE_TYPE` must be **Sleepy End Device** — a plain
(non-sleepy) End Device keeps its receiver on when idle and never reaches EM2.
It is set in **Software Components → Zigbee → Zigbee Device Config**, which
writes `config/zigbee_device_config.h`.

`config/*.h` is the compiled truth. SLC copies a component's config file into
`config/` only if it is absent and then leaves it user-owned, so the values
there survive Studio's generate step. The `.slcp` `configuration:` block still
carries the values the project was *created* with — for this project that
includes a stale `SLI_ZIGBEE_NETWORK_DEVICE_TYPE_END_DEVICE` and
`EMBER_BINDING_TABLE_SIZE: 10`, neither of which reflects what is built. Those
entries are inert; they would only take effect if `config/` were deleted and
regenerated from scratch. Read `config/*.h`, not the `.slcp`, when checking
what a build actually uses.

The node type is also written to NVM3 when the device joins, so a device that
joined while built as a plain End Device stays one until it is re-paired
(ON+OFF for `PAIR_HOLD_MS`).

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
