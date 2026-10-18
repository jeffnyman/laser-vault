#include "games/dataeast/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Two revisions of Bega's Battle, which differ in five program chips (a
// set may hold only those, with the rest in the other set's zip beside
// it), Data East's own Cobra Command, and Road Blaster. (Names and
// checksums from MAME's deco_ld.cpp.)

typedef struct {
    const char *name;
    uint32_t crc;
    int where;     // which region: see load_set
    uint32_t at;   // offset in it
} RomChip;

enum { MAIN, SOUND, GFX };

typedef struct {
    DecoGame game;
    const char *title;
    RomChip chips[13];  // the unused ones at the end have no name
} RomSet;

#define COMMON_CHIPS                                                                              \
    {"an01", 0x15F8921D, MAIN, 0x8000}, {"an06", 0xCBBCD730, SOUND, 0x0000},                       \
    {"an0a", 0xE429305D, GFX, 0x0000}, {"an07", 0x6B8AD735, GFX, 0x2000},                          \
    {"an0b", 0x09E4B780, GFX, 0x4000}, {"an08", 0xB5518391, GFX, 0x6000},                          \
    {"an0c", 0x0C127207, GFX, 0x8000}, {"an09", 0xB7375FD7, GFX, 0xA000}

static const RomSet sets[] = {
    {DECO_BEGAS, "Bega's Battle (Data East, revision 3)",
     {{"an05-3", 0xC917A283, MAIN, 0x0000}, {"an04-3", 0x935B2B0A, MAIN, 0x2000},
      {"an03-3", 0x79438D80, MAIN, 0x4000}, {"an02-3", 0x98CE4CA0, MAIN, 0x6000},
      {"an00-3", 0x124A3A36, MAIN, 0xA000}, COMMON_CHIPS}},
    {DECO_BEGAS, "Bega's Battle (Data East, revision 1)",
     {{"an05", 0x91A05549, MAIN, 0x0000}, {"an04", 0x670966FE, MAIN, 0x2000},
      {"an03", 0xD2D85CDF, MAIN, 0x4000}, {"an02", 0x84D13C20, MAIN, 0x6000},
      {"an00", 0x184297F3, MAIN, 0xA000}, COMMON_CHIPS}},
    // Its program starts at 8000.
    {DECO_COBRA, "Cobra Command (Data East's version)",
     {{"au03-2", 0x8F0A8FBA, MAIN, 0x4000}, {"au02-2", 0x7DB11ACF, MAIN, 0x6000},
      {"au01-2", 0x523DD8F6, MAIN, 0x8000}, {"au00-2", 0x6C0F1F16, MAIN, 0xA000},
      {"au06", 0xCCC94EB0, SOUND, 0x0000},
      {"au0a", 0x6AAEDCF3, GFX, 0x0000}, {"au07", 0xD4BF12A5, GFX, 0x2000},
      {"au0b", 0x92247877, GFX, 0x4000}, {"au08", 0x63158274, GFX, 0x6000},
      {"au0c", 0xD00A2762, GFX, 0x8000}, {"au09", 0x74E93394, GFX, 0xA000}}},
    // Its program starts at C000, and both layers have the same graphics
    // (chips 03 and 06, 04 and 07, 05 and 08 match; each pair dumped once).
    {DECO_RBLASTER, "Road Blaster",
     {{"03-06.bin", 0xD1FF5FFB, GFX, 0x0000}, {"03-06.bin", 0xD1FF5FFB, GFX, 0x2000},
      {"04-07.bin", 0xDA2C84D9, GFX, 0x4000}, {"04-07.bin", 0xDA2C84D9, GFX, 0x6000},
      {"05-08.bin", 0x4608B516, GFX, 0x8000}, {"05-08.bin", 0x4608B516, GFX, 0xA000},
      {"01.bin", 0xE4733C49, MAIN, 0x8000}, {"00.bin", 0x084D6AE2, MAIN, 0xA000},
      {"02.bin", 0x6C20335D, SOUND, 0x0000}}},
};
#define SET_COUNT (int)(sizeof sets / sizeof sets[0])

static const RomSet *find_set(const char *path)
{
    for (int i = 0; i < SET_COUNT; i++)
        if (roms_has(path, sets[i].chips[0].name)) return &sets[i];
    return NULL;
}

