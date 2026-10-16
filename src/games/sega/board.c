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
// The board ran three games of its own, each in two versions: one for
// Pioneer's LD-V1000, and one for Sega's own player, driven over a serial
// link. Sega also converted Data East's Cobra Command to it, for its own
// player. The games share the character
// graphics and colour PROM, and Galaxy Ranger and Star Blazer (its Japanese
// release) share everything but the program. MAME keeps a Pioneer set as
// just its program chips (astronp.zip, galaxyrp.zip, sblazerp.zip), with
// the rest in its parent's zip beside it, which is where laser-vault looks
// for any chip the set doesn't have. (Names and checksums from MAME's
// segald.cpp; the second and third names are ones other sets use.)

typedef struct {
    const char *names[3];
    size_t size;
    uint32_t crc;
} RomChip;

static const RomChip char_chips[2] = {
    {{"5280", "epr5280.bin", "gr5280.bin"}, 0x800, 0x583AF1FF},
    {{"5281", "epr5281.bin", "gr5281.bin"}, 0x800, 0x7B5C820C},
};
static const RomChip char_prom = {{"5279", "pr-5279.bin", NULL}, 0x200, 0x8716AEB5};
static const RomChip cobra_chars[2] = {
    {{"ic-11.bin", NULL, NULL}, 0x800, 0x5A2E8F4E},
    {{"ic-12.bin", NULL, NULL}, 0x800, 0x4B89D7ED},
};
static const RomChip cobra_prom = {{"ic-13.bin", NULL, NULL}, 0x200, 0x3547A14C};

typedef struct {
    SegaGame game;
    const char *title;
    bool tested;            // known to work with laser-vault
    const char *parent;     // the parent set's zip
    RomChip program[2];     // 0000-3FFF, 4000-7FFF
    RomChip banks[2];       // the two banks at 8000-BFFF, if any
    RomChip sprites[4];     // at 0000, 4000, 8000 and C000 of the sprite graphics
    const RomChip *chars;   // the character graphics (two chips)
    const RomChip *prom;    // and their colours
    int first_line;         // the first line of the grid on screen
    bool serial;            // drives Sega's player, not the LD-V1000
} RomSet;

#define NO_CHIP {{NULL, NULL, NULL}, 0, 0}
#define GR_BANKS {{{"gr5592.bin", "epr-5592.bin", NULL}, 0x4000, 0xD13715F8}, \
                  {{"gr5593.bin", "epr-5593.bin", NULL}, 0x4000, 0xB0A557AA}}
#define GR_SPRITES {{{"gr5594.bin", "epr-5594.bin", NULL}, 0x4000, 0x4403EF5A}, \
                    {{"gr5611.bin", "epr-5611.bin", NULL}, 0x4000, 0xB16BDFE4}, \
                    {{"gr5595.bin", "epr-5595.bin", NULL}, 0x4000, 0xCCBAEC4F}, \
                    {{"gr5612.bin", "epr-5612.bin", NULL}, 0x4000, 0xE312D080}}

