#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdint.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "roboto_ttf.h"

// --- Framebuffer Globals ---
static int fbfd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static long int screensize = 0;
static char *fbp = NULL;
static char *backbuffer = NULL;

int fb_width = 480;
int fb_height = 800;
int fb_bpp = 16;

// --- Input Globals ---
#define MAX_INPUT_DEVS 20
static int input_fds[MAX_INPUT_DEVS];
static int input_count = 0;

static int touch_x = -1;
static int touch_y = -1;
static bool touch_active = false;
static bool touch_was_active = false;

// Scrolling
static int scroll_y = 0;

// --- Colors ---
#define BG_R 28
#define BG_G 28
#define BG_B 28
#define TOP_R 45
#define TOP_G 45
#define TOP_B 45
#define ITEM_R 35
#define ITEM_G 35
#define ITEM_B 35

// DIMENSIONS
#define ITEM_H 90
#define TOP_BAR_H 90

// --- File System ---
#define SD_ROOT "/data/mnt/sd_0"
#define MAX_PATH 512
#define MAX_FILES 1024

char current_dir[MAX_PATH] = SD_ROOT;
char clipboard_path[MAX_PATH] = "";
bool clipboard_is_move = false;

typedef struct {
    char name[256];
    bool is_dir;
    off_t size;
    mode_t mode;
} FileItem;

FileItem files[MAX_FILES];
int file_count = 0;
int selected_index = -1;

typedef enum {
    STATE_LIST,
    STATE_MENU,
    STATE_CONFIRM_DELETE,
    STATE_PROPERTIES,
    STATE_KEYBOARD,
    STATE_VIEWER
} AppState;

AppState current_state = STATE_LIST;
char keyboard_buffer[256] = "";
bool is_rename = false;
bool needs_render = true;

char *viewer_text = NULL;
int viewer_scroll_y = 0;
int viewer_max_scroll = 0;

// --- Font Globals ---
#define ATLAS_SIZE 1024
unsigned char font_atlas[ATLAS_SIZE * ATLAS_SIZE];
stbtt_packedchar chardata_ascii_36[95];
stbtt_packedchar chardata_cyrillic_36[256];
stbtt_packedchar chardata_ascii_24[95];
stbtt_packedchar chardata_cyrillic_24[256];