bool deco_recognizes(const char *path)
{
    return find_set(path) != NULL;
}

// Read and check one chip, from path or a zip beside it. A checksum that
// doesn't match only warns.
static bool load_chip(const char *path, const char *title, const RomChip *chip, uint8_t *dest)
{
    char nearby[1024];
    const char *from = path;
    if (!roms_has(path, chip->name) && roms_find_nearby(path, chip->name, nearby, sizeof nearby)) from = nearby;
    uint8_t *data = NULL;
    size_t size = 0;
    if (!roms_read(from, chip->name, &data, &size)) {
        fprintf(stderr, "ROMs: %s looks like %s, but %s is missing\n", path, title, chip->name);
        return false;
    }
    if (size != 0x2000) {
        fprintf(stderr, "ROMs: %s is %zu bytes, expected 8192\n", chip->name, size);
        free(data);
        return false;
    }
    uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
    if (crc != chip->crc)
        fprintf(stderr, "ROMs: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                        "modified chip?); trying it anyway\n", chip->name, crc, chip->crc);
    memcpy(dest, data, size);
    free(data);
    return true;
}

bool deco_load_roms(DecoBoard *b, const char *path)
{
    const RomSet *set = find_set(path);
    if (!set) return false;
    memset(b->rom, 0xFF, sizeof b->rom);
    for (int i = 0; i < 13 && set->chips[i].name; i++) {
        const RomChip *chip = &set->chips[i];
        uint8_t *region = chip->where == MAIN ? b->rom : chip->where == SOUND ? b->sound_rom : b->gfx;
        if (!load_chip(path, set->title, chip, region + chip->at)) return false;
    }
    b->game = set->game;
    // The DIP switches, as the game's code reads them (a switch is on when
    // its bit is 0; MAME and Daphne list them as unknown):
    //
    //     DSW1  bits 0-1  coin slot 1: 1 coin 1 credit (11), 3 coins 1
    //                     credit (10), 1 coin 2 credits (01), 2 coins 1
    //                     credit (00)
    //           bits 2-3  coin slot 2, the same way
    //     DSW2  bits 0-1  lives: 3 (11), 5 (10), 4 (01), 6 (00)
    //           bit 2     continues: on when 0
    //           bits 3-4  the bonus life (at 00, also: holding the button
    //                     fires repeatedly)
    //           bit 5     difficulty (0 seems the harder)
    //           bit 6     1: one player only
    //           bit 7     1: the game; 0: the board's test program
    //
    // There's no free play setting, and no switch for the attract mode's
    // sound, that the code shows.
    //
    // Cobra Command reads DSW1 the same way. Its DSW2, from its code:
    //
    //     DSW2  bit 0     lives: 3 (1), 5 (0)
    //           bits 1-2  the score for a bonus life
    //           bits 3-4  pick from two tables of settings (the difficulty?)
    //           bit 5     1: the disc's sound in the attract mode
    //           bit 6     read at start-up; unknown
    //           bit 7     1: the game; 0: the board's test program
    //
    // Road Blaster's DSW1 is the same again. Its DSW2, from its code (and
    // Hypseus's notes):
    //
    //     DSW2  bit 0     cars: 3 (1), 5 (0)
    //           bits 1-2  the score for a bonus car
    //           bit 4     difficulty: normal (1), hard (0)
    //           bit 5     1: the disc's sound in the attract mode
    //           bit 6     1: the game starts at the first stage; 0: at a
    //                     random one
    //           bit 7     1: the game; 0: it plays itself, ignoring the
    //                     controls
    b->dsw1 = 0x0F;  // 1 coin 1 credit in both slots
    if (b->game == DECO_COBRA || b->game == DECO_RBLASTER)
        b->dsw2 = 0xFF;  // all off: 3 lives, the first bonus setting, sound in the attract mode, the game
    else
        b->dsw2 = 0xBB;  // 3 lives, continues, the first bonus setting, one or two players, the game
    printf("ROMs: %s\n", set->title);
    return true;
}

const char *deco_game_name(const DecoBoard *b)
{
    return b->game == DECO_COBRA ? "Cobra Command" : b->game == DECO_RBLASTER ? "Road Blaster" : "Bega's Battle";
}

