#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include "i_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"

#define NUM_CHANNELS 16
#define SAMPLE_RATE 11025
#define BUFFER_SAMPLES 512

/* ALSA dynamic loading */
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
    x_snd_pcm_hw_params_set_access(g_pcm, hw, 3); // SND_PCM_ACCESS_RW_INTERLEAVED
    x_snd_pcm_hw_params_set_format(g_pcm, hw, 2); // SND_PCM_FORMAT_S16_LE
    x_snd_pcm_hw_params_set_channels(g_pcm, hw, channels);
    
    unsigned int want_rate = rate;
    int dir = 0;
    x_snd_pcm_hw_params_set_rate_near(g_pcm, hw, &want_rate, &dir);
    
    unsigned int btime = 500000, bdir = 0;
    x_snd_pcm_hw_params_set_buffer_time_near(g_pcm, hw, &btime, (int *)&bdir);
    unsigned int ptime = 25000, pdir = 0; // Much shorter period for gaming
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
    printf("ALSA initialized for Doom: %d Hz, %d ch\n", want_rate, channels);
    return 0;
}

typedef struct {
    int active;
    unsigned char *data;
    int length;
    int pos;
    int volume; // 0-127
    int sep;    // 0-254 (127=center)
} channel_t;

static channel_t channels[NUM_CHANNELS];
static pthread_mutex_t sound_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t sound_thread;
static volatile int sound_running = 0;

int global_audio_volume = 20; // Default 20%

static void *sound_loop(void *arg) {
    short mix_buffer[BUFFER_SAMPLES * 2]; // Stereo S16_LE
    int mix_temp[BUFFER_SAMPLES * 2];

    while (sound_running) {
        memset(mix_temp, 0, sizeof(mix_temp));
        
        pthread_mutex_lock(&sound_mutex);
        for (int c = 0; c < NUM_CHANNELS; c++) {
            if (channels[c].active) {
                int samples_to_mix = BUFFER_SAMPLES;
                if (channels[c].pos + samples_to_mix > channels[c].length) {
                    samples_to_mix = channels[c].length - channels[c].pos;
                }
                
                // Doom separation: 0 = completely left, 254 = completely right, 127 = center
                int vol_l = (channels[c].volume * (254 - channels[c].sep)) / 254;
                int vol_r = (channels[c].volume * channels[c].sep) / 254;
                
                for (int i = 0; i < samples_to_mix; i++) {
                    // Doom samples are 8-bit unsigned (0-255, center 128)
                    int sample = channels[c].data[channels[c].pos + i];
                    sample = (sample - 128) << 8; // Convert to 16-bit signed
                    
                    mix_temp[i * 2]     += (sample * vol_l) / 127;
                    mix_temp[i * 2 + 1] += (sample * vol_r) / 127;
                }
                
                channels[c].pos += samples_to_mix;
                if (channels[c].pos >= channels[c].length) {
                    channels[c].active = 0;
                }
            }
        }
        pthread_mutex_unlock(&sound_mutex);
        
        // Clamp and copy to short buffer (dynamic volume)
        for (int i = 0; i < BUFFER_SAMPLES * 2; i++) {
            int val = (mix_temp[i] * global_audio_volume) / 100;
            if (val < -32768) val = -32768;
            if (val > 32767) val = 32767;
            mix_buffer[i] = (short)val;
        }
        
        if (g_pcm) {
            long err = x_snd_pcm_writei(g_pcm, mix_buffer, BUFFER_SAMPLES);
            if (err < 0) {
                x_snd_pcm_recover(g_pcm, err, 0);
            }
        } else {
            usleep(10000); // Backoff if ALSA failed
        }
    }
    return NULL;
}

void I_InitSound() {
    memset(channels, 0, sizeof(channels));
    if (open_alsa(SAMPLE_RATE, 2) == 0) {
        sound_running = 1;
        pthread_create(&sound_thread, NULL, sound_loop, NULL);
    }
}

void I_UpdateSound(void) {}
void I_SubmitSound(void) {}

void I_ShutdownSound(void) {
    if (sound_running) {
        sound_running = 0;
        pthread_join(sound_thread, NULL);
    }
    if (g_pcm) {
        x_snd_pcm_close(g_pcm);
        g_pcm = NULL;
    }
    if (g_alsa_lib) {
        dlclose(g_alsa_lib);
        g_alsa_lib = NULL;
    }
}

void I_SetChannels() {}

int I_GetSfxLumpNum(sfxinfo_t* sfxinfo) {
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfxinfo->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority) {
    if (!sound_running) return -1;
    
    sfxinfo_t* sfx = &S_sfx[id];
    int lump = I_GetSfxLumpNum(sfx);
    
    // Cache the lump
    unsigned char* data = W_CacheLumpNum(lump, PU_STATIC);
    
    // Doom sound format: 
    // uint16 format (usually 3)
    // uint16 samplerate
    // uint32 length
    // followed by raw 8-bit unsigned PCM
    
    // Read header
    unsigned short format = (data[1] << 8) | data[0];
    if (format != 3) {
        // Not a standard doom sound lump, abort
        return -1;
    }
    
    unsigned int length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
    unsigned char *pcm_data = data + 8;
    
    pthread_mutex_lock(&sound_mutex);
    
    // Find free channel
    int c = -1;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!channels[i].active) {
            c = i;
            break;
        }
    }
    
    if (c != -1) {
        channels[c].data = pcm_data;
        channels[c].length = length;
        channels[c].pos = 0;
        channels[c].volume = vol;
        channels[c].sep = sep;
        channels[c].active = 1;
    }
    
    pthread_mutex_unlock(&sound_mutex);
    return c;
}

void I_StopSound(int handle) {
    if (handle >= 0 && handle < NUM_CHANNELS) {
        pthread_mutex_lock(&sound_mutex);
        channels[handle].active = 0;
        pthread_mutex_unlock(&sound_mutex);
    }
}

int I_SoundIsPlaying(int handle) {
    if (handle >= 0 && handle < NUM_CHANNELS) {
        return channels[handle].active;
    }
    return 0;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) {
    if (handle >= 0 && handle < NUM_CHANNELS) {
        pthread_mutex_lock(&sound_mutex);
        channels[handle].volume = vol;
        channels[handle].sep = sep;
        pthread_mutex_unlock(&sound_mutex);
    }
}

