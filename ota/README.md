# Automatic OTA Updates via Zigbee2MQTT

Once you have flashed this custom firmware to your remote for the first time, you can perform all future updates Over-The-Air (OTA) directly through Zigbee2MQTT. You don't need to open the remote or use a debugger again (provided you also flashed the Gecko Bootloader during the initial install).

## 1. Configure Zigbee2MQTT

To tell Zigbee2MQTT where to find the latest firmware updates for this specific custom firmware, add the following line to your Zigbee2MQTT `configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: https://raw.githubusercontent.com/semicolonmystery/07087-2-Enhanced/refs/heads/main/ota/index.json
```

*(If you already have other custom OTA index URLs, you can separate them with commas or as a list depending on your Z2M version, or just append this URL).*

After adding this, **restart Zigbee2MQTT**.

## 2. Triggering an Update

Because this remote is a "sleepy end device," it only turns on its radio when a button is pressed or once a day to check in. To update it:

1. In the Zigbee2MQTT dashboard, go to the **OTA** tab.
2. Find your remote (`TS1001_TYZB01_7qf81wty_Enhanced`) and click **Check for new updates**.
3. **Immediately wake the remote** by pressing any button so it receives the check request.
4. If an update is available, click **Update device**.
5. The device might go back to sleep before the download starts. To force it to stay awake and fetch the update, **hold PLUS + MINUS together for 10 seconds**.
6. The LED on the remote will start **breathing**, indicating that the OTA download is in progress.

A ~250 KB image over Zigbee takes several minutes. The remote will automatically reboot into the new firmware once the download is fully verified and installed. The update session is hard-capped at 10 minutes per attempt; if it fails or goes to sleep, a partial download is saved and will resume the next time you trigger it.

> **Note**: For the OTA to successfully *apply* after downloading, the **Gecko bootloader** must be present on the device. If you skipped flashing the bootloader during your initial wired installation, the remote will download the update but fail to install it.

---

### 🛠️ Under the Hood: Building and Releasing OTA (For Developers)

*(This section is only relevant if you are modifying the C code and compiling your own firmware versions).*

To release a new OTA image for the remote, the `fileVersion` must be strictly greater than what the remote is currently running. Both ends (Z2M and the remote) check the identity:
- **Manufacturer Code**: `0x1002` (DIY-Immax)
- **Image Type**: `0x0000`

**To publish a new version:**
1. Bump the version in `app_config.h`:
   ```c
   #define FW_OTA_FILE_VERSION   0x01000300UL   // e.g., bump to 1.0.3
   #define FW_VERSION_PATCH      3
   #define FW_VERSION_STRING     "1.0.3"
   ```
2. Make sure you also update `config/ota-client-policy-config.h` (or via Studio GUI) to match `0x01000300`.
3. Build the project in Simplicity Studio to get the `.s37` (or `.hex` / `.bin`) file.
4. Copy the compiled `.s37` file into this `ota/` directory.
5. `git add`, `commit`, and `push` the `.s37` file to the `main` branch.

**Automated GitHub Action**
When you push the raw `.s37` file to `main`, a GitHub Action automatically runs:
- It uses Simplicity Commander to compress the `.s37` into an LZMA `.gbl`.
- It wraps the `.gbl` with the 56-byte Zigbee OTA header, creating `TS1001_TYZB01_7qf81wty_Enhanced-v<MAJOR>.<MINOR>.<PATCH>.ota`.
- It rewrites `index.json` to point to the newly generated `.ota` file.
- It deletes your raw `.s37` and commits the final OTA artifacts back to the repo.

**Local Build Alternative**
If you prefer to build the OTA image locally, drop the `.s37` into the `ota/` folder and run the provided Docker image from the repo root:
1. `docker build -t ota-builder tools/ota-builder`
2. `docker run --rm -v ${PWD}:/repo ota-builder`
This executes the exact same pipeline locally and updates `index.json`.
