#!/bin/sh
# GameBoy Emulator Launcher for Bidhata Menu

# Free GPU/Framebuffer and audio by killing hiby_player
killall    hiby_player >/dev/null 2>&1
killall -9 hiby_player >/dev/null 2>&1
killall    bidhata-menu >/dev/null 2>&1
killall -9 bidhata-menu >/dev/null 2>&1

SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1

log() {
    echo "gb-launcher: $*" > /dev/console 2>/dev/null
}

log "Starting GameBoy emulator"

chmod +x gb-emu 2>/dev/null

if [ -x "./gb-emu" ]; then
    ./gb-emu
fi

log "gb-emu exited, returning to menu"

reboot
