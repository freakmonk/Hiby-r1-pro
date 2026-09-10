#include "sys.h"
static int global_audio_volume = 3;
static unsigned int alsa_rate = 22050;


#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <linux/input.h>
#include <pthread.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/time.h>

#define MAX_INPUT_DEVS 20
static int input_fds[MAX_INPUT_DEVS];
static int input_count = 0;

static int fbfd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static long int screensize = 0;
static char *fbp = NULL;

static int hiby_fb_width = 480;
static int hiby_fb_height = 800;
static int hiby_fb_bpp = 16;

#define SCREEN_W 320
#define SCREEN_H 200
#define GAME_VIEW_X 0
#define GAME_VIEW_Y 40

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


static int touch_x = -1, touch_y = -1;
static bool touch_active = false;
static bool touch_held_up = false;
static bool touch_held_down = false;
static bool touch_held_left = false;
static bool touch_held_right = false;
static bool touch_held_btn = false;
static bool touch_held_pause = false;
static bool touch_held_code = false;
static bool touch_held_load = false;
static bool touch_held_save = false;

static void update_touch_held() {
    touch_held_up = touch_held_down = touch_held_left = touch_held_right = false;
    touch_held_btn = touch_held_pause = touch_held_code = touch_held_load = touch_held_save = false;
    
    if (!touch_active || touch_x < 0 || touch_y < 0) return;
    
    int x = touch_x, y = touch_y;
    
    // Top bar
    if (y > 370 && y < 430) {
        if (x > 10 && x < 100) touch_held_pause = true;
        if (x > 110 && x < 200) touch_held_code = true;
        if (x > 210 && x < 300) touch_held_load = true;
        if (x > 310 && x < 400) touch_held_save = true;
    }
    
    // D-Pad
    if (x < 300) {
        if (y > 500 && y < 600 && x > 100 && x < 200) touch_held_up = true;
        if (y > 700 && y < 800 && x > 100 && x < 200) touch_held_down = true;
        if (x < 100 && y > 600 && y < 700) touch_held_left = true;
        if (x > 200 && x < 300 && y > 600 && y < 700) touch_held_right = true;
    } else {
        // Action
        if (y > 590 && x > 320) touch_held_btn = true;
    }
}


static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void draw_rect(int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
    if (rx < 0 || ry < 0 || rx + rw > hiby_fb_width || ry + rh > hiby_fb_height) return;
    uint16_t c16 = rgb565(r, g, b);
    for (int y = ry; y < ry + rh; y++) {
        char *line = fbp + y * finfo.line_length;
        for (int x = rx; x < rx + rw; x++) {
            ((uint16_t*)line)[x] = c16;
        }
    }
}

static void draw_letter(int rx, int ry, int size, char c) {
    uint8_t r=255, g=255, b=255;
    if (c == 'P') {
        draw_rect(rx, ry, size, size*5, r,g,b); draw_rect(rx, ry, size*3, size, r,g,b);
        draw_rect(rx, ry+size*2, size*3, size, r,g,b); draw_rect(rx+size*3, ry, size, size*3, r,g,b);
    } else if (c == 'C') {
        draw_rect(rx, ry, size, size*5, r,g,b); draw_rect(rx, ry, size*4, size, r,g,b);
        draw_rect(rx, ry+size*4, size*4, size, r,g,b);
    } else if (c == 'L') {
        draw_rect(rx, ry, size, size*5, r,g,b); draw_rect(rx, ry+size*4, size*4, size, r,g,b);
    } else if (c == 'S') {
        draw_rect(rx, ry, size*4, size, r,g,b); draw_rect(rx, ry, size, size*3, r,g,b);
        draw_rect(rx, ry+size*2, size*4, size, r,g,b); draw_rect(rx+size*3, ry+size*2, size, size*3, r,g,b);
        draw_rect(rx, ry+size*4, size*4, size, r,g,b);
    }
}

static void draw_touch_hud(void) {
    // D-PAD
    draw_rect(100, 500, 100, 100, touch_held_up ? 150:70, 70, 70);   // UP
    draw_rect(100, 700, 100, 100, touch_held_down ? 150:70, 70, 70); // DOWN
    draw_rect(0, 600, 100, 100, touch_held_left ? 150:70, 70, 70);   // LEFT
    draw_rect(200, 600, 100, 100, touch_held_right ? 150:70, 70, 70);// RIGHT

    // ACTION 
    draw_rect(370, 595, 110, 110, touch_held_btn ? 250:180, 40, 40); // FIRE (Red)

    // TOP MENU BAR
    draw_rect(10, 370, 90, 60, touch_held_pause ? 150:100, 100, 100);
    draw_letter(45, 385, 6, 'P');
    
    draw_rect(110, 370, 90, 60, touch_held_code ? 150:100, 100, 100);
    draw_letter(145, 385, 6, 'C');
    
    draw_rect(210, 370, 90, 60, touch_held_load ? 150:100, 100, 100);
    draw_letter(245, 385, 6, 'L');
    
    draw_rect(310, 370, 90, 60, touch_held_save ? 150:100, 100, 100);
    draw_letter(345, 385, 6, 'S');
}

