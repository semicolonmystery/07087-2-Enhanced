/**
 * ts1001-tyzb01-enhanced.js — Zigbee2MQTT external converter for the DIY TS1001 remote
 * (custom firmware; stock device identified as TS1001 / _TYZB01_7qf81wty,
 *  sold as Immax NEO Smart Remote v2 / Müller Licht tint remote).
 *
 * Install: copy into Z2M's external converters folder (data/external_converters/
 * or configure `external_converters:` in configuration.yaml), restart Z2M.
 *
 * The custom firmware fingerprints as manufacturerName "DIY-Immax",
 * modelID "TS1001_TYZB01_7qf81wty_Enhanced" — deliberately NOT the stock strings, so Z2M never
 * loads the stock Tuya/Müller-Licht definition for it.
 */
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const e = exposes.presets;

const definition = {
    fingerprint: [{modelID: 'TS1001_TYZB01_7qf81wty_Enhanced', manufacturerName: 'DIY-Immax'}],
    model: 'TS1001_TYZB01_7qf81wty_Enhanced',
    vendor: 'Immax NEO (DIY firmware)',
    description: 'Immax NEO Smart Remote v2 with custom open firmware ' +
                 '(hardware: Tuya TS1001 / _TYZB01_7qf81wty, TYZS3 module)',
    fromZigbee: [
        fz.battery,
        fz.command_on,
        fz.command_off,
        fz.command_step,                    // brightness_step_up / _down
        fz.command_move,                    // brightness_move_up / _down
        fz.command_stop,                    // brightness_stop
        fz.command_step_color_temperature,  // color_temperature_step_up / _down
        fz.command_move_color_temperature,  // color_temperature_move_up / _down / stop
    ],
    toZigbee: [],
    exposes: [
        e.battery(),
        e.battery_voltage(),
        e.action([
            'on', 'off',
            'brightness_step_up', 'brightness_step_down',
            'brightness_move_up', 'brightness_move_down', 'brightness_stop',
            'color_temperature_step_up', 'color_temperature_step_down',
            'color_temperature_move_up', 'color_temperature_move_down',
            'color_temperature_stop',
        ]),
    ],
    ota: true,
    configure: async (device, coordinatorEndpoint) => {
        const endpoint = device.getEndpoint(1);
        // Bind command clusters to the coordinator so actions are logged in Z2M,
        // and Power Configuration for battery reporting. These coexist with any
        // light/group bindings added later via the Z2M "Bind" tab.
        // NOTE: the remote is a sleepy end device — wake it (press any button)
        // right when running configure/bind so the requests reach it.
        await reporting.bind(endpoint, coordinatorEndpoint,
            ['genOnOff', 'genLevelCtrl', 'lightingColorCtrl', 'genPowerCfg',
             'genPollCtrl']);
        await reporting.batteryPercentageRemaining(endpoint);
        await reporting.batteryVoltage(endpoint);

        // Poll Control (genPollCtrl / cluster 0x0020), same arrangement IKEA
        // remotes use. The bind above is what makes the cluster do anything at
        // all: the firmware only sends Check-in commands to devices sitting in
        // its Poll Control binding table, so without this bind the cluster is
        // inert. Check-ins are how a sleepy remote stays reachable — a Z2M ping
        // can never reach it, because its parent only holds indirect messages
        // for ~7.68 s, far below the 30 min long poll interval.
        //
        // 14400 quarter-seconds = 1 h, matching POLL_CTRL_CHECK_IN_QS in the
        // firmware's app_config.h. Keep the two in sync if you change either.
        // If this write is not answered the firmware simply short-polls for 8 s
        // and returns to its long poll — unanswered check-ins are a no-op, not
        // a failure, so a Z2M version that ignores check-ins costs nothing.
        await endpoint.write('genPollCtrl', {checkinInterval: 14400});
    },
};

module.exports = definition;
