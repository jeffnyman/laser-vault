#include "games/konami/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// MAME knows two sets; the second has its devices at other addresses, and
// isn't supported here. (Names and checksums from MAME's konblands.cpp.)

typedef struct {
    const char *name;
    size_t size;
    uint32_t crc;
} RomChip;

static const RomChip program_chips[2] = {
    {"badlands.a13", 0x2000, 0xA44776D6},  // C000-DFFF
    {"badlands.a14", 0x2000, 0x82CB4614},  // E000-FFFF
};
static const RomChip tile_chip = {"badlands.c8", 0x2000, 0x590209FE};
static const RomChip colour_prom = {"badlands.c4", 0x20, 0x6757BE8D};

bool konami_recognizes(const char *path)
{
    return roms_has(path, program_chips[0].name);
}

// Read and check one chip. A checksum that doesn't match only warns.
static bool load_chip(const char *path, const RomChip *chip, uint8_t *dest)
{
    uint8_t *data = NULL;
    size_t size = 0;
    if (!roms_read(path, chip->name, &data, &size)) {
        fprintf(stderr, "ROMs: %s looks like Badlands, but %s is missing\n", path, chip->name);
        return false;
    }
    if (size != chip->size) {
        fprintf(stderr, "ROMs: %s is %zu bytes, expected %zu\n", chip->name, size, chip->size);
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

// Each colour PROM entry is 3 bits of red (bits 0-2), 3 of green (3-5) and
// 2 of blue (6-7), through resistors weighted (from MAME) as below. Only
// the first 16 entries are used.
static void decode_palette(KonamiBoard *b, const uint8_t prom[0x20])
{
    static const int weights[3] = {0x21, 0x47, 0x97};
    for (int i = 0; i < 16; i++) {
        uint8_t v = prom[i];
        int r = 0, g = 0, bl = 0;
        for (int bit = 0; bit < 3; bit++) {
            if (v & (1 << bit)) r += weights[bit];
            if (v & (1 << (bit + 3))) g += weights[bit];
        }
        if (v & 0x40) bl += weights[1];
        if (v & 0x80) bl += weights[2];
        b->palette[i] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
    }
}

bool konami_load_roms(KonamiBoard *b, const char *path)
{
    uint8_t prom[0x20];
    if (!load_chip(path, &program_chips[0], b->rom) || !load_chip(path, &program_chips[1], b->rom + 0x2000) ||
        !load_chip(path, &tile_chip, b->tiles) || !load_chip(path, &colour_prom, prom))
        return false;
    decode_palette(b, prom);
    b->dsw1 = 0xFF;  // 1 coin, 1 credit
    b->dsw2 = 0x7E;  // 3 lives, sound in the attract mode, unused switches off
    printf("ROMs: Badlands (Konami, set 1)\n");
    return true;
}

const char *konami_game_name(const KonamiBoard *b)
{
    (void)b;
    return "Badlands";
}

// Free play is one of the coinage settings, all of DIP bank 1 on; turning
// it off goes back to 1 coin, 1 credit. Demo sounds are bank 2's switch 8.
void konami_set(KonamiBoard *b, KonamiSetting setting, bool on)
{
    if (setting == KONAMI_FREE_PLAY) {
        if (on) b->dsw1 = 0x00;
        else if (b->dsw1 == 0x00) b->dsw1 = 0xFF;
    } else {
        if (on) b->dsw2 |= 0x80;
        else b->dsw2 &= (uint8_t)~0x80;
    }
}

bool konami_get(const KonamiBoard *b, KonamiSetting setting)
{
    if (setting == KONAMI_FREE_PLAY) return b->dsw1 == 0x00;
    return b->dsw2 & 0x80;
}

// ---------------------------------------------------------------------------
// The address space
// ---------------------------------------------------------------------------

static uint8_t read_inputs(const KonamiInputs *in)
{
    // Bit 2 is the service button (not wired up here).
    uint8_t v = 0xFF;
    if (in->coin1) v &= ~0x01;
    if (in->coin2) v &= ~0x02;
    if (in->start1) v &= ~0x08;
    if (in->start2) v &= ~0x10;
    if (in->fire) v &= ~0x20;
    return v;
}

// The sound chip sees the latch's bits in reverse order.
static uint8_t reverse_bits(uint8_t v)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) r = (uint8_t)(r << 1 | ((v >> i) & 1));
    return r;
}

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    KonamiBoard *b = ctx;
    if (addr >= 0xC000) return b->rom[addr - 0xC000];
    if (addr >= 0x4000 && addr < 0x4800) return b->tile_ram[addr - 0x4000];
    if (addr >= 0x4800 && addr < 0x4C00) return b->ram[addr - 0x4800];
    switch (addr) {
    case 0x0000: return b->dsw2;
    case 0x0800: return b->dsw1;
    case 0x1000: return ldv1000_read_status(b->player, b->cycles);
    case 0x1800: return read_inputs(&b->inputs);
    default: return 0xFF;  // nothing there (8000-9FFF: a diagnostics ROM, if fitted)
    }
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    KonamiBoard *b = ctx;
    if (addr >= 0x4000 && addr < 0x4800) {
        if (b->tile_ram[addr - 0x4000] != v) b->overlay_serial++;
        b->tile_ram[addr - 0x4000] = v;
        return;
    }
    if (addr >= 0x4800 && addr < 0x4C00) {
        b->ram[addr - 0x4800] = v;
        return;
    }
    switch (addr) {
    case 0x0000: sn76496_write(&b->sound, b->cycles, reverse_bits(b->sound_latch)); break;
    case 0x0800: b->ld_out = v; break;
    case 0x1800: b->sound_latch = v; break;
    case 0x1000: case 0x1001: case 0x1002: case 0x1003:
    case 0x1004: case 0x1005: case 0x1006: case 0x1007:
        if (addr == 0x1003 && ((v ^ b->controls[3]) & 1)) b->overlay_serial++;
        b->controls[addr - 0x1000] = v;
        if (addr == 0x1006 && !(v & 1)) b->irq_pending = false;
        if (addr == 0x1007 && !(v & 1)) b->firq_pending = false;
        break;
    default: break;  // 5800: the watchdog
    }
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// The cycle field n starts on: 59.94 fields a second.
static uint64_t field_start(uint64_t n)
{
    return n * KONAMI_CPU_HZ * 1001 / 60000;
}

