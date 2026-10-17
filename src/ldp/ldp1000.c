#include "ldp/ldp1000.h"
#include "ldp/font8x8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void ldp1450_init(Ldp1000 *p, Ldp *ldp, double clock_hz)
{
    ldp1000_init(p, ldp, clock_hz);
    p->ldp1450 = true;
    memset(p->text, ' ', sizeof p->text);
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

// The bytes after an LDP-1450's 80 (text display) command.
enum { TEXT_NONE, TEXT_WHICH, TEXT_X, TEXT_Y, TEXT_MODE, TEXT_POS, TEXT_CHARS, TEXT_START };

static void text_byte(Ldp1000 *p, uint8_t byte)
{
    switch (p->text_state) {
    case TEXT_WHICH:
        p->text_state = byte == 0 ? TEXT_X : byte == 1 ? TEXT_POS : byte == 2 ? TEXT_START : TEXT_NONE;
        return;
    case TEXT_X: p->text_x = byte & 0x3F; p->text_state = TEXT_Y; break;
    case TEXT_Y: p->text_y = byte & 0x3F; p->text_state = TEXT_MODE; break;
    case TEXT_MODE: p->text_mode = byte; p->text_state = TEXT_NONE; break;
    case TEXT_POS: p->text_pos = byte & 0x1F; p->text_state = TEXT_CHARS; return;
    case TEXT_CHARS:
        p->text[p->text_pos] = byte;
        p->text_pos = (p->text_pos + 1) & 0x1F;
        if (byte == 0x1A) p->text_state = TEXT_NONE;  // the end of the text
        break;
    case TEXT_START: p->text_start = byte & 0x1F; p->text_state = TEXT_NONE; break;
    }
    p->text_serial++;
}

void ldp1000_write(Ldp1000 *p, uint64_t now, uint8_t byte)
{
    ldp1000_update(p, now);
    Ldp *ldp = p->ldp;
    if (p->ldp1450) {
        if (p->text_state != TEXT_NONE) {
            text_byte(p, byte);
            return;
        }
        switch (byte) {
        case 0x67: {  // status: ready, door closed, disc in; playing or still
            static const uint8_t status[4] = {0x80, 0x00, 0x10, 0x00};
            for (int i = 0; i < 4; i++) reply_after(p, now, status[i], MS_DIGIT);
            reply(p, now, ldp->state == LDP_PLAYING ? 0x01 : 0x20);
            return;
        }
        case 0x80: p->text_state = TEXT_WHICH; return;
        case 0x81: p->text_on = true; p->text_serial++; return;
        case 0x82: p->text_on = false; p->text_serial++; return;
        case 0x26: return;  // video off (it's left on)
        // Audio off and on switch both channels, which 46-49 then switch one
        // at a time (as MAME and Daphne have it: Dragon's Lair II turns the
        // audio off, then each channel on).
        case 0x24: p->channel_on[0] = p->channel_on[1] = false; return;
        case 0x25: p->channel_on[0] = p->channel_on[1] = true; return;
        }
    }
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

// One character, the font's 8x8 scaled to w x h, in white; style 1 adds
// a shadow, 3 a grey box behind.
static void draw_char(uint32_t *pixels, uint8_t ch, int x0, int y0, int w, int h, int style)
{
    if (style == 3)
        for (int y = y0; y < y0 + h; y++)
            for (int x = x0; x < x0 + w; x++)
                if (x >= 0 && x < LDP1450_TEXT_W && y >= 0 && y < LDP1450_TEXT_H)
                    pixels[y * LDP1450_TEXT_W + x] = 0xFF808080;
    if (ch < FONT8X8_FIRST || ch > FONT8X8_LAST) return;
    const uint8_t *glyph = font8x8[ch - FONT8X8_FIRST];
    int shadow = w / 8;
    for (int pass = style == 1 ? 0 : 1; pass < 2; pass++) {
        int off = pass == 0 ? shadow : 0;
        uint32_t colour = pass == 0 ? 0xFF000000 : 0xFFFFFFFF;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (!((glyph[y * 8 / h] >> (x * 8 / w)) & 1)) continue;
                int px = x0 + x + off, py = y0 + y + off;
                if (px >= 0 && px < LDP1450_TEXT_W && py >= 0 && py < LDP1450_TEXT_H)
                    pixels[py * LDP1450_TEXT_W + px] = colour;
            }
    }
}

void ldp1450_draw_text(const Ldp1000 *p, uint32_t *pixels)
{
    memset(pixels, 0, (size_t)LDP1450_TEXT_W * LDP1450_TEXT_H * sizeof *pixels);
    if (!p->text_on) return;
    uint8_t mode = p->text_mode;
    if (mode & 0x80)  // a blue screen
        for (int i = 0; i < LDP1450_TEXT_W * LDP1450_TEXT_H; i++) pixels[i] = 0xFF1A1AB4;
    int w = 16 * (1 + (mode & 3)), h = 16 * (1 + ((mode >> 2) & 3));
    int x0 = p->text_x * LDP1450_TEXT_W / 128, y0 = p->text_y * LDP1450_TEXT_H / 64;
    int style = (mode >> 5) & 3;
    if (mode & 0x10) {  // three lines of ten
        for (int line = 0; line < 3; line++)
            for (int i = 0; i < 10; i++) {
                uint8_t ch = p->text[line * 10 + i];
                if (ch == 0x1A) break;
                draw_char(pixels, ch, x0 + i * w, y0 + line * h, w, h, style);
            }
    } else {
        for (int i = p->text_start; i < 32; i++) {
            uint8_t ch = p->text[i];
            if (ch == 0x1A) break;
            draw_char(pixels, ch, x0 + (i - p->text_start) * w, y0, w, h, style);
        }
    }
}
