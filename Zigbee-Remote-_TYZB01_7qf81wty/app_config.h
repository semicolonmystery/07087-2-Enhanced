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
#define SLEEP_IDLE_MS               0       // MUST STAY 0. Sleep backoff: forced
                                            //   stay-awake window after every wake.
                                            //   This is NOT a "shorter = less drain"
                                            //   knob — any non-zero value is a huge
                                            //   battery leak, because the SDK arms the
                                            //   wake timer for the time to the NEXT
                                            //   EVENT instead of for the remainder of
                                            //   the backoff window
                                            //   (zigbee_app_framework_sleep.c:190-247).
                                            //   So one wake inside the backoff pins the
                                            //   MCU in EM1 (~1.5 mA) until the next poll
                                            //   — i.e. LONG_POLL_S of EM1, then
                                            //   LONG_POLL_S of EM2, ~50 % EM1 duty
                                            //   forever. Observed effect: batteries flat
                                            //   in 1-2 months. 0 = SDK default = the
                                            //   device drops to EM2 as soon as it is
                                            //   idle, which is what F4 ("wake only on
                                            //   button press") actually requires.
#define TX_GRACE_MS                 500     // max wait for in-flight TX before sleeping
#define LONG_POLL_S                 1800    // long poll interval (sleepy end device):
                                            //   how often it wakes to poll the parent.
                                            //   30 min — far under the ~256 min parent
                                            //   child-aging timeout, so it stays joined,
                                            //   while cutting periodic radio wakeups.

// ---------------------------------------------------------------------------
// Poll Control server cluster (0x0020) — the IKEA-style "coordinator may manage
// my poll rate" contract. Values are in QUARTER-SECONDS (ZCL unit).
//
// The Poll Control server plugin reads these from the ZCL *attribute defaults*
// in the .zap, which are edited in Studio's ZCL editor. This block is the single
// source of truth for what to type there — see docs/BUILD.md. ShortPollInterval
// is additionally written at boot by app.c straight from the constant below, so
// that one cannot drift from the .zap default (it sets OTA download speed, and
// was forgotten once already).
//
// At startup the plugin pushes LongPollInterval / ShortPollInterval into the
// end-device-support plugin (poll-control-server.c:441-461), so once the
// cluster exists these attributes OVERRIDE
// EMBER_AF_PLUGIN_END_DEVICE_SUPPORT_{LONG,SHORT}_POLL_INTERVAL_SECONDS.
// That is why POLL_CTRL_LONG_POLL_QS must stay equal to LONG_POLL_S.
// ---------------------------------------------------------------------------
#define POLL_CTRL_CHECK_IN_QS       14400   // CheckInInterval (0x0000), ZAP: 0x00003840
                                            //   Device-initiated "I am alive, does
                                            //   anyone want me awake?" — this is what
                                            //   keeps Z2M availability working for a
                                            //   sleepy device (a Z2M ping can never
                                            //   reach us: the parent only holds
                                            //   indirect messages ~7.68 s). Z2M's
                                            //   poll_control configure step may
                                            //   rewrite this; that is fine and
                                            //   expected. 0 would disable check-ins
                                            //   entirely (poll-control-server.c:148).
#define POLL_CTRL_LONG_POLL_QS      (LONG_POLL_S * 4)   // LongPollInterval (0x0001),
                                            //   ZAP: 0x00001C20 (INT32U)
#define POLL_CTRL_SHORT_POLL_QS     1       // ShortPollInterval (0x0002), ZAP: 0x0001
                                            //   (INT16U). 250 ms — the ZCL minimum,
                                            //   and THE knob that sets OTA download
                                            //   speed. The SDK's OTA client asks for
                                            //   EMBER_AF_SHORT_POLL while an Image
                                            //   Block Request is outstanding
                                            //   (ota-client.c:477-485) but never
                                            //   shortens the interval itself, so one
                                            //   63-byte block moves per short poll:
                                            //   at 1 s that is ~44 min for a 166 KB
                                            //   image, at 250 ms it is ~11 min.
                                            //   Short poll only engages while a task
                                            //   is pending, so idle drain is
                                            //   unaffected.
