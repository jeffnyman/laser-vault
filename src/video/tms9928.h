// tms9928.h - the TI TMS9918/9928 family of video display processors.
//
// A chip that draws a 256 x 192 picture from its own 16 KB of video RAM:
// a background of 8 x 8 characters and up to 32 sprites. Laserdisc games
// (Cliff Hanger's board has a TMS9128) use it to draw over the disc's
// picture: wherever the chip draws colour 0, "transparent", the video shows
// through.
//
// The CPU talks to it through two ports. The control port takes two bytes
// at a time, either a register write (value, then 0x80 + register number) or
// a video RAM address (low byte, then the high six bits, plus 0x40 to write
// rather than read); the data port then reads or writes video RAM, moving
// the address on by one each time. Reading the control port returns the
// status byte: bit 7 is set at the end of each picture (the vertical blank)
// and, with register 1's bit 5 set, holds the interrupt line active until
// the CPU reads it; bit 6 says a line had five sprites (bits 0-4 say which
// was the fifth); bit 5 says two sprites touched.
//
// Registers: 0 bit 1 = graphics mode 2, bit 0 = external video; 1 bit 6 =
// display on, bit 5 = interrupts, bit 4 = text mode, bit 3 = multicolour
// mode, bit 1 = 16 x 16 sprites, bit 0 = double-size sprites; 2-6 the
// tables' addresses; 7 the text colour (high four bits) and backdrop (low).
// Details from TI's data manual (SPPU004).

#ifndef LV_TMS9928_H
#define LV_TMS9928_H

#include <stdbool.h>
#include <stdint.h>

// The picture, and the border around it as much as a TV shows (13 pixels
// each side, 24 lines top and bottom), filled with the backdrop colour.
#define TMS9928_ACTIVE_W 256
#define TMS9928_ACTIVE_H 192
#define TMS9928_BORDER_X 13
#define TMS9928_BORDER_Y 24
#define TMS9928_W (TMS9928_ACTIVE_W + 2 * TMS9928_BORDER_X)
#define TMS9928_H (TMS9928_ACTIVE_H + 2 * TMS9928_BORDER_Y)

typedef struct {
    uint8_t vram[0x4000];
    uint8_t reg[8];
    uint8_t status;
    uint16_t addr;           // video RAM address (14 bits)
    uint8_t latch;           // first byte of a control-port pair
    bool second_byte;        // waiting for the second byte of a pair
    uint8_t read_ahead;      // the byte the next data read returns
    unsigned serial;         // changes whenever the picture may have changed
} Tms9928;

void tms9928_reset(Tms9928 *vdp);

void tms9928_write_control(Tms9928 *vdp, uint8_t v);
void tms9928_write_data(Tms9928 *vdp, uint8_t v);
uint8_t tms9928_read_status(Tms9928 *vdp);
uint8_t tms9928_read_data(Tms9928 *vdp);

// The end of the picture: sets the status flags (vertical blank, fifth
// sprite, collision) as drawing it would have.
void tms9928_vblank(Tms9928 *vdp);

// Is the interrupt line active?
bool tms9928_irq(const Tms9928 *vdp);

// Draw the picture as it looks now: TMS9928_W x TMS9928_H pixels, as
// 0xAARRGGBB, alpha 0 where it's transparent.
void tms9928_draw(const Tms9928 *vdp, uint32_t *pixels);

#endif
