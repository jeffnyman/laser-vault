#include "video/tms9928.h"

#include <string.h>

// The 15 colours (and 0, transparent), as commonly measured from the
// TMS9928's video output.
static const uint32_t palette[16] = {
    0x00000000, 0xFF000000, 0xFF21C842, 0xFF5EDC78, 0xFF5455ED, 0xFF7D76FC, 0xFFD4524D, 0xFF42EBF5,
    0xFFFC5554, 0xFFFF7978, 0xFFD4C154, 0xFFE6CE80, 0xFF21B03B, 0xFFC95BBA, 0xFFCCCCCC, 0xFFFFFFFF,
};

void tms9928_reset(Tms9928 *vdp)
{
    unsigned serial = vdp->serial;
    memset(vdp, 0, sizeof *vdp);
    vdp->serial = serial + 1;
}

void tms9928_write_control(Tms9928 *vdp, uint8_t v)
{
    if (!vdp->second_byte) {
        vdp->latch = v;
        vdp->second_byte = true;
        return;
    }
    vdp->second_byte = false;
    if (v & 0x80) {
        vdp->reg[v & 7] = vdp->latch;
        vdp->serial++;
        return;
    }
    vdp->addr = (uint16_t)(((v & 0x3F) << 8) | vdp->latch);
    if (!(v & 0x40)) {  // set up for reading: fetch the first byte now
        vdp->read_ahead = vdp->vram[vdp->addr];
        vdp->addr = (vdp->addr + 1) & 0x3FFF;
    }
}

void tms9928_write_data(Tms9928 *vdp, uint8_t v)
{
    vdp->second_byte = false;
    if (vdp->vram[vdp->addr] != v) vdp->serial++;
    vdp->vram[vdp->addr] = v;
    vdp->read_ahead = v;
    vdp->addr = (vdp->addr + 1) & 0x3FFF;
}

uint8_t tms9928_read_data(Tms9928 *vdp)
{
    vdp->second_byte = false;
    uint8_t v = vdp->read_ahead;
    vdp->read_ahead = vdp->vram[vdp->addr];
    vdp->addr = (vdp->addr + 1) & 0x3FFF;
    return v;
}

uint8_t tms9928_read_status(Tms9928 *vdp)
{
    vdp->second_byte = false;
    uint8_t v = vdp->status;
    vdp->status &= 0x1F;  // clears the interrupt, fifth-sprite and collision flags
    return v;
}

bool tms9928_irq(const Tms9928 *vdp)
{
    return (vdp->status & 0x80) && (vdp->reg[1] & 0x20);
}

static bool text_mode(const Tms9928 *vdp)
{
    return vdp->reg[1] & 0x10;
}

// The sprites on one line of the picture. Up to four show on a line; the
// lowest-numbered win where they overlap. Fills colours[x] with each
// pixel's sprite colour (0 for none), and reports a fifth sprite and any
// collision (two sprites' pixels on the same spot, whatever their colour).
static void sprite_line(const Tms9928 *vdp, int line, uint8_t colours[256], int *fifth, bool *collision)
{
    memset(colours, 0, 256);
    *fifth = -1;
    *collision = false;
    if (text_mode(vdp)) return;

    bool covered[256] = {false};
    const uint8_t *attrs = vdp->vram + ((vdp->reg[5] & 0x7F) << 7);
    const uint8_t *patterns = vdp->vram + ((vdp->reg[6] & 7) << 11);
    int size = (vdp->reg[1] & 0x02) ? 16 : 8;
    int mag = (vdp->reg[1] & 0x01) ? 2 : 1;
    int shown = 0;
    for (int i = 0; i < 32; i++) {
        const uint8_t *a = attrs + i * 4;
        if (a[0] == 0xD0) break;  // end of the list
        int y = a[0] > 0xD0 ? a[0] - 255 : a[0] + 1;  // drawn from the line after its Y
        int row = (line - y) / mag;
        if (line < y || row >= size) continue;
        if (++shown > 4) {
            *fifth = i;
            break;
        }
        int x = a[1] - ((a[3] & 0x80) ? 32 : 0);  // the "early clock" bit moves it left 32
        uint8_t colour = a[3] & 0x0F;
        int name = size == 16 ? a[2] & 0xFC : a[2];
        // A 16 x 16 sprite is four 8 x 8 blocks: left column then right.
        uint16_t bits = patterns[name * 8 + row];
        if (size == 16) bits = (uint16_t)(bits << 8 | patterns[name * 8 + 16 + row]);
        else bits <<= 8;
        for (int px = 0; px < size * mag; px++) {
            int sx = x + px;
            if (sx < 0 || sx > 255) continue;
            if (!(bits & (0x8000 >> (px / mag)))) continue;
            if (covered[sx]) *collision = true;
            else {
                covered[sx] = true;
                colours[sx] = colour;
            }
        }
    }
}

