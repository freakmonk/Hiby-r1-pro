#!/bin/sh
# mplayer    Launcher for Bidhata Menu

SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1

log() {
    echo "mplayer: $*" > /dev/console 2>/dev/null
}

log "Starting mplayer"

killall    hiby_player >/dev/null 2>&1
killall -9 hiby_player >/dev/null 2>&1
killall    bidhata-menu >/dev/null 2>&1
killall -9 bidhata-menu >/dev/null 2>&1

chmod +x mplayer 2>/dev/null

if [ -x "./mplayer" ]; then
    ./mplayer 1.mp4
elif [ -f "./mplayer" ]; then
    sh ./mplayer 1.mp4 # won't work for a binary, but fallback
else
    /usr/bin/mplayer 1.mp4
fi

log "mplayer exited, returning to menu"
