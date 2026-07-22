/***************************************************************************//**
 * @file action_cache.c
 * @brief TS1001_TYZB01_7qf81wty_Enhanced offline action cache with semantic coalescing (F7).
 *
 * See action_cache.h for the contract and PLAN.md (M7, F7) for the spec.
 * All amounts/rates come from app_config.h — no magic numbers.
 *
 * Storage is three semantic slots in plain static RAM: RAM is retained in EM2
 * sleep, and a reboot (battery swap) legitimately forgets pending intent.
 * ACTION_CACHE_MAX (app_config.h) caps the number of distinct cached actions;
 * with everything coalesced into the 3 slots below the cap is respected by
 * construction — the compile-time check keeps the define load-bearing.
 *
 * Tick source: halCommonGetInt64uMillisecondTick() — monotonic milliseconds
 * backed by the sleeptimer RTCC, keeps counting in EM2, 64-bit so it never
 * wraps (platform/service/legacy_hal/inc/hal.h:99, implementation
 * legacy_hal/src/base-replacement.c:78-85). Same source battery.c uses.
 *
 * Flush re-entrancy note: action_cache_flush() calls remote_send_on()/off(),
 * whose own failure paths call action_cache_on_onoff_failed() again. That is
 * deliberate and harmless — the flush clears a slot only on success, and a
 * failing flush send re-caches the identical value (latest-wins), so "failed
 * slots keep their content". The Level/CT flushes use the amount-parameterised
 * senders (remote_send_level_step_amount / remote_send_ct_step_amount) which
 * have NO cache hooks, and the accumulators are likewise only cleared on
 * success.
 ******************************************************************************/

#include "app/framework/include/af.h"   // EmberStatus, debug print, hal tick
#include "app_config.h"
#include "action_cache.h"
#include "remote_zigbee.h"

#if ACTION_CACHE_MAX < 3
#error "F7: ACTION_CACHE_MAX must cover the 3 semantic slots (On/Off, Level, CT)"
#endif

// Level accumulator clamp: a Step amount is one unsigned byte on the wire and
// the whole Level range is 254 units, so anything bigger is equivalent.
#define LEVEL_ACCUM_MAX     254
// CT accumulator clamp: the commands always carry CT_MIN/CT_MAX bounds, so a
// step spanning the whole allowed window saturates identically.
#define CT_ACCUM_MAX        (CT_MAX_MIREDS - CT_MIN_MIREDS)

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
static bool     s_onoff_pending;    // On/Off slot occupied
static bool     s_onoff_on;         // latest-wins value

static int16_t  s_level_accum;      // pending Level delta, +/-LEVEL_ACCUM_MAX

static int16_t  s_ct_accum;         // pending CT delta (mireds), +/-CT_ACCUM_MAX

// Failed-Move duration tracking (one per slot; the button FSM guarantees
// strict Move..Stop nesting per gesture, so a single tracker each suffices).
typedef struct {
  bool     tracking;                // a failed Move is in progress
  bool     up;                      // its direction
  uint64_t start_ms;                // tick at the failed Move
} move_track_t;

static move_track_t s_level_move;
static move_track_t s_ct_move;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** @brief Add delta to *accum with symmetric clamping. */
static void accum_add(int16_t *accum, int32_t delta, int16_t clamp)
{
  int32_t v = (int32_t)*accum + delta;
  if (v > clamp) {
    v = clamp;
  } else if (v < -clamp) {
    v = -clamp;
  }
  *accum = (int16_t)v;
}

/** @brief Convert a finished failed-Move into an accumulator delta:
 *         +/- rate(units-or-mireds/s) x elapsed-ms / 1000, integer math. */
static void move_finalize(move_track_t *track, int16_t *accum,
                          uint16_t rate_per_s, int16_t clamp, const char *what)
{
  if (!track->tracking) {
    return;                         // lone offline Stop: no-op
  }
  track->tracking = false;

  uint64_t elapsed_ms = halCommonGetInt64uMillisecondTick() - track->start_ms;
  // The button FSM's stuck cap (STUCK_BUTTON_MS) bounds a hold to 20 s, so
  // rate x elapsed fits easily in 32 bits; clamp does the rest.
  int32_t delta = (int32_t)(((uint64_t)rate_per_s * elapsed_ms) / 1000u);
  accum_add(accum, track->up ? delta : -delta, clamp);
  TS_LOG("cache: %s move %s %lu ms -> accum %d",
                              what, track->up ? "up" : "down",
                              (unsigned long)elapsed_ms, *accum);
}

