/***************************************************************************//**
 * @file app.c
 * @brief TS1001_TYZB01_7qf81wty_Enhanced custom remote — application entry & Zigbee stack callbacks.
 *
 * M0 baseline: the example's ZLL / touch-link / find-and-bind / simple-button /
 * simple-led code has been removed (those components are gone from the .slcp).
 * This file is intentionally a minimal, compiling skeleton. Subsequent
 * milestones add the pieces:
 *   M1  buttons.c/h        — GPIO button driver + debounced state machine
 *   M2  led_effects.c/h    — timer/PWM LED effect engine (PA5)
 *   M3  remote_zigbee.c/h  — On/Off + Level commands to bindings
 *   M4  sleep policy / power-manager audit
 *   M5  Color Control (color temperature)
 *   M6  battery.c/h        — ADC battery measurement + Power Config reporting
 *   M7  action_cache.c/h   — offline action cache with coalescing
 *   M8  pairing/reset combo (F9) — the only network-steering trigger
 *   M9  OTA client
 * See PLAN.md and the implementation brief for details.
 *******************************************************************************
 * # License
 * <b>Copyright 2021 Silicon Laboratories Inc. www.silabs.com</b>
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

#include "app/framework/include/af.h"
#include "network-steering.h"
#include "app_config.h"
#include "buttons.h"
#include "led_effects.h"
#include "remote_zigbee.h"   // also provides REMOTE_ENDPOINT
#include "battery.h"         // M6: measure-on-wake + Power Config attrs
#include "action_cache.h"    // M7: offline action cache (flush on NETWORK_UP)
#include "ota_trigger.h"     // M9: OTA session control (F10)

//----------------------
// M4 (F4) compile-time cross-checks: the sleep/poll/TX-power numbers live in
// component config headers that Studio owns, so they cannot #include
// app_config.h themselves. Instead we pull them in here (config/ is on the
// include path; redefinition is harmless because the values are identical
// token-for-token) and fail the build if they ever drift from app_config.h.
#include "end-device-support-config.h"   // EMBER_AF_PLUGIN_END_DEVICE_SUPPORT_*
#include "zigbee_sleep_config.h"         // SL_ZIGBEE_APP_FRAMEWORK_*
#include "network-steering-config.h"     // EMBER_AF_PLUGIN_NETWORK_STEERING_*
#include "sl_rail_util_pa_config.h"      // SL_RAIL_UTIL_PA_POWER_DECI_DBM

#if EMBER_AF_PLUGIN_END_DEVICE_SUPPORT_LONG_POLL_INTERVAL_SECONDS != LONG_POLL_S
#error "end-device-support-config.h long poll interval != LONG_POLL_S (app_config.h)"
#endif
#if SL_ZIGBEE_APP_FRAMEWORK_BACKOFF_SLEEP_MS != SLEEP_IDLE_MS
#error "zigbee_sleep_config.h sleep backoff != SLEEP_IDLE_MS (app_config.h)"
#endif
#if SL_ZIGBEE_APP_FRAMEWORK_STAY_AWAKE_WHEN_NOT_JOINED != 0
#error "F4: stay-awake-when-not-joined must be 0 or an unjoined remote never sleeps"
#endif
#if EMBER_AF_PLUGIN_NETWORK_STEERING_RADIO_TX_POWER != TX_POWER_DBM
#error "network-steering-config.h radio TX power != TX_POWER_DBM (app_config.h)"
#endif
#if SL_RAIL_UTIL_PA_POWER_DECI_DBM != (TX_POWER_DBM * 10)
#error "sl_rail_util_pa_config.h initial PA power != TX_POWER_DBM (app_config.h)"
#endif

//----------------------
// M9 (F10) compile-time cross-checks — same pattern as the M4 block above.
// The OTA plugin configs live in config/*.h owned by Studio; pull them in and
// fail the build on any drift from app_config.h / the required policy.
#include "sl_component_catalog.h"
#include "ota-client-config.h"           // EMBER_AF_PLUGIN_OTA_CLIENT_*
#include "ota-client-policy-config.h"    // EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_*
#include "ota-storage-simple-eeprom-config.h"

// The image is applied and pre-verified through the bootloader via the Slot
// Manager: the "OTA Cluster Platform Bootloader" component (zigbee_ota_bootload)
// provides the SoC bootload call (ota-bootload-soc.c) and pulls Slot Manager in
// as a dependency; without Slot Manager the client policy's EBL verification
// unconditionally fails every download (ota-client-policy.c:212-227).
#if !defined(SL_CATALOG_ZIGBEE_OTA_BOOTLOAD_PRESENT) \
  || !defined(SL_CATALOG_SLOT_MANAGER_PRESENT)
#error "M9: install 'OTA Cluster Platform Bootloader' (zigbee_ota_bootload) in the Studio SOFTWARE COMPONENTS GUI — it auto-installs its 'Slot Manager' dependency. Without it a downloaded image is never verified or applied."
#endif

#if EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION != FW_OTA_FILE_VERSION
#error "ota-client-policy-config.h firmware version != FW_OTA_FILE_VERSION (app_config.h) — bump BOTH for every OTA release"
#endif
#if EMBER_AF_PLUGIN_OTA_CLIENT_AUTO_START != 0
#error "F10: OTA client auto-start must be 0 — only ota_trigger.c starts sessions"
#endif
#if (EMBER_AF_PLUGIN_OTA_CLIENT_QUERY_DELAY_MINUTES * 60) != OTA_QUERY_MIN_INTERVAL_S
#error "ota-client-config.h query delay != OTA_QUERY_MIN_INTERVAL_S (app_config.h)"
#endif
#if EMBER_AF_PLUGIN_OTA_CLIENT_POLICY_DELETE_FAILED_DOWNLOADS != 0
#error "F10: delete-failed-downloads must be 0 or the session cap erases partial downloads (see ota-client-policy-config.h note)"
#endif
#if EMBER_AF_PLUGIN_OTA_STORAGE_SIMPLE_EEPROM_STORAGE_START != OTA_SLOT0_START \
  || EMBER_AF_PLUGIN_OTA_STORAGE_SIMPLE_EEPROM_STORAGE_END != OTA_SLOT0_END
#error "OTA storage offsets != bootloader slot 0 (OTA_SLOT0_* in app_config.h; ground truth btl_storage_slot_cfg.h)"
#endif
#if OTA_TRIGGER_HOLD_MS >= STUCK_BUTTON_MS
#error "F10: OTA trigger hold must stay below the stuck-button cap"
#endif

//----------------------
// F9 pairing / reset (M8) — the ONLY path that ever starts network steering.
//
// ON+OFF held for PAIR_HOLD_MS -> leave network + clear bindings -> steer for
// at most PAIR_WINDOW_MS with the pairing LED effect. On join success OR window
// expiry: stop steering, stop the LED, go idle (sleep). Steering never starts
// any other way (not at power-on, not on rejoin failure).
//
// emberLeaveNetwork() is asynchronous (network-formation.h:186): when we are
// still joined, steering is deferred until the EMBER_NETWORK_DOWN stack status
// arrives; when there is no network, steering starts immediately.
// emberClearBindingTable(): binding-table.h:115. Steering start/stop:
// network-steering.h:170/178.

static sl_zigbee_event_t pairing_hold_event;    // fires PAIR_HOLD_MS into the combo
static sl_zigbee_event_t pairing_window_event;  // hard cap: PAIR_WINDOW_MS of steering
static sl_zigbee_event_t ota_hold_event;        // fires OTA_TRIGGER_HOLD_MS into the
                                                // PLUS+MINUS combo (M9, F10)
static bool pairing_active;                     // combo confirmed, pairing in progress
static bool pairing_wait_net_down;              // leave issued; steer on NETWORK_DOWN

static void pairing_begin_steering(void)
{
  pairing_wait_net_down = false;
  EmberStatus status = emberAfPluginNetworkSteeringStart();
  TS_LOG("Pairing: steering start 0x%02X", status);
  (void)status;   // only consumed by TS_LOG (compiled out in production)
  sl_zigbee_event_set_delay_ms(&pairing_window_event, PAIR_WINDOW_MS);
}

static void pairing_end(const char *why)
{
  if (!pairing_active) {
    return;
  }
  pairing_active = false;
  pairing_wait_net_down = false;
  sl_zigbee_event_set_inactive(&pairing_window_event);
  led_effect_stop();
  TS_LOG("Pairing: end (%s)", why);
}

/** ON+OFF have been held for the full PAIR_HOLD_MS -> reset & pair. */
static void pairing_hold_handler(sl_zigbee_event_t *event)
{
  (void)event;
  if (!buttons_combo_active(BTN_COMBO_ON_OFF) || pairing_active) {
    return;   // released just before the deadline, or already pairing
  }
  pairing_active = true;
  TS_LOG("Pairing: combo held %d ms -> reset", PAIR_HOLD_MS);
  led_effect_start(LED_EFFECT_PAIRING, true, 0);
  (void)emberClearBindingTable();

  if (emberAfNetworkState() == EMBER_NO_NETWORK) {
    pairing_begin_steering();
  } else {
    pairing_wait_net_down = true;
    EmberStatus status = emberLeaveNetwork();
    TS_LOG("Pairing: leave 0x%02X", status);
    if (status != EMBER_SUCCESS) {
      pairing_begin_steering();   // no leave in flight -> steer right away
    }
  }
}

