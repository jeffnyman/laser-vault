// gpworld.h - Sega's GP World board (1984), a racing game over film of a
// real circuit.
//
// The cabinet was huge: two monitors side by side, the laserdisc's picture
// stretched across both, and a steering wheel, two pedals and a gear
// lever. The board is a bigger relative of Astron Belt's: a Z80 driving
// Pioneer's LD-V1000 through latches, with characters and sprites drawn
// over the picture.
//
//     Z80 at 5 MHz (a guess, as for Astron Belt)
//     0000-BFFF  ROM
//     C000-C7FF  sprites: 256 of 8 bytes
//     C800-C9FF  the characters' colours: 256 of 2 bytes
//     CA00-CBFF  the sprites' colours: 256 of 2 bytes
//     D000-D7FF  characters: 64 x 32 tile numbers, by rows (across both
//                screens)
//     D800       LD-V1000: read the latched status, write the next command
//     DA00       the steering wheel
//     DA01       the sound circuits
//     DA02       write: bit 0, DA20 reads the accelerator (1) or the brake
//                (0); read: bit 4, the pedals' converter busy
//     DA20       write: start converting; read: the pedal chosen, 00 (up)
//                to FF (down)
//     E000-FFFF  RAM
//
//     port 01    write: bit 6, NMI on; bit 2, the start button's lamp
//     port 80    the gear lever (bit 4: set in low gear)
//     port 81    coins (bits 0, 1), test (2), service (3), start (4)
//     port 82, 83  DIP switches 1 and 2
//
// The inputs are active low. The wheel is eight switches, four each way
// for how far it's turned (slight, medium, strong, fierce), and turning
// further closes the next one as well.
//
// The IRQ, at vertical blank, runs the game; the NMI, once a field, is the
// laserdisc link, as on Astron Belt. The game varies the disc's speed with
// the car's (the LD-V1000's FORWARD x1/4 to x5 commands).
//
// Characters are 8x8 pixels of 2 bits, from one graphics chip of two
// halves; with the top six bits of the tile number they pick one of 256
// colours. Sprites are drawn row by row, four pixels of 4 bits from each
// pair of bytes in the graphics ROMs, with the sprite's colour group (16
// colours) above them. Colours are 4 bits each of red, green and blue.
// Colour 0 is see-through (and sprite pen 15 too).
//
// The sound was analogue circuits (engine, tyres, crash), not emulated.
//
// Details from MAME's gpworld.cpp (marked not working) and the Daphne
// emulator's gpworld.cpp, checked against the game's code.

#ifndef LV_SEGA_GPWORLD_H
#define LV_SEGA_GPWORLD_H

#include "cpu/z80.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define GPW_CPU_HZ 5000000
#define GPW_SAMPLE_RATE 44100

// The overlay: the characters' grid is 512 x 256 pixels, across both
// screens; the game draws on columns 19-63 of it, which (as in Daphne) are
// shown over the picture. Of its lines, a monitor shows about 224: here
// lines 6-229, so that the player's car (whose last line is 229) sits on
// the bottom edge, as if the screen cut it off.
#define GPW_OVERLAY_W 360
#define GPW_OVERLAY_H 224
#define GPW_FIRST_X 152
#define GPW_FIRST_LINE 6

// True means pressed. Holding left or right turns the wheel further the
// longer it's held, and letting go centres it; up and down move the gear
// lever to high and low.
typedef struct {
    bool left, right, gas, brake, gear_up, gear_down;
    bool start1, coin1, coin2, test, service;
} GpwInputs;

typedef struct {
    Z80 cpu;
    uint8_t rom[0xC000];
    uint8_t chars[0x1000];       // two bit planes of 0x800
    uint8_t sprites[0x30000];    // banks of 0x10000: 0x8000 of low bytes, then high
    uint8_t sprite_ram[0x800];   // C000-C7FF
    uint8_t colour_ram[0x800];   // C800-CFFF
    uint8_t tile_ram[0x800];     // D000-D7FF
    uint8_t ram[0x2000];         // E000-FFFF
    uint8_t dsw1, dsw2;
    GpwInputs inputs;
    int wheel;                   // -4 (fierce left) to 4 (fierce right)
    int wheel_fields;            // fields the wheel has been held turning
    bool low_gear;
    uint8_t out1;                // port 01
    uint8_t pedal_select;        // DA02

    Ldv1000 *player;
    uint8_t ld_in, ld_out;       // the latches

    uint64_t cycles;
    uint64_t fields;
    uint64_t next_field, next_strobe, irq_end;
    unsigned overlay_serial;
} GpwBoard;

bool gpw_recognizes(const char *path);
bool gpw_load_roms(GpwBoard *board, const char *path);
const char *gpw_game_name(const GpwBoard *board);

// The attract mode's sound (DSW2 bit 4, on when 0).
void gpw_set_quiet_attract(GpwBoard *board, bool quiet);
bool gpw_quiet_attract(const GpwBoard *board);

void gpw_reset(GpwBoard *board, Ldv1000 *player);
void gpw_run(GpwBoard *board, uint64_t cycles);

// Draw the characters and sprites: GPW_OVERLAY_W x GPW_OVERLAY_H pixels,
// as 0xAARRGGBB, alpha 0 where the picture shows through.
void gpw_draw_overlay(const GpwBoard *board, uint32_t *pixels);

#endif
