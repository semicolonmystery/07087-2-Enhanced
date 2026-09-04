# Automatic OTA Updates via Zigbee2MQTT

Once you have flashed this custom firmware to your remote for the first time, you can perform all future updates Over-The-Air (OTA) directly through Zigbee2MQTT. You don't need to open the remote or use a debugger again (provided you also flashed the Gecko Bootloader during the initial install).

## 1. Configure Zigbee2MQTT

To tell Zigbee2MQTT where to find the latest firmware updates for this specific custom firmware, add the following line to your Zigbee2MQTT `configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: https://github.com/semicolonmystery/07087-2-Enhanced/releases/latest/download/index.json
```

*(If you already have other custom OTA index URLs, you can separate them with commas or as a list depending on your Z2M version, or just append this URL).*

After adding this, **restart Zigbee2MQTT**.

> **If you used this project before v1.0.12, you must change this URL.** It used
> to point at `raw.githubusercontent.com/.../ota/index.json`, and that file no
> longer exists. The old path was also actively harmful: raw URLs are served
> through a CDN with `Cache-Control: max-age=300`, so Z2M could keep reading a
> stale index for minutes after a release and offer you a version that did not
> match the image it then downloaded. `releases/latest/download/` always
> resolves to the newest release's index.

## 2. Triggering an Update

Because this remote is a "sleepy end device," it only turns on its radio when a button is pressed or once a day to check in. To update it:

1. In the Zigbee2MQTT dashboard, go to the **OTA** tab.
2. Find your remote (`TS1001_TYZB01_7qf81wty_Enhanced`) and click **Check for new updates**.
3. **Immediately wake the remote** by pressing any button so it receives the check request.
4. If an update is available, click **Update device**.
5. The device might go back to sleep before the download starts. To force it to stay awake and fetch the update, **hold PLUS + MINUS together for 10 seconds**.
6. The LED on the remote will start **breathing**, indicating that the OTA download is in progress.

The transfer takes roughly **11-13 minutes**. Zigbee moves one 63-byte block
per MAC data poll, and the remote polls every 250 ms while a block request is
outstanding (the `ShortPollInterval` attribute of the Poll Control cluster), so
~166 KB works out to about 2,650 round trips. The remote reboots into the new
firmware automatically once the download is verified and installed.

There is no fixed time limit on a session. A download is abandoned only if it
makes **no progress at all** for ~3 minutes; a partial image is saved either way
and resumes the next time you trigger an update. (Before v1.0.12 there was a
hard 10-minute cap, which aborted every healthy transfer mid-flight and made one
update look like several failed ones.)

> **Note**: For the OTA to successfully *apply* after downloading, the **Gecko bootloader** must be present on the device. If you skipped flashing the bootloader during your initial wired installation, the remote will download the update but fail to install it.

---

### 🛠️ Under the Hood: Building and Releasing OTA (For Developers)

*(This section is only relevant if you are modifying the C code and compiling your own firmware versions).*

To release a new OTA image for the remote, the `fileVersion` must be strictly greater than what the remote is currently running. Both ends (Z2M and the remote) check the identity:
- **Manufacturer Code**: `0x1002` (DIY-Immax)
- **Image Type**: `0x0000`

**To publish a new version:**

1. Push a version tag from the current tip of `main`:

   ```sh
   git tag v1.0.16
   git push origin v1.0.16
   ```

   That's the whole manual part. `.github/workflows/release.yml` takes over
   from there — you do **not** hand-edit `app_config.h`'s version fields or
   build anything locally for a normal release.

**What the release workflow does (`.github/workflows/release.yml`):**

