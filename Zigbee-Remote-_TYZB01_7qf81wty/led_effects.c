/***************************************************************************//**
 * @file led_effects.c
 * @brief TS1001_TYZB01_7qf81wty_Enhanced non-blocking LED effect engine (F8) — PWM on PA5.
 *
 * See led_effects.h for the public contract and PLAN.md (M2, F8) for the spec.
 * Every timing constant comes from app_config.h.
 *
 * Hardware & PWM configuration
 * ----------------------------
 *   LED       : PA5, ACTIVE-LOW (pin low = LED on).
 *   Timer     : TIMER0 CC0 in PWM mode. TIMER0 is free in this app (RAIL uses
 *               its own PROTIMER, sleeptimer uses RTCC).
 *   Routing   : Series-1 pin routing — TIMER0->ROUTELOC0 CC0LOC = LOC5 puts CC0
 *               on PA5 (verified: AF_TIMER0_CC0_PORT(5)=A / AF_TIMER0_CC0_PIN(5)=5
 *               in Device/SiliconLabs/EFR32MG13P/Include/efr32mg13p_af_ports.h /
 *               _af_pins.h), gated by TIMER0->ROUTEPEN bit TIMER_ROUTEPEN_CC0PEN
 *               (efr32mg13p_timer.h:601,648).
 *   Frequency : LED_PWM_HZ (2 kHz — flicker-free, low switching current).
 *               HFPERCLK / timerPrescale16 feeds the counter; TOP is derived at
 *               init from CMU_ClockFreqGet(cmuClock_TIMER0) so the frequency is
 *               correct whatever the board clock is (38.4/40 MHz ⇒ TOP ≈ 1200,
 *               comfortably 16-bit, ≥1000-step duty resolution).
 *   Polarity  : ACTIVE-LOW is handled with the CC output invert
 *               (TIMER_InitCC_TypeDef.outInvert, em_timer.h:439). Normal EFR32
 *               PWM drives the pin high while CNT < CCV; inverted it is LOW
 *               while CNT < CCV, so CCV is directly the LED-ON time and
 *               brightness ∝ CCV with no duty mirroring anywhere else.
 *   Off state : ROUTEPEN CC0 is cleared and TIMER0 stopped, so PA5 falls back
 *               to its GPIO push-pull level (DOUT=1 → pin high → LED off).
 *               The cmuClock_TIMER0 HFPERCLK gate is enabled once at init and
 *               LEFT ENABLED: HF clocks stop in EM2 anyway, so an idle-but-
 *               gated-on TIMER0 costs nothing while sleeping, and the registers
 *               stay writable without enable/disable churn.
 *
 * Engine
 * ------
 *   A single sl_zigbee_event_t steps the animation in main-loop context:
 *   ramps tick every LED_TICK_MS; blink/pairing schedule whole phase lengths
 *   (LED_BLINK_MS / LED_PAIR_BLINK_MS) so they wake the CPU as rarely as
 *   possible. Duty updates while running go through TIMER_CompareBufSet
 *   (buffered CCVB, applied at the next PWM period ⇒ glitch-free).
 *
 *   EM1 power-manager requirement (PWM needs a running HF clock, i.e. EM1):
 *   added exactly once when the engine goes NONE→active in led_effect_start(),
 *   removed exactly once when it goes active→NONE in led_effect_stop(). Every
 *   termination path — natural end of a ramp/blink, effect replacement stop,
 *   explicit/early stop, pairing stop — funnels through led_effect_stop(), and
 *   both transitions are guarded by s_em1_held, so the add/remove calls are
 *   balanced by construction.
 *
 * Verified SDK APIs (headers cited):
 *   TIMER_Init / TIMER_InitCC / TIMER_TopSet / TIMER_CompareSet /
 *   TIMER_CompareBufSet / TIMER_CounterSet / TIMER_Enable / timerCCModePWM /
 *   timerPrescale16 ................................ platform/emlib/inc/em_timer.h
 *   CMU_ClockEnable / CMU_ClockFreqGet / cmuClock_TIMER0
 *   ................................................ platform/emlib/inc/em_cmu.h
 *   GPIO_PinModeSet ................................ platform/emlib/inc/em_gpio.h
 *   sl_power_manager_add/remove_em_requirement(SL_POWER_MANAGER_EM1)
 *   ............... platform/service/power_manager/inc/sl_power_manager.h:344,362
 *   sl_zigbee_event_init / _set_delay_ms / _set_inactive
 *   .......... protocol/zigbee/app/framework/common/zigbee_app_framework_event.h
 ******************************************************************************/

