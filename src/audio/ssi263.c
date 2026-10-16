#include "audio/ssi263.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// The phonemes
// ---------------------------------------------------------------------------
//
// Each phoneme is a kind of sound, its three formants (for an adult voice,
// from the usual measurements of English speech) and, for the hissing
// ones, the middle of the band the hiss falls in. The names are the data
// sheet's; the sounds are this synthesizer's best guess at them.

enum {
    PAUSE,
    VOWEL,      // voiced, through the formants
    NASAL,      // voiced, quieter (M, N, NG)
    FRICATIVE,  // hiss in its own band (S, SH, F, TH)
    VOICED_FRICATIVE,  // the same with a buzz under it (Z, V, J)
    STOP,       // silence, then a burst of hiss (P, T, K)
    VOICED_STOP,  // a murmur, then a burst (B, D, G)
    BREATH,     // hiss through the formants (H)
    VOICED_BREATH,  // the same with some buzz
};

typedef struct {
    uint8_t kind;
    uint16_t f1, f2, f3;
    uint16_t hiss;  // the hiss band's centre, Hz
} Phoneme;

static const Phoneme phonemes[64] = {
    [0x00] = {PAUSE, 500, 1500, 2500, 0},          // PA
    [0x01] = {VOWEL, 270, 2290, 3010, 0},          // E    (beet)
    [0x02] = {VOWEL, 300, 2200, 2900, 0},          // E1
    [0x03] = {VOWEL, 280, 2250, 2950, 0},          // Y    (yes)
    [0x04] = {VOWEL, 320, 2050, 2750, 0},          // YI
    [0x05] = {VOWEL, 480, 1900, 2600, 0},          // AY   (day)
    [0x06] = {VOWEL, 650, 1500, 2500, 0},          // IE   (pie)
    [0x07] = {VOWEL, 390, 1990, 2550, 0},          // I    (bit)
    [0x08] = {VOWEL, 480, 1850, 2550, 0},          // A    (made)
    [0x09] = {VOWEL, 520, 1700, 2500, 0},          // AI   (care)
    [0x0A] = {VOWEL, 530, 1840, 2480, 0},          // EH   (bet)
    [0x0B] = {VOWEL, 550, 1780, 2480, 0},          // EH1
    [0x0C] = {VOWEL, 660, 1720, 2410, 0},          // AE   (bat)
    [0x0D] = {VOWEL, 640, 1650, 2400, 0},          // AE1
    [0x0E] = {VOWEL, 730, 1090, 2440, 0},          // AH   (father)
    [0x0F] = {VOWEL, 700, 1150, 2440, 0},          // AH1
    [0x10] = {VOWEL, 300, 700, 2200, 0},           // W    (water)
    [0x11] = {VOWEL, 500, 900, 2400, 0},           // O    (boat)
    [0x12] = {VOWEL, 450, 850, 2350, 0},           // OU
    [0x13] = {VOWEL, 300, 870, 2240, 0},           // OO   (boot)
    [0x14] = {VOWEL, 350, 1300, 2300, 0},          // IU
    [0x15] = {VOWEL, 380, 1400, 2300, 0},          // IU1
    [0x16] = {VOWEL, 440, 1020, 2240, 0},          // U    (book)
    [0x17] = {VOWEL, 420, 1000, 2240, 0},          // U1
    [0x18] = {VOWEL, 640, 1190, 2390, 0},          // UH   (but)
    [0x19] = {VOWEL, 600, 1220, 2400, 0},          // UH1
    [0x1A] = {VOWEL, 550, 1300, 2400, 0},          // UH2
    [0x1B] = {VOWEL, 500, 1400, 2400, 0},          // UH3
    [0x1C] = {VOWEL, 490, 1350, 1690, 0},          // ER   (bird)
    [0x1D] = {VOWEL, 460, 1200, 1600, 0},          // R
    [0x1E] = {VOWEL, 450, 1150, 1550, 0},          // R1
    [0x1F] = {VOWEL, 440, 1100, 1500, 0},          // R2
    [0x20] = {VOWEL, 360, 1000, 2700, 0},          // L
    [0x21] = {VOWEL, 380, 1050, 2700, 0},          // L1
    [0x22] = {VOWEL, 400, 1100, 2650, 0},          // LF
    [0x23] = {VOWEL, 300, 650, 2200, 0},           // W1
    [0x24] = {VOICED_STOP, 200, 900, 2300, 1200},  // B
    [0x25] = {VOICED_STOP, 200, 1700, 2600, 4000}, // D
    [0x26] = {VOICED_STOP, 250, 1900, 2300, 2500}, // KV   (g)
    [0x27] = {STOP, 400, 1100, 2300, 1500},        // P
    [0x28] = {STOP, 400, 1700, 2600, 4500},        // T
    [0x29] = {STOP, 400, 1900, 2400, 2500},        // K
    [0x2A] = {VOICED_BREATH, 500, 1500, 2500, 0},  // HV
    [0x2B] = {VOICED_BREATH, 500, 1500, 2500, 0},  // HVC
    [0x2C] = {BREATH, 500, 1500, 2500, 0},         // HF
    [0x2D] = {BREATH, 500, 1500, 2500, 0},         // HFC
    [0x2E] = {NASAL, 280, 1500, 2500, 0},          // HN
    [0x2F] = {VOICED_FRICATIVE, 250, 1600, 2600, 5000},  // Z
    [0x30] = {FRICATIVE, 400, 1600, 2600, 5500},   // S
    [0x31] = {VOICED_FRICATIVE, 250, 1800, 2500, 2600},  // J    (measure)
    [0x32] = {FRICATIVE, 400, 1800, 2500, 2800},   // SCH  (ship)
    [0x33] = {VOICED_FRICATIVE, 250, 1100, 2400, 3500},  // V
    [0x34] = {FRICATIVE, 400, 1100, 2400, 6500},   // F
    [0x35] = {VOICED_FRICATIVE, 250, 1400, 2500, 4500},  // THV  (the)
    [0x36] = {FRICATIVE, 400, 1400, 2500, 6000},   // TH   (thin)
    [0x37] = {NASAL, 280, 1000, 2200, 0},          // M
    [0x38] = {NASAL, 280, 1700, 2600, 0},          // N
    [0x39] = {NASAL, 280, 2300, 2750, 0},          // NG
    [0x3A] = {VOWEL, 500, 1700, 2500, 0},          // :A
    [0x3B] = {VOWEL, 400, 1300, 2300, 0},          // :OH
    [0x3C] = {VOWEL, 300, 1600, 2200, 0},          // :U
    [0x3D] = {VOWEL, 450, 1400, 2300, 0},          // :UH
    [0x3E] = {VOWEL, 300, 2300, 3000, 0},          // E2
    [0x3F] = {VOWEL, 380, 900, 2600, 0},           // LB
};

