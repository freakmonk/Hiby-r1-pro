#!/bin/sh

# Determine the directory where this script is located
SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR" || exit 1
HOME_DIR=$(pwd)
SD_MOUNT=/data/mnt/sd_0

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

# Try to force executable bit just in case
chmod +x "$SD_MOUNT/.rockbox/rockbox" 2>/dev/null
cd "$SD_MOUNT"

# Run  – all stdout/stderr (including [doom-input] lines) goes to SD card log
"$SD_MOUNT/.rockbox/rockbox.r1"

log "rockbox exited."

bidhata-launcher.sh
