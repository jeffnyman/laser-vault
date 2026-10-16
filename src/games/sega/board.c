#include "games/sega/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Astron Belt came in two versions: one for Pioneer's LD-V1000 (MAME's
// astronp) and one for Sega's own player, driven over a serial link
// (astron). Only the first is supported. MAME keeps the Pioneer set as
// just its two program chips, astronp.zip, beside the rest in the Sega
// set's astron.zip. (Names and checksums from MAME's segald.cpp.)

typedef struct {
    const char *names[2];  // MAME's name, and the one some sets use
    size_t size;
    uint32_t crc;
} RomChip;

static const RomChip program_chips[2] = {
    {{"abp-ic03.bin", "5282"}, 0x4000, 0xFD0DCFC9},  // 0000-3FFF
    {{"abp-ic10.bin", "5283"}, 0x4000, 0xA3746393},  // 4000-7FFF
};
static const RomChip bank_chips[2] = {
    {{"5284", NULL}, 0x4000, 0xEEC6DB27},
    {{"5285", NULL}, 0x4000, 0x820E154E},
};
static const RomChip char_chips[2] = {
    {{"5280", NULL}, 0x800, 0x583AF1FF},
    {{"5281", NULL}, 0x800, 0x7B5C820C},
};
static const RomChip sprite_chips[2] = {
    {{"5286", NULL}, 0x4000, 0x8EB1C28E},
    {{"5338", NULL}, 0x4000, 0x94CA5F9A},
};
static const RomChip char_prom = {{"5279", NULL}, 0x200, 0x8716AEB5};

// The Sega player's version, recognized only to say it isn't supported.
static const RomChip sega_player_program = {{"5473c", NULL}, 0x4000, 0x0F24BAAA};

#define PARENT_ZIP "astron.zip"

// The chip's name in the set at path, or NULL if it isn't there.
static const char *chip_name(const char *path, const RomChip *chip)
{
    for (int i = 0; i < 2; i++)
        if (chip->names[i] && roms_has(path, chip->names[i])) return chip->names[i];
    return NULL;
}

bool sega_recognizes(const char *path)
{
    return chip_name(path, &program_chips[0]) || chip_name(path, &sega_player_program);
}

// The parent set's path: astron.zip in the same folder as path.
static void parent_path(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
    if (backslash > slash) slash = backslash;
    int dir = slash ? (int)(slash - path + 1) : 0;
    snprintf(out, size, "%.*s%s", dir, path, PARENT_ZIP);
}

// Read and check one chip, from the set or else its parent. A checksum
// that doesn't match only warns.
static bool load_chip(const char *path, const char *parent, const RomChip *chip, uint8_t *dest)
{
    const char *from = path, *name = chip_name(path, chip);
    if (!name) {
        from = parent;
        name = chip_name(parent, chip);
    }
    uint8_t *data = NULL;
    size_t size = 0;
    if (!name || !roms_read(from, name, &data, &size)) {
        fprintf(stderr, "ROMs: %s looks like Astron Belt, but %s is missing (looked in it and in\n"
                        "%s)\n", path, chip->names[0], parent);
        return false;
    }
    if (size != chip->size) {
        fprintf(stderr, "ROMs: %s is %zu bytes, expected %zu\n", name, size, chip->size);
        free(data);
        return false;
    }
    uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
    if (crc != chip->crc)
        fprintf(stderr, "ROMs: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                        "modified chip?); trying it anyway\n", name, crc, chip->crc);
    memcpy(dest, data, size);
    free(data);
    return true;
}

bool sega_load_roms(SegaBoard *b, const char *path)
{
    if (!chip_name(path, &program_chips[0])) {
        fprintf(stderr, "ROMs: %s is Astron Belt for Sega's own laserdisc player, which\n"
                        "laser-vault doesn't emulate yet. Use the version for Pioneer's\n"
                        "LD-V1000 (astronp.zip, with astron.zip beside it).\n", path);
        return false;
    }
    char parent[1024];
    parent_path(path, parent, sizeof parent);
    for (int i = 0; i < 2; i++) {
        if (!load_chip(path, parent, &program_chips[i], b->rom + i * 0x4000) ||
            !load_chip(path, parent, &bank_chips[i], b->banks[i]) ||
            !load_chip(path, parent, &char_chips[i], b->chars + i * 0x800) ||
            !load_chip(path, parent, &sprite_chips[i], b->sprites + i * 0x8000))
            return false;
    }
    if (!load_chip(path, parent, &char_prom, b->char_prom)) return false;
    samples_init(&b->sound, SEGA_CPU_HZ, SEGA_SAMPLE_RATE);
    b->dsw1 = 0xFF;  // 1 coin, 1 credit in both slots
    b->dsw2 = 0xFF;  // demo sounds, 60 s play timer, normal, ships at 5000/10000/20000, continues
    printf("ROMs: Astron Belt (Pioneer LD-V1000 version)\n");
    return true;
}

