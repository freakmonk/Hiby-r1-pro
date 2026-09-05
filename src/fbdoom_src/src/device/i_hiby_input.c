#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#define MAX_INPUT_DEVS 20
static int input_fds[MAX_INPUT_DEVS];
static int input_count = 0;

/* Exposed to i_hiby_video.c for live debug overlay */
int debug_touch_active = 0;
int debug_touch_x = -1;
int debug_touch_y = -1;
int debug_last_code = 0;
int debug_last_val = 0;
int debug_dev_count = 0;

/* Doom global key state – written directly in addition to D_PostEvent */
extern boolean gamekeydown[256];
/* Doom menu active flag – used for context-sensitive key dispatch */
extern boolean menuactive;

/* Internal button indices – prefixed IDX_ to avoid collision with linux/input.h BTN_* macros */
enum {
    IDX_UP,
    IDX_DOWN,
    IDX_LEFT,
    IDX_RIGHT,
    IDX_FIRE,
    IDX_USE,
    IDX_RUN,
    IDX_WEAPON,
    IDX_ESCAPE,
    IDX_TAB,
    IDX_Y,
    IDX_N,
    IDX_COUNT
};

/* Doom keycodes posted for each button */
static const int btn_doom_key[IDX_COUNT] = {
    [IDX_UP]     = KEY_UPARROW,
    [IDX_DOWN]   = KEY_DOWNARROW,
    [IDX_LEFT]   = KEY_LEFTARROW,
    [IDX_RIGHT]  = KEY_RIGHTARROW,
    [IDX_FIRE]   = KEY_RCTRL,
    [IDX_USE]    = ' ',
    [IDX_RUN]    = KEY_RSHIFT,
    [IDX_WEAPON] = '2',
    [IDX_ESCAPE] = KEY_ESCAPE,
    [IDX_TAB]    = KEY_TAB,
    [IDX_Y]      = 'y',
    [IDX_N]      = 'n'
};

static bool hw_held[IDX_COUNT];
static bool touch_held[IDX_COUNT];
static bool prev_merged[IDX_COUNT];

static int  touch_x = -1, touch_y = -1;
static bool touch_active = false;

/* ------------------------------------------------------------------ */
void I_InitInput(void)
{
    memset(hw_held,      0, sizeof hw_held);
    memset(touch_held,   0, sizeof touch_held);
    memset(prev_merged,  0, sizeof prev_merged);

    /* Redirect stdout to SD card log file so all printf debug output is readable */
    const char *sd_log = "/data/mnt/sd_0/doom/doom.log";
    FILE *lf = fopen(sd_log, "a");
    if (lf) {
        /* Unbuffered so every line is flushed immediately */
        setvbuf(lf, NULL, _IOLBF, 0);
        dup2(fileno(lf), STDOUT_FILENO);
        dup2(fileno(lf), STDERR_FILENO);
        fclose(lf);
    }

    for (int i = 0; i < MAX_INPUT_DEVS; i++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0)
            input_fds[input_count++] = fd;
    }
    debug_dev_count = input_count;
    printf("[doom-input] opened %d /dev/input/event* nodes\n", input_count);
}

/* ------------------------------------------------------------------ */
static void post_doom_key(int doom_key, int is_down)
{
    if (doom_key <= 0 || doom_key >= 256) return;
    event_t ev;
    ev.type  = is_down ? ev_keydown : ev_keyup;
    ev.data1 = doom_key;
    ev.data2 = 0;
    ev.data3 = 0;
    D_PostEvent(&ev);
    /* Also stamp the array directly so G_BuildTiccmd never misses it */
    gamekeydown[doom_key] = is_down ? true : false;
}

