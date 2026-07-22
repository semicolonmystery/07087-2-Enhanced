/***************************************************************************//**
 * @file battery.c
 * @brief TS1001-CUS battery measurement + Power Configuration reporting (F3).
 *
 * See battery.h for the contract and PLAN.md (M6, F3) for the spec. Every
 * tunable comes from app_config.h; only fixed hardware facts live here.
 *
 * ADC configuration rationale
 * ---------------------------
 *   Reference : internal 1.25 V (adcRef1V25). The divider scales VBAT by 1/3,
 *               so the full battery range 2.30-3.20 V appears as 0.77-1.07 V
 *               at PB11 — comfortably under 1.25 V with headroom. The 2.5 V
 *               reference would waste half of the 12-bit resolution.
 *   Input     : PB11 single-ended vs VSS. On Series 1 every analog-capable
 *               GPIO is hard-wired to fixed APORT bus/channel pairs; PB11 is
 *               APORT channel CH27 reachable on buses 3Y and 4X. Evidence:
 *               - SDK hardware/kit/SLSTK3401A_EFM32PG/config/capsenseconfig.h:47
 *                 documents "PB11 | APORT4XCH27" (same Series-1 APORT fabric),
 *                 and its neighbour PB12 as APORT3XCH28 — i.e. port-B pin n =
 *                 CH(16+n), odd channels on 3Y/4X, even on 3X/4Y.
 *               - Both enums exist for this part: adcPosSelAPORT3YCH27 /
 *                 adcPosSelAPORT4XCH27 (em_adc.h:449/:481; device defines
 *                 efr32mg13p_adc.h:457/:489). ADC0 (the only ADC) is a client
 *                 of all shared APORT buses, and no other analog peripheral is
 *                 used in this app, so there is no bus contention either way.
 *               Chosen: adcPosSelAPORT4XCH27 — matches the one concrete PB11
 *               mapping documented in the SDK.
 *   Acq. time : 32 ADC clock cycles at a 1 MHz ADC clock = 32 us. The divider
 *               source impedance is 200k||100k ~= 67 kOhm, far above the
 *               datasheet's ~kOhm assumption for minimum acquisition time, so
 *               we give the sample cap ample time. (The 10 nF-class node cap
 *               has already settled during BATTERY_SETTLE_MS.)
 *   Warmup    : adcWarmupNormal — ADC fully shut down after each conversion,
 *               nothing draws current between measurements or in EM2.
 *
 * Verified SDK APIs (headers cited):
 *   ADC_Init / ADC_InitSingle .............. platform/emlib/inc/em_adc.h:1136/1151
 *   ADC_TimebaseCalc(0) / ADC_PrescaleCalc(f,0) ("0 = use current HFPERCLK",
 *     em_adc.c:1012/1109) ........................ em_adc.h:1152/1153
 *   ADC_INIT_DEFAULT / ADC_INITSINGLE_DEFAULT ... em_adc.h:838/1039
 *   ADC_Start(adc, adcStartSingle) .............. em_adc.h:1284
 *   ADC_DataSingleGet ........................... em_adc.h:1073
 *   ADC_STATUS_SINGLEDV ("Single Channel Data Valid")
 *     .......... platform/Device/.../EFR32MG13P/Include/efr32mg13p_adc.h:267
 *   adcRef1V25 / adcRes12Bit / adcAcqTime32 / adcNegSelVSS
 *     .......................... em_adc.h:187/262/59/681
 *   CMU_ClockEnable(cmuClock_ADC0, ...) ......... platform/emlib/inc/em_cmu.h
 *   GPIO_PinModeSet, GPIO_ROUTEPEN_TDOPEN ....... em_gpio.h,
 *     efr32mg13p_gpio.h:1296 (PF2 = JTAG TDO route, enabled at reset)
 *   sl_power_manager_add/remove_em_requirement(SL_POWER_MANAGER_EM1)
 *     .......... platform/service/power_manager/inc/sl_power_manager.h:344,362
 *   emberAfWriteServerAttribute(endpoint, cluster, attr, dataPtr, dataType)
 *     .......... protocol/zigbee/app/framework/include/af.h:306
 *   ZCL_POWER_CONFIG_CLUSTER_ID / ZCL_BATTERY_VOLTAGE_ATTRIBUTE_ID /
 *   ZCL_BATTERY_PERCENTAGE_REMAINING_ATTRIBUTE_ID
 *     .......... autogen/zap-id.h:73/86/87 (via af.h -> zap-id.h, af.h:98)
 *   ZCL_INT8U_ATTRIBUTE_TYPE .................... autogen/zap-type.h:26
 *   halCommonGetInt64uMillisecondTick — monotonic ms backed by the sleeptimer
 *     RTCC (keeps counting in EM2), 64-bit so it never wraps
 *     .......... platform/service/legacy_hal/inc/hal.h:99, implemented in
 *                legacy_hal/src/base-replacement.c:78-85 (via af.h -> hal/hal.h)
 ******************************************************************************/

