#include "ldp/vip9500sg.h"

#include <stdlib.h>
#include <string.h>

void vip9500sg_init(Vip9500sg *vip, Ldp *ldp, double clock_hz)
{
    *vip = (Vip9500sg){.ldp = ldp, .clock_hz = clock_hz};
    ldp->frame = 0;
    ldp->blank_during_search = true;  // no picture mid-search
    ldp_stop(ldp);
}

static void reply(Vip9500sg *vip, uint8_t byte)
{
    if (vip->reply_count == (int)sizeof vip->replies) return;
    vip->replies[(vip->reply_head + vip->reply_count) % (int)sizeof vip->replies] = byte;
    vip->reply_count++;
}

void vip9500sg_update(Vip9500sg *vip, uint64_t now)
{
    if (now > vip->last) {
        ldp_update(vip->ldp, (double)(now - vip->last) / vip->clock_hz);
        vip->last = now;
    }
    // A search ends with the player holding the frame.
    if (vip->searching && vip->ldp->state != LDP_SEARCHING) {
        vip->searching = false;
        reply(vip, 0x41);
        reply(vip, 0xB0);
    }
}

static int entered_number(Vip9500sg *vip)
{
    vip->digits[vip->digit_count] = '\0';
    int n = atoi(vip->digits);
    vip->digit_count = 0;
    return n;
}

void vip9500sg_write(Vip9500sg *vip, uint64_t now, uint8_t byte)
{
    vip9500sg_update(vip, now);
    Ldp *ldp = vip->ldp;
    if (byte >= '0' && byte <= '9') {
        if (vip->digit_count < (int)sizeof vip->digits - 1) vip->digits[vip->digit_count++] = (char)byte;
        return;
    }
    switch (byte) {
    case 0x25: ldp_play(ldp); reply(vip, 0xA5); break;      // play
    case 0x53: ldp_play(ldp); reply(vip, 0xD3); break;      // play at 1x, with sound
    case 0x29: ldp_pause(ldp); reply(vip, 0xA9); break;     // step back, used as pause
    case 0x2F: reply(vip, 0xAF); break;                     // stop (Daphne ignores it, and so do we)
    case 0x4C: reply(vip, 0xCC); break;                     // frame counter on
    case 0x4D: reply(vip, 0xCD); break;                     // frame counter off
    case 0x68: reply(vip, 0xE8); break;                     // reset
    case 0x6E: reply(vip, 0xEE); break;                     // (meaning unknown)
    case 0x2B: case 0x46:                                   // search, skip forward: a number follows
        vip->pending = byte;
        vip->digit_count = 0;
        break;
    case 0x41:                                              // enter: finish the search or skip
        if (vip->pending == 0x2B) {
            int frame = entered_number(vip);
            if (!vip->searching) {
                ldp_search(ldp, frame);
                vip->searching = true;
            }
        } else if (vip->pending == 0x46) {
            ldp->frame += entered_number(vip);
            reply(vip, 0x41);
            reply(vip, 0xC6);
        }
        vip->pending = 0;
        break;
    case 0x6B: {                                            // report the frame
        int frame = ldp->frame;
        reply(vip, 0x6B);
        reply(vip, (uint8_t)(frame >> 8));
        reply(vip, (uint8_t)frame);
        break;
    }
    default: break;  // 50-59: variable speeds, not supported (no answer, as in Daphne)
    }
}

bool vip9500sg_has_reply(Vip9500sg *vip, uint64_t now)
{
    vip9500sg_update(vip, now);
    return vip->reply_count > 0;
}

uint8_t vip9500sg_read(Vip9500sg *vip, uint64_t now)
{
    vip9500sg_update(vip, now);
    if (!vip->reply_count) return 0;
    uint8_t byte = vip->replies[vip->reply_head];
    vip->reply_head = (vip->reply_head + 1) % (int)sizeof vip->replies;
    vip->reply_count--;
    return byte;
}

bool vip9500sg_audio_on(const Vip9500sg *vip, int channel)
{
    (void)channel;
    return vip->ldp->state == LDP_PLAYING;
}
