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

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>

/* --- Input --- */
#define MAX_INPUT_DEVS 20
static int input_fds[MAX_INPUT_DEVS];
static int input_count = 0;
static volatile int quit = 0;
static volatile int paused = 0;
static volatile int seek_request = 0; /* +1 = fwd 1min, -1 = back 1min */

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

static int current_volume = 205; // 20% default (205/1024)

static void check_input(void) {
    struct input_event ev;
    for (int i = 0; i < input_count; i++) {
        while (read(input_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                if (ev.code == KEY_POWER || ev.code == KEY_ESC) {
                    printf("Exit button pressed\n");
                    quit = 1;
                    return;
                } else if (ev.code == KEY_VOLUMEUP) {
                    current_volume += 51; // +5%
                    if (current_volume > 1024) current_volume = 1024;
                    printf("Volume: %d%%\n", (current_volume * 100) / 1024);
                } else if (ev.code == KEY_VOLUMEDOWN) {
                    current_volume -= 51; // -5%
                    if (current_volume < 0) current_volume = 0;
                    printf("Volume: %d%%\n", (current_volume * 100) / 1024);
                } else if (ev.code == KEY_PREVIOUSSONG || ev.code == KEY_LEFT) {
                    printf("Seek -60s\n");
                    seek_request = -1;
                } else if (ev.code == KEY_NEXTSONG || ev.code == KEY_RIGHT) {
                    printf("Seek +60s\n");
                    seek_request = 1;
                } else if (ev.code == KEY_PLAY || ev.code == KEY_PLAYPAUSE ||
                           ev.code == KEY_PAUSE || ev.code == KEY_ENTER) {
                    paused = !paused;
                    printf("%s\n", paused ? "Paused" : "Resumed");
                }
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
    if ((err = x_snd_pcm_open(&g_pcm, "plughw:0,0", 0 /* SND_PCM_STREAM_PLAYBACK */, 0)) < 0) {
        if ((err = x_snd_pcm_open(&g_pcm, "hw:0,0", 0, 0)) < 0) {
            printf("Cannot open ALSA audio device (%d)\n", err);
            return -1;
        }
    }
    
    void *hw;
    x_snd_pcm_hw_params_malloc(&hw);
    x_snd_pcm_hw_params_any(g_pcm, hw);
    x_snd_pcm_hw_params_set_access(g_pcm, hw, 3 /* SND_PCM_ACCESS_RW_INTERLEAVED */);
    x_snd_pcm_hw_params_set_format(g_pcm, hw, 2 /* SND_PCM_FORMAT_S16_LE */);
    x_snd_pcm_hw_params_set_channels(g_pcm, hw, channels);
    
    unsigned int want_rate = rate;
    int dir = 0;
    x_snd_pcm_hw_params_set_rate_near(g_pcm, hw, &want_rate, &dir);
    
    unsigned int btime = 500000, bdir = 0;
    x_snd_pcm_hw_params_set_buffer_time_near(g_pcm, hw, &btime, (int *)&bdir);
    unsigned int ptime = 100000, pdir = 0;
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

int main(int argc, char *argv[]) {
    printf("==== PROGRAM START ====\n"); fflush(stdout);
    sleep(2); // Wait 2 seconds so the network stack can send the text before any crash

    const char *filename = "1.mp4";
    if (argc >= 2) {
        filename = argv[1];
    }
    printf("Playing file: %s\n", filename); fflush(stdout);
    sleep(1);

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
    printf("[DBG] FB ok, out_fmt=%d, mmap=%p, screensize=%ld\n", out_fmt, fbp, screensize); fflush(stdout);

    /* 2. FFmpeg Init */
    printf("[DBG] avformat_alloc_context...\n"); fflush(stdout);
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    printf("[DBG] avformat_alloc_context OK (ptr=%p)\n", (void*)pFormatCtx); fflush(stdout);

    printf("[DBG] avformat_open_input(%s)...\n", filename); fflush(stdout);
    if (avformat_open_input(&pFormatCtx, filename, NULL, NULL) != 0) {
        printf("Couldn't open input file.\n");
        return -1;
    }
    printf("[DBG] avformat_open_input OK\n"); fflush(stdout);

    printf("[DBG] avformat_find_stream_info...\n"); fflush(stdout);
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Couldn't find stream information.\n");
        return -1;
    }
    printf("[DBG] avformat_find_stream_info OK\n"); fflush(stdout);

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
                /* Resampler: convert any format -> S16LE stereo 44100Hz */
                AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                int ret = swr_alloc_set_opts2(&swr_ctx,
                    &out_ch_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
                    &aCodecCtx->ch_layout, aCodecCtx->sample_fmt, aCodecCtx->sample_rate,
                    0, NULL);
                if (ret >= 0) swr_init(swr_ctx);

                printf("Audio: %dHz %dch -> %dHz %dch S16LE\n",
                       aCodecCtx->sample_rate, aCodecCtx->ch_layout.nb_channels,
                       out_sample_rate, out_channels);

                /* Open native ALSA via dlopen */
                open_alsa(out_sample_rate, out_channels);
            } else {
                printf("Warning: Could not open audio codec.\n");
                avcodec_free_context(&aCodecCtx);
                aCodecCtx = NULL;
            }
        }
    }

    /* Video scaling setup */
    AVFrame *pFrame = av_frame_alloc();
    AVFrame *pFrameFB = av_frame_alloc();
    AVFrame *aFrame = av_frame_alloc();

    /* Scale to rotated dimensions: fb_height x fb_width (800x480) */
    int scale_w = vinfo.yres;  /* 800 */
    int scale_h = vinfo.xres;  /* 480 */
    int bpp_bytes = fb_bpp / 8;

    int numBytes = av_image_get_buffer_size(out_fmt, scale_w, scale_h, 1);
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

    av_image_fill_arrays(pFrameFB->data, pFrameFB->linesize, buffer, out_fmt, scale_w, scale_h, 1);

    struct SwsContext *sws_ctx = sws_getContext(
        pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        scale_w, scale_h, out_fmt,
        SWS_FAST_BILINEAR, NULL, NULL, NULL
    );

    AVPacket *packet = av_packet_alloc();

    init_input();

    printf("Playing video (rotated 90 CW)...\n");
    printf("Controls: VOL+ seek -60s, VOL- seek +60s, PLAY pause, POWER quit\n");

    AVStream *vstream = pFormatCtx->streams[videoStream];
    int64_t last_pts = 0;

    while (!quit && av_read_frame(pFormatCtx, packet) >= 0) {

        /* --- VIDEO PACKET --- */
        if (packet->stream_index == videoStream) {
            if (avcodec_send_packet(pCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                    last_pts = pFrame->pts;

                    static int frame_counter = 0;
                    frame_counter++;
                    /* Skip scaling and drawing for every 2nd frame to save 50% CPU */
                    if ((frame_counter % 2) != 0) {
                        /* We still need to process input and pause logic */
                        check_input();
                        if (quit || seek_request) break;
                        continue;
                    }

                    sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
                              pFrame->linesize, 0, pCodecCtx->height,
                              pFrameFB->data, pFrameFB->linesize);

                    /* Rotate 90° CW: src(sx,sy) -> dst(scale_h-1-sy, sx) */
                    if (fb_bpp == 16) {
                        for (int sy = 0; sy < scale_h; sy++) {
                            uint16_t *src_row = (uint16_t *)(pFrameFB->data[0] + sy * pFrameFB->linesize[0]);
                            int dst_x = scale_h - 1 - sy;
                            for (int sx = 0; sx < scale_w; sx++) {
                                int dst_y = sx;
                                uint16_t *dst_ptr = (uint16_t *)(fbp + dst_y * finfo.line_length + dst_x * 2);
                                *dst_ptr = src_row[sx];
                            }
                        }
                    } else if (fb_bpp == 32) {
                        for (int sy = 0; sy < scale_h; sy++) {
                            uint32_t *src_row = (uint32_t *)(pFrameFB->data[0] + sy * pFrameFB->linesize[0]);
                            int dst_x = scale_h - 1 - sy;
                            for (int sx = 0; sx < scale_w; sx++) {
                                int dst_y = sx;
                                uint32_t *dst_ptr = (uint32_t *)(fbp + dst_y * finfo.line_length + dst_x * 4);
                                *dst_ptr = src_row[sx];
                            }
                        }
                    }

                    /* Pause loop */
                    do {
                        check_input();
                        if (quit) break;

                        /* Handle seek */
                        if (seek_request != 0) {
                            int64_t offset = (int64_t)seek_request * 60 * vstream->time_base.den / vstream->time_base.num;
                            int64_t target = last_pts + offset;
                            if (target < 0) target = 0;
                            av_seek_frame(pFormatCtx, videoStream, target, (seek_request < 0) ? AVSEEK_FLAG_BACKWARD : 0);
                            avcodec_flush_buffers(pCodecCtx);
                            if (aCodecCtx) avcodec_flush_buffers(aCodecCtx);
                            if (g_pcm) {
                                x_snd_pcm_drop(g_pcm);
                                x_snd_pcm_prepare(g_pcm);
                            }
                            seek_request = 0;
                            break;
                        }

                        if (paused) usleep(50000);
                    } while (paused && !quit);

                    if (quit || seek_request) break;

                    /* Delay only if no audio (audio write provides natural pacing) */
                    if (!g_pcm) usleep(30000);
                }
            }
        }

        /* --- AUDIO PACKET --- */
        else if (packet->stream_index == audioStream && aCodecCtx && g_pcm && !paused) {
            if (avcodec_send_packet(aCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(aCodecCtx, aFrame) == 0) {
                    int out_samples = swr_get_out_samples(swr_ctx, aFrame->nb_samples);
                    int needed = out_samples * out_channels * 2; /* S16LE = 2 bytes */
                    if (needed > audio_buf_size) {
                        audio_buf = realloc(audio_buf, needed);
                        audio_buf_size = needed;
                    }

                    uint8_t *out_buf = audio_buf;
                    int converted = swr_convert(swr_ctx, &out_buf, out_samples,
                        (const uint8_t **)aFrame->data, aFrame->nb_samples);

                    if (converted > 0 && g_pcm) {
                        /* Force Volume using integer math (current_volume/1024) */
                        int16_t *samples = (int16_t *)audio_buf;
                        int total_samples = converted * out_channels;
                        for (int i = 0; i < total_samples; i++) {
                            samples[i] = (int16_t)((samples[i] * current_volume) >> 10);
                        }

                        long err = x_snd_pcm_writei(g_pcm, audio_buf, converted);
                        if (err < 0) {
                            x_snd_pcm_recover(g_pcm, err, 0);
                        }
                    }
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

    av_free(buffer);
    av_frame_free(&aFrame);
    av_frame_free(&pFrameFB);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecCtx);
    avformat_close_input(&pFormatCtx);
    av_packet_free(&packet);
    sws_freeContext(sws_ctx);
    
    munmap(fbp, screensize);
    close(fbfd);

    return 0;
}
