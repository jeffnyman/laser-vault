#include "games/gottlieb/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Us vs. Them: five program chips, one for each sound CPU, two of tiles and
// four of sprites (one bit plane each). (Names and checksums from MAME's
// gottlieb.cpp.)

enum { MAIN, DSOUND, YSOUND, TILES, SPRITES };

typedef struct {
    const char *name;
    uint32_t crc, size;
    int where;     // which region
    uint32_t at;   // offset in it
} RomChip;

static const RomChip usvsthem[] = {
    {"usvs.rm4", 0x0D7A4072, 0x2000, MAIN, 0x0000},  // 6000
    {"usvs.rm3", 0x6F32A671, 0x2000, MAIN, 0x2000},
    {"usvs.rm2", 0x36770716, 0x2000, MAIN, 0x4000},
    {"usvs.rm1", 0x697BC989, 0x2000, MAIN, 0x6000},
    {"usvs.rm0", 0x30CF6BD9, 0x2000, MAIN, 0x8000},  // E000
    {"usvsdrom.1", 0xC0B5CAB0, 0x2000, DSOUND, 0x0000},  // C000, repeated at E000
    {"usvsyrom.1", 0xC3D245CA, 0x2000, YSOUND, 0x2000},  // E000
    {"usvs.bg0", 0x8A0DE09F, 0x1000, TILES, 0x0000},
    {"usvs.bg1", 0x6FB64D3C, 0x1000, TILES, 0x1000},
    {"usvs.fg3", 0x98703015, 0x4000, SPRITES, 0x0000},  // the top bit plane
    {"usvs.fg2", 0xD3990707, 0x4000, SPRITES, 0x4000},
    {"usvs.fg1", 0xA2057430, 0x4000, SPRITES, 0x8000},
    {"usvs.fg0", 0x7734E53F, 0x4000, SPRITES, 0xC000},
};
#define CHIP_COUNT (int)(sizeof usvsthem / sizeof usvsthem[0])

bool gottlieb_recognizes(const char *path)
{
    return roms_has(path, usvsthem[0].name);
}

bool gottlieb_load_roms(GottliebBoard *b, const char *path)
{
    if (!gottlieb_recognizes(path)) return false;
    memset(b, 0, sizeof *b);
    for (int i = 0; i < CHIP_COUNT; i++) {
        const RomChip *chip = &usvsthem[i];
        uint8_t *data = NULL;
        size_t size = 0;
        if (!roms_read(path, chip->name, &data, &size)) {
            fprintf(stderr, "ROMs: %s looks like Us vs. Them, but %s is missing\n", path, chip->name);
            return false;
        }
        if (size != chip->size) {
            fprintf(stderr, "ROMs: %s is %zu bytes, expected %u\n", chip->name, size, (unsigned)chip->size);
            free(data);
            return false;
        }
        uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
        if (crc != chip->crc)
            fprintf(stderr, "ROMs: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                            "modified chip?); trying it anyway\n", chip->name, crc, chip->crc);
        uint8_t *region = chip->where == MAIN     ? b->rom
                          : chip->where == DSOUND ? b->d_rom
                          : chip->where == YSOUND ? b->y_rom
                          : chip->where == TILES  ? b->tile_gfx
                                                  : b->sprite_gfx;
        memcpy(region + chip->at, data, size);
        free(data);
    }
    // The DIP switches (a switch on reads as 1): bits 0 and 3 the coinage
    // (00 1 coin 1 credit, 08 2 coins 1 credit, 01 free play), bit 2 hard.
    b->dsw = 0x00;
    printf("ROMs: Us vs. Them (Mylstar)\n");
    return true;
}

void gottlieb_free(GottliebBoard *b)
{
    for (int i = 0; i < 2; i++) ay8910_free(&b->ay[i]);
    free(b->dac_samples);
    free(b->samples);
    b->dac_samples = b->samples = NULL;
}

const char *gottlieb_game_name(const GottliebBoard *b)
{
    (void)b;
    return "Us vs. Them";
}

