// autoplay.h - let the program play the game by itself.
//
// The game keeps the moves it's waiting for in a list in RAM (see
// autoplay.c). Each entry names an input, the windows of time when it
// counts, and an outcome code for each window, but nothing says which
// outcome is good. So whenever a new set of moves opens, the autoplayer
// tries each one on a copy of the machine, runs the copy ahead (much faster
// than real time), and keeps the move after which the hero doesn't lose a
// life. It only plays the joystick and sword; coins, start and Space Ace's
// skill level are up to you.

#ifndef LV_CINE_AUTOPLAY_H
#define LV_CINE_AUTOPLAY_H

#include "games/cinematronics/board.h"
#include "ldp/ldp.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define CINE_AUTOPLAY_MAX_MOVES 16

// A move the game will accept right now: an input (0 = sword, otherwise
// joystick bits 1 up, 2 down, 4 left, 8 right) and the outcome code it
// leads to.
typedef struct {
    uint8_t input;
    uint8_t outcome;
} CineMove;

typedef struct {
    bool verbose;  // print each decision

    // The moves open now, and since when (the game writes a new list an
    // entry at a time, so a set is only judged once it has settled).
    CineMove seen[CINE_AUTOPLAY_MAX_MOVES];
    int seen_count;
    uint64_t seen_since;

    // The moves open when we last decided, so each set is judged once.
    CineMove decided[CINE_AUTOPLAY_MAX_MOVES];
    int decided_count;

    int pressing;         // input being held, or -1
    uint64_t release_at;  // board cycle at which to let go
    int moves_made;

    void *machine;        // the copy to look ahead with, made when first needed
} CineAutoplay;

void cine_autoplay_init(CineAutoplay *ap, bool verbose);
void cine_autoplay_free(CineAutoplay *ap);

// Start afresh after a break (the player took over for a while): judge
// whatever moves are open now, even if they were judged before.
void cine_autoplay_resume(CineAutoplay *ap);

// Call before each stretch of running the board. Decides when a new set of
// moves opens, and holds the chosen input in board->inputs (on top of
// whatever the player is pressing). ldp and player are the board's
// laserdisc player, copied along with the board for looking ahead.
void cine_autoplay_update(CineAutoplay *ap, CineBoard *board, const Ldp *ldp, const Ldv1000 *player);

// The moves the game will accept right now. Returns how many.
int cine_open_moves(const CineBoard *board, CineMove *moves, int max);

#endif