1. **Bumps the version.** `.github/scripts/bump_version.py` parses
   `MAJOR.MINOR.PATCH` from the tag and updates `app_config.h`'s
   `FW_VERSION_MAJOR/MINOR/PATCH`, `FW_VERSION_STRING`, and `FW_DATE_CODE` —
   plus, since v1.0.11, the OTA file version follows the layout the Zigbee
   OTA spec actually defines, `app-release . app-build . stack-release .
   stack-build`, rather than `major.minor.patch.build` (under the old scheme
   the patch number sat in the *stack release* byte, so every release showed
   up in Z2M as an unchanged application with a moving "stack version": 0.5,
   0.8, 0.10 …). `FW_BUILD` — the flat build counter that actually makes the
   file version grow — is read from the current file and incremented by 1,
   **not** derived from the tag's patch number: one byte can't hold
   major/minor/patch without collisions (1.0.10 and 1.1.0 would tie), and
   deriving it from PATCH would reset it to 0 on the next minor/major bump.
   `FW_OTA_FILE_VERSION` is recomputed from `MAJOR`/`FW_BUILD`/`FW_STACK_*`
   and written as the hex literal `create_ota.py` parses with the regex
   `FW_OTA_FILE_VERSION\s+0x([0-9a-fA-F]+)`. The same value is also written
   into `config/ota-client-policy-config.h`'s
   `EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION` — `app.c` has a
   compile-time `#if` that fails the build if these two ever disagree. Both
   invariants `app_config.h`'s own `#if` guards enforce (new value strictly
   greater than the current one; new value above the last version published
   under the old scheme, `0x01000A00` = v1.0.10) are re-checked here too, so
   a bad bump fails in CI with a clear message rather than as a compile
   error three steps later.

   The Basic cluster attributes (SW Build ID, DateCode, ApplicationVersion,
   StackVersion) are written at boot from these constants by `app.c`, so
   there is nothing to update in the ZCL editor for a release.

2. **Commits the bump and moves the tag.** The version-bump commit goes to
   `main`, and the tag is force-moved to point at it — so the tag, the
   released binary, and the committed source always agree.
3. **Builds the app from source.** `tools/slc-install.sh` +
   `tools/slc-build.sh` compile the bumped commit headlessly with `slc-cli`
   — see [`../docs/BUILD.md`](../docs/BUILD.md)'s "CI / CLI build" section.
   Studio is not involved.
4. Everything from here on is unchanged from before:
   - Simplicity Commander compresses the build into an **LZ4** `.gbl` (LZMA
     doesn't fit — see [`../docs/BUILD.md`](../docs/BUILD.md)'s "OTA
     compression" section for why);
   - wraps that in the 56-byte Zigbee OTA header, producing
     `TS1001_TYZB01_7qf81wty_Enhanced-v<MAJOR>.<MINOR>.<PATCH>.ota`;
   - two hard gates refuse the release if it doesn't actually fit flash: the
     uncompressed app image must not run past the OTA slot's start address,
     and the compressed `.ota` must not exceed the slot's size;
   - builds a single-entry `index.json` whose fields are read back out of the
     `.ota`'s own header, so the index can never disagree with the image;
   - publishes both as assets of a **GitHub Release** tagged `v<MAJOR>.<MINOR>.<PATCH>`
     (re-runs update the existing release rather than failing);
   - downloads the published assets again and verifies magic, size, sha512,
     fileVersion and manufacturer/image type before finishing.

Firmware binaries are **never committed to the repo** — `ota/` is only ever a
transient staging spot inside the CI run. Everything is served from Release
assets, which is what makes a new version visible to Z2M immediately.

**Local Build Alternative**
To produce the `.ota` locally without CI, drop a `.s37` into `ota/` — from
`../build.sh` (headless `slc-cli` build, no Studio) or from Studio directly —
and run the provided Docker image from the repo root:
1. `docker build -t ota-builder tools/ota-builder`
2. `docker run --rm -v ${PWD}:/repo ota-builder`

This runs the same `create_ota.py` and leaves the image in `dist/`. It does not
produce an index — `index.json` needs the release-asset URL, which only exists
once the release is published, so build it separately if you need one:

```sh
python3 .github/scripts/build_ota_index.py dist/<name>.ota   --url "https://github.com/<owner>/<repo>/releases/download/<tag>/<name>.ota"   --model-id TS1001_TYZB01_7qf81wty_Enhanced   --manufacturer-name DIY-Immax --out dist/index.json
```
