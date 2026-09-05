import re

with open("src/platform.c", "r") as f:
    content = f.read()

old_init = """    if (platform->fb_bpp != 16 && platform->fb_bpp != 32) {"""
new_init = """    /* Disable console blanking */
    int tty = open("/dev/tty0", O_RDWR);
    if (tty >= 0) {
        write(tty, "\\033[9;0]", 6);
        close(tty);
    }
    ioctl(platform->fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);

    if (platform->fb_bpp != 16 && platform->fb_bpp != 32) {"""
if "Disable console blanking" not in content:
    content = content.replace(old_init, new_init)

old_update_video = """void gb_platform_update_video(gb_platform_t *platform, gb_ppu_t *ppu) {
    if (!platform->fb_mem) return;"""
new_update_video = """void gb_platform_update_video(gb_platform_t *platform, gb_ppu_t *ppu) {
    if (!platform->fb_mem) return;

    /* Periodically unblank the screen in case a system daemon tries to turn it off */
    static int frame_count = 0;
    if (++frame_count >= 600) {
        frame_count = 0;
        ioctl(platform->fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    }"""
if "Periodically unblank" not in content:
    content = content.replace(old_update_video, new_update_video)

with open("src/platform.c", "w") as f:
    f.write(content)