// Only Cobra Command and Road Blaster have one of these on their switches:
// the attract mode's sound (DSW2 bit 5).
static bool has_attract_sound_switch(const DecoBoard *b)
{
    return b->game == DECO_COBRA || b->game == DECO_RBLASTER;
}

void deco_set(DecoBoard *b, DecoSetting setting, bool on)
{
    if (!has_attract_sound_switch(b) || setting != DECO_QUIET_ATTRACT) return;
    if (on) b->dsw2 &= (uint8_t)~0x20;
    else b->dsw2 |= 0x20;
}

bool deco_get(const DecoBoard *b, DecoSetting setting)
{
    return has_attract_sound_switch(b) && setting == DECO_QUIET_ATTRACT && !(b->dsw2 & 0x20);
}

// ---------------------------------------------------------------------------
// The serial chip
// ---------------------------------------------------------------------------
//
// A Motorola 6850. Its status register: bit 0, a byte has arrived; bit 1,
// ready to send another; bit 7, it is interrupting. The control register's
// bit 7 enables the interrupt for arriving bytes, and bits 5-6 set to 01
// one for being ready to send; 03 in bits 0-1 resets it.
//
// The line runs at 9600 baud, a byte (with start and stop bits) every
// 1.04 ms.

#define SERIAL_BYTE_CYCLES (DECO_CPU_HZ * 10 / 9600)

static bool acia_irq(const DecoBoard *b)
{
    if ((b->acia_control & 0x80) && b->rx_full) return true;
    return (b->acia_control & 0x60) == 0x20 && !b->tx_busy;
}

static uint8_t acia_status(const DecoBoard *b)
{
    return (uint8_t)((b->rx_full ? 0x01 : 0) | (b->tx_busy ? 0 : 0x02) | (acia_irq(b) ? 0x80 : 0));
}

// Move bytes along the line: the one being sent reaches the player, and
// the player's next reply arrives if there's room for it.
static void serial_tick(DecoBoard *b)
{
    if (b->tx_busy) {
        ldp1000_write(b->player, b->cycles, b->tx_data);
        b->tx_busy = false;
    }
    if (!b->rx_full && ldp1000_has_reply(b->player, b->cycles)) {
        b->rx_data = ldp1000_read(b->player, b->cycles);
        b->rx_full = true;
    }
}

// ---------------------------------------------------------------------------
// The address spaces
// ---------------------------------------------------------------------------

// The cycle field n starts on: 59.94 fields a second.
static uint64_t field_start(uint64_t n)
{
    return n * DECO_CPU_HZ * 1001 / 60000;
}

// Vertical blank: the last 22 of the field's 262.5 lines.
static bool in_vblank(const DecoBoard *b)
{
    uint64_t field = b->cycles * 60000 / ((uint64_t)DECO_CPU_HZ * 1001);
    uint64_t start = field_start(field), length = field_start(field + 1) - start;
    return (b->cycles - start) * 525 >= length * 2 * 240;
}

// Cobra Command has up and down the other way round from Bega's Battle
// (its gun sight moves up the screen on bit 0), and so does Road Blaster
// (the next letter of your initials is on bit 0). Road Blaster's wheel is
// left and right, its accelerator bit 4 and its brake bit 5; it reads
// nothing else.
static uint8_t read_controls(const DecoBoard *b)
{
    const DecoInputs *in = &b->inputs;
    bool up_is_0 = b->game == DECO_COBRA || b->game == DECO_RBLASTER;
    uint8_t v = 0xFF;  // bit 7: tilt (the game resets if it reads 0)
    if (up_is_0 ? in->up : in->down) v &= ~0x01;
    if (up_is_0 ? in->down : in->up) v &= ~0x02;
    if (in->left) v &= ~0x04;
    if (in->right) v &= ~0x08;
    if (b->game == DECO_RBLASTER) {
        if (in->button1) v &= ~0x10;
        if (in->button2) v &= ~0x20;
        return v;
    }
    if (in->button1) v &= ~0x40;
    if (in->button2) v &= ~0x20;
    if (in->button3) v &= ~0x10;
    return v;
}

