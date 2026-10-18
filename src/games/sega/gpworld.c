#include "games/sega/gpworld.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Each chip goes by MAME's name or Daphne's (checksums from MAME's
// gpworld.cpp). The colour PROMs aren't needed.

enum { MAIN, CHARS, SPRITES };

typedef struct {
    const char *mame, *daphne;
    uint32_t size, crc;
    int where;
    uint32_t at;
} RomChip;

static const RomChip chips[] = {
    {"epr6162a.ic51", "gpw_6162a.bin", 0x4000, 0x70E42574, MAIN, 0x0000},
    {"epr6163.ic67", "gpw_6163.bin", 0x4000, 0x49539E46, MAIN, 0x4000},
    {"epr6164.ic83", "gpw_6164.bin", 0x4000, 0x7F0E6853, MAIN, 0x8000},
    {"epr6148.ic18", "gpw_6148.bin", 0x1000, 0xA4B11CF5, CHARS, 0x0000},
    {"epr6149.ic111", "gpw_6149.bin", 0x4000, 0x7E6C4797, SPRITES, 0x00000},
    {"epr6151.ic110", "gpw_6151.bin", 0x4000, 0x26D72B96, SPRITES, 0x04000},
    {"epr6150.ic128", "gpw_6150.bin", 0x4000, 0x6837E095, SPRITES, 0x08000},
    {"epr6152.ic127", "gpw_6152.bin", 0x4000, 0x86939FE2, SPRITES, 0x0C000},
    {"epr6153.ic109", "gpw_6153.bin", 0x4000, 0xF52B7C1B, SPRITES, 0x10000},
    {"epr6155.ic108", "gpw_6155.bin", 0x4000, 0xA020BD03, SPRITES, 0x14000},
    {"epr6154.ic126", "gpw_6154.bin", 0x4000, 0xBBF5D7DB, SPRITES, 0x18000},
    {"epr6156.ic125", "gpw_6156.bin", 0x4000, 0x2C03C64C, SPRITES, 0x1C000},
    {"epr6157.ic107", "gpw_6157.bin", 0x4000, 0xCDD31036, SPRITES, 0x20000},
    {"epr6158.ic124", "gpw_6158.bin", 0x4000, 0xD15AC707, SPRITES, 0x28000},
};
#define CHIP_COUNT (int)(sizeof chips / sizeof chips[0])

static const char *chip_name(const char *path, const RomChip *chip)
{
    if (roms_has(path, chip->mame)) return chip->mame;
    if (roms_has(path, chip->daphne)) return chip->daphne;
    return NULL;
}

bool gpw_recognizes(const char *path)
{
    return chip_name(path, &chips[0]) != NULL;
}

bool gpw_load_roms(GpwBoard *b, const char *path)
{
    if (!gpw_recognizes(path)) return false;
    memset(b, 0, sizeof *b);
    for (int i = 0; i < CHIP_COUNT; i++) {
        const RomChip *chip = &chips[i];
        const char *name = chip_name(path, chip);
        uint8_t *data = NULL;
        size_t size = 0;
        if (!name || !roms_read(path, name, &data, &size)) {
            fprintf(stderr, "ROMs: %s looks like GP World, but %s (%s) is missing\n", path, chip->mame,
                    chip->daphne);
            return false;
        }
        if (size != chip->size) {
            fprintf(stderr, "ROMs: %s is %zu bytes, expected %u\n", name, size, (unsigned)chip->size);
            free(data);
            return false;
        }
        uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
        if (crc != chip->crc)
            fprintf(stderr, "ROMs: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                            "modified chip?); trying it anyway\n", name, crc, chip->crc);
        uint8_t *region = chip->where == MAIN ? b->rom : chip->where == CHARS ? b->chars : b->sprites;
        memcpy(region + chip->at, data, size);
        free(data);
    }
    // The DIP switches (a switch on reads as 0; from MAME): DSW1 the
    // coinage, slot 1 in the top four bits, slot 2 in the bottom (all off:
    // 1 coin 1 credit). DSW2 bit 4 the attract mode's sound (0: on), bits
    // 5-6 the difficulty (11 medium, 10 easy, 01 hard, 00 hardest), bit 7
    // the first year's season (1: 1984), bit 0 the steering (1: medium),
    // bit 1 the place to qualify (1: fourth, 0: third).
    b->dsw1 = 0xFF;
    b->dsw2 = 0xEF;
    printf("ROMs: GP World (Sega)\n");
    return true;
}

