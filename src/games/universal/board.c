#include "games/universal/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Super Don Quix-ote came in two versions. The first had long scenes; a
// later program cut them shorter (with a new character set to match), and
// there are two dumps of it. All use the same disc. (Names and checksums
// from MAME's superdq.cpp.)

typedef struct {
    const char *name;
    size_t size;
    uint32_t crc;
} RomChip;

typedef struct {
    const char *title;
    bool tested;    // known to work with laser-vault
    RomChip program, tiles, colours;
} RomSet;

#define COLOUR_PROM {"sdq-cprm.bin", 0x20, 0x96701569}

static const RomSet rom_sets[] = {
    {"Super Don Quix-ote (Long Scenes)", true,
     {"sdq-prog.bin", 0x4000, 0x96B931E2}, {"sdq-char.bin", 0x2000, 0x5FB0E440}, COLOUR_PROM},
    {"Super Don Quix-ote (Short Scenes)", false,
     {"sdq_c45.rom", 0x4000, 0x0F4D4832}, {"sdq_a8.rom", 0x2000, 0x7D981A14}, COLOUR_PROM},
    {"Super Don Quix-ote (Short Scenes, Alternate)", false,
     {"sdq_c45a.rom", 0x4000, 0xB12CE1F8}, {"sdq_a8.rom", 0x2000, 0x7D981A14}, COLOUR_PROM},
};

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

// The DIP switches, which the game reads at start-up. A switch on reads
// as 0. DSW1: bits 0-1 difficulty (both off = easy), bit 2 off = sound in
// the attract mode, bit 3 off = a bonus life at 100,000 points, bits 4-5
// lives (both off = 3), bit 6 on = unlimited lives, bit 7 on = free play.
// DSW2: bits 2-3 and 0-1 the coinage of the two slots (all off = 1 coin a
// credit). The factory setting is every switch off. (Layout from MAME;
// the game's own code agrees.)
#define DEFAULT_DSW1 0xFF
#define DEFAULT_DSW2 0xFF

static const struct {
    uint8_t bit;  // in DSW1; a setting is on when its bit is clear
} settings[] = {
    [UNI_FREE_PLAY] = {0x80},
    [UNI_UNLIMITED_LIVES] = {0x40},
    [UNI_QUIET_ATTRACT] = {0x04},
};

static bool has_set(const char *path, const RomSet *set)
{
    return roms_has(path, set->program.name);
}

bool uni_recognizes(const char *path)
{
    for (size_t i = 0; i < COUNT(rom_sets); i++)
        if (has_set(path, &rom_sets[i])) return true;
    return false;
}

static bool load_chip(const char *path, const RomSet *set, const RomChip *chip, uint8_t *dest)
{
    uint8_t *data = NULL;
    size_t size = 0;
    if (!roms_read(path, chip->name, &data, &size)) {
        fprintf(stderr, "ROMs: %s looks like %s, but %s is missing\n", path, set->title, chip->name);
        return false;
    }
    if (size != chip->size) {
        fprintf(stderr, "ROMs: %s is %zu bytes, expected %zu\n", chip->name, size, chip->size);
        free(data);
        return false;
    }
    // A wrong checksum means a bad copy or a modified chip. Warn, but carry
    // on: it might still run.
    uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
    if (crc != chip->crc)
        fprintf(stderr, "ROMs: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                        "modified chip?); trying it anyway\n", chip->name, crc, chip->crc);
    memcpy(dest, data, size);
    free(data);
    return true;
}

