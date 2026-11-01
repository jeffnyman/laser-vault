// gpworld_sound.h - GP World's engine, tyres and crash, made up.
//
// The cabinet's sounds came from analogue circuits on a sound board, which
// the game drives through a few addresses (worked out from its code):
//
//     DA01   eight switches, a 0 bit turning a sound on: bit 0 the engine,
//            2 the coin chime, 3 the starting countdown's beep, 4 the
//            start signal (and the warning that time is running out),
//            5 the tyres squealing. Bit 1 is on through the attract mode
//            and the race; what it does isn't known. Bits 6 and 7 are
//            never seen.
//     DA80   the engine's revs, 0 to 63, inverted (so FF is 0)
//     DAA0   a 3-bit setting for the engine (17 through a race, FF off)
//     DAC0   written as the revs drop at a gear change
//     DAE0   written when the car crashes
//
// Nobody has documented those circuits or recorded what they sounded
// like, so the sounds here are laser-vault's own: an engine whose pitch
// follows the revs, tones for the chime, countdown and signal, a squeal,
// a crash and the thump of a gear change. Writes are stamped with the
// CPU's clock, and the sound is made up to each write first, as for the
// sound chips.

#ifndef LV_SEGA_GPWORLD_SOUND_H
#define LV_SEGA_GPWORLD_SOUND_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double cycles_per_sample;
    double sample_rate;
    double time;               // CPU cycle the sound has been made up to
    bool rendering;

    uint8_t switches;          // DA01 as last written
    int revs;                  // 0-63
    double crash_left;         // seconds of crash still sounding
    double thump_left;         // seconds of gear-change thump still sounding

    // Generators.
    double engine_freq;        // gliding towards the revs' pitch
    double engine_phase, rough_phase;
    double tone_level[3];      // chime, countdown, signal: each fades in and out
    double tone_phase[3];
    double squeal_level, squeal_phase, wobble_phase;
    double engine_level;
    uint32_t noise;            // random numbers for the noisy sounds
    double crash_low, squeal_band_low, squeal_band_mid;  // filter states
    double thump_phase;

    float *samples;
    int sample_count, sample_capacity;
} GpwSound;

void gpw_sound_init(GpwSound *s, double cpu_hz, double sample_rate);
void gpw_sound_free(GpwSound *s);

// The game wrote v to addr (DA01-DAFF).
void gpw_sound_write(GpwSound *s, uint64_t now, uint16_t addr, uint8_t v);

// Make the sound up to time now; start or stop keeping samples from then.
void gpw_sound_run(GpwSound *s, uint64_t now);
void gpw_sound_set_rendering(GpwSound *s, bool on, uint64_t now);

// Take the samples made so far (mono, -1..1); valid until the next call.
int gpw_sound_take(GpwSound *s, const float **samples);

#endif
