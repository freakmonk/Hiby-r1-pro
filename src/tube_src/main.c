#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <dlfcn.h>
#include <errno.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "splash.h"
#include "loading.h"
#include "font.h"
#include <libavutil/imgutils.h>

/* --- Input --- */
#define MAX_INPUT_DEVS 20
static int input_fds[MAX_INPUT_DEVS];
static int input_count = 0;
static volatile int quit = 0;
static volatile int paused = 0;
static volatile int seek_request = 0; // Seeking won't work easily with piped input!

static void init_input(void) {
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

static int current_volume = 31; // 3% default (31/1024)

static void load_volume(void) {
    FILE *f = fopen("/tmp/tube_vol", "r");
    if (f) {
        fscanf(f, "%d", &current_volume);
        fclose(f);
    }
}

static void save_volume(void) {
    FILE *f = fopen("/tmp/tube_vol", "w");
    if (f) {
        fprintf(f, "%d\n", current_volume);
        fclose(f);
    }
}

static char **g_argv;
static const char *g_video_id;

static void do_seek(int ty, int current_sec, int total_duration) {
    int target = current_sec;
    int seek_amount = (total_duration > 0) ? (total_duration / 10) : 15;
    if (seek_amount == 0) seek_amount = 15;
    
    char seek_label[32];
    if (ty > 400) {
        target += seek_amount; // Right half visually
        snprintf(seek_label, sizeof(seek_label), "+%dsec", seek_amount);
    } else {
        target -= seek_amount; // Left half visually
        snprintf(seek_label, sizeof(seek_label), "-%dsec", seek_amount);
    }
    
    if (target < 0) target = 0;
    if (target >= total_duration && total_duration > 0) target = total_duration - 2;
    
    char st_str[32];
    snprintf(st_str, sizeof(st_str), "%d", target);
    
    printf("Seeking to %d seconds...\n", target);
    
    char dur_str[32];
    snprintf(dur_str, sizeof(dur_str), "%d", total_duration);
    
    char *page_arg = "0";
    // Check if g_argv has at least 5 arguments (page)
    int argc_count = 0;
    while (g_argv[argc_count] != NULL) argc_count++;
    if (argc_count >= 5) {
        page_arg = g_argv[4];
    }
    
    char *new_argv[] = { g_argv[0], (char*)g_video_id, st_str, dur_str, page_arg, seek_label, NULL };
    execvp(g_argv[0], new_argv);
}

static int touch_x = 0;
static int touch_y = 0;
static int touch_down = 0;

static void check_input(int current_sec, int total_duration) {
    struct input_event ev;
    for (int i = 0; i < input_count; i++) {
        while (read(input_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                if (ev.code == KEY_POWER || ev.code == KEY_ESC) {
                    printf("Exit button pressed\n");
                    quit = 1;
                    return;
                } else if (ev.code == KEY_VOLUMEUP) {
                    current_volume += 2; // +0.2%
                    if (current_volume > 1024) current_volume = 1024;
                    printf("Volume: %.1f%%\n", (float)current_volume * 100.0f / 1024.0f);
                    save_volume();
                } else if (ev.code == KEY_VOLUMEDOWN) {
                    current_volume -= 2; // -0.2%
                    if (current_volume < 0) current_volume = 0;
                    printf("Volume: %.1f%%\n", (float)current_volume * 100.0f / 1024.0f);
                    save_volume();
                } else if (ev.code == KEY_PLAY || ev.code == KEY_PLAYPAUSE ||
                           ev.code == KEY_PAUSE || ev.code == KEY_ENTER) {
                    paused = !paused;
                    printf("%s\n", paused ? "Paused" : "Resumed");
                }
            } else if (ev.type == EV_ABS) {
                if (ev.code == 0x00 || ev.code == 0x35) touch_x = ev.value;
                else if (ev.code == 0x01 || ev.code == 0x36) touch_y = ev.value;
                else if (ev.code == 0x39) {
                    int new_down = (ev.value != -1);
                    if (touch_down && !new_down) {
                        do_seek(touch_y, current_sec, total_duration);
                    }
                    touch_down = new_down;
                }
            } else if (ev.type == EV_KEY && (ev.code == 0x14a || ev.code == 0x110)) {
                int new_down = (ev.value != 0);
                if (touch_down && !new_down) {
                    do_seek(touch_y, current_sec, total_duration);
                }
                touch_down = new_down;
            }
        }
    }
}

static void close_input(void) {
    for (int i = 0; i < input_count; i++) {
        close(input_fds[i]);
    }
}

/* --- ALSA Dynamic Loading (dlopen) --- */
#define SYM(h, ptr, sym, type) ptr = (type)dlsym(h, sym)

static void *g_alsa_lib = NULL;
static void *g_pcm = NULL;

static int (*x_snd_pcm_open)(void **, const char *, int, int);
static int (*x_snd_pcm_close)(void *);
static long (*x_snd_pcm_writei)(void *, const void *, unsigned long);
static int (*x_snd_pcm_prepare)(void *);
static int (*x_snd_pcm_drop)(void *);
static int (*x_snd_pcm_recover)(void *, int, int);
static int (*x_snd_pcm_hw_params_malloc)(void **);
static void (*x_snd_pcm_hw_params_free)(void *);
static int (*x_snd_pcm_hw_params_any)(void *, void *);
static int (*x_snd_pcm_hw_params_set_access)(void *, void *, int);
static int (*x_snd_pcm_hw_params_set_format)(void *, void *, int);
static int (*x_snd_pcm_hw_params_set_channels)(void *, void *, unsigned int);
static int (*x_snd_pcm_hw_params_set_rate_near)(void *, void *, unsigned int *, int *);
static int (*x_snd_pcm_hw_params_set_buffer_time_near)(void *, void *, unsigned int *, int *);
static int (*x_snd_pcm_hw_params_set_period_time_near)(void *, void *, unsigned int *, int *);
static int (*x_snd_pcm_hw_params)(void *, void *);

static int load_alsa(void) {
    if (g_alsa_lib) return 0;
    
    g_alsa_lib = dlopen("libasound.so", RTLD_LAZY);
    if (!g_alsa_lib) g_alsa_lib = dlopen("libasound.so.2", RTLD_LAZY);
    if (!g_alsa_lib) { printf("dlopen libasound failed: %s\n", dlerror()); return -1; }

    SYM(g_alsa_lib, x_snd_pcm_open, "snd_pcm_open", int (*)(void **, const char *, int, int));
    SYM(g_alsa_lib, x_snd_pcm_close, "snd_pcm_close", int (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_writei, "snd_pcm_writei", long (*)(void *, const void *, unsigned long));
    SYM(g_alsa_lib, x_snd_pcm_prepare, "snd_pcm_prepare", int (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_drop, "snd_pcm_drop", int (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_recover, "snd_pcm_recover", int (*)(void *, int, int));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_malloc, "snd_pcm_hw_params_malloc", int (*)(void **));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_free, "snd_pcm_hw_params_free", void (*)(void *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_any, "snd_pcm_hw_params_any", int (*)(void *, void *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_access, "snd_pcm_hw_params_set_access", int (*)(void *, void *, int));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_format, "snd_pcm_hw_params_set_format", int (*)(void *, void *, int));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_channels, "snd_pcm_hw_params_set_channels", int (*)(void *, void *, unsigned int));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_rate_near, "snd_pcm_hw_params_set_rate_near", int (*)(void *, void *, unsigned int *, int *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_buffer_time_near, "snd_pcm_hw_params_set_buffer_time_near", int (*)(void *, void *, unsigned int *, int *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params_set_period_time_near, "snd_pcm_hw_params_set_period_time_near", int (*)(void *, void *, unsigned int *, int *));
    SYM(g_alsa_lib, x_snd_pcm_hw_params, "snd_pcm_hw_params", int (*)(void *, void *));

    if (!x_snd_pcm_open || !x_snd_pcm_close || !x_snd_pcm_writei || !x_snd_pcm_hw_params_malloc) {
        printf("Missing ALSA symbols!\n");
        return -1;
    }
    return 0;
}

static int open_alsa(unsigned int rate, unsigned int channels) {
    if (load_alsa() < 0) return -1;
    
    int err;
    if ((err = x_snd_pcm_open(&g_pcm, "plughw:0,0", 0, 0)) < 0) {
        if ((err = x_snd_pcm_open(&g_pcm, "hw:0,0", 0, 0)) < 0) {
            printf("Cannot open ALSA audio device (%d)\n", err);
            return -1;
        }
    }
    
    void *hw;
    x_snd_pcm_hw_params_malloc(&hw);
    x_snd_pcm_hw_params_any(g_pcm, hw);
    x_snd_pcm_hw_params_set_access(g_pcm, hw, 3); /* SND_PCM_ACCESS_RW_INTERLEAVED */
    x_snd_pcm_hw_params_set_format(g_pcm, hw, 2); /* SND_PCM_FORMAT_S16_LE */
    x_snd_pcm_hw_params_set_channels(g_pcm, hw, channels);
    
    unsigned int want_rate = rate;
    int dir = 0;
    x_snd_pcm_hw_params_set_rate_near(g_pcm, hw, &want_rate, &dir);
    
    unsigned int btime = 500000, bdir = 0; // 500ms buffer
    x_snd_pcm_hw_params_set_buffer_time_near(g_pcm, hw, &btime, (int *)&bdir);
    unsigned int ptime = 100000, pdir = 0; // 100ms period
    x_snd_pcm_hw_params_set_period_time_near(g_pcm, hw, &ptime, (int *)&pdir);
    
    if (x_snd_pcm_hw_params(g_pcm, hw) < 0) {
        printf("Cannot set ALSA hw params\n");
        x_snd_pcm_hw_params_free(hw);
        x_snd_pcm_close(g_pcm);
        g_pcm = NULL;
        return -1;
    }
    x_snd_pcm_hw_params_free(hw);
    
    x_snd_pcm_prepare(g_pcm);
    printf("ALSA initialized: %d Hz, %d ch\n", want_rate, channels);
    return 0;
}

/* --- AVIO Read Callback --- */
/* --- AVIO Read Callback --- */
static int read_packet(void *opaque, uint8_t *buf, int buf_size) {
    FILE *f = (FILE *)opaque;
    // Use read() instead of fread() to return partial data immediately and prevent freezing!
    ssize_t len = read(fileno(f), buf, buf_size);
    if (len == 0) {
        return AVERROR_EOF;
    } else if (len < 0) {
        return AVERROR(errno);
    }
    return (int)len;
}

/* --- YUV to RGB Fast Conversion --- */
static int Y_table[256];
static int RV_table[256];
static int GU_table[256];
static int GV_table[256];
static int BU_table[256];

static void init_yuv_tables(void) {
    for (int i = 0; i < 256; i++) {
        Y_table[i] = (298 * (i - 16)) + 128;
        RV_table[i] = 409 * (i - 128);
        GU_table[i] = -100 * (i - 128);
        GV_table[i] = -208 * (i - 128);
        BU_table[i] = 516 * (i - 128);
    }
}

static inline uint16_t fast_yuv_to_rgb565(uint8_t y, uint8_t u, uint8_t v) {
    int y_val = Y_table[y];
    int r = (y_val + RV_table[v]) >> 8;
    int g = (y_val + GU_table[u] + GV_table[v]) >> 8;
    int b = (y_val + BU_table[u]) >> 8;
    
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static inline uint32_t fast_yuv_to_rgb888(uint8_t y, uint8_t u, uint8_t v) {
    int y_val = Y_table[y];
    int r = (y_val + RV_table[v]) >> 8;
    int g = (y_val + GU_table[u] + GV_table[v]) >> 8;
    int b = (y_val + BU_table[u]) >> 8;
    
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    
    return (r << 16) | (g << 8) | b;
}

#define MAX_FEED_VIDEOS 30
static char feed_videos[MAX_FEED_VIDEOS][32];
static int feed_count = 0;

static void fetch_feed_list(const char *proxy_ip) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "wget -qO /tmp/feed_list http://%s:8080/feed/list", proxy_ip);
    system(cmd);
    
    FILE *f = fopen("/tmp/feed_list", "r");
    feed_count = 0;
    if (f) {
        while (feed_count < MAX_FEED_VIDEOS && fscanf(f, "%31s", feed_videos[feed_count]) == 1) {
            feed_count++;
        }
        fclose(f);
        remove("/tmp/feed_list");
    }
}

static void draw_text_rotated(char *fbp, int phys_x, int phys_y, int logical_cx, int logical_cy, const char *str, int scale, uint16_t color, uint16_t bg_color) {
    int len = strlen(str);
    int total_w = len * 8 * scale;
    int total_h = 8 * scale;
    int start_x = logical_cx - total_w / 2;
    int start_y = logical_cy - total_h / 2;
    
    // Draw background box
    for (int by = -4*scale; by < total_h + 4*scale; by++) {
        for (int bx = -4*scale; bx < total_w + 4*scale; bx++) {
            int lx = start_x + bx;
            int ly = start_y + by;
            int px = phys_x - 1 - ly;
            int py = lx;
            if (px >= 0 && px < phys_x && py >= 0 && py < phys_y) {
                uint16_t *dst = (uint16_t *)(fbp + py * (phys_x * 2) + px * 2);
                *dst = bg_color;
            }
        }
    }
    
    int cx = start_x;
    int cy = start_y;
    while (*str) {
        if (*str >= 32 && *str < 128) {
            const unsigned char *glyph = font8x8[*str - 32];
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    if (glyph[r] & (1 << c)) {
                        for (int sy = 0; sy < scale; sy++) {
                            for (int sx = 0; sx < scale; sx++) {
                                int logical_x = cx + c * scale + sx;
                                int logical_y = cy + r * scale + sy;
                                
                                int phys_px = phys_x - 1 - logical_y;
                                int phys_py = logical_x;
                                
                                if (phys_px >= 0 && phys_px < phys_x && phys_py >= 0 && phys_py < phys_y) {
                                    uint16_t *dst = (uint16_t *)(fbp + phys_py * (phys_x * 2) + phys_px * 2);
                                    *dst = color;
                                }
                            }
                        }
                    }
                }
            }
        }
        cx += 8 * scale;
        str++;
    }
}

