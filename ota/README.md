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

1. Bump the version block in `app_config.h`. Since v1.0.11 the OTA file version
   follows the layout the Zigbee OTA spec actually defines —
   `app-release . app-build . stack-release . stack-build` — instead of
   `major.minor.patch.build`. (Under the old scheme the patch number sat in the
   *stack release* byte, so every release showed up in Z2M as an unchanged
   application with a moving "stack version": 0.5, 0.8, 0.10 …)

   ```c
   #define FW_VERSION_PATCH      12
   #define FW_VERSION_STRING     "1.0.12"   // -> Basic SW Build ID
   #define FW_DATE_CODE          "20260915" // -> Basic DateCode, YYYYMMDD
   #define FW_BUILD              12         // MUST increase every release
   #define FW_OTA_FILE_VERSION   0x010C0704UL
   //                              ^^ ^^ ^^^^
   //                              |  |  └── stack 7.4 (EmberZNet, unchanged)
   //                              |  └───── FW_BUILD (12 = 0x0C)
   //                              └──────── FW_VERSION_MAJOR
   ```

   `FW_BUILD` is a flat counter, deliberately not derived from
   major/minor/patch — one byte cannot hold all three without collisions
   (1.0.10 and 1.1.0 would tie). It is the only field that makes the file
   version grow, and an OTA is offered only when the file version is strictly
   greater than the running one.

   You do not have to compute `FW_OTA_FILE_VERSION` carefully by hand: `#if`
   checks in `app_config.h` fail the build if the literal disagrees with
   `MAJOR`/`FW_BUILD`/`FW_STACK_*`, or if it is not above the last version
   published under the old scheme (`0x01000A00`, v1.0.10). It is kept as a
   literal rather than an expression because `create_ota.py` parses it with the
   regex `FW_OTA_FILE_VERSION\s+0x([0-9a-fA-F]+)`.

   The Basic cluster attributes (SW Build ID, DateCode, ApplicationVersion,
   StackVersion) are written at boot from these constants by `app.c`, so there
   is nothing to update in the ZCL editor for a release.

2. Make sure you also update `config/ota-client-policy-config.h` (or via Studio GUI) to match — e.g. `0x010C0704`. A `#if` in `app.c` fails the build if the two disagree.
3. Build the project in Simplicity Studio to get the `.s37` (or `.hex` / `.bin`) file.
4. Copy the compiled `.s37` file into this `ota/` directory.
5. `git add`, `commit`, and `push` the `.s37` file to the `main` branch.

**Automated GitHub Action**
When you push the raw `.s37` file to `main`, a GitHub Action automatically:
- uses Simplicity Commander to compress it into an LZMA `.gbl`;
- wraps that in the 56-byte Zigbee OTA header, producing
  `TS1001_TYZB01_7qf81wty_Enhanced-v<MAJOR>.<MINOR>.<PATCH>.ota`;
- builds a single-entry `index.json` whose fields are read back out of the
  `.ota`'s own header, so the index can never disagree with the image;
- publishes both as assets of a **GitHub Release** tagged `v<MAJOR>.<MINOR>.<PATCH>`
  (re-runs update the existing release rather than failing);
- downloads the published assets again and verifies magic, size, sha512,
  fileVersion and manufacturer/image type before finishing;
- deletes your raw `.s37` from the repo.

Firmware binaries are **never committed to the repo** any more — `ota/` holds
this README and nothing else. Everything is served from Release assets, which is
what makes a new version visible to Z2M immediately.

**Local Build Alternative**
To produce the `.ota` locally without CI, drop the `.s37` into `ota/` and run the
provided Docker image from the repo root:
1. `docker build -t ota-builder tools/ota-builder`
2. `docker run --rm -v ${PWD}:/repo ota-builder`

This runs the same `create_ota.py` and leaves the image in `dist/`. It does not
produce an index — `index.json` needs the release-asset URL, which only exists
once the release is published, so build it separately if you need one:

```sh
python3 .github/scripts/build_ota_index.py dist/<name>.ota   --url "https://github.com/<owner>/<repo>/releases/download/<tag>/<name>.ota"   --model-id TS1001_TYZB01_7qf81wty_Enhanced   --manufacturer-name DIY-Immax --out dist/index.json
```
