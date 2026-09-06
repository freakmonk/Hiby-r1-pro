#!/bin/sh
# Launcher for Bidhata Menu

killall    hiby_player >/dev/null 2>&1
killall -9 hiby_player >/dev/null 2>&1
killall    bidhata-menu >/dev/null 2>&1
killall -9 bidhata-menu >/dev/null 2>&1
killall    bidhata-launcher.sh >/dev/null 2>&1
killall -9 bidhata-launcher.sh >/dev/null 2>&1

SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1

log() {
    echo "InfoNES: $*" > /dev/console 2>/dev/null
}

log "Starting InfoNES"

chmod +x InfoNES 2>/dev/null

if [ -x "./InfoNES" ]; then
    ./InfoNES
fi

log "InfoNES exited, returning to menu"

killall    InfoNES >/dev/null 2>&1
killall -9 InfoNES >/dev/null 2>&1

bidhata-launcher.sh
