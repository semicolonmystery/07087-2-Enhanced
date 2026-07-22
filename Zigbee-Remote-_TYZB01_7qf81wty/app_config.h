/***************************************************************************//**
 * @file app_config.h
 * @brief Single source of every tunable constant for the TS1001_TYZB01_7qf81wty_Enhanced remote.
 *
 * All values are the spec defaults. No magic numbers elsewhere in the app —
 * anything a user might reasonably want to tune lives here with a comment.
 * See PLAN.md / the implementation brief for the meaning of each group.
 ******************************************************************************/
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// ---------------------------------------------------------------------------
// Buttons / debounce (F1, F2, F4, F6)
// ---------------------------------------------------------------------------
#define DEBOUNCE_MS                 20      // button debounce window
#define DOUBLE_PRESS_MS             300     // 2nd-press window for double click/hold
#define HOLD_MS                     400     // press duration that becomes a "hold"
#define STUCK_BUTTON_MS             20000   // no-change ⇒ stuck; drop action & sleep
#define ONE_CLICK_DISPATCH_DELAYED  1       // 1: delay single-click by DOUBLE_PRESS_MS
                                            //    to disambiguate double click/hold

// ---------------------------------------------------------------------------
// Level control (F6) — PLUS/MINUS single click & single hold
// ---------------------------------------------------------------------------
#define LEVEL_STEP                  32      // Step command amount (units)
#define LEVEL_STEP_TIME_DS          1       // Step transition time (deciseconds)
#define LEVEL_MOVE_RATE             50      // Move rate (units / second) while held

// ---------------------------------------------------------------------------
// Color temperature (F6) — PLUS/MINUS double click & double hold
// ---------------------------------------------------------------------------
#define CT_STEP_MIREDS              30      // Step Color Temperature amount (mireds)
#define CT_STEP_TIME_DS             1       // Step CT transition time (deciseconds);
                                            //   mirrors LEVEL_STEP_TIME_DS so a CT
                                            //   step eases in instead of snapping
#define CT_MOVE_RATE                20      // Move Color Temperature rate (mireds/s)
#define CT_MIN_MIREDS               153     // lower bound in commands (~6500 K)
#define CT_MAX_MIREDS               500     // upper bound in commands (~2000 K)
#define COLOR_TEMP_PLUS_DIR         0       // 0: PLUS decreases mireds (cooler),
                                            //    MINUS increases (warmer); 1: reversed

// ---------------------------------------------------------------------------
// LED effects (F8) — PA5, active-low, PWM via TIMER
// ---------------------------------------------------------------------------
#define LED_RAMP_FAST_MS            400     // level sawtooth cycle length
#define LED_RAMP_SLOW_MS            1000    // color-temp sawtooth cycle length
#define LED_BLINK_MS                100     // single feedback blink length (ON/OFF)
#define LED_PAIR_BLINK_MS           200     // pairing continuous-blink half-period
#define LED_OTA_BREATHE_MS          2000    // OTA session "breathing" full period
                                            //   (fade in + fade out; runs while an
                                            //   OTA query/download is in progress)

// ---------------------------------------------------------------------------
// Sleep / power policy (F4)
// ---------------------------------------------------------------------------
#define SLEEP_IDLE_MS               1000    // stay-awake window after any wake before
                                            //   dropping to EM2 (shorter = less drain)
#define TX_GRACE_MS                 500     // max wait for in-flight TX before sleeping
#define LONG_POLL_S                 1800    // long poll interval (sleepy end device):
                                            //   how often it wakes to poll the parent.
                                            //   30 min — far under the ~256 min parent
                                            //   child-aging timeout, so it stays joined,
                                            //   while cutting periodic radio wakeups.