/* ------------------------------------------------------------------ */
/* Touch hit-test – must match the rectangles drawn in i_hiby_video.c */
static void update_touch_held(void)
{
    memset(touch_held, 0, sizeof touch_held);
    if (!touch_active || touch_x < 0 || touch_y < 0) return;

    int x = touch_x, y = touch_y;
    if (y < 350) return;

    if (y < 430) {
        /* Top bar: ESC | TAB | FIRE | USE */
        if      (x < 120) touch_held[IDX_ESCAPE] = true;
        else if (x < 240) touch_held[IDX_TAB]    = true;
        else if (x < 360) touch_held[IDX_FIRE]   = true;
        else               touch_held[IDX_USE]    = true;
        return;
    }

    if (x < 240) {
        /* D-Pad */
        if      (y < 530) touch_held[IDX_UP]    = true;
        else if (y >= 630) touch_held[IDX_DOWN]  = true;
        if      (x < 90)  touch_held[IDX_LEFT]  = true;
        else if (x >= 150) touch_held[IDX_RIGHT] = true;
        return;
    }

    /* Action buttons (right side) */
    if (x < 360 && y < 560)  touch_held[IDX_WEAPON] = true;
    if (x < 360 && y >= 560 && y < 670) touch_held[IDX_FIRE]   = true;
    if (x >= 360 && y < 570) touch_held[IDX_USE]    = true;
    if (x >= 360 && y >= 570 && y < 670) touch_held[IDX_RUN]   = true;

    /* Y and N buttons under the action buttons */
    if (y >= 670 && y < 750) {
        if (x >= 250 && x < 360) touch_held[IDX_Y] = true;
        if (x >= 360) touch_held[IDX_N] = true;
    }
}