void tms9928_vblank(Tms9928 *vdp)
{
    uint8_t status = vdp->status | 0x80;
    for (int line = 0; line < TMS9928_ACTIVE_H; line++) {
        uint8_t colours[256];
        int fifth;
        bool collision;
        sprite_line(vdp, line, colours, &fifth, &collision);
        if (collision) status |= 0x20;
        if (fifth >= 0 && !(status & 0x40)) status = (uint8_t)((status & 0xE0) | 0x40 | fifth);
    }
    vdp->status = status;
}

// The background's colour index (0-15) for one pixel of the picture.
static int background_pixel(const Tms9928 *vdp, int x, int y)
{
    const uint8_t *vram = vdp->vram;
    const uint8_t *r = vdp->reg;
    int backdrop = r[7] & 0x0F;
    if (text_mode(vdp)) {
        // 40 columns of 6-pixel characters, with 8 pixels of backdrop each side.
        if (x < 8 || x >= 248) return backdrop;
        int col = (x - 8) / 6, px = (x - 8) % 6;
        int name = vram[((r[2] & 0x0F) << 10) + (y / 8) * 40 + col];
        uint8_t bits = vram[((r[4] & 7) << 11) + name * 8 + (y & 7)];
        return (bits & (0x80 >> px)) ? r[7] >> 4 : backdrop;
    }
    int name = vram[((r[2] & 0x0F) << 10) + (y / 8) * 32 + x / 8];
    if (r[1] & 0x08) {
        // Multicolour: each character is 2 x 2 blocks of 4 x 4 pixels, their
        // colours from two pattern bytes chosen by the row.
        uint8_t b = vram[((r[4] & 7) << 11) + name * 8 + ((y / 8) & 3) * 2 + ((y / 4) & 1)];
        int c = (x & 4) ? b & 0x0F : b >> 4;
        return c ? c : backdrop;
    }
    uint8_t bits, colour;
    if (r[0] & 0x02) {
        // Graphics mode 2: each third of the screen has its own 256
        // patterns and colours, one colour byte per pattern row.
        int index = (y / 64) * 256 + name;
        bits = vram[((r[4] & 4) << 11) + (index & (((r[4] & 3) << 8) | 0xFF)) * 8 + (y & 7)];
        colour = vram[((r[3] & 0x80) << 6) + (index & (((r[3] & 0x7F) << 3) | 7)) * 8 + (y & 7)];
    } else {
        // Graphics mode 1: one colour byte for each group of 8 patterns.
        bits = vram[((r[4] & 7) << 11) + name * 8 + (y & 7)];
        colour = vram[(r[3] << 6) + name / 8];
    }
    int c = (bits & (0x80 >> (x & 7))) ? colour >> 4 : colour & 0x0F;
    return c ? c : backdrop;
}

void tms9928_draw(const Tms9928 *vdp, uint32_t *pixels)
{
    uint32_t backdrop = palette[vdp->reg[7] & 0x0F];
    for (int i = 0; i < TMS9928_W * TMS9928_H; i++) pixels[i] = backdrop;
    if (!(vdp->reg[1] & 0x40)) return;  // display off: all backdrop

    for (int y = 0; y < TMS9928_ACTIVE_H; y++) {
        uint8_t sprites[256];
        int fifth;
        bool collision;
        sprite_line(vdp, y, sprites, &fifth, &collision);
        uint32_t *out = pixels + (y + TMS9928_BORDER_Y) * TMS9928_W + TMS9928_BORDER_X;
        for (int x = 0; x < TMS9928_ACTIVE_W; x++)
            out[x] = palette[sprites[x] ? sprites[x] : background_pixel(vdp, x, y)];
    }
}
