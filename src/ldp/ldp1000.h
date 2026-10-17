// ldp1000.h - Sony's LDP-1000 laserdisc player, as a game sees it.
//
// Data East's laserdisc board (Bega's Battle, its own Cobra Command, Road
// Blaster) drives this player over a serial line, through a 6850 on the
// game's board (see games/dataeast/board.c). Each command is one byte, and
// the player answers most of them with 0A ("acknowledged") a millisecond or
// a few later; the game waits for that before it sends the next.
//
//     3A  play           4F  still (pause)
//     30-39  the digits "0" to "9" of a number
//     43  search: a frame number follows, then 40 ("enter"); when the
//         player arrives at the frame it sends 01 ("completed")
//     44  repeat: the frame to play to, 40, how many times (none means
//         once, 0 means forever), 40; it plays from where it is to that
//         frame, sending 01 at the end of each pass, then holds the frame
//     56  clear ("C.L."): cancel the entry, or a search under way
//     60  report the frame: answered with five digits, "01234"
//     24, 25  audio muted, unmuted     46-49  audio channel 1 or 2 on, off
//
// An error, such as a search to a frame that isn't on the disc, is
// answered 02. Commands and timings from the Daphne emulator's ldp1000.cpp,
// which took the timings from Sony's LDP-1000A programming manual.
//
// The LDP-1450, which Dragon's Lair II drives (see games/leland/board.c),
// takes the same commands and more, among them:
//
//     67  report the status: five bytes, the first 80 when the player's
//         ready, the last 01 while playing, 20 when still
//     80  the text display ("user index"), then:
//         00 x y mode    where it goes: x across in 128ths of the
//                        picture's width, y down in 64ths of its height;
//                        mode bits 0-1 and 2-3 the characters' width and
//                        height (16, 32, 48 or 64 pixels of 720 by 480),
//                        bit 4 three lines of ten characters (else one
//                        line), bits 5-6 1 for a shadow, 3 on a grey box,
//                        bit 7 on a blue screen
//         01 n text 1A   characters to put at position n (of 32)
//         02 n           where a single line starts
//     81, 82  text shown, hidden
//
// These answer nothing. Its 24 and 25 switch both audio channels off and
// on, as 46-49 switch each. How the text looks is from MAME's ldp1450hle.cpp;
// the characters here are the PC's.

#ifndef LV_LDP1000_H
#define LV_LDP1000_H

#include "ldp/ldp.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    Ldp *ldp;
    double clock_hz;  // rate of the "now" timestamps
    uint64_t last;    // time the player was last brought up to date

    uint8_t replies[64];  // bytes waiting to go back to the game
    int reply_head, reply_count;
    uint64_t reply_at;    // when the next one may go: a command takes a while

    char digits[8];  // a number being entered
    int digit_count;
    int entering;    // 0, or the command (43, 44) that 40 will finish
    bool searching;  // a search is under way; 01 when it ends
    bool aborting;   // ... and was cleared, so no 01
    int queued_search;  // a search sent while one was being cleared (-1: none)

    bool repeating;  // a repeat is under way
    int repeat_start, repeat_end, repeat_times;  // repeat_times -1: forever

    bool muted;
    bool channel_on[2];

    // An LDP-1450's text display.
    bool ldp1450;
    bool text_on;
    uint8_t text_x, text_y, text_mode, text_start;
    uint8_t text[32];
    int text_state;  // where in an 80 command the next byte goes (0: not in one)
    int text_pos;
    unsigned text_serial;  // changes whenever the text may look different
} Ldp1000;

// The size the text display is drawn at.
#define LDP1450_TEXT_W 720
#define LDP1450_TEXT_H 480

// Attach to a player model (already set up with ldp_init) and park it.
void ldp1000_init(Ldp1000 *p, Ldp *ldp, double clock_hz);

// The same, as an LDP-1450.
void ldp1450_init(Ldp1000 *p, Ldp *ldp, double clock_hz);

// Bring the player up to time now.
void ldp1000_update(Ldp1000 *p, uint64_t now);

// A byte from the game.
void ldp1000_write(Ldp1000 *p, uint64_t now, uint8_t byte);

// Is a reply ready to go? Take it (0 if none).
bool ldp1000_has_reply(Ldp1000 *p, uint64_t now);
uint8_t ldp1000_read(Ldp1000 *p, uint64_t now);

// Can the disc's audio channel (0 or 1) be heard?
bool ldp1000_audio_on(const Ldp1000 *p, int channel);

// Draw an LDP-1450's text as it looks now: LDP1450_TEXT_W x LDP1450_TEXT_H
// pixels, 0xAARRGGBB, alpha 0 where the picture shows through.
void ldp1450_draw_text(const Ldp1000 *p, uint32_t *pixels);

#endif
