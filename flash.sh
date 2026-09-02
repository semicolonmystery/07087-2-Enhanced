#!/bin/bash
# =============================================================================
# flash.sh — TS1001_TYZB01_7qf81wty_Enhanced remote flasher (EFR32MG13 / TYZS3 via OpenOCD, Pi CM4)
#
# Commands:
#   ./flash.sh check              Test the SWD link (halt, peek memory, resume)
#   ./flash.sh backup             Full dump into backups/<timestamp>/ — DO THIS FIRST
#   ./flash.sh app [app.hex]      Erase + flash + verify the application, reboot
#   ./flash.sh boot [boot.s37]    Flash the Gecko bootloader (bootloader region)
#   ./flash.sh all                Flash bootloader + app in one session
#   ./flash.sh verify [app.hex]   Verify flash against the hex (no writes)
#   ./flash.sh restore <dir>      Restore every region found in a backup dir
#   ./flash.sh restore-main <bin> Restore only the 512 KB main-flash dump
#
# Paths are resolved relative to this script, so it works from any directory:
#   app image        build/bin/TS1001_TYZB01_7qf81wty_Enhanced.hex  (./fetch.sh puts it there)
#   bootloader       bin/bootloader-combined.s37                    (committed; static)
#   OpenOCD config   efr32.cfg
#   backups          backups/<timestamp>/
# Override any of them: OPENOCD_CFG=/path/efr32.cfg APP_IMG=x.hex BOOT_IMG=y.s37
#
# Typical first install on a Pi:
#   ./fetch.sh && ./flash.sh backup && ./flash.sh check && ./flash.sh all
#
# Notes:
#   - Auto-elevates with sudo (GPIO bit-banging needs root).
#   - Nothing here ever creates a backup unless you run "backup" yourself.
#   - The bootloader s37 carries its own 0x0FE10000 addresses — programmed as-is.
#
# EFR32MG13P732F512GM48 memory layout (ground truth for this project):
#   0x00000000..0x00044000  application (272 KB; linker ORIGIN=0x0)
#   0x00044000..0x00074000  OTA storage slot 0 (192 KB, internal storage bootloader)
#   0x00077000..0x00080000  NVM3 (36 KB, top of main flash)
#   0x0FE00000 (2 KB)       user data page (manufacturing tokens — PRESERVE)
#   0x0FE04000 (2 KB)       lock bits page
#   0x0FE10000 (16 KB)      bootloader region (first stage + main bootloader)
# =============================================================================
set -euo pipefail

# Resolve our own directory BEFORE the sudo re-exec, and carry it across, so
# every default path is anchored to the checkout rather than to $PWD or $HOME.
HERE="${FLASH_SH_HERE:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"

# GPIO bit-bang needs root: re-exec under sudo transparently.
if [ "${EUID}" -ne 0 ]; then
    exec sudo FLASH_SH_HERE="$HERE" OPENOCD="${OPENOCD:-}" \
         OPENOCD_CFG="${OPENOCD_CFG:-}" APP_IMG="${APP_IMG:-}" \
         BOOT_IMG="${BOOT_IMG:-}" "$0" "$@"
fi

OPENOCD=${OPENOCD:-openocd}
OPENOCD_CFG=${OPENOCD_CFG:-$HERE/efr32.cfg}
APP_IMG_DEFAULT=${APP_IMG:-$HERE/build/bin/TS1001_TYZB01_7qf81wty_Enhanced.hex}
BOOT_IMG_DEFAULT=${BOOT_IMG:-$HERE/bin/bootloader-combined.s37}

[ -f "$OPENOCD_CFG" ] || { echo "OpenOCD config not found: $OPENOCD_CFG"; exit 1; }

ocd() { "$OPENOCD" -f "$OPENOCD_CFG" -c "init" "$@" -c "shutdown"; }

