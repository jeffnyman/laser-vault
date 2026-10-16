#include "games/stern/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Three versions of Cliff Hanger's program are known. (Names and checksums
// from MAME's cliffhgr.cpp.)

typedef struct {
    const char *name;
    uint32_t crc;
} RomChip;

typedef struct {
    const char *title;
    bool tested;           // known to work with laser-vault
    RomChip chips[5];      // 8 KB each, from address 0; unused ones NULL
    uint8_t dsw[4];        // the factory DIP settings
} RomSet;

// DIP switches: a switch on reads as 0. In set 1, bank 1 holds the hints
// (bits 6-7, 5), scores shown during the animation (4), scene lengths (3),
// the hanging scene (2) and lives (0-1); bank 2 the coinage; bank 3
// continues (7), short scenes (6), demo sounds (5, set = on), the disc and
// switch tests (4, 1), immortality (3), free play (2); bank 4 the
// difficulty of the moves (0-3). Sets 2 and 3 use fewer of them. (From
// MAME.)
static const RomSet rom_sets[] = {
    {"Cliff Hanger (set 1)", true,
     {{"cliff_u1.bin", 0xA86EC38F}, {"cliff_u2.bin", 0xB8D33B6B}, {"cliff_u3.bin", 0x75A64CD2},
      {"cliff_u4.bin", 0x906B2AF1}, {"cliff_u5.bin", 0x5922E710}},
     {0xC7, 0xFF, 0x3F, 0xFF}},
    {"Cliff Hanger (set 2)", false,
     {{"cliff_alt_0.bin", 0x27CAA67C}, {"cliff_alt_1.bin", 0x6E5F1515}, {"cliff_alt_2.bin", 0x045F895D},
      {"cliff_alt_3.bin", 0x54CDB4A1}},
     {0xFF, 0xFF, 0xFF, 0xFF}},
    {"Cliff Hanger (set 3)", false,
     {{"cliff_alt2_0.bin", 0x598D57FC}, {"cliff_alt2_1.bin", 0x7BCE618E}, {"cliff_alt2_2.bin", 0x65D2B984},
      {"cliff_alt2_3.bin", 0xF43A5269}},
     {0xFF, 0xFF, 0xFF, 0xFF}},
};

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

// Bank 3's bits for the named settings; a setting is on when its bit is
// clear.
static const uint8_t setting_bits[] = {
    [STERN_FREE_PLAY] = 0x04,
    [STERN_UNLIMITED_LIVES] = 0x08,
    [STERN_QUIET_ATTRACT] = 0x20,
};

bool stern_recognizes(const char *path)
{
    for (size_t i = 0; i < COUNT(rom_sets); i++)
        if (roms_has(path, rom_sets[i].chips[0].name)) return true;
    return false;
}

bool stern_load_roms(SternBoard *b, const char *path)
{
    const RomSet *set = NULL;
    for (size_t i = 0; i < COUNT(rom_sets) && !set; i++)
        if (roms_has(path, rom_sets[i].chips[0].name)) set = &rom_sets[i];
    if (!set) {
        fprintf(stderr, "ROMs: found no Cliff Hanger ROMs in %s\n", path);
        return false;
    }
    memset(b->rom, 0xFF, sizeof b->rom);
    for (int i = 0; i < 5 && set->chips[i].name; i++) {
        const RomChip *chip = &set->chips[i];
        uint8_t *data = NULL;
        size_t size = 0;
        if (!roms_read(path, chip->name, &data, &size)) {
            fprintf(stderr, "ROMs: %s looks like %s, but %s is missing\n", path, set->title, chip->name);
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
        memcpy(b->rom + i * 0x2000, data, size);
        free(data);
    }
    memcpy(b->dsw, set->dsw, sizeof b->dsw);
    printf("ROMs: %s%s\n", set->title, set->tested ? "" : " (not yet tested with laser-vault)");
    return true;
}

void stern_set(SternBoard *b, SternSetting setting, bool on)
{
    if (on) b->dsw[2] &= (uint8_t)~setting_bits[setting];
    else b->dsw[2] |= setting_bits[setting];
}

bool stern_get(const SternBoard *b, SternSetting setting)
{
    return !(b->dsw[2] & setting_bits[setting]);
}

const char *stern_game_name(const SternBoard *b)
{
    (void)b;
    return "Cliff Hanger";
}

// ---------------------------------------------------------------------------
// The address space
// ---------------------------------------------------------------------------

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    SternBoard *b = ctx;
    if (addr < 0xC000) return b->rom[addr];
    if (addr >= 0xE000 && addr < 0xF000) return b->ram[addr - 0xE000];
    return 0xFF;  // nothing there
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    SternBoard *b = ctx;
    if (addr >= 0xE000 && addr < 0xF000) b->ram[addr - 0xE000] = v;
}

