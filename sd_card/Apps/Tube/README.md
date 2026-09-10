# Tube — YouTube Player for Hiby R1

A YouTube client for the **Hiby R1** digital audio player. It displays a browsable video feed from your subscribed channels and streams video directly to the device's framebuffer.

The project consists of two parts:
- **`tube`** — a C binary that runs on the Hiby R1 itself (MIPS architecture)
- **`proxy/`** — a Python HTTP server that runs on a separate machine (macOS or Linux/Debian) and does all the heavy YouTube work

> For proxy setup details, see [Proxy/README.md](Proxy/README.md).

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

## Deploying to the Device

Copy files to the Hiby R1:

- tube
- run.sh
- tube.cfg


Edit `tube.cfg` to contain the IP address of your proxy server (one line, no trailing spaces):

```
192.168.1.100
```

Run Proxy server, and give him 3 minutes for update feeds and sort 50 most fresh items.

Then run Tube app on the device.

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
