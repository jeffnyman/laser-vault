#include "ldp/ldv1000.h"

#include <math.h>
#include <stddef.h>

// Command bytes.
enum {
    CMD_CLEAR = 0xBF,
    CMD_PLAY = 0xFD,
    CMD_STOP = 0xFB,
    CMD_AUTOSTOP = 0xF3,
    CMD_SEARCH = 0xF7,
    CMD_STEP_FWD = 0xF6,
    CMD_STEP_REV = 0xFE,
    CMD_REJECT = 0xF9,
    CMD_NO_ENTRY = 0xFF,
    CMD_AUDIO1 = 0xF4,
    CMD_AUDIO2 = 0xFC,
};

// Status bytes. READY (bit 7) is or'ed in when the player will take a
// command; SEARCH_FINISH has it built in.
enum {
    STATUS_READY = 0x80,
    STATUS_PARK = 0x7C,
    STATUS_PLAY = 0x64,
    STATUS_STOP = 0x65,
    STATUS_SEARCH = 0x50,
    STATUS_SEARCH_FINISH = 0xD0,
    STATUS_AUTOSTOP = 0x54,
};

// Digits are sent as these bytes, in order 0 to 9.
static const uint8_t digit_bytes[10] = {0x3F, 0x0F, 0x8F, 0x4F, 0x2F, 0xAF, 0x6F, 0x1F, 0x9F, 0x5F};

static int digit_value(uint8_t byte)
{
    for (int i = 0; i < 10; i++)
        if (digit_bytes[i] == byte) return i;
    return -1;
}

// The number entered so far, as a plain integer.
static int entered_number(const Ldv1000 *ld)
{
    int n = 0;
    for (int i = 0; i < ld->digit_count; i++) n = n * 10 + ld->digits[i];
    return n;
}

void ldv1000_init(Ldv1000 *ld, Ldp *ldp, double clock_hz)
{
    *ld = (Ldv1000){
        .ldp = ldp,
        .clock_hz = clock_hz,
        .status = STATUS_PARK | STATUS_READY,
        .autostop_frame = -1,
        .audio = {true, true},
    };
    ldp->frame = 0;
    ldp->blank_during_search = true;  // the LD-V1000 outputs no picture mid-search
    ldp_stop(ldp);  // parked: disc not spinning
    ld->previous_state = ldp->state;
}

void ldv1000_update(Ldv1000 *ld, uint64_t now)
{
    if (now <= ld->last) return;
    Ldp *ldp = ld->ldp;
    ldp_update(ldp, (double)(now - ld->last) / ld->clock_hz);
    ld->last = now;

    // A search that just finished leaves the player paused on the target,
    // ready for the next command.
    if (ld->previous_state == LDP_SEARCHING && ldp->state == LDP_PAUSED)
        ld->status = STATUS_SEARCH_FINISH;

    // AUTOSTOP: play until the target frame, then hold it.
    if (ld->autostop_frame >= 0 && ldp->state == LDP_PLAYING && ldp->frame >= ld->autostop_frame) {
        ldp_pause(ldp);
        ldp->frame = ld->autostop_frame;
        ld->autostop_frame = -1;
        ld->status = (uint8_t)((ld->status & STATUS_READY) | STATUS_STOP);
    }

    // A timed STOP (STOP with a number) resumes playing when time's up.
    if (ld->resume_at && now >= ld->resume_at) {
        ld->resume_at = 0;
        ldp_play(ldp);
        ld->status = ld->status_before_stop;
    }

    ld->previous_state = ldp->state;
}

static void execute(Ldv1000 *ld, uint64_t now, uint8_t byte)
{
    Ldp *ldp = ld->ldp;
    int number = entered_number(ld);

    switch (byte) {
    case CMD_PLAY:
        if (ldp->state == LDP_SEARCHING) {  // finish the search instantly
            ldp->frame = ldp->search_target;
            ldp->state = LDP_PAUSED;
        }
        ldp_play(ldp);
        ld->autostop_frame = -1;
        ld->status = STATUS_PLAY;
        break;
    case CMD_STOP:
        if (ld->digit_count > 0) {  // hold for number * 0.1 s, then play on
            ld->resume_at = now + (uint64_t)(number * 0.1 * ld->clock_hz);
            ld->status_before_stop = ld->status;
        }
        ldp_pause(ldp);
        ld->status = STATUS_STOP;
        break;
    case CMD_SEARCH:
        ldp_search(ldp, number);
        ld->autostop_frame = -1;
        ld->status = STATUS_SEARCH;
        break;
    case CMD_AUTOSTOP:
        if (number < ldp->frame) {  // a target behind us becomes a search
            ldp_search(ldp, number);
            ld->status = STATUS_SEARCH;
        } else {
            ld->autostop_frame = number;
            ldp_play(ldp);
            ld->status = STATUS_AUTOSTOP;
        }
        break;
    case CMD_STEP_FWD:
    case CMD_STEP_REV:
        ldp_step(ldp, byte == CMD_STEP_FWD ? 1 : -1);
        ld->status = STATUS_STOP;
        break;
    case CMD_REJECT:  // stop the disc and park
        ldp_stop(ldp);
        ld->autostop_frame = -1;
        ld->status = STATUS_PARK;
        break;
    case CMD_AUDIO1:
    case CMD_AUDIO2: {
        // With no number, toggle the channel; with a number, set it on/off.
        int ch = byte == CMD_AUDIO1 ? 0 : 1;
        ld->audio[ch] = ld->digit_count ? (number & 1) : !ld->audio[ch];
        break;
    }
    case CMD_NO_ENTRY:
        ld->status |= STATUS_READY;
        break;
    default:  // CLEAR, display and other commands: nothing to do
        break;
    }
}