#include "app/framework/include/af.h"   // sl_zigbee_event_*, emberAfWrite*, hal
#include "app_config.h"
#include "battery.h"
#include "remote_zigbee.h"              // REMOTE_ENDPOINT

#include "em_adc.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "sl_power_manager.h"

// ---------------------------------------------------------------------------
// Local hardware constants (the tunables live in app_config.h).
// ---------------------------------------------------------------------------
#define BAT_EN_PORT         gpioPortF       // AO3401 gate; low = divider ON
#define BAT_EN_PIN          2               // PF2 (200k external pull-up to VBAT)
#define BAT_SENSE_PORT      gpioPortB       // divider tap = VBAT/3
#define BAT_SENSE_PIN       11              // PB11 = APORT4X CH27 (see banner)
#define BAT_ADC             ADC0
#define BAT_ADC_POSSEL      adcPosSelAPORT4XCH27
#define BAT_ADC_CLOCK_HZ    1000000         // 1 MHz: slow+safe for 67k source
#define BAT_ADC_REF_MV      1250            // adcRef1V25
#define BAT_ADC_FULL_SCALE  4096            // 12-bit
#define BAT_ADC_POLL_MAX    100000          // conversion is ~45 us; ~10 ms cap

// Integer mV endpoints derived once from the float tunables (app_config.h
// keeps them as volts-as-float per spec). Arithmetic-constant-expression
// initializers, folded at compile time — no float math at runtime.
#define BATTERY_V_100_MV    ((uint16_t)(BATTERY_V_100 * 1000.0f))   // 3100
#define BATTERY_V_0_MV      ((uint16_t)(BATTERY_V_0 * 1000.0f))     // 2300

// ---------------------------------------------------------------------------
// AAA-alkaline pack discharge curve (F3): pack mV -> percent, descending,
// linearly interpolated between points. Endpoints are the configured clamps:
// >= BATTERY_V_100_MV (3.10 V) => 100 %, <= BATTERY_V_0_MV (2.30 V) => 0 %.
// Shape (per-cell values x2): a fresh cell falls quickly from ~1.55 V to the
// long ~1.45-1.30 V working plateau, then the knee steepens toward the 1.15
// V/cell floor imposed by the module's 2.2 V minimum — hence steep segments at
// both ends and a flat middle. Points hand-shaped from typical alkaline
// discharge data (e.g. Energizer E92 datasheet curves) compressed into the
// 3.10-2.30 V window.
// ---------------------------------------------------------------------------
typedef struct {
  uint16_t mv;        // pack voltage in mV
  uint8_t  percent;   // remaining capacity, 0-100
} battery_lut_entry_t;

