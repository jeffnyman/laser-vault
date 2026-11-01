// sp0250.h - the General Instrument SP0250 speech synthesizer.
//
// The SP0250 (1981) speaks by linear predictive coding: rather than playing
// recorded sound, it models a voice. A source, either a buzz of pulses at a
// pitch (for voiced sounds, like vowels) or noise (for hissing ones, like
// "s"), goes through six filters in a row, each a resonance the shape of the
// mouth and throat would make. The CPU feeds it frames of 15 bytes, each
// lasting a few hundredths of a second:
//
//     0, 1    filter 1's two coefficients (B, F)
//     2       amplitude (5 bits, times 2 to the power of the top 3)
//     3, 4    filter 2
//     5       pitch: the buzz's period, in samples
//     6, 7    filter 3
//     8       bits 0-5 how many pitch periods the frame lasts; bit 6 voiced
//     9-14    filters 4, 5 and 6
//
// A coefficient byte picks one of 128 values from a table inside the chip,
// bit 7 its sign. The chip makes 10,000 samples a second (its 3.12 MHz
// clock / 312), 7 bits each. It holds one frame waiting while it speaks
// another, and signals (DRQ) while it wants the next byte.
//
// Details from the datasheet's description and MAME's sp0250.cpp (Olivier
// Galibert), which took the noise generator from a dump of a real chip.
//
// As with the AY-3-8910, everything is stamped with the CPU's clock: before
// each write or read, the chip runs up to that moment.

#ifndef LV_SP0250_H
#define LV_SP0250_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t f, b;    // coefficients for the last output and the one before
    int16_t z1, z2;  // the last two outputs
} Sp0250Filter;

typedef struct {
    uint8_t fifo[15];  // the next frame, as it arrives
    int fifo_pos;      // bytes of it so far (15: full, waiting)

    // The frame being spoken.
    Sp0250Filter filter[6];
    int16_t amp;
    uint8_t pitch, repeat;
    bool voiced;
    int pcount, rcount;  // samples into this pitch period; periods so far
    uint16_t lfsr;       // noise generator

    // Clocks: timestamps are in CPU cycles.
    double cycles_per_sample;  // CPU cycles per chip sample
    double time;               // CPU cycle the chip has been run up to

    // Output, resampled to the output rate.
    bool rendering;
    double out_per_chip;       // output samples per chip sample
    double out_phase;          // output samples owed, fractional
    float last, current;       // the last two chip samples, -1..1
    float *samples;
    int sample_count, sample_capacity;
} Sp0250;

// cpu_hz: rate of the timestamps. chip_hz: the chip's clock (3.12 MHz).
// sample_rate: output rate. Starts silent, not rendering.
void sp0250_init(Sp0250 *sp, double cpu_hz, double chip_hz, double sample_rate);
void sp0250_free(Sp0250 *sp);

// The RESET pin: the chip takes up whatever frame it holds and starts over.
void sp0250_reset(Sp0250 *sp, uint64_t now);

// Give it the next byte of a frame (ignored if it already holds a whole one).
void sp0250_write(Sp0250 *sp, uint64_t now, uint8_t value);

// DRQ: does it want more bytes?
bool sp0250_wants_data(Sp0250 *sp, uint64_t now);

// Run up to time now, and start or stop making samples from then.
void sp0250_run(Sp0250 *sp, uint64_t now);
void sp0250_set_rendering(Sp0250 *sp, bool on, uint64_t now);

// The samples made so far (mono, -1..1), valid until the next call; then
// say how many of them were used, and the rest wait for next time.
int sp0250_samples(Sp0250 *sp, const float **samples);
void sp0250_used_samples(Sp0250 *sp, int count);

#endif