void ldv1000_write(Ldv1000 *ld, uint64_t now, uint8_t byte)
{
    ldv1000_update(ld, now);

    if (!(ld->status & STATUS_READY)) {
        // Busy with the last command; only NO ENTRY gets through.
        if (byte == CMD_NO_ENTRY) ld->status |= STATUS_READY;
        return;
    }
    ld->status &= ~STATUS_READY;

    int digit = digit_value(byte);
    if (digit >= 0) {
        if (ld->digit_count < (int)sizeof ld->digits) ld->digits[ld->digit_count++] = (uint8_t)digit;
        return;
    }
    execute(ld, now, byte);
    ld->digit_count = 0;  // any non-digit ends the number
    ld->previous_state = ld->ldp->state;
}

uint8_t ldv1000_read_status(Ldv1000 *ld, uint64_t now)
{
    ldv1000_update(ld, now);
    return ld->status;
}

// ---------------------------------------------------------------------------
// Strobes
// ---------------------------------------------------------------------------
//
// Times in microseconds, from MAME's model. The status strobe is active for
// 26 us; the command strobe starts 54 us after it and lasts 25 us. Parked,
// they repeat every 21 ms from time zero; otherwise once per field, starting
// 500 us into it.

#define PARKED_PERIOD_US 21000.0
#define FIELD_PERIOD_US (1e6 / 59.94)
#define FIELD_STROBE_OFFSET_US 500.0
#define STATUS_STROBE_US 26.0
#define COMMAND_STROBE_START_US 54.0
#define COMMAND_STROBE_US 25.0

// Microseconds since the last status strobe started (can be negative just
// before the first one in a field).
static double since_status_strobe(Ldv1000 *ld, uint64_t now)
{
    ldv1000_update(ld, now);
    double us = (double)now * 1e6 / ld->clock_hz;
    if (ld->ldp->state == LDP_STOPPED) return fmod(us, PARKED_PERIOD_US);
    return fmod(us, FIELD_PERIOD_US) - FIELD_STROBE_OFFSET_US;
}

bool ldv1000_status_strobe(Ldv1000 *ld, uint64_t now)
{
    double t = since_status_strobe(ld, now);
    return t >= 0 && t < STATUS_STROBE_US;
}

bool ldv1000_command_strobe(Ldv1000 *ld, uint64_t now)
{
    double t = since_status_strobe(ld, now);
    return t >= COMMAND_STROBE_START_US && t < COMMAND_STROBE_START_US + COMMAND_STROBE_US;
}

const char *ldv1000_command_name(uint8_t byte)
{
    static const char *digits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    int d = digit_value(byte);
    if (d >= 0) return digits[d];
    switch (byte) {
    case CMD_CLEAR: return "CLEAR";
    case CMD_PLAY: return "PLAY";
    case CMD_STOP: return "STOP";
    case CMD_AUTOSTOP: return "AUTOSTOP";
    case CMD_SEARCH: return "SEARCH";
    case CMD_STEP_FWD: return "STEP FWD";
    case CMD_STEP_REV: return "STEP REV";
    case CMD_REJECT: return "REJECT";
    case CMD_NO_ENTRY: return "NO ENTRY";
    case CMD_AUDIO1: return "AUDIO1";
    case CMD_AUDIO2: return "AUDIO2";
    case 0xF5: return "STORE";
    case 0x7F: return "RECALL";
    case 0xF1: return "DISPLAY";
    case 0xF0: return "SCAN FWD";
    case 0xF8: return "SCAN REV";
    case 0xCC: return "LOAD";
    case 0xCD: return "DISPLAY OFF";
    case 0xCE: return "DISPLAY ON";
    case 0xC2: return "GET FRAME NUMBER";
    default: return NULL;
    }
}