static void draw_feed_page(const char *proxy_ip, int page, char *fbp, long screensize) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "wget -qO- http://%s:8080/feed/page?p=%d", proxy_ip, page);
    FILE *f = popen(cmd, "r");
    if (f) {
        size_t total = 0;
        while (total < (size_t)screensize) {
            size_t r = fread(fbp + total, 1, screensize - total, f);
            if (r <= 0) break;
            total += r;
        }
        pclose(f);
    }
}

int main(int argc, char *argv[]) {
    printf("==== YouTube Tube Player ====\n"); fflush(stdout);
    
    init_yuv_tables();
    load_volume();
    
    const char *video_id = "tpa_mdO21jA"; 
    if (argc >= 2) {
        video_id = argv[1];
    }
    
    g_argv = argv;
    g_video_id = video_id;

    char proxy_ip[64] = "192.168.88.24"; // Default
    FILE *cfg = fopen("tube.cfg", "r");
    if (cfg) {
        if (fgets(proxy_ip, sizeof(proxy_ip), cfg)) {
            char *nl = strchr(proxy_ip, '\n');
            if (nl) *nl = '\0';
        }
        fclose(cfg);
    } else {
        printf("tube.cfg not found, using default proxy IP: %s\n", proxy_ip);
    }

    printf("Using proxy: %s:8080\n", proxy_ip);


    int start_time = 0;
    if (argc >= 3) {
        start_time = atoi(argv[2]);
    }

    /* 1. Init Framebuffer */
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) {
        perror("Cannot open /dev/fb0");
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        return -1;
    }
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        return -1;
    }

    printf("Framebuffer: %dx%d, %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    long screensize = vinfo.yres_virtual * finfo.line_length;
    char *fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if ((intptr_t)fbp == -1) {
        perror("mmap failed");
        return -1;
    }
    
    int is_feed_mode = 0;
    if (argc == 1 || (argc >= 2 && strcmp(argv[1], "FEED") == 0)) {
        is_feed_mode = 1;
    }

    int is_seek = (argc >= 6 && strlen(argv[5]) > 0);

    if (is_seek) {
        // Draw the seek label overlaid on top of the frozen video frame
        // logical width = 800, height = 480 (landscape). center is 400, 240.
        draw_text_rotated(fbp, 480, 800, 400, 240, argv[5], 6, 0xFFFF, 0x0000);
    } else {
        // Clear screen to black and ensure first buffer is displayed
        memset(fbp, 0, screensize);
        if (is_feed_mode) {
            memcpy(fbp, splash_image, sizeof(splash_image) > screensize ? screensize : sizeof(splash_image));
        } else {
            memcpy(fbp, loading_image, sizeof(loading_image) > screensize ? screensize : sizeof(loading_image));
        }
    }
    vinfo.yoffset = 0;
    ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo);

    // Initialize inputs BEFORE feed mode so buttons/touch work
    init_input();

    int current_page = 0;
    if (argc == 1) {
        printf("Entering FEED MODE...\n");
        printf("Enabling WIFI...\n");
        system("/usr/bin/wifi_on.sh");
        sleep(3);
    } else if (is_feed_mode) {
        printf("Returning to FEED MODE...\n");
        if (argc >= 3) current_page = atoi(argv[2]);
    }

    if (is_feed_mode) {
        fetch_feed_list(proxy_ip);
        
        int total_pages = (feed_count + 1) / 2;
        if (total_pages == 0) total_pages = 1;
        if (current_page >= total_pages) current_page = total_pages - 1;
        if (current_page < 0) current_page = 0;
        
        draw_feed_page(proxy_ip, current_page, fbp, screensize);
        
        int ticks = 0;
        while (1) {
            struct input_event ev;
            int needs_redraw = 0;
            
            for (int i = 0; i < input_count; i++) {
                while (read(input_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ev.type == EV_KEY && ev.value == 1) {
                        if (ev.code == KEY_POWER || ev.code == KEY_ESC) {
                            quit = 1;
                        } else if (ev.code == KEY_VOLUMEUP) {
                            current_page--;
                            if (current_page < 0) current_page = 0;
                            needs_redraw = 1;
                        } else if (ev.code == KEY_VOLUMEDOWN) {
                            current_page++;
                            if (current_page >= total_pages) current_page = total_pages - 1;
                            needs_redraw = 1;
                        }
                    } else if (ev.type == EV_ABS) {
                        if (ev.code == 0x00 || ev.code == 0x35) touch_x = ev.value;
                        else if (ev.code == 0x01 || ev.code == 0x36) touch_y = ev.value;
                        else if (ev.code == 0x39) {
                            int new_down = (ev.value != -1);
                            if (touch_down && !new_down) {
                                int idx = current_page * 2;
                                if (touch_y >= 400) idx += 1;
                                
                                if (idx < feed_count) {
                                    char page_str[16];
                                    snprintf(page_str, sizeof(page_str), "%d", current_page);
                                    char *new_argv[] = { g_argv[0], feed_videos[idx], "0", "0", page_str, NULL };
                                    execvp(g_argv[0], new_argv);
                                }
                            }
                            touch_down = new_down;
                        }
                    } else if (ev.type == EV_KEY && (ev.code == 0x14a || ev.code == 0x110)) {
                        int new_down = (ev.value != 0);
                        if (touch_down && !new_down) {
                            int idx = current_page * 2;
                            if (touch_y >= 400) idx += 1;
                            
                            if (idx < feed_count) {
                                char page_str[16];
                                snprintf(page_str, sizeof(page_str), "%d", current_page);
                                char *new_argv[] = { g_argv[0], feed_videos[idx], "0", "0", page_str, NULL };
                                execvp(g_argv[0], new_argv);
                            }
                        }
                        touch_down = new_down;
                    }
                }
            }
            if (quit) break;
            
            if (feed_count == 0) {
                ticks++;
                if (ticks >= 40) { // Every 2 seconds
                    ticks = 0;
                    fetch_feed_list(proxy_ip);
                    total_pages = (feed_count + 1) / 2;
                    if (total_pages == 0) total_pages = 1;
                    needs_redraw = 1;
                }
            }
            
            if (needs_redraw) {
                draw_feed_page(proxy_ip, current_page, fbp, screensize);
            }
            usleep(50000);
        }
        
        close_input();
        munmap(fbp, screensize);
        close(fbfd);
        
        // Turn off wifi when fully exiting the app
        system("/usr/bin/wifi_off.sh");
        return 0;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "wget -qO- \"http://%s:8080/stream?v=%s&ss=%d\"", proxy_ip, video_id, start_time);
    
    printf("Connecting to proxy for stream...\n");
    FILE *stream_fp = popen(cmd, "r");
    if (!stream_fp) {
        printf("Failed to start wget for streaming\n");
        return -1;
    }
    
    // Fetch video duration
    int total_duration = 0;
    if (argc >= 4 && atoi(argv[3]) > 0) {
        total_duration = atoi(argv[3]);
    } else {
        char dur_cmd[256];
        printf("Fetching video duration...\n");
        snprintf(dur_cmd, sizeof(dur_cmd), "wget -qO /tmp/vid_dur http://%s:8080/duration?v=%s", proxy_ip, video_id);
        system(dur_cmd);
        FILE *dur_file = fopen("/tmp/vid_dur", "r");
        if (dur_file) {
            fscanf(dur_file, "%d", &total_duration);
            fclose(dur_file);
            remove("/tmp/vid_dur");
        }
    }
    printf("Video duration: %d seconds\n", total_duration);    
    int fb_bpp = vinfo.bits_per_pixel;
    enum AVPixelFormat out_fmt;
    if (fb_bpp == 32) {
        out_fmt = AV_PIX_FMT_BGRA;
    } else if (fb_bpp == 16) {
        out_fmt = AV_PIX_FMT_RGB565LE;
    } else {
        printf("Unsupported bpp: %d\n", fb_bpp);
        return -1;
    }

    /* 2. FFmpeg Init */
    size_t avio_ctx_buffer_size = 1048576; // 1MB network buffer
    uint8_t *avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    AVIOContext *avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, stream_fp, &read_packet, NULL, NULL);
    
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    pFormatCtx->pb = avio_ctx;

    printf("Opening input stream...\n"); fflush(stdout);
    if (avformat_open_input(&pFormatCtx, "dummy", NULL, NULL) != 0) {
        printf("Couldn't open input stream.\n");
        return -1;
    }

    printf("Finding stream info...\n"); fflush(stdout);
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Couldn't find stream information.\n");
        return -1;
    }

    /* Find video and audio streams */
    int videoStream = -1;
    int audioStream = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0) {
            videoStream = i;
        }
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0) {
            audioStream = i;
        }
    }

    if (videoStream == -1) {
        printf("Didn't find a video stream.\n");
        return -1;
    }

    /* Video decoder setup */
    AVCodecParameters *pCodecPar = pFormatCtx->streams[videoStream]->codecpar;
    const AVCodec *pCodec = avcodec_find_decoder(pCodecPar->codec_id);
    if (pCodec == NULL) {
        printf("Video codec not found.\n");
        return -1;
    }

    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, pCodecPar);
    
    // Skip B-frames to save CPU!
    pCodecCtx->skip_frame = AVDISCARD_NONREF;
    
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("Could not open video codec.\n");
        return -1;
    }

    /* Audio decoder + resampler + ALSA setup */
    AVCodecContext *aCodecCtx = NULL;
    SwrContext *swr_ctx = NULL;
    uint8_t *audio_buf = NULL;
    int audio_buf_size = 0;
    int out_sample_rate = 44100;
    int out_channels = 2;

    if (audioStream >= 0) {
        AVCodecParameters *aCodecPar = pFormatCtx->streams[audioStream]->codecpar;
        const AVCodec *aCodec = avcodec_find_decoder(aCodecPar->codec_id);
        if (aCodec) {
            aCodecCtx = avcodec_alloc_context3(aCodec);
            avcodec_parameters_to_context(aCodecCtx, aCodecPar);
            if (avcodec_open2(aCodecCtx, aCodec, NULL) == 0) {
                AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                int ret = swr_alloc_set_opts2(&swr_ctx,
                    &out_ch_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
                    &aCodecCtx->ch_layout, aCodecCtx->sample_fmt, aCodecCtx->sample_rate,
                    0, NULL);
                if (ret >= 0) swr_init(swr_ctx);

                printf("Audio: %dHz %dch -> %dHz %dch S16LE\n",
                       aCodecCtx->sample_rate, aCodecCtx->ch_layout.nb_channels,
                       out_sample_rate, out_channels);

                open_alsa(out_sample_rate, out_channels);
            }
        }
    }

    /* Video setup */
    AVFrame *pFrame = av_frame_alloc();
    AVFrame *aFrame = av_frame_alloc();

    /* OPTIMIZATION 2: Render at native 1:1 resolution (640x360) and center it */
    int scale_w = pCodecCtx->width;
    int scale_h = pCodecCtx->height;
    int phys_x = vinfo.xres; // 480
    int phys_y = vinfo.yres; // 800
    
    // Center the video on screen (video is already pre-rotated by proxy)
    int y_offset = (phys_y > scale_h) ? (phys_y - scale_h) / 2 : 0;
    int x_offset = (phys_x > scale_w) ? (phys_x - scale_w) / 2 : 0;

    AVPacket *packet = av_packet_alloc();

    printf("Playing video (rotated 90 CW)...\n");
    
    // No need for a backbuffer since our rotation loop writes sequentially now!
    // We will write directly to the memory-mapped framebuffer `fbp`.

    long long sum_read_time = 0;
    long long sum_vdec_time = 0;
    long long sum_vrend_time = 0;
    long long sum_adec_time = 0;
    long long sum_awrite_time = 0;
    int profile_frames = 0;
    
    struct timeval tv;
    #define GET_TIME_MS() (gettimeofday(&tv, NULL) == 0 ? (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000 : 0)

    while (!quit) {
        long long t_read_start = GET_TIME_MS();
        int read_ret = av_read_frame(pFormatCtx, packet);
        long long t_read_end = GET_TIME_MS();
        if (read_ret < 0) break;
        
        sum_read_time += (t_read_end - t_read_start);

        /* --- VIDEO PACKET --- */
        if (packet->stream_index == videoStream) {
            long long t_vdec_start = GET_TIME_MS();
            if (avcodec_send_packet(pCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                    long long t_vdec_end = GET_TIME_MS();
                    sum_vdec_time += (t_vdec_end - t_vdec_start);
                    
                    // No frame skipping here! The proxy will control the frame rate via ffmpeg.

                    long long t_vrend_start = GET_TIME_MS();

                    /* OPTIMIZATION 3 & 4: Cache-friendly sequential render (Rotation is offloaded to Mac!) */
                    if (fb_bpp == 16) {
                        for (int sy = 0; sy < scale_h; sy++) {
                            int dst_y = sy + y_offset;
                            if (dst_y >= phys_y) break;
                            uint16_t *dst_row = (uint16_t *)(fbp + dst_y * finfo.line_length);
                            dst_row += x_offset;
                            int uv_y = sy / 2;
                            for (int sx = 0; sx < scale_w; sx++) {
                                int uv_x = sx / 2;
                                uint8_t y_val = pFrame->data[0][sy * pFrame->linesize[0] + sx];
                                uint8_t u_val = pFrame->data[1][uv_y * pFrame->linesize[1] + uv_x];
                                uint8_t v_val = pFrame->data[2][uv_y * pFrame->linesize[2] + uv_x];
                                *dst_row++ = fast_yuv_to_rgb565(y_val, u_val, v_val);
                            }
                        }
                    } else if (fb_bpp == 32) {
                        for (int sy = 0; sy < scale_h; sy++) {
                            int dst_y = sy + y_offset;
                            if (dst_y >= phys_y) break;
                            uint32_t *dst_row = (uint32_t *)(fbp + dst_y * finfo.line_length);
                            dst_row += x_offset;
                            int uv_y = sy / 2;
                            for (int sx = 0; sx < scale_w; sx++) {
                                int uv_x = sx / 2;
                                uint8_t y_val = pFrame->data[0][sy * pFrame->linesize[0] + sx];
                                uint8_t u_val = pFrame->data[1][uv_y * pFrame->linesize[1] + uv_x];
                                uint8_t v_val = pFrame->data[2][uv_y * pFrame->linesize[2] + uv_x];
                                *dst_row++ = fast_yuv_to_rgb888(y_val, u_val, v_val);
                            }
                        }
                    }

                    long long t_vrend_end = GET_TIME_MS();
                    sum_vrend_time += (t_vrend_end - t_vrend_start);

                    double current_sec = start_time;
                    if (pFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
                        current_sec += pFrame->best_effort_timestamp * av_q2d(pFormatCtx->streams[videoStream]->time_base);
                    }

                    /* OPTIMIZATION 5: Draw Progress Bar (Fast memory burst) */
                    if (total_duration > 0 && current_sec > 0) {
                        float progress = current_sec / total_duration;
                        if (progress < 0.0f) progress = 0.0f;
                        if (progress > 1.0f) progress = 1.0f;
                        int filled_h = progress * phys_y;

                        if (fb_bpp == 16) {
                            for (int by = 0; by < phys_y; by++) {
                                uint16_t *dst_row = (uint16_t *)(fbp + by * finfo.line_length);
                                uint16_t color = (by < filled_h) ? 0xF800 : 0x2104;
                                for (int bx = 0; bx < 8; bx++) {
                                    dst_row[bx] = color;
                                }
                            }
                        } else if (fb_bpp == 32) {
                            for (int by = 0; by < phys_y; by++) {
                                uint32_t *dst_row = (uint32_t *)(fbp + by * finfo.line_length);
                                uint32_t color = (by < filled_h) ? 0xFF0000 : 0x333333;
                                for (int bx = 0; bx < 8; bx++) {
                                    dst_row[bx] = color;
                                }
                            }
                        }
                    }

                    do {
                        check_input((int)current_sec, total_duration);
                        if (quit) break;
                        if (paused) usleep(50000);
                    } while (paused && !quit);

                    if (quit) break;

                    if (!g_pcm) usleep(30000);
                    
                    profile_frames++;
                    if (profile_frames >= 30) {
                        printf("\n[PROFILING AVG MS]\n");
                        printf("Read (Network): %lld ms\n", sum_read_time / profile_frames);
                        printf("Video Decode  : %lld ms\n", sum_vdec_time / profile_frames);
                        printf("Video Render  : %lld ms\n", sum_vrend_time / profile_frames);
                        printf("Audio Decode  : %lld ms\n", sum_adec_time / profile_frames);
                        printf("Audio ALSA Out: %lld ms\n", sum_awrite_time / profile_frames);
                        sum_read_time = sum_vdec_time = sum_vrend_time = sum_adec_time = sum_awrite_time = 0;
                        profile_frames = 0;
                    }
                    
                    t_vdec_start = GET_TIME_MS(); // for next loop iteration
                }
            }
        }
        /* --- AUDIO PACKET --- */
        else if (packet->stream_index == audioStream && aCodecCtx && g_pcm && !paused) {
            long long t_adec_start = GET_TIME_MS();
            if (avcodec_send_packet(aCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(aCodecCtx, aFrame) == 0) {
                    long long t_adec_end = GET_TIME_MS();
                    sum_adec_time += (t_adec_end - t_adec_start);
                    
                    int out_samples = swr_get_out_samples(swr_ctx, aFrame->nb_samples);
                    int needed = out_samples * out_channels * 2;
                    if (needed > audio_buf_size) {
                        audio_buf = realloc(audio_buf, needed);
                        audio_buf_size = needed;
                    }

                    uint8_t *out_buf = audio_buf;
                    int converted = swr_convert(swr_ctx, &out_buf, out_samples,
                        (const uint8_t **)aFrame->data, aFrame->nb_samples);

                    if (converted > 0 && g_pcm) {
                        int16_t *samples = (int16_t *)audio_buf;
                        int total_samples = converted * out_channels;
                        for (int i = 0; i < total_samples; i++) {
                            samples[i] = (int16_t)((samples[i] * current_volume) >> 10);
                        }

                        long long t_awrite_start = GET_TIME_MS();
                        long err = x_snd_pcm_writei(g_pcm, audio_buf, converted);
                        if (err < 0) {
                            x_snd_pcm_recover(g_pcm, err, 0);
                        }
                        long long t_awrite_end = GET_TIME_MS();
                        sum_awrite_time += (t_awrite_end - t_awrite_start);
                    }
                    t_adec_start = GET_TIME_MS();
                }
            }
        }

        av_packet_unref(packet);
    }

    printf("Done.\n");

    /* 3. Cleanup */
    close_input();

    if (g_pcm) x_snd_pcm_close(g_pcm);
    if (g_alsa_lib) dlclose(g_alsa_lib);

    if (swr_ctx) swr_free(&swr_ctx);
    if (aCodecCtx) avcodec_free_context(&aCodecCtx);
    free(audio_buf);

    av_frame_free(&aFrame);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecCtx);
    
    // For custom AVIOContext, we must free it correctly:
    if (avio_ctx) {
        av_freep(&avio_ctx->buffer);
        avio_context_free(&avio_ctx);
    }
    
    avformat_close_input(&pFormatCtx);
    av_packet_free(&packet);
    
    munmap(fbp, screensize);
    close(fbfd);
    
    pclose(stream_fp);
    
    // Return to FEED MODE instead of exiting
    char page_str[16] = "0";
    if (argc >= 5) {
        strncpy(page_str, argv[4], sizeof(page_str) - 1);
    }
    char *new_argv[] = { g_argv[0], "FEED", page_str, NULL };
    execvp(g_argv[0], new_argv);

    return 0;
}
