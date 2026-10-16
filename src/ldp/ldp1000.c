#include "ldp/ldp1000.h"

#include <stdio.h>
#include <stdlib.h>

#define ACK 0x0A
#define COMPLETED 0x01
#define ERROR 0x02

// How long the player takes to acknowledge, in milliseconds (from Sony's
// manual, by way of Daphne). Bega's Battle counts on the delay after a
// clear.
#define MS_DIGIT 1.2
#define MS_ENTER 2.6
#define MS_CLEAR 4.3

// The highest frame number on a disc.
#define LAST_FRAME 99999

void ldp1000_init(Ldp1000 *p, Ldp *ldp, double clock_hz)
{
    *p = (Ldp1000){.ldp = ldp, .clock_hz = clock_hz, .queued_search = -1, .channel_on = {true, true}};
    ldp->frame = 0;
    ldp_stop(ldp);
}

static void reply_after(Ldp1000 *p, uint64_t now, uint8_t byte, double ms)
{
    if (p->reply_count == (int)sizeof p->replies) return;
    if (p->reply_count == 0) p->reply_at = now + (uint64_t)(ms * p->clock_hz / 1000);
    p->replies[(p->reply_head + p->reply_count) % (int)sizeof p->replies] = byte;
    p->reply_count++;
}

static void reply(Ldp1000 *p, uint64_t now, uint8_t byte) { reply_after(p, now, byte, 0); }

static int entered_number(Ldp1000 *p)
{
    p->digits[p->digit_count] = '\0';
    int n = p->digit_count ? atoi(p->digits) : -1;
    p->digit_count = 0;
    return n;
}

static void start_search(Ldp1000 *p, uint64_t now, int frame)
{
    if (frame < 0 || frame > LAST_FRAME) {
        reply(p, now, ERROR);
        return;
    }
    p->repeating = false;
    ldp_search(p->ldp, frame);
    p->searching = true;
}

void ldp1000_update(Ldp1000 *p, uint64_t now)
{
    Ldp *ldp = p->ldp;
    if (now > p->last) {
        ldp_update(ldp, (double)(now - p->last) / p->clock_hz);
        p->last = now;
    }
    if (p->searching && ldp->state != LDP_SEARCHING) {
        // Arrived: say so, unless the search was cleared, in which case
        // start the one sent since, if any.
        p->searching = false;
        if (!p->aborting) reply(p, now, COMPLETED);
        p->aborting = false;
        if (p->queued_search >= 0) {
            start_search(p, now, p->queued_search);
            p->queued_search = -1;
        }
    }
    if (p->repeating && ldp->state == LDP_PLAYING && ldp->frame >= p->repeat_end) {
        // The end of a pass: go round again, or hold the frame.
        if (p->repeat_times > 0) p->repeat_times--;
        if (p->repeat_times != 0) ldp->frame = p->repeat_start;
        else {
            ldp_pause(ldp);
            p->repeating = false;
        }
        reply(p, now, COMPLETED);
    }
}

void ldp1000_write(Ldp1000 *p, uint64_t now, uint8_t byte)
{
    ldp1000_update(p, now);
    Ldp *ldp = p->ldp;
    if (byte >= '0' && byte <= '9') {
        if (p->digit_count < (int)sizeof p->digits - 1) p->digits[p->digit_count++] = (char)byte;
        reply_after(p, now, ACK, MS_DIGIT);
        return;
    }
    switch (byte) {
    case 0x3A:  // play
        p->repeating = false;
        ldp_play(ldp);
        reply_after(p, now, ACK, MS_ENTER);
        break;
    case 0x4F:  // still
        p->repeating = false;
        ldp_pause(ldp);
        reply(p, now, ACK);
        break;
    case 0x43:  // search: a frame number and enter follow
    case 0x44:  // repeat: a frame number and enter, then a count and enter
        p->entering = byte;
        p->digit_count = 0;
        reply_after(p, now, ACK, MS_ENTER);
        break;
    case 0x40:  // enter
        reply_after(p, now, ACK, MS_ENTER);
        if (p->entering == 0x43) {
            int frame = entered_number(p);
            if (p->aborting) p->queued_search = frame;  // after the cleared one
            else if (!p->searching) start_search(p, now, frame);
            p->entering = 0;
        } else if (p->entering == 0x44) {
            p->repeat_end = entered_number(p);
            p->repeat_start = ldp->frame;
            p->entering = 0x144;  // now the count
        } else if (p->entering == 0x144) {
            int times = entered_number(p);
            p->repeat_times = times < 0 ? 1 : times == 0 ? -1 : times;
            p->repeating = true;
            p->entering = 0;
            ldp_play(ldp);
        }
        break;
    case 0x56:  // clear
        if (p->searching) p->aborting = true;
        p->entering = 0;
        p->digit_count = 0;
        reply_after(p, now, ACK, MS_CLEAR);
        break;
    case 0x60: {  // report the frame
        char text[8];
        snprintf(text, sizeof text, "%05d", ldp->frame % 100000);
        for (int i = 0; i < 5; i++) reply(p, now, (uint8_t)text[i]);
        break;
    }
    case 0x24: p->muted = true; break;   // audio muted
    case 0x25: p->muted = false; break;  // audio unmuted
    case 0x46: p->channel_on[0] = true; reply(p, now, ACK); break;
    case 0x47: p->channel_on[0] = false; reply(p, now, ACK); break;
    case 0x48: p->channel_on[1] = true; reply(p, now, ACK); break;
    case 0x49: p->channel_on[1] = false; reply(p, now, ACK); break;
    // Video on, stop codes on and off, frame mode, motor on, and one Daphne
    // doesn't know: as in Daphne, no answer.
    case 0x27: case 0x28: case 0x29: case 0x55: case 0x62: case 0x6E: break;
    default: reply(p, now, ACK); break;
    }
}

bool ldp1000_has_reply(Ldp1000 *p, uint64_t now)
{
    ldp1000_update(p, now);
    return p->reply_count > 0 && now >= p->reply_at;
}

uint8_t ldp1000_read(Ldp1000 *p, uint64_t now)
{
    if (!ldp1000_has_reply(p, now)) return 0;
    uint8_t byte = p->replies[p->reply_head];
    p->reply_head = (p->reply_head + 1) % (int)sizeof p->replies;
    p->reply_count--;
    return byte;
}

bool ldp1000_audio_on(const Ldp1000 *p, int channel)
{
    return !p->muted && p->channel_on[channel & 1] && p->ldp->state == LDP_PLAYING;
}
