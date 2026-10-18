// board.h - Stern's laserdisc game board, which runs Cliff Hanger and Goal to
// Go (both 1983).
//
// A Z80 with a Pioneer PR-8210, a home laserdisc player driven through its
// remote-control socket (see ldp/pr8210.h), and a TMS9128 video chip that
// draws the score and messages over the picture (see video/tms9928.h).
//
//     Z80 at 4 MHz
//     0000-BFFF  ROM (Cliff Hanger uses 32 or 40 KB of it)
//     E000-E7FF  RAM, kept with a battery
//     E800-EFFF  RAM
//
// and the Z80's I/O ports:
//
//     out 44 / in 45  video chip data      out 54 / in 55  video chip control
//     out 46  tone 1 (bit 0), tone 2 (bit 1), overlay (bit 4)
//     in  50-52  the Philips code read off the disc (low, middle, high byte)
//     in  53  acknowledges the interrupt
//     out 60  picks one of seven input banks, which in 62 reads:
//             0 service buttons, 1-4 DIP switches, 5 coins and buttons,
//             6 joystick
//     out 66  bit 0: the wire to the laserdisc player
//     out 6A  the infrared lamp (the same codes, for a cabinet wired that way)
//
// The player can't tell the game anything. Instead a decoder on the board
// reads the Philips codes recorded on lines 17 and 18 of each video field
// (the frame number, while the disc is playing or paused) and, for each
// valid one, interrupts the CPU. The video chip's interrupt, at the end of
// each picture it draws, is wired to the NMI.
//
// Details from MAME's cliffhgr.cpp (Ernesto Corvi), checked against the
// game's code.

#ifndef LV_STERN_BOARD_H
#define LV_STERN_BOARD_H

#include "cpu/z80.h"
#include "ldp/pr8210.h"
#include "video/tms9928.h"

#include <stdbool.h>
#include <stdint.h>

#define STERN_CPU_HZ 4000000
#define STERN_SAMPLE_RATE 44100

// Settings the DIP switches hold, which the main program offers by name.
typedef enum {
    STERN_FREE_PLAY,
    STERN_UNLIMITED_LIVES,
    STERN_QUIET_ATTRACT,  // on: no sound in the attract mode ("demo sounds" off)
} SternSetting;

// Inputs are active low on the real board; here, true means pressed. The
// control panel has a joystick and buttons for Cliff's hands and feet; the
// feet buttons double as the start buttons.
typedef struct {
    bool up, down, left, right, hand, feet;
    bool start1, start2, coin1, coin2;
} SternInputs;

// Where a game keeps its moves, for autoplay (see board.c).
typedef struct {
    uint16_t check;        // the address of its move check, 0 if not known
    uint8_t move_at;       // where in a move's record the right move is
    uint8_t opens_at, closes_at;  // and the window's first and last frames (BCD)
    bool stick_as_wired;   // the move's stick bits are the board's (up, right, down, left);
                           // else Cliff Hanger's order (left, down, right, up)
    bool both_buttons;     // F in the top bits: hands and feet together (else either)
} SternMoveRecords;

#define STERN_MAX_SAMPLES 4096

typedef struct {
    Z80 cpu;
    uint8_t rom[0xC000];
    uint8_t ram[0x1000];        // E000-EFFF
    uint8_t dsw[4];             // DIP switch banks 1-4
    int coins_per_credit;       // at the factory coinage (set 2 has no 1 coin 1 credit)
    bool goal_to_go;            // else Cliff Hanger
    uint32_t chapter_code;      // the disc's chapter code, for Goal to Go's side check (see board.c)

    // Autoplay (see board.c): where the game checks a move, the move it
    // checked last, and what autoplay is pressing.
    bool autoplay;
    SternMoveRecords moves;     // where the game keeps its moves; check 0 if not known
    uint16_t move_record;       // the move's record in the game's script, or 0
    int move_seen;              // the disc frame the game started checking it on
    SternInputs auto_inputs;

    SternInputs inputs;
    Pr8210 *player;
    Tms9928 vdp;
    bool vdp_irq;               // the video chip's interrupt line, last seen
    uint8_t bank;               // input bank picked by port 60
    uint8_t misc;               // last byte written to port 46
    uint32_t philips_code;      // the code the decoder last read

    // The two tones: 555 timers, about 696 Hz and 327 Hz.
    bool rendering;
    double next_sample;         // CPU cycle the next sample is due
    double phase[2];
    float filtered, dc_in, dc_out;
    float samples[STERN_MAX_SAMPLES];
    int sample_count;

    uint64_t cycles;            // CPU clock cycles since power-on
    uint64_t fields;            // video fields since power-on
    uint64_t next_field;        // cycle the next field starts on
    int next_event;             // which event in the field comes next
    uint64_t next_event_at;     // and when
} SternBoard;

// Does path hold a ROM set for this board? Quiet: for choosing a board.
bool stern_recognizes(const char *path);

// Load the ROMs from a zip file or a folder, checking each chip's checksum,
// and set the DIP switches as the factory did. Returns false (after
// printing why) if the set can't be run.
bool stern_load_roms(SternBoard *board, const char *path);

// Can autoplay play this ROM set? (Cliff Hanger set 1, and Goal to Go.) With it on, the
// board presses each move's control itself, as well as the player's.
bool stern_can_autoplay(const SternBoard *board);
void stern_set_autoplay(SternBoard *board, bool on);

void stern_set(SternBoard *board, SternSetting setting, bool on);
bool stern_get(const SternBoard *board, SternSetting setting);

const char *stern_game_name(const SternBoard *board);

// Power on, connected to a PR-8210. (The DIP switches stay as they are.)
void stern_reset(SternBoard *board, Pr8210 *player);

// Run the board for (at least) the given number of CPU cycles.
void stern_run(SternBoard *board, uint64_t cycles);

// The tones: switch making samples on, and take what's been made.
void stern_start_sound(SternBoard *board);
int stern_take_samples(SternBoard *board, const float **samples);

// Draw the video chip's picture as it looks now (TMS9928_W x TMS9928_H, as
// 0xAARRGGBB, alpha 0 where the disc's picture shows through).
void stern_draw_overlay(const SternBoard *board, uint32_t *pixels);

#endif