const char *sega_game_name(const SegaBoard *b)
{
    (void)b;
    return "Astron Belt";
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------
//
// The sounds, by the low four bits of what the game writes to D800. 0 is the
// ship's sound only with bit 5 clear; with it set, it means "nothing".
// (From the Daphne emulator, whose recordings these are.)

enum { SOUND_SHIP, SOUND_FIRE, SOUND_ENEMY, SOUND_ALARM1, SOUND_ALARM2, SOUND_ALARM3, SOUND_ALARM4, SOUND_COUNT };

static const char *const sound_files[SOUND_COUNT] = {
    "ab_ship.wav", "ab_fire.wav", "ab_enemy.wav", "ab_alarm1.wav", "ab_alarm2.wav", "ab_alarm3.wav", "ab_alarm4.wav",
};

int sega_load_samples(SegaBoard *b, const char *dir)
{
    int found = 0;
    for (int i = 0; i < SOUND_COUNT; i++) found += samples_load(&b->sound, i, dir, sound_files[i]);
    return found;
}

static void sound_write(SegaBoard *b, uint8_t v)
{
    int sound;
    switch (v & 0x0F) {
    case 0x0: sound = (v & 0x20) ? -1 : SOUND_SHIP; break;
    case 0xA: sound = SOUND_ENEMY; break;
    case 0xB: sound = SOUND_FIRE; break;
    case 0xC: sound = SOUND_ALARM4; break;
    case 0xD: sound = SOUND_ALARM3; break;
    case 0xE: sound = SOUND_ALARM2; break;
    case 0xF: sound = SOUND_ALARM1; break;
    default: sound = -1; break;
    }
    if (sound >= 0) samples_trigger(&b->sound, b->cycles, sound);
}

// Free play is one of the coinage settings; laser-vault takes "1 coin, 1
// credit" in slot 1 as it. Demo sounds are DSW2 bit 7 (set: on).
void sega_set(SegaBoard *b, SegaSetting setting, bool on)
{
    if (setting == SEGA_FREE_PLAY) {
        (void)on;  // no free play setting known
    } else if (on) {
        b->dsw2 &= (uint8_t)~0x80;
    } else {
        b->dsw2 |= 0x80;
    }
}

bool sega_get(const SegaBoard *b, SegaSetting setting)
{
    if (setting == SEGA_FREE_PLAY) return false;
    return !(b->dsw2 & 0x80);
}

// ---------------------------------------------------------------------------
// The address space
// ---------------------------------------------------------------------------

static uint8_t read_buttons(const SegaInputs *in)
{
    uint8_t v = 0xFF;  // bit 2 test, bit 3 service (not wired up here)
    if (in->coin1) v &= ~0x01;
    if (in->coin2) v &= ~0x02;
    if (in->start1) v &= ~0x10;
    if (in->start2) v &= ~0x40;
    return v;
}

// The stick as Sega's schematic wires it (pin 11 up, pin 12 down), so
// pushing up flies the ship up the screen. (Daphne swaps the two, which
// flies it like an aircraft: push up to dive.)
static uint8_t read_stick(const SegaInputs *in)
{
    uint8_t v = 0xFF;
    if (in->right) v &= ~0x01;
    if (in->left) v &= ~0x02;
    if (in->up) v &= ~0x04;
    if (in->down) v &= ~0x08;
    if (in->fire) v &= ~0x10;
    return v;
}

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    SegaBoard *b = ctx;
    if (addr < 0x8000) return b->rom[addr];
    if (addr < 0xC000) return b->banks[b->bank][addr - 0x8000];
    if (addr < 0xC800) return b->sprite_ram[addr - 0xC000];
    if (addr < 0xD000) return b->ld_in;
    switch (addr) {
    case 0xD000: return b->dsw1;
    case 0xD001: return b->dsw2;
    case 0xD002: return read_buttons(&b->inputs);
    case 0xD003: return read_stick(&b->inputs);
    default: break;
    }
    if (addr >= 0xD800 && addr < 0xD804) return b->out[addr - 0xD800];
    if (addr >= 0xE000 && addr < 0xE200) return b->colour_ram[addr - 0xE000];
    if (addr >= 0xF000 && addr < 0xF800) return b->char_ram[addr & 0x3FF];
    if (addr >= 0xF800) return b->ram[addr - 0xF800];
    return 0xFF;  // nothing there
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    SegaBoard *b = ctx;
    if (addr >= 0xC000 && addr < 0xC800) {
        b->sprite_ram[addr - 0xC000] = v;
        b->overlay_serial++;
    } else if (addr >= 0xC800 && addr < 0xD000) {
        b->ld_out = v;
    } else if (addr >= 0xD800 && addr < 0xD804) {
        if (addr == 0xD800) sound_write(b, v);
        if (addr == 0xD801 && ((v ^ b->out[1]) & 0x20)) b->overlay_serial++;
        b->out[addr - 0xD800] = v;
    } else if (addr >= 0xE000 && addr < 0xE200) {
        b->colour_ram[addr - 0xE000] = v;
        b->overlay_serial++;
    } else if (addr >= 0xF000 && addr < 0xF800) {
        b->char_ram[addr & 0x3FF] = v;
        b->overlay_serial++;
    } else if (addr >= 0xF800) {
        b->ram[addr - 0xF800] = v;
    }
}

