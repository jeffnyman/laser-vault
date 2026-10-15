// sn76496.h - the Texas Instruments SN76496 sound chip.
//
// The SN76496 and its relatives (SN76489 and others) make the sound in
// many early-1980s arcade boards, home computers and consoles. It is
// simpler than the AY-3-8910: three square-wave tone channels and one noise
// channel, each with its own volume, and no envelopes. The CPU writes to it
// one byte at a time; it can't be read back.
//
// Four registers per channel pair: a 10-bit tone period and a 4-bit
// attenuation for each tone channel (0, 1, 2), and a 3-bit noise setting
// and an attenuation for the noise channel (3). A byte with bit 7 set
// "latches" a register and writes its low four bits:
//
//     1 c c t d d d d    c = channel 0-3, t = 1 for attenuation, 0 for
//                        the tone period (or noise setting); d = data
//
// A byte with bit 7 clear writes the upper six bits of the latched tone
// period:
//
//     0 x d d d d d d
//
// A tone channel's frequency is clock / (32 * period). Attenuation is in
// 2 dB steps, 15 meaning silent. The noise setting picks white noise (bit 2
// set) or a periodic buzz, and its rate: clock / 512, / 1024, / 2048, or
// the rate of tone channel 2 (bits 0-1 = 3).
//
// Like the AY-3-8910 module, writes are stamped with the CPU's clock, and
// sound is generated up to each write before it takes effect.

#ifndef LV_SN76496_H
#define LV_SN76496_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t period[4];      // tone periods; [3] is the noise rate, in the same units
    uint8_t attenuation[4];  // 0 (loudest) to 15 (silent)
    uint8_t noise;           // noise setting (3 bits)
    int latched;             // register the last latch byte chose (channel * 2 + t)

    // Clocks: timestamps are in CPU cycles; the chip steps at chip_hz / 16.
    double ticks_per_cycle;
    double samples_per_tick;
    uint64_t time;
    double tick_carry;

    // Generators.
    uint16_t count[4];
    bool high[4];            // each channel's output: high or low
    uint32_t shift;          // the noise generator's shift register

    // Output.
    bool rendering;
    double mix_sum;
    int mix_count;
    double sample_phase;
    float dc_in, dc_out;
    float *samples;
    int sample_count, sample_capacity;
} Sn76496;

// cpu_hz: rate of the timestamps. chip_hz: the chip's clock. sample_rate:
// output rate. The chip starts silent, not rendering.
void sn76496_init(Sn76496 *sn, double cpu_hz, double chip_hz, double sample_rate);
void sn76496_free(Sn76496 *sn);

void sn76496_write(Sn76496 *sn, uint64_t now, uint8_t byte);

// Generate sound up to time now.
void sn76496_run(Sn76496 *sn, uint64_t now);

// Start or stop generating samples (from time now).
void sn76496_set_rendering(Sn76496 *sn, bool on, uint64_t now);

// Take the samples generated so far (mono, -1..1). Returns how many; the
// pointer stays valid until the next call to any sn76496 function.
int sn76496_take_samples(Sn76496 *sn, const float **samples);

#endif