void gottlieb_set(GottliebBoard *b, GottliebSetting setting, bool on)
{
    if (setting != GOTTLIEB_FREE_PLAY) return;
    b->dsw = (uint8_t)((b->dsw & ~0x09) | (on ? 0x01 : 0x00));
}

bool gottlieb_get(const GottliebBoard *b, GottliebSetting setting)
{
    return setting == GOTTLIEB_FREE_PLAY && (b->dsw & 0x09) == 0x01;
}

// ---------------------------------------------------------------------------
// The laserdisc interface
// ---------------------------------------------------------------------------
//
// The pulses to the player are timed by a 555 running at about 40 kHz: each
// lasts 10 of its ticks, and the gap to the next is 40 ticks for a 0 and 80
// for a 1 (about 1 and 2 ms).

#define TICK_CYCLES (GOTTLIEB_CPU_HZ * 24.948e-6)

// After asking for a search, the game waits for the player's picture to
// drop out (5807 bit 5) as a sign that the player took the command, then
// for it to come back. A real PR-8210 takes a second or more to find a
// frame; the player model finds it in a fraction of that, often before the
// game starts watching, and the game would try again for ever. So, as
// Hypseus does, the picture counts as lost for a second after each SEEK.
#define SEARCH_FIELDS 60
#define PR8210_SEEK 0x1A

static void send_command(GottliebBoard *b, uint8_t v)
{
    if ((v & 0x1F) == PR8210_SEEK) b->search_fields = SEARCH_FIELDS;
    b->command = v;
    b->pulses_left = 13;
    b->next_pulse = b->cycles + (uint64_t)(10 * TICK_CYCLES);
}

static void clock_pulses(GottliebBoard *b)
{
    if (b->pulse_end && b->cycles >= b->pulse_end) {
        pr8210_control(b->player, b->pulse_end, false);
        b->pulse_end = 0;
    }
    if (b->pulses_left && b->cycles >= b->next_pulse) {
        pr8210_control(b->player, b->next_pulse, true);
        b->pulse_end = b->next_pulse + (uint64_t)(10 * TICK_CYCLES);
        bool one = b->command & 0x80;
        b->command = (uint8_t)(b->command << 1);
        if (--b->pulses_left) b->next_pulse += (uint64_t)((one ? 80 : 40) * TICK_CYCLES);
    }
}

// 5807 with the status selected: bits 0-2 the frame number's top digit,
// bit 3 the data channel's buffer full (always, as nothing is read from
// it), bit 4 ready for another command, bit 5 a picture from the disc.
static uint8_t ld_status(const GottliebBoard *b)
{
    return (uint8_t)(((b->philips_code >> 16) & 7) | 0x08 | (b->pulses_left ? 0 : 0x10) | (b->disc_ready ? 0x20 : 0));
}

// ---------------------------------------------------------------------------
// The main CPU's address space
// ---------------------------------------------------------------------------

static uint8_t mem_read(void *ctx, uint32_t addr)
{
    GottliebBoard *b = ctx;
    addr &= 0xFFFF;
    if (addr >= 0x6000) return b->rom[addr - 0x6000];
    if (addr < 0x3000) return b->ram[addr];
    if (addr >= 0x3800 && addr < 0x4000) return b->tiles[addr & 0x3FF];
    if (addr < 0x5800) return 0xFF;  // sprites, character RAM, colours: written only
    const GottliebInputs *in = &b->inputs;
    switch (addr & 7) {
    case 0: return b->dsw;
    case 1:  // bit 0 service (low), bit 1 its select button, bit 5 tilt (low)
        return (uint8_t)(0x21 | (in->coin1 ? 0x04 : 0) | (in->coin2 ? 0x08 : 0) | (in->start1 ? 0x40 : 0) |
                         (in->start2 ? 0x80 : 0));
    case 4:
        return (uint8_t)((in->up ? 0x01 : 0) | (in->down ? 0x02 : 0) | (in->left ? 0x04 : 0) | (in->right ? 0x08 : 0) |
                         (in->button1 ? 0x10 : 0) | (in->button2 ? 0x20 : 0) | (in->button3 ? 0x40 : 0));
    case 5: return (uint8_t)b->philips_code;
    case 6: return (uint8_t)(b->philips_code >> 8);
    case 7: return b->data_select ? ld_status(b) : 0;
    default: return 0xFF;  // the trackball, which Us vs. Them doesn't have
    }
}

