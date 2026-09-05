#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdint.h>
#include <dirent.h>

/* --- Framebuffer Globals --- */
static int fbfd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static long int screensize = 0;
static char *fbp = NULL;
static char *backbuffer = NULL;

int fb_width = 480;
int fb_height = 800;
int fb_bpp = 16;

/* --- Input Globals --- */
#define MAX_INPUT_DEVS 20
static int input_fds[MAX_INPUT_DEVS];
static int input_count = 0;

static int touch_x = -1;
static int touch_y = -1;
static bool touch_active = false;
static bool touch_was_active = false;

/* --- UI Definitions --- */
typedef enum {
    BTN_NUM,
    BTN_FUNC,
    BTN_OP
} BtnType;

typedef struct {
    int cx, cy, r;
    char label[4];
    BtnType type;
    bool pressed;
    char id;
} Button;

#define BTN_COUNT 20
Button buttons[BTN_COUNT];

/* --- Calculator Logic --- */
char display_text[256] = "0";
double current_val = 0;
double stored_val = 0;
char current_op = 0;
bool is_new_val = true;

/* --- Font (8x8 scaled) --- */
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
    ['+'] = {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00},
    ['/'] = {0x00, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00, 0x00},
    ['*'] = {0x22, 0x14, 0x08, 0x14, 0x22, 0x00, 0x00, 0x00}, // Multiplication sign (×)
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['.'] = {0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00},
    [','] = {0x00, 0x40, 0x40, 0x20, 0x00, 0x00, 0x00, 0x00},
    ['<'] = {0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00}, // Simple < icon
    ['%'] = {0x23, 0x13, 0x08, 0x04, 0x32, 0x32, 0x00, 0x00},
};

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void draw_rect(int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
    if (rx < 0 || ry < 0 || rx + rw > fb_width || ry + rh > fb_height) return;
    if (!backbuffer) return;
    uint16_t c16 = rgb565(r, g, b);
    uint32_t c32 = (r << 16) | (g << 8) | b;

    for (int y = ry; y < ry + rh; y++) {
        char *line = backbuffer + y * finfo.line_length;
        for (int x = rx; x < rx + rw; x++) {
            if (fb_bpp == 16) ((uint16_t*)line)[x] = c16;
            else if (fb_bpp == 32) ((uint32_t*)line)[x] = c32;
        }
    }
}

static void draw_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b) {
    if (!backbuffer) return;
    uint16_t c16 = rgb565(r, g, b);
    uint32_t c32 = (r << 16) | (g << 8) | b;
    
    int r2 = radius * radius;
    for (int y = cy - radius; y <= cy + radius; y++) {
        if (y < 0 || y >= fb_height) continue;
        char *line = backbuffer + y * finfo.line_length;
        int dy2 = (y - cy) * (y - cy);
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x < 0 || x >= fb_width) continue;
            int dx2 = (x - cx) * (x - cx);
            if (dx2 + dy2 <= r2) {
                if (fb_bpp == 16) ((uint16_t*)line)[x] = c16;
                else if (fb_bpp == 32) ((uint32_t*)line)[x] = c32;
            }
        }
    }
}

static void draw_char_scaled(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, int scale) {
    uint8_t uc = (uint8_t)c;
    if (uc >= 128) return;
    if (!backbuffer) return;
    uint16_t c16 = rgb565(r, g, b);
    uint32_t c32 = (r << 16) | (g << 8) | b;

    for (int col = 0; col < 6; col++) {
        uint8_t bits = font8x8[uc][col];
        for (int row = 0; row < 8; row++) {
            if (bits & (1 << row)) {
                int px = x + col * scale;
                int py = y + row * scale;
                for (int dy = 0; dy < scale; dy++) {
                    int fy = py + dy;
                    if (fy < 0 || fy >= fb_height) continue;
                    char *line = backbuffer + fy * finfo.line_length;
                    for (int dx = 0; dx < scale; dx++) {
                        int fx = px + dx;
                        if (fx < 0 || fx >= fb_width) continue;
                        if (fb_bpp == 16) ((uint16_t*)line)[fx] = c16;
                        else if (fb_bpp == 32) ((uint32_t*)line)[fx] = c32;
                    }
                }
            }
        }
    }
}

static void draw_str_scaled(int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b, int scale) {
    int cx = x;
    while (*str) {
        draw_char_scaled(cx, y, *str, r, g, b, scale);
        cx += 6 * scale + scale; // 7 * scale width per char
        str++;
    }
}

