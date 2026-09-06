#!/bin/sh
# calc for Bidhata Menu

killall    hiby_player >/dev/null 2>&1
killall -9 hiby_player >/dev/null 2>&1
killall    bidhata-menu >/dev/null 2>&1
killall -9 bidhata-menu >/dev/null 2>&1
killall    bidhata-launcher.sh >/dev/null 2>&1
killall -9 bidhata-launcher.sh >/dev/null 2>&1

SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1

log() {
    echo "gb-launcher: $*" > /dev/console 2>/dev/null
}

log "Starting calc"

chmod +x calc 2>/dev/null

if [ -x "./calc" ]; then
    ./calc
fi

log "calc exited, returning to menu"

killall    calc >/dev/null 2>&1
killall -9 calc >/dev/null 2>&1

bidhata-launcher.sh
