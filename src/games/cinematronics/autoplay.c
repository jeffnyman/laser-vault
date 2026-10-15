#include "games/cinematronics/autoplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The game's list of moves
// ---------------------------------------------------------------------------
//
// Found by watching Dragon's Lair rev F2 run; the fan sets for Dragon's Lair
// and Space Ace use the same list.
// While a scene plays, RAM from A0E0 holds 16-byte entries, one per input
// the game is watching for:
//
//   byte 0      bit 7: entry in use (a clear bit ends the list);
//               bits 0-3: the input (0 = sword, else joystick bits)
//   byte 1      bits 0-1: how many time windows follow (up to 3);
//               bits 2-7: for each window, 2 bits picking an outcome
//   bytes 2-5   outcome codes (FF = nothing happens)
//   byte 12     a delay before the windows start
//   bytes 13-15 the windows' lengths, in timer ticks (about 30 a second).
//               Only the first that isn't zero counts down: that's the
//               window open now.
//
// When the player presses an input whose entry has a window open, the game
// takes that window's outcome: the next stretch of the disc to play, a
// death scene or the way on.

#define MOVE_LIST 0x0E0  // A0E0, as an offset into the 2 KB of RAM
#define MOVE_ENTRY_SIZE 16
#define MOVE_LIST_MAX 16

int cine_open_moves(const CineBoard *b, CineMove *moves, int max)
{
    int n = 0;
    for (int e = 0; e < MOVE_LIST_MAX && n < max; e++) {
        const uint8_t *x = b->ram + MOVE_LIST + MOVE_ENTRY_SIZE * e;
        if (!(x[0] & 0x80)) break;
        if (x[12] != 0) continue;  // windows not started yet
        int windows = x[1] & 3;
        for (int w = 0; w < windows; w++) {
            if (x[13 + w] == 0) continue;  // this window is over
            uint8_t outcome = x[2 + ((x[1] >> (2 + 2 * w)) & 3)];
            if (outcome != 0xFF) moves[n++] = (CineMove){x[0] & 0x0F, outcome};
            break;
        }
    }
    return n;
}

static bool same_moves(const CineMove *a, int na, const CineMove *b, int nb)
{
    return na == nb && memcmp(a, b, (size_t)na * sizeof *a) == 0;
}

// ---------------------------------------------------------------------------
// Looking ahead
// ---------------------------------------------------------------------------

#define PRESS_SECONDS 0.1  // how long to hold a move (a few timer ticks)
#define SETTLE_CYCLES (CINE_CPU_HZ / 100)  // 10 ms
#define LOOKAHEAD_SECONDS 6.0
#define SLICE_CYCLES (CINE_CPU_HZ / 1000)

// A copy of everything that decides what the game does next.
typedef struct {
    CineBoard board;
    Ldp ldp;
    Ldv1000 player;
} Machine;

static void copy_machine(Machine *m, const CineBoard *board, const Ldp *ldp, const Ldv1000 *player)
{
    m->board = *board;
    m->ldp = *ldp;
    m->player = *player;
    // Point the copy's parts at each other instead of at the originals.
    m->player.ldp = &m->ldp;
    m->board.laserdisc = cine_ldv1000_link(&m->player);
    m->board.cpu.bus.ctx = &m->board;
    // A lost life is how the copy shows a move was fatal, so it plays with
    // lives that run out even when the real game has unlimited ones.
    cine_set_now(&m->board, CINE_UNLIMITED_LIVES, false);
    // The copy makes no sound, and mustn't touch the original's samples.
    m->board.ay.rendering = false;
    m->board.ay.samples = NULL;
    m->board.ay.sample_count = m->board.ay.sample_capacity = 0;
}

static void set_input(CineInputs *in, int input, bool down)
{
    if (input == 0) {
        in->sword = down;
        return;
    }
    if (input & 1) in->up = down;
    if (input & 2) in->down = down;
    if (input & 4) in->left = down;
    if (input & 8) in->right = down;
}

// Lives left, from the score display (digit 6 for player 1, 7 for player
// 2; blank counts as none).
static int lives(const CineBoard *b)
{
    int n = 0;
    for (int i = 6; i <= 7; i++)
        if (b->digits[i] <= 9) n += b->digits[i];
    return n;
}

// The entries' fixed parts (bytes 0-11, not the timers). The game rewrites
// them when it takes a move.
static void list_headers(const CineBoard *b, uint8_t out[MOVE_LIST_MAX * 12])
{
    memset(out, 0, MOVE_LIST_MAX * 12);
    for (int e = 0; e < MOVE_LIST_MAX; e++) {
        const uint8_t *x = b->ram + MOVE_LIST + MOVE_ENTRY_SIZE * e;
        memcpy(out + 12 * e, x, 12);
        if (!(x[0] & 0x80)) break;
    }
}

typedef enum { DIES, SURVIVES, IGNORED } Verdict;