// Fricatives differ in strength: S and SH are loud, F and TH faint.
static double hiss_strength(int p)
{
    switch (p) {
    case 0x30: case 0x32: case 0x2F: case 0x31: return 0.8;  // S, SCH, Z, J
    case 0x34: case 0x36: case 0x33: case 0x35: return 0.3;  // F, TH, V, THV
    default: return 0.6;
    }
}

// ---------------------------------------------------------------------------
// Registers and timing
// ---------------------------------------------------------------------------

void ssi263_init(Ssi263 *s, double cpu_hz, int sample_rate)
{
    float *samples = s->samples;
    int capacity = s->sample_capacity;
    memset(s, 0, sizeof *s);
    s->samples = samples;
    s->sample_capacity = capacity;
    s->cpu_hz = cpu_hz;
    s->sample_rate = sample_rate;
    s->cycles_per_sample = cpu_hz / sample_rate;
    s->mode = 3;  // no requests until the program sets a mode
    s->noise = 0x12345678;
    for (int i = 0; i < 3; i++) s->f[i] = s->f_target[i] = phonemes[0].f1 + i * 1000;
}

void ssi263_free(Ssi263 *s)
{
    free(s->samples);
    s->samples = NULL;
    s->sample_capacity = s->sample_count = 0;
}

// Microseconds per frame, and per phoneme, at the current rate and
// duration (from MAME).
static double frame_us(const Ssi263 *s) { return 4096.0 * (16 - s->rate) / 2; }
static double phoneme_us(const Ssi263 *s) { return frame_us(s) * (4 - s->duration); }

