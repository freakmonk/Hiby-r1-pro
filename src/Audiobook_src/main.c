/* main.c — Standalone entry point for the Audiobook app on Hiby R1.
 *
 * Replaces hook.c for standalone operation. Instead of LD_PRELOAD injection
 * into hiby_player, this opens /dev/fb0 directly, mmaps the framebuffer,
 * initializes input and font, then calls the audiobook UI event loop.
 *
 * Build: mipsel-linux-gnu-gcc or zig cc -target mipsel-linux-gnueabihf.2.22
 *        -DSTANDALONE ... -o Audiobook
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <linux/fb.h>

#include "ui.h"
#include "font.h"
#include "storage_guard.h"

/* ---- Known fb geometry (fixed for R1) ----------------------------------- */
#define FB_W    480
#define FB_H    800
#define FB_BPP  16
#define FB_STRIDE 960
#define FB_BUF_SIZE (FB_STRIDE * FB_H)

/* ---- Persistent log fd -------------------------------------------------- */
/* ui.c calls get_log_fd() for logging. In hook.c this was a shared fd to
 * /tmp/.audiobook_hook.log. For standalone, we log to the SD card. */
static int g_log_fd = -1;
#define LOG_PATH "/data/mnt/sd_0/Audiobook.log"

__attribute__((visibility("default")))
int get_log_fd(void) {
    return -1;
}

/* Also provide the raw log writer that ui.c may reference. */
__attribute__((visibility("default")))
void ui_log_raw(const char *buf, int len) {
    int fd = get_log_fd();
    if (fd >= 0) write(fd, buf, len);
}

/* ---- ui_notify_fb_blank stub -------------------------------------------- */
/* In hook.c this is called from the ioctl interceptor when hiby_player
 * requests FBIOBLANK. In standalone mode there is no ioctl hook and the
 * function is never called, but ui.c declares it extern. */

/* ---- Main --------------------------------------------------------------- */

uint16_t *g_fb_hw = NULL;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Ignore SIGHUP so terminal detach doesn't kill us */
    signal(SIGHUP, SIG_IGN);

    int fd = get_log_fd();
    if (fd >= 0) {
        const char *msg = "=== Audiobook standalone started ===\n";
        write(fd, msg, strlen(msg));
    }

    /* Open framebuffer */
    int fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        fprintf(stderr, "[main] cannot open /dev/fb0\n");
        return 1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);

    long screensize = finfo.smem_len;
    if (screensize < FB_BUF_SIZE * 2)
        screensize = FB_BUF_SIZE * 2;  /* need space for double buffer */

    uint16_t *fb_hw = (uint16_t *)mmap(0, screensize,
                                     PROT_READ | PROT_WRITE, MAP_SHARED,
                                     fb_fd, 0);
    g_fb_hw = fb_hw;
    uint16_t *fb = (uint16_t *)malloc(screensize);
    if (!fb) {
        fprintf(stderr, "[main] malloc backbuffer failed\n");
        return 1;
    }
    if (fb == MAP_FAILED) {
        fprintf(stderr, "[main] mmap failed\n");
        close(fb_fd);
        return 1;
    }

    /* Load TrueType font */
    if (font_init())
        fprintf(stderr, "[main] font: msyh.ttf loaded\n");
    else
        fprintf(stderr, "[main] font: using bitmap fallback\n");

    /* Acquire SD storage guard (prevents MMC auto-suspend) */
    storage_guard_acquire();

    /* Run the audiobook UI. Returns when user exits. */
    ui_run(fb, fb_fd);

    /* Cleanup */
    storage_guard_release();
    free(fb);
    munmap(g_fb_hw, screensize);
    close(fb_fd);

    if (g_log_fd >= 0) {
        const char *msg = "=== Audiobook standalone exited (powering off) ===\n";
        write(fd, msg, strlen(msg));
        close(g_log_fd);
    }

    /* Shutdown the hardware device completely */
    system("poweroff");

    return 0;
}
