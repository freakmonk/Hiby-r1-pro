import re

with open("src/apu.c", "r") as f:
    content = f.read()

old_sample = """    int mono = (left + right) / 2;
    int sample = (mono * 32767) / (4 * 15 * 8);
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;"""

new_sample = """    int mono = (left + right) / 2;
    /* Map 0..480 to -32768..32767 to remove DC offset and utilize full dynamic range */
    int sample = (mono * 65535) / (4 * 15 * 8) - 32768;
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;"""

if "Map 0..480" not in content:
    content = content.replace(old_sample, new_sample)

with open("src/apu.c", "w") as f:
    f.write(content)

with open("src/platform.c", "r") as f:
    plat = f.read()

plat = plat.replace("static int current_volume = 205; /* 20% default (205/1024) */", "static int current_volume = 512; /* 50% default (512/1024) */")
plat = plat.replace("static int current_volume = 205;", "static int current_volume = 512;")

with open("src/platform.c", "w") as f:
    f.write(plat)
