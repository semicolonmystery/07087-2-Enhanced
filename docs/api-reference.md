# Verified API reference (GSDK 4.4.6 / EmberZNet 7.4.x)

Signatures verified against `SDKs/gecko_sdk/...` and the project's generated
`autogen/zap-command.h`. Subagents: trust these; re-verify only if a build error
contradicts them.

## Send a filled command to ALL bindings (unicast lights + groups)
```c
#include "app/framework/include/af.h"
emberAfGetCommandApsFrame()->sourceEndpoint = 1;   // our endpoint
emberAf<Fill command below>;
EmberStatus u = emberAfSendCommandUnicastToBindings();    // af.h:1578
EmberStatus m = emberAfSendCommandMulticastToBindings();  // af.h:1619
// Treat "delivered" as: at least one of u/m == EMBER_SUCCESS. Else cache (F7).
```
Rationale: reporting.c:363-367 uses exactly this unicast+multicast pair.

## On/Off (client → server), no args
```c
emberAfFillCommandOnOffClusterOn();
emberAfFillCommandOnOffClusterOff();
```

## Level Control (client → server)
```c
// stepMode: 0x00 = Up, 0x01 = Down. stepSize u8, transitionTime u16 (0.1s units)
emberAfFillCommandLevelControlClusterStep(stepMode, stepSize, transitionTime, optMask, optOverride);
// moveMode: 0x00 = Up, 0x01 = Down. rate u8 (units/s)
emberAfFillCommandLevelControlClusterMove(moveMode, rate, optMask, optOverride);
emberAfFillCommandLevelControlClusterStop(optMask, optOverride);
// optMask/optOverride: pass 0,0 (no options override).
```

## Color Control — color temperature (client → server)
```c
// stepMode: 0x01 = Up (increase mireds/warmer), 0x03 = Down (decrease/cooler)
// stepSize u16 (mireds), transitionTime u16, ctMin/ctMax u16 (mired bounds)
emberAfFillCommandColorControlClusterStepColorTemperature(
    stepMode, stepSize, transitionTime, ctMin, ctMax, optMask, optOverride);
// moveMode: 0x00 = Stop, 0x01 = Up, 0x03 = Down. rate u16 (mireds/s)
emberAfFillCommandColorControlClusterMoveColorTemperature(
    moveMode, rate, ctMin, ctMax, optMask, optOverride);
// NOTE: fill macros are generated globally — Color Control client only needs to be
// enabled on EP1 in ZAP; no per-command enable required (verified in autogen).
```
> CONFIRMED (M5): the SDK color-control server decodes exactly
> Stop=0x00 / Up=0x01 / Down=0x03 for BOTH MoveColorTemperature and
> StepColorTemperature (`color-control-server.c:24-26`, Move :1341/:1364,
> Step :1436/:1454). On Stop the server returns before reading rate/bounds
> (:1341-1344). Fill-macro param order re-verified in project
> `autogen/zap-command.h` (Move :5982, Step :6011).

## Network / state
```c
EmberNetworkStatus emberAfNetworkState(void);      // EMBER_JOINED_NETWORK, EMBER_NO_NETWORK...
EmberStatus emberAfPluginNetworkSteeringStart(void);
EmberStatus emberLeaveNetwork(void);               // F9 pairing/reset
void emberAfStackStatusCallback(EmberStatus status); // EMBER_NETWORK_UP/DOWN → cache flush hook (F7)
```

## Attribute write (Power Config reporting, F3) — verify in M6
```c
// emberAfWriteServerAttribute(endpoint, clusterId, attrId, dataPtr, dataType) — confirm signature
// Power Config: BatteryVoltage 0x0020 (u8, 100mV units), BatteryPercentageRemaining 0x0021 (u8, 0.5% units)
```