// Coins read 1 when in, the start buttons 0 when pressed.
static uint8_t read_dsw1(const DecoBoard *b)
{
    uint8_t v = (uint8_t)((b->dsw1 & 0x0F) | 0x30);
    if (b->inputs.start2) v &= ~0x10;
    if (b->inputs.start1) v &= ~0x20;
    if (b->inputs.coin1) v |= 0x40;
    if (b->inputs.coin2) v |= 0x80;
    return v;
}

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    DecoBoard *b = ctx;
    if (addr >= 0x4000) return b->rom[addr - 0x4000];
    if (addr < 0x1000) return b->ram[addr];
    if (addr >= 0x2000) return b->video[(addr - 0x2000) & ~0x0800];
    if (addr >= 0x1800) return b->colours[addr - 0x1800];
    switch (addr) {
    case 0x1000: return read_controls(b);
    case 0x1001: return read_dsw1(b);
    case 0x1002: return b->dsw2;
    case 0x1003: return in_vblank(b) ? 0x80 : 0x00;
    case 0x1005: return 0xBF;  // the sound board's status (only the test program reads it)
    case 0x1006: return acia_status(b);
    case 0x1007:
        b->rx_full = false;
        return b->rx_data;
    default: return 0xFF;
    }
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    DecoBoard *b = ctx;
    if (addr >= 0x4000) return;
    if (addr < 0x1000) {
        b->ram[addr] = v;
        return;
    }
    if (addr >= 0x2000) {
        uint8_t *p = &b->video[(addr - 0x2000) & ~0x0800];
        if (*p != v) b->overlay_serial++;
        *p = v;
        return;
    }
    if (addr >= 0x1800) {
        if (b->colours[addr - 0x1800] != v) b->overlay_serial++;
        b->colours[addr - 0x1800] = v;
        return;
    }
    switch (addr) {
    case 0x1004:
        b->sound_command = v;
        b->sound_irq = true;
        break;
    case 0x1006:
        if ((v & 0x03) == 0x03) b->rx_full = b->tx_busy = false;  // reset
        b->acia_control = v;
        break;
    case 0x1007:
        b->tx_data = v;
        b->tx_busy = true;
        break;
    default: break;  // 1000: coin lockout; 1001-1003: unknown
    }
}

static uint8_t sound_read(void *ctx, uint16_t addr)
{
    DecoBoard *b = ctx;
    if (addr >= 0xE000) return b->sound_rom[addr - 0xE000];
    if (addr < 0x2000) return b->sound_ram[addr & 0x7FF];
    if (addr == 0xA000) {
        b->sound_irq = false;
        return b->sound_command;
    }
    return 0xFF;
}

static void sound_write(void *ctx, uint16_t addr, uint8_t v)
{
    DecoBoard *b = ctx;
    if (addr < 0x2000) {
        b->sound_ram[addr & 0x7FF] = v;
        return;
    }
    switch (addr) {
    case 0x2000: ay8910_write(&b->ay[0], b->sound_cycles, b->ay_reg[0], v); break;
    case 0x4000: b->ay_reg[0] = v & 0x0F; break;
    case 0x6000: ay8910_write(&b->ay[1], b->sound_cycles, b->ay_reg[1], v); break;
    case 0x8000: b->ay_reg[1] = v & 0x0F; break;
    default: break;  // A000: a reply to the main CPU, which the game doesn't read
    }
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

#define AY_HZ 1500000
#define SOUND_NMI_HZ 640

void deco_reset(DecoBoard *b, Ldp1000 *player)
{
    b->player = player;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->ram, 0, sizeof b->ram);
    memset(b->sound_ram, 0, sizeof b->sound_ram);
    memset(b->colours, 0xFF, sizeof b->colours);  // inverted: black
    memset(b->video, 0, sizeof b->video);
    b->coin_was_in = false;
    b->sound_command = 0;
    b->sound_irq = false;
    b->acia_control = 0;
    b->rx_full = b->tx_busy = false;
    b->cycles = b->sound_cycles = 0;
    b->sound_nmis = 0;
    b->next_serial = SERIAL_BYTE_CYCLES;
    b->overlay_serial++;
    for (int i = 0; i < 2; i++) {
        ay8910_free(&b->ay[i]);
        ay8910_init(&b->ay[i], DECO_CPU_HZ, AY_HZ, DECO_SAMPLE_RATE);
        b->ay_reg[i] = 0;
    }
    m6502_init(&b->cpu, (M6502Bus){.ctx = b, .read = mem_read, .write = mem_write});
    m6502_init(&b->sound_cpu, (M6502Bus){.ctx = b, .read = sound_read, .write = sound_write});
}