// A new phoneme: the synthesizer starts gliding to its sound.
static void start_phoneme(Ssi263 *s)
{
    const Phoneme *p = &phonemes[s->phoneme];
    s->kind = p->kind;
    s->f_target[0] = p->f1;
    s->f_target[1] = p->f2;
    s->f_target[2] = p->f3;
    s->hiss_freq = p->hiss ? p->hiss : 3000;
    s->hiss_shaped = p->kind == BREATH || p->kind == VOICED_BREATH;
    s->phoneme_time = 0;
    switch (p->kind) {
    case VOWEL: s->voice_target = 1.0, s->hiss_target = 0; break;
    case NASAL: s->voice_target = 0.55, s->hiss_target = s->phoneme == 0x2E ? 0.2 : 0; break;
    case FRICATIVE: s->voice_target = 0, s->hiss_target = hiss_strength(s->phoneme); break;
    case VOICED_FRICATIVE: s->voice_target = 0.45, s->hiss_target = hiss_strength(s->phoneme) * 0.6; break;
    case BREATH: s->voice_target = 0, s->hiss_target = 0.5; break;
    case VOICED_BREATH: s->voice_target = 0.35, s->hiss_target = 0.35; break;
    case STOP: case VOICED_STOP: s->voice_target = s->kind == VOICED_STOP ? 0.2 : 0, s->hiss_target = 0; break;
    default: s->voice_target = 0, s->hiss_target = 0; break;
    }
}

void ssi263_write(Ssi263 *s, uint64_t now, int reg, uint8_t v)
{
    ssi263_run(s, now);  // the sound so far is made with the old settings
    switch (reg & 7) {
    case 0:
        s->duration = (v >> 5) & 3;
        s->phoneme = v & 0x3F;
        s->log[s->log_count++ % SSI263_LOG] = s->phoneme;
        start_phoneme(s);
        if (s->mode != 3) {
            // Busy with this phoneme; ask for the next when it's done.
            double us = s->mode == 2 ? frame_us(s) : phoneme_us(s);
            s->requesting = false;
            s->request_at = now + (uint64_t)(us * s->cpu_hz / 1e6);
        }
        break;
    case 1:
        s->inflection = (uint16_t)((s->inflection & 0x807) | v << 3);
        break;
    case 2:
        s->inflection = (uint16_t)((s->inflection & 0x7F8) | (v & 7) | (v & 8) << 8);
        s->rate = v >> 4;
        break;
    case 3: {
        bool control = v & 0x80;
        if (s->control && !control) s->mode = s->duration;  // control 1 -> 0 latches the mode
        s->control = control;
        s->articulation = (v >> 4) & 7;
        s->amplitude = v & 0x0F;
        if (s->mode == 3) s->requesting = false;
        break;
    }
    default:
        s->filter = v;
        break;
    }
}

bool ssi263_requesting(Ssi263 *s, uint64_t now)
{
    if (s->request_at && now >= s->request_at) {
        s->request_at = 0;
        s->requesting = true;
    }
    return s->requesting;
}

uint8_t ssi263_read(Ssi263 *s, uint64_t now)
{
    return ssi263_requesting(s, now) ? 0x80 : 0x00;
}

// ---------------------------------------------------------------------------
// The synthesizer
// ---------------------------------------------------------------------------

// Tune a resonator to frequency f with bandwidth bw (Hz). Its gain at low
// frequencies is 1, with a peak at f that's higher the narrower it is.
static void tune(Ssi263Resonator *r, double f, double bw, double rate)
{
    double t = 1.0 / rate;
    r->c = -exp(-2 * M_PI * bw * t);
    r->b = 2 * exp(-M_PI * bw * t) * cos(2 * M_PI * f * t);
    r->a = 1 - r->b - r->c;
}

static double resonate(Ssi263Resonator *r, double x)
{
    double y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}

static double white_noise(Ssi263 *s)
{
    s->noise ^= s->noise << 13;
    s->noise ^= s->noise >> 17;
    s->noise ^= s->noise << 5;
    return (double)s->noise / 2147483648.0 - 1.0;
}

// The pitch from the inflection register, Hz. (The chip's own scale isn't
// known; this puts the values the games use in a man's speaking range.)
static double pitch(const Ssi263 *s)
{
    double f0 = 60 + s->inflection * 0.12;
    return f0 < 50 ? 50 : f0 > 400 ? 400 : f0;
}

