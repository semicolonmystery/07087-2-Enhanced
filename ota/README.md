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

## 2. Publish to Z2M (Automated OTA build)

Instead of manually building the `.gbl` and `.ota` files using Silicon Labs tools, this repository uses a GitHub Action to automate it.

1. Locate the built `.s37` (or `.hex`/`.bin`) application image from Simplicity Studio:
   `Zigbee-Remote-_TYZB01_7qf81wty/GNU ARM v12.2.1 - Default/Zigbee-Remote-_TYZB01_7qf81wty.s37`
2. Copy it into this `ota/` folder and name it anything (e.g., `ota/update.s37`).
3. Commit and push it to `main`:

```sh
git add ota/update.s37
git commit -m "ota: release v1.0.2"
git push
```

A GitHub Action ([`.github/workflows/build-ota.yml`](../.github/workflows/build-ota.yml)) will automatically:
- Read `app_config.h` to determine the exact version number you set.
- Compress the binary into an LZMA `.gbl` using Simplicity Commander.
- Wrap it in a proper `.ota` file with the correct Zigbee OTA headers (`TS1001-CUS-v<MAJOR>.<MINOR>.<PATCH>.ota`).
- Regenerate `ota/index.json` with the new file size and SHA512.
- Remove your `.s37` file from the folder, commit the `.ota` and `index.json`, and push the result back to `main`.

You don't need to compute, wrap, or edit anything by hand.

### Local Docker Build

If you want to build the `.ota` file locally without installing Simplicity Commander on your machine, a Dockerfile is provided:
1. Build the image once: `docker build -t ota-builder tools/ota-builder`
2. Drop your `.s37` into the `ota/` folder.
3. Run the image from the repository root: `docker run --rm -v ${PWD}:/repo ota-builder`

This will generate the `.ota` file and clean up the `.s37` locally, exactly as the GitHub Action does.

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