static void sound_command(GottliebBoard *b, uint8_t v);

static void mem_write(void *ctx, uint32_t addr, uint8_t v)
{
    GottliebBoard *b = ctx;
    addr &= 0xFFFF;
    if (addr < 0x3000) {
        b->ram[addr] = v;
        return;
    }
    if (addr < 0x3800) {
        if (b->sprites[addr & 0xFF] != v) b->overlay_serial++;
        b->sprites[addr & 0xFF] = v;
        return;
    }
    if (addr < 0x4000) {
        if (b->tiles[addr & 0x3FF] != v) b->overlay_serial++;
        b->tiles[addr & 0x3FF] = v;
        return;
    }
    if (addr < 0x5000 || addr >= 0x6000) return;  // character RAM (unused), ROM
    if (addr < 0x5800) {
        if (b->colours[addr & 0x1F] != v) b->overlay_serial++;
        b->colours[addr & 0x1F] = v;
        return;
    }
    switch (addr & 7) {
    case 2: sound_command(b, v); break;
    case 3:
        if (b->video_control != v) b->overlay_serial++;
        b->video_control = v;
        break;
    case 5: send_command(b, v); break;
    case 6: b->data_select = v & 1; break;
    default: break;  // 5800 watchdog, 5801 trackball reset, 5804 lamps
    }
}

static uint8_t port_in(void *ctx, uint16_t port)
{
    (void)ctx, (void)port;
    return 0xFF;
}

static void port_out(void *ctx, uint16_t port, uint8_t v)
{
    (void)ctx, (void)port, (void)v;
}

static uint8_t no_inta(void *ctx)
{
    (void)ctx;
    return 0;
}

// ---------------------------------------------------------------------------
// The sound board
// ---------------------------------------------------------------------------

// A byte from the main board reaches both CPUs' latches unless it's FF,
// and a byte after an FF raises their interrupts.
static void sound_command(GottliebBoard *b, uint8_t v)
{
    if (v != 0xFF) {
        b->d_latch = b->y_latch = v;
        if (b->last_command == 0xFF) b->d_irq = b->y_irq = true;
    }
    b->last_command = v;
}

#define AY_HZ 2000000

// The NMI timer: 256 x (256 - rate) ticks of 250 kHz.
static uint64_t nmi_period(const GottliebBoard *b)
{
    return (uint64_t)4 * 256 * (256 - b->nmi_rate);
}

static uint8_t d_read(void *ctx, uint16_t addr)
{
    GottliebBoard *b = ctx;
    if (addr < 0x4000) return b->d_ram[addr & 0x3FF];
    if (addr >= 0xC000) return b->d_rom[addr & 0x1FFF];
    if (addr >= 0x8000) {
        b->d_irq = false;
        return b->d_latch;
    }
    return 0xFF;
}

static void d_write(void *ctx, uint16_t addr, uint8_t v)
{
    GottliebBoard *b = ctx;
    if (addr < 0x4000) b->d_ram[addr & 0x3FF] = v;
    else if (addr < 0x8000) {
        if (addr & 1) b->dac = v;
        else b->dac_volume = v;
    }
}

static uint8_t y_read(void *ctx, uint16_t addr)
{
    GottliebBoard *b = ctx;
    if (addr < 0x2000) return b->y_ram[addr & 0x3FF];
    if (addr >= 0xC000) return b->y_rom[addr - 0xC000];
    if (addr >= 0x6000 && addr < 0x8000) return 0xFF;  // switches off, the speech chip ready for data
    if (addr >= 0xA800 && addr < 0xB000) {
        b->y_irq = false;
        return b->y_latch;
    }
    if (addr >= 0xB000 && addr < 0xB800) b->dcpu.nmi = true;
    return 0xFF;
}

