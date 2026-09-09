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
    echo "launcher: $*" > /dev/console 2>/dev/null
}

log "Starting ..."

chmod +x open_hiby_player 2>/dev/null

if [ -x "./open_hiby_player" ]; then
    ./open_hiby_player
fi

log "open_hiby_player exited, returning to menu"

killall    open_hiby_player >/dev/null 2>&1
killall -9 open_hiby_player >/dev/null 2>&1

reboot