// ---------------------------------------------------------------------------
// Battery (F3) — 2×AAA alkaline, VBAT/3 divider, PF2 enable, PB11 sense
// ---------------------------------------------------------------------------
#define BATTERY_SETTLE_MS           10      // RC settle after enabling divider
#define BATTERY_V_100               3.10f   // ≥ this ⇒ 100 %
#define BATTERY_V_0                 2.30f   // ≤ this ⇒ 0 % (never below 2.2 V floor)
#define BATTERY_DIVIDER_NUM         1       // VADC = VBAT * NUM/DEN  (100k / 300k)
#define BATTERY_DIVIDER_DEN         3
#define BATTERY_MEASURE_MIN_INTERVAL_S  3600 // throttle measurements

// ---------------------------------------------------------------------------
// Offline action cache (F7)
// ---------------------------------------------------------------------------
#define ACTION_CACHE_MAX            10      // cap; On/Off & Level & CT coalesce anyway

// ---------------------------------------------------------------------------
// Pairing / reset (F9) and OTA (F10)
// ---------------------------------------------------------------------------
#define PAIR_HOLD_MS                15000   // ON+OFF hold to leave & re-pair
#define PAIR_WINDOW_MS              30000   // network-steering window
#define OTA_AUTO_QUERY_ENABLED      0       // 0: NEVER auto-query the OTA server on
                                            //   wake — saves battery; OTA runs ONLY on
                                            //   the manual PLUS+MINUS trigger. 1: also
                                            //   auto-query at most once per interval.
#define OTA_QUERY_MIN_INTERVAL_S    86400   // (only if OTA_AUTO_QUERY_ENABLED) at most
                                            //   one auto OTA query / day
#define OTA_TRIGGER_HOLD_MS         10000   // PLUS+MINUS hold to force OTA query
                                            //   (spec default 5000; user raised to 10 s)
#define OTA_SESSION_MAX_S           600     // hard cap: an OTA session (query or
                                            //   download) is aborted after this
                                            //   long; partial downloads are kept
                                            //   and resume on the next session
#define OTA_QUERY_GRACE_S           30      // after starting a session, how long
                                            //   discovery+query may take before an
                                            //   idle session is ended early (LED
                                            //   off, back to sleep); re-checked
                                            //   every interval while downloading
// Ground truth from the bootloader project (btl_storage_slot_cfg.h): OTA
// storage slot 0. NOT tunables — change ONLY together with the bootloader.
// Cross-checked against ota-storage-simple-eeprom-config.h by #if in app.c.
#define OTA_SLOT0_START             278528  // 0x44000 (absolute flash address)
#define OTA_SLOT0_END               475136  // 0x74000 = start + 196608 (192 KB)

// ---------------------------------------------------------------------------
// Radio
// ---------------------------------------------------------------------------
#define TX_POWER_DBM                10      // radio TX power

// ---------------------------------------------------------------------------
// Firmware identity / version (keep Basic attrs, converter, and OTA in sync)
// ---------------------------------------------------------------------------
#define FW_VERSION_MAJOR            1
#define FW_VERSION_MINOR            0
#define FW_VERSION_PATCH            5
#define FW_APP_VERSION_ATTR         0x01    // Basic ApplicationVersion (0x0001)
#define FW_VERSION_STRING           "1.0.5" // Basic SW Build ID (0x4000)
// OTA image file version: monotonic; 0xMMmmppbb (major.minor.patch.build).
#define FW_OTA_FILE_VERSION         0x01000500UL

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------
#define DEBUG_UART_ENABLED          0       // 1: enable UART0 debug log on PA0 (TXD)

// Master switch for THIS firmware's own console/RTT logging. 0 = production:
// every TS_LOG(...) below compiles to nothing (no flash, no cycles, no wakeups),
// so the app is silent even with the RTT/CLI components still installed. Set to 1
// AND keep the iostream_rtt + zigbee_debug_print components to get the logs back.
#define DEBUG_LOGGING               1
#if DEBUG_LOGGING
  #define TS_LOG(...)  sl_zigbee_app_debug_println(__VA_ARGS__)
#else
  #define TS_LOG(...)  ((void)0)
#endif

#endif // APP_CONFIG_H