/* ------------------------------------------------------------------ */
void I_StartTic(void)
{
    struct input_event ev;
    bool touch_changed = false;

    /* ── read all queued kernel events ─────────────────────────────── */
    for (int i = 0; i < input_count; i++) {
        int fd = input_fds[i];
        if (fd < 0) continue;

        while (read(fd, &ev, sizeof ev) == sizeof ev) {

            /* ── touch / absolute axes ───────────────────────── */
            if (ev.type == EV_ABS) {
                switch (ev.code) {
                    case ABS_X:
                    case ABS_MT_POSITION_X:
                        touch_x = ev.value;
                        debug_touch_x = ev.value;
                        touch_changed = true;
                        break;
                    case ABS_Y:
                    case ABS_MT_POSITION_Y:
                        touch_y = ev.value;
                        debug_touch_y = ev.value;
                        touch_changed = true;
                        break;
                    case ABS_MT_TRACKING_ID:
                        touch_active = (ev.value >= 0);
                        debug_touch_active = touch_active;
                        touch_changed = true;
                        break;
                    default: break;
                }
                continue;
            }

            /* ── keyboard / buttons ──────────────────────────── */
            if (ev.type != EV_KEY) continue;

            int pressed = (ev.value != 0);  /* 0=up 1=down 2=repeat */

            /* finger-up / finger-down from the panel */
            if (ev.code == BTN_TOUCH) {
                touch_active  = pressed;
                debug_touch_active = pressed;
                touch_changed = true;
                continue;
            }

            /* ── hardware key table (exact HiBy R1 scan codes) ── */
            debug_last_code = ev.code;
            debug_last_val = ev.value;

            extern int global_audio_volume;

            switch (ev.code) {
                case KEY_VOLUMEUP:   
                    if (pressed) {
                        global_audio_volume += 10;
                        if (global_audio_volume > 100) global_audio_volume = 100;
                    }
                    break;
                case KEY_VOLUMEDOWN:
                    if (pressed) {
                        global_audio_volume -= 10;
                        if (global_audio_volume < 0) global_audio_volume = 0;
                    }
                    break;
                case KEY_PLAYPAUSE:  hw_held[IDX_FIRE]   = pressed; break;
                case KEY_NEXTSONG:   hw_held[IDX_USE]    = pressed; break;
                case KEY_POWER:      hw_held[IDX_ESCAPE] = pressed; break;
                /* USB keyboard aliases kept for desk testing */
                case KEY_UP:         hw_held[IDX_UP]     = pressed; break;
                case KEY_DOWN:       hw_held[IDX_DOWN]   = pressed; break;
                case KEY_LEFT:       hw_held[IDX_LEFT]   = pressed; break;
                case KEY_RIGHT:      hw_held[IDX_RIGHT]  = pressed; break;
                case KEY_RIGHTCTRL:  
                case KEY_LEFTCTRL:   hw_held[IDX_FIRE]   = pressed; break;
                case KEY_SPACE:      hw_held[IDX_USE]    = pressed; break;
                case KEY_RIGHTSHIFT:
                case KEY_LEFTSHIFT:  hw_held[IDX_RUN]    = pressed; break;
                case 2: // KEY_1 on some boards?
                case KEY_2:          hw_held[IDX_WEAPON] = pressed; break;
                case KEY_ESC:        hw_held[IDX_ESCAPE] = pressed; break;
                case KEY_TAB:        hw_held[IDX_TAB]    = pressed; break;
                default: break;
            }
        }
    }

    if (touch_changed) update_touch_held();

    /* ── merge and dispatch ─────────────────────────────────────────── */
    for (int b = 0; b < IDX_COUNT; b++) {
        bool merged = hw_held[b] || touch_held[b];

        if (merged == prev_merged[b]) {
            prev_merged[b] = merged;
            continue;
        }

        int is_down = merged ? 1 : 0;

        /*
         * Context-aware dispatch:
         * In menu (menuactive=true) : FIRE → ENTER, USE → DOWN
         * In game                   : FIRE → CTRL,  USE → SPACE
         *
         * Vol+/Vol- always = UP/DOWN (works for both menu navigation
         * and in-game movement simultaneously).
         */
        switch (b) {
            case IDX_UP:
                post_doom_key(KEY_UPARROW, is_down);
                gamekeydown[KEY_UPARROW] = is_down;
                printf("[doom-input] UP → %d\n", is_down);
                break;

            case IDX_DOWN:
                post_doom_key(KEY_DOWNARROW, is_down);
                gamekeydown[KEY_DOWNARROW] = is_down;
                printf("[doom-input] DOWN → %d\n", is_down);
                break;

            case IDX_LEFT:
                post_doom_key(KEY_LEFTARROW, is_down);
                gamekeydown[KEY_LEFTARROW] = is_down;
                break;

            case IDX_RIGHT:
                post_doom_key(KEY_RIGHTARROW, is_down);
                gamekeydown[KEY_RIGHTARROW] = is_down;
                break;

            case IDX_FIRE:
                /* In menus: ENTER selects the highlighted item.
                   In game:  CTRL  fires the weapon. */
                if (menuactive) {
                    /* DOOM's KEY_ENTER is 13. linux/input.h overrides KEY_ENTER to 28!
                       We must send 13 to M_Responder. */
                    post_doom_key(13, is_down);
                    gamekeydown[13] = is_down;
                    printf("[doom-input] FIRE→ENTER(menu) → %d\n", is_down);
                } else {
                    post_doom_key(KEY_RCTRL, is_down);
                    gamekeydown[KEY_RCTRL] = is_down;
                    printf("[doom-input] FIRE→CTRL(game) → %d\n", is_down);
                }
                break;

            case IDX_USE:
                /* In menus: DOWN navigates.
                   In game:  SPACE opens doors. */
                if (menuactive) {
                    post_doom_key(KEY_DOWNARROW, is_down);
                    gamekeydown[KEY_DOWNARROW] = is_down;
                    printf("[doom-input] USE→DOWN(menu) → %d\n", is_down);
                } else {
                    post_doom_key(' ', is_down);
                    gamekeydown[' '] = is_down;
                    printf("[doom-input] USE→SPACE(game) → %d\n", is_down);
                }
                break;

            case IDX_RUN:
                post_doom_key(KEY_RSHIFT, is_down);
                gamekeydown[KEY_RSHIFT] = is_down;
                break;

            case IDX_WEAPON:
                post_doom_key('2', is_down);
                gamekeydown['2'] = is_down;
                break;

            case IDX_ESCAPE:
                post_doom_key(KEY_ESCAPE, is_down);
                gamekeydown[KEY_ESCAPE] = is_down;
                printf("[doom-input] ESC → %d\n", is_down);
                break;

            case IDX_TAB:
                post_doom_key(KEY_TAB, is_down);
                gamekeydown[KEY_TAB] = is_down;
                break;

            case IDX_Y:
                post_doom_key('y', is_down);
                gamekeydown['y'] = is_down;
                break;

            case IDX_N:
                post_doom_key('n', is_down);
                gamekeydown['n'] = is_down;
                break;

            default: break;
        }

        prev_merged[b] = merged;
    }
}
