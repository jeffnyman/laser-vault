#include "games/funai/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Three dumps of the program are known; the tile graphics and PROMs are the
// same in all. Each chip goes by MAME's name or by the one the Daphne
// emulator used. (Names and checksums from MAME's esh.cpp.)

typedef struct {
    const char *names[2];
    size_t size;
    uint32_t crc;
} RomChip;

typedef struct {
    const char *title;
    bool tested;    // known to work with laser-vault
    RomChip program[2];
} RomSet;

#define IS1 {{"is1.h8", "h8_is1.bin"}, 0x2000, 0x114C912B}

static const RomSet rom_sets[] = {
    {"Esh's Aurunmilla (set 1)", true, {IS1, {{"is2.f8", "f8_is2.bin"}, 0x2000, 0x0E3B6E62}}},
    {"Esh's Aurunmilla (set 2)", false, {IS1, {{"is2.f8", "f8_is2.bin"}, 0x2000, 0x7A562F49}}},
    {"Esh's Aurunmilla (set 3)", false,
     {{{"1.h8", NULL}, 0x2000, 0x8D27D363}, {{"is2.f8", "f8_is2.bin"}, 0x2000, 0x0E3B6E62}}},
};

// The tile graphics, one bit plane per chip, and the colour PROM.
static const RomChip tile_chips[3] = {
    {{"a.m3", "m3_a.bin"}, 0x1000, 0xA04736D8},
    {{"b.l3", "l3_b.bin"}, 0x1000, 0x9366DDE7},
    {{"c.k3", "k3_c.bin"}, 0x1000, 0xA936EF01},
};
static const RomChip colour_prom = {{"rgb.j1", "j1_rgb.bin"}, 0x200, 0x1E9F795F};

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

// The chip's name in this set, or NULL if it isn't there.
static const char *chip_name(const char *path, const RomChip *chip)
{
    for (int i = 0; i < 2; i++)
        if (chip->names[i] && roms_has(path, chip->names[i])) return chip->names[i];
    return NULL;
}

// Read and check one chip. Sets with the same name for different chips
// (set 2's program) are told apart by checksum, so a mismatch only warns.
static bool load_chip(const char *path, const RomChip *chip, uint8_t *dest, uint32_t *crc_out)
{
    const char *name = chip_name(path, chip);
    uint8_t *data = NULL;
    size_t size = 0;
    if (!name || !roms_read(path, name, &data, &size)) {
        fprintf(stderr, "ROMs: %s looks like Esh's Aurunmilla, but %s is missing\n", path, chip->names[0]);
        return false;
    }
    if (size != chip->size) {
        fprintf(stderr, "ROMs: %s is %zu bytes, expected %zu\n", name, size, chip->size);
        free(data);
        return false;
    }
    uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
    if (crc_out) *crc_out = crc;
    memcpy(dest, data, size);
    free(data);
    return true;
}

static void warn_crc(const RomChip *chip, uint32_t crc)
{
    if (crc != chip->crc)
        fprintf(stderr, "ROMs: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                        "modified chip?); trying it anyway\n", chip->names[0], crc, chip->crc);
}

bool funai_recognizes(const char *path)
{
    return chip_name(path, &rom_sets[0].program[0]) || chip_name(path, &rom_sets[2].program[0]);
}

// The colour PROM has 512 entries, addressed (from the top bit down) by
// whether the tiles are being shown at all, whether the disc's picture is
// off, the tile's colour (4 bits) and the pixel's value (3 bits). Bit 7 of
// an entry makes the pixel see-through; bits 0-2 are red, 3-4 green and 5-6
// blue. With the picture on, pixel value 0 is see-through in every colour;
// with it off, it's a dark blue background. (The first 256 entries, for
// outside the tiles, are all see-through or that blue.) The levels are
// spread evenly; the resistor values aren't known.
static void decode_palette(FunaiBoard *b, const uint8_t prom[0x200])
{
    for (int i = 0; i < 0x200; i++) {
        uint8_t v = prom[i];
        if (v & 0x80) {
            b->palette[i] = 0;
            continue;
        }
        uint32_t r = (v & 7) * 255 / 7, g = ((v >> 3) & 3) * 255 / 3, bl = ((v >> 5) & 3) * 255 / 3;
        b->palette[i] = 0xFF000000u | r << 16 | g << 8 | bl;
    }
}