// ---------------------------------------------------------------------------
// Public API — cache hooks (called from remote_zigbee.c failure paths)
// ---------------------------------------------------------------------------

void action_cache_init(void)
{
  s_onoff_pending = false;
  s_level_accum = 0;
  s_ct_accum = 0;
  s_level_move.tracking = false;
  s_ct_move.tracking = false;
}

void action_cache_on_onoff_failed(bool on)
{
  s_onoff_pending = true;
  s_onoff_on = on;                  // latest-wins: ON then OFF => only OFF
  TS_LOG("cache: onoff = %s", on ? "on" : "off");
}

void action_cache_on_level_step_failed(bool up)
{
  accum_add(&s_level_accum, up ? LEVEL_STEP : -(int32_t)LEVEL_STEP,
            LEVEL_ACCUM_MAX);
  TS_LOG("cache: level accum %d", s_level_accum);
}

void action_cache_on_level_move_failed(bool up)
{
  s_level_move.tracking = true;
  s_level_move.up = up;
  s_level_move.start_ms = halCommonGetInt64uMillisecondTick();
}

void action_cache_on_level_stop(void)
{
  move_finalize(&s_level_move, &s_level_accum,
                LEVEL_MOVE_RATE, LEVEL_ACCUM_MAX, "level");
}

void action_cache_on_ct_step_failed(bool mireds_up)
{
  accum_add(&s_ct_accum, mireds_up ? CT_STEP_MIREDS : -(int32_t)CT_STEP_MIREDS,
            CT_ACCUM_MAX);
  TS_LOG("cache: ct accum %d", s_ct_accum);
}

void action_cache_on_ct_move_failed(bool mireds_up)
{
  s_ct_move.tracking = true;
  s_ct_move.up = mireds_up;
  s_ct_move.start_ms = halCommonGetInt64uMillisecondTick();
}

void action_cache_on_ct_stop(void)
{
  move_finalize(&s_ct_move, &s_ct_accum,
                CT_MOVE_RATE, CT_ACCUM_MAX, "ct");
}

// ---------------------------------------------------------------------------
// Public API — flush (called from app.c on EMBER_NETWORK_UP)
// ---------------------------------------------------------------------------

void action_cache_flush(void)
{
  // Rare edge: the network came back while a failed Move is still being held.
  // The pending accumulator (from earlier offline actions) still flushes now;
  // the in-progress hold finalizes into the accumulator at its Stop and waits
  // for the NEXT flush — by then the live Move has failed anyway, so nothing
  // is delivered twice. Documented tradeoff, keeps this path trivial.

  if (!s_onoff_pending && s_level_accum == 0 && s_ct_accum == 0) {
    return;                         // nothing cached
  }
  TS_LOG("cache: flush onoff=%d level=%d ct=%d",
                              s_onoff_pending ? (s_onoff_on ? 1 : 0) : -1,
                              s_level_accum, s_ct_accum);

  // Flush order per F7: On/Off -> Level -> CT, at most one message each.
  if (s_onoff_pending) {
    EmberStatus st = s_onoff_on ? remote_send_on() : remote_send_off();
    if (st == EMBER_SUCCESS) {
      s_onoff_pending = false;
    }
    // else: the sender's failure hook has re-cached the same value — kept.
  }

  if (s_level_accum != 0) {
    if (remote_send_level_step_amount(s_level_accum) == EMBER_SUCCESS) {
      s_level_accum = 0;
    }                               // else: kept for the next NETWORK_UP
  }

  if (s_ct_accum != 0) {
    if (remote_send_ct_step_amount(s_ct_accum) == EMBER_SUCCESS) {
      s_ct_accum = 0;
    }                               // else: kept for the next NETWORK_UP
  }
}