const char *gpw_game_name(const GpwBoard *b)
{
    (void)b;
    return "GP World";
}

void gpw_set_quiet_attract(GpwBoard *b, bool quiet)
{
    if (quiet) b->dsw2 |= 0x10;
    else b->dsw2 &= (uint8_t)~0x10;
}

bool gpw_quiet_attract(const GpwBoard *b)
{
    return b->dsw2 & 0x10;
}

// ---------------------------------------------------------------------------
// The address spaces
// ---------------------------------------------------------------------------

// The wheel: bits 0-3 right, 4-7 left, active low, each further position
// closing the next switch as well.
static uint8_t read_wheel(const GpwBoard *b)
{
    uint8_t v = 0xFF;
    for (int i = 0; i < b->wheel && i < 4; i++) v &= (uint8_t)~(0x01 << i);
    for (int i = 0; i < -b->wheel && i < 4; i++) v &= (uint8_t)~(0x10 << i);
    return v;
}

// Once a field: a held key turns the wheel one position further every
// tenth of a second, up to the fourth; letting go centres it. The gear
// lever stays where it was last moved.
#define WHEEL_STEP_FIELDS 6

static void update_controls(GpwBoard *b)
{
    const GpwInputs *in = &b->inputs;
    int dir = in->right - in->left;
    if (!dir || (dir > 0) != (b->wheel > 0)) {
        b->wheel = 0;
        b->wheel_fields = 0;
    }
    if (dir) {
        if (b->wheel_fields++ % WHEEL_STEP_FIELDS == 0 && b->wheel * dir < 4) b->wheel += dir;
    }
    if (in->gear_up) b->low_gear = false;
    if (in->gear_down) b->low_gear = true;
}

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    GpwBoard *b = ctx;
    if (addr < 0xC000) return b->rom[addr];
    if (addr >= 0xE000) return b->ram[addr - 0xE000];
    if (addr < 0xC800) return b->sprite_ram[addr - 0xC000];
    if (addr < 0xD000) return b->colour_ram[addr - 0xC800];
    if (addr < 0xD800) return b->tile_ram[addr - 0xD000];
    switch (addr) {
    case 0xD800: return b->ld_in;
    case 0xDA00: return read_wheel(b);
    case 0xDA02:
        // The pedals go through an analogue-to-digital converter: writing
        // DA20 starts it, and bit 4 here reads 1 until it's done. Here it's
        // done at once.
        return (uint8_t)(b->pedal_select & ~0x10);
    case 0xDA20:
        if (b->pedal_select & 1) return b->inputs.gas ? 0xFF : 0x00;
        return b->inputs.brake ? 0xFF : 0x00;
    default: return 0xFF;
    }
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    GpwBoard *b = ctx;
    if (addr < 0xC000) return;
    if (addr >= 0xE000) {
        b->ram[addr - 0xE000] = v;
        return;
    }
    uint8_t *p = NULL;
    if (addr < 0xC800) p = &b->sprite_ram[addr - 0xC000];
    else if (addr < 0xD000) p = &b->colour_ram[addr - 0xC800];
    else if (addr < 0xD800) p = &b->tile_ram[addr - 0xD000];
    if (p) {
        if (*p != v) b->overlay_serial++;
        *p = v;
        return;
    }
    switch (addr) {
    case 0xD800: b->ld_out = v; break;
    case 0xDA02: b->pedal_select = v; break;
    default: break;  // DA01 and DAxx: the sound circuits
    }
}