#include "app/framework/include/af.h"   // sl_zigbee_event_*
#include "app_config.h"
#include "led_effects.h"

#include "em_timer.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "sl_power_manager.h"

// ---------------------------------------------------------------------------
// Local hardware constants (the tunable timings live in app_config.h).
// ---------------------------------------------------------------------------
#define LED_PORT            gpioPortA
#define LED_PIN             5
#define LED_TIMER           TIMER0
#define LED_PWM_HZ          2000            // ~1-10 kHz: flicker-free, low current
#define LED_PWM_PRESC       timerPrescale16 // enum value, log2 encoding
#define LED_PWM_PRESC_DIV   16              // matching numeric divisor

// Animation tick for the ramp effects, derived from the config values:
// 20 steps across the fast ramp ⇒ LED_TICK_MS = 20 ms, giving the slow ramp
// LED_RAMP_SLOW_MS / LED_TICK_MS = 50 steps. Blink/pairing do not use the tick
// (they schedule whole phase lengths).
#define LED_TICK_MS         (LED_RAMP_FAST_MS / 20)
#if LED_TICK_MS < 1
#error "LED_RAMP_FAST_MS too small for the 20-step ramp tick"
#endif

// ---------------------------------------------------------------------------
// Engine state
// ---------------------------------------------------------------------------
static sl_zigbee_event_t s_tick_event;   // steps the running animation

static led_effect_t s_effect;            // LED_EFFECT_NONE when idle
static bool         s_repeat;            // ramps: loop until led_effect_stop()
static bool         s_em1_held;          // EM1 requirement add/remove guard
static uint16_t     s_ramp_steps;        // ticks per ramp cycle (fast/slow)
static uint16_t     s_step;              // current ramp tick [0 .. s_ramp_steps)
static uint16_t     s_phases_left;       // blink: remaining on+off half-phases
static bool         s_led_on;            // blink/pairing: current phase level

static uint32_t     s_top1;              // TOP + 1 (PWM period in timer ticks)

// ---------------------------------------------------------------------------
// PWM primitives
// ---------------------------------------------------------------------------

// compare == LED-on (low) time in timer ticks, thanks to outInvert (see banner).
// compare = 0 ⇒ LED fully off; compare = s_top1 (> TOP, never matches) ⇒ fully on.
static inline uint32_t duty_compare(uint32_t numer, uint32_t denom)
{
  return (s_top1 * numer) / denom;
}

// Start driving PA5 from the PWM with an initial compare value.
static void pwm_run(uint32_t compare)
{
  TIMER_CounterSet(LED_TIMER, 0);                    // fresh period
  TIMER_CompareSet(LED_TIMER, 0, compare);           // immediate (CCV)
  LED_TIMER->ROUTEPEN |= TIMER_ROUTEPEN_CC0PEN;      // CC0 takes over PA5
  TIMER_Enable(LED_TIMER, true);
}

// Update the duty of a running PWM (buffered ⇒ applied at next period).
static inline void pwm_set(uint32_t compare)
{
  TIMER_CompareBufSet(LED_TIMER, 0, compare);
}

// Stop the PWM and return PA5 to its idle GPIO level (high ⇒ LED off).
static void pwm_idle(void)
{
  TIMER_Enable(LED_TIMER, false);
  LED_TIMER->ROUTEPEN &= ~TIMER_ROUTEPEN_CC0PEN;     // pin high via GPIO DOUT=1
  TIMER_CounterSet(LED_TIMER, 0);
}

