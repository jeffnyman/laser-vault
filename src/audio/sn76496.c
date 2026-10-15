#include "audio/sn76496.h"

#include <math.h>
#include <stdlib.h>

// How loud the chip is compared with the laserdisc soundtrack.
#define CHIP_GAIN 0.5f

// The noise generator is a 17-bit shift register. Each step it shifts
// right; the bit coming in at the top is bit 2 of the old value, xor'ed
// with bit 3 for white noise. Its output is the bottom bit. (Register size
// and taps as in MAME's sn76496.cpp for this chip.)
#define NOISE_TOP 0x10000
#define NOISE_TAP1 0x04
#define NOISE_TAP2 0x08

// Output level for an attenuation: 2 dB a step, 15 meaning silent.
static float level(int attenuation)
{
    return attenuation >= 15 ? 0.0f : powf(10.0f, -0.1f * (float)attenuation);
}

void sn76496_init(Sn76496 *sn, double cpu_hz, double chip_hz, double sample_rate)
{
    *sn = (Sn76496){
        .ticks_per_cycle = chip_hz / 16 / cpu_hz,
        .samples_per_tick = sample_rate / (chip_hz / 16),
        .attenuation = {15, 15, 15, 15},
        .period = {0x400, 0x400, 0x400, 32},
        .shift = NOISE_TOP,
    };
}

void sn76496_free(Sn76496 *sn)
{
    free(sn->samples);
    sn->samples = NULL;
}

// The noise rate, in chip ticks per shift: 32, 64 or 128, or follow tone
// channel 2, whose output changes every period[2] ticks (a full wave is
// twice that).
static uint16_t noise_period(const Sn76496 *sn)
{
    return (sn->noise & 3) == 3 ? (uint16_t)(sn->period[2] * 2) : (uint16_t)(32 << (sn->noise & 3));
}

static float mixer_output(const Sn76496 *sn)
{
    float out = 0;
    for (int ch = 0; ch < 4; ch++)
        if (sn->high[ch]) out += level(sn->attenuation[ch]);
    return out / 4;
}

// One chip tick (clock / 16): advance the generators, return the output.
static float chip_tick(Sn76496 *sn)
{
    for (int ch = 0; ch < 3; ch++) {
        if (sn->count[ch] == 0 || --sn->count[ch] == 0) {
            sn->count[ch] = sn->period[ch];
            sn->high[ch] = !sn->high[ch];
        }
    }
    if (sn->count[3] == 0 || --sn->count[3] == 0) {
        sn->count[3] = noise_period(sn);
        bool in = (sn->shift & NOISE_TAP1) != 0;
        if (sn->noise & 4) in ^= (sn->shift & NOISE_TAP2) != 0;
        sn->shift = (sn->shift >> 1) | (in ? NOISE_TOP : 0);
        sn->high[3] = sn->shift & 1;
    }
    return mixer_output(sn);
}

static void emit_sample(Sn76496 *sn, float level)
{
    // The outputs never go negative; take off the steady offset, as the
    // capacitor on a real board does.
    float out = level - sn->dc_in + 0.995f * sn->dc_out;
    sn->dc_in = level;
    sn->dc_out = out;

    if (sn->sample_count == sn->sample_capacity) {
        sn->sample_capacity = sn->sample_capacity ? sn->sample_capacity * 2 : 4096;
        sn->samples = realloc(sn->samples, (size_t)sn->sample_capacity * sizeof(float));
    }
    sn->samples[sn->sample_count++] = out * CHIP_GAIN;
}

void sn76496_run(Sn76496 *sn, uint64_t now)
{
    if (now <= sn->time) return;
    double ticks = (double)(now - sn->time) * sn->ticks_per_cycle + sn->tick_carry;
    sn->time = now;
    long whole = (long)ticks;
    sn->tick_carry = ticks - (double)whole;
    if (!sn->rendering) return;  // no envelopes: nothing to keep in step

    for (long i = 0; i < whole; i++) {
        sn->mix_sum += chip_tick(sn);
        sn->mix_count++;
        sn->sample_phase += sn->samples_per_tick;
        if (sn->sample_phase >= 1.0) {
            sn->sample_phase -= 1.0;
            emit_sample(sn, (float)(sn->mix_sum / sn->mix_count));
            sn->mix_sum = 0;
            sn->mix_count = 0;
        }
    }
}

void sn76496_write(Sn76496 *sn, uint64_t now, uint8_t byte)
{
    sn76496_run(sn, now);  // the old setting sounds right up to this moment
    if (byte & 0x80) sn->latched = (byte >> 4) & 7;
    int ch = sn->latched >> 1;
    if (sn->latched & 1) {  // attenuation: always the low four bits
        sn->attenuation[ch] = byte & 0x0F;
    } else if (ch == 3) {  // noise setting; writing it restarts the noise
        sn->noise = byte & 7;
        sn->shift = NOISE_TOP;
    } else {  // tone period: low four bits from a latch byte, the rest from a data byte
        uint16_t p = sn->period[ch] & 0x3FF;
        p = (byte & 0x80) ? (uint16_t)((p & 0x3F0) | (byte & 0x0F)) : (uint16_t)((p & 0x00F) | ((byte & 0x3F) << 4));
        sn->period[ch] = p ? p : 0x400;  // a period of 0 counts as 1024
    }
}

void sn76496_set_rendering(Sn76496 *sn, bool on, uint64_t now)
{
    sn76496_run(sn, now);
    sn->rendering = on;
    sn->sample_count = 0;
    sn->dc_in = mixer_output(sn);  // no click on switching on
    sn->dc_out = 0;
}

int sn76496_take_samples(Sn76496 *sn, const float **samples)
{
    int n = sn->sample_count;
    *samples = sn->samples;
    sn->sample_count = 0;
    return n;
}