static const battery_lut_entry_t battery_lut[] = {
  { BATTERY_V_100_MV, 100 },  // 3100 — fresh-cell surface charge, drops fast
  { 3000,              80 },
  { 2900,              65 },  // top of the working plateau
  { 2750,              50 },
  { 2600,              35 },  // plateau: ~10 %/100 mV through the middle
  { 2500,              25 },
  { 2400,              15 },  // knee: tail steepens
  { 2350,               8 },
  { BATTERY_V_0_MV,     0 },  // 2300 — module floor is 2.2 V, spec clamps here
};
#define BATTERY_LUT_LEN  (sizeof(battery_lut) / sizeof(battery_lut[0]))

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static sl_zigbee_event_t s_sample_event;    // fires BATTERY_SETTLE_MS after enable
static uint64_t s_last_measure_ms;          // throttle timestamp (monotonic ms)
static bool     s_measured_once;            // first call after boot always measures

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** @brief Pack mV -> percent via the LUT with linear interpolation. */
static uint8_t battery_percent_from_mv(uint16_t mv)
{
  if (mv >= battery_lut[0].mv) {
    return 100;
  }
  if (mv <= battery_lut[BATTERY_LUT_LEN - 1].mv) {
    return 0;
  }
  // Find the bracketing pair (entries are strictly descending in mv).
  for (uint8_t i = 1; i < BATTERY_LUT_LEN; i++) {
    if (mv >= battery_lut[i].mv) {
      uint16_t v_hi = battery_lut[i - 1].mv;
      uint16_t v_lo = battery_lut[i].mv;
      uint8_t  p_hi = battery_lut[i - 1].percent;
      uint8_t  p_lo = battery_lut[i].percent;
      // Linear interpolation, integer math, rounded to nearest percent.
      uint32_t num = (uint32_t)(p_hi - p_lo) * (mv - v_lo)
                     + (uint32_t)(v_hi - v_lo) / 2;
      return (uint8_t)(p_lo + num / (v_hi - v_lo));
    }
  }
  return 0;   // unreachable (endpoints handled above)
}

/** @brief Divider switch: drive PF2 low (FET on) or release it to the external
 *         200k pull-up (FET off, zero divider drain). */
static void battery_divider_enable(bool enable)
{
  if (enable) {
    GPIO_PinModeSet(BAT_EN_PORT, BAT_EN_PIN, gpioModePushPull, 0);
  } else {
    // High-Z (input disabled, no internal pull): the external 200k to VBAT
    // holds the AO3401 gate at the source -> FET off.
    GPIO_PinModeSet(BAT_EN_PORT, BAT_EN_PIN, gpioModeDisabled, 0);
  }
}

/** @brief One blocking 12-bit single conversion on PB11 (takes ~45 us at the
 *         1 MHz ADC clock; polled — no interrupt machinery for a one-shot).
 *  @return raw 12-bit sample, or negative on timeout. */
static int32_t battery_adc_sample(void)
{
  ADC_InitSingle_TypeDef single = ADC_INITSINGLE_DEFAULT;
  single.reference  = adcRef1V25;       // see banner: full range < 1.25 V
  single.resolution = adcRes12Bit;
  single.posSel     = BAT_ADC_POSSEL;   // PB11 (evidence in banner)
  single.negSel     = adcNegSelVSS;     // single-ended vs ground
  single.acqTime    = adcAcqTime32;     // 32 us: generous for 67k source Z
  ADC_InitSingle(BAT_ADC, &single);

  ADC_Start(BAT_ADC, adcStartSingle);

  // Poll the single-conversion data-valid flag with a hard iteration cap.
  uint32_t guard = BAT_ADC_POLL_MAX;
  while ((BAT_ADC->STATUS & ADC_STATUS_SINGLEDV) == 0u) {
    if (--guard == 0u) {
      return -1;
    }
  }
  return (int32_t)ADC_DataSingleGet(BAT_ADC);
}

/** @brief BATTERY_SETTLE_MS after enabling the divider: sample, restore the
 *         GPIO, convert, publish. */