#define POLL_CTRL_FAST_POLL_QS      40      // FastPollTimeout (0x0003), ZAP: 0x0028
                                            //   (INT16U). 10 s cap on a
                                            //   client-requested fast-poll burst.
#define POLL_CTRL_LONG_POLL_MIN_QS  20      // LongPollIntervalMin (0x0005), ZAP:
                                            //   0x00000014 (INT32U). Floor a
                                            //   client may set (5 s). Battery guard —
                                            //   without it a misbehaving coordinator
                                            //   could pin us at a 1 s long poll.

// Plugin constraint (poll-control-server.c:296-311): a long poll interval longer
// than the check-in interval is rejected as invalid.
#if POLL_CTRL_LONG_POLL_QS > POLL_CTRL_CHECK_IN_QS
#error "POLL_CTRL_LONG_POLL_QS must be <= POLL_CTRL_CHECK_IN_QS (see poll-control-server.c:296-311)"
#endif
#if POLL_CTRL_LONG_POLL_QS < POLL_CTRL_LONG_POLL_MIN_QS
#error "POLL_CTRL_LONG_POLL_QS must be >= POLL_CTRL_LONG_POLL_MIN_QS"
#endif

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
// A download is bounded by PROGRESS, not by wall-clock time. There used to be a
// fixed 600 s session cap here, which aborted perfectly healthy transfers: a
// full image legitimately takes ~11 min at the OTA poll rate, so the cap fired
// mid-download every time and the user saw one update as several aborted and
// resumed sessions. The session now ends only when the file offset stops
// advancing for OTA_PROGRESS_CHECK_S * OTA_STALL_CHECKS, which catches a real stall
// (server gone, parent lost) without punishing a slow-but-working transfer.
#define OTA_PROGRESS_CHECK_S        60      // how often to sample the OTA client's
                                            //   FileOffset attribute during a session
#define OTA_STALL_CHECKS            3       // consecutive samples with no progress
                                            //   before the session is aborted
                                            //   (=> ~180 s of true silence)
#define OTA_QUERY_GRACE_S           30      // after starting a session, how long
                                            //   discovery+query may take before an
                                            //   idle session is ended early (LED
                                            //   off, back to sleep); re-checked
                                            //   every interval while downloading
// Ground truth from the bootloader project (btl_storage_slot_cfg.h): OTA
// storage slot 0. NOT tunables — change ONLY together with the bootloader.
// Cross-checked against ota-storage-simple-eeprom-config.h by #if in app.c.
#define OTA_SLOT0_START             262144  // 0x40000 (absolute flash address)
#define OTA_SLOT0_END               487424  // 0x77000 = start + 225280 (220 KB)

// Flash map, and why the slot moved (v1.0.15):
//   app   0x00000..0x40000  256 KB   OTA_APP_MAX_BYTES below
//   slot  0x40000..0x77000  220 KB   this slot
//   NVM3  0x77000..0x80000   36 KB   network keys, bindings, tokens
//
// The image has to be compressed to fit the slot at all, and the Gecko
// bootloader can only decompress what its parser was built with. LZMA needs
// 5144 bytes of bootloader flash and the Series-1 main stage has only 14336
// total, so it overflowed by 3072 with nothing left to reclaim (ECDSA cannot
// be removed — the signature tag is compiled in unconditionally). LZ4 costs
// 894 bytes and fits, but only compresses this image to ~87 %, so the slot had
// to grow: start moved down 0x44000 -> 0x40000 and the end into the 12 KB that
// was sitting unused between the old slot and NVM3.
//
// THE APP LINKER DOES NOT ENFORCE THIS. autogen/linkerfile.ld hands the app all
// 512 KB (ORIGIN 0x0, LENGTH 0x80000), so an app that grows past OTA_SLOT0_START
// silently overwrites the OTA slot instead of failing to link. create_ota.py
// refuses to build a release image that crosses the line; keep it that way.
#define OTA_APP_MAX_BYTES           OTA_SLOT0_START

