import re

# 1. Update platform.h
with open("include/platform.h", "r") as f:
    hdr = f.read()

hdr = hdr.replace("bool button_select;", "bool button_select;\n    bool running_behind;")
with open("include/platform.h", "w") as f:
    f.write(hdr)

# 2. Update platform.c
with open("src/platform.c", "r") as f:
    plat = f.read()

old_wait = """    if (delta_ns <= 0) {
        /* Running behind: resync so the deficit does not accumulate. */
        if (delta_ns < -FRAME_NS * 4) next_frame = now;
        return;
    }"""
new_wait = """    if (delta_ns <= 0) {
        /* Running behind: resync so the deficit does not accumulate. */
        if (delta_ns < -FRAME_NS * 2) next_frame = now;
        platform->running_behind = true;
        return;
    }
    platform->running_behind = false;"""

if "platform->running_behind = true;" not in plat:
    plat = plat.replace(old_wait, new_wait)
with open("src/platform.c", "w") as f:
    f.write(plat)

# 3. Update main.c
with open("src/main.c", "r") as f:
    main_c = f.read()

old_loop = """    while (gb.state == GB_STATE_RUNNING && running) {
        gb_run_frame(&gb);
        gb_platform_update_video(platform, &gb.ppu);
        gb_platform_update_audio(platform, &gb.apu);
        if (gb_platform_poll_input(platform)) {
            gb.state = GB_STATE_STOPPED;
        }
        gb_platform_wait_frame(platform);
    }"""

new_loop = """    int skip_count = 0;
    while (gb.state == GB_STATE_RUNNING && running) {
        gb_run_frame(&gb);
        
        if (!platform->running_behind || skip_count >= 1) {
            gb_platform_update_video(platform, &gb.ppu);
            skip_count = 0;
        } else {
            skip_count++;
        }
        
        gb_platform_update_audio(platform, &gb.apu);
        if (gb_platform_poll_input(platform)) {
            gb.state = GB_STATE_STOPPED;
        }
        gb_platform_wait_frame(platform);
    }"""

if "skip_count = 0;" not in main_c:
    main_c = main_c.replace(old_loop, new_loop)
with open("src/main.c", "w") as f:
    f.write(main_c)