// ALSA
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

#define SYM(lib, ptr, name, type) do { ptr = (type)dlsym(lib, name); } while(0)

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


struct HibyStub : System {
    virtual ~HibyStub() {}
    virtual void init(const char *title);
    virtual void destroy();
    virtual void setPalette(const uint8_t *buf);
    virtual void updateDisplay(const uint8_t *src);
    virtual void processEvents();
    virtual void sleep(uint32_t duration);
    virtual uint32_t getTimeStamp();
    virtual void startAudio(AudioCallback callback, void *param);
    virtual void stopAudio();
    virtual uint32_t getOutputSampleRate();
    virtual int addTimer(uint32_t delay, TimerCallback callback, void *param);
    virtual void removeTimer(int timerId);
    virtual void *createMutex();
    virtual void destroyMutex(void *mutex);
    virtual void lockMutex(void *mutex);
    virtual void unlockMutex(void *mutex);

    pthread_t audio_thread;
    bool audio_running = false;
    AudioCallback audio_cb = nullptr;
    void *audio_param = nullptr;
};

extern bool g_enableLogging;
void HibyStub::init(const char *title) {

    // Redirect stdout/stderr to log file
    if (g_enableLogging) {
        FILE *lf = fopen("/data/mnt/sd_0/another.log", "a");
        if (lf) {
            setvbuf(lf, NULL, _IOLBF, 0);
            dup2(fileno(lf), STDOUT_FILENO);
            dup2(fileno(lf), STDERR_FILENO);
            fclose(lf);
        }
    }
    printf("\n--- Another World Started ---\n");

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

    // Init inputs
    for (int i = 0; i < MAX_INPUT_DEVS; i++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0)
            input_fds[input_count++] = fd;
    }
}

void HibyStub::destroy() {
    if (fbp && fbp != MAP_FAILED) munmap(fbp, screensize);
    if (fbfd >= 0) close(fbfd);
}

void HibyStub::setPalette(const uint8_t *p) {
    for (int i = 0; i < NUM_COLORS; ++i) {
        uint8_t c1 = *(p + 0);
        uint8_t c2 = *(p + 1);
        colors[i].col.r = (((c1 & 0x0F) << 2) | ((c1 & 0x0F) >> 2)) << 2; // r
        colors[i].col.g = (((c2 & 0xF0) >> 2) | ((c2 & 0xF0) >> 6)) << 2; // g
        colors[i].col.b = (((c2 & 0x0F) >> 2) | ((c2 & 0x0F) << 2)) << 2; // b
        colors[i].col.a = 0xFF;
        p += 2;
    }
}

