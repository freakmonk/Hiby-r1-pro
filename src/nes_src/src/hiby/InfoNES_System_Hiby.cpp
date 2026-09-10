
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include "../InfoNES.h"
#include "../InfoNES_System.h"
#include "../InfoNES_pAPU.h"
#include "platform.h"
#include "font.h"

static gb_platform_t platform;
static int g_running = 1;

// Base palette (RGB555 from Linux port)
static WORD base_palette[ 64 ] =
{
  0x39ce, 0x1071, 0x0015, 0x2013, 0x440e, 0x5402, 0x5000, 0x3c20,
  0x20a0, 0x0100, 0x0140, 0x00e2, 0x0ceb, 0x0000, 0x0000, 0x0000,
  0x5ef7, 0x01dd, 0x10fd, 0x401e, 0x5c17, 0x700b, 0x6ca0, 0x6521,
  0x45c0, 0x0240, 0x02a0, 0x0247, 0x0211, 0x0000, 0x0000, 0x0000,
  0x7fff, 0x1eff, 0x2e5f, 0x223f, 0x79ff, 0x7dd6, 0x7dcc, 0x7e67,
  0x7ae7, 0x4342, 0x2769, 0x2ff3, 0x03bb, 0x0000, 0x0000, 0x0000,
  0x7fff, 0x579f, 0x635f, 0x6b3f, 0x7f1f, 0x7f1b, 0x7ef6, 0x7f75,
  0x7f94, 0x73f4, 0x57d7, 0x5bf9, 0x4ffe, 0x0000, 0x0000, 0x0000
};

WORD NesPalette[64];

static void init_palette() {
    for (int i=0; i<64; i++) {
        // Keep as RGB555 so InfoNES can safely use 0x8000 for its internal priority flag
        NesPalette[i] = base_palette[i]; 
    }
}


int InfoNES_ReadRom( const char *pszFileName ) {
    FILE *fp;
    fp = fopen( pszFileName, "rb" );
    if ( fp == NULL ) return -1;
    fread( &NesHeader, sizeof NesHeader, 1, fp );
    if ( memcmp( NesHeader.byID, "NES\x1a", 4 ) != 0 ) {
        fclose( fp );
        return -1;
    }
    memset( SRAM, 0, SRAM_SIZE );
    if ( NesHeader.byInfo1 & 4 ) {
        fread( &SRAM[ 0x1000 ], 512, 1, fp );
    }
    ROM = (BYTE *)malloc( NesHeader.byRomSize * 0x4000 );
    fread( ROM, 0x4000, NesHeader.byRomSize, fp );
    if ( NesHeader.byVRomSize > 0 ) {
        VROM = (BYTE *)malloc( NesHeader.byVRomSize * 0x2000 );
        fread( VROM, 0x2000, NesHeader.byVRomSize, fp );
    }
    fclose( fp );
    return 0;
}

void InfoNES_ReleaseRom() {
    if ( ROM ) { free( ROM ); ROM = NULL; }
    if ( VROM ) { free( VROM ); VROM = NULL; }
}

