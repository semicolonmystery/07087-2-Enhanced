/***************************************************************************//**
 * @file buttons.h
 * @brief TS1001-CUS custom GPIO button driver + debounced gesture state machine.
 *
 * Milestone 1. Replaces the stock `simple_button` component (which hardcodes the
 * wrong pins and discards the press that wakes the chip from EM2). This driver
 * owns the four remote buttons directly via EMLIB GPIO + the emdrv GPIOINT
 * interrupt dispatcher, and turns raw edges into semantic gesture events.
 *
 * Hardware (all active-low, internal pull-up + glitch filter):
 *   ON    = PD15   OFF   = PF4
 *   MINUS = PA2    PLUS  = PF5
 *
 * Behaviour (see PLAN.md F1/F2/F4/F5/F6 and app_config.h for the tunables):
 *   - ON / OFF   : only BTN_CLICK.
 *   - PLUS/MINUS : BTN_CLICK, BTN_HOLD_START/STOP, BTN_DOUBLE_CLICK,
 *                  BTN_DOUBLE_HOLD_START/STOP.
 *   - Wake press is NOT lost: the GPIO IRQ wakes the chip and its live level is
 *     fed into the same debounce path used while awake.
 *   - Stuck-button cap (STUCK_BUTTON_MS): a button whose level never changes is
 *     marked stuck, its pending action dropped, and it stops holding the device
 *     awake; the eventual release edge is consumed silently.
 *   - Combo detection (detect-only in M1): ON+OFF (pairing, M8) and PLUS+MINUS
 *     (OTA, M9) held simultaneously.
 ******************************************************************************/
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Logical button identifiers. Also index into the internal tables. */
typedef enum {
  BTN_ON    = 0,   ///< PD15
  BTN_OFF   = 1,   ///< PF4
  BTN_PLUS  = 2,   ///< PF5
  BTN_MINUS = 3,   ///< PA2
  BTN_COUNT = 4    ///< Number of buttons (not a button).
} button_id_t;

/** @brief Semantic gesture events emitted by the state machine.
 *
 * ON/OFF emit only BTN_CLICK. PLUS/MINUS emit the full set. HOLD/DOUBLE_HOLD are
 * emitted as START/STOP pairs so the consumer can drive a continuous action
 * (e.g. a ZCL Move) between them.
 */
typedef enum {
  BTN_CLICK              = 0, ///< Single press+release (< HOLD_MS), no 2nd press.
  BTN_HOLD_START         = 1, ///< Held past HOLD_MS (no preceding click).
  BTN_HOLD_STOP          = 2, ///< Release after a BTN_HOLD_START.
  BTN_DOUBLE_CLICK       = 3, ///< Two clicks within DOUBLE_PRESS_MS.
  BTN_DOUBLE_HOLD_START  = 4, ///< Click, then 2nd press held past HOLD_MS.
  BTN_DOUBLE_HOLD_STOP   = 5, ///< Release after a BTN_DOUBLE_HOLD_START.
  BTN_EVENT_COUNT        = 6  ///< Number of event kinds (not an event).
} button_event_t;

/** @brief Simultaneous-hold combos, detected for later milestones. */
typedef enum {
  BTN_COMBO_ON_OFF     = 0, ///< ON + OFF   -> pairing / reset (M8).
  BTN_COMBO_PLUS_MINUS = 1, ///< PLUS + MINUS -> OTA trigger (M9).
  BTN_COMBO_COUNT      = 2  ///< Number of combos (not a combo).
} button_combo_t;

/** @brief Application callback for button events.
 *  @param button  A ::button_id_t.
 *  @param event   A ::button_event_t.
 *  Invoked from DSR / main-loop context (never from ISR), so it is safe to call
 *  the Zigbee stack from here in later milestones.
 */
typedef void (*buttons_event_cb_t)(uint8_t button, uint8_t event);

/***************************************************************************//**
 * @brief One-time init: GPIO config, EXTI registration, event objects.
 *
 * Call from emberAfMainInitCallback(). The `gpiointerrupt` component has already
 * run GPIOINT_Init() at driver-init time; this only registers the four per-pin
 * callbacks and configures the pins.
 ******************************************************************************/
void buttons_init(void);

/***************************************************************************//**
 * @brief Register the application event sink. Pass NULL to detach.
 ******************************************************************************/
void buttons_set_callback(buttons_event_cb_t cb);

/***************************************************************************//**
 * @brief Query whether a combo is currently active (both members held).
 * @param combo A ::button_combo_t.
 * @return true if both member buttons are currently debounced-pressed.
 ******************************************************************************/
bool buttons_combo_active(uint8_t combo);

/** @brief Combo transition callback.
 *  @param combo  A ::button_combo_t.
 *  @param active true when both members become held, false when either releases.
 *  DSR context (never ISR). The app times the hold itself (e.g. PAIR_HOLD_MS).
 */
typedef void (*buttons_combo_cb_t)(uint8_t combo, bool active);

/***************************************************************************//**
 * @brief Register the combo transition sink. Pass NULL to detach.
 ******************************************************************************/
void buttons_set_combo_callback(buttons_combo_cb_t cb);

#endif // BUTTONS_H
