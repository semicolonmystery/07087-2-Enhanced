/***************************************************************************//**
 * @file buttons.c
 * @brief TS1001_TYZB01_7qf81wty_Enhanced custom GPIO button driver + debounced gesture state machine.
 *
 * See buttons.h for the public contract and PLAN.md (M1, F1/F2/F4/F5/F6) for the
 * behavioural spec. Every timing constant comes from app_config.h.
 *
 * Architecture
 * ------------
 *   ISR  : emdrv GPIOINT dispatches each pin's both-edge interrupt to
 *          button_irq_cb(). There we ONLY sample the live level and kick a
 *          single ISR-safe event (s_wake_event). No stack/event-scheduler calls
 *          with a delay are made from ISR context (the Zigbee event queue is not
 *          ISR-safe for delayed scheduling).
 *   DSR  : wake_handler() runs in main-loop context, turns the raw sample into a
 *          "wait-for-quiet" debounce (per-button debounce_event, DEBOUNCE_MS).
 *   DSR  : debounce_handler() commits a stable level change and feeds the
 *          per-button gesture FSM (on_button_edge()). gesture_event drives the
 *          HOLD / double-press deadlines; stuck_event enforces STUCK_BUTTON_MS.
 *
 * Verified SDK APIs (headers cited inline):
 *   GPIO_PinModeSet / GPIO_ExtIntConfig / GPIO_PinInGet .. platform/emlib/inc/em_gpio.h
 *   CMU_ClockEnable ........................................ platform/emlib/inc/em_cmu.h
 *   GPIOINT_CallbackRegister .............. platform/emdrv/gpiointerrupt/inc/gpiointerrupt.h
 *   sl_zigbee_event_* / sl_zigbee_af_isr_event_init ....... protocol/zigbee/app/framework/
 *                                            common/zigbee_app_framework_event.h (via af.h)
 ******************************************************************************/

#include "app/framework/include/af.h"   // sl_zigbee_event_*, TS_LOG
#include "app_config.h"
#include "buttons.h"

#include "em_gpio.h"
#include "em_cmu.h"
#include "gpiointerrupt.h"

// ---------------------------------------------------------------------------
// Pin map (active-low). intNo == pin number; pins 15/4/5/2 are all distinct so
// the Series-1 external-interrupt lines never collide.
// ---------------------------------------------------------------------------
typedef struct {
  GPIO_Port_TypeDef port;
  uint8_t           pin;
  bool              advanced;   // true => full click/hold/double set (PLUS/MINUS)
} button_hw_t;

static const button_hw_t s_hw[BTN_COUNT] = {
  [BTN_ON]    = { gpioPortD, 15, false },
  [BTN_OFF]   = { gpioPortF,  4, false },
  [BTN_PLUS]  = { gpioPortF,  5, true  },
  [BTN_MINUS] = { gpioPortA,  2, true  },
};

// ---------------------------------------------------------------------------
// Gesture FSM states (per button).
// ---------------------------------------------------------------------------
typedef enum {
  G_IDLE = 0,   // released, nothing pending
  G_PRESS1,     // first press down; advanced: HOLD_MS timer armed
  G_HOLD1,      // first press held past HOLD_MS (HOLD_START emitted)
  G_WAIT2,      // first click done; DOUBLE_PRESS_MS window open
  G_PRESS2,     // second press down; HOLD_MS timer armed
  G_HOLD2,      // second press held past HOLD_MS (DOUBLE_HOLD_START emitted)
  G_STUCK,      // stuck cap hit; waiting for a silent release
  G_CONSUMED,   // press absorbed by a combo; release consumed silently
} gesture_state_t;

typedef struct {
  sl_zigbee_event_t debounce_event;  // wait-for-quiet settle (DEBOUNCE_MS)
  sl_zigbee_event_t gesture_event;   // HOLD_MS / DOUBLE_PRESS_MS deadline
  sl_zigbee_event_t stuck_event;     // STUCK_BUTTON_MS no-change cap

  volatile uint8_t  irq_level;       // level sampled in ISR (0 = pressed)
  volatile bool     irq_pending;     // ISR flagged a fresh sample

  bool              pressed;         // committed (debounced) logical state
  gesture_state_t   state;
} button_ctx_t;