static const RomSet rom_sets[] = {
    {SEGA_ASTRON_BELT, "Astron Belt (Pioneer LD-V1000 version)", true, "astron.zip",
     {{{"abp-ic03.bin", "5282", NULL}, 0x4000, 0xFD0DCFC9}, {{"abp-ic10.bin", "5283", NULL}, 0x4000, 0xA3746393}},
     {{{"5284", NULL, NULL}, 0x4000, 0xEEC6DB27}, {{"5285", NULL, NULL}, 0x4000, 0x820E154E}},
     {{{"5286", NULL, NULL}, 0x4000, 0x8EB1C28E}, NO_CHIP, {{"5338", NULL, NULL}, 0x4000, 0x94CA5F9A}, NO_CHIP},
     char_chips, &char_prom, 16, false},
    {SEGA_STAR_BLAZER, "Star Blazer (Pioneer LD-V1000 version)", true, "galaxyr.zip",
     {{{"epr-5590.bin", NULL, NULL}, 0x4000, 0xC06E41BB}, {{"epr-5591.bin", NULL, NULL}, 0x4000, 0xB179D18C}},
     GR_BANKS, GR_SPRITES, char_chips, &char_prom, 16, false},
    {SEGA_GALAXY_RANGER, "Galaxy Ranger (Pioneer LD-V1000 version)", false, "galaxyr.zip",
     {{{"epr-5613.bin", NULL, NULL}, 0x4000, 0x6617E702}, {{"epr-5614.bin", NULL, NULL}, 0x4000, 0x73AD8932}},
     GR_BANKS, GR_SPRITES, char_chips, &char_prom, 16, false},
    // Sega's version of Data East's Cobra Command: its own graphics and
    // colours, an 8 KB second program chip, and no banks.
    {SEGA_COBRA_COMMAND, "Cobra Command (Sega laserdisc hardware)", false, "cobraab.zip",
     {{{"ic-1.bin", NULL, NULL}, 0x4000, 0x079783C7}, {{"ic-2.bin", NULL, NULL}, 0x2000, 0x40C0B825}},
     {NO_CHIP, NO_CHIP},
     {{{"ic-7.bin", NULL, NULL}, 0x2000, 0x9E12B19C}, NO_CHIP, {{"ic-8.bin", NULL, NULL}, 0x2000, 0x201041C0}, NO_CHIP},
     cobra_chars, &cobra_prom, 8, true},
    // The versions for Sega's player.
    {SEGA_ASTRON_BELT, "Astron Belt (Sega laserdisc player version)", false, "astron.zip",
     {{{"5473c", NULL, NULL}, 0x4000, 0x0F24BAAA}, {{"5474a", NULL, NULL}, 0x4000, 0x5D44603D}},
     {{{"5284", NULL, NULL}, 0x4000, 0xEEC6DB27}, {{"5285", NULL, NULL}, 0x4000, 0x820E154E}},
     {{{"5286", NULL, NULL}, 0x4000, 0x8EB1C28E}, NO_CHIP, {{"5338", NULL, NULL}, 0x4000, 0x94CA5F9A}, NO_CHIP},
     char_chips, &char_prom, 16, true},
    {SEGA_GALAXY_RANGER, "Galaxy Ranger (Sega laserdisc player version)", false, "galaxyr.zip",
     {{{"gr5633.bin", NULL, NULL}, 0x4000, 0x398F6B23}, {{"gr5634.bin", NULL, NULL}, 0x4000, 0x2C5BE1B7}},
     GR_BANKS, GR_SPRITES, char_chips, &char_prom, 16, true},
};

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

// The chip's name in the set at path, or NULL if it isn't there.
static const char *chip_name(const char *path, const RomChip *chip)
{
    for (int i = 0; i < 3; i++)
        if (chip->names[i] && roms_has(path, chip->names[i])) return chip->names[i];
    return NULL;
}

// The supported set at path, or NULL.
static const RomSet *find_set(const char *path)
{
    for (size_t i = 0; i < COUNT(rom_sets); i++)
        if (chip_name(path, &rom_sets[i].program[0])) return &rom_sets[i];
    return NULL;
}

bool sega_recognizes(const char *path)
{
    return find_set(path) != NULL;
}

// The parent set's path: its zip in the same folder as path.
static void parent_path(const char *path, const char *parent, char *out, size_t size)
{
    const char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
    if (backslash > slash) slash = backslash;
    int dir = slash ? (int)(slash - path + 1) : 0;
    snprintf(out, size, "%.*s%s", dir, path, parent);
}

