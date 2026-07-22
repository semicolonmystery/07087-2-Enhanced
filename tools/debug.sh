#!/bin/bash
# =============================================================================
# debug.sh — live debug console for the TS1001_TYZB01_7qf81wty_Enhanced remote (Raspberry Pi CM4)
#
# Modes:
#   ./debug.sh uart [baud]   Watch the firmware's serial debug log (default,
#                            best for testing buttons/sleep — debugger-free,
#                            so real EM2 sleep behavior is not disturbed)
#   ./debug.sh gdb           Start OpenOCD as a persistent GDB/telnet server
#                            (breakpoints, memory inspection from your PC)
#   ./debug.sh rtt           Stream SEGGER RTT output over SWD (only if the
#                            firmware was built with an RTT/iostream-rtt log)
#
# UART wiring (one extra jumper):
#   TYZS3 pin 16 - TXD  ->  Pi physical pin 10 (GPIO15, UART RX)
#   (GND is already shared via the SWD harness)
#   Optional two-way: TYZS3 pin 15 - RXD  <-  Pi physical pin 8 (GPIO14, TX)
#
# One-time Pi serial setup (then reboot):
#   sudo raspi-config  ->  Interface Options -> Serial Port
#     "login shell over serial?"  -> No
#     "serial port hardware enabled?" -> Yes
#   If /dev/serial0 still doesn't exist afterwards (CM4 with Bluetooth), add
#   to /boot/firmware/config.txt:   enable_uart=1
#                                   dtoverlay=disable-bt
# =============================================================================
set -euo pipefail

REAL_HOME=$(getent passwd "${SUDO_USER:-$USER}" | cut -d: -f6)
OPENOCD=${OPENOCD:-openocd}
OPENOCD_CFG=${OPENOCD_CFG:-$REAL_HOME/efr32.cfg}

mode=${1:-uart}

case "$mode" in
  uart)
    baud=${2:-115200}
    dev=""
    for d in /dev/serial0 /dev/ttyAMA0 /dev/ttyS0; do
        [ -e "$d" ] && { dev=$d; break; }
    done
    [ -n "$dev" ] || { echo "No serial device found — do the one-time Pi serial setup (see header of this script) and reboot."; exit 1; }
    echo "Listening on $dev @ $baud (Ctrl+C to stop)"
    echo "Wiring: TYZS3 pin 16 - TXD -> Pi pin 10 (GPIO15). Now press buttons on the remote..."
    stty -F "$dev" raw "$baud" -echo
    exec cat "$dev"
    ;;

  gdb)
    if [ "${EUID}" -ne 0 ]; then exec sudo OPENOCD_CFG="$OPENOCD_CFG" "$0" gdb; fi
    echo "Starting OpenOCD server: GDB on :3333, telnet on :4444 (Ctrl+C to stop)."
    echo "From your PC:  arm-none-eabi-gdb path/to/app.axf -ex 'target extended-remote testpi:3333'"
    echo "Note: while attached, the chip's sleep behavior is not representative."
    exec "$OPENOCD" -f "$OPENOCD_CFG" -c "bindto 0.0.0.0"
    ;;

  rtt)
    if [ "${EUID}" -ne 0 ]; then exec sudo OPENOCD_CFG="$OPENOCD_CFG" "$0" rtt; fi
    echo "Attaching and starting RTT server on :9090."
    echo "In a second terminal:  nc localhost 9090      (Ctrl+C here stops everything)"
    exec "$OPENOCD" -f "$OPENOCD_CFG" \
        -c "init" \
        -c "rtt setup 0x20000000 65536 \"SEGGER RTT\"" \
        -c "rtt start" \
        -c "rtt server start 9090 0"
    ;;

  *)
    echo "usage: $0 {uart [baud]|gdb|rtt}"
    exit 1
    ;;
esac
