// samples.h - recorded sound effects, played when a board asks for them.
//
// Some boards made their sound effects with circuits of their own (Astron
// Belt's separate sound board, for one) that laser-vault doesn't emulate.
// Recordings of the real sounds, as WAV files, stand in for them: the
// board triggers a sound by number, and it plays from the start (again,
// if it was already playing). Sounds that overlap are mixed.
//
// Like the sound chips, triggers are stamped with the CPU's clock, and
// output is generated up to each trigger before it takes effect.

#ifndef LV_SAMPLES_H
#define LV_SAMPLES_H

#include <stdbool.h>
#include <stdint.h>

#define SAMPLES_MAX 16

typedef struct {
    float *data[SAMPLES_MAX];       // each sound, mixed down to mono
    int length[SAMPLES_MAX];        // in samples; 0 if not loaded
    double step[SAMPLES_MAX];       // its rate / the output rate
    double position[SAMPLES_MAX];   // where each is playing, or -1 if not

    int sample_rate;                // output rate
    double cycles_per_sample;       // CPU cycles per output sample
    double next_sample;             // CPU cycle the next output sample is due
    bool rendering;
    float *out;
    int out_count, out_capacity;
} Samples;

// cpu_hz: rate of the timestamps. sample_rate: output rate.
void samples_init(Samples *s, double cpu_hz, int sample_rate);
void samples_free(Samples *s);

// Load sound n from dir/name (a PCM WAV file: 8 or 16 bits, mono or
// stereo, any rate). Returns false if it can't be read.
bool samples_load(Samples *s, int n, const char *dir, const char *name);

// How many sounds are loaded.
int samples_loaded(const Samples *s);

// Play sound n from the start, at time now.
void samples_trigger(Samples *s, uint64_t now, int n);

// Generate sound up to time now.
void samples_run(Samples *s, uint64_t now);

// Start generating output, from time now. (Until then triggers are
// ignored, so a fast boot doesn't save up sounds to play all at once.)
void samples_start(Samples *s, uint64_t now);

// Take what's been generated (mono, -1..1). Returns how many; the pointer
// stays valid until the next call to any samples function.
int samples_take(Samples *s, const float **out);

#endif