static void battery_sample_handler(sl_zigbee_event_t *event)
{
  (void)event;

  // The conversion needs the HF clock tree; hold EM1 for its (~us) duration —
  // same pattern as the LED engine's PWM (led_effects.c).
  sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
  int32_t sample = battery_adc_sample();
  sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);

  battery_divider_enable(false);        // FET off again — zero sleep drain

  if (sample < 0) {
    TS_LOG("battery: ADC timeout");
    return;                             // throttle timestamp already set; retry
                                        // next interval, not next keypress
  }

  // sample/4096 * 1250 mV = node voltage; VBAT = node * DEN/NUM (VADC =
  // VBAT * NUM/DEN with NUM/DEN = 1/3). Integer math throughout; worst case
  // 4095 * 1250 * 3 ~= 15.4e6, well inside uint32_t.
  uint32_t vbat_mv = ((uint32_t)sample * BAT_ADC_REF_MV * BATTERY_DIVIDER_DEN)
                     / ((uint32_t)BAT_ADC_FULL_SCALE * BATTERY_DIVIDER_NUM);

  uint8_t percent = battery_percent_from_mv((uint16_t)vbat_mv);

  // ZCL encodings: BatteryVoltage 0x0020 in 100 mV units (rounded);
  // BatteryPercentageRemaining 0x0021 in half-percent units (0-200).
  uint8_t voltage_attr = (uint8_t)((vbat_mv + 50u) / 100u);
  uint8_t percent_attr = (uint8_t)(percent * 2u);

  // Local writes; the zigbee_reporting component (installed alongside this
  // module) watches attribute changes and sends the actual reports per the
  // intervals Z2M configured with ConfigureReporting — nothing to send here.
  EmberAfStatus vs = emberAfWriteServerAttribute(REMOTE_ENDPOINT,
                                                 ZCL_POWER_CONFIG_CLUSTER_ID,
                                                 ZCL_BATTERY_VOLTAGE_ATTRIBUTE_ID,
                                                 &voltage_attr,
                                                 ZCL_INT8U_ATTRIBUTE_TYPE);
  EmberAfStatus ps = emberAfWriteServerAttribute(REMOTE_ENDPOINT,
                                                 ZCL_POWER_CONFIG_CLUSTER_ID,
                                                 ZCL_BATTERY_PERCENTAGE_REMAINING_ATTRIBUTE_ID,
                                                 &percent_attr,
                                                 ZCL_INT8U_ATTRIBUTE_TYPE);

  TS_LOG("battery: %lu mV = %d%% (attr write 0x%02X/0x%02X)",
                              (unsigned long)vbat_mv, percent, vs, ps);
  (void)vs;   // attribute-write status only consumed by TS_LOG (prod: compiled out)
  (void)ps;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void battery_init(void)
{
  sl_zigbee_event_init(&s_sample_event, battery_sample_handler);
  s_last_measure_ms = 0;
  s_measured_once = false;

  CMU_ClockEnable(cmuClock_GPIO, true);

  // PF2 comes out of reset routed as JTAG TDO (GPIO ROUTEPEN reset value
  // 0x0F includes TDOPEN, efr32mg13p_gpio.h:1284/1296). This project debugs
  // over SWD (RTT), so release the JTAG TDO route and make PF2 a plain GPIO.
  GPIO->ROUTEPEN &= ~GPIO_ROUTEPEN_TDOPEN;
  battery_divider_enable(false);        // gate high-Z -> divider off

  // PB11: analog input for the APORT — input logic disabled, no pull.
  GPIO_PinModeSet(BAT_SENSE_PORT, BAT_SENSE_PIN, gpioModeDisabled, 0);

  // ADC0 common init: defaults + a >=1 us warmup timebase and a 1 MHz ADC
  // clock derived from the actual HFPERCLK (arg 0 = "use current clock",
  // em_adc.c:1012/1109). adcWarmupNormal (default) keeps the ADC powered off
  // between conversions. Registers are retained in EM2, so init once.
  CMU_ClockEnable(cmuClock_ADC0, true);
  ADC_Init_TypeDef init = ADC_INIT_DEFAULT;
  init.timebase = ADC_TimebaseCalc(0);
  init.prescale = ADC_PrescaleCalc(BAT_ADC_CLOCK_HZ, 0);
  ADC_Init(BAT_ADC, &init);
}

void battery_measure_on_wake(void)
{
  if (sl_zigbee_event_is_scheduled(&s_sample_event)) {
    return;                             // measurement already in flight
  }

  uint64_t now = halCommonGetInt64uMillisecondTick();
  if (s_measured_once
      && (now - s_last_measure_ms)
         < (uint64_t)BATTERY_MEASURE_MIN_INTERVAL_S * 1000u) {
    return;                             // throttled (F3): once per hour
  }
  s_measured_once = true;
  s_last_measure_ms = now;

  // Switch the divider in and let the 200k/100k node settle without busy-
  // waiting. No EM1 requirement is needed for the wait: if the chip did enter
  // EM2, the GPIO state (PF2 low) is retained and the sleeptimer-backed event
  // still fires on time.
  battery_divider_enable(true);
  sl_zigbee_event_set_delay_ms(&s_sample_event, BATTERY_SETTLE_MS);
}
