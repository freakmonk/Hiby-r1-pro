#!/bin/sh
# Doom Launcher for HiBy R1
# Kills the HiBy music player, mounts SD card, and runs Doom

# Determine the directory where this script is located
SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1
DOOM_DIR=$(pwd)
SD_MOUNT=/data/mnt/sd_0

# Log goes to SD card so user can read it after game exits
LOG_FILE="$DOOM_DIR/doom.log"

log() {
    echo "doom-launcher: $*" > /dev/console 2>/dev/null
    echo "doom-launcher: $*" >> "$LOG_FILE"
}

log "Starting Doom launcher from $DOOM_DIR..."

# Free GPU/Framebuffer and audio by killing hiby_player
killall    hiby_player >/dev/null 2>&1
killall -9 hiby_player >/dev/null 2>&1
killall    bidhata-menu >/dev/null 2>&1
killall -9 bidhata-menu >/dev/null 2>&1
killall    bidhata-launcher.sh >/dev/null 2>&1
killall -9 bidhata-launcher.sh >/dev/null 2>&1

# Mount SD card if unmounted (though bidhata-launcher should have done this)
if ! grep -q " $SD_MOUNT " /proc/mounts 2>/dev/null; then
    mkdir -p "$SD_MOUNT" 2>/dev/null
    for dev in /dev/mmcblk0p1 /dev/mmcblk0 /dev/mmcblk1p1 /dev/mmcblk1; do
        [ -b "$dev" ] || continue
        mount -t vfat,exfat -o rw "$dev" "$SD_MOUNT" 2>/dev/null || \
        mount -o rw "$dev" "$SD_MOUNT" 2>/dev/null && break
    done
fi

# Determine path to doom executable (MicroSD binary > /usr/data/doom > /usr/bin/doom)
DOOM_BIN="$DOOM_DIR/doom"

# Try to force executable bit just in case
chmod +x "$DOOM_BIN" 2>/dev/null

log "Binary: $DOOM_BIN"
cd "$DOOM_DIR"

# Run doom – all stdout/stderr (including [doom-input] lines) goes to SD card log
"$DOOM_BIN" >> "$LOG_FILE" 2>&1

log "Doom exited."

bidhata-launcher.sh
