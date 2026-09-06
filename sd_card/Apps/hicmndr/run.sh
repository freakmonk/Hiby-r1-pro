#!/bin/sh
# hicmndr for Bidhata Menu

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

log "Starting hicmndr"

chmod +x hicmndr 2>/dev/null

if [ -x "./hicmndr" ]; then
    ./hicmndr
fi

log "hicmndr exited, returning to menu"

killall    hicmndr >/dev/null 2>&1
killall -9 hicmndr >/dev/null 2>&1

bidhata-launcher.sh