static button_ctx_t s_btn[BTN_COUNT];

// Single ISR-safe event: any pin IRQ activates it, its handler runs in DSR.
static sl_zigbee_event_t s_wake_event;

// pin number -> button id (0xFF = not one of ours). Sized for Series-1 pins 0..15.
static uint8_t s_pin_to_btn[16];

static buttons_event_cb_t s_cb;
static buttons_combo_cb_t s_combo_cb;

// Live combo state (mirrors both-members-pressed); used for edge detection.
static bool s_combo_active[BTN_COMBO_COUNT];
static const button_id_t s_combo_members[BTN_COMBO_COUNT][2] = {
  [BTN_COMBO_ON_OFF]     = { BTN_ON,   BTN_OFF   },
  [BTN_COMBO_PLUS_MINUS] = { BTN_PLUS, BTN_MINUS },
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static inline void emit(uint8_t btn, button_event_t evt)
{
  if (s_cb != NULL) {
    s_cb(btn, (uint8_t)evt);
  }
}

// Read the LIVE debounced-sense level (true == pressed, active-low).
static inline bool pin_pressed(uint8_t btn)
{
  return GPIO_PinInGet(s_hw[btn].port, s_hw[btn].pin) == 0;
}

// ---------------------------------------------------------------------------
// Combo detection (detect-only in M1). Called after every committed edge.
// When a combo forms, both members are absorbed (G_CONSUMED) so they emit no
// individual click/hold; any hold already in progress is cleanly stopped.
// ---------------------------------------------------------------------------
static void consume_for_combo(uint8_t btn)
{
  button_ctx_t *c = &s_btn[btn];
  if (c->state == G_HOLD1) {
    emit(btn, BTN_HOLD_STOP);
  } else if (c->state == G_HOLD2) {
    emit(btn, BTN_DOUBLE_HOLD_STOP);
  }
  sl_zigbee_event_set_inactive(&c->gesture_event);
  if (c->state != G_STUCK) {
    c->state = G_CONSUMED;   // release will be swallowed silently
  }
}

static void update_combos(void)
{
  for (uint8_t k = 0; k < BTN_COMBO_COUNT; k++) {
    button_id_t a = s_combo_members[k][0];
    button_id_t b = s_combo_members[k][1];
    bool active = s_btn[a].pressed && s_btn[b].pressed;
    if (active && !s_combo_active[k]) {
      s_combo_active[k] = true;
      consume_for_combo(a);
      consume_for_combo(b);
      if (s_combo_cb != NULL) {
        s_combo_cb(k, true);
      }
    } else if (!active && s_combo_active[k]) {
      s_combo_active[k] = false;
      if (s_combo_cb != NULL) {
        s_combo_cb(k, false);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Gesture FSM — fed one committed edge at a time from the debounce layer.
// ---------------------------------------------------------------------------
static void on_button_edge(uint8_t btn, bool pressed)
{
  button_ctx_t *c = &s_btn[btn];
  bool advanced = s_hw[btn].advanced;

  if (pressed) {
    // Any press (re)arms the stuck cap: level must change within STUCK_BUTTON_MS.
    sl_zigbee_event_set_delay_ms(&c->stuck_event, STUCK_BUTTON_MS);

    switch (c->state) {
      case G_IDLE:
        c->state = G_PRESS1;
        if (advanced) {
          sl_zigbee_event_set_delay_ms(&c->gesture_event, HOLD_MS);
        }
        break;
      case G_WAIT2:                       // 2nd press within the double window
        // Supersede the (possibly still-pending) single click.
        sl_zigbee_event_set_inactive(&c->gesture_event);
        c->state = G_PRESS2;
        sl_zigbee_event_set_delay_ms(&c->gesture_event, HOLD_MS);
        break;
      default:
        break;                            // unexpected; ignore
    }
  } else {
    // Release: the level changed, so the stuck cap no longer applies.
    sl_zigbee_event_set_inactive(&c->stuck_event);

    switch (c->state) {
      case G_PRESS1:
        if (!advanced) {
          emit(btn, BTN_CLICK);           // ON/OFF: simple click on release
          c->state = G_IDLE;
        } else {
          sl_zigbee_event_set_inactive(&c->gesture_event);  // cancel HOLD timer
#if ONE_CLICK_DISPATCH_DELAYED
          // Defer the click so a 2nd press can supersede it with a double gesture.
          c->state = G_WAIT2;
          sl_zigbee_event_set_delay_ms(&c->gesture_event, DOUBLE_PRESS_MS);
#else
          // Low-latency: emit now; still open the window to also catch a double.
          emit(btn, BTN_CLICK);
          c->state = G_WAIT2;
          sl_zigbee_event_set_delay_ms(&c->gesture_event, DOUBLE_PRESS_MS);
#endif
        }
        break;
      case G_HOLD1:
        emit(btn, BTN_HOLD_STOP);
        c->state = G_IDLE;
        break;
      case G_PRESS2:
        sl_zigbee_event_set_inactive(&c->gesture_event);    // cancel HOLD timer
        emit(btn, BTN_DOUBLE_CLICK);
        c->state = G_IDLE;
        break;
      case G_HOLD2:
        emit(btn, BTN_DOUBLE_HOLD_STOP);
        c->state = G_IDLE;
        break;
      case G_STUCK:                        // the awaited silent release
      case G_CONSUMED:                     // combo member releasing
        c->state = G_IDLE;
        break;
      default:
        break;
    }
  }

  update_combos();
}

// ---------------------------------------------------------------------------
// Event handlers (all run in DSR / main-loop context).
// Each is shared across buttons; the button is recovered from the event pointer.
// ---------------------------------------------------------------------------
static void gesture_handler(sl_zigbee_event_t *event)
{
  for (uint8_t btn = 0; btn < BTN_COUNT; btn++) {
    button_ctx_t *c = &s_btn[btn];
    if (event != &c->gesture_event) {
      continue;
    }
    switch (c->state) {
      case G_PRESS1:                       // held past HOLD_MS -> single hold
        c->state = G_HOLD1;
        emit(btn, BTN_HOLD_START);
        break;
      case G_WAIT2:                        // double window closed with no 2nd press
#if ONE_CLICK_DISPATCH_DELAYED
        emit(btn, BTN_CLICK);              // the deferred single click stands
#endif
        c->state = G_IDLE;
        break;
      case G_PRESS2:                       // 2nd press held past HOLD_MS
        c->state = G_HOLD2;
        emit(btn, BTN_DOUBLE_HOLD_START);
        break;
      default:
        break;
    }
    return;
  }
}

static void stuck_handler(sl_zigbee_event_t *event)
{
  for (uint8_t btn = 0; btn < BTN_COUNT; btn++) {
    button_ctx_t *c = &s_btn[btn];
    if (event != &c->stuck_event) {
      continue;
    }
    // Level unchanged for STUCK_BUTTON_MS -> stuck. Terminate any in-progress
    // hold so its action (e.g. a ZCL Move) stops and the device can sleep; drop
    // any not-yet-dispatched pending action silently. The release edge that
    // eventually arrives is consumed silently (handled in on_button_edge()).
    if (c->state == G_HOLD1) {
      emit(btn, BTN_HOLD_STOP);
    } else if (c->state == G_HOLD2) {
      emit(btn, BTN_DOUBLE_HOLD_STOP);
    }
    sl_zigbee_event_set_inactive(&c->gesture_event);
    c->state = G_STUCK;   // stop holding the device awake: no timers remain armed
    return;
  }
}

static void debounce_handler(sl_zigbee_event_t *event)
{
  for (uint8_t btn = 0; btn < BTN_COUNT; btn++) {
    button_ctx_t *c = &s_btn[btn];
    if (event != &c->debounce_event) {
      continue;
    }
    // Settle window elapsed with no further edge: commit the live level.
    bool p = pin_pressed(btn);
    if (p != c->pressed) {
      c->pressed = p;
      on_button_edge(btn, p);
    }
    return;
  }
}

// Runs once per DSR cycle after any button IRQ. Turns raw ISR samples into the
// wait-for-quiet debounce: (re)arm the settle timer while the level differs from
// the committed state; cancel it if the level bounced back.
static void wake_handler(sl_zigbee_event_t *event)
{
  (void)event;
  for (uint8_t btn = 0; btn < BTN_COUNT; btn++) {
    button_ctx_t *c = &s_btn[btn];
    if (!c->irq_pending) {
      continue;
    }
    c->irq_pending = false;
    bool p = (c->irq_level == 0);          // active-low
    if (p != c->pressed) {
      sl_zigbee_event_set_delay_ms(&c->debounce_event, DEBOUNCE_MS);
    } else {
      sl_zigbee_event_set_inactive(&c->debounce_event);  // transient bounce
    }
  }
}

// ---------------------------------------------------------------------------
// GPIOINT ISR callback (one shared, dispatched by pin/intNo). ISR context:
// only sample the live level and kick the ISR-safe wake event. F1/F2: sampling
// the level here guarantees the press that woke the chip from EM2 is captured.
// ---------------------------------------------------------------------------
static void button_irq_cb(uint8_t intNo)
{
  if (intNo >= 16) {
    return;
  }
  uint8_t btn = s_pin_to_btn[intNo];
  if (btn >= BTN_COUNT) {
    return;
  }
  button_ctx_t *c = &s_btn[btn];
  c->irq_level = (uint8_t)GPIO_PinInGet(s_hw[btn].port, s_hw[btn].pin);
  c->irq_pending = true;
  sl_zigbee_event_set_active(&s_wake_event);   // ISR-safe: immediate activation
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void buttons_set_callback(buttons_event_cb_t cb)
{
  s_cb = cb;
}

void buttons_set_combo_callback(buttons_combo_cb_t cb)
{
  s_combo_cb = cb;
}

bool buttons_combo_active(uint8_t combo)
{
  if (combo >= BTN_COMBO_COUNT) {
    return false;
  }
  button_id_t a = s_combo_members[combo][0];
  button_id_t b = s_combo_members[combo][1];
  return s_btn[a].pressed && s_btn[b].pressed;
}

void buttons_init(void)
{
  s_cb = NULL;
  s_combo_cb = NULL;

  // GPIOINT_Init() is already run by the `gpiointerrupt` component at driver
  // init; enabling the GPIO clock again here is harmless and self-documenting.
  CMU_ClockEnable(cmuClock_GPIO, true);

  // pin -> button lookup for the shared IRQ callback.
  for (uint8_t i = 0; i < sizeof(s_pin_to_btn); i++) {
    s_pin_to_btn[i] = 0xFF;
  }

  // ISR-safe wake event: activated from ISR, handler runs in DSR.
  sl_zigbee_af_isr_event_init(&s_wake_event, wake_handler);

  for (uint8_t btn = 0; btn < BTN_COUNT; btn++) {
    button_ctx_t *c = &s_btn[btn];
    c->pressed = false;
    c->irq_pending = false;
    c->state = G_IDLE;

    sl_zigbee_event_init(&c->debounce_event, debounce_handler);
    sl_zigbee_event_init(&c->gesture_event, gesture_handler);
    sl_zigbee_event_init(&c->stuck_event, stuck_handler);

    // Active-low input with pull-up + glitch filter; DOUT=1 selects pull-up.
    GPIO_PinModeSet(s_hw[btn].port, s_hw[btn].pin, gpioModeInputPullFilter, 1);

    // Both-edge external interrupt, intNo == pin (distinct pins => no conflict).
    s_pin_to_btn[s_hw[btn].pin] = btn;
    GPIOINT_CallbackRegister(s_hw[btn].pin, button_irq_cb);
    GPIO_ExtIntConfig(s_hw[btn].port, s_hw[btn].pin, /*intNo=*/s_hw[btn].pin,
                      /*risingEdge=*/true, /*fallingEdge=*/true, /*enable=*/true);
  }
}