static uint8_t io_in(void *ctx, uint16_t port)
{
    (void)ctx;
    (void)port;
    return 0xFF;
}

static void io_out(void *ctx, uint16_t port, uint8_t v)
{
    SegaBoard *b = ctx;
    if ((port & 0xFF) <= 1) b->bank = v & 1;
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// The cycle field n starts on: 59.94 fields a second.
static uint64_t field_start(uint64_t n)
{
    return n * SEGA_CPU_HZ * 1001 / 60000;
}

// The player's command strobe comes 554 us into each field (see
// ldp/ldv1000.c).
#define STROBE_CYCLES ((uint64_t)SEGA_CPU_HZ * 554 / 1000000)

void sega_reset(SegaBoard *b, Ldv1000 *player)
{
    b->player = player;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->sprite_ram, 0, sizeof b->sprite_ram);
    memset(b->colour_ram, 0, sizeof b->colour_ram);
    memset(b->char_ram, 0, sizeof b->char_ram);
    memset(b->ram, 0, sizeof b->ram);
    memset(b->out, 0, sizeof b->out);
    b->bank = 0;
    for (int i = 0; i < SAMPLES_MAX; i++) b->sound.position[i] = -1;  // any sounds playing stop
    b->ld_in = 0xFF;
    b->ld_out = 0xFF;
    b->cycles = 0;
    b->fields = 0;
    b->next_field = field_start(1);
    b->next_strobe = STROBE_CYCLES;
    b->overlay_serial++;
    z80_init(&b->cpu, (Z80Bus){.ctx = b, .read = mem_read, .write = mem_write, .in = io_in, .out = io_out});
}