void init_buttons() {
    int id_idx = 0;
    
    // Grid: 4 cols (120px each), 5 rows (120px each). Display takes top 200px.
    int y_start = 200;
    int cell_s = 120;
    int rad = 50; // Button radius
    
    struct { char id; const char* label; BtnType type; } layout[5][4] = {
        { {'D', "<",  BTN_FUNC}, {'C', "AC", BTN_FUNC}, {'%', "%", BTN_FUNC}, {'/', "/", BTN_OP} },
        { {'7', "7",  BTN_NUM},  {'8', "8",  BTN_NUM},  {'9', "9", BTN_NUM},  {'*', "*", BTN_OP} },
        { {'4', "4",  BTN_NUM},  {'5', "5",  BTN_NUM},  {'6', "6", BTN_NUM},  {'-', "-", BTN_OP} },
        { {'1', "1",  BTN_NUM},  {'2', "2",  BTN_NUM},  {'3', "3", BTN_NUM},  {'+', "+", BTN_OP} },
        { {'N', "+/-",BTN_NUM},  {'0', "0",  BTN_NUM},  {'.', ".", BTN_NUM},  {'=', "=", BTN_OP} }
    };
    
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            Button *b = &buttons[id_idx++];
            b->cx = col * cell_s + (cell_s / 2);
            b->cy = y_start + row * cell_s + (cell_s / 2);
            b->r = rad;
            b->id = layout[row][col].id;
            strcpy(b->label, layout[row][col].label);
            b->type = layout[row][col].type;
            b->pressed = false;
        }
    }
}

void init_graphics() {
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) {
        perror("Error: cannot open /dev/fb0");
        exit(1);
    }

    ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);

    fb_width = vinfo.xres;
    fb_height = vinfo.yres;
    fb_bpp = vinfo.bits_per_pixel;

    screensize = finfo.smem_len;
    if (screensize == 0) {
        screensize = fb_width * fb_height * (fb_bpp / 8);
    }

    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == MAP_FAILED) {
        perror("Error: mmap framebuffer failed");
        close(fbfd);
        exit(4);
    }
    
    backbuffer = (char *)malloc(screensize);
    if (!backbuffer) {
        perror("Error: failed to allocate backbuffer");
        exit(5);
    }
    memset(backbuffer, 0, screensize);
    memset(fbp, 0, screensize);
    
    printf("Framebuffer: %dx%d @ %dbpp\n", fb_width, fb_height, fb_bpp);
    
    init_buttons();
}

void init_input() {
    for (int i = 0; i < MAX_INPUT_DEVS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            input_fds[input_count++] = fd;
        }
    }
    printf("Opened %d input devices\n", input_count);
}

void process_input() {
    struct input_event ev;
    for (int i = 0; i < input_count; i++) {
        int fd = input_fds[i];
        while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) touch_x = ev.value;
                if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) touch_y = ev.value;
                if (ev.code == ABS_MT_TRACKING_ID) touch_active = (ev.value >= 0);
            } else if (ev.type == EV_KEY) {
                if (ev.code == BTN_TOUCH) {
                    touch_active = (ev.value != 0);
                } else if (ev.value == 1) { // On Key Down
                    if (ev.code == KEY_POWER || ev.code == KEY_ESC || ev.code == KEY_SLEEP || ev.code == KEY_WAKEUP) {
                        printf("Exit button pressed: %d\n", ev.code);
                        if (backbuffer) free(backbuffer);
                        if (fbp) munmap(fbp, screensize);
                        exit(0); 
                    }
                }
            }
        }
    }
}

