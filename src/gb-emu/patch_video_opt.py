import re

with open("src/platform.c", "r") as f:
    content = f.read()

old_func = """    const int scale = platform->scale;
    u8 *base = (u8 *)platform->fb_mem;

    for (int y = 0; y < GB_HEIGHT; y++) {
        for (int sy = 0; sy < scale; sy++) {
            int fb_y = platform->offset_y + y * scale + sy;
            if (fb_y < 0 || fb_y >= platform->fb_height) continue;

            u8 *line = base + (size_t)fb_y * platform->fb_stride;

            for (int x = 0; x < GB_WIDTH; x++) {
                u32 color = ppu->framebuffer[y * GB_WIDTH + x];
                int fb_x = platform->offset_x + x * scale;

                for (int sx = 0; sx < scale; sx++, fb_x++) {
                    if (fb_x < 0 || fb_x >= platform->fb_width) continue;
                    if (platform->fb_bpp == 32) {
                        *((u32 *)(line + (size_t)fb_x * 4)) = color;
                    } else {
                        *((u16 *)(line + (size_t)fb_x * 2)) = argb_to_rgb565(color);
                    }
                }
            }
        }
    }"""

new_func = """    const int scale = platform->scale;
    u8 *base = (u8 *)platform->fb_mem;

    /* Optimized rendering: bounds are guaranteed by init math, and branch is hoisted */
    if (platform->fb_bpp == 32) {
        for (int y = 0; y < GB_HEIGHT; y++) {
            u8 *line0 = base + (size_t)(platform->offset_y + y * scale) * platform->fb_stride;
            /* Build one scaled line */
            u32 *out = (u32 *)line0 + platform->offset_x;
            for (int x = 0; x < GB_WIDTH; x++) {
                u32 color = ppu->framebuffer[y * GB_WIDTH + x];
                for (int sx = 0; sx < scale; sx++) {
                    *out++ = color;
                }
            }
            /* Copy to remaining scaled rows */
            for (int sy = 1; sy < scale; sy++) {
                u8 *lineN = base + (size_t)(platform->offset_y + y * scale + sy) * platform->fb_stride;
                memcpy(lineN + platform->offset_x * 4, line0 + platform->offset_x * 4, GB_WIDTH * scale * 4);
            }
        }
    } else {
        for (int y = 0; y < GB_HEIGHT; y++) {
            u8 *line0 = base + (size_t)(platform->offset_y + y * scale) * platform->fb_stride;
            /* Build one scaled line */
            u16 *out = (u16 *)line0 + platform->offset_x;
            for (int x = 0; x < GB_WIDTH; x++) {
                u16 color = argb_to_rgb565(ppu->framebuffer[y * GB_WIDTH + x]);
                for (int sx = 0; sx < scale; sx++) {
                    *out++ = color;
                }
            }
            /* Copy to remaining scaled rows */
            for (int sy = 1; sy < scale; sy++) {
                u8 *lineN = base + (size_t)(platform->offset_y + y * scale + sy) * platform->fb_stride;
                memcpy(lineN + platform->offset_x * 2, line0 + platform->offset_x * 2, GB_WIDTH * scale * 2);
            }
        }
    }"""

if "Optimized rendering" not in content:
    content = content.replace(old_func, new_func)

with open("src/platform.c", "w") as f:
    f.write(content)