// Read and check one chip: from the set, else its parent, else any other
// zip in the same folder (a set may lack a chip its relatives share). A
// checksum that doesn't match only warns.
static bool load_chip(const char *path, const char *parent, const RomChip *chip, uint8_t *dest)
{
    static char nearby[1024];
    const char *from = path, *name = chip_name(path, chip);
    if (!name) {
        from = parent;
        name = chip_name(parent, chip);
    }
    for (int i = 0; !name && i < 3 && chip->names[i]; i++) {
        if (roms_find_nearby(path, chip->names[i], nearby, sizeof nearby)) {
            from = nearby;
            name = chip->names[i];
        }
    }
    uint8_t *data = NULL;
    size_t size = 0;
    if (!name || !roms_read(from, name, &data, &size)) {
        fprintf(stderr, "ROMs: %s is missing (looked in %s and in %s)\n", chip->names[0], path, parent);
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
    const RomSet *set = find_set(path);
    if (!set) return false;
    char parent[1024];
    parent_path(path, set->parent, parent, sizeof parent);
    // Empty sockets read as FF.
    memset(b->rom, 0xFF, sizeof b->rom);
    memset(b->banks, 0xFF, sizeof b->banks);
    memset(b->sprites, 0xFF, sizeof b->sprites);
    for (int i = 0; i < 2; i++) {
        if (!load_chip(path, parent, &set->program[i], b->rom + i * 0x4000) ||
            (set->banks[i].size && !load_chip(path, parent, &set->banks[i], b->banks[i])) ||
            !load_chip(path, parent, &set->chars[i], b->chars + i * 0x800))
            return false;
    }
    for (int i = 0; i < 4; i++)
        if (set->sprites[i].size && !load_chip(path, parent, &set->sprites[i], b->sprites + i * 0x4000)) return false;
    if (!load_chip(path, parent, set->prom, b->char_prom)) return false;
    b->game = set->game;
    b->first_line = set->first_line;
    b->serial = set->serial;
    samples_init(&b->sound, SEGA_CPU_HZ, SEGA_SAMPLE_RATE);
    b->dsw1 = 0xFF;  // 1 coin, 1 credit in both slots
    b->dsw2 = 0xFF;  // demo sounds, 60 s play timer, normal, ships at 5000/10000/20000, continues
    printf("ROMs: %s%s\n", set->title, set->tested ? "" : " (not yet tested with laser-vault)");
    return true;
}

const char *sega_game_name(const SegaBoard *b)
{
    switch (b->game) {
    case SEGA_STAR_BLAZER: return "Star Blazer";
    case SEGA_GALAXY_RANGER: return "Galaxy Ranger";
    case SEGA_COBRA_COMMAND: return "Cobra Command";
    default: return "Astron Belt";
    }
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------
//
// The sounds, by the low four bits of what the game writes to D800 (from
// the Daphne emulator, whose recordings these are). In Astron Belt, 0 is
// the ship's sound only with bit 5 clear; with it set, it means "nothing".

typedef struct {
    const char *files[16];  // by the low four bits; NULL: no sound
    const char *example;    // one of them, to name in messages
} SoundTable;

static const SoundTable astron_sounds = {
    {[0x0] = "ab_ship.wav", [0xA] = "ab_enemy.wav", [0xB] = "ab_fire.wav", [0xC] = "ab_alarm4.wav",
     [0xD] = "ab_alarm3.wav", [0xE] = "ab_alarm2.wav", [0xF] = "ab_alarm1.wav"},
    "ab_fire.wav",
};
static const SoundTable galaxy_sounds = {
    {[0x8] = "gr_mineon.wav", [0x9] = "gr_cannon.wav", [0xA] = "gr_attack.wav", [0xB] = "gr_fire.wav",
     [0xC] = "gr_alarm4.wav", [0xD] = "gr_alarm3.wav", [0xE] = "gr_alarm2.wav", [0xF] = "gr_alarm1.wav"},
    "gr_fire.wav",
};

// No recordings are known of Cobra Command's sounds.
static const SoundTable *sound_table(const SegaBoard *b)
{
    if (b->game == SEGA_COBRA_COMMAND) return NULL;
    return b->game == SEGA_ASTRON_BELT ? &astron_sounds : &galaxy_sounds;
}

int sega_load_samples(SegaBoard *b, const char *dir)
{
    const SoundTable *t = sound_table(b);
    int found = 0;
    if (!t) return 0;
    for (int i = 0; i < 16; i++)
        if (t->files[i]) found += samples_load(&b->sound, i, dir, t->files[i]);
    return found;
}

const char *sega_sample_example(const SegaBoard *b)
{
    return sound_table(b) ? sound_table(b)->example : NULL;
}

static void sound_write(SegaBoard *b, uint8_t v)
{
    int sound = v & 0x0F;
    const SoundTable *t = sound_table(b);
    if (!t || (sound == 0 && (b->game != SEGA_ASTRON_BELT || (v & 0x20)))) return;
    if (t->files[sound]) samples_trigger(&b->sound, b->cycles, sound);
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

// Bit 2 is the test switch and bit 3 service (not wired up here). Cobra
// Command has its second button where the others have start 2 (bit 6),
// and start 2 on bit 5.
static uint8_t read_buttons(const SegaBoard *b)
{
    const SegaInputs *in = &b->inputs;
    bool cobra = b->game == SEGA_COBRA_COMMAND;
    uint8_t v = 0xFF;
    if (in->coin1) v &= ~0x01;
    if (in->coin2) v &= ~0x02;
    if (in->start1) v &= ~0x10;
    if (in->start2) v &= cobra ? ~0x20 : ~0x40;
    if (cobra && in->fire2) v &= ~0x40;
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

// ---------------------------------------------------------------------------
// The serial chip (for Sega's player)
// ---------------------------------------------------------------------------
//
// An Intel 8251, as far as the games use it (from the Daphne emulator):
// after a reset the first control byte is the mode, and the rest are
// commands (bit 0 transmitter on, bit 2 receiver on, bit 6 reset). The
// status has "ready to send" in bit 0 and "byte received" in bit 1. Every
// 2 ms (USART_TICK) it looks at the line, and interrupts (the NMI, gated by
// D801 bit 6) when a byte has come in, or else when it's ready to send.

#define USART_TICK (SEGA_CPU_HZ / 500)

static void usart_interrupt(SegaBoard *b)
{
    if (b->out[1] & 0x40) b->cpu.nmi = true;
}

static uint8_t usart_read(SegaBoard *b, uint16_t addr)
{
    if (addr & 1) return (uint8_t)((b->tx_ready ? 0x01 : 0) | (b->rx_ready ? 0x02 : 0));
    b->rx_ready = false;
    return vip9500sg_read(b->vip, b->cycles);
}

static void usart_write(SegaBoard *b, uint16_t addr, uint8_t v)
{
    if (!(addr & 1)) {
        b->tx_ready = false;
        vip9500sg_write(b->vip, b->cycles, v);
        return;
    }
    if (b->usart_mode_next) {  // the mode (baud rate, bits): nothing to do
        b->usart_mode_next = false;
        return;
    }
    if (v & 0x40) {  // internal reset: the next byte is a mode again
        b->usart_mode_next = true;
        b->tx_enabled = b->rx_enabled = false;
        return;
    }
    bool tx = v & 0x01;
    if (tx && !b->tx_enabled) {
        b->tx_ready = true;
        usart_interrupt(b);
    }
    if (!tx) b->tx_ready = false;
    b->tx_enabled = tx;
    b->rx_enabled = v & 0x04;
}

static void usart_tick(SegaBoard *b)
{
    if (b->rx_enabled && !b->rx_ready && vip9500sg_has_reply(b->vip, b->cycles)) {
        b->rx_ready = true;
        usart_interrupt(b);
    } else if (b->tx_enabled && !b->tx_ready) {
        b->tx_ready = true;
        usart_interrupt(b);
    }
}

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    SegaBoard *b = ctx;
    if (addr < 0x8000) return b->rom[addr];
    if (addr < 0xC000) return b->banks[b->bank][addr - 0x8000];
    if (addr < 0xC800) return b->sprite_ram[addr - 0xC000];
    if (addr < 0xD000) return b->serial ? usart_read(b, addr) : b->ld_in;
    switch (addr) {
    case 0xD000: return b->dsw1;
    case 0xD001: return b->dsw2;
    case 0xD002: return read_buttons(b);
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
        if (b->serial) usart_write(b, addr, v);
        else b->ld_out = v;
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

void sega_reset(SegaBoard *b, Ldv1000 *player, Vip9500sg *vip)
{
    b->player = player;
    b->vip = vip;
    b->usart_mode_next = true;
    b->tx_enabled = b->rx_enabled = b->tx_ready = b->rx_ready = false;
    b->next_usart = USART_TICK;
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
        if (b->serial && b->cycles >= b->next_usart) {
            usart_tick(b);
            b->next_usart += USART_TICK;
        }
        if (!b->serial && b->cycles >= b->next_strobe) {
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
static void put(const SegaBoard *b, uint32_t *pixels, int x, int y, uint32_t c)
{
    y -= b->first_line;
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
            if (p1) put(b, pixels, x, y, colour(b, p1));
            if (p2) put(b, pixels, x + 1, y, colour(b, p2));
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
                    if (c) put(b, pixels, col * 8 + x, row * 8 + y, char_colour(c));
                }
            }
        }
    }
}