// --- Utilities ---
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void draw_rect(int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
    if (rx >= fb_width || ry >= fb_height || rx + rw <= 0 || ry + rh <= 0) return;
    int start_y = ry < 0 ? 0 : ry;
    int end_y = ry + rh > fb_height ? fb_height : ry + rh;
    int start_x = rx < 0 ? 0 : rx;
    int end_x = rx + rw > fb_width ? fb_width : rx + rw;
    if (!backbuffer) return;
    uint16_t c16 = rgb565(r, g, b);
    uint32_t c32 = (r << 16) | (g << 8) | b;
    for (int y = start_y; y < end_y; y++) {
        char *line = backbuffer + y * finfo.line_length;
        for (int x = start_x; x < end_x; x++) {
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

static void init_font() {
    stbtt_pack_context spc;
    stbtt_PackBegin(&spc, font_atlas, ATLAS_SIZE, ATLAS_SIZE, 0, 1, NULL);
    stbtt_PackSetOversampling(&spc, 1, 1);
    
    stbtt_pack_range ranges[4];
    ranges[0].font_size = 36.0f;
    ranges[0].first_unicode_codepoint_in_range = 32;
    ranges[0].array_of_unicode_codepoints = NULL;
    ranges[0].num_chars = 95;
    ranges[0].chardata_for_range = chardata_ascii_36;

    ranges[1].font_size = 36.0f;
    ranges[1].first_unicode_codepoint_in_range = 0x0400;
    ranges[1].array_of_unicode_codepoints = NULL;
    ranges[1].num_chars = 256;
    ranges[1].chardata_for_range = chardata_cyrillic_36;

    ranges[2].font_size = 24.0f;
    ranges[2].first_unicode_codepoint_in_range = 32;
    ranges[2].array_of_unicode_codepoints = NULL;
    ranges[2].num_chars = 95;
    ranges[2].chardata_for_range = chardata_ascii_24;

    ranges[3].font_size = 24.0f;
    ranges[3].first_unicode_codepoint_in_range = 0x0400;
    ranges[3].array_of_unicode_codepoints = NULL;
    ranges[3].num_chars = 256;
    ranges[3].chardata_for_range = chardata_cyrillic_24;

    stbtt_PackFontRanges(&spc, roboto_ttf, 0, ranges, 4);
    stbtt_PackEnd(&spc);
}

const char *utf8_to_codepoint(const char *p, uint32_t *codepoint) {
    if (!p || !*p) return NULL;
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) {
        *codepoint = c;
        return p + 1;
    } else if ((c & 0xE0) == 0xC0) {
        *codepoint = ((c & 0x1F) << 6) | (p[1] & 0x3F);
        return p + 2;
    } else if ((c & 0xF0) == 0xE0) {
        *codepoint = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return p + 3;
    } else if ((c & 0xF8) == 0xF0) {
        *codepoint = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return p + 4;
    }
    *codepoint = '?';
    return p + 1;
}

static void draw_str_ttf(int x, int y, const char *text, uint8_t r, uint8_t g, uint8_t b, int size_mode) {
    if (!backbuffer) return;
    float fx = x;
    float fy = y;
    uint32_t codepoint;
    
    stbtt_packedchar *ascii_data = (size_mode == 36) ? chardata_ascii_36 : chardata_ascii_24;
    stbtt_packedchar *cyrillic_data = (size_mode == 36) ? chardata_cyrillic_36 : chardata_cyrillic_24;

    while (text && *text) {
        text = utf8_to_codepoint(text, &codepoint);
        stbtt_aligned_quad q;
        if (codepoint >= 32 && codepoint < 127) {
            stbtt_GetPackedQuad(ascii_data, ATLAS_SIZE, ATLAS_SIZE, codepoint - 32, &fx, &fy, &q, 0);
        } else if (codepoint >= 0x0400 && codepoint < 0x0500) {
            stbtt_GetPackedQuad(cyrillic_data, ATLAS_SIZE, ATLAS_SIZE, codepoint - 0x0400, &fx, &fy, &q, 0);
        } else {
            fx += (size_mode == 36) ? 18 : 12;
            continue;
        }
        
        int qx0 = q.x0; int qy0 = q.y0;
        int qx1 = q.x1; int qy1 = q.y1;
        int src_x = q.s0 * ATLAS_SIZE;
        int src_y = q.t0 * ATLAS_SIZE;
        
        for (int dy = 0; dy < (qy1 - qy0); dy++) {
            int cy = qy0 + dy;
            if (cy < 0 || cy >= fb_height) continue;
            char *line = backbuffer + cy * finfo.line_length;
            
            for (int dx = 0; dx < (qx1 - qx0); dx++) {
                int cx = qx0 + dx;
                if (cx < 0 || cx >= fb_width) continue;
                
                uint8_t alpha = font_atlas[(src_y + dy) * ATLAS_SIZE + (src_x + dx)];
                if (alpha > 0) {
                    if (fb_bpp == 16) {
                        uint16_t bg = ((uint16_t*)line)[cx];
                        uint8_t br = (bg >> 11) << 3;
                        uint8_t bg_g = ((bg >> 5) & 0x3F) << 2;
                        uint8_t bb = (bg & 0x1F) << 3;
                        uint8_t out_r = (r * alpha + br * (255 - alpha)) / 255;
                        uint8_t out_g = (g * alpha + bg_g * (255 - alpha)) / 255;
                        uint8_t out_b = (b * alpha + bb * (255 - alpha)) / 255;
                        ((uint16_t*)line)[cx] = rgb565(out_r, out_g, out_b);
                    }
                }
            }
        }
    }
}

static void measure_viewer_text() {
    if (!viewer_text) { viewer_max_scroll = 0; return; }
    float fx = 10;
    float fy = TOP_BAR_H + 30;
    int line_height = 30; // 24px font
    const char *text = viewer_text;
    uint32_t codepoint;
    
    while (*text) {
        if (*text == '\n') { fx = 10; fy += line_height; text++; continue; }
        const char *next_text = utf8_to_codepoint(text, &codepoint);
        if (codepoint == '\r') { text = next_text; continue; }
        
        float temp_fx = fx, temp_fy = fy;
        stbtt_aligned_quad q;
        bool valid = false;
        
        if (codepoint >= 32 && codepoint < 127) {
            stbtt_GetPackedQuad(chardata_ascii_24, ATLAS_SIZE, ATLAS_SIZE, codepoint - 32, &temp_fx, &temp_fy, &q, 0); valid = true;
        } else if (codepoint >= 0x0400 && codepoint < 0x0500) {
            stbtt_GetPackedQuad(chardata_cyrillic_24, ATLAS_SIZE, ATLAS_SIZE, codepoint - 0x0400, &temp_fx, &temp_fy, &q, 0); valid = true;
        }
        
        if (valid) {
            if (q.x1 > fb_width - 10) {
                fx = 10; fy += line_height;
                if (codepoint >= 32 && codepoint < 127) {
                    stbtt_GetPackedQuad(chardata_ascii_24, ATLAS_SIZE, ATLAS_SIZE, codepoint - 32, &fx, &fy, &q, 0);
                } else {
                    stbtt_GetPackedQuad(chardata_cyrillic_24, ATLAS_SIZE, ATLAS_SIZE, codepoint - 0x0400, &fx, &fy, &q, 0);
                }
            } else {
                fx = temp_fx; fy = temp_fy;
            }
        } else {
            fx += 12;
        }
        text = next_text;
    }
    int total_h = (fy - (TOP_BAR_H + 30)) + line_height;
    viewer_max_scroll = total_h - (fb_height - TOP_BAR_H - 10);
    if (viewer_max_scroll < 0) viewer_max_scroll = 0;
}

static void draw_text_wrapped(int start_x, int start_y, const char *text, uint8_t r, uint8_t g, uint8_t b) {
    if (!backbuffer || !text) return;
    float fx = start_x;
    float fy = start_y;
    uint32_t codepoint;
    int line_height = 30; // 24px font

    while (*text) {
        if (*text == '\n') { fx = start_x; fy += line_height; text++; continue; }
        const char *next_text = utf8_to_codepoint(text, &codepoint);
        if (codepoint == '\r') { text = next_text; continue; }
        
        float temp_fx = fx, temp_fy = fy;
        stbtt_aligned_quad q;
        bool valid = false;
        
        if (codepoint >= 32 && codepoint < 127) {
            stbtt_GetPackedQuad(chardata_ascii_24, ATLAS_SIZE, ATLAS_SIZE, codepoint - 32, &temp_fx, &temp_fy, &q, 0); valid = true;
        } else if (codepoint >= 0x0400 && codepoint < 0x0500) {
            stbtt_GetPackedQuad(chardata_cyrillic_24, ATLAS_SIZE, ATLAS_SIZE, codepoint - 0x0400, &temp_fx, &temp_fy, &q, 0); valid = true;
        }
        
        if (valid) {
            if (q.x1 > fb_width - 10) {
                fx = start_x; fy += line_height;
                if (codepoint >= 32 && codepoint < 127) {
                    stbtt_GetPackedQuad(chardata_ascii_24, ATLAS_SIZE, ATLAS_SIZE, codepoint - 32, &fx, &fy, &q, 0);
                } else {
                    stbtt_GetPackedQuad(chardata_cyrillic_24, ATLAS_SIZE, ATLAS_SIZE, codepoint - 0x0400, &fx, &fy, &q, 0);
                }
            } else {
                fx = temp_fx; fy = temp_fy;
            }
            
            if (q.y1 >= TOP_BAR_H && q.y0 < fb_height) {
                int qx0 = q.x0; int qy0 = q.y0;
                int qx1 = q.x1; int qy1 = q.y1;
                int src_x = q.s0 * ATLAS_SIZE;
                int src_y = q.t0 * ATLAS_SIZE;
                
                for (int dy = 0; dy < (qy1 - qy0); dy++) {
                    int cy = qy0 + dy;
                    if (cy < TOP_BAR_H || cy >= fb_height) continue;
                    char *line = backbuffer + cy * finfo.line_length;
                    
                    for (int dx = 0; dx < (qx1 - qx0); dx++) {
                        int cx = qx0 + dx;
                        if (cx < 0 || cx >= fb_width) continue;
                        uint8_t alpha = font_atlas[(src_y + dy) * ATLAS_SIZE + (src_x + dx)];
                        if (alpha > 0) {
                            if (fb_bpp == 16) {
                                uint16_t bg = ((uint16_t*)line)[cx];
                                uint8_t br = (bg >> 11) << 3;
                                uint8_t bg_g = ((bg >> 5) & 0x3F) << 2;
                                uint8_t bb = (bg & 0x1F) << 3;
                                uint8_t out_r = (r * alpha + br * (255 - alpha)) / 255;
                                uint8_t out_g = (g * alpha + bg_g * (255 - alpha)) / 255;
                                uint8_t out_b = (b * alpha + bb * (255 - alpha)) / 255;
                                ((uint16_t*)line)[cx] = rgb565(out_r, out_g, out_b);
                            }
                        }
                    }
                }
            } else if (q.y0 >= fb_height) {
                break; // Stop rendering if we are below the screen
            }
        } else {
            fx += 12;
        }
        text = next_text;
    }
}

static void draw_icon_folder(int x, int y, int scale) {
    draw_rect(x, y + 2*scale, 8*scale, 6*scale, 255, 200, 100);
    draw_rect(x, y, 4*scale, 2*scale, 255, 200, 100);
}

static void draw_icon_file(int x, int y, int scale) {
    draw_rect(x + 1*scale, y, 6*scale, 8*scale, 200, 200, 200);
    draw_rect(x + 5*scale, y, 2*scale, 2*scale, 150, 150, 150);
}

bool is_text_ext(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return true;
    ext++;
    const char *exts[] = {"txt", "md", "cfg", "log", "ini", "sh", "c", "h", "cpp", "py", "json", "xml", NULL};
    for (int i = 0; exts[i]; i++) {
        if (strcasecmp(ext, exts[i]) == 0) return true;
    }
    return false;
}

void open_viewer(const char *path) {
    if (viewer_text) free(viewer_text);
    viewer_text = NULL;
    
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize > 256 * 1024) fsize = 256 * 1024; // Limit to 256KB to avoid massive lag
    
    viewer_text = malloc(fsize + 1);
    if (viewer_text) {
        fread(viewer_text, 1, fsize, f);
        viewer_text[fsize] = '\0';
        viewer_scroll_y = 0;
        measure_viewer_text();
        current_state = STATE_VIEWER;
        needs_render = true;
    }
    fclose(f);
}

int cmp_fileitem(const void *a, const void *b) {
    FileItem *fa = (FileItem *)a;
    FileItem *fb = (FileItem *)b;
    if (strcmp(fa->name, "..") == 0) return -1;
    if (strcmp(fb->name, "..") == 0) return 1;

    if (fa->is_dir && !fb->is_dir) return -1;
    if (!fa->is_dir && fb->is_dir) return 1;
    return strcasecmp(fa->name, fb->name);
}

void load_dir(const char *path) {
    DIR *d = opendir(path);
    file_count = 0;
    scroll_y = 0;
    
    if (!d) {
        if (strcmp(path, SD_ROOT) != 0) {
            strcpy(current_dir, SD_ROOT);
            load_dir(SD_ROOT);
        }
        return;
    }
    
    if (strcmp(path, "/") != 0) {
        strcpy(files[file_count].name, "..");
        files[file_count].is_dir = true;
        file_count++;
    }
    
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL && file_count < MAX_FILES) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, dir->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            strcpy(files[file_count].name, dir->d_name);
            files[file_count].is_dir = S_ISDIR(st.st_mode);
            files[file_count].size = st.st_size;
            files[file_count].mode = st.st_mode;
            file_count++;
        }
    }
    closedir(d);
    
    qsort(files, file_count, sizeof(FileItem), cmp_fileitem);
    needs_render = true;
}

