#include "i_video.h"
#include "v_video.h"
#include "doomdef.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>

extern int debug_touch_active;
extern int debug_touch_x;
extern int debug_touch_y;
extern int debug_last_code;
extern int debug_last_val;
extern int debug_dev_count;

static int fbfd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static long int screensize = 0;
static char *fbp = NULL;

int hiby_fb_width = 480;
int hiby_fb_height = 800;
int hiby_fb_bpp = 16;

#define GAME_VIEW_X 0
#define GAME_VIEW_Y 40
#define GAME_VIEW_W 480
#define GAME_VIEW_H 300

__attribute__((packed))
struct Color {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
};

union ColorInt {
    struct Color col;
    uint32_t raw;
};

static union ColorInt colors[256];

static const uint8_t font8x8[128][8] = {
    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x00, 0x00},
    ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, 0x00, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00, 0x00},
    ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31, 0x00, 0x00, 0x00},
    ['4'] = {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00, 0x00, 0x00},
    ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00, 0x00},
    ['6'] = {0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00, 0x00, 0x00},
    ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03, 0x00, 0x00, 0x00},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00},
    ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1E, 0x00, 0x00, 0x00},
    ['A'] = {0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00, 0x00, 0x00},
    ['B'] = {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00, 0x00},
    ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00, 0x00, 0x00},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01, 0x00, 0x00, 0x00},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00, 0x00, 0x00},
    ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x00, 0x00},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00, 0x00, 0x00},
    ['J'] = {0x20, 0x40, 0x41, 0x3F, 0x01, 0x00, 0x00, 0x00},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00},
    ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00},
    ['M'] = {0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00, 0x00, 0x00},
    ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00, 0x00, 0x00},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00, 0x00, 0x00},
    ['P'] = {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00},
    ['Q'] = {0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00, 0x00, 0x00},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00, 0x00, 0x00},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31, 0x00, 0x00, 0x00},
    ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01, 0x00, 0x00, 0x00},
    ['U'] = {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00, 0x00, 0x00},
    ['V'] = {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00, 0x00, 0x00},
    ['W'] = {0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00, 0x00, 0x00},
    ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63, 0x00, 0x00, 0x00},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07, 0x00, 0x00, 0x00},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x00, 0x00},
    [':'] = {0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['='] = {0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00},
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['('] = {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00},
    [')'] = {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00},
};

void I_InitGraphics(void) {
    if (fbfd >= 0) return; // Already initialized

    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) {
        perror("Error: cannot open /dev/fb0");
        exit(1);
    }

    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) < 0 ||
        ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("Error reading framebuffer info");
        close(fbfd);
        exit(2);
    }

    hiby_fb_width = vinfo.xres;
    hiby_fb_height = vinfo.yres;
    hiby_fb_bpp = vinfo.bits_per_pixel;

    screensize = finfo.smem_len;
    if (screensize == 0) {
        screensize = hiby_fb_width * hiby_fb_height * (hiby_fb_bpp / 8);
    }

    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == MAP_FAILED) {
        perror("Error: mmap framebuffer failed");
        close(fbfd);
        exit(4);
    }

    memset(fbp, 0, screensize);
    printf("HiBy R1 Framebuffer initialized: %dx%d @ %dbpp\n", hiby_fb_width, hiby_fb_height, hiby_fb_bpp);
}

void I_ShutdownGraphics(void) {
    if (fbp && fbp != MAP_FAILED) munmap(fbp, screensize);
    if (fbfd >= 0) close(fbfd);
}

void I_StartFrame(void) {}

void I_SetPalette(byte* palette) {
    byte c;
    for (int i = 0; i < 256; i++) {
        c = gammatable[usegamma][*palette++];
        colors[i].col.r = c;
        c = gammatable[usegamma][*palette++];
        colors[i].col.g = c;
        c = gammatable[usegamma][*palette++];
        colors[i].col.b = c;
        colors[i].col.a = 255;
    }
}

void I_UpdateNoBlit(void) {}

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void draw_rect(int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
    if (rx < 0 || ry < 0 || rx + rw > hiby_fb_width || ry + rh > hiby_fb_height) return;
    uint16_t c16 = rgb565(r, g, b);
    uint32_t c32 = (r << 16) | (g << 8) | b;

    for (int y = ry; y < ry + rh; y++) {
        char *line = fbp + y * finfo.line_length;
        for (int x = rx; x < rx + rw; x++) {
            if (hiby_fb_bpp == 16) {
                ((uint16_t*)line)[x] = c16;
            } else if (hiby_fb_bpp == 32) {
                ((uint32_t*)line)[x] = c32;
            }
        }
    }
}