/** PAIR_WINDOW_MS elapsed without a join -> stop steering, sleep. */
static void pairing_window_handler(sl_zigbee_event_t *event)
{
  (void)event;
  if (pairing_active) {
    (void)emberAfPluginNetworkSteeringStop();
    pairing_end("window expired");
  }
}

/** PLUS+MINUS held for the full OTA_TRIGGER_HOLD_MS -> manual OTA query (F10).
 *  Same pattern as pairing_hold_handler: the timer only proves intent — verify
 *  the combo is STILL held when it fires. Session lifetime (LED, cap, idle
 *  check) is owned by ota_trigger.c. */
static void ota_hold_handler(sl_zigbee_event_t *event)
{
  (void)event;
  if (!buttons_combo_active(BTN_COMBO_PLUS_MINUS) || pairing_active) {
    return;   // released just before the deadline, or pairing owns the device
  }
  TS_LOG("OTA: combo held %d ms -> query", OTA_TRIGGER_HOLD_MS);
  ota_trigger_start(true);
}

/** Combo transitions from buttons.c (DSR context). */
static void app_combo_event(uint8_t combo, bool active)
{
  if (combo == BTN_COMBO_ON_OFF) {
    if (active) {
      sl_zigbee_event_set_delay_ms(&pairing_hold_event, PAIR_HOLD_MS);
    } else if (!pairing_active) {
      sl_zigbee_event_set_inactive(&pairing_hold_event);  // released early
    }
  } else if (combo == BTN_COMBO_PLUS_MINUS) {
    // M9: OTA query trigger (OTA_TRIGGER_HOLD_MS)
    if (active) {
      sl_zigbee_event_set_delay_ms(&ota_hold_event, OTA_TRIGGER_HOLD_MS);
    } else {
      sl_zigbee_event_set_inactive(&ota_hold_event);      // released early
    }
  }
}