static uint8_t io_in(void *ctx, uint16_t port)
{
    GpwBoard *b = ctx;
    const GpwInputs *in = &b->inputs;
    switch (port & 0xFF) {
    case 0x80: return b->low_gear ? 0xFF : 0xEF;
    case 0x81:
        return (uint8_t)~((in->coin1 ? 0x01 : 0) | (in->coin2 ? 0x02 : 0) | (in->test ? 0x04 : 0) |
                          (in->service ? 0x08 : 0) | (in->start1 ? 0x10 : 0));
    case 0x82: return b->dsw1;
    case 0x83: return b->dsw2;
    default: return 0xFF;
    }
}

static void io_out(void *ctx, uint16_t port, uint8_t v)
{
    GpwBoard *b = ctx;
    if ((port & 0xFF) == 0x01) b->out1 = v;
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

static uint64_t field_start(uint64_t n)
{
    return n * GPW_CPU_HZ * 1001 / 60000;
}

// The player's command strobe comes 554 us into each field (see
// ldp/ldv1000.c); the IRQ is held for 100 us (MAME).
#define STROBE_CYCLES ((uint64_t)GPW_CPU_HZ * 554 / 1000000)
#define IRQ_CYCLES ((uint64_t)GPW_CPU_HZ * 100 / 1000000)

void gpw_reset(GpwBoard *b, Ldv1000 *player)
{
    b->player = player;
    memset(&b->inputs, 0, sizeof b->inputs);
    b->wheel = b->wheel_fields = 0;
    b->low_gear = true;
    memset(b->sprite_ram, 0, sizeof b->sprite_ram);
    memset(b->colour_ram, 0, sizeof b->colour_ram);
    memset(b->tile_ram, 0, sizeof b->tile_ram);
    memset(b->ram, 0, sizeof b->ram);
    b->out1 = 0;
    b->pedal_select = 0;
    b->ld_in = b->ld_out = 0xFF;
    b->cycles = 0;
    b->fields = 0;
    b->next_field = field_start(1);
    b->next_strobe = STROBE_CYCLES;
    b->irq_end = 0;
    b->overlay_serial++;
    z80_init(&b->cpu, (Z80Bus){.ctx = b, .read = mem_read, .write = mem_write, .in = io_in, .out = io_out});
}

void gpw_run(GpwBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        if (b->cycles >= b->next_field) {
            b->cpu.irq = true;
            b->cpu.irq_data = 0xFF;
            b->irq_end = b->cycles + IRQ_CYCLES;
            update_controls(b);
            b->fields++;
            b->next_field = field_start(b->fields + 1);
        }
        if (b->cpu.irq && b->cycles >= b->irq_end) b->cpu.irq = false;
        if (b->cycles >= b->next_strobe) {
            // As on Astron Belt: the latches swap the player's status and the
            // game's byte, and the NMI tells the game, while the NMI is on.
            if (b->out1 & 0x40) {
                b->ld_in = ldv1000_read_status(b->player, b->cycles);
                ldv1000_write(b->player, b->cycles, b->ld_out);
                b->cpu.nmi = true;
            }
            b->next_strobe = field_start(b->fields + 1) + STROBE_CYCLES;
        }
        b->cycles += (uint64_t)z80_step(&b->cpu);
        if (b->cpu.irq_taken) {
            b->cpu.irq = false;
            b->cpu.irq_taken = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// A colour from the colour RAM: 4 bits each of red (bits 0-3 of the first
// byte), green (4-7) and blue (0-3 of the second), through resistors
// weighted (from Daphne) 0x8F, 0x43, 0x1F and 0x0E, top bit first. Black
// is see-through.
static uint32_t colour(const GpwBoard *b, int offset)
{
    static const uint8_t levels[16] = {
        0x00, 0x0E, 0x1F, 0x2D, 0x43, 0x51, 0x62, 0x70,
        0x8F, 0x9D, 0xAE, 0xBC, 0xD2, 0xE0, 0xF1, 0xFF,
    };
    uint8_t lo = b->colour_ram[offset], hi = b->colour_ram[offset + 1] & 0x0F;
    if (!lo && !hi) return 0;
    return 0xFF000000u | (uint32_t)levels[lo & 0x0F] << 16 | (uint32_t)levels[lo >> 4] << 8 | levels[hi];
}

// Set pixel (x, y) of the 512 x 256 grid, if it's in the part shown.
static void put(uint32_t *pixels, int x, int y, uint32_t c)
{
    x -= GPW_FIRST_X;
    y -= GPW_FIRST_LINE;
    if (c && x >= 0 && x < GPW_OVERLAY_W && y >= 0 && y < GPW_OVERLAY_H) pixels[y * GPW_OVERLAY_W + x] = c;
}

// A sprite is 8 bytes: top and bottom lines (0, 1), x (2, and bit 0 of
// 3), the colour group (3, bits 4-7) and graphics bank (bits 1-3), how far
// to move through the graphics before each line (4, 5), and where its
// graphics start (6, 7). Each line runs four pixels at a time until a
// pixel of 15 ends it; a start address with bit 15 set draws the line
// backwards, mirrored.
static void draw_sprite(const GpwBoard *b, const uint8_t *s, uint32_t *pixels)
{
    int src = s[6] | s[7] << 8;
    int skip = s[4] | s[5] << 8;
    int height = s[1] - s[0];
    int sx = s[2] | (s[3] & 1) << 8;
    int group = (s[3] >> 4) & 0x0F;
    uint32_t bank = (uint32_t)((s[3] >> 1) & 7) << 16;
    for (int row = 0; row < height; row++) {
        src = (src + skip) & 0xFFFF;
        int at = src;
        int x = sx, y = s[0] + 1 + row;
        for (int n = 0; n < 128; n++) {
            uint32_t lo_at = bank | (uint32_t)(at & 0x7FFF);
            if (lo_at + 0x8000 >= sizeof b->sprites) break;
            uint8_t lo = b->sprites[lo_at], hi = b->sprites[lo_at + 0x8000];
            int p[4] = {hi >> 4, hi & 0x0F, lo >> 4, lo & 0x0F};
            bool backwards = src & 0x8000;
            if (backwards) {
                int t = p[0];
                p[0] = p[3], p[3] = t;
                t = p[1], p[1] = p[2], p[2] = t;
                at--;
            } else {
                at++;
            }
            for (int i = 0; i < 4; i++)
                if (p[i] && p[i] != 0x0F) put(pixels, x + i, y, colour(b, 0x200 + (group * 16 + p[i]) * 2));
            x += 4;
            if (backwards ? (hi & 0xF0) == 0xF0 : (lo & 0x0F) == 0x0F) break;
        }
    }
}

// Sprites first, then the characters over them.
void gpw_draw_overlay(const GpwBoard *b, uint32_t *pixels)
{
    memset(pixels, 0, sizeof *pixels * GPW_OVERLAY_W * GPW_OVERLAY_H);
    for (int n = 0; n < 0x800; n += 8) {
        const uint8_t *s = b->sprite_ram + n;
        if (s[1] && s[1] > s[0] && s[2] != 0xFF) draw_sprite(b, s, pixels);
    }
    for (int row = 0; row < 32; row++) {
        for (int col = GPW_FIRST_X / 8; col < 64; col++) {
            int tile = b->tile_ram[row * 64 + col];
            for (int y = 0; y < 8; y++) {
                uint8_t p0 = b->chars[tile * 8 + y], p1 = b->chars[0x800 + tile * 8 + y];
                for (int x = 0; x < 8; x++) {
                    int bit = 7 - x;
                    int pen = ((p0 >> bit) & 1) | ((p1 >> bit) & 1) << 1;
                    if (pen) put(pixels, col * 8 + x, row * 8 + y, colour(b, (pen | (tile & 0xFC)) * 2));
                }
            }
        }
    }
}
