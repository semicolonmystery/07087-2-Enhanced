/***************************************************************************//**
 * @file remote_zigbee.h
 * @brief TS1001_TYZB01_7qf81wty_Enhanced Zigbee command layer (F5/F6) — On/Off, Level and Color
 *        Temperature to bindings.
 *
 * Milestone 3 (+ M5 color temperature). Every control command is addressed via the BINDING TABLE — no
 * hardcoded destinations. After filling a command the layer sends it BOTH ways
 * (emberAfSendCommandUnicastToBindings + emberAfSendCommandMulticastToBindings)
 * so plain unicast binds (a light) and group binds both work; an action counts
 * as delivered if EITHER send returns EMBER_SUCCESS. This mirrors the SDK
 * reporting plugin (reporting.c:363-367).
 *
 * All command parameters come from app_config.h (LEVEL_STEP, LEVEL_STEP_TIME_DS,
 * LEVEL_MOVE_RATE, CT_STEP_MIREDS, CT_STEP_TIME_DS, CT_MOVE_RATE,
 * CT_MIN_MIREDS, CT_MAX_MIREDS, COLOR_TEMP_PLUS_DIR) — no magic numbers.
 *
 * Every sender first checks emberAfNetworkState(); when not joined it returns
 * EMBER_NETWORK_DOWN without touching the radio. M7 (action cache): every
 * failure — not-joined OR both binding sends failing — is routed into the
 * action_cache_* hooks inside these senders, and the two *_amount() senders
 * below exist so action_cache_flush() can replay a coalesced total as a
 * single Step; the amount senders deliberately have NO cache hooks (the
 * cache keeps its own slot on flush failure).
 ******************************************************************************/
#ifndef REMOTE_ZIGBEE_H
#define REMOTE_ZIGBEE_H

#include <stdbool.h>

#include "app/framework/include/af.h"   // EmberStatus

/** @brief The remote's ZCL endpoint (client clusters live here, see ZAP). */
#define REMOTE_ENDPOINT 1

/***************************************************************************//**
 * @brief One-time init. Reserved for later milestones (M7 cache hooks); calling
 *        it from emberAfMainInitCallback() is still required so the wiring is
 *        already in place. Currently a no-op.
 ******************************************************************************/
void remote_zigbee_init(void);

/***************************************************************************//**
 * @brief F5: send On/Off cluster On (no args per ZCL) to all bindings.
 * @return EMBER_SUCCESS if at least one send path accepted the message;
 *         EMBER_NETWORK_DOWN if not joined (nothing sent); else the unicast
 *         send status.
 ******************************************************************************/
EmberStatus remote_send_on(void);

/***************************************************************************//**
 * @brief F5: send On/Off cluster Off (no args per ZCL) to all bindings.
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_off(void);

/***************************************************************************//**
 * @brief F6: one Level Control Step (single click on PLUS/MINUS).
 *        Step(stepMode, LEVEL_STEP, LEVEL_STEP_TIME_DS, 0, 0).
 * @param up true = step up (stepMode 0x00), false = step down (0x01).
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_level_step(bool up);

/***************************************************************************//**
 * @brief F6: continuous Level Control Move (hold start on PLUS/MINUS).
 *        Move(moveMode, LEVEL_MOVE_RATE, 0, 0); pair with
 *        remote_send_level_stop() on release.
 * @param up true = move up (moveMode 0x00), false = move down (0x01).
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_level_move(bool up);

/***************************************************************************//**
 * @brief F6: Level Control Stop(0, 0) — ends a Move (hold release).
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_level_stop(void);

/***************************************************************************//**
 * @brief F6: one Color Control StepColorTemperature (double click on
 *        PLUS/MINUS). Step(mode, CT_STEP_MIREDS, CT_STEP_TIME_DS,
 *        CT_MIN_MIREDS, CT_MAX_MIREDS, 0, 0).
 * @param plus_dir true = the PLUS button direction; the actual mired step
 *        direction is derived from COLOR_TEMP_PLUS_DIR (app_config.h).
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_ct_step(bool plus_dir);

/***************************************************************************//**
 * @brief F6: continuous Color Control MoveColorTemperature (double-hold start
 *        on PLUS/MINUS). Move(mode, CT_MOVE_RATE, CT_MIN_MIREDS,
 *        CT_MAX_MIREDS, 0, 0); pair with remote_send_ct_stop() on release.
 * @param plus_dir true = the PLUS button direction (see remote_send_ct_step()).
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_ct_move(bool plus_dir);

/***************************************************************************//**
 * @brief F6: stop a color-temperature Move (double-hold release) —
 *        MoveColorTemperature with moveMode 0x00 (Stop).
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_ct_stop(void);

/***************************************************************************//**
 * @brief F7 flush helper: one Level Control Step of an explicit amount.
 *        Step(mode from sign, |amount| clamped to 254, LEVEL_STEP_TIME_DS,
 *        0, 0). NO action-cache hooks — the caller (action_cache_flush())
 *        handles failure by keeping its slot.
 * @param amount signed Level delta in units; 0 is a no-op (EMBER_SUCCESS).
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_level_step_amount(int16_t amount);

/***************************************************************************//**
 * @brief F7 flush helper: one Color Control StepColorTemperature of an
 *        explicit amount. Step(mode from sign, |mireds|, CT_STEP_TIME_DS,
 *        CT_MIN_MIREDS, CT_MAX_MIREDS, 0, 0) — positive = mireds increase
 *        (warmer). NO action-cache hooks (see remote_send_level_step_amount()).
 * @param mireds signed mired delta; 0 is a no-op (EMBER_SUCCESS).
 * @return See remote_send_on().
 ******************************************************************************/
EmberStatus remote_send_ct_step_amount(int16_t mireds);

#endif // REMOTE_ZIGBEE_H