static void recursive_delete(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            DIR *d = opendir(path);
            if (d) {
                struct dirent *dir;
                while ((dir = readdir(d)) != NULL) {
                    if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
                    char sub[1024];
                    snprintf(sub, sizeof(sub), "%s/%s", path, dir->d_name);
                    recursive_delete(sub);
                }
                closedir(d);
            }
            rmdir(path);
        } else {
            unlink(path);
        }
    }
}

static void do_copy_file(const char *src, const char *dst) {
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) return;
    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd_dst < 0) { close(fd_src); return; }
    
    char buf[4096];
    ssize_t bytes;
    while ((bytes = read(fd_src, buf, sizeof(buf))) > 0) {
        write(fd_dst, buf, bytes);
    }
    close(fd_src);
    close(fd_dst);
}

static void recursive_copy(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            mkdir(dst, 0777);
            DIR *d = opendir(src);
            if (d) {
                struct dirent *dir;
                while ((dir = readdir(d)) != NULL) {
                    if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
                    char sub_src[1024], sub_dst[1024];
                    snprintf(sub_src, sizeof(sub_src), "%s/%s", src, dir->d_name);
                    snprintf(sub_dst, sizeof(sub_dst), "%s/%s", dst, dir->d_name);
                    recursive_copy(sub_src, sub_dst);
                }
                closedir(d);
            }
        } else {
            do_copy_file(src, dst);
        }
    }
}

