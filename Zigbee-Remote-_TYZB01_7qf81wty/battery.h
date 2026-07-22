/***************************************************************************//**
 * @file battery.h
 * @brief TS1001-CUS battery measurement + Power Configuration reporting (F3).
 *
 * Milestone 6. Measures the 2xAAA alkaline pack through a P-MOSFET-switched
 * 200k/100k divider and publishes the result as the endpoint-1 Power
 * Configuration server attributes BatteryVoltage (0x0020, 100 mV units) and
 * BatteryPercentageRemaining (0x0021, half-percent units). With the
 * `zigbee_reporting` component installed and Z2M's ConfigureReporting applied,
 * every attribute write here is picked up by the reporting plugin and sent to
 * the coordinator automatically — this module never sends reports itself.
 *
 * Hardware (verified circuit ground truth):
 *   - Pack: 2xAAA alkaline, ~3.2 V fresh down to the 2.2 V module floor.
 *   - AO3401 P-MOSFET high-side switch; gate = PF2 with an external 200 kOhm
 *     pull-up to VBAT, so the divider is OFF (zero drain) whenever PF2 is
 *     high-Z. Driving PF2 low turns the divider on.
 *   - Divider: 200 kOhm high side / 100 kOhm low side; ADC node = PB11 =
 *     VBAT/3 (<= ~1.07 V for a fresh pack).
 *
 * Measurement sequence (non-blocking, one sl_zigbee_event):
 *   PF2 output low -> wait BATTERY_SETTLE_MS (event delay, no busy-wait) ->
 *   one ADC0 12-bit single conversion on PB11 (internal 1.25 V reference) ->
 *   PF2 back to high-Z -> convert to pack mV -> LUT percent -> write attrs.
 *
 * All tunables (BATTERY_SETTLE_MS, BATTERY_V_100/_V_0, divider ratio,
 * BATTERY_MEASURE_MIN_INTERVAL_S) live in app_config.h.
 ******************************************************************************/
#ifndef BATTERY_H
#define BATTERY_H

/***************************************************************************//**
 * @brief One-time init: PF2/PB11 GPIO idle states, ADC0 clock + common init,
 *        measurement event. Leaves the divider switched off; nothing here
 *        prevents EM2. Call from emberAfMainInitCallback().
 ******************************************************************************/
void battery_init(void);

/***************************************************************************//**
 * @brief Kick a battery measurement if the throttle allows it.
 *
 * Call on every wake-worthy activity (first thing in the button event path):
 * the first call after boot measures immediately, later calls are rate-limited
 * to one measurement per BATTERY_MEASURE_MIN_INTERVAL_S. The measurement
 * itself completes asynchronously (BATTERY_SETTLE_MS later) and writes the
 * Power Configuration attributes when done. Safe to call from any main-loop /
 * DSR context; never blocks.
 ******************************************************************************/
void battery_measure_on_wake(void);

#endif // BATTERY_H
