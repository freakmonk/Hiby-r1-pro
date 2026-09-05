# Audiobook Standalone for Hiby R1

This is the extracted, standalone version of the Hiby R1 Audiobook app.
It runs directly on the device (no `LD_PRELOAD` required) and bundles the file manager, UI, and playback engine.

## Building

Because the player engine dynamically loads ALSA and AAC libraries at runtime via `dlopen()`, the binary must be dynamically linked against the device's exact libc version (**glibc 2.22**).

Standard cross-compilers (like Debian's `gcc-mipsel-linux-gnu`) usually target newer glibc versions (e.g. 2.36), which will result in a crash on the device.

To build it correctly, use **Zig** as a C compiler, which can cross-compile to specific glibc versions out of the box:

```bash
# 1. Install Zig (0.13.0 or later)
# macOS: brew install zig
# linux: download from ziglang.org

# 2. Build the project
make
# or: zig cc -target mipsel-linux-gnueabihf.2.22 -O2 -s -DSTANDALONE ... (see Makefile)
```

## Installation

1. Copy the resulting `Audiobook` binary to your SD card.
2. Launch it via a shell script or custom launcher on the Hiby R1.

## Architecture

- **`main.c`**: Standalone entry point. Maps `/dev/fb0`, initializes a software backbuffer, and launches the UI.
- **`ui.c`**: The main event loop. Modified for `STANDALONE` mode to draw to the backbuffer and copy it to the hardware framebuffer.
- **Input**: Uses direct `/dev/input/event*` devices instead of stealing file descriptors from `hiby_player`.
