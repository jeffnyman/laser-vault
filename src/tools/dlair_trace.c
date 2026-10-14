// dlair-trace: run the Dragon's Lair ROM and print what it does.
//
// Usage: dlair-trace ROMS [--seconds N]
//
// ROMS is lair.zip or a folder holding the rev F2 ROM files. The board runs
// as fast as possible (no window, no video) for N seconds of game time
// (default 10), printing every byte the game sends to the laserdisc player
// and every change to the score display.
//
// There's no real laserdisc player yet, only a stand-in that always reports
// "parked and ready" and produces the strobe signals at the right times.
// The game will ask it to play and search, and see nothing happen.

#include "games/dlair/dlair.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// A stand-in laserdisc player
// ---------------------------------------------------------------------------
//
// The LD-V1000 tells the game when to talk to it with two strobe lines.
// While parked it pulses them every 21 ms: the status strobe goes active
// for 26 us (the game reads the status byte), then 54 us after it started,
// the command strobe goes active for 25 us (the game sends a byte). Timing
// from MAME's LD-V1000 model. At 4 MHz, 1 us is 4 CPU cycles.

#define STROBE_PERIOD (21000 * 4)
#define STATUS_STROBE_LENGTH (26 * 4)
#define COMMAND_STROBE_START (54 * 4)
#define COMMAND_STROBE_LENGTH (25 * 4)

#define STATUS_PARKED_AND_READY 0xFC

typedef struct {
    uint64_t *clock;  // the board's cycle counter, for timestamps
    int bytes_sent;
} StubPlayer;

static uint8_t stub_read_status(void *ctx)
{
    (void)ctx;
    return STATUS_PARKED_AND_READY;
}

static bool stub_status_strobe(void *ctx, uint64_t now)
{
    (void)ctx;
    return now % STROBE_PERIOD < STATUS_STROBE_LENGTH;
}

static bool stub_command_strobe(void *ctx, uint64_t now)
{
    (void)ctx;
    uint64_t phase = now % STROBE_PERIOD;
    return phase >= COMMAND_STROBE_START && phase < COMMAND_STROBE_START + COMMAND_STROBE_LENGTH;
}

// LD-V1000 command bytes (from MAME's ldv1000hle.h). Digits are sent one
// per byte, so "search to frame 1366" is 1, 3, 6, 6, SEARCH.
static const char *command_name(uint8_t b)
{
    switch (b) {
    case 0x3F: return "0";
    case 0x0F: return "1";
    case 0x8F: return "2";
    case 0x4F: return "3";
    case 0x2F: return "4";
    case 0xAF: return "5";
    case 0x6F: return "6";
    case 0x1F: return "7";
    case 0x9F: return "8";
    case 0x5F: return "9";
    case 0xBF: return "CLEAR";
    case 0xF5: return "STORE";
    case 0x7F: return "RECALL";
    case 0xF1: return "DISPLAY";
    case 0xF4: return "AUDIO1";
    case 0xFC: return "AUDIO2";
    case 0xFD: return "PLAY";
    case 0xFB: return "STOP";
    case 0xF3: return "AUTOSTOP";
    case 0xF7: return "SEARCH";
    case 0xF0: return "SCAN FWD";
    case 0xF8: return "SCAN REV";
    case 0xF6: return "STEP FWD";
    case 0xFE: return "STEP REV";
    case 0xF9: return "REJECT";
    case 0xFF: return "NO ENTRY";
    case 0xCC: return "LOAD";
    case 0xCD: return "DISPLAY OFF";
    case 0xCE: return "DISPLAY ON";
    case 0xC2: return "GET FRAME NUMBER";
    default: return "?";
    }
}

static void stub_send_command(void *ctx, uint8_t byte)
{
    StubPlayer *p = ctx;
    p->bytes_sent++;
    // NO ENTRY (FF) is what the game sends when it has nothing to say;
    // printing every one would drown out the interesting bytes.
    if (byte == 0xFF) return;
    printf("%8.3f s  player <- %02X %s\n", (double)*p->clock / DLAIR_CPU_HZ, byte, command_name(byte));
}

// ---------------------------------------------------------------------------

static void print_digits(const Dlair *b)
{
    static const char glyphs[] = "0123456789AbCdE ";
    char text[17];
    for (int i = 0; i < 16; i++) text[i] = glyphs[b->digits[i]];
    text[16] = '\0';
    printf("%8.3f s  display  [%.8s] [%.8s]\n", (double)b->cycles / DLAIR_CPU_HZ, text, text + 8);
}

int main(int argc, char **argv)
{
    const char *roms = NULL;
    double seconds = 10;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) seconds = atof(argv[++i]);
        else if (argv[i][0] != '-' && !roms) roms = argv[i];
        else roms = NULL, i = argc;
    }
    if (!roms || seconds <= 0) {
        fprintf(stderr, "usage: dlair-trace ROMS [--seconds N]\n");
        return 1;
    }

    static Dlair board;
    if (!dlair_load_roms(&board, roms)) return 1;

    StubPlayer player = {.clock = &board.cycles};
    dlair_reset(&board, (DlairLaserdisc){
        .ctx = &player,
        .read_status = stub_read_status,
        .send_command = stub_send_command,
        .status_strobe = stub_status_strobe,
        .command_strobe = stub_command_strobe,
    });

    // Run in slices of 1 ms, checking the display between them.
    uint8_t shown[16];
    memcpy(shown, board.digits, sizeof shown);
    uint64_t total = (uint64_t)(seconds * DLAIR_CPU_HZ);
    while (board.cycles < total) {
        dlair_run(&board, DLAIR_CPU_HZ / 1000);
        if (memcmp(shown, board.digits, sizeof shown) != 0) {
            memcpy(shown, board.digits, sizeof shown);
            print_digits(&board);
        }
    }

    printf("Ran %.1f s of game time: %d bytes sent to the player; CPU now at PC=%04X%s\n",
           seconds, player.bytes_sent, board.cpu.pc, board.cpu.halted ? " (halted)" : "");
    return 0;
}
