import http.server
import socketserver
import subprocess
import urllib.parse
import sys

PORT = 8080

import feed_manager

feed_mgr = feed_manager.FeedManager()

class YTProxyHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed_path = urllib.parse.urlparse(self.path)
        
        if parsed_path.path == '/feed/list':
            lst = feed_mgr.get_list()
            self.send_response(200)
            self.send_header("Content-type", "text/plain")
            self.end_headers()
            self.wfile.write(lst.encode())
            return
            
        elif parsed_path.path == '/feed/page':
            query = urllib.parse.parse_qs(parsed_path.query)
            page_idx = int(query.get('p', ['0'])[0])
            data = feed_mgr.get_page(page_idx)
            self.send_response(200)
            self.send_header("Content-type", "application/octet-stream")
            self.end_headers()
            self.wfile.write(data)
            return
            
        elif parsed_path.path == '/stream':
            query = urllib.parse.parse_qs(parsed_path.query)
            video_id = query.get('v', [''])[0]
            start_time = query.get('ss', ['0'])[0]
            
            if not video_id:
                self.send_response(400)
                self.end_headers()
                self.wfile.write(b"Missing video id (v=)")
                return

            print(f"Streaming video {video_id} starting at {start_time}s...")
            
            # Send HTTP OK header
            self.send_response(200)
            self.send_header("Content-type", "video/mp4")
            self.end_headers()

            # Run yt-dlp to download and output to stdout (-o -)
            # Pipe it to ffmpeg to scale to 426x240 (240p)
            # Check if video is vertical
            is_vertical = False
            info_cmd = ["yt-dlp", "--print", "%(width)s|%(height)s", f"https://www.youtube.com/watch?v={video_id}"]
            try:
                proc = subprocess.run(info_cmd, capture_output=True, text=True, timeout=10)
                lines = proc.stdout.strip().split('\n')
                for line in lines:
                    if '|' in line:
                        w, h = line.split('|')
                        if w.isdigit() and h.isdigit():
                            if int(h) > int(w):
                                is_vertical = True
                            break
            except Exception as e:
                print("Failed to get video dimensions:", e)

            vf_filter = ""
            if is_vertical:
                # Scale to fit 448x800, keep aspect ratio, pad with black
                vf_filter = "scale=448:800:force_original_aspect_ratio=decrease,pad=448:800:(ow-iw)/2:(oh-ih)/2"
            else:
                # Horizontal: scale to 800x448, transpose to 448x800, keep aspect ratio, pad
                vf_filter = "scale=800:448:force_original_aspect_ratio=decrease,pad=800:448:(ow-iw)/2:(oh-ih)/2,transpose=1"

            # Get direct URL
            yt_cmd = [
                "yt-dlp",
                "-f", "18",
                "-g",
                "--extractor-args", "youtube:player_client=android",
                f"https://www.youtube.com/watch?v={video_id}"
            ]
            try:
                proc = subprocess.run(yt_cmd, capture_output=True, text=True, timeout=15)
                direct_url = proc.stdout.strip().split('\n')[-1] # the last line is the URL (ignore warnings)
                if not direct_url.startswith('http'):
                    raise ValueError("Direct URL not found")
            except Exception as e:
                print(f"Failed to get direct URL: {e}")
                self.send_response(500)
                self.end_headers()
                return

            ffmpeg_cmd = [
                "ffmpeg",
                "-hide_banner", "-loglevel", "error",
                "-reconnect", "1",
                "-reconnect_streamed", "1",
                "-reconnect_delay_max", "5",
                "-ss", start_time,
                "-i", direct_url,
                "-c:v", "libx264",
                "-preset", "ultrafast",
                "-tune", "zerolatency",
                "-vf", vf_filter,
                "-r", "15",
                "-b:v", "900k",
                "-c:a", "copy",
                "-f", "mpegts",
                "pipe:1"
            ]
            
            try:
                ffmpeg_proc = subprocess.Popen(ffmpeg_cmd, stdout=subprocess.PIPE)
                
                # Stream chunk by chunk from ffmpeg stdout
                while True:
                    chunk = ffmpeg_proc.stdout.read(65536)
                    if not chunk:
                        break
                    try:
                        self.wfile.write(chunk)
                    except OSError:
                        break
                        
                ffmpeg_proc.terminate()
                ffmpeg_proc.wait()
            except Exception as e:
                print(f"Streaming error: {e}")
        elif parsed_path.path == '/duration':
            query = urllib.parse.parse_qs(parsed_path.query)
            video_id = query.get('v', [''])[0]
            if not video_id:
                self.send_response(400)
                self.end_headers()
                return

            cmd = [
                "yt-dlp",
                "--print", "duration",
                f"https://www.youtube.com/watch?v={video_id}"
            ]
            try:
                proc = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
                dur_str = proc.stdout.strip()
                self.send_response(200)
                self.send_header("Content-type", "text/plain")
                self.end_headers()
                self.wfile.write(dur_str.encode())
            except Exception as e:
                print(f"Duration error: {e}")
                self.send_response(500)
                self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()

if __name__ == "__main__":
    # Allow address reuse
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    with socketserver.ThreadingTCPServer(("0.0.0.0", PORT), YTProxyHandler) as httpd:
        print(f"Serving proxy on port {PORT}")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down proxy")
            sys.exit(0)
