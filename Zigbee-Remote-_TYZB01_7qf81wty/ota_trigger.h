/***************************************************************************//**
 * @file ota_trigger.h
 * @brief TS1001_TYZB01_7qf81wty_Enhanced OTA session control (F10, M9).
 *
 * The stock OTA client plugin is configured to never start or poll on its own
 * (AUTO_START off, periodic timers backstopped to 1/day — see
 * config/ota-client-config.h). This module is the ONLY thing that starts an
 * OTA query/download session, and it puts a hard lifetime bound on every one:
 *
 *   start   : PLUS+MINUS held OTA_TRIGGER_HOLD_MS (app.c combo timer, same
 *             pattern as pairing), or automatically on wake at most once per
 *             OTA_QUERY_MIN_INTERVAL_S (app.c calls ota_trigger_on_wake()
 *             next to battery_measure_on_wake()).
 *   during  : the plugin itself drives fast polling — every OTA tick is
 *             scheduled with EMBER_AF_SHORT_POLL while a response is pending
 *             (ota-client.c:477-490), and end-device-support takes the max
 *             poll control over all scheduled ZCL ticks (end-device-support.c:
 *             239-251) — so no explicit short-poll calls are needed here.
 *             LED: continuous breathing (LED_EFFECT_OTA) while the session
 *             lives.
 *   end     : ONE funnel (all paths):
 *             - idle check every OTA_QUERY_GRACE_S — "no image / no server /
 *               aborted" sessions end within seconds, LED off, sleep;
 *             - hard cap OTA_SESSION_MAX_S — aborts even a mid-download
 *               session (partial download is preserved, see
 *               DELETE_FAILED_DOWNLOADS=0 in ota-client-policy-config.h, and
 *               resumes next session);
 *             - about-to-bootload (emberAfPluginOtaClientPreBootloadCallback)
 *               — LED off, no client abort, device reboots into new image.
 ******************************************************************************/
#ifndef OTA_TRIGGER_H
#define OTA_TRIGGER_H

#include <stdbool.h>

/***************************************************************************//**
 * @brief One-time init (event objects). Call from emberAfMainInitCallback().
 ******************************************************************************/
void ota_trigger_init(void);

/***************************************************************************//**
 * @brief Start an OTA session now (manual trigger path; also used internally
 *        by the on-wake path). No-op when not joined. Arms the session cap +
 *        idle check and starts the OTA LED.
 * @param manual true = PLUS+MINUS combo (log flavor only).
 ******************************************************************************/
void ota_trigger_start(bool manual);

/***************************************************************************//**
 * @brief Auto-query hook: call on every wake (button event). Starts a session
 *        at most once per OTA_QUERY_MIN_INTERVAL_S (first wake after boot
 *        queries immediately — deliberately, so a freshly OTA'd unit confirms
 *        its new version on first use). No-op while a session is active or
 *        when not joined.
 ******************************************************************************/
void ota_trigger_on_wake(void);

#endif // OTA_TRIGGER_H
