// dlair_autoplay.h - let the program play Dragon's Lair by itself.
//
// The game keeps the moves it's waiting for in a list in RAM (see
// dlair_autoplay.c). Each entry names an input, the windows of time when it
// counts, and an outcome code for each window, but nothing says which
// outcome is good. So whenever a new set of moves opens, the autoplayer
// tries each one on a copy of the machine, runs the copy ahead (much faster
// than real time), and keeps the move after which Dirk doesn't lose a life.
// It only plays the joystick and sword; coins and start are up to you.

#ifndef LV_DLAIR_AUTOPLAY_H
#define LV_DLAIR_AUTOPLAY_H

#include "games/dlair/dlair.h"
#include "ldp/ldp.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define DLAIR_AUTOPLAY_MAX_MOVES 16

// A move the game will accept right now: an input (0 = sword, otherwise
// joystick bits 1 up, 2 down, 4 left, 8 right) and the outcome code it
// leads to.
typedef struct {
    uint8_t input;
    uint8_t outcome;
} DlairMove;

typedef struct {
    bool verbose;  // print each decision

    // The moves open now, and since when (the game writes a new list an
    // entry at a time, so a set is only judged once it has settled).
    DlairMove seen[DLAIR_AUTOPLAY_MAX_MOVES];
    int seen_count;
    uint64_t seen_since;

    // The moves open when we last decided, so each set is judged once.
    DlairMove decided[DLAIR_AUTOPLAY_MAX_MOVES];
    int decided_count;

    int pressing;         // input being held, or -1
    uint64_t release_at;  // board cycle at which to let go
    int moves_made;

    void *machine;        // the copy to look ahead with, made when first needed
} DlairAutoplay;

void dlair_autoplay_init(DlairAutoplay *ap, bool verbose);
void dlair_autoplay_free(DlairAutoplay *ap);

// Start afresh after a break (the player took over for a while): judge
// whatever moves are open now, even if they were judged before.
void dlair_autoplay_resume(DlairAutoplay *ap);

// Call before each stretch of running the board. Decides when a new set of
// moves opens, and holds the chosen input in board->inputs (on top of
// whatever the player is pressing). ldp and player are the board's
// laserdisc player, copied along with the board for looking ahead.
void dlair_autoplay_update(DlairAutoplay *ap, Dlair *board, const Ldp *ldp, const Ldv1000 *player);

// The moves the game will accept right now. Returns how many.
int dlair_open_moves(const Dlair *board, DlairMove *moves, int max);

#endif