void deco_run(DecoBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        if (b->cycles >= b->next_serial) {
            serial_tick(b);
            b->next_serial += SERIAL_BYTE_CYCLES;
        }
        bool coin_in = b->inputs.coin1 || b->inputs.coin2;
        if (coin_in && !b->coin_was_in) b->cpu.nmi = true;
        b->coin_was_in = coin_in;
        b->cpu.irq = acia_irq(b);
        b->cycles += (uint64_t)m6502_step(&b->cpu);

        // The sound CPU keeps up.
        while (b->sound_cycles < b->cycles) {
            uint64_t next_nmi = (b->sound_nmis + 1) * DECO_CPU_HZ / SOUND_NMI_HZ;
            if (b->sound_cycles >= next_nmi) {
                b->sound_cpu.nmi = true;
                b->sound_nmis++;
            }
            b->sound_cpu.irq = b->sound_irq;
            b->sound_cycles += (uint64_t)m6502_step(&b->sound_cpu);
        }
    }
}

void deco_start_sound(DecoBoard *b)
{
    for (int i = 0; i < 2; i++) ay8910_set_rendering(&b->ay[i], true, b->sound_cycles);
}

int deco_take_samples(DecoBoard *b, const float **samples)
{
    const float *s[2];
    int n[2];
    for (int i = 0; i < 2; i++) {
        ay8910_run(&b->ay[i], b->sound_cycles);
        n[i] = ay8910_take_samples(&b->ay[i], &s[i]);
    }
    int count = n[0] < n[1] ? n[0] : n[1];
    if (count > b->sample_capacity) {
        b->sample_capacity = count * 2;
        b->samples = realloc(b->samples, (size_t)b->sample_capacity * sizeof *b->samples);
    }
    for (int i = 0; i < count; i++) b->samples[i] = (s[0][i] + s[1][i]) * 0.5f;
    *samples = b->samples;
    return count;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
//
// The graphics chips hold three bit planes, 0x4000 bytes apart (the pen's
// top bit in the last). Layer 1 uses the first half of each plane, layer 0
// the second. An 8x8 tile is 8 bytes, a 16x16 sprite 32 (its left half,
// then its right); both are stored bottom row first, with the leftmost
// pixel in bit 0.
//
// Colour RAM holds groups of 8 colours; tiles use group 5 and sprites
// group 6, with pen 0 see-through.

#define TILE_COLOURS (5 * 8)
#define SPRITE_COLOURS (6 * 8)

static int gfx_pen(const DecoBoard *b, int offset, int bit)
{
    return ((b->gfx[offset + 0x8000] >> bit) & 1) << 2 | ((b->gfx[offset + 0x4000] >> bit) & 1) << 1 |
           ((b->gfx[offset] >> bit) & 1);
}

static uint32_t colour(const DecoBoard *b, int index)
{
    static const int weights[3] = {0x21, 0x47, 0x97};
    uint8_t v = (uint8_t)~b->colours[index];
    int r = 0, g = 0, bl = 0;
    for (int bit = 0; bit < 3; bit++) {
        if (v & (1 << bit)) r += weights[bit];
        if (v & (1 << (bit + 3))) g += weights[bit];
    }
    if (v & 0x40) bl += weights[1];
    if (v & 0x80) bl += weights[2];
    return 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
}

// The overlay starts at the tile layers' second row.
#define FIRST_LINE 8

static void draw_sprites(const DecoBoard *b, const uint8_t *layer, int bank, uint32_t *pixels)
{
    for (int i = 0; i < 0x40; i += 4) {
        const uint8_t *s = layer + (i < 0x20 ? i : 0x3E0 + i - 0x20);
        if (!(s[0] & 1)) continue;
        bool flip_x = s[0] & 0x04, flip_y = s[0] & 0x02;
        int base = (bank + s[1]) * 32;
        for (int y = 0; y < 16; y++) {
            int sy = s[2] + y - FIRST_LINE;
            if (sy < 0 || sy >= DECO_OVERLAY_H) continue;
            int row = flip_y ? y : 15 - y;
            for (int x = 0; x < 16; x++) {
                int sx = s[3] + x;
                if (sx >= DECO_OVERLAY_W) continue;
                int col = flip_x ? 15 - x : x;
                int pen = gfx_pen(b, base + row + (col >= 8 ? 16 : 0), col & 7);
                if (pen) pixels[sy * DECO_OVERLAY_W + sx] = colour(b, SPRITE_COLOURS + pen);
            }
        }
    }
}

// One tile at a layer's column and row (row 0 is the hidden top row).
static void draw_tile(const DecoBoard *b, int tile, int col, int layer_row, uint32_t *pixels)
{
    int top = layer_row * 8 - FIRST_LINE;
    if (col < 0 || col >= 32 || top < 0 || top >= DECO_OVERLAY_H) return;
    for (int y = 0; y < 8; y++) {
        uint32_t *out = pixels + (top + y) * DECO_OVERLAY_W + col * 8;
        for (int x = 0; x < 8; x++) {
            int pen = gfx_pen(b, tile * 8 + 7 - y, x);
            if (pen) out[x] = colour(b, TILE_COLOURS + pen);
        }
    }
}

static void draw_tiles(const DecoBoard *b, const uint8_t *layer, int bank, const bool *skip, uint32_t *pixels)
{
    for (int row = FIRST_LINE / 8; row < FIRST_LINE / 8 + DECO_OVERLAY_H / 8; row++) {
        for (int col = 0; col < 32; col++) {
            int index = row * 32 + col;
            if (skip && skip[index]) continue;
            draw_tile(b, bank + (layer[index] | (layer[0x400 + index] & 3) << 8), col, row, pixels);
        }
    }
}

// Cobra Command's canopy struts. With the cockpit, the game draws two
// diagonal bars from the top corners, six tiles at a time from tables in
// its ROM (at C8BF and C8C5), each run 0x23 (or -0x1D) on from the last in
// layer 1. The tiles only join up if that part of the layer runs in
// columns, 32 tiles each, though the cockpit and all the text run in rows;
// read as rows, the left strut is a line of broken dashes and the right
// one lands where the cockpit is drawn over it. So, while the left strut's
// tiles are there, those cells are left out and both struts are drawn as
// columns. Drawn that way, each strut runs from a top corner down to the
// post on the cockpit's top edge that's there for it. (How the real board
// manages this isn't known.)
static const uint8_t strut_tiles[2][6] = {{0x5B, 0x5F, 0x5E, 0x59, 0x5D, 0x5C}, {0x29, 0x41, 0x40, 0x2B, 0x43, 0x42}};
static const int strut_start[2] = {-0x001, 0x3DF};  // the first run, of which only tiles 2-5 are drawn
static const int strut_step[2] = {0x23, -0x1D};

static bool cobra_struts_shown(const DecoBoard *b)
{
    if (b->game != DECO_COBRA) return false;
    for (int i = 0; i < 6; i++)
        if (b->video[0x1000 + strut_start[0] + strut_step[0] + i] != strut_tiles[0][i]) return false;
    return true;
}

static void draw_cobra_struts(const DecoBoard *b, bool *skip, uint32_t *pixels)
{
    for (int s = 0; s < 2; s++) {
        for (int run = 0; run < 8; run++) {
            int base = strut_start[s] + run * strut_step[s];
            for (int i = run == 0 ? 2 : 0; i < 6; i++) {
                int at = base + i;
                draw_tile(b, 0x200 + strut_tiles[s][i], at >> 5, at & 31, pixels);
                if (s == 0 && run > 0) skip[at] = true;
            }
        }
    }
}

void deco_draw_overlay(const DecoBoard *b, uint32_t *pixels)
{
    memset(pixels, 0, sizeof *pixels * DECO_OVERLAY_W * DECO_OVERLAY_H);
    draw_sprites(b, b->video + 0x1000, 0x000, pixels);
    draw_sprites(b, b->video, 0x100, pixels);
    draw_tiles(b, b->video, 0x400, NULL, pixels);
    bool skip[0x400] = {false};
    if (cobra_struts_shown(b)) draw_cobra_struts(b, skip, pixels);
    draw_tiles(b, b->video + 0x1000, 0x000, skip, pixels);
}