//----------------------
// Implemented Callbacks

/** @brief M1/M2/M3/M5 button event sink.
 *
 * Each gesture drives its LED effect (F8, M2) AND its Zigbee action (M3/M5,
 * remote_zigbee.c — all commands go to the binding table). The debug log stays
 * for RTT-side verification.
 *
 * Mapping (F5/F6/F8):
 *   ON  click            -> On  + 1 blink   OFF click -> Off + 2 blinks
 *   PLUS click           -> Level Step up   + fast ramp-up once
 *   PLUS hold            -> Level Move up   + fast ramp-up repeating;
 *                           release -> Level Stop + effect stop
 *   PLUS double-click    -> CT Step + slow ramp-up once
 *   PLUS double-hold     -> CT Move + slow ramp-up repeating;
 *                           release -> CT Stop + effect stop
 *   MINUS                -> same with down direction / ramp-DOWN effects
 *   (CT direction: COLOR_TEMP_PLUS_DIR in app_config.h decides whether the
 *   PLUS gesture means cooler (fewer mireds, default) or warmer.)
 */
static void app_button_event(uint8_t button, uint8_t event)
{
  // M6 (F3): any button event means we are awake — measure the battery if the
  // hourly throttle allows (first event after boot measures immediately).
  battery_measure_on_wake();

  // M9 (F10): same piggyback for the auto OTA query — at most once per
  // OTA_QUERY_MIN_INTERVAL_S (daily), never while pairing owns the device.
  if (!pairing_active) {
    ota_trigger_on_wake();
  }

  TS_LOG("btn %d evt %d", button, event);

  switch (button) {
    case BTN_ON:
      if (event == BTN_CLICK) {
        led_effect_start(LED_EFFECT_BLINK, false, 1);
        (void)remote_send_on();
      }
      break;

    case BTN_OFF:
      if (event == BTN_CLICK) {
        led_effect_start(LED_EFFECT_BLINK, false, 2);
        (void)remote_send_off();
      }
      break;

    case BTN_PLUS:
    case BTN_MINUS: {
      bool up = (button == BTN_PLUS);
      led_effect_t fast = up ? LED_EFFECT_RAMP_UP_FAST : LED_EFFECT_RAMP_DOWN_FAST;
      led_effect_t slow = up ? LED_EFFECT_RAMP_UP_SLOW : LED_EFFECT_RAMP_DOWN_SLOW;
      switch (event) {
        case BTN_CLICK:
          led_effect_start(fast, false, 0);         // one fast cycle
          (void)remote_send_level_step(up);
          break;
        case BTN_HOLD_START:
          led_effect_start(fast, true, 0);          // repeat while held
          (void)remote_send_level_move(up);
          break;
        case BTN_DOUBLE_CLICK:
          led_effect_start(slow, false, 0);         // one slow cycle
          (void)remote_send_ct_step(up);            // F6 CT step (M5)
          break;
        case BTN_DOUBLE_HOLD_START:
          led_effect_start(slow, true, 0);          // repeat while held
          (void)remote_send_ct_move(up);            // F6 CT move (M5)
          break;
        case BTN_HOLD_STOP:
          led_effect_stop();
          (void)remote_send_level_stop();
          break;
        case BTN_DOUBLE_HOLD_STOP:
          led_effect_stop();
          (void)remote_send_ct_stop();              // F6 CT move stop (M5)
          break;
        default:
          break;
      }
      break;
    }

    default:
      break;
  }
}