void render_list() {
    draw_rect(0, 0, fb_width, fb_height, BG_R, BG_G, BG_B);
    
    int y_offset = TOP_BAR_H - scroll_y;
    
    for (int i = 0; i < file_count; i++) {
        int item_y = y_offset + i * ITEM_H;
        if (item_y + ITEM_H < TOP_BAR_H || item_y > fb_height) continue;
        
        if (i % 2 == 0) {
            draw_rect(0, item_y, fb_width, ITEM_H, ITEM_R, ITEM_G, ITEM_B);
        } else {
            draw_rect(0, item_y, fb_width, ITEM_H, BG_R, BG_G, BG_B);
        }
        
        if (files[i].is_dir) {
            draw_icon_folder(20, item_y + 25, 5);
        } else {
            draw_icon_file(20, item_y + 25, 5);
        }
        
        draw_str_ttf(75, item_y + 55, files[i].name, 255, 255, 255, 24);
        
        draw_circle(fb_width - 40, item_y + ITEM_H/2, 22, 80, 80, 80);
        draw_str_ttf(fb_width - 55, item_y + ITEM_H/2 + 6, "...", 255, 255, 255, 24);
    }
    
    draw_rect(0, 0, fb_width, TOP_BAR_H, TOP_R, TOP_G, TOP_B);
    draw_str_ttf(15, 60, "HibyCommander", 255, 255, 255, 36);
    
    draw_rect(fb_width - 200, 20, 50, 50, 100, 100, 100);
    draw_str_ttf(fb_width - 192, 58, "H", 255, 255, 255, 36);
    
    draw_rect(fb_width - 130, 20, 50, 50, 100, 100, 100);
    draw_str_ttf(fb_width - 122, 58, "+F", 255, 255, 255, 36);

    if (strlen(clipboard_path) > 0) {
        draw_rect(fb_width - 60, 20, 50, 50, 50, 150, 50);
        draw_str_ttf(fb_width - 52, 58, "P", 255, 255, 255, 36);
    }
}