void InfoNES_LoadFrame() {
    if (!platform.fb_mem) return;
    static int frame_count = 0;
    gb_platform_wait_frame(&platform);

    if (++frame_count >= 60) {
        frame_count = 0;
        ioctl(platform.fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    }
    
    u8 *base = (u8 *)platform.fb_mem;
    
    int dst_w = platform.fb_width;
    int dst_h = NES_DISP_HEIGHT * dst_w / NES_DISP_WIDTH;
    
    if (dst_h > 496) {
        dst_h = 496;
        dst_w = NES_DISP_WIDTH * dst_h / NES_DISP_HEIGHT;
    }
    
    int off_x = (platform.fb_width - dst_w) / 2;
    int off_y = (496 - dst_h) / 2;
    
    if (platform.fb_bpp == 32) {
        for (int y = 0; y < dst_h; y++) {
            int src_y = y * NES_DISP_HEIGHT / dst_h;
            u8 *line = base + (size_t)(off_y + y) * platform.fb_stride;
            u32 *out = (u32 *)line + off_x;
            for (int x = 0; x < dst_w; x++) {
                int src_x = x * NES_DISP_WIDTH / dst_w;
                // Strip the 0x8000 priority flag used by InfoNES engine
                WORD c = WorkFrame[src_y * NES_DISP_WIDTH + src_x] & 0x7FFF;
                u8 r = (c >> 10) & 0x1F;
                u8 g = (c >> 5) & 0x1F;
                u8 b = (c & 0x1F);
                *out++ = 0xFF000000 | ((r << 3) << 16) | ((g << 3) << 8) | (b << 3);
            }
        }
    } else {
        for (int y = 0; y < dst_h; y++) {
            int src_y = y * NES_DISP_HEIGHT / dst_h;
            u8 *line = base + (size_t)(off_y + y) * platform.fb_stride;
            u16 *out = (u16 *)line + off_x;
            for (int x = 0; x < dst_w; x++) {
                int src_x = x * NES_DISP_WIDTH / dst_w;
                // Strip the 0x8000 priority flag used by InfoNES engine
                WORD c = WorkFrame[src_y * NES_DISP_WIDTH + src_x] & 0x7FFF;
                int r = (c >> 10) & 0x1F;
                int g = (c >> 5) & 0x1F;
                int b = (c & 0x1F);
                // Convert RGB555 to RGB565 for the framebuffer
                *out++ = (r << 11) | (((g << 1) | (g >> 4)) << 5) | b;
            }
        }
    }
}

void InfoNES_PadState( DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem ) {
    int quit = gb_platform_poll_input(&platform);
    if (quit) g_running = 0;
    
    DWORD p1 = 0;
    if (platform.button_right) p1 |= (1 << 7);
    if (platform.button_left) p1 |= (1 << 6);
    if (platform.button_down) p1 |= (1 << 5);
    if (platform.button_up) p1 |= (1 << 4);
    if (platform.button_start) p1 |= (1 << 3);
    if (platform.button_select) p1 |= (1 << 2);
    if (platform.button_a) p1 |= (1 << 1);
    if (platform.button_b) p1 |= (1 << 0);
    
    *pdwPad1 = p1;
    *pdwPad2 = 0;
    *pdwSystem = quit ? PAD_SYS_QUIT : 0;
}

void *InfoNES_MemoryCopy( void *dest, const void *src, int count ) {
    return memcpy( dest, src, count );
}

void *InfoNES_MemorySet( void *dest, int c, int count ) {
    return memset( dest, c, count );
}

void InfoNES_DebugPrint( char *pszMsg ) {
    fprintf(stderr, "%s\n", pszMsg);
}

void InfoNES_Wait() {
    static int wait_count = 0;

    
}

void InfoNES_SoundInit( void ) {
}

int InfoNES_SoundOpen( int samples_per_sync, int sample_rate ) {
    return 1;
}

void InfoNES_SoundClose( void ) {
}

// from platform.c for snd_pcm_writei
extern "C" long (*x_snd_pcm_writei)(void *, const void *, unsigned long);
extern "C" int (*x_snd_pcm_recover)(void *, int, int);

void InfoNES_SoundOutput(int samples, BYTE *wave1, BYTE *wave2, BYTE *wave3, BYTE *wave4, BYTE *wave5) {
    if (!platform.audio_handle || !platform.audio_buffer) return;
    
    for (int i=0; i<samples; i++) {
        int w = (wave1[i] + wave2[i] + wave3[i] + wave4[i] + wave5[i]) / 5;
        // 8-bit unsigned to 16-bit signed
        s16 val = (s16)((w - 128) * 256);
        // apply volume
        val = (s16)((val * current_volume) >> 10);
        
        platform.audio_buffer[platform.audio_pos++] = val;
        
        if (platform.audio_pos >= platform.audio_buf_size) {
            void *pcm = platform.audio_handle;
            s16 *cursor = platform.audio_buffer;
            int remaining = platform.audio_pos;
            while (remaining > 0) {
                if (!x_snd_pcm_writei) break;
                long frames = x_snd_pcm_writei(pcm, cursor, remaining);
                if (frames < 0) {
                    if (x_snd_pcm_recover && x_snd_pcm_recover(pcm, (int)frames, 1) < 0) break;
                    continue;
                }
                cursor += frames;
                remaining -= (int)frames;
            }
            platform.audio_pos = 0;
        }
    }
}

void InfoNES_MessageBox( char *pszMsg, ... ) {
    va_list args;
    va_start(args, pszMsg);
    vfprintf(stderr, pszMsg, args);
    va_end(args);
    fprintf(stderr, "\n");
}

#include "menu.h"

int InfoNES_Menu() {
    if (!g_running) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (gb_platform_init(&platform) != 0) {
        fprintf(stderr, "Failed to init platform\n");
        return 1;
    }
    
    init_palette();

    if (argc >= 2) {
        gb_platform_clear(&platform, 0xFF000000);
        gb_platform_draw_touch_overlay(&platform);
        if (InfoNES_Load(argv[1]) == 0) {
            InfoNES_Main();
        }
    } else {
        int cursor = 0;
        while (1) {
            g_running = 1; // Reset running state for the menu and emulator
            gb_menu_result_t choice = gb_menu_run(&platform, &cursor);
            if (choice.action == GB_MENU_QUIT || choice.action == GB_MENU_PLAYER || choice.action == GB_MENU_SHUTDOWN || choice.action == GB_MENU_FIRMWARE_UPDATE || choice.action == GB_MENU_FACTORY_RESET) {
                break;
            }
            if (choice.action == GB_MENU_ROM) {
                gb_platform_clear(&platform, 0xFF000000);
                gb_platform_draw_touch_overlay(&platform);
                
                if (InfoNES_Load(choice.rom_path) == 0) {
                    InfoNES_Main();
                }
            }
        }
    }
    
    gb_platform_destroy(&platform);
    return 0;
}
