/***************************************************************************//**
 * @file ota_trigger.c
 * @brief TS1001_TYZB01_7qf81wty_Enhanced OTA session control (F10, M9). Contract in ota_trigger.h.
 *
 * Verified SDK APIs (headers cited):
 *   emberAfOtaClientStartCallback() — kick off server discovery + query-next-
 *     image now; safe to call any time (no-op unless the client is idle /
 *     delaying / discovering) .... global-callback.h:877, ota-client.c:705-717
 *   sli_zigbee_af_ota_client_stop() — abort the session: finishes the download
 *     path with EMBER_AF_OTA_CLIENT_ABORTED, resets the state machine to
 *     BOOTLOAD_STATE_NONE and deactivates the OTA cluster tick (kills all
 *     pending plugin timers) ......... ota-client.h:237, ota-client.c:719-726
 *     (this is exactly what the CLI "plugin ota-client stop" runs,
 *      ota-client-cli.c:76)
 *   emberAfPluginOtaClientPreBootloadCallback — WEAK app hook fired right
 *     before the image is installed .. ota-client-cb.c:28, ota-client.c:2092
 *   emberAfReadClientAttribute ....... af.h:480
 *   ZCL_OTA_BOOTLOAD_CLUSTER_ID / ZCL_IMAGE_UPGRADE_STATUS_ATTRIBUTE_ID
 *     .................................. autogen/zap-id.h (:548 for the attr)
 *   OTA_UPGRADE_STATUS_* enum ......... app/framework/plugin/ota-common/ota.h:49
 *     The plugin mirrors its internal state into the ImageUpgradeStatus
 *     attribute on every transition (ota-client.c:979-996); states up to and
 *     including "Querying Next Image" read back as OTA_UPGRADE_STATUS_NORMAL —
 *     that is our "idle, nothing worth staying up for" signal.
 *   halCommonGetInt64uMillisecondTick — EM2-proof monotonic ms (same throttle
 *     pattern as battery.c) ............ legacy_hal/src/base-replacement.c:78-85
 *
 * Bootload path (verified, nothing to reinvent): server sends Upgrade End
 * Response -> runUpgrade() (ota-client.c:2092-2140) -> policy
 * emberAfOtaClientBootloadCallback (ota-client-policy.c:177) -> with the Slot
 * Manager present it boots storage slot 0 via emberAfPluginSlotManagerBootSlot
 * -> bootloader_setImagesToBootload + bootloader_rebootAndInstall. The
 * internal-storage Gecko bootloader then verifies the GBL in slot 0
 * (0x44000..0x74000) and installs it over the app area. Image verification
 * BEFORE the reboot is the policy's EBL verification (EBL_VERIFICATION=1),
 * which runs bootloader_initVerifyImage/continueVerifyImage through the Slot
 * Manager (ota-client-policy.c:115-144, slot-manager.c) — this is why the
 * Slot Manager component is a hard requirement (guarded by #if in app.c).
 ******************************************************************************/

#include "app/framework/include/af.h"
#include "app/framework/plugin/ota-client/ota-client.h"  // sli_..._ota_client_stop
#include "app/framework/plugin/ota-common/ota.h"         // OTA_UPGRADE_STATUS_*

#include "app_config.h"
#include "ota_trigger.h"
#include "led_effects.h"
#include "remote_zigbee.h"   // REMOTE_ENDPOINT

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static sl_zigbee_event_t s_cap_event;     // OTA_SESSION_MAX_S hard lifetime cap
static sl_zigbee_event_t s_check_event;   // OTA_QUERY_GRACE_S idle detector
static uint64_t s_last_query_ms;          // throttle timestamp (monotonic ms)
static bool     s_queried_once;           // first wake after boot queries now
static bool     s_session_active;         // between start and the end funnel

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** @brief True when the OTA client has nothing in flight: the plugin keeps the
 *         ImageUpgradeStatus attribute at OTA_UPGRADE_STATUS_NORMAL for every
 *         state up to "query sent"; download/verify/wait/countdown all differ. */
static bool ota_client_is_idle(void)
{
  uint8_t status = OTA_UPGRADE_STATUS_NORMAL;
  (void)emberAfReadClientAttribute(REMOTE_ENDPOINT,
                                   ZCL_OTA_BOOTLOAD_CLUSTER_ID,
                                   ZCL_IMAGE_UPGRADE_STATUS_ATTRIBUTE_ID,
                                   &status,
                                   sizeof(status));
  return status == OTA_UPGRADE_STATUS_NORMAL;
}