void render() {
    if (!backbuffer || !fbp) return;

    // Dark theme background
    draw_rect(0, 0, fb_width, fb_height, 28, 28, 28);
    
    // Draw display text
    int scale = 8;
    int text_w = strlen(display_text) * (7 * scale);
    int text_x = fb_width - 20 - text_w;
    if (text_x < 20) text_x = 20;
    
    // Draw white text in display area
    draw_str_scaled(text_x, 80, display_text, 255, 255, 255, scale);

    // Draw buttons
    for (int i = 0; i < BTN_COUNT; i++) {
        Button *b = &buttons[i];
        
        uint8_t r = 0, g = 0, bl = 0;
        
        // Colors based on type
        if (b->type == BTN_NUM) {
            r = 60; g = 60; bl = 60; // Dark Gray
        } else if (b->type == BTN_FUNC) {
            r = 100; g = 100; bl = 100; // Medium Gray
        } else if (b->type == BTN_OP) {
            r = 255; g = 149; bl = 0; // Orange
        }
        
        // Darken if pressed
        if (b->pressed) {
            r = r > 40 ? r - 40 : 0;
            g = g > 40 ? g - 40 : 0;
            bl = bl > 40 ? bl - 40 : 0;
        }
        
        // Draw circular button
        draw_circle(b->cx, b->cy, b->r, r, g, bl);
        
        // Draw label
        int lbl_scale = 5;
        int lbl_len = strlen(b->label);
        int lbl_w = lbl_len * (7 * lbl_scale) - lbl_scale;
        int lbl_h = 8 * lbl_scale;
        
        int lbl_x = b->cx - lbl_w / 2;
        int lbl_y = b->cy - lbl_h / 2;
        
        // Always white text
        draw_str_scaled(lbl_x, lbl_y, b->label, 255, 255, 255, lbl_scale);
    }
    
    // Blit backbuffer to actual framebuffer
    memcpy(fbp, backbuffer, screensize);
}

void handle_logic(char id) {
    if ((id >= '0' && id <= '9') || id == '.') {
        if (is_new_val) {
            snprintf(display_text, sizeof(display_text), "%c", id);
            is_new_val = false;
        } else {
            if (strlen(display_text) < 15) {
                int len = strlen(display_text);
                display_text[len] = id;
                display_text[len+1] = '\0';
            }
        }
        current_val = atof(display_text);
    } else if (id == 'C') { // AC
        current_val = 0;
        stored_val = 0;
        current_op = 0;
        is_new_val = true;
        strcpy(display_text, "0");
    } else if (id == 'D') { // Delete
        int len = strlen(display_text);
        if (len > 1 && !is_new_val) {
            display_text[len-1] = '\0';
        } else {
            strcpy(display_text, "0");
            is_new_val = true;
        }
        current_val = atof(display_text);
    } else if (id == 'N') { // +/-
        current_val = -current_val;
        snprintf(display_text, sizeof(display_text), "%g", current_val);
    } else if (id == '%') {
        current_val = current_val / 100.0;
        snprintf(display_text, sizeof(display_text), "%g", current_val);
        is_new_val = true;
    } else if (id == '+' || id == '-' || id == '*' || id == '/') {
        stored_val = current_val;
        current_op = id;
        is_new_val = true;
    } else if (id == '=') {
        if (current_op == '+') current_val = stored_val + current_val;
        else if (current_op == '-') current_val = stored_val - current_val;
        else if (current_op == '*') current_val = stored_val * current_val;
        else if (current_op == '/') {
            if (current_val != 0) current_val = stored_val / current_val;
        }
        
        snprintf(display_text, sizeof(display_text), "%g", current_val);
        stored_val = current_val;
        current_op = 0;
        is_new_val = true;
    }
}

void init_logging() {
    const char *sd_log = "/data/mnt/sd_0/calc.log";
    FILE *lf = fopen(sd_log, "a");
    if (lf) {
        setvbuf(lf, NULL, _IOLBF, 0);
        dup2(fileno(lf), STDOUT_FILENO);
        dup2(fileno(lf), STDERR_FILENO);
        fclose(lf);
        printf("=== Calculator Log Started ===\n");
    }
}

int main() {
    init_logging();
    init_graphics();
    init_input();

    render();

    while (1) {
        process_input();
        
        bool update_needed = false;

        for (int i = 0; i < BTN_COUNT; i++) {
            Button *b = &buttons[i];
            
            // Check if touch is inside circle
            int dx = touch_x - b->cx;
            int dy = touch_y - b->cy;
            bool in_bounds = (dx*dx + dy*dy <= b->r*b->r);
            
            bool is_pressed = touch_active && in_bounds;
            if (is_pressed != b->pressed) {
                b->pressed = is_pressed;
                update_needed = true;
                
                if (!is_pressed && in_bounds && touch_was_active) {
                    handle_logic(b->id);
                }
            }
        }
        
        touch_was_active = touch_active;
        
        if (update_needed || touch_active) {
             render();
        }

        usleep(16000); // ~60 FPS
    }

    return 0;
}