// The player's command strobe comes 554 us into each field (see
// ldp/ldv1000.c).
#define STROBE_CYCLES (KONAMI_CPU_HZ * 554 / 1000000)

// The FIRQ comes eight times a field (every 32 lines, as far as is known).
#define FIRQS_PER_FIELD 8

static uint64_t firq_time(uint64_t n)
{
    return n * KONAMI_CPU_HZ * 1001 / (60000 * FIRQS_PER_FIELD);
}

void konami_reset(KonamiBoard *b, Ldv1000 *player)
{
    b->player = player;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->tile_ram, 0, sizeof b->tile_ram);
    memset(b->ram, 0, sizeof b->ram);
    memset(b->controls, 0, sizeof b->controls);
    b->ld_out = 0xFF;
    b->sound_latch = 0;
    b->irq_pending = b->firq_pending = false;
    b->cycles = 0;
    b->fields = 0;
    b->next_field = field_start(1);
    b->next_strobe = STROBE_CYCLES;
    b->firq_ticks = 0;
    b->next_firq = firq_time(1);
    b->overlay_serial++;
    sn76496_free(&b->sound);
    sn76496_init(&b->sound, KONAMI_CPU_HZ, KONAMI_CPU_HZ, KONAMI_SAMPLE_RATE);  // the chip runs at the CPU's clock
    m6809_init(&b->cpu, (M6809Bus){.ctx = b, .read = mem_read, .write = mem_write});
}

void konami_run(KonamiBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        if (b->cycles >= b->next_field) {
            if (b->controls[4] & 1) b->cpu.nmi = true;  // vertical blank
            b->fields++;
            b->next_field = field_start(b->fields + 1);
        }
        if (b->cycles >= b->next_firq) {
            if (b->controls[7] & 1) b->firq_pending = true;
            b->firq_ticks++;
            b->next_firq = firq_time(b->firq_ticks + 1);
        }
        if (b->cycles >= b->next_strobe) {
            // The player takes the byte the game left, and its strobe
            // starts the game's work for the field.
            ldv1000_write(b->player, b->cycles, b->ld_out);
            if (b->controls[6] & 1) b->irq_pending = true;
            b->next_strobe = field_start(b->fields + 1) + STROBE_CYCLES;
        }
        b->cpu.irq = b->irq_pending;
        b->cpu.firq = b->firq_pending;
        b->cycles += (uint64_t)m6809_step(&b->cpu);
        // Each interrupt stays waiting until the game clears it, by
        // writing 0 to its enable (which every handler does first).
        b->cpu.irq_taken = b->cpu.firq_taken = false;
    }
}

bool konami_disc_audio_on(const KonamiBoard *b)
{
    return b->controls[5] & 1;
}

// Each tile is 8x8 pixels of 4 bits, 32 bytes: a row is 4 bytes, two
// pixels a byte, the left one in the top half. Pen 0 is see-through,
// unless 1003 is set: the game sets it to hide the picture while the disc
// searches.
#define FIRST_COL 4
#define FIRST_ROW 2

void konami_draw_overlay(const KonamiBoard *b, uint32_t *pixels)
{
    bool see_through = !(b->controls[3] & 1);
    for (int row = 0; row < KONAMI_OVERLAY_H / 8; row++) {
        for (int col = 0; col < KONAMI_OVERLAY_W / 8; col++) {
            int tile_number = b->tile_ram[(row + FIRST_ROW) * 64 + col + FIRST_COL];
            const uint8_t *tile = b->tiles + tile_number * 32;
            for (int y = 0; y < 8; y++) {
                uint32_t *out = pixels + (row * 8 + y) * KONAMI_OVERLAY_W + col * 8;
                for (int x = 0; x < 8; x++) {
                    uint8_t byte = tile[y * 4 + x / 2];
                    int pen = (x & 1) ? byte & 0x0F : byte >> 4;
                    out[x] = (pen == 0 && see_through) ? 0 : b->palette[pen];
                }
            }
        }
    }
}