void render_menu() {
    render_list(); 
    
    int mw = 360, mh = 545;
    int mx = (fb_width - mw) / 2;
    int my = (fb_height - mh) / 2;
    
    draw_rect(mx, my, mw, mh, 50, 50, 50);
    draw_rect(mx, my, mw, 55, 70, 70, 70);
    draw_str_ttf(mx + 15, my + 40, files[selected_index].name, 255, 255, 255, 36);
    
    const char *opts[] = {"Rename", "Copy", "Move", "Delete", "Properties", "Open as text", "Cancel"};
    for (int i = 0; i < 7; i++) {
        draw_rect(mx + 15, my + 70 + i * 65, mw - 30, 55, 90, 90, 90);
        draw_str_ttf(mx + 30, my + 110 + i * 65, opts[i], 255, 255, 255, 36);
    }
}

void render_confirm() {
    render_list();
    
    int mw = 360, mh = 260;
    int mx = (fb_width - mw) / 2;
    int my = (fb_height - mh) / 2;
    
    draw_rect(mx, my, mw, mh, 50, 50, 50);
    draw_str_ttf(mx + 20, my + 50, "Delete this?", 255, 100, 100, 36);
    draw_str_ttf(mx + 20, my + 100, files[selected_index].name, 255, 255, 255, 24);
    
    draw_rect(mx + 30, my + 150, 120, 70, 200, 50, 50);
    draw_str_ttf(mx + 55, my + 195, "YES", 255, 255, 255, 36);
    
    draw_rect(mx + 210, my + 150, 120, 70, 100, 100, 100);
    draw_str_ttf(mx + 245, my + 195, "NO", 255, 255, 255, 36);
}

void render_properties() {
    render_list();
    
    int mw = 400, mh = 320;
    int mx = (fb_width - mw) / 2;
    int my = (fb_height - mh) / 2;
    
    draw_rect(mx, my, mw, mh, 50, 50, 50);
    draw_str_ttf(mx + 20, my + 40, "Properties", 200, 200, 200, 36);
    
    char buf[256];
    snprintf(buf, sizeof(buf), "Name: %s", files[selected_index].name);
    draw_str_ttf(mx + 20, my + 90, buf, 255, 255, 255, 24);
    
    off_t sz = files[selected_index].size;
    if (sz >= 1024 * 1024) {
        snprintf(buf, sizeof(buf), "Size: %.2f MB", (double)sz / (1024.0 * 1024.0));
    } else if (sz >= 1024) {
        snprintf(buf, sizeof(buf), "Size: %.2f KB", (double)sz / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "Size: %ld B", (long)sz);
    }
    draw_str_ttf(mx + 20, my + 140, buf, 255, 255, 255, 24);
    
    snprintf(buf, sizeof(buf), "Perms: %o", files[selected_index].mode & 0777);
    draw_str_ttf(mx + 20, my + 190, buf, 255, 255, 255, 24);
    
    draw_rect(mx + 125, my + 230, 150, 60, 100, 100, 100);
    draw_str_ttf(mx + 175, my + 272, "OK", 255, 255, 255, 36);
}

