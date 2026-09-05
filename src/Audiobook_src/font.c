static int utf8_decode(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    if (!*p) return 0;
    int c = *p;
    if (c < 0x80) { (*s)++; return c; }
    if ((c & 0xE0) == 0xC0) {
        if (!p[1]) { (*s)++; return c; }
        (*s) += 2;
        return ((c & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        if (!p[1] || !p[2]) { (*s)++; return c; }
        (*s) += 3;
        return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
        if (!p[1] || !p[2] || !p[3]) { (*s)++; return c; }
        (*s) += 4;
        return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
    (*s)++;
    return c;
}

/* font.c — truetype font renderer for the audiobook UI.
 *
 * Loads the system font /usr/resource/fonts/msyh.ttf at runtime (not embedded),
 * rasterizes ASCII glyphs into a cache, and alpha-blends them onto an RGB565
 * framebuffer. Public API in font.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>      /* mmap the TTF read-only instead of an eager heap copy */

#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"

#include "font.h"
#include "render.h"   /* RENDER_FB_W/H, RENDER_STRIDE geometry */



#define N_SIZES   4
#define N_GLYPHS  1500
#define FIRST_CH  32

typedef struct {
    unsigned char *bmp;  /* alpha bitmap, w*h bytes; NULL if glyph is empty */
    int w, h;
    int xoff, yoff;       /* offset from pen (x, baseline+yoff = bmp top) */
    int advance;          /* x advance in pixels */
    int cached;           /* 1 once rasterized (so empty glyphs aren't re-rasterized) */
} glyph_t;

static stbtt_fontinfo g_info;
static unsigned char *g_ttf_data = NULL;
static size_t g_ttf_map_len = 0;   /* mmap length, for munmap on a future shutdown */
static int g_ok = 0;

static glyph_t g_cache[N_SIZES][N_GLYPHS];
static const int g_px[N_SIZES] = { FONT_PX_1, FONT_PX_2, FONT_PX_3, FONT_PX_4 };

static int size_index(int px) {
    for (int i = 0; i < N_SIZES; i++) if (g_px[i] == px) return i;
    return -1;
}

#include "roboto_ttf.h"

int font_init(void) {
    g_ttf_data = (unsigned char *)roboto_ttf;
    g_ttf_map_len = roboto_ttf_len;
    
    if (!stbtt_InitFont(&g_info, g_ttf_data,
                       stbtt_GetFontOffsetForIndex(g_ttf_data, 0))) {
        g_ttf_data = NULL; g_ttf_map_len = 0; return 0;
    }
    g_ok = 1;
    return 1;
}

int font_available(void) { return g_ok; }

int font_px_for_scale(int scale) {
    if (scale < 1 || scale > N_SIZES) return FONT_PX_1;
    return g_px[scale - 1];
}

/* Rasterize a glyph into the cache if not already present. Returns pointer or NULL. */
static glyph_t *get_glyph(int si, int c) {
    if (!g_ok) return NULL;
    
    int gi = 0;
    if (c >= 32 && c <= 126) gi = c - 32;
    else if (c >= 0x400 && c <= 0x4FF) gi = 95 + (c - 0x400);
    else if (c >= 0x40 && c <= 0xFF) gi = 95 + 256 + (c - 0x40);
    else gi = 63 - 32; // '?'
    
    if (gi < 0 || gi >= N_GLYPHS) gi = 63 - 32;
    
    glyph_t *g = &g_cache[si][gi];
    if (g->cached) return g;

    int px = g_px[si];
    float scale = stbtt_ScaleForPixelHeight(&g_info, (float)px);
    int xoff, yoff;
    int codepoint = c;
    /* stbtt_GetCodepointBitmap returns alpha bitmap; yoff is from baseline.
     * For empty glyphs (e.g. space) it returns NULL with w=h=0 — that's fine,
     * we still mark cached so we don't re-rasterize every frame. */
    g->bmp = stbtt_GetCodepointBitmap(&g_info, scale, scale, codepoint,
                                      &g->w, &g->h, &xoff, &yoff);
    g->xoff = xoff;
    g->yoff = yoff;
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&g_info, codepoint, &advance, &lsb);
    g->advance = (int)(advance * scale);
    if (g->advance <= 0) g->advance = (g->w + g->xoff > 0) ? (g->w + g->xoff) : 1;
    g->cached = 1;
    return g;
}

static inline void blend(uint16_t *fb, int x, int y, uint16_t color, int a) {
    if (a <= 0) return;
    if (x < 0 || x >= RENDER_FB_W || y < 0 || y >= RENDER_FB_H) return;
    int stride_px = RENDER_STRIDE / 2;
    uint16_t dst = fb[y * stride_px + x];
    int dr = (dst >> 11) & 0x1f, dg = (dst >> 5) & 0x3f, db = dst & 0x1f;
    int cr = (color >> 11) & 0x1f, cg = (color >> 5) & 0x3f, cb = color & 0x1f;
    int ia = 255 - a;
    int r = (cr * a + dr * ia) / 255;
    int g = (cg * a + dg * ia) / 255;
    int b = (cb * a + db * ia) / 255;
    fb[y * stride_px + x] = (uint16_t)((r << 11) | (g << 5) | b);
}

int font_draw_char(uint16_t *fb, int x, int y, int c, int px, uint16_t color) {
    if (!g_ok) return x;
    int si = size_index(px);
    if (si < 0) si = 0;
    glyph_t *g = get_glyph(si, c);
    if (!g || !g->bmp) return x + (g ? g->advance : 0);

    int asc = font_ascent(px);
    int base_y = y + asc;        /* baseline */
    int draw_x = x + g->xoff;
    int draw_y = base_y + g->yoff;
    for (int j = 0; j < g->h; j++) {
        for (int i = 0; i < g->w; i++) {
            int a = g->bmp[j * g->w + i];
            blend(fb, draw_x + i, draw_y + j, color, a);
        }
    }
    return x + g->advance;
}

int font_draw_text(uint16_t *fb, int x, int y, const char *s, int px,
                   uint16_t color) {
    if (!s) return x;
    int si = size_index(px);
    if (si < 0) si = 0;
    int cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += font_line_height(px); s++; continue; }
        int c = utf8_decode(&s);
        cx = font_draw_char(fb, cx, y, c, px, color);
    }
    return cx;
}

int font_text_width(const char *s, int px) {
    if (!s) return 0;
    int si = size_index(px);
    if (si < 0) si = 0;
    int w = 0;
    while (*s) {
        if (*s == '\n') break;
        int c = utf8_decode(&s);
        glyph_t *g = get_glyph(si, c);
        if (g) w += g->advance;
    }
    return w;
}

int font_ascent(int px) {
    if (!g_ok) return px;
    int si = size_index(px);
    if (si < 0) si = 0;
    float scale = stbtt_ScaleForPixelHeight(&g_info, (float)g_px[si]);
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&g_info, &ascent, &descent, &linegap);
    return (int)(ascent * scale + 0.5f);
}

int font_line_height(int px) {
    if (!g_ok) return px;
    int si = size_index(px);
    if (si < 0) si = 0;
    float scale = stbtt_ScaleForPixelHeight(&g_info, (float)g_px[si]);
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&g_info, &ascent, &descent, &linegap);
    return (int)((ascent + descent) * scale + 0.5f);
}