// Press the input on the copy and run it ahead:
//
//   - Dirk loses a life: the move is fatal. (After a fatal move the list
//     has no open moves until the death scene ends and the life is gone.)
//   - The game takes the move, rewriting the list, and a new set of moves
//     opens: he survived. The game may take its time: it often stores the
//     move and only acts on it when the current stretch of the disc ends.
//   - Neither, by the time limit: the game ignored the move (pressed before
//     the scene's windows started counting, say).
//
// A window running out on its own changes the open set too, but leaves the
// list as it was, so it doesn't count as moving on.
static Verdict try_move(Machine *m, int input, const CineMove *open, int open_count)
{
    // The copy doesn't need a real player at the controls.
    memset(&m->board.inputs, 0, sizeof m->board.inputs);
    int start_lives = lives(&m->board);
    uint8_t start_headers[MOVE_LIST_MAX * 12], headers[MOVE_LIST_MAX * 12];
    list_headers(&m->board, start_headers);
    bool taken = false;
    uint64_t release = m->board.cycles + (uint64_t)(PRESS_SECONDS * CINE_CPU_HZ);
    uint64_t end = m->board.cycles + (uint64_t)(LOOKAHEAD_SECONDS * CINE_CPU_HZ);
    while (m->board.cycles < end) {
        set_input(&m->board.inputs, input, m->board.cycles < release);
        cine_run(&m->board, SLICE_CYCLES);
        ldv1000_update(&m->player, m->board.cycles);
        if (lives(&m->board) < start_lives) return DIES;
        if (!taken) {
            list_headers(&m->board, headers);
            taken = memcmp(headers, start_headers, sizeof headers) != 0;
        }
        if (taken && m->board.cycles >= release) {
            CineMove now[CINE_AUTOPLAY_MAX_MOVES];
            int n = cine_open_moves(&m->board, now, CINE_AUTOPLAY_MAX_MOVES);
            if (n > 0 && !same_moves(now, n, open, open_count)) return SURVIVES;
        }
    }
    return taken ? SURVIVES : IGNORED;
}

static const char *input_name(int input)
{
    static const char *names[16] = {
        "sword", "up", "down", "up+down", "left", "up+left", "down+left", "?",
        "right", "up+right", "down+right", "?", "?", "?", "?", "?",
    };
    return names[input & 15];
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

void cine_autoplay_init(CineAutoplay *ap, bool verbose)
{
    memset(ap, 0, sizeof *ap);
    ap->verbose = verbose;
    ap->pressing = -1;
}

void cine_autoplay_free(CineAutoplay *ap)
{
    free(ap->machine);
    ap->machine = NULL;
}

void cine_autoplay_resume(CineAutoplay *ap)
{
    ap->seen_count = 0;
    ap->decided_count = 0;
    ap->pressing = -1;
}

void cine_autoplay_update(CineAutoplay *ap, CineBoard *board, const Ldp *ldp, const Ldv1000 *player)
{
    // Holding a move: keep it down until it's time to let go.
    if (ap->pressing >= 0) {
        bool down = board->cycles < ap->release_at;
        set_input(&board->inputs, ap->pressing, down);
        if (down) return;
        ap->pressing = -1;
    }

    // Moves only count while the scene plays (their windows count down
    // then), and the game writes a new list one entry at a time, so wait
    // until a set has been open for a moment before judging it.
    CineMove open[CINE_AUTOPLAY_MAX_MOVES];
    int n = ldp->state == LDP_PLAYING ? cine_open_moves(board, open, CINE_AUTOPLAY_MAX_MOVES) : 0;
    if (!same_moves(open, n, ap->seen, ap->seen_count)) {
        memcpy(ap->seen, open, sizeof open);
        ap->seen_count = n;
        ap->seen_since = board->cycles;
        return;
    }
    if (board->cycles - ap->seen_since < SETTLE_CYCLES) return;
    if (same_moves(open, n, ap->decided, ap->decided_count)) return;
    memcpy(ap->decided, open, sizeof open);
    ap->decided_count = n;
    if (n == 0) return;

    // Try each different outcome once, on a fresh copy each time.
    if (!ap->machine) ap->machine = malloc(sizeof(Machine));  // big: it holds the ROM
    Machine *m = ap->machine;
    for (int i = 0; i < n; i++) {
        bool tried = false;
        for (int j = 0; j < i; j++) tried = tried || open[j].outcome == open[i].outcome;
        if (tried) continue;
        copy_machine(m, board, ldp, player);
        if (try_move(m, open[i].input, open, n) == SURVIVES) {
            if (ap->verbose)
                printf("%8.3f s  autoplay: %s at frame %d\n",
                       (double)board->cycles / CINE_CPU_HZ, input_name(open[i].input), ldp->frame);
            ap->pressing = open[i].input;
            ap->release_at = board->cycles + (uint64_t)(PRESS_SECONDS * CINE_CPU_HZ);
            ap->moves_made++;
            set_input(&board->inputs, ap->pressing, true);
            return;
        }
    }
    // Every move open now is fatal: wait for the next window.
}