void render_keyboard() {
    render_list();
    
    draw_rect(0, fb_height - 480, fb_width, 480, 40, 40, 40);
    
    draw_rect(15, fb_height - 460, fb_width - 30, 60, 255, 255, 255);
    draw_str_ttf(25, fb_height - 418, keyboard_buffer, 0, 0, 0, 36);
    
    const char *keys[4][10] = {
        {"q","w","e","r","t","y","u","i","o","p"},
        {"a","s","d","f","g","h","j","k","l","."},
        {"z","x","c","v","b","n","m","-","_","DEL"},
        {"SPACE", "OK", "CANCEL", "", "", "", "", "", "", ""}
    };
    
    int kw = fb_width / 10;
    int kh = 90;
    int ky = fb_height - 380;
    
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 10; c++) {
            if (strlen(keys[r][c]) == 0) continue;
            
            int bx = c * kw;
            int by = ky + r * kh;
            int bw = kw - 2;
            
            if (r == 3) {
                if (c == 0) bw = kw * 4 - 2; // SPACE
                else if (c == 1) { bx = 4 * kw; bw = kw * 3 - 2; } // OK
                else if (c == 2) { bx = 7 * kw; bw = kw * 3 - 2; } // CANCEL
                else continue;
            }
            
            draw_rect(bx + 1, by + 1, bw, kh - 2, 80, 80, 80);
            draw_str_ttf(bx + 15, by + 60, keys[r][c], 255, 255, 255, 24);
        }
    }
}

void render_viewer() {
    draw_rect(0, 0, fb_width, fb_height, BG_R, BG_G, BG_B);
    draw_rect(0, 0, fb_width, TOP_BAR_H, TOP_R, TOP_G, TOP_B);
    
    draw_str_ttf(15, 60, files[selected_index].name, 255, 255, 255, 36);
    
    draw_rect(fb_width - 70, 20, 50, 50, 200, 50, 50);
    draw_str_ttf(fb_width - 55, 58, "X", 255, 255, 255, 36);
    
    if (viewer_text) {
        draw_text_wrapped(10, TOP_BAR_H + 30 - viewer_scroll_y, viewer_text, 200, 200, 200);
    }
}

void render_all() {
    if (!needs_render) return;
    
    if (current_state == STATE_LIST) render_list();
    else if (current_state == STATE_MENU) render_menu();
    else if (current_state == STATE_CONFIRM_DELETE) render_confirm();
    else if (current_state == STATE_PROPERTIES) render_properties();
    else if (current_state == STATE_KEYBOARD) render_keyboard();
    else if (current_state == STATE_VIEWER) render_viewer();
    
    memcpy(fbp, backbuffer, screensize);
    needs_render = false;
}

void handle_list_touch(int tx, int ty, bool pressed) {
    if (!pressed && touch_was_active) {
        if (ty < TOP_BAR_H) {
            if (tx > fb_width - 200 && tx < fb_width - 150) {
                strcpy(current_dir, SD_ROOT);
                load_dir(current_dir);
            } else if (tx > fb_width - 130 && tx < fb_width - 80) {
                current_state = STATE_KEYBOARD;
                is_rename = false;
                keyboard_buffer[0] = '\0';
                needs_render = true;
            } else if (tx > fb_width - 60 && tx < fb_width - 10 && strlen(clipboard_path) > 0) {
                char dst[1024];
                char *basename = strrchr(clipboard_path, '/');
                if (basename) basename++; else basename = clipboard_path;
                snprintf(dst, sizeof(dst), "%s/%s", current_dir, basename);
                
                if (clipboard_is_move) {
                    rename(clipboard_path, dst);
                    clipboard_path[0] = '\0';
                } else {
                    recursive_copy(clipboard_path, dst);
                }
                load_dir(current_dir);
            }
            return;
        }
        
        int clicked_idx = (ty - TOP_BAR_H + scroll_y) / ITEM_H;
        if (clicked_idx >= 0 && clicked_idx < file_count) {
            if (tx > fb_width - 90) { 
                selected_index = clicked_idx;
                current_state = STATE_MENU;
                needs_render = true;
            } else {
                if (files[clicked_idx].is_dir) {
                    char next_dir[1024];
                    if (strcmp(files[clicked_idx].name, "..") == 0) {
                        char *last_slash = strrchr(current_dir, '/');
                        if (last_slash && last_slash != current_dir) {
                            *last_slash = '\0';
                        } else if (last_slash == current_dir) {
                            strcpy(current_dir, "/");
                        }
                    } else {
                        if (strcmp(current_dir, "/") == 0) {
                            snprintf(next_dir, sizeof(next_dir), "/%s", files[clicked_idx].name);
                        } else {
                            snprintf(next_dir, sizeof(next_dir), "%s/%s", current_dir, files[clicked_idx].name);
                        }
                        strcpy(current_dir, next_dir);
                    }
                    load_dir(current_dir);
                } else if (is_text_ext(files[clicked_idx].name)) {
                    char full_path[1024];
                    if (strcmp(current_dir, "/") == 0) {
                        snprintf(full_path, sizeof(full_path), "/%s", files[clicked_idx].name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, files[clicked_idx].name);
                    }
                    selected_index = clicked_idx;
                    open_viewer(full_path);
                }
            }
        }
    }
}