// One output sample.
static float synthesize(Ssi263 *s)
{
    double rate = s->sample_rate;

    // Stops: a closure for the first part of the phoneme, then the burst.
    if ((s->kind == STOP || s->kind == VOICED_STOP) && s->phoneme_time == (int)(0.035 * rate)) {
        s->hiss = 0.9;           // the burst starts at full strength...
        s->hiss_target = 0;      // ...and fades
        if (s->kind == VOICED_STOP) s->voice_target = 0.8;
    }
    s->phoneme_time++;

    // Glide towards the targets: formants at the articulation's speed
    // (faster for higher settings), loudness more quickly.
    double glide_ms = 8 + (7 - s->articulation) * 6;
    double k = 1 - exp(-1000.0 / (glide_ms * rate));
    double k_level = 1 - exp(-1000.0 / (6 * rate));
    for (int i = 0; i < 3; i++) s->f[i] += (s->f_target[i] - s->f[i]) * k;
    s->voice += (s->voice_target - s->voice) * k_level;
    s->hiss += (s->hiss_target - s->hiss) * k_level;
    double level_target = s->phoneme == 0 ? 0 : s->amplitude / 15.0;
    s->level += (level_target - s->level) * k_level;

    // The buzz: a smooth pulse each pitch period (open for 60%, closing over
    // 15%, then shut), differentiated as the lips do to it.
    s->phase += pitch(s) / rate;
    if (s->phase >= 1) s->phase -= 1;
    double g = 0;
    if (s->phase < 0.6) g = 0.5 * (1 - cos(M_PI * s->phase / 0.6));
    else if (s->phase < 0.75) g = cos(0.5 * M_PI * (s->phase - 0.6) / 0.15);
    double buzz = (g - s->last_glottal) * 60;
    s->last_glottal = g;
    double noise = white_noise(s);

    // The formants shape the buzz (and the breath, for H sounds).
    tune(&s->formant[0], s->f[0], 70, rate);
    tune(&s->formant[1], s->f[1], 100, rate);
    tune(&s->formant[2], s->f[2], 160, rate);
    double x = buzz * s->voice + (s->hiss_shaped ? noise * s->hiss * 0.25 : 0);
    for (int i = 0; i < 3; i++) x = resonate(&s->formant[i], x);
    double out = x * 0.04;

    // Other hiss has its own band.
    if (!s->hiss_shaped) {
        tune(&s->hiss_band, s->hiss_freq, 2000, rate);
        out += resonate(&s->hiss_band, noise * s->hiss) * 0.5;
    }

    out *= s->level;
    return (float)(out / (1 + fabs(out)));  // a soft limit, never past 1
}

void ssi263_run(Ssi263 *s, uint64_t now)
{
    if (!s->rendering) return;
    while (s->next_sample <= (double)now) {
        s->next_sample += s->cycles_per_sample;
        if (s->sample_count == s->sample_capacity) {
            int capacity = s->sample_capacity ? s->sample_capacity * 2 : 4096;
            float *grown = realloc(s->samples, sizeof *grown * (size_t)capacity);
            if (!grown) return;
            s->samples = grown;
            s->sample_capacity = capacity;
        }
        s->samples[s->sample_count++] = synthesize(s);
    }
}

void ssi263_start(Ssi263 *s, uint64_t now)
{
    s->rendering = true;
    s->next_sample = (double)now;
    s->sample_count = 0;
}

int ssi263_take_samples(Ssi263 *s, const float **samples)
{
    *samples = s->samples;
    int n = s->sample_count;
    s->sample_count = 0;
    return n;
}

const char *ssi263_phoneme_name(int p)
{
    static const char *const names[64] = {
        "PA", "E", "E1", "Y", "YI", "AY", "IE", "I", "A", "AI", "EH", "EH1", "AE", "AE1", "AH", "AH1",
        "W", "O", "OU", "OO", "IU", "IU1", "U", "U1", "UH", "UH1", "UH2", "UH3", "ER", "R", "R1", "R2",
        "L", "L1", "LF", "W1", "B", "D", "KV", "P", "T", "K", "HV", "HVC", "HF", "HFC", "HN", "Z",
        "S", "J", "SCH", "V", "F", "THV", "TH", "M", "N", "NG", ":A", ":OH", ":U", ":UH", "E2", "LB",
    };
    return names[p & 0x3F];
}
