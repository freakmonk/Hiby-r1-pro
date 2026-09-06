#!/bin/sh
# Launcher for Bidhata Menu

SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1

log() {
    echo "raw: $*" > /dev/console 2>/dev/null
}

log "Starting raw"

killall    hiby_player >/dev/null 2>&1
killall -9 hiby_player >/dev/null 2>&1
killall    bidhata-menu >/dev/null 2>&1
killall -9 bidhata-menu >/dev/null 2>&1
killall    bidhata-launcher.sh >/dev/null 2>&1
killall -9 bidhata-launcher.sh >/dev/null 2>&1

chmod +x raw 2>/dev/null

if [ -x "./raw" ]; then
    ./raw &>/dev/null
fi

log "raw exited, returning to menu"

killall    raw >/dev/null 2>&1
killall -9 raw >/dev/null 2>&1

bidhata-launcher.sh