void handle_menu_touch(int tx, int ty, bool pressed) {
    if (!pressed && touch_was_active) {
        int mw = 360, mh = 545;
        int mx = (fb_width - mw) / 2;
        int my = (fb_height - mh) / 2;
        
        if (tx > mx && tx < mx + mw && ty > my + 70 && ty < my + mh) {
            int opt = (ty - (my + 70)) / 65;
            char full_path[1024];
            if (strcmp(current_dir, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", files[selected_index].name);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, files[selected_index].name);
            }
            
            if (opt == 0) {
                current_state = STATE_KEYBOARD;
                is_rename = true;
                strcpy(keyboard_buffer, files[selected_index].name);
            } else if (opt == 1) {
                strcpy(clipboard_path, full_path);
                clipboard_is_move = false;
                current_state = STATE_LIST;
            } else if (opt == 2) {
                strcpy(clipboard_path, full_path);
                clipboard_is_move = true;
                current_state = STATE_LIST;
            } else if (opt == 3) {
                current_state = STATE_CONFIRM_DELETE;
            } else if (opt == 4) {
                current_state = STATE_PROPERTIES;
            } else if (opt == 5) {
                open_viewer(full_path);
            } else if (opt == 6) {
                current_state = STATE_LIST;
            }
            needs_render = true;
        } else {
            current_state = STATE_LIST;
            needs_render = true;
        }
    }
}

void handle_confirm_touch(int tx, int ty, bool pressed) {
    if (!pressed && touch_was_active) {
        int mw = 360, mh = 260;
        int mx = (fb_width - mw) / 2;
        int my = (fb_height - mh) / 2;
        
        if (ty > my + 150 && ty < my + 220) {
            if (tx > mx + 30 && tx < mx + 150) {
                char full_path[1024];
                if (strcmp(current_dir, "/") == 0) {
                    snprintf(full_path, sizeof(full_path), "/%s", files[selected_index].name);
                } else {
                    snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, files[selected_index].name);
                }
                recursive_delete(full_path);
                load_dir(current_dir);
                current_state = STATE_LIST;
            } else if (tx > mx + 210 && tx < mx + 330) {
                current_state = STATE_LIST;
            }
            needs_render = true;
        }
    }
}

void handle_properties_touch(int tx, int ty, bool pressed) {
    if (!pressed && touch_was_active) {
        int mw = 400, mh = 320;
        int mx = (fb_width - mw) / 2;
        int my = (fb_height - mh) / 2;
        
        if (tx > mx + 125 && tx < mx + 275 && ty > my + 230 && ty < my + 290) {
            current_state = STATE_LIST;
            needs_render = true;
        }
    }
}