// The colour PROM holds 32 colours, one byte each: red in bits 7-5, green
// in bits 4-2, blue in bits 1-0, most significant bit first. Each bit drives
// the monitor's input through a resistor (200, 390 and 820 ohms, the
// smallest for the top bit), into a 220 ohm load. So each bit adds a fixed
// share of the full voltage: the conductance of its resistor over the
// network's total. Blue has only the 200 and 390 ohm resistors, so its
// brightest is a little dimmer than red's or green's, which are scaled to
// reach 255.
static void decode_palette(UniBoard *b, const uint8_t prom[32])
{
    const double load = 1.0 / 220;
    const double g200 = 1.0 / 200, g390 = 1.0 / 390, g820 = 1.0 / 820;
    double rg_total = g200 + g390 + g820 + load, b_total = g200 + g390 + load;
    double rg[3] = {g200 / rg_total, g390 / rg_total, g820 / rg_total};  // top bit first
    double bl[2] = {g200 / b_total, g390 / b_total};
    double scale = 255 / (rg[0] + rg[1] + rg[2]);

    for (int i = 0; i < 32; i++) {
        uint8_t v = prom[i];
        double r = rg[0] * ((v >> 7) & 1) + rg[1] * ((v >> 6) & 1) + rg[2] * ((v >> 5) & 1);
        double g = rg[0] * ((v >> 4) & 1) + rg[1] * ((v >> 3) & 1) + rg[2] * ((v >> 2) & 1);
        double bb = bl[0] * ((v >> 1) & 1) + bl[1] * (v & 1);
        uint32_t r8 = (uint32_t)(r * scale + 0.5), g8 = (uint32_t)(g * scale + 0.5), b8 = (uint32_t)(bb * scale + 0.5);
        b->palette[i] = 0xFF000000u | r8 << 16 | g8 << 8 | b8;
    }
}

bool uni_load_roms(UniBoard *b, const char *path)
{
    const RomSet *set = NULL;
    for (size_t i = 0; i < COUNT(rom_sets) && !set; i++)
        if (has_set(path, &rom_sets[i])) set = &rom_sets[i];
    if (!set) {
        fprintf(stderr, "ROMs: found no Super Don Quix-ote ROMs in %s\n", path);
        return false;
    }

    uint8_t prom[32];
    if (!load_chip(path, set, &set->program, b->rom) || !load_chip(path, set, &set->tiles, b->tiles) ||
        !load_chip(path, set, &set->colours, prom))
        return false;
    decode_palette(b, prom);
    printf("ROMs: %s%s\n", set->title, set->tested ? "" : " (not yet tested with laser-vault)");
    b->dsw1 = DEFAULT_DSW1;
    b->dsw2 = DEFAULT_DSW2;
    return true;
}

void uni_set(UniBoard *b, UniSetting setting, bool on)
{
    if (on) b->dsw1 &= (uint8_t)~settings[setting].bit;
    else b->dsw1 |= settings[setting].bit;
}

bool uni_get(const UniBoard *b, UniSetting setting)
{
    return !(b->dsw1 & settings[setting].bit);
}

const char *uni_game_name(const UniBoard *b)
{
    (void)b;
    return "Super Don Quix-ote";
}

// ---------------------------------------------------------------------------
// The address space
// ---------------------------------------------------------------------------

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    UniBoard *b = ctx;
    if (addr < 0x4000) return b->rom[addr];
    if (addr < 0x4800) return b->ram[addr - 0x4000];
    if (addr >= 0x5C00 && addr < 0x6000) return b->video_ram[addr - 0x5C00];
    return 0xFF;  // nothing there
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    UniBoard *b = ctx;
    if (addr >= 0x4000 && addr < 0x4800) {
        b->ram[addr - 0x4000] = v;
    } else if (addr >= 0x5C00 && addr < 0x6000) {
        if (b->video_ram[addr - 0x5C00] != v) b->overlay_serial++;
        b->video_ram[addr - 0x5C00] = v;
    }
}

static uint8_t read_joystick(const UniInputs *in)
{
    uint8_t v = 0xFF;
    if (in->left) v &= ~0x02;
    if (in->down) v &= ~0x08;
    if (in->right) v &= ~0x20;
    if (in->up) v &= ~0x80;
    return v;
}

static uint8_t read_buttons(const UniInputs *in)
{
    // Bit 0 is the service button, bit 1 the test button, bit 4 the second
    // player's button (for a cocktail table); none are wired up here.
    uint8_t v = 0xFF;
    if (in->coin2) v &= ~0x04;
    if (in->coin1) v &= ~0x08;
    if (in->button) v &= ~0x20;
    if (in->start2) v &= ~0x40;
    if (in->start1) v &= ~0x80;
    return v;
}

static uint8_t io_in(void *ctx, uint16_t port)
{
    UniBoard *b = ctx;
    switch (port & 0xFF) {
    case 0x00: return read_joystick(&b->inputs);
    case 0x01: return read_buttons(&b->inputs);
    case 0x02: return b->dsw1;
    case 0x03: return b->dsw2;
    case 0x04: return b->ld_in;
    default: return 0xFF;
    }
}