static uint8_t read_bank(const SternBoard *b)
{
    const SternInputs *in = &b->inputs;
    uint8_t v = 0xFF;
    switch (b->bank) {
    case 1: case 2: case 3: case 4:
        return b->dsw[b->bank - 1];
    case 5:
        // Bits 2 and 3 are the feet buttons, 4 and 5 the hand buttons (the
        // game takes either of each pair); bits 2 and 3 also start a one- or
        // two-player game. (From how the game checks moves: it tells you
        // "you should have used your feet" when bits 2-3 were wanted. MAME
        // calls them player 1's and 2's buttons.) Bit 7 is the tilt switch.
        if (in->coin1) v &= ~0x01;
        if (in->coin2) v &= ~0x02;
        if (in->feet || in->start1) v &= ~0x04;
        if (in->start2) v &= ~0x08;
        if (in->hand) v &= ~0x10;
        return v;
    case 6:
        if (in->up) v &= ~0x01;
        if (in->right) v &= ~0x02;
        if (in->down) v &= ~0x04;
        if (in->left) v &= ~0x08;
        return v;
    default:
        return 0xFF;  // 0: the service buttons on the CPU board; 7 and up: nothing
    }
}

static void run_sound(SternBoard *b, uint64_t now);

static uint8_t io_in(void *ctx, uint16_t port)
{
    SternBoard *b = ctx;
    switch (port & 0xFF) {
    case 0x45: return tms9928_read_data(&b->vdp);
    case 0x55: return tms9928_read_status(&b->vdp);
    case 0x50: return (uint8_t)b->philips_code;
    case 0x51: return (uint8_t)(b->philips_code >> 8);
    case 0x52: return (uint8_t)(b->philips_code >> 16);
    case 0x53: b->cpu.irq = false; return 0;  // acknowledges the interrupt
    case 0x62: return read_bank(b);
    default: return 0xFF;
    }
}

