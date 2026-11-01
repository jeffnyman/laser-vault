#include "audio/sp0250.h"

#include <stdlib.h>
#include <string.h>

// The coefficient table in the chip's ROM: 128 magnitudes, packed closer
// together as they near the top (where a filter resonates sharply).
static const uint16_t coefficients[128] = {
      0,   9,  17,  25,  33,  41,  49,  57,  65,  73,  81,  89,  97, 105, 113, 121,
    129, 137, 145, 153, 161, 169, 177, 185, 193, 201, 209, 217, 225, 233, 241, 249,
    257, 265, 273, 281, 289, 297, 301, 305, 309, 313, 317, 321, 325, 329, 333, 337,
    341, 345, 349, 353, 357, 361, 365, 369, 373, 377, 381, 385, 389, 393, 397, 401,
    405, 409, 413, 417, 421, 425, 427, 429, 431, 433, 435, 437, 439, 441, 443, 445,
    447, 449, 451, 453, 455, 457, 459, 461, 463, 465, 467, 469, 471, 473, 475, 477,
    479, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495,
    496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511,
};

static int16_t coefficient(uint8_t v)
{
    int16_t c = (int16_t)coefficients[v & 0x7F];
    return (v & 0x80) ? c : (int16_t)-c;
}

// Amplitude: a 5-bit mantissa shifted by a 3-bit exponent.
static int16_t amplitude(uint8_t v)
{
    return (int16_t)((v & 0x1F) << (v >> 5));
}

// Start speaking the frame in the FIFO (whatever it holds), and ask for the
// next.
static void load_frame(Sp0250 *sp)
{
    const uint8_t *f = sp->fifo;
    static const int coefficient_at[6] = {0, 3, 6, 9, 11, 13};
    for (int i = 0; i < 6; i++) {
        sp->filter[i].b = coefficient(f[coefficient_at[i]]);
        sp->filter[i].f = coefficient(f[coefficient_at[i] + 1]);
        sp->filter[i].z1 = sp->filter[i].z2 = 0;
    }
    sp->amp = amplitude(f[2]);
    sp->pitch = f[5];
    sp->repeat = f[8] & 0x3F;
    sp->voiced = f[8] & 0x40;
    sp->fifo_pos = 0;
    sp->pcount = sp->rcount = 0;
}

// One sample, as the chip's 7-bit output (-64..63).
static int next_sample(Sp0250 *sp)
{
    if (sp->rcount >= sp->repeat) {
        if (sp->fifo_pos == 15) {
            load_frame(sp);
        } else {
            // Nothing waiting: carry on in single periods at the same pitch,
            // as the chip does while it waits.
            sp->repeat = 1;
            sp->pcount = sp->rcount = 0;
        }
    }

    // The noise generator, a 15-bit shift register, runs all the time.
    sp->lfsr ^= (uint16_t)((sp->lfsr ^ (sp->lfsr >> 1)) << 15);
    sp->lfsr >>= 1;

    int16_t z0;
    if (sp->voiced) z0 = sp->pcount == 0 ? sp->amp : 0;  // a pulse each period
    else z0 = (sp->lfsr & 1) ? sp->amp : (int16_t)-sp->amp;

    // Each filter adds its last two outputs back in, scaled by its
    // coefficients (16-bit arithmetic, as in the chip).
    for (int i = 0; i < 6; i++) {
        Sp0250Filter *flt = &sp->filter[i];
        z0 = (int16_t)(z0 + ((flt->z1 * flt->f) >> 8) + ((flt->z2 * flt->b) >> 9));
        flt->z2 = flt->z1;
        flt->z1 = z0;
    }

    int out = z0 >> 6;  // 13 bits down to the DAC's 7
    if (out < -64) out = -64;
    if (out > 63) out = 63;

    if (sp->pcount++ == sp->pitch) {
        sp->pcount = 0;
        sp->rcount++;
    }
    return out;
}

static void push(Sp0250 *sp, float v)
{
    if (sp->sample_count == sp->sample_capacity) {
        int cap = sp->sample_capacity ? sp->sample_capacity * 2 : 4096;
        float *p = realloc(sp->samples, (size_t)cap * sizeof *p);
        if (!p) return;
        sp->samples = p;
        sp->sample_capacity = cap;
    }
    sp->samples[sp->sample_count++] = v;
}

void sp0250_run(Sp0250 *sp, uint64_t now)
{
    while (sp->time + sp->cycles_per_sample <= (double)now) {
        sp->time += sp->cycles_per_sample;
        sp->last = sp->current;
        sp->current = (float)next_sample(sp) / 64.0f;
        if (!sp->rendering) continue;
        // Up to the output rate, drawing straight lines between the chip's
        // samples (about 4.4 output samples to each).
        sp->out_phase += sp->out_per_chip;
        int n = (int)sp->out_phase;
        sp->out_phase -= n;
        for (int i = 0; i < n; i++) {
            float t = (float)(i + 1) / (float)n;
            push(sp, sp->last + (sp->current - sp->last) * t);
        }
    }
}

void sp0250_init(Sp0250 *sp, double cpu_hz, double chip_hz, double sample_rate)
{
    memset(sp, 0, sizeof *sp);
    double chip_rate = chip_hz / 312;  // 10,000 samples a second at 3.12 MHz
    sp->cycles_per_sample = cpu_hz / chip_rate;
    sp->out_per_chip = sample_rate / chip_rate;
    sp->lfsr = 0x7FFF;
    load_frame(sp);
}

void sp0250_free(Sp0250 *sp)
{
    free(sp->samples);
    sp->samples = NULL;
    sp->sample_count = sp->sample_capacity = 0;
}

void sp0250_reset(Sp0250 *sp, uint64_t now)
{
    sp0250_run(sp, now);
    load_frame(sp);
}

void sp0250_write(Sp0250 *sp, uint64_t now, uint8_t value)
{
    sp0250_run(sp, now);
    if (sp->fifo_pos < 15) sp->fifo[sp->fifo_pos++] = value;
}

bool sp0250_wants_data(Sp0250 *sp, uint64_t now)
{
    sp0250_run(sp, now);
    return sp->fifo_pos < 15;
}

void sp0250_set_rendering(Sp0250 *sp, bool on, uint64_t now)
{
    sp0250_run(sp, now);
    sp->rendering = on;
    sp->out_phase = 0;
}

int sp0250_samples(Sp0250 *sp, const float **samples)
{
    *samples = sp->samples;
    return sp->sample_count;
}

void sp0250_used_samples(Sp0250 *sp, int count)
{
    if (count > sp->sample_count) count = sp->sample_count;
    memmove(sp->samples, sp->samples + count, (size_t)(sp->sample_count - count) * sizeof *sp->samples);
    sp->sample_count -= count;
}
