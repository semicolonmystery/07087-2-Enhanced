/***************************************************************************//**
 * @brief Zigbee OTA Bootload Cluster Client Policy component configuration header.
 *\n*******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

// <<< Use Configuration Wizard in Context Menu >>>

// <h>Zigbee OTA Bootload Cluster Client Policy configuration

// <o EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_IMAGE_TYPE_ID> Image Type ID <0-65535>
// <i> Default: 0
// <i> This is the device's OTA image identifier used for querying the OTA server about the next image to use for an upgrade.
#define EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_IMAGE_TYPE_ID   0

// <o EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION> Firmware Version <1-4294967295>
// <i> Default: 1
// <i> This is the device's current firmware version, used when querying the OTA server about the next image to use for an upgrade.
// M9 (F10): 1 -> 0x01000000 = FW_OTA_FILE_VERSION (app_config.h), scheme
// 0xMMmmppbb. MUST be bumped together with FW_OTA_FILE_VERSION for every OTA
// release (cross-checked by #if in app.c). The full OTA image identity is:
//   manufacturer code 0x1002 (EMBER_AF_MANUFACTURER_CODE, ZAP default kept
//   per project decision #4), image type 0x0000 (below), version this value.
#define EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION   0x01000400

// <o EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_HARDWARE_VERSION> Hardware Version <0-65535>
// <i> Default: 0
// <i> Devices may have a hardware version that limits what images they can use.  OTA Images may be configured with minimum and maximum hardware versions that they are supported on.  If the device is not restricted by hardware version then this value should be 0xFFFF.
#define EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_HARDWARE_VERSION   0

// <q EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_EBL_VERIFICATION> Perform EBL Verification (SOC Only)
// <i> Default: TRUE
// <i> This uses the application bootloader routines to verify the EBL image after signature verification passes.
#define EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_EBL_VERIFICATION   1

// <q EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_INCLUDE_HARDWARE_VERSION> Include Hardware Version
// <i> Default: FALSE
// <i> This indicates that the current hardware version of the product should be included in the messages sent to the ZigBee OTA Cluster server.
#define EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_INCLUDE_HARDWARE_VERSION   0

// <q EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_DELETE_FAILED_DOWNLOADS> Delete Failed Downloads
// <i> Default: TRUE
// <i> This causes the device to delete any image (partial or complete) that has been downloaded but did not pass verification or when the server tells us to abort the download or upgrade.
// M9 (F10): 1 -> 0. REQUIRED for the session cap to work: aborting a session
// (OTA_SESSION_MAX_S) funnels through emberAfOtaClientDownloadCompleteCallback
// with a non-success result (ota-client-policy.c:149) which, with this at 1,
// would ERASE the partial download every time — and a full image takes several
// capped sessions at sleepy-poll pace. With 0 the partial file is kept and the
// next session resumes from the saved offset ("Partial file download found",
// ota-client.c:911-919). Escape hatch for a genuinely bad stored image:
// CLI `plugin ota-storage-common delete 0` (or flash.sh erase of the slot).
// Cross-checked by #if in app.c.
#define EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_DELETE_FAILED_DOWNLOADS   0

// </h>

// <<< end of configuration section >>>
