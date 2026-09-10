import subprocess
cmd = [
    "yt-dlp",
    "--print", "%(id)s|%(title)s|%(upload_date)s",
    "https://www.youtube.com/channel/UC--fuTAI8LWUf7WuLvwydLA",
    "--playlist-end", "2"
]
proc = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
print("STDOUT:")
print(repr(proc.stdout))
print("STDERR:")
print(repr(proc.stderr))
