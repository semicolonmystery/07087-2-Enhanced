#!/bin/bash
# =============================================================================
# flash.sh — TS1001_TYZB01_7qf81wty_Enhanced remote flasher (EFR32MG13 / TYZS3 via OpenOCD, Pi CM4)
#
# Commands:
#   ./flash.sh check             Test the SWD link (halt, peek memory, resume)
#   ./flash.sh app [app.hex]     Erase + flash + verify the application, reboot
#   ./flash.sh boot [boot.s37]   Flash the Gecko bootloader (bootloader region)
#   ./flash.sh all               Flash bootloader + app in one session
#   ./flash.sh verify [app.hex]  Verify flash against the hex (no writes)
#   ./flash.sh backup            Explicit full dump into ./backup-<timestamp>/
#   ./flash.sh restore [main.bin] Write a stock main-flash dump back (undo)
#
# Notes:
#   - Defaults can be overridden: OPENOCD_CFG=/path/efr32.cfg APP_IMG=x.hex BOOT_IMG=y.s37
#   - Nothing here ever creates a backup unless you run "backup" yourself.
#   - The Bootloader s37 carries its own 0x0FE10000 addresses — programmed as-is.
#
# EFR32MG13P732F512GM48 memory layout (ground truth for this project):
#   0x00000000..0x00044000  application (272 KB; linker ORIGIN=0x0)
#   0x00044000..0x00074000  OTA storage slot 0 (192 KB, internal storage bootloader)
#   0x00077000..0x00080000  NVM3 (36 KB, top of main flash)
#   0x0FE00000 (2 KB)       user data page (manufacturing tokens — PRESERVE)
#   0x0FE10000 (16 KB)      bootloader region (first stage + main bootloader)
# =============================================================================
set -euo pipefail

# GPIO bit-bang needs root: re-exec under sudo transparently.
if [ "${EUID}" -ne 0 ]; then
    exec sudo OPENOCD_CFG="${OPENOCD_CFG:-}" APP_IMG="${APP_IMG:-}" BOOT_IMG="${BOOT_IMG:-}" "$0" "$@"
fi

# Resolve the invoking user's home so ~/efr32.cfg works under sudo too.
REAL_HOME=$(getent passwd "${SUDO_USER:-$USER}" | cut -d: -f6)
OPENOCD=${OPENOCD:-openocd}
OPENOCD_CFG=${OPENOCD_CFG:-$REAL_HOME/efr32.cfg}
APP_IMG_DEFAULT=${APP_IMG:-Zigbee-Remote-_TYZB01_7qf81wty.hex}
BOOT_IMG_DEFAULT=${BOOT_IMG:-bootloader-storage-internal-single-512k-combined.s37}

[ -f "$OPENOCD_CFG" ] || { echo "OpenOCD config not found: $OPENOCD_CFG"; exit 1; }

ocd() { "$OPENOCD" -f "$OPENOCD_CFG" -c "init" "$@" -c "shutdown"; }

cmd=${1:-}
arg=${2:-}

case "$cmd" in
  check)
    ocd -c "reset run" -c "halt" -c "mdw 0x00000000 4" -c "reset run"
    echo "SWD link OK — chip halted, memory readable, rebooted."
    ;;

  app)
    img=${arg:-$APP_IMG_DEFAULT}
    [ -f "$img" ] || { echo "App image not found: $img"; exit 1; }
    ocd -c "reset run" -c "halt" \
        -c "flash write_image erase $img" \
        -c "verify_image $img" \
        -c "reset run"
    echo "Flashed + verified: $img — device rebooted into it."
    ;;

  boot)
    img=${arg:-$BOOT_IMG_DEFAULT}
    [ -f "$img" ] || { echo "Bootloader image not found: $img"; exit 1; }
    ocd -c "reset run" -c "halt" \
        -c "flash write_image erase $img" \
        -c "verify_image $img" \
        -c "reset run"
    echo "Bootloader flashed + verified: $img — device rebooted."
    ;;

  all)
    app_img=${APP_IMG:-$APP_IMG_DEFAULT}
    boot_img=${BOOT_IMG:-$BOOT_IMG_DEFAULT}
    [ -f "$app_img" ] || { echo "App image not found: $app_img"; exit 1; }
    [ -f "$boot_img" ] || { echo "Bootloader image not found: $boot_img"; exit 1; }

    # Flashing bootloader first, then app, mapping to the newer specific halt commands
    ocd -c "reset run" -c "halt" \
        -c "flash write_image erase $boot_img" \
        -c "verify_image $boot_img" \
        -c "flash write_image erase $app_img" \
        -c "verify_image $app_img" \
        -c "reset run"
    echo "Flashed + verified: bootloader and app — device rebooted."
    ;;

  verify)
    img=${arg:-$APP_IMG_DEFAULT}
    [ -f "$img" ] || { echo "App image not found: $img"; exit 1; }
    ocd -c "reset run" -c "halt" -c "verify_image $img" -c "reset run"
    echo "Verify OK: flash matches $img"
    ;;

  backup)
    dir="backup-$(date +%Y%m%d-%H%M%S)"
    mkdir "$dir"
    ocd -c "reset run" -c "halt" \
        -c "dump_image $dir/stock_main.bin     0x00000000 0x80000" \
        -c "dump_image $dir/stock_userdata.bin 0x0FE00000 0x800" \
        -c "dump_image $dir/stock_lockbits.bin 0x0FE04000 0x800" \
        -c "dump_image $dir/stock_btl.bin      0x0FE10000 0x4000" \
        -c "reset run"
    chown -R "${SUDO_USER:-$USER}" "$dir"
    echo "Backup written to $dir/:"
    ls -l "$dir"
    ;;

  restore)
    img=${arg:-stock_main.bin}
    [ -f "$img" ] || { echo "Stock dump not found: $img (pass the path: ./flash.sh restore /path/stock_main.bin)"; exit 1; }
    sz=$(stat -c %s "$img")
    [ "$sz" -eq 524288 ] || { echo "Refusing: $img is $sz bytes, expected 524288 (full 512 KB main dump)."; exit 1; }
    ocd -c "reset run" -c "halt" \
        -c "flash write_image erase $img 0x00000000" \
        -c "verify_image $img 0x00000000" \
        -c "reset run"
    echo "Stock main flash restored from $img — device rebooted."
    ;;

  *)
    echo "usage: $0 {check|app|boot|all|verify|backup|restore} [image]"
    exit 1
    ;;
esac
