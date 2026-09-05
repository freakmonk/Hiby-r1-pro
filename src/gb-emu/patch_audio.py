import re

with open("src/platform.c", "r") as f:
    content = f.read()

# Replace include block
old_include = """#ifdef GB_USE_ALSA
#include <alsa/asoundlib.h>
#endif"""

new_include = """#include <dlfcn.h>

static void *g_alsa_lib = NULL;
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
static int (*x_snd_pcm_drain)(void *);
static const char *(*x_snd_strerror)(int);

#define SYM(h, ptr, sym, type) ptr = (type)dlsym(h, sym)

static int load_alsa(void) {
    if (g_alsa_lib) return 0;
    
    g_alsa_lib = dlopen("libasound.so", RTLD_LAZY);
    if (!g_alsa_lib) g_alsa_lib = dlopen("libasound.so.2", RTLD_LAZY);
    if (!g_alsa_lib) { 
        fprintf(stderr, "dlopen libasound failed: %s\\n", dlerror()); 
        return -1; 
    }

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
    SYM(g_alsa_lib, x_snd_pcm_drain, "snd_pcm_drain", int (*)(void *));
    SYM(g_alsa_lib, x_snd_strerror, "snd_strerror", const char *(*)(int));

    if (!x_snd_pcm_open || !x_snd_pcm_close || !x_snd_pcm_writei || !x_snd_pcm_hw_params_malloc) {
        fprintf(stderr, "Missing ALSA symbols!\\n");
        return -1;
    }
    return 0;
}"""

content = content.replace(old_include, new_include)

# Replace init block
old_init = """#ifdef GB_USE_ALSA
    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        err = snd_pcm_open(&pcm, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
    }
    if (err >= 0 && pcm) {
        err = snd_pcm_set_params(pcm,
            SND_PCM_FORMAT_S16_LE,
            SND_PCM_ACCESS_RW_INTERLEAVED,
            1,                     /* mono */
            GB_APU_SAMPLE_RATE,
            1,                     /* allow resampling */
            50000);                /* 50 ms latency */
        if (err < 0) {
            fprintf(stderr, "Warning: ALSA setup failed: %s\\n", snd_strerror(err));
            snd_pcm_close(pcm);
        } else {
            platform->audio_handle = pcm;
        }
    } else {
        fprintf(stderr, "Warning: ALSA init failed: %s\\n", snd_strerror(err));
    }
#else
    fprintf(stderr, "Audio disabled (no ALSA)\\n");
#endif"""

new_init = """    if (load_alsa() == 0) {
        void *pcm = NULL;
        int err = x_snd_pcm_open(&pcm, "plughw:0,0", 0, 0); /* SND_PCM_STREAM_PLAYBACK */
        if (err < 0) {
            err = x_snd_pcm_open(&pcm, "hw:0,0", 0, 0);
        }
        if (err < 0) {
            err = x_snd_pcm_open(&pcm, "default", 0, 0);
        }
        if (err >= 0 && pcm) {
            void *hw;
            x_snd_pcm_hw_params_malloc(&hw);
            x_snd_pcm_hw_params_any(pcm, hw);
            x_snd_pcm_hw_params_set_access(pcm, hw, 3); /* SND_PCM_ACCESS_RW_INTERLEAVED */
            x_snd_pcm_hw_params_set_format(pcm, hw, 2); /* SND_PCM_FORMAT_S16_LE */
            x_snd_pcm_hw_params_set_channels(pcm, hw, 1);
            
            unsigned int want_rate = GB_APU_SAMPLE_RATE;
            int dir = 0;
            x_snd_pcm_hw_params_set_rate_near(pcm, hw, &want_rate, &dir);
            
            unsigned int btime = 50000;
            int bdir = 0;
            x_snd_pcm_hw_params_set_buffer_time_near(pcm, hw, &btime, &bdir);
            
            unsigned int ptime = 10000;
            int pdir = 0;
            x_snd_pcm_hw_params_set_period_time_near(pcm, hw, &ptime, &pdir);
            
            if (x_snd_pcm_hw_params(pcm, hw) < 0) {
                fprintf(stderr, "Warning: ALSA hw_params failed\\n");
                x_snd_pcm_hw_params_free(hw);
                x_snd_pcm_close(pcm);
            } else {
                x_snd_pcm_hw_params_free(hw);
                x_snd_pcm_prepare(pcm);
                platform->audio_handle = pcm;
                fprintf(stderr, "ALSA audio initialized\\n");
            }
        } else {
            fprintf(stderr, "Warning: ALSA init failed: %s\\n", x_snd_strerror ? x_snd_strerror(err) : "Unknown error");
        }
    } else {
        fprintf(stderr, "Audio disabled (no libasound.so found)\\n");
    }"""

