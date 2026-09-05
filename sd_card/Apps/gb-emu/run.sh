#!/bin/sh
# GameBoy Emulator Launcher for Bidhata Menu

SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1

log() {
    echo "gb-launcher: $*" > /dev/console 2>/dev/null
}

log "Starting GameBoy emulator"

chmod +x gb-emu 2>/dev/null

if [ -x "./gb-emu" ]; then
    ./gb-emu
elif [ -f "./gb-emu" ]; then
    sh ./gb-emu # won't work for a binary, but fallback
else
    /usr/bin/gb-emu
fi

log "gb-emu exited, returning to menu"