bool funai_load_roms(FunaiBoard *b, const char *path)
{
    // Pick the set by the program's checksums.
    uint8_t chips[2][0x2000];
    uint32_t crcs[2];
    const RomSet *set = NULL;
    const RomChip *first = chip_name(path, &rom_sets[0].program[0]) ? &rom_sets[0].program[0]
                                                                    : &rom_sets[2].program[0];
    if (!load_chip(path, first, chips[0], &crcs[0]) || !load_chip(path, &rom_sets[0].program[1], chips[1], &crcs[1]))
        return false;
    for (size_t i = 0; i < COUNT(rom_sets) && !set; i++)
        if (rom_sets[i].program[0].crc == crcs[0] && rom_sets[i].program[1].crc == crcs[1]) set = &rom_sets[i];
    if (!set) {
        set = &rom_sets[0];
        warn_crc(&set->program[0], crcs[0]);
        warn_crc(&set->program[1], crcs[1]);
    }
    b->moves_known = crcs[1] == rom_sets[0].program[1].crc;
    memcpy(b->rom, chips[0], 0x2000);
    memcpy(b->rom + 0x2000, chips[1], 0x2000);

    uint8_t prom[0x200];
    uint32_t crc;
    for (int i = 0; i < 3; i++) {
        if (!load_chip(path, &tile_chips[i], b->tiles + i * 0x1000, &crc)) return false;
        warn_crc(&tile_chips[i], crc);
    }
    if (!load_chip(path, &colour_prom, prom, &crc)) return false;
    warn_crc(&colour_prom, crc);
    decode_palette(b, prom);
    printf("ROMs: %s%s\n", set->title, set->tested ? "" : " (not yet tested with laser-vault)");
    return true;
}

const char *funai_game_name(const FunaiBoard *b)
{
    (void)b;
    return "Esh's Aurunmilla";
}

// ---------------------------------------------------------------------------
// The address space
// ---------------------------------------------------------------------------

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    FunaiBoard *b = ctx;
    if (addr < 0x4000) return b->rom[addr];
    if (addr >= 0xE000 && addr < 0xE800) return b->ram[addr - 0xE000];
    if (addr >= 0xF000 && addr < 0xF400) return b->tile_ram[addr - 0xF000];
    if (addr >= 0xF400 && addr < 0xF800) return b->attr_ram[addr - 0xF400];
    return 0xFF;  // nothing there
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    FunaiBoard *b = ctx;
    if (addr >= 0xE000 && addr < 0xE800) {
        b->ram[addr - 0xE000] = v;
    } else if (addr >= 0xF000 && addr < 0xF400) {
        if (b->tile_ram[addr - 0xF000] != v) b->overlay_serial++;
        b->tile_ram[addr - 0xF000] = v;
    } else if (addr >= 0xF400 && addr < 0xF800) {
        if (b->attr_ram[addr - 0xF400] != v) b->overlay_serial++;
        b->attr_ram[addr - 0xF400] = v;
    }
}

// ---------------------------------------------------------------------------
// Autoplay
// ---------------------------------------------------------------------------
//
// Each scene has a record in the program (from 2180), and the game keeps a
// pointer to the one it's playing at E408. While a prompt is up the game
// steps E407 from 1 to 3 through the scene's window (0 before it, 4 after)
// and looks for a newly pressed control. Counting the record's bytes in
// hex: one in byte 15 is the move it's asking for (it lights the button
// lamps from it), and takes the game on to the scene at bytes 16-18; one
// in byte 19 goes to the scene at 1A-1C instead, and one in byte 11 counts
// as something else again if it comes in the window's first part. E432 is set once a press has been taken. Controls
// are bits 0-5 in the order of port F1: up, down, left, right, buttons 1
// and 2.
//
// Autoplay presses byte 15's controls through the window, on and off a few
// fields at a time, as the game wants a new press and treats a control
// already held as the window opens as old.

#define SCENE_POINTER 0xE408
#define SCENE_PHASE 0xE407
#define SCENE_PRESS_TAKEN 0xE432

