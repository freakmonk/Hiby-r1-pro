import re

with open("src/platform.c", "r") as f:
    content = f.read()

# 1. Revert KEY_VOLUMEUP / KEY_VOLUMEDOWN back to TB_UP / TB_DOWN
old_keys = """            switch (ev.code) {
                case KEY_UP:            platform->key_held[TB_UP] = pressed; break;
                case KEY_VOLUMEUP:
                    if (pressed && ev.value == 1) { /* Only on fresh press, not auto-repeat */
                        current_volume += 51;
                        if (current_volume > 1024) current_volume = 1024;
                    }
                    break;
                case KEY_DOWN:          platform->key_held[TB_DOWN] = pressed; break;
                case KEY_VOLUMEDOWN:
                    if (pressed && ev.value == 1) {
                        current_volume -= 51;
                        if (current_volume < 0) current_volume = 0;
                    }
                    break;"""

new_keys = """            switch (ev.code) {
                case KEY_UP:
                case KEY_VOLUMEUP:      platform->key_held[TB_UP] = pressed; break;
                case KEY_DOWN:
                case KEY_VOLUMEDOWN:    platform->key_held[TB_DOWN] = pressed; break;"""

content = content.replace(old_keys, new_keys)

# 2. Add TB_VOL_MINUS and TB_VOL_PLUS to enum
old_enum = """enum {
    TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT,
    TB_A, TB_B, TB_START, TB_SELECT, TB_COUNT
};"""
new_enum = """enum {
    TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT,
    TB_A, TB_B, TB_START, TB_SELECT,
    TB_VOL_MINUS, TB_VOL_PLUS,
    TB_COUNT
};"""
content = content.replace(old_enum, new_enum)

# 3. Add to touch_zones array
old_zones = """    /* Start and Select across the bottom. */
    { 0.30f, 0.93f, 0.50f, 1.00f, TB_SELECT },
    { 0.52f, 0.93f, 0.72f, 1.00f, TB_START  },
};"""
new_zones = """    /* Start and Select across the bottom. */
    { 0.30f, 0.93f, 0.50f, 1.00f, TB_SELECT },
    { 0.52f, 0.93f, 0.72f, 1.00f, TB_START  },
    /* Volume buttons at the top, between D-pad and A/B */
    { 0.38f, 0.62f, 0.45f, 0.72f, TB_VOL_MINUS },
    { 0.47f, 0.62f, 0.54f, 0.72f, TB_VOL_PLUS  },
};"""
content = content.replace(old_zones, new_zones)

# 4. Add to touch_labels array
old_labels = """static const char *touch_labels[TB_COUNT] = {
    [TB_UP] = "^", [TB_DOWN] = "v", [TB_LEFT] = "<", [TB_RIGHT] = ">",
    [TB_A] = "A", [TB_B] = "B", [TB_START] = "START", [TB_SELECT] = "SELECT"
};"""
new_labels = """static const char *touch_labels[TB_COUNT] = {
    [TB_UP] = "^", [TB_DOWN] = "v", [TB_LEFT] = "<", [TB_RIGHT] = ">",
    [TB_A] = "A", [TB_B] = "B", [TB_START] = "START", [TB_SELECT] = "SELECT",
    [TB_VOL_MINUS] = "-", [TB_VOL_PLUS] = "+"
};"""
content = content.replace(old_labels, new_labels)

# 5. Handle volume edge detection at the end of gb_platform_poll_input
old_merge = """    if (touch_changed) update_touch_held(platform);
    merge_buttons(platform);

    return quit;"""

new_merge = """    if (touch_changed) update_touch_held(platform);
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

    return quit;"""
content = content.replace(old_merge, new_merge)

with open("src/platform.c", "w") as f:
    f.write(content)
