/***************************************************************************//**
 * @file remote_zigbee.c
 * @brief TS1001-CUS Zigbee command layer (F5/F6) — implementation.
 *
 * See remote_zigbee.h for the contract. Design invariant: fill the command,
 * stamp sourceEndpoint = REMOTE_ENDPOINT, then send to the binding table both
 * as unicast(s) AND as multicast(s) so light binds and group binds both fire.
 *
 * Verified APIs (SDKs/gecko_sdk/protocol/zigbee/app/framework/include/af.h):
 *   EmberNetworkStatus emberAfNetworkState(void);                  // af.h:1092
 *   EmberStatus emberAfSendCommandUnicastToBindings(void);         // af.h:1578
 *   EmberStatus emberAfSendCommandMulticastToBindings(void);       // af.h:1619
 *   EmberApsFrame *emberAfGetCommandApsFrame(void);                // af.h:1747
 * Fill macros verified in this project's autogen/zap-command.h:
 *   emberAfFillCommandOnOffClusterOn()                             // :1580
 *   emberAfFillCommandOnOffClusterOff()                            // :1548
 *   emberAfFillCommandLevelControlClusterStep(stepMode, stepSize,
 *       transitionTime, optionMask, optionOverride)                // :1789
 *   emberAfFillCommandLevelControlClusterMove(moveMode, rate,
 *       optionMask, optionOverride)                                // :1764
 *   emberAfFillCommandLevelControlClusterStop(optionMask,
 *       optionOverride)                                            // :1808
 *   emberAfFillCommandColorControlClusterMoveColorTemperature(moveMode,
 *       rate, colorTemperatureMinimum, colorTemperatureMaximum,
 *       optionsMask, optionsOverride)                              // :5982
 *   emberAfFillCommandColorControlClusterStepColorTemperature(stepMode,
 *       stepSize, transitionTime, colorTemperatureMinimum,
 *       colorTemperatureMaximum, optionsMask, optionsOverride)     // :6011
 * Status codes (stack/include/error-def.h): EMBER_SUCCESS (0x00, :59),
 *   EMBER_NETWORK_DOWN (0x91, :602).
 ******************************************************************************/

#include "app/framework/include/af.h"
#include "app_config.h"
#include "remote_zigbee.h"
#include "action_cache.h"    // M7: failure hooks at every sender's fail path

// Level Control Step/Move direction field values (ZCL 8, 3.10.2.3.2/.3.10.2.3.1;
// see docs/api-reference.md): 0x00 = Up, 0x01 = Down.
#define LEVEL_DIR_UP    0x00u
#define LEVEL_DIR_DOWN  0x01u

// Color Control Move/StepColorTemperature mode field values. NOT the same
// encoding as Level Control: the CT commands reuse the color "move mode" enum.
// Verified against the SDK color-control server, which decodes exactly these
// (SDKs/gecko_sdk/protocol/zigbee/app/framework/plugin/color-control-server/
// color-control-server.c:24-26, applied to CT Move at :1341/:1364 and to CT
// Step at :1436/:1454):
//   0x00 = Stop (Move only), 0x01 = Up (mireds increase → warmer),
//   0x03 = Down (mireds decrease → cooler).
#define CT_MODE_STOP    0x00u
#define CT_MODE_UP      0x01u
#define CT_MODE_DOWN    0x03u

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

/** @brief True when we can transmit; otherwise log why (M7 turns the failure
 *         returns at the call sites into cache entries). */
static bool network_ready(const char *what)
{
  if (emberAfNetworkState() == EMBER_JOINED_NETWORK) {
    return true;
  }
  TS_LOG("%s: not joined, dropped", what);
  return false;
}

/** @brief Map a button direction to the CT Up/Down mode value (F6).
 *
 * plus_dir is the physical gesture (true = PLUS button); COLOR_TEMP_PLUS_DIR
 * (app_config.h) selects what PLUS means:
 *   COLOR_TEMP_PLUS_DIR = 0 (default): PLUS DECREASES mireds (cooler light),
 *     so plus_dir=true → CT_MODE_DOWN (0x03) and MINUS → CT_MODE_UP (0x01).
 *   COLOR_TEMP_PLUS_DIR = 1: reversed — PLUS increases mireds (warmer).
 * Truth table (mireds_up = "mireds increase"):
 *   plus_dir  COLOR_TEMP_PLUS_DIR  mireds_up  mode
 *     true            0              false    CT_MODE_DOWN (cooler)
 *     false           0              true     CT_MODE_UP   (warmer)
 *     true            1              true     CT_MODE_UP   (warmer)
 *     false           1              false    CT_MODE_DOWN (cooler)
 * i.e. mireds_up = plus_dir XOR (COLOR_TEMP_PLUS_DIR == 0).
 */
