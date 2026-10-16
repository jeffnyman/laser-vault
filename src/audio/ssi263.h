// ssi263.h - the Silicon Systems SSI-263 speech synthesizer.
//
// The SSI-263 speaks phonemes: the CPU writes one sound of speech at a time
// (one of 64: vowels, consonants, pauses), with how long to hold it, the
// pitch (inflection), the speaking rate, how quickly to move from one
// sound to the next (articulation) and the volume. When it's ready for the
// next phoneme it lowers its A/R line, which interrupts the CPU.
//
//     register 0  bits 5-6 duration, bits 0-5 phoneme
//     register 1  inflection, bits 3-10
//     register 2  bits 4-7 rate, bit 3 inflection bit 11, bits 0-2
//                 inflection bits 0-2
//     register 3  bit 7 control, bits 4-6 articulation, bits 0-3 amplitude
//     register 4  filter frequency
//
// Setting the control bit and clearing it again latches the duration bits
// as the mode: 0 and 1 request the next phoneme when this one ends, 2 at
// the end of each frame, and 3 never. Reading returns A/R (inverted) in
// bit 7. Register layout and timing from MAME's ssi263hle.cpp.
//
// How the real chip makes its sounds isn't documented, so the voice here
// is a formant synthesizer of the classic kind, driven by what the game
// sends: a buzz at the pitch the inflection sets (for voiced sounds) and a
// hiss (for the others), shaped by three resonators into each phoneme's
// formants, the peaks in the spectrum that make one vowel sound unlike
// another. The formants glide from one phoneme to the next at the speed
// the articulation sets. It's not the chip's voice, but it says the same
// things, at the same times.

#ifndef LV_SSI263_H
#define LV_SSI263_H

#include <stdbool.h>
#include <stdint.h>

#define SSI263_LOG 64

// A two-pole resonator (one formant).
typedef struct {
    double a, b, c;   // coefficients
    double y1, y2;    // last two outputs
} Ssi263Resonator;

typedef struct {
    double cpu_hz;
    uint8_t phoneme, duration, rate, articulation, amplitude, filter;
    uint16_t inflection;
    bool control;
    int mode;
    bool requesting;       // A/R low: ready for the next phoneme
    uint64_t request_at;   // when it will be, or 0

    // The most recent phonemes, for watching what's said.
    uint8_t log[SSI263_LOG];
    unsigned log_count;

    // The synthesizer: what it's sounding now, gliding towards the
    // current phoneme's targets.
    int sample_rate;
    double cycles_per_sample;
    double next_sample;    // CPU cycle the next sample is due
    bool rendering;
    double f[3], f_target[3];       // formant frequencies, Hz
    double voice, voice_target;     // loudness of the buzz, 0-1
    double hiss, hiss_target;       // loudness of the hiss, 0-1
    double hiss_freq;               // centre of the hiss's band, Hz
    bool hiss_shaped;               // the hiss goes through the formants (H sounds)
    int kind;                       // the phoneme's kind (see ssi263.c)
    double phoneme_time;            // samples since this phoneme began
    double phase;                   // through the current pitch period, 0-1
    double last_glottal;            // for differentiating the buzz
    uint32_t noise;                 // noise generator state
    double level;                   // overall loudness, gliding
    Ssi263Resonator formant[3], hiss_band;
    float *samples;
    int sample_count, sample_capacity;
} Ssi263;

// cpu_hz: rate of the timestamps. sample_rate: output rate.
void ssi263_init(Ssi263 *ssi, double cpu_hz, int sample_rate);
void ssi263_free(Ssi263 *ssi);

void ssi263_write(Ssi263 *ssi, uint64_t now, int reg, uint8_t value);
uint8_t ssi263_read(Ssi263 *ssi, uint64_t now);

// Is A/R low (asking for the next phoneme) at time now?
bool ssi263_requesting(Ssi263 *ssi, uint64_t now);

// Generate sound up to time now; start generating (from time now); take
// what's been generated (mono, -1..1; the pointer stays valid until the
// next call to any ssi263 function).
void ssi263_run(Ssi263 *ssi, uint64_t now);
void ssi263_start(Ssi263 *ssi, uint64_t now);
int ssi263_take_samples(Ssi263 *ssi, const float **samples);

// The phoneme's name, as the data sheet has it ("PA", "E", "AY", ...).
const char *ssi263_phoneme_name(int phoneme);

#endif