void handle_keyboard_touch(int tx, int ty, bool pressed) {
    if (!pressed && touch_was_active) {
        if (ty > fb_height - 380) {
            int r = (ty - (fb_height - 380)) / 90;
            int kw = fb_width / 10;
            if (r >= 0 && r < 3) {
                int c = tx / kw;
                const char *keys[3][10] = {
                    {"q","w","e","r","t","y","u","i","o","p"},
                    {"a","s","d","f","g","h","j","k","l","."},
                    {"z","x","c","v","b","n","m","-","_","DEL"}
                };
                if (c == 9 && r == 2) {
                    int len = strlen(keyboard_buffer);
                    if (len > 0) keyboard_buffer[len - 1] = '\0';
                } else {
                    if (strlen(keyboard_buffer) < 255) {
                        strcat(keyboard_buffer, keys[r][c]);
                    }
                }
                needs_render = true;
            } else if (r == 3) {
                if (tx < kw * 4) {
                    if (strlen(keyboard_buffer) < 255) strcat(keyboard_buffer, " ");
                } else if (tx < kw * 7) {
                    if (strlen(keyboard_buffer) > 0) {
                        char new_path[1024];
                        if (strcmp(current_dir, "/") == 0) {
                            snprintf(new_path, sizeof(new_path), "/%s", keyboard_buffer);
                        } else {
                            snprintf(new_path, sizeof(new_path), "%s/%s", current_dir, keyboard_buffer);
                        }
                        if (is_rename) {
                            char old_path[1024];
                            if (strcmp(current_dir, "/") == 0) {
                                snprintf(old_path, sizeof(old_path), "/%s", files[selected_index].name);
                            } else {
                                snprintf(old_path, sizeof(old_path), "%s/%s", current_dir, files[selected_index].name);
                            }
                            rename(old_path, new_path);
                        } else {
                            mkdir(new_path, 0777);
                        }
                        load_dir(current_dir);
                    }
                    current_state = STATE_LIST;
                } else {
                    current_state = STATE_LIST;
                }
                needs_render = true;
            }
        }
    }
}

void handle_viewer_touch(int tx, int ty, bool pressed) {
    if (!pressed && touch_was_active) {
        if (ty < TOP_BAR_H && tx > fb_width - 80) {
            current_state = STATE_LIST;
            if (viewer_text) { free(viewer_text); viewer_text = NULL; }
            needs_render = true;
        }
    }
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
                } else if (ev.value == 1 || ev.value == 2) { 
                    if (ev.code == KEY_POWER || ev.code == KEY_ESC) {
                        if (current_state == STATE_VIEWER) {
                            current_state = STATE_LIST;
                            if (viewer_text) { free(viewer_text); viewer_text = NULL; }
                            needs_render = true;
                        } else {
                            exit(0); 
                        }
                    } else if (ev.code == KEY_VOLUMEUP) {
                        if (current_state == STATE_LIST) {
                            scroll_y -= ITEM_H * 2;
                            if (scroll_y < 0) scroll_y = 0;
                            needs_render = true;
                        } else if (current_state == STATE_VIEWER) {
                            viewer_scroll_y -= 120;
                            if (viewer_scroll_y < 0) viewer_scroll_y = 0;
                            needs_render = true;
                        }
                    } else if (ev.code == KEY_VOLUMEDOWN) {
                        if (current_state == STATE_LIST) {
                            int max_scroll = file_count * ITEM_H - (fb_height - TOP_BAR_H);
                            if (max_scroll < 0) max_scroll = 0;
                            scroll_y += ITEM_H * 2;
                            if (scroll_y > max_scroll) scroll_y = max_scroll;
                            needs_render = true;
                        } else if (current_state == STATE_VIEWER) {
                            viewer_scroll_y += 120;
                            if (viewer_scroll_y > viewer_max_scroll) viewer_scroll_y = viewer_max_scroll;
                            needs_render = true;
                        }
                    }
                }
            }
        }
    }
    
    if (touch_active || touch_was_active) {
        if (current_state == STATE_LIST) handle_list_touch(touch_x, touch_y, touch_active);
        else if (current_state == STATE_MENU) handle_menu_touch(touch_x, touch_y, touch_active);
        else if (current_state == STATE_CONFIRM_DELETE) handle_confirm_touch(touch_x, touch_y, touch_active);
        else if (current_state == STATE_PROPERTIES) handle_properties_touch(touch_x, touch_y, touch_active);
        else if (current_state == STATE_KEYBOARD) handle_keyboard_touch(touch_x, touch_y, touch_active);
        else if (current_state == STATE_VIEWER) handle_viewer_touch(touch_x, touch_y, touch_active);
    }
    
    touch_was_active = touch_active;
}

void init_graphics() {
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) exit(1);
    ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
    fb_width = vinfo.xres;
    fb_height = vinfo.yres;
    fb_bpp = vinfo.bits_per_pixel;
    screensize = finfo.smem_len;
    if (screensize == 0) screensize = fb_width * fb_height * (fb_bpp / 8);
    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == MAP_FAILED) exit(4);
    backbuffer = (char *)malloc(screensize);
    if (!backbuffer) exit(5);
    memset(backbuffer, 0, screensize);
}

void init_input() {
    for (int i = 0; i < MAX_INPUT_DEVS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) input_fds[input_count++] = fd;
    }
}

int main() {
    init_font();
    init_graphics();
    init_input();
    load_dir(current_dir);
    while (1) {
        process_input();
        render_all();
        usleep(16000); 
    }
    return 0;
}
