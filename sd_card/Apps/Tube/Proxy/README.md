# Hiby R1 YouTube Proxy

This proxy server handles downloading videos from YouTube, processing images (generating the player's menu interface), and "repackaging" video streams into a format that the Hiby R1 player's underpowered processor can play smoothly.

Since the Hiby R1 player does not have enough processing power for font rendering, TLS/HTTPS connections, or complex YouTube parsing, this proxy handles all the "heavy lifting."

## Requirements (Dependencies)

The following system utilities and libraries are required for the proxy to work:

### 1. System Programs
*   **Python 3.8+**
*   **yt-dlp** - required for extracting direct video links and retrieving channel/duration information.
    *   *Installation (macOS):* `brew install yt-dlp`
*   **ffmpeg** - used for transcoding video "on the fly" (cropping edges, resizing, converting to `mpegts`, and automatic reconnection).
    *   *Installation (macOS):* `brew install ffmpeg`

### 2. Python Libraries
*   **Pillow (PIL)** - required by `feed_manager.py` (downloading video thumbnails, overlaying text, and generating the final RGB565 menu "frame").
    *   *Installation:* `pip3 install Pillow`

## Subscription Setup

The proxy reads the channel list from `proxy.conf` (format: `channel_ID,channel_name`).
To generate this file from your `ltm-database.json` database used by [localtube-manager](https://github.com/abhishekY495/localtube-manager):
1. Place your `ltm-database.json` file in the `proxy` directory.
2. Run the script:
   ```bash
   ./extract_channels.py
   ```
It will automatically extract all channels and create the correct `proxy.conf`. You can also edit `proxy.conf` manually by adding or removing lines.

## Running

1. Make sure that `proxy.conf` has been created and populated.
2. Run the main script:
   ```bash
   python3 yt_proxy.py
   ```

After startup, the server will listen on port `8080` (by default).
When the proxy starts, it immediately begins polling the channels listed in `proxy.conf` in the background. The list of new videos (up to 50) is updated every 30 minutes.

## IP Configuration
Make sure that your server IP address (where the proxy is running) matches the address specified in the `tube.cfg` file on the Hiby R1 player.
