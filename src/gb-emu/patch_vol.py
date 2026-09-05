import re

with open("src/platform.c", "r") as f:
    content = f.read()

# Add current_volume
if "static int current_volume" not in content:
    content = content.replace("static size_t fb_map_size;", "static size_t fb_map_size;\nstatic int current_volume = 205; /* 20% default (205/1024) */")

# Modify gb_platform_update_audio to apply volume
old_update = """    void *pcm = platform->audio_handle;
    int count;
    while ((count = gb_apu_read_samples(apu, platform->audio_buffer,
                                       platform->audio_buf_size)) > 0) {
        s16 *cursor = platform->audio_buffer;
        int remaining = count;
        while (remaining > 0) {
            long frames = x_snd_pcm_writei(pcm, cursor, remaining);"""

new_update = """    void *pcm = platform->audio_handle;
    int count;
    while ((count = gb_apu_read_samples(apu, platform->audio_buffer,
                                       platform->audio_buf_size)) > 0) {
        /* Apply software volume scaling */
        for (int i = 0; i < count; i++) {
            platform->audio_buffer[i] = (s16)((platform->audio_buffer[i] * current_volume) >> 10);
        }

        s16 *cursor = platform->audio_buffer;
        int remaining = count;
        while (remaining > 0) {
            long frames = x_snd_pcm_writei(pcm, cursor, remaining);"""

if "Apply software volume scaling" not in content:
    content = content.replace(old_update, new_update)

with open("src/platform.c", "w") as f:
    f.write(content)