bool funai_can_autoplay(const FunaiBoard *b)
{
    return b->moves_known;
}

void funai_set_autoplay(FunaiBoard *b, bool on)
{
    b->autoplay = on && b->moves_known;
}

static uint8_t ram_byte(const FunaiBoard *b, uint16_t addr)
{
    return b->ram[addr - 0xE000];
}

static void add_autoplay(const FunaiBoard *b, FunaiInputs *in)
{
    unsigned phase = ram_byte(b, SCENE_PHASE);
    unsigned scene = ram_byte(b, SCENE_POINTER) | ram_byte(b, SCENE_POINTER + 1) << 8;
    if (phase < 1 || phase > 3 || ram_byte(b, SCENE_PRESS_TAKEN)) return;
    if (scene < 0x2000 || scene + 0x20 > sizeof b->rom || (b->fields & 4)) return;
    uint8_t want = b->rom[scene + 0x15] & 0x3F;
    if (phase == 1 && (b->rom[scene + 0x11] & want)) return;  // wait for the move to mean the move
    in->up |= (want & 0x01) != 0;
    in->down |= (want & 0x02) != 0;
    in->left |= (want & 0x04) != 0;
    in->right |= (want & 0x08) != 0;
    in->button1 |= (want & 0x10) != 0;
    in->button2 |= (want & 0x20) != 0;
}

static uint8_t read_coins(const FunaiInputs *in)
{
    // Bit 4 is the service button (not wired up here).
    uint8_t v = 0xFF;
    if (in->coin1) v &= ~0x01;
    if (in->coin2) v &= ~0x02;
    if (in->start1) v &= ~0x04;
    if (in->start2) v &= ~0x08;
    return v;
}

static uint8_t read_player(const FunaiInputs *in)
{
    uint8_t v = 0xFF;
    if (in->up) v &= ~0x01;
    if (in->down) v &= ~0x02;
    if (in->left) v &= ~0x04;
    if (in->right) v &= ~0x08;
    if (in->button1) v &= ~0x10;
    if (in->button2) v &= ~0x20;
    return v;
}

static void run_sound(FunaiBoard *b, uint64_t now);

static uint8_t io_in(void *ctx, uint16_t port)
{
    FunaiBoard *b = ctx;
    switch (port & 0xFF) {
    case 0xF0: return read_coins(&b->inputs);
    case 0xF1: {
        FunaiInputs controls = b->inputs;
        if (b->autoplay) add_autoplay(b, &controls);
        return read_player(&controls);
    }
    case 0xF4: return ldv1000_read_status(b->player, b->cycles);
    default: return 0xFF;  // F2: player 2's controls (none here), F3: unused
    }
}