// Port 08: bit 6 acknowledges the interrupt; bit 1 picks the second set of
// 16 colours for the tiles; bit 7 makes the "black" colours see-through
// (see uni_draw_overlay); bits 3 and 2 drive the coin counters.
static void misc_write(UniBoard *b, uint8_t v)
{
    if (v & 0x40) b->cpu.irq = false;
    if ((v ^ b->misc) & 0x82) b->overlay_serial++;
    b->misc = v;
}

static void io_out(void *ctx, uint16_t port, uint8_t v)
{
    UniBoard *b = ctx;
    switch (port & 0xFF) {
    case 0x00: b->ld_out = v; break;
    case 0x04: sn76496_write(&b->sound, b->cycles, v); break;
    case 0x08: misc_write(b, v); break;
    default: break;  // 0C-0D: the video timing chip, set up once for the overlay's size
    }
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// The cycle field n starts on: 59.94 fields a second.
static uint64_t field_start(uint64_t n)
{
    return n * UNI_CPU_HZ * 1001 / 60000;
}

void uni_reset(UniBoard *b, Ldv1000 *player)
{
    b->player = player;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->ram, 0, sizeof b->ram);
    memset(b->video_ram, 0, sizeof b->video_ram);
    sn76496_free(&b->sound);
    sn76496_init(&b->sound, UNI_CPU_HZ, UNI_CPU_HZ, UNI_SAMPLE_RATE);  // the chip runs at the CPU's clock
    b->ld_in = 0;
    b->ld_out = 0xFF;
    b->misc = 0;
    b->cycles = 0;
    b->fields = 0;
    b->next_field = field_start(1);
    b->overlay_serial++;
    z80_init(&b->cpu, (Z80Bus){.ctx = b, .read = mem_read, .write = mem_write, .in = io_in, .out = io_out});
}

// The start of a field: the player link's strobes (status in, command out),
// and the interrupt, which stays raised until the game acknowledges it.
// (On the real board the strobes come a little after the field starts,
// timed by the player; MAME does them together, and so do we.)
static void start_field(UniBoard *b)
{
    b->ld_in = ldv1000_read_status(b->player, b->cycles);
    ldv1000_write(b->player, b->cycles, b->ld_out);
    b->cpu.irq = true;
    b->cpu.irq_data = 0xFF;  // nothing drives the data bus: RST 38h in mode 0
    b->fields++;
    b->next_field = field_start(b->fields + 1);
}

void uni_run(UniBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        if (b->cycles >= b->next_field) start_field(b);
        b->cycles += (uint64_t)z80_step(&b->cpu);
    }
}

// Each tile is 8x8 pixels, 4 bits each, 32 bytes: a row is four bytes, two
// pixels to a byte, the left one in the high four bits. A pixel's value
// picks one of 16 colours in the current half of the palette. With port 08's
// bit 7 set, colours 7 and 15 of the first half and all of the second half
// are see-through: those are the PROM's "black" entries.
void uni_draw_overlay(const UniBoard *b, uint32_t *pixels)
{
    int bank = (b->misc & 0x02) ? 16 : 0;
    bool see_through = b->misc & 0x80;
    uint32_t colours[16];
    for (int i = 0; i < 16; i++) {
        int pen = bank + i;
        bool clear = see_through && (pen == 7 || pen >= 15);
        colours[i] = clear ? 0 : b->palette[pen];
    }

    for (int row = 0; row < UNI_OVERLAY_H / 8; row++) {
        for (int col = 0; col < UNI_OVERLAY_W / 8; col++) {
            const uint8_t *tile = b->tiles + b->video_ram[row * 32 + col] * 32;
            for (int y = 0; y < 8; y++) {
                uint32_t *out = pixels + (row * 8 + y) * UNI_OVERLAY_W + col * 8;
                for (int x = 0; x < 8; x++) {
                    uint8_t byte = tile[y * 4 + x / 2];
                    out[x] = colours[(x & 1) ? byte & 0x0F : byte >> 4];
                }
            }
        }
    }
}