// ---------------------------------------------------------------------------
// Radio
// ---------------------------------------------------------------------------
#define TX_POWER_DBM                10      // radio TX power

// ---------------------------------------------------------------------------
// Firmware identity / version (keep Basic attrs, converter, and OTA in sync)
// ---------------------------------------------------------------------------
// ---- Human-readable version. Bump all four together for a release. --------
#define FW_VERSION_MAJOR            1
#define FW_VERSION_MINOR            0
#define FW_VERSION_PATCH            17
#define FW_VERSION_STRING           "1.0.17"   // -> Basic SW Build ID (0x4000)
#define FW_DATE_CODE                "20260904" // -> Basic DateCode (0x0006), YYYYMMDD

// Flat build counter. MUST increase on EVERY released image — it is the only
// field that makes FW_OTA_FILE_VERSION grow, and an OTA is offered only when
// the file version is strictly greater than the running one. It is deliberately
// NOT derived from major/minor/patch: a single byte cannot encode all three
// without collisions (1.0.10 and 1.1.0 would tie).
#define FW_BUILD                    17

// EmberZNet version this image is built against (GSDK 4.4.6 = EmberZNet 7.4.x).
#define FW_STACK_REL                7
#define FW_STACK_BUILD              4

#define FW_APP_VERSION_ATTR         FW_BUILD   // -> Basic ApplicationVersion (0x0001)

// ---- Zigbee OTA file version ----------------------------------------------
// The OTA spec fixes the meaning of these four bytes:
//   31-24 application release | 23-16 application build
//   15-8  stack release       |  7-0  stack build
// Up to and including v1.0.10 this field held major.minor.patch.build, so the
// PATCH number sat in the STACK RELEASE byte: every release looked to Z2M and
// other tools like an unchanged application (app 1, build 0) with a moving
// "stack version" (0.5, 0.8, 0.10...). Since v1.0.11 the version lives in the
// application bytes and the stack bytes report the real EmberZNet version.
//
// Kept as a plain hex literal on purpose: .github/scripts/create_ota.py parses
// it with the regex `FW_OTA_FILE_VERSION\s+0x([0-9a-fA-F]+)` and would fail on
// an expression. The #if below is what keeps the literal honest.
#define FW_OTA_FILE_VERSION         0x01110704UL

#if FW_OTA_FILE_VERSION != (((FW_VERSION_MAJOR) << 24) | ((FW_BUILD) << 16) \
                            | ((FW_STACK_REL) << 8) | (FW_STACK_BUILD))
#error "FW_OTA_FILE_VERSION literal does not match MAJOR/FW_BUILD/FW_STACK_* — recompute it (see the byte layout above)"
#endif

// Highest file version ever published under the OLD major.minor.patch.build
// scheme (v1.0.10). Every future image must stay above it or OTA clients will
// refuse the upgrade. This is a fixed floor marking the scheme change, not a
// per-release value — incrementing FW_BUILD keeps us clear of it forever.
#define FW_OTA_LAST_LEGACY_VERSION  0x01000A00UL
#if FW_OTA_FILE_VERSION <= FW_OTA_LAST_LEGACY_VERSION
#error "FW_OTA_FILE_VERSION must exceed the last published legacy version (0x01000A00 = v1.0.10) or no device will accept the update"
#endif

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------
#define DEBUG_UART_ENABLED          0       // 1: enable UART0 debug log on PA0 (TXD)

// Master switch for THIS firmware's own console/RTT logging. 0 = production:
// every TS_LOG(...) below compiles to nothing (no flash, no cycles, no wakeups),
// so the app is silent even with the RTT/CLI components still installed. Set to 1
// AND keep the iostream_rtt + zigbee_debug_print components to get the logs back.
#define DEBUG_LOGGING               0
#if DEBUG_LOGGING
  #define TS_LOG(...)  sl_zigbee_app_debug_println(__VA_ARGS__)
#else
  #define TS_LOG(...)  ((void)0)
#endif

#endif // APP_CONFIG_H