void draw_char8x8(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t uc = (uint8_t)c;
    if (uc >= 128) return;
    uint16_t c16 = rgb565(r, g, b);
    uint32_t c32 = (r << 16) | (g << 8) | b;

    for (int col = 0; col < 6; col++) {
        uint8_t bits = font8x8[uc][col];
        for (int row = 0; row < 8; row++) {
            if (bits & (1 << row)) {
                int px = x + col * 2;
                int py = y + row * 2;
                for (int dy = 0; dy < 2; dy++) {
                    int fy = py + dy;
                    if (fy < 0 || fy >= hiby_fb_height) continue;
                    char *line = fbp + fy * finfo.line_length;
                    for (int dx = 0; dx < 2; dx++) {
                        int fx = px + dx;
                        if (fx < 0 || fx >= hiby_fb_width) continue;
                        if (hiby_fb_bpp == 16) ((uint16_t*)line)[fx] = c16;
                        else if (hiby_fb_bpp == 32) ((uint32_t*)line)[fx] = c32;
                    }
                }
            }
        }
    }
}

void draw_str(int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b) {
    int cx = x;
    while (*str) {
        draw_char8x8(cx, y, *str, r, g, b);
        cx += 13;
        str++;
    }
}

static void draw_touch_hud(void) {
    // D-PAD buttons (Left side)
    draw_rect(80, 440, 80, 70, 70, 70, 70);   // UP
    draw_rect(80, 670, 80, 70, 70, 70, 70);   // DOWN
    draw_rect(10, 550, 70, 80, 70, 70, 70);   // LEFT
    draw_rect(160, 550, 70, 80, 70, 70, 70);  // RIGHT

    // ACTION buttons (Right side)
    draw_rect(260, 570, 90, 90, 180, 40, 40);  // FIRE (Red)
    draw_rect(370, 480, 90, 80, 40, 180, 40);  // USE (Green)
    draw_rect(370, 580, 90, 80, 40, 100, 200); // RUN (Blue)
    draw_rect(260, 480, 90, 70, 180, 180, 40); // WEAPON (Yellow)

    // Y/N Confirm buttons
    draw_rect(260, 680, 90, 60, 120, 160, 120); // Y (Light Greenish)
    draw_rect(370, 680, 90, 60, 160, 120, 120); // N (Light Reddish)
    draw_str(295, 705, "Y", 255, 255, 255);
    draw_str(405, 705, "N", 255, 255, 255);

    // TOP MENU BAR
    draw_rect(10, 370, 100, 50, 100, 100, 100);  // MENU (ESC)
    draw_rect(130, 370, 100, 50, 100, 100, 100); // MAP (TAB)
    draw_rect(250, 370, 100, 50, 100, 100, 100); // ENTER
    draw_rect(370, 370, 100, 50, 100, 100, 100); // YES
}

static void draw_live_debug_bar(void) {
    // Background bar at the bottom (y: 750..798)
    draw_rect(0, 748, 480, 50, 10, 10, 25);

    char line1[64];
    char line2[64];

    snprintf(line1, sizeof(line1), "TOUCH ACT=%d X=%-4d Y=%-4d", debug_touch_active, debug_touch_x, debug_touch_y);
    snprintf(line2, sizeof(line2), "KEY CODE=%-4d VAL=%-2d DEVS=%d", debug_last_code, debug_last_val, debug_dev_count);

    draw_str(10, 752, line1, 255, 255, 0);   // Yellow debug text
    draw_str(10, 774, line2, 0, 255, 255);   // Cyan debug text
}

void I_FinishUpdate(void) {
    byte *src = screens[0];

    for (int gy = 0; gy < SCREENHEIGHT; gy++) {
        int dst_y1 = GAME_VIEW_Y + (gy * 3) / 2;
        int dst_y2 = GAME_VIEW_Y + ((gy + 1) * 3) / 2;

        for (int gx = 0; gx < SCREENWIDTH; gx++) {
            uint8_t pal_idx = src[gy * SCREENWIDTH + gx];
            struct Color col = colors[pal_idx].col;

            uint16_t c16 = rgb565(col.r, col.g, col.b);
            uint32_t c32 = (col.r << 16) | (col.g << 8) | col.b;

            int dst_x1 = GAME_VIEW_X + (gx * 3) / 2;
            int dst_x2 = GAME_VIEW_X + ((gx + 1) * 3) / 2;

            for (int dy = dst_y1; dy < dst_y2; dy++) {
                if (dy < 0 || dy >= hiby_fb_height) continue;
                char *line = fbp + dy * finfo.line_length;
                for (int dx = dst_x1; dx < dst_x2; dx++) {
                    if (dx < 0 || dx >= hiby_fb_width) continue;
                    if (hiby_fb_bpp == 16) ((uint16_t*)line)[dx] = c16;
                    else if (hiby_fb_bpp == 32) ((uint32_t*)line)[dx] = c32;
                }
            }
        }
    }

    // Render touch HUD controls
    draw_touch_hud();

    // Render live real-time debug info status bar at the bottom
    // draw_live_debug_bar();
}

void I_ReadScreen(byte* scr) {
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}