static void io_out(void *ctx, uint16_t port, uint8_t v)
{
    SternBoard *b = ctx;
    switch (port & 0xFF) {
    case 0x44: tms9928_write_data(&b->vdp, v); break;
    case 0x54: tms9928_write_control(&b->vdp, v); break;
    case 0x46:
        run_sound(b, b->cycles);  // the tones change from now
        if ((v ^ b->misc) & 0x10) b->vdp.serial++;
        b->misc = v;
        break;
    case 0x60: b->bank = v == 0x0F ? 0 : v & 0x0F; break;
    case 0x66: pr8210_control(b->player, b->cycles, v & 1); break;
    default: break;  // 57: resets the code reader; 64, 68, 6A, 6E, 6F: delays, counters, lamps
    }
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------
//
// Two 555 timers, each with 24k and 10k resistors and a 0.047 or 0.1 uF
// capacitor: square waves of 1.44 / ((24k + 2 x 10k) C), about 696 and 327
// Hz, high 34/44 of the time. Port 46's bits 0 and 1 switch them on; a
// filter of about 500 Hz rounds them off. (Values from MAME's
// cliffhgr_a.cpp.)

#define TONE_LEVEL 0.25f

static void run_sound(SternBoard *b, uint64_t now)
{
    if (!b->rendering) return;
    static const double hz[2] = {1.44 / (44000 * 0.047e-6), 1.44 / (44000 * 0.1e-6)};
    const double cycles_per_sample = (double)STERN_CPU_HZ / STERN_SAMPLE_RATE;
    const float smooth = 0.068f;  // one-pole low-pass at about 500 Hz
    while (b->next_sample <= (double)now) {
        b->next_sample += cycles_per_sample;
        float level = 0;
        for (int i = 0; i < 2; i++) {
            b->phase[i] += hz[i] / STERN_SAMPLE_RATE;
            if (b->phase[i] >= 1) b->phase[i] -= 1;
            if (b->misc & (1 << i)) level += b->phase[i] < 34.0 / 44 ? TONE_LEVEL : 0;
        }
        b->filtered += smooth * (level - b->filtered);
        float out = b->filtered - b->dc_in + 0.995f * b->dc_out;  // remove the steady part
        b->dc_in = b->filtered;
        b->dc_out = out;
        if (b->sample_count < STERN_MAX_SAMPLES) b->samples[b->sample_count++] = out;
    }
}

void stern_start_sound(SternBoard *b)
{
    b->rendering = true;
    b->next_sample = (double)b->cycles;
    b->sample_count = 0;
}

int stern_take_samples(SternBoard *b, const float **samples)
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
    return n * STERN_CPU_HZ * 1001 / 60000;
}

// Events within a field, by line (262.5 lines a field): the Philips codes
// on lines 17 and 18, and the end of the video chip's picture (its 192
// lines start 40 lines into the field, after 13 blank and 27 border lines).
static const int event_lines[] = {17, 18, 232};

static uint64_t event_time(uint64_t field, int event)
{
    uint64_t start = field_start(field);
    return start + (field_start(field + 1) - start) * (uint64_t)event_lines[event] * 2 / 525;
}

void stern_reset(SternBoard *b, Pr8210 *player)
{
    b->player = player;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->ram, 0, sizeof b->ram);
    tms9928_reset(&b->vdp);
    b->vdp_irq = false;
    b->bank = 0;
    b->misc = 0;
    b->philips_code = 0;
    b->cycles = 0;
    b->fields = 0;
    b->next_event = 0;
    b->next_event_at = event_time(0, 0);
    b->next_sample = 0;
    b->sample_count = 0;
    z80_init(&b->cpu, (Z80Bus){.ctx = b, .read = mem_read, .write = mem_write, .in = io_in, .out = io_out});
}

static void field_event(SternBoard *b)
{
    if (b->next_event < 2) {
        // The decoder reads the line's code; a valid one (top bit set)
        // interrupts until the game acknowledges it.
        b->philips_code = pr8210_philips_code(b->player, b->cycles);
        if (b->philips_code & 0x800000) {
            b->cpu.irq = true;
            b->cpu.irq_data = 0xFF;  // RST 38h
        }
    } else {
        tms9928_vblank(&b->vdp);
    }
    if (++b->next_event == (int)COUNT(event_lines)) {
        b->next_event = 0;
        b->fields++;
    }
    b->next_event_at = event_time(b->fields, b->next_event);
}

void stern_run(SternBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        if (b->cycles >= b->next_event_at) field_event(b);
        b->cycles += (uint64_t)z80_step(&b->cpu);
        // The video chip's interrupt goes to the NMI, which acts on its edge.
        bool irq = tms9928_irq(&b->vdp);
        if (irq && !b->vdp_irq) b->cpu.nmi = true;
        b->vdp_irq = irq;
    }
}

// The disc's picture shows through the video chip's transparent colour only
// while the chip is set to mix in external video (register 0, bit 0);
// otherwise transparent is black.
void stern_draw_overlay(const SternBoard *b, uint32_t *pixels)
{
    tms9928_draw(&b->vdp, pixels);
    if (b->vdp.reg[0] & 0x01) return;
    for (int i = 0; i < TMS9928_W * TMS9928_H; i++)
        if (!(pixels[i] >> 24)) pixels[i] = 0xFF000000;
}
