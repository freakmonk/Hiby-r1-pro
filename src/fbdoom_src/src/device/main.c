#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include "doomdef.h"
#include "m_argv.h"
#include "d_main.h"

extern void I_InitInput(void);
extern void I_StartTic(void);
extern void I_InitGraphics(void);
extern void draw_rect(int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b);
extern void draw_str(int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b);

extern int debug_touch_active;
extern int debug_touch_x;
extern int debug_touch_y;

#define MAX_WADS 10

static char* new_argv[32];

void select_wad_menu(void) {
    DIR *dir;
    struct dirent *ent;
    char wad_files[MAX_WADS][256];
    int wad_count = 0;

    dir = opendir(".");
    if (dir != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            int len = strlen(ent->d_name);
            if (len > 4 && strcasecmp(ent->d_name + len - 4, ".wad") == 0) {
                if (wad_count < MAX_WADS) {
                    strncpy(wad_files[wad_count], ent->d_name, 255);
                    wad_files[wad_count][255] = '\0';
                    wad_count++;
                }
            }
        }
        closedir(dir);
    }

    if (wad_count == 0) return; // Let Doom handle missing WADs

    int selected_idx = -1;

    if (wad_count == 1) {
        selected_idx = 0;
    } else {
        I_InitGraphics();
        
        // Clear screen
        draw_rect(0, 0, 480, 800, 0, 0, 0);
        draw_str(100, 50, "SELECT WAD TO PLAY", 255, 255, 255);

        int button_h = 60;
        int button_w = 400;
        int start_y = 120;
        int gap = 20;

        for (int i = 0; i < wad_count; i++) {
            draw_rect(40, start_y + i * (button_h + gap), button_w, button_h, 50, 50, 150);
            draw_str(60, start_y + i * (button_h + gap) + 20, wad_files[i], 255, 255, 255);
        }

        while (selected_idx == -1) {
            I_StartTic(); // Poll inputs

            if (debug_touch_active && debug_touch_y > start_y) {
                int rel_y = debug_touch_y - start_y;
                int idx = rel_y / (button_h + gap);
                int mod = rel_y % (button_h + gap);
                
                if (idx >= 0 && idx < wad_count && mod <= button_h) {
                    selected_idx = idx;
                    
                    // Highlight button
                    draw_rect(40, start_y + idx * (button_h + gap), button_w, button_h, 150, 50, 50);
                    draw_str(60, start_y + idx * (button_h + gap) + 20, wad_files[idx], 255, 255, 255);
                    usleep(200000); // 200ms visual feedback
                }
            }
            usleep(16000); // ~60 FPS
        }
    }

    // Inject selected WAD into myargv
    for (int i = 0; i < myargc; i++) {
        new_argv[i] = (char*)myargv[i];
    }
    
    new_argv[myargc] = "-iwad";
    new_argv[myargc + 1] = strdup(wad_files[selected_idx]);
    
    myargv = (const char**)new_argv;
    myargc += 2;
    
    // Clear screen before continuing
    draw_rect(0, 0, 480, 800, 0, 0, 0);
}

int main(int argc, const char** argv)
{
    myargc = argc;
    myargv = argv;

    printf("Starting Doom on HiBy R1...\n");
    I_InitInput();

    select_wad_menu();

    D_DoomMain();

    return 0;
}