/** @brief Main Init
 *
 * Called from the application framework's main init. Milestones hook their
 * one-time initialization (event init, GPIO, ADC, LED timer) in here.
 */
void emberAfMainInitCallback(void)
{
  buttons_init();
  buttons_set_callback(app_button_event);
  buttons_set_combo_callback(app_combo_event);
  led_effects_init();
  remote_zigbee_init();
  sl_zigbee_event_init(&pairing_hold_event, pairing_hold_handler);
  sl_zigbee_event_init(&pairing_window_event, pairing_window_handler);
  sl_zigbee_event_init(&ota_hold_event, ota_hold_handler);   // M9 (F10)
  battery_init();        // M6 (F3)
  action_cache_init();   // M7 (F7)
  ota_trigger_init();    // M9 (F10)
}

/** @brief Stack Status
 *
 * Notified of changes to the network stack status. F7 (action-cache flush) will
 * hook NETWORK_UP here in M7.
 */
void emberAfStackStatusCallback(EmberStatus status)
{
  if (status == EMBER_NETWORK_UP) {
    TS_LOG("Network up");
    pairing_end("joined");
    action_cache_flush();   // M7 (F7): replay coalesced offline actions (<=3 msgs)
  } else if (status == EMBER_NETWORK_DOWN) {
    TS_LOG("Network down");
    if (pairing_wait_net_down) {
      pairing_begin_steering();   // leave completed -> now steer (F9 sequence)
    }
  }
}

/** @brief Complete network steering.
 *
 * Fired when the Network Steering plugin finishes. In this firmware steering is
 * started ONLY by the F9 pairing combo (added in M8); no find-and-bind here.
 */
void emberAfPluginNetworkSteeringCompleteCallback(EmberStatus status,
                                                  uint8_t totalBeacons,
                                                  uint8_t joinAttempts,
                                                  uint8_t finalState)
{
  (void)totalBeacons;
  (void)joinAttempts;
  (void)finalState;
  TS_LOG("Join complete: 0x%02X", status);
  // Success is also handled via EMBER_NETWORK_UP; this catches steering giving
  // up before the PAIR_WINDOW_MS cap (all channels scanned, no open network).
  if (status != EMBER_SUCCESS) {
    pairing_end("steering failed");
  }
}

#ifndef SL_CATALOG_ZIGBEE_EZSP_PRESENT
/** @brief Application framework equivalent of ::emberRadioNeedsCalibratingHandler */
void emberAfRadioNeedsCalibratingCallback(void)
{
  sl_mac_calibrate_current_channel();
}
#endif // SL_CATALOG_ZIGBEE_EZSP_PRESENT