# Regions written by "backup", and restored by "restore" — file, address, size.
# Keep in sync with the memory map above.
REGIONS=(
  "stock_main.bin      0x00000000  524288"
  "stock_userdata.bin  0x0FE00000  2048"
  "stock_lockbits.bin  0x0FE04000  2048"
  "stock_btl.bin       0x0FE10000  16384"
)

cmd=${1:-}
arg=${2:-}

case "$cmd" in
  check)
    ocd -c "reset run" -c "halt" -c "mdw 0x00000000 4" -c "reset run"
    echo "SWD link OK — chip halted, memory readable, rebooted."
    ;;

  app)
    img=${arg:-$APP_IMG_DEFAULT}
    [ -f "$img" ] || { echo "App image not found: $img (run ./fetch.sh first, or pass a path)"; exit 1; }
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
    [ -f "$app_img" ] || { echo "App image not found: $app_img (run ./fetch.sh first)"; exit 1; }
    [ -f "$boot_img" ] || { echo "Bootloader image not found: $boot_img"; exit 1; }

    # Bootloader first, then the app, in a single OpenOCD session.
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
    dir="$HERE/backups/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$dir"
    ocd -c "reset run" -c "halt" \
        -c "dump_image $dir/stock_main.bin     0x00000000 0x80000" \
        -c "dump_image $dir/stock_userdata.bin 0x0FE00000 0x800" \
        -c "dump_image $dir/stock_lockbits.bin 0x0FE04000 0x800" \
        -c "dump_image $dir/stock_btl.bin      0x0FE10000 0x4000" \
        -c "reset run"
    chown -R "${SUDO_USER:-$USER}" "$HERE/backups"
    echo "Backup written to $dir/:"
    ls -l "$dir"
    echo
    echo "This is your only copy of this device's stock firmware — keep it safe."
    echo "stock_userdata.bin holds this device's unique EUI64, so it is valid"
    echo "ONLY for this device. Never restore it onto a different one."
    ;;

  restore)
    dir=$arg
    [ -n "$dir" ] || { echo "usage: $0 restore <backup-dir>   (e.g. backups/20260902-120000)"; exit 1; }
    [ -d "$dir" ] || { echo "Backup directory not found: $dir"; exit 1; }

    # Check every present region before writing anything, so a wrong-sized file
    # cannot leave the device half-restored.
    found=0
    for r in "${REGIONS[@]}"; do
        read -r name addr size <<<"$r"
        f="$dir/$name"
        [ -f "$f" ] || continue
        sz=$(stat -c %s "$f")
        [ "$sz" -eq "$size" ] || { echo "Refusing: $f is $sz bytes, expected $size."; exit 1; }
        found=$((found + 1))
    done
    [ "$found" -gt 0 ] || { echo "No stock_*.bin files found in $dir"; exit 1; }

    args=(-c "reset run" -c "halt")
    for r in "${REGIONS[@]}"; do
        read -r name addr size <<<"$r"
        f="$dir/$name"
        [ -f "$f" ] || continue
        echo "  will restore $name -> $addr ($size bytes)"
        args+=(-c "flash write_image erase $f $addr" -c "verify_image $f $addr")
    done
    args+=(-c "reset run")
    ocd "${args[@]}"
    echo "Restored $found region(s) from $dir — device rebooted."
    ;;

  restore-main)
    img=$arg
    [ -n "$img" ] || { echo "usage: $0 restore-main <stock_main.bin>"; exit 1; }
    [ -f "$img" ] || { echo "Stock dump not found: $img"; exit 1; }
    sz=$(stat -c %s "$img")
    [ "$sz" -eq 524288 ] || { echo "Refusing: $img is $sz bytes, expected 524288 (full 512 KB main dump)."; exit 1; }
    ocd -c "reset run" -c "halt" \
        -c "flash write_image erase $img 0x00000000" \
        -c "verify_image $img 0x00000000" \
        -c "reset run"
    echo "Stock main flash restored from $img — device rebooted."
    ;;

  *)
    sed -n '3,36p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
