import json
import urllib.request
import urllib.parse
import xml.etree.ElementTree as ET
import threading
import time
from datetime import datetime
from PIL import Image, ImageDraw, ImageFont
import io
import os

DB_PATH = 'ltm-database.json'
CACHE_DIR = 'thumb_cache'

class FeedManager:
    def __init__(self):
        self.videos = []
        self.pages = {} # page_index -> raw RGB565 bytes
        self.lock = threading.Lock()
        self.running = True
        
        if not os.path.exists(CACHE_DIR):
            os.makedirs(CACHE_DIR)
            
        font_paths = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", # Debian/Ubuntu
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",          # Other Linux
            "/System/Library/Fonts/Supplemental/Arial.ttf",    # macOS
            "/System/Library/Fonts/Helvetica.ttc",             # macOS fallback
        ]
        
        self.font = None
        for path in font_paths:
            try:
                self.font = ImageFont.truetype(path, 24)
                self.font_small = ImageFont.truetype(path, 18)
                print(f"Loaded font from {path}")
                break
            except Exception:
                continue
                
        if self.font is None:
            print("WARNING: No TTF font found. Using default tiny font (no Cyrillic).")
            self.font = ImageFont.load_default()
            self.font_small = ImageFont.load_default()

        self.thread = threading.Thread(target=self._update_loop, daemon=True)
        self.thread.start()

    def _fetch_thumbnail(self, video_id):
        cache_path = os.path.join(CACHE_DIR, f"{video_id}.jpg")
        if os.path.exists(cache_path):
            try:
                return Image.open(cache_path).copy()
            except:
                pass
        
        url = f"https://i.ytimg.com/vi/{video_id}/mqdefault.jpg"
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=10) as resp:
                data = resp.read()
                with open(cache_path, 'wb') as f:
                    f.write(data)
                return Image.open(io.BytesIO(data))
        except Exception as e:
            print(f"Failed to fetch thumbnail for {video_id}: {e}")
            img = Image.new('RGB', (320, 180), color=(50,50,50))
            return img

    def _generate_page(self, page_index):
        img = Image.new('RGB', (480, 800), color=(20, 20, 20))
        draw = ImageDraw.Draw(img)
        
        if not self.videos:
            draw.text((100, 380), "Loading feeds...", font=self.font, fill=(255, 255, 255))
        else:
            start_idx = page_index * 2
            for i in range(2):
                idx = start_idx + i
                if idx >= len(self.videos):
                    break
                    
                vid = self.videos[idx]
                y_offset = i * 400
                
                # Thumbnail (320x180 scaled to 480x270)
                thumb = self._fetch_thumbnail(vid['id'])
                thumb = thumb.resize((480, 270), Image.Resampling.LANCZOS)
                img.paste(thumb, (0, y_offset))
                
                # Text
                title = vid['title']
                channel = vid['channel']
                
                # Simple text wrapping for title
                words = title.split()
                lines = []
                current_line = []
                for word in words:
                    current_line.append(word)
                    bbox = draw.textbbox((0, 0), " ".join(current_line), font=self.font)
                    if bbox[2] > 460:
                        current_line.pop()
                        lines.append(" ".join(current_line))
                        current_line = [word]
                if current_line:
                    lines.append(" ".join(current_line))
                
                text_y = y_offset + 280
                for line in lines[:2]: # Max 2 lines for title
                    draw.text((10, text_y), line, font=self.font, fill=(255, 255, 255))
                    text_y += 30
                    
                draw.text((10, text_y + 10), channel, font=self.font_small, fill=(170, 170, 170))
                
                # Draw separator
                if i == 0:
                    draw.line([(0, 399), (480, 399)], fill=(50, 50, 50), width=2)
                
        # Convert to RGB565
        r, g, b = img.split()
        
        r_bytes = r.tobytes()
        g_bytes = g.tobytes()
        b_bytes = b.tobytes()
        
        out = bytearray(480 * 800 * 2)
        for i in range(480 * 800):
            val = ((r_bytes[i] >> 3) << 11) | ((g_bytes[i] >> 2) << 5) | (b_bytes[i] >> 3)
            # Pack as little-endian uint16
            out[i*2] = val & 0xFF
            out[i*2 + 1] = (val >> 8) & 0xFF
            
        return bytes(out)

    def _update_loop(self):
        while self.running:
            print("Updating feed from yt-dlp...")
            try:
                channels = []
                try:
                    with open('proxy.conf', 'r') as f:
                        for line in f:
                            line = line.strip()
                            if line and not line.startswith('#'):
                                parts = line.split(',', 1)
                                if len(parts) == 2:
                                    channels.append((parts[0], parts[1]))
                                else:
                                    channels.append((parts[0], "Unknown Channel"))
                except FileNotFoundError:
                    print("proxy.conf not found. Please run extract_channels.py first.")
                    time.sleep(60)
                    continue
                
                all_vids = []
                import subprocess
                
                for cid, cname in channels:
                    if not self.running: break
                    if not cid: continue
                    
                    cmd = [
                        "yt-dlp",
                        "--print", "%(id)s|%(title)s|%(upload_date)s|%(duration)s",
                        f"https://www.youtube.com/channel/{cid}/videos",
                        "--playlist-end", "3",
                        "--match-filter", "duration > 60"
                    ]
                    try:
                        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
                        lines = proc.stdout.strip().split('\n')
                        for line in lines:
                            if not line or line.startswith('WARNING:') or line.startswith('Deprecated'): continue
                            parts = line.split('|', 3)
                            if len(parts) >= 3:
                                vid = parts[0]
                                title = parts[1]
                                pub = parts[2]
                                # duration = parts[3] if len(parts) > 3 else '0'
                                if pub == 'NA': pub = '20000101'
                                all_vids.append({
                                    'id': vid,
                                    'title': title,
                                    'channel': cname,
                                    'published': pub
                                })
                    except Exception as e:
                        print(f"Feed error for {cname}: {e}")
                    
                    # Update incrementally so UI doesn't stay empty for minutes
                    if all_vids:
                        temp_vids = all_vids[:]
                        temp_vids.sort(key=lambda x: x['published'], reverse=True)
                        with self.lock:
                            self.videos = temp_vids[:50]
                            self.pages.clear()
                            
                print(f"Feed updated completely! Found {len(all_vids)} videos, kept top 50.")
                
            except Exception as e:
                print(f"Feed update failed: {e}")
                
            # Sleep 6 hours, check every second to allow fast exit
            for _ in range(21600):
                if not self.running: break
                time.sleep(1)

    def get_list(self):
        with self.lock:
            return "\n".join([v['id'] for v in self.videos])
            
    def get_page(self, page_index):
        with self.lock:
            if page_index in self.pages:
                return self.pages[page_index]
                
        # Generate on demand
        data = self._generate_page(page_index)
        with self.lock:
            self.pages[page_index] = data
            return data

if __name__ == '__main__':
    fm = FeedManager()
    time.sleep(5)
    print("List:")
    print(fm.get_list())
