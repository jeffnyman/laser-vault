// machine.h - a laserdisc game machine, whichever board it is.
//
// The main program runs every game the same way: load the ROMs, connect the
// board to its laserdisc player, run the CPU for as long as real time has
// passed, pass in the controls, and take out the sound and anything the
// board draws over the picture. This is that common ground; each call passes
// on to the board the ROMs are for:
//
//     Cinematronics (games/cinematronics)  Dragon's Lair, Space Ace    LD-V1000
//     Universal System 1 (games/universal) Super Don Quix-ote          LD-V1000
//     Funai (games/funai)                  Esh's Aurunmilla            LD-V1000
//     Stern (games/stern)                  Cliff Hanger                PR-8210
//     Konami (games/konami)                Badlands                    LD-V1000
//     Sega (games/sega)                    Astron Belt, Galaxy Ranger  LD-V1000
//
// Things only one board has (the Cinematronics score displays, autoplay)
// are reached through its own board pointer.

#ifndef LV_MACHINE_H
#define LV_MACHINE_H

#include "games/cinematronics/board.h"
#include "games/funai/board.h"
#include "games/konami/board.h"
#include "games/sega/board.h"
#include "games/stern/board.h"
#include "games/universal/board.h"
#include "ldp/ldv1000.h"
#include "ldp/pr8210.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MACHINE_CINEMATRONICS,
    MACHINE_UNIVERSAL,
    MACHINE_FUNAI,
    MACHINE_STERN,
    MACHINE_KONAMI,
    MACHINE_SEGA,
} MachineKind;

typedef struct {
    MachineKind kind;
    CineBoard *cine;  // when kind is MACHINE_CINEMATRONICS
    UniBoard *uni;    // when kind is MACHINE_UNIVERSAL
    FunaiBoard *funai;  // when kind is MACHINE_FUNAI
    SternBoard *stern;  // when kind is MACHINE_STERN
    KonamiBoard *konami;  // when kind is MACHINE_KONAMI
    SegaBoard *sega;      // when kind is MACHINE_SEGA

    // The laserdisc player the board drives, which drives the player model.
    Ldv1000 ldv1000;  // all but Stern's board
    Pr8210 pr8210;    // Stern's board
} Machine;

// The control panel: true means pressed. Games use what they have: Esh's
// Aurunmilla alone has a second button (the others take either as their
// one button), Space Ace alone the three skill buttons.
typedef struct {
    bool up, down, left, right, button, button2;
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

// Load recordings of sound effects the board can't make itself (Astron
// Belt's sound board) from dir. Sets *wanted if the board uses any; returns
// how many were found.
int machine_load_samples(Machine *m, const char *dir, bool *wanted);

// The name of one of those recordings, for messages ("ab_fire.wav").
const char *machine_sample_example(const Machine *m);

const char *machine_game_name(const Machine *m);
double machine_cpu_hz(const Machine *m);
uint64_t machine_cycles(const Machine *m);

// Which player the board drives: true for the PR-8210, false for the LD-V1000.
bool machine_uses_pr8210(const Machine *m);

// The rate the disc's frame numbers count at: 23.976 for the Cinematronics
// games (their frame numbers count film frames; see video/disc_video.h),
// 29.97 (NTSC video frames) for the others.
double machine_disc_fps(const Machine *m);

// Power on, with the board's laserdisc player driving the player model
// (set up with ldp_init).
void machine_reset(Machine *m, Ldp *ldp);
void machine_run(Machine *m, uint64_t cycles);

// Bring the laserdisc player up to the board's time.
void machine_update_player(Machine *m);

// Can the disc's audio channel 0 or 1 be heard? (The game can switch each
// off, and some ways of playing are silent.)
bool machine_audio_on(const Machine *m, int channel);
void machine_set_controls(Machine *m, const Controls *c);

// A DIP switch bank (from 1), or NULL for one the board doesn't have. Most
// have two banks, Cliff Hanger four; Esh's Aurunmilla keeps its settings in
// battery-backed memory instead.
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
