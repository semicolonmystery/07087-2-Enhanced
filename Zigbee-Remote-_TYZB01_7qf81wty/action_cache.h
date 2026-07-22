/***************************************************************************//**
 * @file action_cache.h
 * @brief TS1001_TYZB01_7qf81wty_Enhanced offline action cache with semantic coalescing (F7).
 *
 * Milestone 7. When a control command cannot be delivered (not joined, or both
 * binding send paths failed), the USER INTENT — not the raw command — is
 * cached in plain static RAM (survives EM2 sleep; lost on reboot, which is
 * acceptable per spec) and replayed as at most THREE messages when the network
 * comes back:
 *
 *   On/Off slot : latest-wins boolean ("ON then OFF" replays only Off).
 *   Level slot  : one signed accumulator in Level units, clamped to +/-254.
 *                 Failed click => +/-LEVEL_STEP. Failed hold (Move..Stop) =>
 *                 +/-LEVEL_MOVE_RATE x held-seconds. Flushed as ONE Step.
 *   CT slot     : same pattern in mireds, clamped to +/-(CT_MAX_MIREDS -
 *                 CT_MIN_MIREDS). Failed double-click => +/-CT_STEP_MIREDS;
 *                 failed double-hold => +/-CT_MOVE_RATE x held-seconds.
 *                 Flushed as ONE StepColorTemperature (bounds included).
 *
 * Move/Stop duration state machine (per slot):
 *   - A FAILED Move records direction + a start timestamp (monotonic ms tick,
 *     read internally — callers pass no timestamps).
 *   - The matching Stop hook is called on EVERY stop attempt (success or
 *     failure): if a failed Move is being tracked it converts the elapsed time
 *     into an accumulator delta and clears the tracking. A lone offline Stop
 *     with no tracked Move is a no-op.
 *   - Move SUCCEEDED but Stop failed: nothing is cached. The bulb was told to
 *     move and never told to stop, so it saturates at a limit; replaying a
 *     Stop minutes later is meaningless and the final level is unknowable.
 *
 * Integration: remote_zigbee.c calls the on_*_failed()/on_*_stop() hooks at
 * its send-failure points; app.c calls action_cache_flush() on
 * EMBER_NETWORK_UP. Flush order is On/Off -> Level -> CT; a slot is cleared
 * only when its send succeeds, so a flush that fails again (parent lost)
 * keeps the remaining intent for the next NETWORK_UP.
 ******************************************************************************/
#ifndef ACTION_CACHE_H
#define ACTION_CACHE_H

#include <stdbool.h>

/***************************************************************************//**
 * @brief One-time init: empty all slots. Call from emberAfMainInitCallback().
 ******************************************************************************/
void action_cache_init(void);

/***************************************************************************//**
 * @brief An On or Off command failed to send. Latest-wins single slot.
 * @param on true = On, false = Off.
 ******************************************************************************/
void action_cache_on_onoff_failed(bool on);

/***************************************************************************//**
 * @brief A Level Step (single click) failed: accumulate +/-LEVEL_STEP.
 * @param up true = step up.
 ******************************************************************************/
void action_cache_on_level_step_failed(bool up);

/***************************************************************************//**
 * @brief A Level Move (hold start) failed: start duration tracking.
 * @param up true = move up.
 ******************************************************************************/
void action_cache_on_level_move_failed(bool up);

/***************************************************************************//**
 * @brief A Level Stop was attempted (hold release — call on success AND
 *        failure): finalizes a tracked failed Move into the accumulator.
 ******************************************************************************/
void action_cache_on_level_stop(void);

/***************************************************************************//**
 * @brief A CT Step (double click) failed: accumulate +/-CT_STEP_MIREDS.
 * @param mireds_up true = mireds increase (warmer) — the caller resolves
 *        COLOR_TEMP_PLUS_DIR before calling.
 ******************************************************************************/
void action_cache_on_ct_step_failed(bool mireds_up);

/***************************************************************************//**
 * @brief A CT Move (double-hold start) failed: start duration tracking.
 * @param mireds_up true = mireds increase (warmer).
 ******************************************************************************/
void action_cache_on_ct_move_failed(bool mireds_up);

/***************************************************************************//**
 * @brief A CT Stop was attempted (double-hold release — call on success AND
 *        failure): finalizes a tracked failed Move into the accumulator.
 ******************************************************************************/
void action_cache_on_ct_stop(void);

/***************************************************************************//**
 * @brief Replay everything cached: On/Off -> Level -> CT, at most one message
 *        each (<=3 total). Call on EMBER_NETWORK_UP. Slots whose send fails
 *        keep their content for the next flush.
 ******************************************************************************/
void action_cache_flush(void);

#endif // ACTION_CACHE_H
