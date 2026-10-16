#include "ldp/pr8210.h"

#include <stddef.h>

// Command codes.
enum {
    CMD_PLAY = 0x14,
    CMD_PAUSE = 0x0A,
    CMD_STEP_FWD = 0x04,
    CMD_STEP_REV = 0x12,
    CMD_SEEK = 0x1A,
    CMD_AUDIO1 = 0x0E,
    CMD_AUDIO2 = 0x16,
    CMD_REJECT = 0x1E,
    CMD_PLAY_3X = 0x10,
    CMD_SCAN_FWD = 0x08,
    CMD_SLOW_FWD = 0x18,
    CMD_PLAY_3X_REV = 0x0C,
    CMD_SCAN_REV = 0x1C,
    CMD_SLOW_REV = 0x02,
    CMD_CHAPTER = 0x06,
    CMD_FRAME = 0x0B,
};

// The digits 0 to 9.
static const uint8_t digit_codes[10] = {0x01, 0x11, 0x09, 0x19, 0x05, 0x15, 0x0D, 0x1D, 0x03, 0x13};

// Serial timing, from the player's 455 kHz clock (as in MAME's model): a
// gap longer than 1.32 ms is a 1; a gap over 9 ms, or a code taking over 25
// ms, starts afresh; a repeat of the same command within 101 ms is ignored.
#define MIDPOINT_S (600.0 / 455000)
#define MAX_BIT_S (4096.0 / 455000)
#define MAX_WORD_S (11520.0 / 455000)
#define REPEAT_S (4 * 11520.0 / 455000)

static int digit_value(uint8_t command)
{
    for (int i = 0; i < 10; i++)
        if (digit_codes[i] == command) return i;
    return -1;
}

void pr8210_init(Pr8210 *ld, Ldp *ldp, double clock_hz)
{
    *ld = (Pr8210){
        .ldp = ldp,
        .clock_hz = clock_hz,
        .last_command = 0xFF,
        .audio = {true, true},
    };
    ldp->frame = 0;
    ldp->blank_during_search = true;
    ldp_stop(ldp);
}

void pr8210_update(Pr8210 *ld, uint64_t now)
{
    if (now <= ld->last) return;
    ldp_update(ld->ldp, (double)(now - ld->last) / ld->clock_hz);
    ld->last = now;
}

static void execute(Pr8210 *ld, uint8_t command)
{
    Ldp *ldp = ld->ldp;
    int digit = digit_value(command);
    if (digit >= 0) {
        if (ld->seeking && ld->digit_count < 5) {
            ld->number = ld->number * 10 + digit;
            ld->digit_count++;
        }
        return;
    }
    switch (command) {
    case CMD_SEEK:
        // The first SEEK starts a frame number, the second searches to it.
        // (A SEEK with no digits between does nothing; Cliff Hanger sends
        // that at start-up.)
        if (ld->seeking && ld->digit_count > 0) ldp_search(ldp, ld->number);
        ld->seeking = !ld->seeking;
        ld->number = 0;
        ld->digit_count = 0;
        break;
    case CMD_PLAY:
    case CMD_PLAY_3X:
    case CMD_SCAN_FWD:
    case CMD_SLOW_FWD:
    case CMD_PLAY_3X_REV:
    case CMD_SCAN_REV:
    case CMD_SLOW_REV:
        // Only normal play is modelled.
        if (ldp->state == LDP_STOPPED) ldp->state = LDP_PAUSED;  // spin up
        ldp_play(ldp);
        break;
    case CMD_PAUSE: ldp_pause(ldp); break;
    case CMD_STEP_FWD: ldp_step(ldp, 1); break;
    case CMD_STEP_REV: ldp_step(ldp, -1); break;
    case CMD_AUDIO1: ld->audio[0] = !ld->audio[0]; break;
    case CMD_AUDIO2: ld->audio[1] = !ld->audio[1]; break;
    case CMD_REJECT: ldp_stop(ldp); break;
    default: break;  // CHAPTER, FRAME (display modes): nothing to do
    }
}

void pr8210_control(Pr8210 *ld, uint64_t now, bool high)
{
    bool rising = high && !ld->line;
    ld->line = high;
    if (!rising) return;
    pr8210_update(ld, now);

    // Each rising edge ends the gap that encodes the previous bit.
    double gap = (double)(now - ld->last_edge) / ld->clock_hz;
    double since_start = (double)(now - ld->word_start) / ld->clock_hz;
    ld->last_edge = now;
    if (gap > MAX_BIT_S || since_start > MAX_WORD_S) {
        ld->word_start = now;
        ld->bits = 0x5555;  // nothing that can look like a code
    }
    ld->bits = (uint16_t)(ld->bits << 1 | (gap > MIDPOINT_S));

    // A whole code: 0, 0, 1, five bits, 0, 0.
    if ((ld->bits & 0x383) != 0x080) return;
    uint8_t command = (ld->bits >> 2) & 0x1F;
    double since_last = (double)(now - ld->last_command_time) / ld->clock_hz;
    bool repeat = command == ld->last_command && since_last < REPEAT_S;
    ld->last_command_time = now;
    ld->word_start = 0;  // the next edge starts a new code
    if (repeat) return;
    ld->last_command = command;
    execute(ld, command);
}

uint32_t pr8210_philips_code(Pr8210 *ld, uint64_t now)
{
    pr8210_update(ld, now);
    const Ldp *ldp = ld->ldp;
    if (ldp->state != LDP_PLAYING && ldp->state != LDP_PAUSED) return 0;
    uint32_t bcd = 0;
    int frame = ldp->frame;
    for (int shift = 0; shift < 20; shift += 4) {
        bcd |= (uint32_t)(frame % 10) << shift;
        frame /= 10;
    }
    return 0xF80000 | bcd;
}

bool pr8210_audio_on(const Pr8210 *ld, int channel)
{
    return ld->audio[channel];
}

const char *pr8210_command_name(uint8_t command)
{
    static const char *digits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    int d = digit_value(command);
    if (d >= 0) return digits[d];
    switch (command) {
    case CMD_PLAY: return "PLAY";
    case CMD_PAUSE: return "PAUSE";
    case CMD_STEP_FWD: return "STEP FWD";
    case CMD_STEP_REV: return "STEP REV";
    case CMD_SEEK: return "SEEK";
    case CMD_AUDIO1: return "AUDIO1";
    case CMD_AUDIO2: return "AUDIO2";
    case CMD_REJECT: return "REJECT";
    case CMD_PLAY_3X: return "3X FWD";
    case CMD_SCAN_FWD: return "SCAN FWD";
    case CMD_SLOW_FWD: return "SLOW FWD";
    case CMD_PLAY_3X_REV: return "3X REV";
    case CMD_SCAN_REV: return "SCAN REV";
    case CMD_SLOW_REV: return "SLOW REV";
    case CMD_CHAPTER: return "CHAPTER";
    case CMD_FRAME: return "FRAME";
    default: return NULL;
    }
}
