# Tube — YouTube Player for Hiby R1

A YouTube client for the **Hiby R1** digital audio player. It displays a browsable video feed from your subscribed channels and streams video directly to the device's framebuffer.

The project consists of two parts:
- **`tube`** — a C binary that runs on the Hiby R1 itself (MIPS architecture)
- **`proxy/`** — a Python HTTP server that runs on a separate machine (macOS or Linux/Debian) and does all the heavy YouTube work

> For proxy setup details, see [proxy/README.md](proxy/README.md).

---

## How It Works

The Hiby R1's XBurst JZ4760B CPU is too underpowered to handle TLS connections, YouTube's API, font rendering, or video decoding at a reasonable quality. All of that is offloaded to the proxy server:

```
[Hiby R1 / tube binary]  <--HTTP-->  [Proxy Server / yt_proxy.py]  <--HTTPS-->  [YouTube]
```

1. On launch, `tube` immediately displays a **splash screen** (embedded directly in the binary as raw RGB565 pixels).
2. It connects to Wi-Fi and enters **Feed Mode** — a paginated thumbnail grid of the 50 most recent videos from your subscribed channels.
3. Tapping a video thumbnail launches **Video Mode** — `tube` requests the video stream from the proxy, which transcodes it on the fly using `ffmpeg` and sends it as an MPEG-TS stream back to the device over HTTP.
4. The binary decodes the MPEG-TS stream using FFmpeg libraries and renders each frame directly into `/dev/fb0` (the Linux framebuffer).

---

## Architecture

### Player binary (`tube`)

Written in C, cross-compiled for MIPS using the Ingenic toolchain inside a Docker container. It has no GUI framework — all rendering is done by writing RGB565 pixels directly to the framebuffer via `mmap`.

| Component | Description |
|---|---|
| `main.c` | Main source — framebuffer rendering, input handling, feed UI, video playback loop |
| `splash.h` | Startup splash screen — pre-converted from `image.png` to a raw RGB565 C array |
| `loading.h` | "Loading video..." screen shown when opening a video — same format |
| `font.h` | 8×8 bitmap font for drawing seek labels (`+35sec`) over the frozen video frame |
| `tube.cfg` | One-line config file: the IP address of the proxy server |

### Proxy server (`proxy/`)

A Python `http.server`-based proxy with these main endpoints:

| Endpoint | Description |
|---|---|
| `GET /feed/list` | Returns a newline-separated list of video IDs for the current feed |
| `GET /feed/page?p=N` | Returns a raw 480×800 RGB565 binary blob — a pre-rendered page image ready to be blitted to the framebuffer |
| `GET /stream?v=ID&ss=SEC` | Fetches a direct URL via `yt-dlp`, then streams a transcoded MPEG-TS video via `ffmpeg` |
| `GET /duration?v=ID` | Returns the duration of a video in seconds |

The feed is managed by `feed_manager.py`, which runs as a background thread and refreshes every **6 hours**. Only full videos are shown — YouTube Shorts (duration < 60s) are filtered out.

---

## Controls

### Feed Mode (video grid)

| Button | Action |
|---|---|
| **Vol+** | Previous page |
| **Vol-** | Next page |
| **Tap** | Open video for playback |
| **Power** | Exit the app |

### Video Mode (playback)

| Button / Gesture | Action |
|---|---|
| **Tap right half** | Seek forward 10% of total duration |
| **Tap left half** | Seek backward 10% of total duration |
| **Vol+** | Volume up (+0.2%) |
| **Vol-** | Volume down (−0.2%) |
| **Power** | Return to Feed (same page you left) |

> The device is held **90° counter-clockwise** for landscape video playback. The framebuffer is native portrait (480×800); the proxy pre-rotates horizontal videos 90° CW via `ffmpeg` so they appear correctly. Vertical videos are not rotated — they are padded with black bars instead.

Volume level is saved to `/tmp/tube_vol` on the device and persists between videos.

---

## Building

The binary is cross-compiled for MIPS inside a Docker container using the **Ingenic toolchain** and a statically linked FFmpeg 7.1.1.

### Prerequisites
- **Docker** (with `linux/amd64` emulation support — on Apple Silicon this is handled automatically via Rosetta/QEMU)

### Steps

```bash
cd tube_src
./build.sh
```

This will:
1. Build a Docker image with the Ingenic cross-compiler and a statically-linked FFmpeg
2. Cross-compile `main.c` for MIPS32r2
3. Output the `tube` binary (~11 MB, statically linked) in `tube_src/`

> The first build downloads and compiles FFmpeg (~10–15 min). Subsequent builds are cached and take only a few seconds.

### Updating the splash screen

If you replace `image.png`, regenerate the C header before building:

```bash
# From the project root (not tube_src/)
python3 make_splash.py
cd tube_src && ./build.sh
```

---

## Deploying to the Device

Copy both files to the Hiby R1 via SSH or SCP:

```bash
scp tube_src/tube root@<DEVICE_IP>:/path/to/tube
scp tube_src/tube.cfg root@<DEVICE_IP>:/path/to/tube.cfg
```

Edit `tube.cfg` to contain the IP address of your proxy server (one line, no trailing spaces):

```
192.168.1.100
```

Then run on the device:

```bash
./tube
```

---

## File Structure

```
tube_src/
├── main.c              # Player source code
├── font.h              # Embedded 8x8 bitmap font (generated by make_font.py)
├── splash.h            # Embedded splash screen (generated by make_splash.py)
├── loading.h           # Embedded loading screen (generated by make_loading.py)
├── image.png           # Source splash screen image (480x800)
├── tube.cfg            # Proxy server IP address
├── tube                # Compiled MIPS binary (ready to deploy)
├── Dockerfile          # Cross-compilation environment definition
├── Makefile            # Build rules
├── build.sh            # Build entry point (Docker wrapper)
└── proxy/              # Proxy server — see proxy/README.md
    ├── yt_proxy.py         # HTTP server, stream handler
    ├── feed_manager.py     # Background feed updater + page renderer
    ├── extract_channels.py # Extracts subscriptions from ltm-database.json
    ├── proxy.conf          # Channel list (ID,Name per line)
    └── README.md
```

---

## Hardware Reference

| Spec | Value |
|---|---|
| CPU | Ingenic XBurst JZ4760B (MIPS32r2, ~432 MHz) |
| Display | 480×800 IPS, `/dev/fb0`, 16bpp RGB565 |
| Audio | ALSA (`snd_pcm_open` loaded via `dlopen`) |
| Input | Linux input events (`/dev/input/eventX`) — touch + hardware buttons |
| OS | Embedded Linux (BusyBox) |
| Toolchain | mips-linux-gnu-gcc 5.2.0, glibc 2.22 |