// The control register: bit 0 lets the NMI timer through; bits 2-4 drive the
// AY-3-8913s (on bit 2 going low, bit 3 picks the first chip, and bit 4 says
// whether the PSG latch holds a register number or data for it); bits 5-7
// the speech chip.
static void speech_control(GottliebBoard *b, uint8_t v)
{
    uint8_t before = b->speech_control;
    b->speech_control = v;
    if ((before & 0x04) && !(v & 0x04)) {
        int chip = (v & 0x08) ? 0 : 1;
        if (v & 0x10) b->ay_reg[chip] = b->psg_latch & 0x0F;
        else ay8910_write(&b->ay[chip], b->y_cycles, b->ay_reg[chip], b->psg_latch);
    }
}

static void y_write(void *ctx, uint16_t addr, uint8_t v)
{
    GottliebBoard *b = ctx;
    if (addr < 0x2000) b->y_ram[addr & 0x3FF] = v;
    else if (addr < 0x4000) {}  // speech data
    else if (addr < 0x6000) speech_control(b, v);
    else if (addr >= 0x8000 && addr < 0xA000) b->psg_latch = v;
    else if (addr >= 0xA000 && addr < 0xA800) b->nmi_rate = v;
    else if (addr >= 0xB000 && addr < 0xB800) b->dcpu.nmi = true;
}

static void push_dac_sample(GottliebBoard *b)
{
    if (b->dac_count == b->dac_capacity) {
        int cap = b->dac_capacity ? b->dac_capacity * 2 : 4096;
        float *p = realloc(b->dac_samples, (size_t)cap * sizeof *p);
        if (!p) return;
        b->dac_samples = p;
        b->dac_capacity = cap;
    }
    // The volume DAC sets the reference of the other: its output swings
    // either side of zero by as much as the volume. The board's output is
    // AC-coupled, so a level that holds still (the DAC parked at 00, say)
    // fades away instead of sitting there: a DC-blocking filter (a pole at
    // 0.995, about 35 Hz).
    float volume = b->dac_volume / 255.0f;
    float in = volume * (b->dac / 128.0f - 1.0f);
    b->dac_out = in - b->dac_in + 0.995f * b->dac_out;
    b->dac_in = in;
    b->dac_samples[b->dac_count++] = b->dac_out;
}