// ---------------------------------------------------------------------------
// Animation stepping (main-loop context via s_tick_event)
// ---------------------------------------------------------------------------
static void tick_handler(sl_zigbee_event_t *event)
{
  (void)event;

  switch (s_effect) {
    case LED_EFFECT_RAMP_UP_FAST:
    case LED_EFFECT_RAMP_DOWN_FAST:
    case LED_EFFECT_RAMP_UP_SLOW:
    case LED_EFFECT_RAMP_DOWN_SLOW: {
      s_step++;
      if (s_step >= s_ramp_steps) {                  // cycle complete
        if (!s_repeat) {
          led_effect_stop();                         // self-terminate
          return;
        }
        s_step = 0;                                  // sawtooth reset
      }
      bool up = (s_effect == LED_EFFECT_RAMP_UP_FAST
                 || s_effect == LED_EFFECT_RAMP_UP_SLOW);
      uint32_t k = up ? s_step : (uint32_t)(s_ramp_steps - s_step);
      pwm_set(duty_compare(k, s_ramp_steps));
      sl_zigbee_event_set_delay_ms(&s_tick_event, LED_TICK_MS);
      break;
    }

    case LED_EFFECT_BLINK:
      s_phases_left--;
      if (s_phases_left == 0) {                      // last off-phase done
        led_effect_stop();                           // self-terminate
        return;
      }
      s_led_on = !s_led_on;
      pwm_set(s_led_on ? s_top1 : 0);
      sl_zigbee_event_set_delay_ms(&s_tick_event, LED_BLINK_MS);
      break;

    case LED_EFFECT_PAIRING:                         // runs until stopped
      s_led_on = !s_led_on;
      pwm_set(s_led_on ? s_top1 : 0);
      sl_zigbee_event_set_delay_ms(&s_tick_event, LED_PAIR_BLINK_MS);
      break;

    case LED_EFFECT_OTA: {                           // runs until stopped
      // Triangle: brightness rises for the first half of the period, falls for
      // the second — a smooth "breathing" distinct from sawtooth ramps/blinks.
      s_step++;
      if (s_step >= s_ramp_steps) {
        s_step = 0;
      }
      uint32_t half = s_ramp_steps / 2;
      uint32_t k = (s_step < half) ? s_step : (uint32_t)(s_ramp_steps - s_step);
      pwm_set(duty_compare(k, half));
      sl_zigbee_event_set_delay_ms(&s_tick_event, LED_TICK_MS);
      break;
    }

    default:                                         // stale tick; nothing runs
      break;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void led_effect_start(led_effect_t effect, bool repeat, uint8_t count)
{
  if (effect == LED_EFFECT_NONE) {
    led_effect_stop();
    return;
  }
  if (effect > LED_EFFECT_OTA) {
    return;                                          // unknown effect id
  }
  // F8 priority: pairing > OTA > button feedback. While pairing runs, nothing
  // else gets through; while OTA breathing runs, feedback requests are ignored
  // but pairing may take over. Only led_effect_stop() ends either.
  if (s_effect == LED_EFFECT_PAIRING && effect != LED_EFFECT_PAIRING) {
    return;
  }
  if (s_effect == LED_EFFECT_OTA
      && effect != LED_EFFECT_OTA && effect != LED_EFFECT_PAIRING) {
    return;
  }

  // NONE→active transition: take the EM1 requirement exactly once. If another
  // effect is already running we keep the requirement we already hold and just
  // retarget the engine (replacement is NOT an end: no remove/add pair).
  if (!s_em1_held) {
    sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
    s_em1_held = true;
  }
  sl_zigbee_event_set_inactive(&s_tick_event);       // drop old effect's tick

  s_effect = effect;
  s_repeat = repeat;

  switch (effect) {
    case LED_EFFECT_RAMP_UP_FAST:
    case LED_EFFECT_RAMP_DOWN_FAST:
    case LED_EFFECT_RAMP_UP_SLOW:
    case LED_EFFECT_RAMP_DOWN_SLOW: {
      bool fast = (effect == LED_EFFECT_RAMP_UP_FAST
                   || effect == LED_EFFECT_RAMP_DOWN_FAST);
      s_ramp_steps = (fast ? LED_RAMP_FAST_MS : LED_RAMP_SLOW_MS) / LED_TICK_MS;
      s_step = 0;
      bool up = (effect == LED_EFFECT_RAMP_UP_FAST
                 || effect == LED_EFFECT_RAMP_UP_SLOW);
      // First sample: up starts dark (0%), down starts full (100%).
      pwm_run(up ? 0 : s_top1);
      sl_zigbee_event_set_delay_ms(&s_tick_event, LED_TICK_MS);
      break;
    }

    case LED_EFFECT_BLINK:
      if (count == 0) {
        count = 1;
      }
      s_phases_left = (uint16_t)count * 2;           // on+off per blink
      s_led_on = true;
      pwm_run(s_top1);                               // start in the on-phase
      sl_zigbee_event_set_delay_ms(&s_tick_event, LED_BLINK_MS);
      break;

    case LED_EFFECT_PAIRING:
      s_repeat = true;                               // pairing always repeats
      s_led_on = true;
      pwm_run(s_top1);
      sl_zigbee_event_set_delay_ms(&s_tick_event, LED_PAIR_BLINK_MS);
      break;

    case LED_EFFECT_OTA:
      // Breathing: triangle fade over LED_OTA_BREATHE_MS, repeats until
      // led_effect_stop() (the M9 OTA session controls the lifetime).
      s_repeat = true;
      s_ramp_steps = LED_OTA_BREATHE_MS / LED_TICK_MS;
      if (s_ramp_steps < 2) {
        s_ramp_steps = 2;                            // keep the triangle math sane
      }
      s_step = 0;
      pwm_run(0);                                    // start dark, fade in
      sl_zigbee_event_set_delay_ms(&s_tick_event, LED_TICK_MS);
      break;

    default:
      break;                                         // unreachable (checked above)
  }
}

void led_effect_stop(void)
{
  sl_zigbee_event_set_inactive(&s_tick_event);       // no further wakeups
  pwm_idle();                                        // LED off, timer stopped
  s_effect = LED_EFFECT_NONE;

  // active→NONE transition: release the EM1 requirement exactly once. The
  // guard makes redundant stop() calls (and stop-before-any-start) harmless.
  if (s_em1_held) {
    sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
    s_em1_held = false;
  }
}

bool led_effect_active(void)
{
  return s_effect != LED_EFFECT_NONE;
}

void led_effects_init(void)
{
  s_effect = LED_EFFECT_NONE;
  s_em1_held = false;

  sl_zigbee_event_init(&s_tick_event, tick_handler);

  // PA5 push-pull, DOUT=1: the routed TIMER output only drives the pad while
  // ROUTEPEN CC0 is set; otherwise the pin idles at the GPIO level, high ⇒ LED
  // off (active-low).
  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(LED_PORT, LED_PIN, gpioModePushPull, 1);

  // TIMER0 clock gate: enabled once here and left on (see banner for why).
  CMU_ClockEnable(cmuClock_TIMER0, true);

  // Derive TOP for LED_PWM_HZ from the actual HFPERCLK feeding TIMER0.
  uint32_t timer_hz = CMU_ClockFreqGet(cmuClock_TIMER0) / LED_PWM_PRESC_DIV;
  s_top1 = timer_hz / LED_PWM_HZ;                    // period in timer ticks
  if (s_top1 < 2) {
    s_top1 = 2;                                      // paranoia: keep TOP >= 1
  }

  // Counter: up-count PWM base, /16 prescale, NOT started until an effect runs.
  TIMER_Init_TypeDef init = TIMER_INIT_DEFAULT;      // em_timer.h
  init.enable = false;
  init.prescale = LED_PWM_PRESC;
  TIMER_Init(LED_TIMER, &init);

  // CC0: PWM mode, output INVERTED so CCV = LED-on (low) time (active-low LED;
  // see banner "Polarity"). coist stays false: with the route disabled while
  // idle the compare-output idle state never reaches the pin anyway.
  TIMER_InitCC_TypeDef cc = TIMER_INITCC_DEFAULT;    // em_timer.h
  cc.mode = timerCCModePWM;
  cc.outInvert = true;
  TIMER_InitCC(LED_TIMER, 0, &cc);

  TIMER_TopSet(LED_TIMER, s_top1 - 1);
  TIMER_CompareSet(LED_TIMER, 0, 0);

  // Series-1 routing: CC0 -> location 5 = PA5 (efr32mg13p_af_ports/_pins.h).
  // ROUTEPEN stays clear until pwm_run().
  LED_TIMER->ROUTELOC0 = (LED_TIMER->ROUTELOC0 & ~_TIMER_ROUTELOC0_CC0LOC_MASK)
                         | TIMER_ROUTELOC0_CC0LOC_LOC5;
  LED_TIMER->ROUTEPEN &= ~TIMER_ROUTEPEN_CC0PEN;
}
