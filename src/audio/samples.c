#include "audio/samples.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void samples_init(Samples *s, double cpu_hz, int sample_rate)
{
    memset(s, 0, sizeof *s);
    s->sample_rate = sample_rate;
    s->cycles_per_sample = cpu_hz / sample_rate;
    for (int i = 0; i < SAMPLES_MAX; i++) s->position[i] = -1;
}

void samples_free(Samples *s)
{
    for (int i = 0; i < SAMPLES_MAX; i++) free(s->data[i]);
    free(s->out);
    memset(s, 0, sizeof *s);
}

// ---------------------------------------------------------------------------
// WAV files
// ---------------------------------------------------------------------------
//
// A WAV file is a RIFF file: "RIFF", a size, "WAVE", then chunks, each a
// four-letter name, a 32-bit little-endian size and the data. The "fmt "
// chunk says how the sound is stored; the "data" chunk holds it.

static uint32_t le32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static uint8_t *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = *size > 0 ? malloc((size_t)*size) : NULL;
    if (data && fread(data, 1, (size_t)*size, f) != (size_t)*size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

bool samples_load(Samples *s, int n, const char *dir, const char *name)
{
    if (n < 0 || n >= SAMPLES_MAX) return false;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    long size = 0;
    uint8_t *file = read_file(path, &size);
    if (!file || size < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4)) {
        free(file);
        return false;
    }

    int channels = 0, rate = 0, bits = 0;
    const uint8_t *pcm = NULL;
    uint32_t pcm_bytes = 0;
    for (long at = 12; at + 8 <= size;) {
        uint32_t chunk = le32(file + at + 4);
        if (chunk > (uint32_t)(size - at - 8)) chunk = (uint32_t)(size - at - 8);
        if (!memcmp(file + at, "fmt ", 4) && chunk >= 16 && le16(file + at + 8) == 1) {  // 1: plain PCM
            channels = le16(file + at + 10);
            rate = (int)le32(file + at + 12);
            bits = le16(file + at + 22);
        } else if (!memcmp(file + at, "data", 4)) {
            pcm = file + at + 8;
            pcm_bytes = chunk;
        }
        at += 8 + (long)chunk + (chunk & 1);  // chunks are padded to an even size
    }
    if (!pcm || channels < 1 || rate <= 0 || (bits != 8 && bits != 16)) {
        free(file);
        return false;
    }

    // Mix the channels down to one.
    int frame_bytes = channels * bits / 8;
    int frames = (int)(pcm_bytes / (uint32_t)frame_bytes);
    float *mono = malloc(sizeof *mono * (size_t)(frames > 0 ? frames : 1));
    if (!mono) {
        free(file);
        return false;
    }
    for (int i = 0; i < frames; i++) {
        float sum = 0;
        for (int c = 0; c < channels; c++) {
            const uint8_t *p = pcm + (size_t)i * frame_bytes + (size_t)c * bits / 8;
            sum += bits == 16 ? (int16_t)le16(p) / 32768.0f : (p[0] - 128) / 128.0f;  // 8-bit is unsigned
        }
        mono[i] = sum / channels;
    }
    free(file);

    free(s->data[n]);
    s->data[n] = mono;
    s->length[n] = frames;
    s->step[n] = (double)rate / s->sample_rate;
    s->position[n] = -1;
    return true;
}

int samples_loaded(const Samples *s)
{
    int n = 0;
    for (int i = 0; i < SAMPLES_MAX; i++) n += s->length[i] > 0;
    return n;
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

void samples_run(Samples *s, uint64_t now)
{
    if (!s->rendering) return;
    while (s->next_sample <= (double)now) {
        s->next_sample += s->cycles_per_sample;
        float mix = 0;
        for (int i = 0; i < SAMPLES_MAX; i++) {
            if (s->position[i] < 0) continue;
            // Between two recorded samples: blend them.
            int at = (int)s->position[i];
            double frac = s->position[i] - at;
            float a = s->data[i][at], b = at + 1 < s->length[i] ? s->data[i][at + 1] : 0.0f;
            mix += a + (float)frac * (b - a);
            s->position[i] += s->step[i];
            if (s->position[i] >= s->length[i]) s->position[i] = -1;
        }
        if (s->out_count == s->out_capacity) {
            int capacity = s->out_capacity ? s->out_capacity * 2 : 4096;
            float *grown = realloc(s->out, sizeof *grown * (size_t)capacity);
            if (!grown) return;
            s->out = grown;
            s->out_capacity = capacity;
        }
        s->out[s->out_count++] = mix;
    }
}

void samples_trigger(Samples *s, uint64_t now, int n)
{
    if (!s->rendering || n < 0 || n >= SAMPLES_MAX || !s->length[n]) return;
    samples_run(s, now);
    s->position[n] = 0;
}

void samples_start(Samples *s, uint64_t now)
{
    s->rendering = true;
    s->next_sample = (double)now;
    s->out_count = 0;
}

int samples_take(Samples *s, const float **out)
{
    *out = s->out;
    int n = s->out_count;
    s->out_count = 0;
    return n;
}
