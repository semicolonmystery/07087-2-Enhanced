/**
 * ts1001-cus.js — Zigbee2MQTT external converter for the DIY TS1001 remote
 * (custom firmware; stock device identified as TS1001 / _TYZB01_7qf81wty,
 *  sold as Immax NEO Smart Remote v2 / Müller Licht tint remote).
 *
 * Install: copy into Z2M's external converters folder (data/external_converters/
 * or configure `external_converters:` in configuration.yaml), restart Z2M.
 *
 * The custom firmware fingerprints as manufacturerName "DIY-Immax",
 * modelID "TS1001-CUS" — deliberately NOT the stock strings, so Z2M never
 * loads the stock Tuya/Müller-Licht definition for it.
 */
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const e = exposes.presets;

const definition = {
    fingerprint: [{modelID: 'TS1001-CUS', manufacturerName: 'DIY-Immax'}],
    model: 'TS1001-CUS',
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
            ['genOnOff', 'genLevelCtrl', 'lightingColorCtrl', 'genPowerCfg']);
        await reporting.batteryPercentageRemaining(endpoint);
        await reporting.batteryVoltage(endpoint);
    },
};

module.exports = definition;