void sega_run(SegaBoard *b, uint64_t cycles)
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
            // The latches pass the player's status to the game and the game's
            // byte to the player, while the NMI is on. The status is latched
            // first (at the player's status strobe, which comes before its
            // command strobe), so it never reflects the byte just sent: the
            // game sees a command taken a field later, and in that same
            // field the first digit of a frame number it asked for.
            if (b->out[1] & 0x40) {
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
// weighted (from Daphne) 0x8F, 0x43, 0x1F and 0x0E, top bit first.
static uint32_t colour(const SegaBoard *b, int n)
{
    static const uint8_t levels[16] = {
        0x00, 0x0E, 0x1F, 0x2D, 0x43, 0x51, 0x62, 0x70,
        0x8F, 0x9D, 0xAE, 0xBC, 0xD2, 0xE0, 0xF1, 0xFF,
    };
    uint8_t lo = b->colour_ram[n * 2], hi = b->colour_ram[n * 2 + 1];
    return 0xFF000000u | (uint32_t)levels[lo & 0x0F] << 16 | (uint32_t)levels[lo >> 4] << 8 | levels[hi & 0x0F];
}

// A character's colour is the colour PROM's value itself: 3 bits of red
// (bits 0-2), 3 of green (3-5) and 2 of blue (6-7), the top bits of the
// same 4-bit levels.
static uint32_t char_colour(int v)
{
    static const uint8_t levels[16] = {
        0x00, 0x0E, 0x1F, 0x2D, 0x43, 0x51, 0x62, 0x70,
        0x8F, 0x9D, 0xAE, 0xBC, 0xD2, 0xE0, 0xF1, 0xFF,
    };
    return 0xFF000000u | (uint32_t)levels[(v & 7) << 1] << 16 | (uint32_t)levels[((v >> 3) & 7) << 1] << 8 |
           levels[((v >> 6) & 3) << 2];
}

// Set pixel (x, y) of the 256 x 256 grid, if it's on screen.
static void put(uint32_t *pixels, int x, int y, uint32_t c)
{
    y -= SEGA_FIRST_LINE;
    if (x >= 0 && x < SEGA_OVERLAY_W && y >= 0 && y < SEGA_OVERLAY_H) pixels[y * SEGA_OVERLAY_W + x] = c;
}

// A sprite is 16 bytes: top and bottom lines (0, 1), x (2, and bit 0 of
// 3), how far to move through the graphics before each line (4, 5: may be
// negative), and where its graphics start (6, 7). Each line runs until a
// pixel pair whose bytes are both FF. A graphics address with bit 15 set
// draws the line backwards, mirrored.
static void draw_sprite(const SegaBoard *b, const uint8_t *s, uint32_t *pixels)
{
    int src = s[6] | s[7] << 8;
    int skip = (int16_t)(s[4] | s[5] << 8);
    int height = s[1] - s[0];
    for (int row = 0; row < height; row++) {
        src = (src + skip) & 0xFFFF;
        int at = src;
        int x = s[2] + ((s[3] & 1) << 8) - 3;  // (3 to the left, as Daphne has it)
        int y = s[0] + 1 + row;
        for (int n = 0; n < 256; n++) {
            uint8_t lo = b->sprites[at & 0x7FFF], hi = b->sprites[(at & 0x7FFF) + 0x8000];
            if (lo == 0xFF && hi == 0xFF) break;
            uint8_t p1 = (uint8_t)(lo >> 4 | (hi & 0xF0)), p2 = (uint8_t)((lo & 0x0F) | hi << 4);
            if (src & 0x8000) {
                uint8_t t = p1;
                p1 = p2, p2 = t;
                at--;
            } else {
                at++;
            }
            if (p1) put(pixels, x, y, colour(b, p1));
            if (p2) put(pixels, x + 1, y, colour(b, p2));
            x += 2;
        }
    }
}

// Each character is 8x8 pixels of 2 bits, one from each graphics chip (a
// byte a row, the leftmost pixel in the top bit). The colour PROM, given
// the pixel, the tile number's top five bits and D801's bit 5, gives the
// colour number. Characters are drawn over the sprites.
void sega_draw_overlay(const SegaBoard *b, uint32_t *pixels)
{
    memset(pixels, 0, sizeof *pixels * SEGA_OVERLAY_W * SEGA_OVERLAY_H);
    for (int n = 0; n < 32; n++) {
        const uint8_t *s = b->sprite_ram + n * 16;
        if (s[1] && s[1] > s[0]) draw_sprite(b, s, pixels);
    }
    int half = (b->out[1] & 0x20) << 2;
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int tile = b->char_ram[row * 32 + col];
            for (int y = 0; y < 8; y++) {
                uint8_t p0 = b->chars[tile * 8 + y], p1 = b->chars[0x800 + tile * 8 + y];
                for (int x = 0; x < 8; x++) {
                    int bit = 7 - x;
                    int pen = ((p0 >> bit) & 1) | ((p1 >> bit) & 1) << 1;
                    int c = b->char_prom[pen | (tile & 0xF8) >> 1 | half];
                    if (c) put(pixels, col * 8 + x, row * 8 + y, char_colour(c));
                }
            }
        }
    }
}
