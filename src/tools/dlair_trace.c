// dlair-trace: run the Dragon's Lair ROM and print what it does.
//
// Usage: dlair-trace ROMS [--seconds N] [--press BUTTON@SECONDS ...]
//
// ROMS is lair.zip or a folder holding the rev F2 ROM files. The board and
// its LD-V1000 laserdisc player run as fast as possible (no window, no
// video) for N seconds of game time (default 60), printing every command the
// game sends, every change in what the player is doing, and every change to
// the score display.
//
// --press holds a button at the given time, for 0.2 s or the given length:
// --press coin1@40 or --press coin1@40:0.05. Buttons: up down left right
// sword start1 start2 coin1 coin2.
//
// --autoplay lets the program play (see games/dlair/dlair_autoplay.h): give
// it coin and start presses, and it prints each move it makes.

#include "games/dlair/dlair.h"
#include "games/dlair/dlair_autoplay.h"
#include "ldp/ldv1000.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISC_FPS 23.976  // see video/disc_video.h

static Dlair board;
static Ldp ldp;
static Ldv1000 player;

static double seconds_now(void) { return (double)board.cycles / DLAIR_CPU_HZ; }

#define MAX_PRESSES 32
#define PRESS_SECONDS 0.2

typedef struct {
    bool *button;
    double at, length;
} Press;

static bool *button_named(const char *name)
{
    DlairInputs *in = &board.inputs;
    struct { const char *name; bool *button; } buttons[] = {
        {"up", &in->up}, {"down", &in->down}, {"left", &in->left}, {"right", &in->right},
        {"sword", &in->sword}, {"start1", &in->start1}, {"start2", &in->start2},
        {"coin1", &in->coin1}, {"coin2", &in->coin2},
    };
    for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
        if (strcmp(name, buttons[i].name) == 0) return buttons[i].button;
    return NULL;
}

// Print each command on its way to the player.
static void send_command(void *ctx, uint64_t now, uint8_t byte)
{
    // NO ENTRY (FF) follows every command; printing them all would drown
    // out the interesting bytes.
    if (byte != 0xFF) {
        const char *name = ldv1000_command_name(byte);
        printf("%8.3f s  player <- %02X %s\n", seconds_now(), byte, name ? name : "?");
    }
    ldv1000_write(ctx, now, byte);
}

static void print_digits(void)
{
    static const char glyphs[] = "0123456789AbCdE ";
    char text[17];
    for (int i = 0; i < 16; i++) text[i] = glyphs[board.digits[i]];
    text[16] = '\0';
    printf("%8.3f s  display  [%.8s] [%.8s]\n", seconds_now(), text, text + 8);
}

int main(int argc, char **argv)
{
    const char *roms = NULL;
    double seconds = 60;
    int dsw1 = -1, dsw2 = -1;  // DIP switch overrides, -1 = default
    Press presses[MAX_PRESSES];
    int press_count = 0;
    bool bad_args = false;
    bool autoplay = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--autoplay") == 0) {
            autoplay = true;
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = atof(argv[++i]);
        } else if (strcmp(argv[i], "--dsw1") == 0 && i + 1 < argc) {
            dsw1 = (int)strtol(argv[++i], NULL, 16);
        } else if (strcmp(argv[i], "--dsw2") == 0 && i + 1 < argc) {
            dsw2 = (int)strtol(argv[++i], NULL, 16);
        } else if (strcmp(argv[i], "--press") == 0 && i + 1 < argc && press_count < MAX_PRESSES) {
            char name[32];
            double at, length = PRESS_SECONDS;
            if (sscanf(argv[++i], "%31[a-z0-9]@%lf:%lf", name, &at, &length) < 2 || !button_named(name)) {
                bad_args = true;
                break;
            }
            presses[press_count++] = (Press){button_named(name), at, length};
        } else if (argv[i][0] != '-' && !roms) {
            roms = argv[i];
        } else {
            bad_args = true;
            break;
        }
    }
    if (bad_args || !roms || seconds <= 0) {
        fprintf(stderr, "usage: dlair-trace ROMS [--seconds N] [--press BUTTON@SECONDS ...] [--autoplay]\n");
        return 1;
    }

    if (!dlair_load_roms(&board, roms)) return 1;
    ldp_init(&ldp, DISC_FPS, 0);
    ldv1000_init(&player, &ldp, DLAIR_CPU_HZ);
    DlairLaserdisc link = dlair_ldv1000_link(&player);
    link.send_command = send_command;
    dlair_reset(&board, link);
    if (dsw1 >= 0) board.dsw1 = (uint8_t)dsw1;
    if (dsw2 >= 0) board.dsw2 = (uint8_t)dsw2;

    DlairAutoplay player_bot;
    dlair_autoplay_init(&player_bot, true);

    // Run in slices of 1 ms, reporting changes between them.
    uint8_t shown[16];
    memcpy(shown, board.digits, sizeof shown);
    LdpState shown_state = ldp.state;
    uint64_t total = (uint64_t)(seconds * DLAIR_CPU_HZ);
    while (board.cycles < total) {
        // Each button is held from its press time for its length.
        for (int i = 0; i < press_count; i++) *presses[i].button = false;
        for (int i = 0; i < press_count; i++) {
            double t = seconds_now() - presses[i].at;
            if (t >= 0 && t < presses[i].length) *presses[i].button = true;
        }
        if (autoplay) dlair_autoplay_update(&player_bot, &board, &ldp, &player);
        dlair_run(&board, DLAIR_CPU_HZ / 1000);
        ldv1000_update(&player, board.cycles);
        if (ldp.state != shown_state) {
            shown_state = ldp.state;
            printf("%8.3f s  player is %s at frame %d (status %02X)\n",
                   seconds_now(), ldp_state_name(ldp.state), ldp.frame, player.status);
        }
        if (memcmp(shown, board.digits, sizeof shown) != 0) {
            memcpy(shown, board.digits, sizeof shown);
            print_digits();
        }
    }

    printf("Ran %.1f s of game time; player %s at frame %d\n",
           seconds, ldp_state_name(ldp.state), ldp.frame);
    if (autoplay) printf("Autoplay made %d moves\n", player_bot.moves_made);
    dlair_autoplay_free(&player_bot);
    return 0;
}