static uint8_t ct_mode_from_dir(bool plus_dir)
{
  bool mireds_up = plus_dir ^ (COLOR_TEMP_PLUS_DIR == 0);
  return mireds_up ? CT_MODE_UP : CT_MODE_DOWN;
}

/** @brief Send the already-filled command to the binding table BOTH ways.
 *
 * Unicast covers direct light binds; multicast covers group binds. The action
 * is "delivered" if either path reports EMBER_SUCCESS (same pattern the SDK
 * reporting plugin uses, reporting.c:363-367).
 *
 * @return EMBER_SUCCESS if u or m succeeded, else the unicast status (the more
 *         diagnostic of the two: with only group binds present the unicast pass
 *         finds no match while multicast succeeds, and vice versa).
 */
static EmberStatus send_to_bindings(void)
{
  emberAfGetCommandApsFrame()->sourceEndpoint = REMOTE_ENDPOINT;
  EmberStatus u = emberAfSendCommandUnicastToBindings();
  EmberStatus m = emberAfSendCommandMulticastToBindings();
  TS_LOG("tx u=0x%02X m=0x%02X", u, m);
  if (u == EMBER_SUCCESS || m == EMBER_SUCCESS) {
    return EMBER_SUCCESS;
  }
  return u;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void remote_zigbee_init(void)
{
  // Intentionally empty. The M7 action cache turned out to need no plumbing
  // here: its hooks are called directly from the senders above, and its own
  // init/flush are wired in app.c (action_cache_init / action_cache_flush).
}

EmberStatus remote_send_on(void)
{
  EmberStatus status;
  if (!network_ready("on")) {
    status = EMBER_NETWORK_DOWN;
  } else {
    emberAfFillCommandOnOffClusterOn();
    status = send_to_bindings();
  }
  if (status != EMBER_SUCCESS) {
    action_cache_on_onoff_failed(true);          // M7 (F7): cache the intent
  }
  return status;
}

EmberStatus remote_send_off(void)
{
  EmberStatus status;
  if (!network_ready("off")) {
    status = EMBER_NETWORK_DOWN;
  } else {
    emberAfFillCommandOnOffClusterOff();
    status = send_to_bindings();
  }
  if (status != EMBER_SUCCESS) {
    action_cache_on_onoff_failed(false);         // M7 (F7): latest-wins slot
  }
  return status;
}

EmberStatus remote_send_level_step(bool up)
{
  EmberStatus status;
  if (!network_ready("lvl-step")) {
    status = EMBER_NETWORK_DOWN;
  } else {
    emberAfFillCommandLevelControlClusterStep(up ? LEVEL_DIR_UP : LEVEL_DIR_DOWN,
                                              LEVEL_STEP,
                                              LEVEL_STEP_TIME_DS,
                                              0,    // optionMask: no override
                                              0);   // optionOverride
    status = send_to_bindings();
  }
  if (status != EMBER_SUCCESS) {
    action_cache_on_level_step_failed(up);       // M7 (F7): +/-LEVEL_STEP
  }
  return status;
}

EmberStatus remote_send_level_move(bool up)
{
  EmberStatus status;
  if (!network_ready("lvl-move")) {
    status = EMBER_NETWORK_DOWN;
  } else {
    emberAfFillCommandLevelControlClusterMove(up ? LEVEL_DIR_UP : LEVEL_DIR_DOWN,
                                              LEVEL_MOVE_RATE,
                                              0,    // optionMask: no override
                                              0);   // optionOverride
    status = send_to_bindings();
  }
  if (status != EMBER_SUCCESS) {
    action_cache_on_level_move_failed(up);       // M7 (F7): start hold timer
  }
  return status;
}

EmberStatus remote_send_level_stop(void)
{
  EmberStatus status;
  if (!network_ready("lvl-stop")) {
    status = EMBER_NETWORK_DOWN;
  } else {
    emberAfFillCommandLevelControlClusterStop(0, 0);  // no options override
    status = send_to_bindings();
  }
  // M7 (F7): called on EVERY outcome — finalizes a tracked failed Move into
  // the accumulator (rate x held-time); a lone offline Stop is a no-op there.
  action_cache_on_level_stop();
  return status;
}

EmberStatus remote_send_ct_step(bool plus_dir)
{
  bool mireds_up = (ct_mode_from_dir(plus_dir) == CT_MODE_UP);
  EmberStatus status;
  if (!network_ready("ct-step")) {
    status = EMBER_NETWORK_DOWN;
  } else {
    // Bounds always passed so a bulb clamps to our CT_MIN/CT_MAX window even if
    // its own physical range is wider (ZCL: 0 would mean "use physical limits").
    emberAfFillCommandColorControlClusterStepColorTemperature(
        mireds_up ? CT_MODE_UP : CT_MODE_DOWN,
        CT_STEP_MIREDS,
        CT_STEP_TIME_DS,
        CT_MIN_MIREDS,
        CT_MAX_MIREDS,
        0,    // optionsMask: no override
        0);   // optionsOverride
    status = send_to_bindings();
  }
  if (status != EMBER_SUCCESS) {
    action_cache_on_ct_step_failed(mireds_up);   // M7 (F7): +/-CT_STEP_MIREDS
  }
  return status;
}

EmberStatus remote_send_ct_move(bool plus_dir)
{
  bool mireds_up = (ct_mode_from_dir(plus_dir) == CT_MODE_UP);
  EmberStatus status;
  if (!network_ready("ct-move")) {
    status = EMBER_NETWORK_DOWN;
  } else {
    emberAfFillCommandColorControlClusterMoveColorTemperature(
        mireds_up ? CT_MODE_UP : CT_MODE_DOWN,
        CT_MOVE_RATE,
        CT_MIN_MIREDS,
        CT_MAX_MIREDS,
        0,    // optionsMask: no override
        0);   // optionsOverride
    status = send_to_bindings();
  }
  if (status != EMBER_SUCCESS) {
    action_cache_on_ct_move_failed(mireds_up);   // M7 (F7): start hold timer
  }
  return status;
}

EmberStatus remote_send_ct_stop(void)
{
  EmberStatus status;
  if (!network_ready("ct-stop")) {
    status = EMBER_NETWORK_DOWN;
  } else {
    // Stop = MoveColorTemperature with moveMode 0x00. The server halts the
    // transition and returns before ever looking at rate or bounds
    // (color-control-server.c:1341-1344); we still fill the real values so the
    // frame is well-formed and consistent with remote_send_ct_move().
    emberAfFillCommandColorControlClusterMoveColorTemperature(
        CT_MODE_STOP,
        CT_MOVE_RATE,
        CT_MIN_MIREDS,
        CT_MAX_MIREDS,
        0,    // optionsMask: no override
        0);   // optionsOverride
    status = send_to_bindings();
  }
  // M7 (F7): every outcome — see remote_send_level_stop().
  action_cache_on_ct_stop();
  return status;
}

// -----------------------------------------------------------------------------
// F7 flush helpers (M7) — explicit-amount Steps used by action_cache_flush().
// Deliberately NO action-cache hooks in these two: the flush keeps its own
// slot when the send fails, and a hook here would re-cache the wrong (fixed)
// step amount.
// -----------------------------------------------------------------------------

EmberStatus remote_send_level_step_amount(int16_t amount)
{
  if (amount == 0) {
    return EMBER_SUCCESS;                        // nothing to do
  }
  if (!network_ready("lvl-flush")) {
    return EMBER_NETWORK_DOWN;
  }
  uint16_t magnitude = (uint16_t)(amount > 0 ? amount : -amount);
  if (magnitude > 254u) {
    magnitude = 254u;                            // Step size is one byte; the
  }                                              // Level range is 254 anyway
  emberAfFillCommandLevelControlClusterStep((amount > 0) ? LEVEL_DIR_UP
                                                         : LEVEL_DIR_DOWN,
                                            (uint8_t)magnitude,
                                            LEVEL_STEP_TIME_DS,
                                            0,    // optionMask: no override
                                            0);   // optionOverride
  return send_to_bindings();
}

EmberStatus remote_send_ct_step_amount(int16_t mireds)
{
  if (mireds == 0) {
    return EMBER_SUCCESS;                        // nothing to do
  }
  if (!network_ready("ct-flush")) {
    return EMBER_NETWORK_DOWN;
  }
  uint16_t magnitude = (uint16_t)(mireds > 0 ? mireds : -mireds);
  emberAfFillCommandColorControlClusterStepColorTemperature(
      (mireds > 0) ? CT_MODE_UP : CT_MODE_DOWN,  // positive = mireds increase
      magnitude,
      CT_STEP_TIME_DS,
      CT_MIN_MIREDS,
      CT_MAX_MIREDS,
      0,    // optionsMask: no override
      0);   // optionsOverride
  return send_to_bindings();
}
