import re

with open("src/apu.c", "r") as f:
    apu = f.read()
apu = apu.replace("""    int mono = (left + right) / 2;
    /* Map 0..480 to -32768..32767 to remove DC offset and utilize full dynamic range */
    int sample = (mono * 65535) / (4 * 15 * 8) - 32768;
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;""", """    int mono = (left + right) / 2;
    int sample = (mono * 32767) / (4 * 15 * 8);
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;""")
with open("src/apu.c", "w") as f:
    f.write(apu)

with open("include/platform.h", "r") as f:
    hdr = f.read()
hdr = hdr.replace("bool key_held[16];\n    bool touch_held[16];", "bool key_held[8];\n    bool touch_held[8];")
with open("include/platform.h", "w") as f:
    f.write(hdr)

with open("src/platform.c", "r") as f:
    plat = f.read()

plat = plat.replace("static size_t fb_map_size;\nstatic int current_volume = 512; /* 50% default (512/1024) */", "static size_t fb_map_size;")
plat = plat.replace("static size_t fb_map_size;\nstatic int current_volume = 205; /* 20% default (205/1024) */", "static size_t fb_map_size;")
plat = plat.replace("static size_t fb_map_size;\nstatic int current_volume = 512;", "static size_t fb_map_size;")

plat = plat.replace("""enum {
    TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT,
    TB_A, TB_B, TB_START, TB_SELECT,
    TB_VOL_MINUS, TB_VOL_PLUS,
    TB_COUNT
};""", """enum {
    TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT,
    TB_A, TB_B, TB_START, TB_SELECT, TB_COUNT
};""")

plat = plat.replace("""    /* Start and Select across the bottom. */
    { 0.30f, 0.93f, 0.50f, 1.00f, TB_SELECT },
    { 0.52f, 0.93f, 0.72f, 1.00f, TB_START  },
    /* Volume buttons at the top, between D-pad and A/B */
    { 0.38f, 0.62f, 0.45f, 0.72f, TB_VOL_MINUS },
    { 0.47f, 0.62f, 0.54f, 0.72f, TB_VOL_PLUS  },
};""", """    /* Start and Select across the bottom. */
    { 0.30f, 0.93f, 0.50f, 1.00f, TB_SELECT },
    { 0.52f, 0.93f, 0.72f, 1.00f, TB_START  },
};""")

plat = plat.replace("""static const char *touch_labels[TB_COUNT] = {
    [TB_UP] = "^", [TB_DOWN] = "v", [TB_LEFT] = "<", [TB_RIGHT] = ">",
    [TB_A] = "A", [TB_B] = "B", [TB_START] = "START", [TB_SELECT] = "SELECT",
    [TB_VOL_MINUS] = "-", [TB_VOL_PLUS] = "+"
};""", """static const char *touch_labels[TB_COUNT] = {
    [TB_UP] = "^", [TB_DOWN] = "v", [TB_LEFT] = "<", [TB_RIGHT] = ">",
    [TB_A] = "A", [TB_B] = "B", [TB_START] = "START", [TB_SELECT] = "SELECT"
};""")

plat = plat.replace("""    if (touch_changed) update_touch_held(platform);
    merge_buttons(platform);

    static int vol_plus_held = 0;
    if (platform->touch_held[TB_VOL_PLUS]) {
        if (vol_plus_held == 0 || (vol_plus_held > 20 && vol_plus_held % 5 == 0)) {
            current_volume += 51;
            if (current_volume > 1024) current_volume = 1024;
        }
        vol_plus_held++;
    } else {
        vol_plus_held = 0;
    }

    static int vol_minus_held = 0;
    if (platform->touch_held[TB_VOL_MINUS]) {
        if (vol_minus_held == 0 || (vol_minus_held > 20 && vol_minus_held % 5 == 0)) {
            current_volume -= 51;
            if (current_volume < 0) current_volume = 0;
        }
        vol_minus_held++;
    } else {
        vol_minus_held = 0;
    }

    return quit;""", """    if (touch_changed) update_touch_held(platform);
    merge_buttons(platform);

    return quit;""")

plat = plat.replace("""    void *pcm = platform->audio_handle;
    int count;
    while ((count = gb_apu_read_samples(apu, platform->audio_buffer,
                                       platform->audio_buf_size)) > 0) {
        /* Apply software volume scaling */
        for (int i = 0; i < count; i++) {
            platform->audio_buffer[i] = (s16)((platform->audio_buffer[i] * current_volume) >> 10);
        }

        s16 *cursor = platform->audio_buffer;
        int remaining = count;""", """    void *pcm = platform->audio_handle;
    int count;
    while ((count = gb_apu_read_samples(apu, platform->audio_buffer,
                                       platform->audio_buf_size)) > 0) {
        s16 *cursor = platform->audio_buffer;
        int remaining = count;""")

with open("src/platform.c", "w") as f:
    f.write(plat)