void HibyStub::updateDisplay(const uint8_t *src) {
    // scale up by 3/2 or maybe 2x to fit the screen?
    // Let's just scale by 2x for simple viewing, or 3/2 like fbdoom.
    // Screen is 480x800. Another world is 320x200.
    // Wait, Another World is 320x200 but updateDisplay is passed packed pixels: 2 pixels per byte!
    // Ah, Wait! sysImplementation.cpp says:
    // for each line ... for (i = 0; i < SCREEN_W/2; ++i)
    // p[0] = src[i] >> 4; p[1] = src[i] & 0xF.
    // So src is 160 bytes per line!
    
    // We can render directly to fbp. Let's do 1.5x scale as Another World is 320x200 -> 480x300.
    
    int dst_w = 480;
    int dst_h = 300;
    
    for (int gy = 0; gy < SCREEN_H; gy++) {
        int dst_y1 = GAME_VIEW_Y + (gy * 3) / 2;
        int dst_y2 = GAME_VIEW_Y + ((gy + 1) * 3) / 2;
        
        for (int gx = 0; gx < SCREEN_W; gx++) {
            uint8_t packed = src[gy * (SCREEN_W / 2) + (gx / 2)];
            uint8_t pal_idx = (gx % 2 == 0) ? (packed >> 4) : (packed & 0x0F);
            
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
    draw_touch_hud();
}


static bool hw_held_up = false;
static bool hw_held_down = false;
static bool hw_held_left = false;
static bool hw_held_right = false;
static bool hw_held_btn = false;

void HibyStub::processEvents() {
    struct input_event ev;
    bool touch_changed = false;
    for (int i = 0; i < input_count; i++) {
        int fd = input_fds[i];
        if (fd < 0) continue;

        while (read(fd, &ev, sizeof ev) == sizeof ev) {
            if (ev.type == EV_ABS) {
                switch (ev.code) {
                    case ABS_X:
                    case ABS_MT_POSITION_X:
                        touch_x = ev.value;
                        touch_changed = true;
                        break;
                    case ABS_Y:
                    case ABS_MT_POSITION_Y:
                        touch_y = ev.value;
                        touch_changed = true;
                        break;
                    case ABS_MT_TRACKING_ID:
                        touch_active = (ev.value >= 0);
                        touch_changed = true;
                        break;
                }
            } else if (ev.type == EV_KEY) {
                int pressed = (ev.value != 0);
                if (ev.code == BTN_TOUCH) {
                    touch_active = pressed;
                    touch_changed = true;
                } else {
                    switch (ev.code) {
                        case KEY_VOLUMEUP:
                            if (pressed) {
                                global_audio_volume += 1;
                                if (global_audio_volume > 100) global_audio_volume = 100;
                            }
                            break;
                        case KEY_VOLUMEDOWN:
                            if (pressed) {
                                global_audio_volume -= 1;
                                if (global_audio_volume < 0) global_audio_volume = 0;
                            }
                            break;
                        case KEY_UP: case KEY_NEXTSONG:
                            hw_held_up = pressed;
                            break;
                        case KEY_DOWN:
                            hw_held_down = pressed;
                            break;

                        case KEY_LEFT: case KEY_PREVIOUSSONG:
                            hw_held_left = pressed;
                            break;
                        case KEY_RIGHT:
                            hw_held_right = pressed;
                            break;
                        case KEY_PLAYPAUSE: case KEY_ENTER: case KEY_SPACE:
                            hw_held_btn = pressed;
                            break;
                        case KEY_POWER: case KEY_ESC:
                            if (pressed) input.quit = true;
                            break;
                        case KEY_C:
                            if (pressed) input.code = true;
                            break;
                        case KEY_P:
                            if (pressed) input.pause = true;
                            break;
                    }
                }
            }
        }
    }
    
    if (touch_changed) {
        update_touch_held();
    }
    
    input.dirMask = 0;
    if (hw_held_up || touch_held_up) input.dirMask |= PlayerInput::DIR_UP;
    if (hw_held_down || touch_held_down) input.dirMask |= PlayerInput::DIR_DOWN;
    if (hw_held_left || touch_held_left) input.dirMask |= PlayerInput::DIR_LEFT;
    if (hw_held_right || touch_held_right) input.dirMask |= PlayerInput::DIR_RIGHT;
    
    input.button = hw_held_btn || touch_held_btn;
    if (touch_held_pause) input.pause = true;
    if (touch_held_code) input.code = true;
    if (touch_held_load) input.load = true;
    if (touch_held_save) input.save = true;
}
void HibyStub::sleep(uint32_t duration) {
    usleep(duration * 1000);
}

uint32_t HibyStub::getTimeStamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}







static void* audio_thread_func(void *arg) {
    HibyStub *sys = (HibyStub *)arg;
    uint8_t buffer[2048]; // mono U8 from game
    int16_t out_buffer[2048 * 2]; // stereo S16_LE for ALSA
    while (sys->audio_running) {
        if (sys->audio_cb) {
            sys->audio_cb(sys->audio_param, buffer, sizeof(buffer));
            if (g_pcm) {
                for (int i=0; i<2048; i++) {
                    int32_t sample = ((int16_t)buffer[i] - 128) << 8;
                    sample = (sample * global_audio_volume) / 100;
                    if (sample < -32768) sample = -32768;
                    if (sample > 32767) sample = 32767;
                    out_buffer[i*2] = (int16_t)sample;     // L
                    out_buffer[i*2+1] = (int16_t)sample;   // R
                }
                long err = x_snd_pcm_writei(g_pcm, out_buffer, 2048);
                if (err < 0) {
                    x_snd_pcm_recover(g_pcm, err, 0);
                    usleep(10000); // Backoff on error
                }
            } else {
                usleep(2048 * 1000000 / alsa_rate);
            }
        } else {
            usleep(10000);
        }
    }
    return nullptr;
}
void HibyStub::startAudio(AudioCallback callback, void *param) {
    audio_cb = callback;
    audio_param = param;
    alsa_rate = 22050; // default
    if (load_alsa() == 0) {
        int err;
        if ((err = x_snd_pcm_open(&g_pcm, "plughw:0,0", 0, 0)) < 0) {
            if ((err = x_snd_pcm_open(&g_pcm, "hw:0,0", 0, 0)) < 0) {
                g_pcm = nullptr;
            }
        }
        if (g_pcm) {
            void *hw;
            x_snd_pcm_hw_params_malloc(&hw);
            x_snd_pcm_hw_params_any(g_pcm, hw);
            x_snd_pcm_hw_params_set_access(g_pcm, hw, 3); // RW_INTERLEAVED
            x_snd_pcm_hw_params_set_format(g_pcm, hw, 2); // SND_PCM_FORMAT_S16_LE
            x_snd_pcm_hw_params_set_channels(g_pcm, hw, 2); // Stereo!

            int dir = 0;
            x_snd_pcm_hw_params_set_rate_near(g_pcm, hw, &alsa_rate, &dir);
            unsigned int btime = 500000, ptime = 25000;
            x_snd_pcm_hw_params_set_buffer_time_near(g_pcm, hw, &btime, &dir);
            x_snd_pcm_hw_params_set_period_time_near(g_pcm, hw, &ptime, &dir);
            
            if (x_snd_pcm_hw_params(g_pcm, hw) < 0) {
                x_snd_pcm_hw_params_free(hw);
                x_snd_pcm_close(g_pcm);
                g_pcm = nullptr;
            } else {
                x_snd_pcm_hw_params_free(hw);
                x_snd_pcm_prepare(g_pcm);
            }
        }
    }
    audio_running = true;
    pthread_create(&audio_thread, nullptr, audio_thread_func, this);
}

void HibyStub::stopAudio() {
    if (audio_running) {
        audio_running = false;
        pthread_join(audio_thread, nullptr);
    }
    if (g_pcm) {
        x_snd_pcm_close(g_pcm);
        g_pcm = nullptr;
    }
}

uint32_t HibyStub::getOutputSampleRate() {
    return alsa_rate;
}

struct TimerData {
    uint32_t delay;
    System::TimerCallback callback;
    void *param;
    bool active;
    uint32_t next_tick;
};

static TimerData timers[10];
static pthread_t main_timer_thread;
static bool main_timer_running = false;
static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* timer_thread_func(void *arg) {
    HibyStub *sys = (HibyStub*)arg;
    while (main_timer_running) {
        usleep(5000); // 5ms precision
        uint32_t now = sys->getTimeStamp();
        
        pthread_mutex_lock(&timer_mutex);
        for (int i=0; i<10; i++) {
            if (timers[i].active && now >= timers[i].next_tick) {
                System::TimerCallback cb = timers[i].callback;
                void *param = timers[i].param;
                uint32_t delay = timers[i].delay;
                
                // Unlock to prevent deadlocks during callback
                pthread_mutex_unlock(&timer_mutex);
                uint32_t new_delay = cb(delay, param);
                pthread_mutex_lock(&timer_mutex);
                
                // Check if it's still active (wasn't removed during callback)
                if (timers[i].active) {
                    if (new_delay == 0) {
                        timers[i].active = false;
                    } else {
                        timers[i].delay = new_delay;
                        timers[i].next_tick = now + new_delay;
                    }
                }
            }
        }
        pthread_mutex_unlock(&timer_mutex);
    }
    return nullptr;
}

int HibyStub::addTimer(uint32_t delay, TimerCallback callback, void *param) {
    pthread_mutex_lock(&timer_mutex);
    if (!main_timer_running) {
        main_timer_running = true;
        pthread_create(&main_timer_thread, nullptr, timer_thread_func, this);
    }
    int slot = -1;
    for (int i=0; i<10; i++) {
        if (!timers[i].active) {
            timers[i].delay = delay;
            timers[i].callback = callback;
            timers[i].param = param;
            timers[i].next_tick = getTimeStamp() + delay;
            timers[i].active = true;
            slot = i + 1;
            break;
        }
    }
    pthread_mutex_unlock(&timer_mutex);
    return slot;
}

void HibyStub::removeTimer(int timerId) {
    if (timerId > 0 && timerId <= 10) {
        pthread_mutex_lock(&timer_mutex);
        timers[timerId-1].active = false;
        pthread_mutex_unlock(&timer_mutex);
    }
}

void *HibyStub::createMutex() {
    pthread_mutex_t *m = new pthread_mutex_t;
    pthread_mutex_init(m, nullptr);
    return m;
}

void HibyStub::destroyMutex(void *mutex) {
    pthread_mutex_t *m = (pthread_mutex_t *)mutex;
    pthread_mutex_destroy(m);
    delete m;
}

void HibyStub::lockMutex(void *mutex) {
    pthread_mutex_lock((pthread_mutex_t *)mutex);
}

void HibyStub::unlockMutex(void *mutex) {
    pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

HibyStub sysImplementation;
System *stub = &sysImplementation;