// Run the sound board up to the main CPU's time.
static void run_sound(GottliebBoard *b)
{
    uint64_t target = b->cycles / (GOTTLIEB_CPU_HZ / GOTTLIEB_SOUND_HZ);
    while (b->d_cycles < target) {
        b->dcpu.irq = b->d_irq;
        b->d_cycles += (uint64_t)m6502_step(&b->dcpu);
        if (b->sound_on) {
            while (b->next_sample <= (double)b->d_cycles) {
                push_dac_sample(b);
                b->next_sample += (double)GOTTLIEB_SOUND_HZ / GOTTLIEB_SAMPLE_RATE;
            }
        }
    }
    while (b->y_cycles < target) {
        if (b->y_cycles >= b->next_nmi) {
            if (b->speech_control & 1) b->ycpu.nmi = true;
            b->next_nmi = b->y_cycles + nmi_period(b);
        }
        b->ycpu.irq = b->y_irq;
        b->y_cycles += (uint64_t)m6502_step(&b->ycpu);
    }
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// The cycle field n starts on: 59.94 fields a second.
static uint64_t field_start(uint64_t n)
{
    return n * GOTTLIEB_CPU_HZ * 1001 / 60000;
}

void gottlieb_reset(GottliebBoard *b, Pr8210 *player)
{
    b->player = player;
    memset(b->ram, 0, sizeof b->ram);
    memset(b->ram, 0xFF, 0x1000);  // the battery-backed part, as if new
    memset(b->sprites, 0, sizeof b->sprites);
    memset(b->tiles, 0, sizeof b->tiles);
    memset(b->colours, 0, sizeof b->colours);
    memset(&b->inputs, 0, sizeof b->inputs);
    b->video_control = 0;
    b->data_select = false;
    b->philips_code = 0;
    b->disc_ready = false;
    b->search_fields = 0;
    b->pulses_left = 0;
    b->pulse_end = 0;
    b->cycles = 0;
    b->next_field = field_start(1);
    b->overlay_serial++;

    memset(b->d_ram, 0, sizeof b->d_ram);
    memset(b->y_ram, 0, sizeof b->y_ram);
    b->d_latch = b->y_latch = 0;
    b->last_command = 0;
    b->d_irq = b->y_irq = false;
    b->speech_control = 0;
    b->psg_latch = 0;
    b->ay_reg[0] = b->ay_reg[1] = 0;
    b->nmi_rate = 0;
    b->dac = 0x80;
    b->dac_volume = 0;
    b->dac_in = b->dac_out = 0;
    b->d_cycles = b->y_cycles = 0;
    b->next_nmi = nmi_period(b);
    b->next_sample = 0;
    b->dac_count = 0;
    for (int i = 0; i < 2; i++) {
        ay8910_free(&b->ay[i]);
        ay8910_init(&b->ay[i], GOTTLIEB_SOUND_HZ, AY_HZ, GOTTLIEB_SAMPLE_RATE);
    }
    i8088_init(&b->cpu, (I8088Bus){b, mem_read, mem_write, port_in, port_out, no_inta});
    m6502_init(&b->dcpu, (M6502Bus){.ctx = b, .read = d_read, .write = d_write});
    m6502_init(&b->ycpu, (M6502Bus){.ctx = b, .read = y_read, .write = y_write});
}

void gottlieb_run(GottliebBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        clock_pulses(b);
        if (b->cycles >= b->next_field) {
            // The vertical blank: the decoder has read this field's Philips
            // codes (it keeps a valid one: the top four bits set), and the
            // NMI.
            uint32_t code = pr8210_philips_code(b->player, b->cycles);
            if ((code & 0xF00000) == 0xF00000) b->philips_code = code;
            if (b->search_fields) b->search_fields--;
            b->disc_ready = code != 0 && !b->search_fields;
            b->cpu.nmi = true;
            uint64_t n = b->cycles * 60000 / ((uint64_t)GOTTLIEB_CPU_HZ * 1001) + 1;
            b->next_field = field_start(n);
        }
        b->cycles += (uint64_t)i8088_step(&b->cpu);
        run_sound(b);
    }
}

void gottlieb_start_sound(GottliebBoard *b)
{
    for (int i = 0; i < 2; i++) ay8910_set_rendering(&b->ay[i], true, b->y_cycles);
    b->sound_on = true;
    b->next_sample = (double)b->d_cycles;
    b->dac_count = 0;
}

