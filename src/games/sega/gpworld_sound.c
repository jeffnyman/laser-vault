#include "games/sega/gpworld_sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979

// The switches in DA01 (on when 0).
#define ENGINE 0x01
#define CHIME 0x04
#define COUNTDOWN 0x08
#define SIGNAL 0x10
#define TYRES 0x20

// The engine: a low buzz at idle rising with the revs (63 at the top is
// about 250 Hz), and the time it takes to glide to a new pitch.
#define ENGINE_IDLE_HZ 30.0
#define ENGINE_HZ_PER_REV 3.5
#define ENGINE_GLIDE_S 0.06

// The chime, the countdown's beep and the start signal.
static const double tone_hz[3] = {1200.0, 880.0, 1760.0};
static const uint8_t tone_switch[3] = {CHIME, COUNTDOWN, SIGNAL};

#define FADE_S 0.005   // how fast a sound fades in or out (no clicks)
#define CRASH_S 1.2
#define THUMP_S 0.08

// Levels in the mix.
#define ENGINE_VOLUME 0.30
#define TONE_VOLUME 0.18
#define SQUEAL_VOLUME 0.16
#define CRASH_VOLUME 0.55
#define THUMP_VOLUME 0.30

static float noise(GpwSound *s)
{
    s->noise = s->noise * 1664525u + 1013904223u;
    return (float)((int32_t)s->noise) / 2147483648.0f;
}

// Move level towards target (0 or 1) in FADE_S.
static double fade(double level, bool on, double dt)
{
    double step = dt / FADE_S;
    if (on) return level + step > 1 ? 1 : level + step;
    return level - step < 0 ? 0 : level - step;
}

static float next_sample(GpwSound *s)
{
    double dt = 1.0 / s->sample_rate;
    double out = 0;

    // The engine: a fundamental with a few harmonics, roughened by a
    // flutter at the firing rate, its pitch gliding after the revs.
    double target = ENGINE_IDLE_HZ + ENGINE_HZ_PER_REV * s->revs;
    s->engine_freq += (target - s->engine_freq) * (dt / ENGINE_GLIDE_S);
    s->engine_level = fade(s->engine_level, !(s->switches & ENGINE), dt * 0.1);  // engines don't stop dead
    if (s->engine_level > 0) {
        s->engine_phase = fmod(s->engine_phase + 2 * PI * s->engine_freq * dt, 2 * PI);
        s->rough_phase = fmod(s->rough_phase + 2 * PI * s->engine_freq * 0.5 * dt, 2 * PI);
        double p = s->engine_phase;
        double buzz = sin(p) + 0.6 * sin(2 * p) + 0.4 * sin(3 * p) + 0.25 * sin(5 * p);
        double flutter = 0.75 + 0.25 * sin(s->rough_phase) + 0.08 * noise(s);
        out += ENGINE_VOLUME * s->engine_level * buzz * flutter / 2.25;
    }

    // The tones: rounded squares, so they sound like a cabinet's beeps.
    for (int i = 0; i < 3; i++) {
        s->tone_level[i] = fade(s->tone_level[i], !(s->switches & tone_switch[i]), dt);
        if (s->tone_level[i] <= 0) continue;
        s->tone_phase[i] = fmod(s->tone_phase[i] + 2 * PI * tone_hz[i] * dt, 2 * PI);
        double t = sin(s->tone_phase[i]);
        out += TONE_VOLUME * s->tone_level[i] * tanh(3 * t) / tanh(3);
    }

    // The tyres: a wavering whistle over a band of noise.
    // (The game pulses the switch while the car slides; fading over 25 ms
    // runs the pulses together into one squeal.)
    s->squeal_level = fade(s->squeal_level, !(s->switches & TYRES), dt * 0.2);
    if (s->squeal_level > 0) {
        s->wobble_phase = fmod(s->wobble_phase + 2 * PI * 7.0 * dt, 2 * PI);
        double hz = 1700 + 90 * sin(s->wobble_phase);
        s->squeal_phase = fmod(s->squeal_phase + 2 * PI * hz * dt, 2 * PI);
        // Noise through a two-pole band-pass around 2.5 kHz.
        double f = 2 * sin(PI * 2500 * dt);
        s->squeal_band_low += f * s->squeal_band_mid;
        double high = noise(s) - s->squeal_band_low - 0.3 * s->squeal_band_mid;
        s->squeal_band_mid += f * high;
        out += SQUEAL_VOLUME * s->squeal_level * (0.7 * sin(s->squeal_phase) + 0.5 * s->squeal_band_mid);
    }

    // The crash: rumbling noise, dying away.
    if (s->crash_left > 0) {
        double env = s->crash_left / CRASH_S;
        s->crash_low += (noise(s) - s->crash_low) * 0.08;  // low-pass: a rumble, not a hiss
        out += CRASH_VOLUME * env * env * (s->crash_low * 3 + 0.25 * noise(s));
        s->crash_left -= dt;
    }

    // The gear change: a short low thump.
    if (s->thump_left > 0) {
        double env = s->thump_left / THUMP_S;
        s->thump_phase += 2 * PI * 70 * dt;
        out += THUMP_VOLUME * env * sin(s->thump_phase);
        s->thump_left -= dt;
    }

    if (out > 1) out = 1;
    if (out < -1) out = -1;
    return (float)out;
}

static void push(GpwSound *s, float v)
{
    if (s->sample_count == s->sample_capacity) {
        int cap = s->sample_capacity ? s->sample_capacity * 2 : 4096;
        float *p = realloc(s->samples, (size_t)cap * sizeof *p);
        if (!p) return;
        s->samples = p;
        s->sample_capacity = cap;
    }
    s->samples[s->sample_count++] = v;
}

void gpw_sound_run(GpwSound *s, uint64_t now)
{
    while (s->time + s->cycles_per_sample <= (double)now) {
        s->time += s->cycles_per_sample;
        float v = next_sample(s);
        if (s->rendering) push(s, v);
    }
}

void gpw_sound_init(GpwSound *s, double cpu_hz, double sample_rate)
{
    memset(s, 0, sizeof *s);
    s->cycles_per_sample = cpu_hz / sample_rate;
    s->sample_rate = sample_rate;
    s->switches = 0xFF;
    s->engine_freq = ENGINE_IDLE_HZ;
    s->noise = 1;
}

void gpw_sound_free(GpwSound *s)
{
    free(s->samples);
    s->samples = NULL;
    s->sample_count = s->sample_capacity = 0;
}

void gpw_sound_write(GpwSound *s, uint64_t now, uint16_t addr, uint8_t v)
{
    gpw_sound_run(s, now);
    switch (addr) {
    case 0xDA01: s->switches = v; break;
    case 0xDA80: s->revs = (uint8_t)~v & 0x3F; break;
    case 0xDAC0: s->thump_left = THUMP_S; break;
    case 0xDAE0: s->crash_left = CRASH_S; break;
    default: break;  // DAA0, the engine's setting; DA03
    }
}

void gpw_sound_set_rendering(GpwSound *s, bool on, uint64_t now)
{
    gpw_sound_run(s, now);
    s->rendering = on;
}

int gpw_sound_take(GpwSound *s, const float **samples)
{
    *samples = s->samples;
    int n = s->sample_count;
    s->sample_count = 0;  // the caller copies them out before the next call
    return n;
}
