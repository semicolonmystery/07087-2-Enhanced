# TS1001-CUS OTA image build & Zigbee2MQTT publishing

How to build a new firmware image and hand it to Z2M so the remote can update
over the air. The device checks for updates at most once a day on wake, or
immediately when you **hold PLUS+MINUS for 10 s** (the LED breathes while it
checks/downloads).

## Fixed identity (must match on both ends)

| Field | Value | Where it comes from |
|---|---|---|
| Manufacturer code | `0x1002` (4098) | ZAP default `EMBER_AF_MANUFACTURER_CODE`, `autogen/zap-config.h` |
| Image type | `0x0000` (0) | `EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_IMAGE_TYPE_ID`, `config/ota-client-policy-config.h` |
| Current running version | `0x01000000` | `FW_OTA_FILE_VERSION` in `app_config.h` (scheme `0xMMmmppbb`) |

The server (Z2M) only offers an image whose `manufacturerCode` + `imageType`
match **and** whose `fileVersion` is **greater** than what the remote reports.
So every release must bump the version.

## Release file naming

Built images are committed to this folder as:

```
TS1001-CUS-v<MAJOR>.<MINOR>.<PATCH>.ota
```

for example `TS1001-CUS-v1.0.2.ota`. Pushing a `.ota` file here to `main`
triggers a GitHub Action that regenerates `ota/index.json` automatically —
see [Publish to Z2M](#4-publish-to-z2m-hosted-ota-index) below.

## 1. Bump the version (both must move together — the build errors otherwise)

In `app_config.h`:
```c
#define FW_OTA_FILE_VERSION   0x01000200UL   // e.g. 1.0.1 -> 1.0.2
#define FW_VERSION_PATCH      2
#define FW_VERSION_STRING     "1.0.2"
```
In `config/ota-client-policy-config.h` (Studio owns this file; edit the value):
```c
#define EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION   0x01000200
```
Rebuild in Studio → produces the new `.../GNU ARM v12.2.1 - Default/Zigbee-Remote-_TYZB01_7qf81wty.s37`.

## 2. `.s37` → `.gbl` (Simplicity Commander, LZMA-compressed to fit the 192 KB slot)

Commander ships inside the Studio install, typically at:
`<SimplicityStudio>/developer/adapter_packs/commander/commander.exe`
(find yours; it is NOT on PATH). Then:
```
commander gbl create TS1001-CUS-v1.0.2.gbl \
    --app "Zigbee-Remote-_TYZB01_7qf81wty/GNU ARM v12.2.1 - Default/Zigbee-Remote-_TYZB01_7qf81wty.s37" \
    --compress lzma
```
The internal-storage bootloader on the device understands LZMA GBLs; compression
keeps the image well under the 192 KB storage slot (`0x44000..0x74000`).

## 3. `.gbl` → Zigbee `.ota` (image-builder, bundled in the SDK)

Tool: `SDKs/gecko_sdk/protocol/zigbee/tool/image-builder/image-builder-windows.exe`
(or `image-builder-linux` on the Pi). Verified flags:
```
image-builder-windows.exe --create TS1001-CUS-v1.0.2.ota \
    --manuf-id 0x1002 \
    --image-type 0x0000 \
    --version 0x01000200 \
    --string "TS1001-CUS" \
    --tag-id 0x0000 --tag-file TS1001-CUS-v1.0.2.gbl
```
`--tag-id 0x0000` is the "Upgrade Image" tag that wraps the GBL. `--version` is the
OTA header file version and MUST equal `FW_OTA_FILE_VERSION`.

Sanity-check the header:
```
image-builder-windows.exe --print TS1001-CUS-v1.0.2.ota
# expect: Manufacturer ID 0x1002, Firmware Version 0x01000200, Image Type 0x0000
```

## 4. Publish to Z2M (hosted OTA index)

Commit the `.ota` file into this folder and push to `main`:

```sh
git add ota/TS1001-CUS-v1.0.2.ota
git commit -m "ota: release v1.0.2"
git push
```

A GitHub Action ([`.github/workflows/ota-index.yml`](../.github/workflows/ota-index.yml))
scans every `.ota` file in this folder, reads each Zigbee OTA header, and
regenerates `ota/index.json` with the correct `fileSize`, `sha512`, and a raw
GitHub URL for each release — you don't need to compute or edit any of that
by hand.

Point Zigbee2MQTT at the hosted index in `configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: https://raw.githubusercontent.com/semicolonmystery/07087-2-Enhanced/main/ota/index.json
```

(If you're running your own fork or a local mirror, point this at your own
copy of `ota/index.json` instead.) Restart Z2M, or reload its OTA cache. The
remote's next check (daily-on-wake, or the 10 s PLUS+MINUS trigger) will see
the newer version.

## 5. Test the update

1. Confirm the remote is joined and shows firmware `0x01000000` (Z2M device → OTA tab
   / "Firmware version").
2. Hold **PLUS+MINUS for 10 s** → LED starts breathing; Z2M logs an image-block flow.
3. A ~250 KB image over a sleepy device takes a while (short-poll during download,
   several minutes). At completion the remote reboots; Z2M reports the new version
   `0x01000200`. The session is hard-capped at 10 min per attempt; a partial download
   resumes on the next trigger (the client keeps failed/partial downloads).

## Requirement for OTA *apply* to work — the Gecko bootloader must be flashed

Downloading lands the image in storage slot 0; the **Gecko bootloader** (from the
`bootloader-storage-internal-single-512k` project, region `0x0FE10000`) is what
verifies and installs it on reboot. If only the app was flashed and not the
bootloader, the download completes but the update never applies. Flash it once with
`tools/flash.sh boot` (or `tools/flash.sh all`) — see [`../FLASHING.md`](../FLASHING.md).