int gottlieb_take_samples(GottliebBoard *b, const float **samples)
{
    const float *s[2];
    int n[2];
    for (int i = 0; i < 2; i++) {
        ay8910_run(&b->ay[i], b->y_cycles);
        n[i] = ay8910_take_samples(&b->ay[i], &s[i]);
    }
    int count = n[0] < n[1] ? n[0] : n[1];
    if (count > b->dac_count) count = b->dac_count;
    if (count > b->sample_capacity) {
        b->sample_capacity = count * 2;
        b->samples = realloc(b->samples, (size_t)b->sample_capacity * sizeof *b->samples);
    }
    for (int i = 0; i < count; i++) b->samples[i] = (s[0][i] + s[1][i]) * 0.4f + b->dac_samples[i] * 0.3f;
    // Keep any DAC samples the chips haven't caught up with.
    memmove(b->dac_samples, b->dac_samples + count, (size_t)(b->dac_count - count) * sizeof *b->dac_samples);
    b->dac_count -= count;
    *samples = b->samples;
    return count;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
//
// Tiles: 32 bytes each, four bytes a row, two pixels a byte (the left one
// in the top four bits). Sprites: 16x16, four bit planes 0x4000 bytes
// apart (the top bit first), two bytes a row, the leftmost pixel in the top
// bit. Colour 0 is see-through for the sprites, and for the tiles when
// they're in front.

// The colours' resistor ladder (2000, 1000, 470 and 240 ohms, into 180),
// as levels for each bit.
static int colour_level(int bits)
{
    static const double resistors[4] = {2000, 1000, 470, 240};
    static double weights[4];
    static bool ready;
    if (!ready) {
        double total = 0;
        for (int i = 0; i < 4; i++) {
            // With only bit i on, the others pull down alongside the 180.
            double down = 1 / 180.0;
            for (int j = 0; j < 4; j++)
                if (j != i) down += 1 / resistors[j];
            weights[i] = (1 / down) / (resistors[i] + 1 / down);
            total += weights[i];
        }
        for (int i = 0; i < 4; i++) weights[i] *= 255 / total;
        ready = true;
    }
    double level = 0;
    for (int i = 0; i < 4; i++)
        if (bits & (1 << i)) level += weights[i];
    return level > 255 ? 255 : (int)(level + 0.5);
}

static uint32_t colour(const GottliebBoard *b, int index)
{
    uint8_t gb = b->colours[index * 2], r = b->colours[index * 2 + 1];
    uint32_t alpha = (index == 0 && (b->video_control & 0x08)) ? 0 : 0xFF;
    return alpha << 24 | (uint32_t)colour_level(r & 15) << 16 | (uint32_t)colour_level(gb >> 4) << 8 |
           (uint32_t)colour_level(gb & 15);
}

static void draw_tiles(const GottliebBoard *b, const uint32_t *palette, bool opaque, uint32_t *pixels)
{
    for (int row = 0; row < GOTTLIEB_OVERLAY_H / 8; row++) {
        for (int col = 0; col < 32; col++) {
            const uint8_t *gfx = b->tile_gfx + b->tiles[row * 32 + col] * 32;
            for (int y = 0; y < 8; y++) {
                uint32_t *out = pixels + (row * 8 + y) * GOTTLIEB_OVERLAY_W + col * 8;
                for (int x = 0; x < 8; x++) {
                    int pen = (gfx[y * 4 + x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                    if (pen || opaque) out[x] = palette[pen];
                }
            }
        }
    }
}

static void draw_sprites(const GottliebBoard *b, const uint32_t *palette, uint32_t *pixels)
{
    int bank = (b->video_control & 0x02) ? 256 : 0;
    for (int i = 0; i < 0x100; i += 4) {
        const uint8_t *s = b->sprites + i;
        int sx = s[1] - 4, sy = s[0] - 13;
        const uint8_t *gfx = b->sprite_gfx + ((255 ^ s[2]) + bank) * 32;
        for (int y = 0; y < 16; y++) {
            int py = sy + y;
            if (py < 0 || py >= GOTTLIEB_OVERLAY_H) continue;
            for (int x = 0; x < 16; x++) {
                int px = sx + x;
                if (px < 8 || px >= GOTTLIEB_OVERLAY_W) continue;  // the first column isn't shown
                int byte = y * 2 + x / 8, bit = 7 - (x & 7);
                int pen = ((gfx[byte] >> bit) & 1) << 3 | ((gfx[0x4000 + byte] >> bit) & 1) << 2 |
                          ((gfx[0x8000 + byte] >> bit) & 1) << 1 | ((gfx[0xC000 + byte] >> bit) & 1);
                if (pen) pixels[py * GOTTLIEB_OVERLAY_W + px] = palette[pen];
            }
        }
    }
}

void gottlieb_draw_overlay(const GottliebBoard *b, uint32_t *pixels)
{
    int count = GOTTLIEB_OVERLAY_W * GOTTLIEB_OVERLAY_H;
    if (!(b->video_control & 0x04)) {  // the display off: black
        for (int i = 0; i < count; i++) pixels[i] = 0xFF000000u;
        return;
    }
    uint32_t palette[16];
    for (int i = 0; i < 16; i++) palette[i] = colour(b, i);
    if (b->video_control & 0x01) {  // the tiles in front
        for (int i = 0; i < count; i++) pixels[i] = palette[0];
        draw_sprites(b, palette, pixels);
        draw_tiles(b, palette, false, pixels);
    } else {
        draw_tiles(b, palette, true, pixels);
        draw_sprites(b, palette, pixels);
    }
}