static void io_out(void *ctx, uint16_t port, uint8_t v)
{
    FunaiBoard *b = ctx;
    switch (port & 0xFF) {
    case 0xF4: b->ld_out = v; break;
    case 0xF5:
        run_sound(b, b->cycles);  // the beeper changes from now
        if ((v ^ b->misc) & 0x08) b->overlay_serial++;
        b->misc = v;
        break;
    case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD:
        b->lamps[(port & 0xFF) - 0xF8] = v;
        break;
    case 0xFF: b->nmi_enabled = v & 1; break;
    default: break;  // FE: pulsed at each interrupt
    }
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------
//
// A beeper: port F5's bit 1 switches a square wave on. Its pitch isn't known
// (MAME guesses 2 kHz, and so do we).

#define BEEP_HZ 2000.0
#define BEEP_LEVEL 0.25f

static void run_sound(FunaiBoard *b, uint64_t now)
{
    if (!b->rendering) return;
    const double cycles_per_sample = (double)FUNAI_CPU_HZ / FUNAI_SAMPLE_RATE;
    bool on = b->misc & 0x02;
    while (b->next_sample <= (double)now) {
        b->next_sample += cycles_per_sample;
        float s = 0;
        if (on) {
            b->beep_phase += BEEP_HZ / FUNAI_SAMPLE_RATE;
            if (b->beep_phase >= 1) b->beep_phase -= 1;
            s = b->beep_phase < 0.5 ? BEEP_LEVEL : -BEEP_LEVEL;
        }
        if (b->sample_count < FUNAI_MAX_SAMPLES) b->samples[b->sample_count++] = s;
    }
}

void funai_start_sound(FunaiBoard *b)
{
    b->rendering = true;
    b->next_sample = (double)b->cycles;
    b->sample_count = 0;
}

int funai_take_samples(FunaiBoard *b, const float **samples)
{
    run_sound(b, b->cycles);
    *samples = b->samples;
    int n = b->sample_count;
    b->sample_count = 0;
    return n;
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// The cycle field n starts on: 59.94 fields a second.
static uint64_t field_start(uint64_t n)
{
    return n * FUNAI_CPU_HZ * 1001 / 60000;
}

// The player's command strobe comes 554 us into each field (see
// ldp/ldv1000.c).
#define STROBE_CYCLES (FUNAI_CPU_HZ * 554 / 1000000)

void funai_reset(FunaiBoard *b, Ldv1000 *player)
{
    b->player = player;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->ram, 0, sizeof b->ram);
    memset(b->tile_ram, 0, sizeof b->tile_ram);
    memset(b->attr_ram, 0, sizeof b->attr_ram);
    b->ld_out = 0xFF;
    b->misc = 0;
    b->nmi_enabled = false;
    b->cycles = 0;
    b->fields = 0;
    b->next_field = field_start(1);
    b->next_strobe = STROBE_CYCLES;
    b->next_sample = 0;
    b->sample_count = 0;
    b->overlay_serial++;
    z80_init(&b->cpu, (Z80Bus){.ctx = b, .read = mem_read, .write = mem_write, .in = io_in, .out = io_out});
}

void funai_run(FunaiBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        if (b->cycles >= b->next_field) {
            // Vertical blank: the interrupt, held until the CPU takes it.
            b->cpu.irq = true;
            b->cpu.irq_data = 0xFF;
            b->fields++;
            b->next_field = field_start(b->fields + 1);
        }
        if (b->cycles >= b->next_strobe) {
            // The player takes the byte the game left, and its strobe
            // interrupts the game to leave the next one.
            ldv1000_write(b->player, b->cycles, b->ld_out);
            if (b->nmi_enabled) b->cpu.nmi = true;
            b->next_strobe = field_start(b->fields + 1) + STROBE_CYCLES;
        }
        b->cycles += (uint64_t)z80_step(&b->cpu);
        if (b->cpu.irq_taken) {
            b->cpu.irq = false;
            b->cpu.irq_taken = false;
        }
    }
}

// Each tile is 8x8 pixels of 3 bits, one bit from each graphics chip (8
// bytes per tile, a byte per row, the leftmost pixel in the top bit; chip
// c gives the top bit). A tile's attribute picks its colour and which 256
// tiles it comes from; bit 7 makes the whole tile blink, bit 6 just every
// other line of it (both every 8 frames, as far as is known).
void funai_draw_overlay(const FunaiBoard *b, uint32_t *pixels)
{
    const uint32_t *palette = b->palette + 0x100 + ((b->misc & 0x08) ? 0x80 : 0);
    bool blink = (b->fields / 2) & 8;
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int i = row * 32 + col;
            uint8_t attr = b->attr_ram[i];
            const uint32_t *colours = palette + (attr & 0x0F) * 8;
            int tile = b->tile_ram[i] + ((attr & 0x10) ? 0x100 : 0);
            bool hidden = blink && (attr & 0x80);
            for (int y = 0; y < 8; y++) {
                uint32_t *out = pixels + (row * 8 + y) * FUNAI_OVERLAY_W + col * 8;
                bool line_off = hidden || (blink && (attr & 0x40) && !(y & 1));
                uint8_t p0 = line_off ? 0 : b->tiles[tile * 8 + y];
                uint8_t p1 = line_off ? 0 : b->tiles[0x1000 + tile * 8 + y];
                uint8_t p2 = line_off ? 0 : b->tiles[0x2000 + tile * 8 + y];
                for (int x = 0; x < 8; x++) {
                    int bit = 7 - x;
                    int pen = ((p2 >> bit) & 1) << 2 | ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                    out[x] = colours[pen];
                }
            }
        }
    }
}