/** @brief The ONE session-end funnel (mirrors pairing_end() in app.c).
 *  @param why         log tag.
 *  @param stop_client true = abort the plugin state machine too (cap/idle
 *                     paths). false = leave it alone (about to bootload —
 *                     aborting there would send a spurious UpgradeEnd(ABORT)
 *                     to the server while the reboot is already committed). */
static void ota_session_end(const char *why, bool stop_client)
{
  if (!s_session_active) {
    return;
  }
  s_session_active = false;
  sl_zigbee_event_set_inactive(&s_cap_event);
  sl_zigbee_event_set_inactive(&s_check_event);
  led_effect_stop();                       // OTA breathing off (F8)
  if (stop_client) {
    sli_zigbee_af_ota_client_stop();       // resets state + kills plugin timers
  }
  TS_LOG("OTA: session end (%s)", why);
}

// ---------------------------------------------------------------------------
// Event handlers (DSR / main-loop context)
// ---------------------------------------------------------------------------

/** @brief Hard cap: OTA_SESSION_MAX_S after start, end no matter what. A
 *         mid-download abort keeps the partial image (policy
 *         DELETE_FAILED_DOWNLOADS=0) and the next session resumes from the
 *         saved offset — a large image completes across several sessions. */
static void ota_cap_handler(sl_zigbee_event_t *event)
{
  (void)event;
  ota_session_end("session cap", true);
}

/** @brief Idle check every OTA_QUERY_GRACE_S: discovery + query take seconds;
 *         if the client is back to NORMAL (no image offered, no server found,
 *         or a download that aborted on errors) there is nothing to wait for —
 *         end early instead of breathing the LED for the full cap. While a
 *         download/verify/upgrade-wait is in progress, keep checking. */
static void ota_check_handler(sl_zigbee_event_t *event)
{
  (void)event;
  if (!s_session_active) {
    return;
  }
  if (ota_client_is_idle()) {
    ota_session_end("idle", true);
  } else {
    sl_zigbee_event_set_delay_ms(&s_check_event, OTA_QUERY_GRACE_S * 1000UL);
  }
}

// ---------------------------------------------------------------------------
// Plugin callback override (weak default in ota-client-cb.c:28)
// ---------------------------------------------------------------------------

/** @brief Fired right before the downloaded image is installed. The device
 *         reboots into the bootloader moments later; close the session
 *         housekeeping (LED off, cancel cap) WITHOUT aborting the client. */
void emberAfPluginOtaClientPreBootloadCallback(uint8_t srcEndpoint,
                                               uint8_t serverEndpoint,
                                               EmberNodeId serverNodeId)
{
  (void)srcEndpoint;
  (void)serverEndpoint;
  (void)serverNodeId;
  TS_LOG("OTA: download verified -> installing, rebooting");
  ota_session_end("bootload", false);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ota_trigger_init(void)
{
  sl_zigbee_event_init(&s_cap_event, ota_cap_handler);
  sl_zigbee_event_init(&s_check_event, ota_check_handler);
  s_last_query_ms  = 0;
  s_queried_once   = false;
  s_session_active = false;
}

void ota_trigger_start(bool manual)
{
  if (emberAfNetworkState() != EMBER_JOINED_NETWORK) {
    TS_LOG("OTA: not joined, no query");
    return;
  }

  s_queried_once  = true;
  s_last_query_ms = halCommonGetInt64uMillisecondTick();

  TS_LOG("OTA: %s query session start",
                              manual ? "manual" : "daily");

  // Kick off discovery + query. If a session is already mid-download (e.g.
  // resumed by a server Image Notify) this is a no-op — but we still (re)arm
  // the cap/LED below, adopting that session into the bounded lifecycle.
  emberAfOtaClientStartCallback();

  s_session_active = true;
  led_effect_start(LED_EFFECT_OTA, true, 0);   // breathing until session end
  sl_zigbee_event_set_delay_ms(&s_cap_event, OTA_SESSION_MAX_S * 1000UL);
  sl_zigbee_event_set_delay_ms(&s_check_event, OTA_QUERY_GRACE_S * 1000UL);
}

void ota_trigger_on_wake(void)
{
#if OTA_AUTO_QUERY_ENABLED
  if (s_session_active) {
    return;                                // one session at a time
  }
  uint64_t now = halCommonGetInt64uMillisecondTick();
  if (s_queried_once
      && (now - s_last_query_ms)
         < (uint64_t)OTA_QUERY_MIN_INTERVAL_S * 1000u) {
    return;                                // throttled (F10): once per day
  }
  ota_trigger_start(false);
#else
  // Auto OTA query disabled (OTA_AUTO_QUERY_ENABLED=0) to conserve battery.
  // Updates are triggered only by the manual PLUS+MINUS combo.
  (void)s_last_query_ms;
  (void)s_queried_once;
#endif
}
