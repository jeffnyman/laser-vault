// vip9500sg.h - Sega's laserdisc player (the Hitachi-built VIP9500SG), as
// a game sees it.
//
// Sega's versions of Astron Belt, Galaxy Ranger and Cobra Command drive
// this player instead of Pioneer's LD-V1000, over a serial line (an Intel
// 8251 on the game's board; see games/sega/board.c). Each command is one
// byte, and the player answers each with the same byte plus 80:
//
//     25  play           29  pause (it's "step back", used as pause)
//     2F  stop           53  play at normal speed, with sound
//     4C, 4D  frame counter on, off         68  reset
//     6B  report the frame: answered with 6B and the frame number in two
//         bytes, high byte first
//
// A search is 2B, the frame number as characters ("1", "2", ... 30-39),
// then 41 ("enter"); when the player arrives it sends 41 B0. A skip forward
// is 46, a number of frames the same way, and 41; it answers 41 C6.
//
// Commands and replies from the Daphne emulator's vip9500sg.cpp, which
// notes it does just enough to run Astron Belt; anything else is
// answered as Daphne answers it.

#ifndef LV_VIP9500SG_H
#define LV_VIP9500SG_H

#include "ldp/ldp.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    Ldp *ldp;
    double clock_hz;      // rate of the "now" timestamps
    uint64_t last;        // time the player was last brought up to date

    uint8_t replies[64];  // bytes waiting to go back to the game
    int reply_head, reply_count;
    char digits[8];       // a number being entered
    int digit_count;
    int pending;          // 0, or the command (2B, 46) that 41 will finish
    bool searching;       // a search is under way; 41 B0 when it ends
} Vip9500sg;

// Attach to a player model (already set up with ldp_init) and park it.
void vip9500sg_init(Vip9500sg *vip, Ldp *ldp, double clock_hz);

// Bring the player up to time now.
void vip9500sg_update(Vip9500sg *vip, uint64_t now);

// A byte from the game.
void vip9500sg_write(Vip9500sg *vip, uint64_t now, uint8_t byte);

// Is a reply waiting? Take it (0 if none).
bool vip9500sg_has_reply(Vip9500sg *vip, uint64_t now);
uint8_t vip9500sg_read(Vip9500sg *vip, uint64_t now);

// Can the disc's audio be heard? (While it plays at normal speed.)
bool vip9500sg_audio_on(const Vip9500sg *vip, int channel);

#endif
