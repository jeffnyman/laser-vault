// machine.h - a laserdisc game machine, whichever board it is.
//
// The main program runs every game the same way: load the ROMs, connect the
// board to an LD-V1000, run the CPU for as long as real time has passed,
// pass in the controls, and take out the sound and anything the board draws
// over the picture. This is that common ground; each call passes on to the
// board the ROMs are for:
//
//     Cinematronics (games/cinematronics)  Dragon's Lair, Space Ace
//     Universal System 1 (games/universal) Super Don Quix-ote
//
// Things only one board has (the Cinematronics score displays, autoplay)
// are reached through its own board pointer.

#ifndef LV_MACHINE_H
#define LV_MACHINE_H

#include "games/cinematronics/board.h"
#include "games/universal/board.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MACHINE_CINEMATRONICS,
    MACHINE_UNIVERSAL,
} MachineKind;

typedef struct {
    MachineKind kind;
    CineBoard *cine;  // when kind is MACHINE_CINEMATRONICS
    UniBoard *uni;    // when kind is MACHINE_UNIVERSAL
} Machine;

// The control panel: true means pressed. Games use what they have; Space Ace
// alone has the three skill buttons.
typedef struct {
    bool up, down, left, right, button;
    bool start1, start2, coin1, coin2;
    bool skill[3];  // Space Ace: Cadet, Captain, Ace
} Controls;

// DIP switch settings the main program offers as options. Each board knows
// which switch holds each; a game that lacks one ignores it.
typedef enum {
    MACHINE_FREE_PLAY,
    MACHINE_UNLIMITED_LIVES,
    MACHINE_QUIET_ATTRACT,
} MachineSetting;

// Work out which board the ROMs at path are for and load them. Returns
// false (after printing why) if no board can run them.
bool machine_load_roms(Machine *m, const char *path);
void machine_free(Machine *m);

const char *machine_game_name(const Machine *m);
double machine_cpu_hz(const Machine *m);
uint64_t machine_cycles(const Machine *m);

// The rate the disc's frame numbers count at: 23.976 for the Cinematronics
// games (their frame numbers count film frames; see video/disc_video.h),
// 29.97 (NTSC video frames) for the others.
double machine_disc_fps(const Machine *m);

// Power on, connected to an LD-V1000 (set up with ldv1000_init at
// machine_cpu_hz).
void machine_reset(Machine *m, Ldv1000 *player);
void machine_run(Machine *m, uint64_t cycles);
void machine_set_controls(Machine *m, const Controls *c);

// The DIP switch banks (1 or 2).
uint8_t *machine_dsw(Machine *m, int bank);
void machine_set(Machine *m, MachineSetting setting, bool on);
bool machine_get(const Machine *m, MachineSetting setting);

// The board's sound chip: its sample rate, switching it on (until then it
// makes no samples), and taking what it has made up to now.
int machine_sample_rate(const Machine *m);
void machine_start_sound(Machine *m);
int machine_take_samples(Machine *m, const float **samples);

// Graphics drawn over the picture, if the board has any: its size in
// pixels, and the part of the picture it covers (as fractions of the
// picture's width and height).
typedef struct {
    int width, height;
    float x, y, w, h;
} OverlayLayout;
bool machine_overlay_layout(const Machine *m, OverlayLayout *layout);

// A number that changes whenever the overlay may look different.
unsigned machine_overlay_serial(const Machine *m);
void machine_draw_overlay(const Machine *m, uint32_t *pixels);  // 0xAARRGGBB

#endif
