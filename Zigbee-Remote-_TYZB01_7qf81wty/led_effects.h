/***************************************************************************//**
 * @file led_effects.h
 * @brief TS1001-CUS non-blocking LED effect engine (F8) — PWM on PA5.
 *
 * Milestone 2. Replaces the stock `simple_led` component (wrong pin, no PWM).
 * The single feedback LED sits on PA5 and is ACTIVE-LOW (pin low = LED on).
 * Brightness comes from TIMER0 CC0 PWM routed to PA5; all animation stepping is
 * done with a `sl_zigbee_event_t`, so nothing here ever blocks.
 *
 * Effects (timings from app_config.h — no magic numbers):
 *   RAMP_UP_FAST    sawtooth 0→100% then reset, LED_RAMP_FAST_MS per cycle.
 *   RAMP_DOWN_FAST  reversed sawtooth 100→0, LED_RAMP_FAST_MS per cycle.
 *   RAMP_UP_SLOW    as RAMP_UP_FAST but LED_RAMP_SLOW_MS (color-temp feedback).
 *   RAMP_DOWN_SLOW  as RAMP_DOWN_FAST but LED_RAMP_SLOW_MS.
 *   BLINK           N blinks of LED_BLINK_MS on / LED_BLINK_MS off
 *                   (ON action = 1 blink, OFF action = 2 blinks).
 *   PAIRING         continuous blink, LED_PAIR_BLINK_MS half-period, runs until
 *                   explicitly stopped.
 *   OTA             continuous "breathing" (smooth triangle fade in/out),
 *                   LED_OTA_BREATHE_MS full period, runs until stopped —
 *                   shown while an OTA query/download session is active (M9).
 *
 * Rules (F8):
 *   - Priority pairing > OTA > button feedback: while PAIRING runs, any other
 *     led_effect_start() is ignored; while OTA runs, feedback effects are
 *     ignored but PAIRING takes over; only led_effect_stop() ends them.
 *   - Effects self-terminate (except PAIRING and repeat-mode ramps) and never
 *     hold the device awake past their natural end: an EM1 power-manager
 *     requirement is held exactly while an effect is active.
 *   - LED fully off (pin high, TIMER stopped) whenever no effect runs; the sleep
 *     policy (M4/F4) gates EM2 on led_effect_active().
 ******************************************************************************/
#ifndef LED_EFFECTS_H
#define LED_EFFECTS_H

#include <stdint.h>
#include <stdbool.h>

/** @brief LED effect identifiers. */
typedef enum {
  LED_EFFECT_NONE = 0,        ///< No effect running (LED off).
  LED_EFFECT_RAMP_UP_FAST,    ///< 0→100% sawtooth, LED_RAMP_FAST_MS / cycle.
  LED_EFFECT_RAMP_DOWN_FAST,  ///< 100→0% sawtooth, LED_RAMP_FAST_MS / cycle.
  LED_EFFECT_RAMP_UP_SLOW,    ///< 0→100% sawtooth, LED_RAMP_SLOW_MS / cycle.
  LED_EFFECT_RAMP_DOWN_SLOW,  ///< 100→0% sawtooth, LED_RAMP_SLOW_MS / cycle.
  LED_EFFECT_BLINK,           ///< N short blinks (LED_BLINK_MS on / off).
  LED_EFFECT_PAIRING,         ///< Continuous blink until led_effect_stop().
  LED_EFFECT_OTA,             ///< Continuous breathing (LED_OTA_BREATHE_MS)
                              ///< until led_effect_stop() — OTA session (M9).
} led_effect_t;

/***************************************************************************//**
 * @brief One-time init: PA5 GPIO, TIMER0 PWM config, animation event.
 *
 * Call from emberAfMainInitCallback(). Leaves the LED off (pin high) and the
 * timer stopped; nothing prevents EM2 after this returns.
 ******************************************************************************/
void led_effects_init(void);

/***************************************************************************//**
 * @brief Start an effect (replaces any running feedback effect).
 *
 * Ignored while LED_EFFECT_PAIRING is active unless @p effect is itself
 * LED_EFFECT_PAIRING. Passing LED_EFFECT_NONE is equivalent to
 * led_effect_stop().
 *
 * @param effect  A ::led_effect_t.
 * @param repeat  For ramps: true = loop until led_effect_stop() (hold
 *                feedback); false = play one cycle then self-terminate.
 *                Ignored for BLINK (finite by count) and PAIRING (always
 *                repeats until stopped).
 * @param count   Blink count for LED_EFFECT_BLINK (0 treated as 1); ignored
 *                for every other effect.
 ******************************************************************************/
void led_effect_start(led_effect_t effect, bool repeat, uint8_t count);

/***************************************************************************//**
 * @brief Stop ANY running effect (including PAIRING): LED off, timer stopped,
 *        EM1 requirement released. Safe to call when nothing is running.
 ******************************************************************************/
void led_effect_stop(void);

/***************************************************************************//**
 * @brief Query whether an effect is currently running.
 * @return true while an effect is active — sleep gate for the F4 policy (M4).
 ******************************************************************************/
bool led_effect_active(void);

#endif // LED_EFFECTS_H