content = content.replace(old_init, new_init)

# Replace destroy block
old_destroy = """    if (platform->audio_handle) {
#ifdef GB_USE_ALSA
        snd_pcm_drain((snd_pcm_t *)platform->audio_handle);
        snd_pcm_close((snd_pcm_t *)platform->audio_handle);
#endif
        platform->audio_handle = NULL;
    }"""

new_destroy = """    if (platform->audio_handle) {
        if (x_snd_pcm_drain) x_snd_pcm_drain(platform->audio_handle);
        if (x_snd_pcm_close) x_snd_pcm_close(platform->audio_handle);
        platform->audio_handle = NULL;
    }"""

content = content.replace(old_destroy, new_destroy)

# Replace update block
old_update = """void gb_platform_update_audio(gb_platform_t *platform, gb_apu_t *apu) {
#ifdef GB_USE_ALSA
    if (!platform->audio_handle || !platform->audio_buffer) {
        /* Keep the queue from filling up when playback is unavailable. */
        s16 discard[256];
        while (gb_apu_read_samples(apu, discard, 256) > 0) { }
        return;
    }

    snd_pcm_t *pcm = (snd_pcm_t *)platform->audio_handle;
    int count;
    while ((count = gb_apu_read_samples(apu, platform->audio_buffer,
                                       platform->audio_buf_size)) > 0) {
        s16 *cursor = platform->audio_buffer;
        int remaining = count;
        while (remaining > 0) {
            snd_pcm_sframes_t frames = snd_pcm_writei(pcm, cursor, remaining);
            if (frames < 0) {
                if (snd_pcm_recover(pcm, (int)frames, 1) < 0) return;
                continue;
            }
            cursor += frames;
            remaining -= (int)frames;
        }
        if (count < platform->audio_buf_size) break;
    }
#else
    (void)platform;
    /* No output device: drain the queue so the APU never stalls on a full buffer. */
    s16 discard[256];
    while (gb_apu_read_samples(apu, discard, 256) > 0) { }
#endif
}"""

new_update = """void gb_platform_update_audio(gb_platform_t *platform, gb_apu_t *apu) {
    if (!platform->audio_handle || !platform->audio_buffer) {
        /* Keep the queue from filling up when playback is unavailable. */
        s16 discard[256];
        while (gb_apu_read_samples(apu, discard, 256) > 0) { }
        return;
    }

    void *pcm = platform->audio_handle;
    int count;
    while ((count = gb_apu_read_samples(apu, platform->audio_buffer,
                                       platform->audio_buf_size)) > 0) {
        s16 *cursor = platform->audio_buffer;
        int remaining = count;
        while (remaining > 0) {
            long frames = x_snd_pcm_writei(pcm, cursor, remaining);
            if (frames < 0) {
                if (x_snd_pcm_recover && x_snd_pcm_recover(pcm, (int)frames, 1) < 0) return;
                continue;
            }
            cursor += frames;
            remaining -= (int)frames;
        }
        if (count < platform->audio_buf_size) break;
    }
}"""

content = content.replace(old_update, new_update)

with open("src/platform.c", "w") as f:
    f.write(content)
