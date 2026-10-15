#include "games/dlair/dlair.h"
#include "util/zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

// The timer interrupt: a 16 MHz crystal divided down by 8 * 16^4, about
// 30.5 times a second. In CPU cycles (16 MHz / 4) that's exactly 131,072.
#define IRQ_PERIOD 131072

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Dragon's Lair went through several revisions, and fans later made their
// own. Each set names its chips differently, so the files we find tell us
// which set we have, and each chip's CRC-32 tells us whether it's a good
// copy. (Names and checksums from MAME's dlair.cpp and the DLE readmes.)

#define ROM_CHIP_SIZE 0x2000

typedef struct {
    const char *name;
    uint16_t addr;
    uint32_t crc;  // CRC-32 of a known-good dump
} RomChip;

typedef struct {
    const char *title;
    bool tested;         // known to work with laser-vault
    uint8_t dsw1, dsw2;  // the DIP switches, as the factory (or the authors) set them
    RomChip chips[6];    // up to 5, then an empty entry
} RomSet;

// The sets this board can run, in order of preference (one zip may hold
// several): the US revisions made for the LD-V1000, then the fans'
// "Dragon's Lair Enhancement" sets, which began as rev F.
//
// The DIP switches start as in MAME: 2 coins a credit, 5 lives, sound in
// every 8th attract cycle, LD-V1000 player. The DLE sets give two switches
// new meanings. B:8 must be off (bit set) for the enhanced game in 1.1, and
// for the standard game in 2.0 (on, it plays prototype scenes found only on
// a disc pressed in 2002). In 2.0, B:5 on (bit clear) keeps the narrator in
// the attract mode, as in the original game.
static const RomSet rom_sets[] = {
    {"Dragon's Lair (US Rev. F2)", true, 0x20, 0x79, {
        {"dl_f2_u1.bin", 0x0000, 0xF5EA3B9D},
        {"dl_f2_u2.bin", 0x2000, 0xDCC1DFF2},
        {"dl_f2_u3.bin", 0x4000, 0xAB514E5B},
        {"dl_f2_u4.bin", 0x6000, 0xF5EC23D2},
    }},
    {"Dragon's Lair (US Rev. F)", false, 0x20, 0x79, {
        {"dl_f_u1.bin", 0x0000, 0x06FC6941},
        {"dl_f_u2.bin", 0x2000, 0xDCC1DFF2},
        {"dl_f_u3.bin", 0x4000, 0xAB514E5B},
        {"dl_f_u4.bin", 0x6000, 0xA817324E},
    }},
    {"Dragon's Lair (US Rev. E)", false, 0x20, 0x79, {
        {"dl_e_u1.bin", 0x0000, 0x02980426},
        {"dl_e_u2.bin", 0x2000, 0x979D4C97},
        {"dl_e_u3.bin", 0x4000, 0x897BF075},
        {"dl_e_u4.bin", 0x6000, 0x4EBFFBA5},
    }},
    {"Dragon's Lair (US Rev. D)", false, 0x20, 0x79, {
        {"dl_d_u1.bin", 0x0000, 0x0B5AB120},
        {"dl_d_u2.bin", 0x2000, 0x93EBFFFB},
        {"dl_d_u3.bin", 0x4000, 0x22E6591F},
        {"dl_d_u4.bin", 0x6000, 0x5F7212CB},
        {"dl_d_u5.bin", 0x8000, 0x2B469C89},
    }},
    {"Dragon's Lair Enhancement 2.0", true, 0x20, 0xE9, {
        {"dle20_u1.bin", 0x0000, 0x0ACA15B4},
        {"dle20_u2.bin", 0x2000, 0x1CEA7622},
        {"dle20_u3.bin", 0x4000, 0x21A7E7CA},
        {"dle20_u4.bin", 0x6000, 0x4AE26073},
    }},
    {"Dragon's Lair Enhancement 1.1", true, 0x20, 0xF9, {
        {"dle11u1l.bin", 0x0000, 0x9E65B33D},
        {"dle11u2l.bin", 0x2000, 0xF16FA36F},
        {"dle11u3l.bin", 0x4000, 0xB8D07A16},
        {"dle11u4l.bin", 0x6000, 0x20FC79B7},
    }},
};

// Sets this board can't run. One chip is enough to recognize each, so we
// can say why instead of just "not found".
#define NEEDS_PR7820 "it was made for the Pioneer PR-7820 laserdisc player, and laser-vault only emulates the LD-V1000 (used from rev D on)"
#define EUROPEAN "it runs on the European board, which is different hardware"
#define SPACE_ACE "laser-vault doesn't play Space Ace yet"

static const struct {
    const char *chip, *title, *reason;
} other_sets[] = {
    {"dl_c_u1.bin", "Dragon's Lair (US Rev. C)", NEEDS_PR7820},
    {"dl_b_u1.bin", "Dragon's Lair (US Rev. B)", NEEDS_PR7820},
    {"dl_a_u1.bin", "Dragon's Lair (US Rev. A)", NEEDS_PR7820},
    {"dlu1.bin", "Dragon's Lair (US Beta 2)", NEEDS_PR7820},
    {"dl_n1_u1.bin", "Dragon's Lair (US Beta 1)", NEEDS_PR7820},
    {"elu45.bin", "Dragon's Lair (European)", EUROPEAN},
    {"elu45_d2.bin", "Dragon's Lair (European, alternate)", EUROPEAN},
    {"dlita45.bin", "Dragon's Lair (Italian)", EUROPEAN},
    {"sa_a3_u1.bin", "Space Ace (US Rev. A3)", SPACE_ACE},
    {"sa_a2_u1.bin", "Space Ace (US Rev. A2)", SPACE_ACE},
    {"sa_a_u1.bin", "Space Ace (US Rev. A)", SPACE_ACE},
    {"sa_u45a.bin", "Space Ace (European)", SPACE_ACE},
};

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

static bool ends_with_zip(const char *path)
{
    size_t n = strlen(path);
    return n >= 4 && (strcmp(path + n - 4, ".zip") == 0 || strcmp(path + n - 4, ".ZIP") == 0);
}

// Read one ROM chip, from a zip or from a file in a folder.
static bool load_chip(const char *path, const char *name, uint8_t **data, size_t *size)
{
    uint32_t zip_crc;
    if (ends_with_zip(path)) return zip_read_file(path, name, data, size, &zip_crc);

    char file[1024];
    snprintf(file, sizeof file, "%s/%s", path, name);
    FILE *f = fopen(file, "rb");
    if (!f) return false;
    *data = malloc(ROM_CHIP_SIZE);
    *size = fread(*data, 1, ROM_CHIP_SIZE, f);
    fclose(f);
    return true;
}

static bool has_chip(const char *path, const char *name)
{
    uint8_t *data = NULL;
    size_t size;
    if (!load_chip(path, name, &data, &size)) return false;
    free(data);
    return true;
}

bool dlair_load_roms(Dlair *board, const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "dlair: cannot find %s\n", path);
        return false;
    }

    // Which set is it? Look for each set's first chip.
    const RomSet *set = NULL;
    for (size_t i = 0; i < COUNT(rom_sets) && !set; i++)
        if (has_chip(path, rom_sets[i].chips[0].name)) set = &rom_sets[i];
    if (!set) {
        for (size_t i = 0; i < COUNT(other_sets); i++) {
            if (has_chip(path, other_sets[i].chip)) {
                fprintf(stderr, "dlair: %s holds %s, but %s.\n",
                        path, other_sets[i].title, other_sets[i].reason);
                return false;
            }
        }
        fprintf(stderr, "dlair: found no Dragon's Lair ROMs in %s. laser-vault plays rev F2\n"
                        "(lair.zip, or dl_f2_u1.bin to dl_f2_u4.bin in a folder), and also revs D,\n"
                        "E and F and the Dragon's Lair Enhancement 1.1 and 2.0 sets.\n", path);
        return false;
    }

    memset(board->rom, 0xFF, sizeof board->rom);  // empty sockets read as FF
    for (const RomChip *chip = set->chips; chip->name; chip++) {
        uint8_t *data = NULL;
        size_t size = 0;
        if (!load_chip(path, chip->name, &data, &size)) {
            fprintf(stderr, "dlair: %s looks like %s, but %s is missing\n", path, set->title, chip->name);
            return false;
        }
        if (size != ROM_CHIP_SIZE) {
            fprintf(stderr, "dlair: %s is %zu bytes, expected %d\n", chip->name, size, ROM_CHIP_SIZE);
            free(data);
            return false;
        }
        // A wrong checksum means a bad copy or a modified chip. Warn, but
        // carry on: it might still run.
        uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
        if (crc != chip->crc)
            fprintf(stderr, "dlair: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                            "modified chip?); trying it anyway\n", chip->name, crc, chip->crc);
        memcpy(board->rom + chip->addr, data, size);
        free(data);
    }
    printf("ROMs: %s%s\n", set->title, set->tested ? "" : " (not yet tested with laser-vault)");
    board->dsw1 = set->dsw1;
    board->dsw2 = set->dsw2;
    return true;
}

// ---------------------------------------------------------------------------
// The address space
// ---------------------------------------------------------------------------
//
// The board only decodes some address lines, so each device appears at many
// addresses. In C000-FFFF, bits 3-5 pick the device: C000, C008, C010, ...

static uint8_t read_system_port(Dlair *b)
{
    // Bits 0-3: start and coin buttons, active low. Bits 4-5 unused (high).
    // Bit 6: the player's status strobe; it goes low for a moment when the
    // player has fresh status. Bit 7: the command strobe, high for a moment
    // when the player is ready for the next command byte.
    const DlairInputs *in = &b->inputs;
    uint8_t v = 0x3F;
    if (in->start1) v &= ~0x01;
    if (in->start2) v &= ~0x02;
    if (in->coin1) v &= ~0x04;
    if (in->coin2) v &= ~0x08;
    const DlairLaserdisc *ld = &b->laserdisc;
    if (!ld->status_strobe(ld->ctx, b->cycles)) v |= 0x40;
    if (ld->command_strobe(ld->ctx, b->cycles)) v |= 0x80;
    return v;
}

static uint8_t read_joystick_port(Dlair *b)
{
    const DlairInputs *in = &b->inputs;
    uint8_t v = 0xFF;
    if (in->up) v &= ~0x01;
    if (in->down) v &= ~0x02;
    if (in->left) v &= ~0x04;
    if (in->right) v &= ~0x08;
    if (in->sword) v &= ~0x10;
    return v;
}

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    Dlair *b = ctx;
    if (addr < 0xA000) return b->rom[addr];
    if (addr < 0xC000) return b->ram[addr & 0x7FF];
    if (addr >= 0xE000) return 0xFF;  // output-only area

    switch (addr & 0x38) {
    case 0x00:  // sound chip data; its I/O ports A and B are the DIP switches
        if (b->ay_select == 14) return b->dsw1;
        if (b->ay_select == 15) return b->dsw2;
        return ay8910_read(&b->ay, b->ay_select);
    case 0x08: return read_joystick_port(b);
    case 0x10: return read_system_port(b);
    case 0x20: return b->laserdisc.read_status(b->laserdisc.ctx, b->cycles);
    default: return 0xFF;  // nothing connected
    }
}

static void misc_write(Dlair *b, uint8_t v)
{
    // Bit 5 going low sends the byte in the laserdisc latch to the player.
    // (Bit 4 drives the coin counter, bit 6 the player's ENTER line; neither
    // matters for emulation yet.)
    bool was_high = b->misc & 0x20;
    b->misc = v;
    if (was_high && !(v & 0x20))
        b->laserdisc.send_command(b->laserdisc.ctx, b->cycles, b->ld_latch);
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    Dlair *b = ctx;
    if (addr < 0xA000) return;  // ROM
    if (addr < 0xC000) {
        b->ram[addr & 0x7FF] = v;
        return;
    }
    if (addr < 0xE000) return;  // input-only area

    switch (addr & 0x38) {
    case 0x00: ay8910_write(&b->ay, b->cycles, b->ay_select, v); break;  // sound chip data
    case 0x08: misc_write(b, v); break;
    case 0x10: b->ay_select = v; break;                   // sound chip register select
    case 0x20: b->ld_latch = v; break;
    case 0x30: b->digits[8 + (addr & 7)] = v & 0x0F; break;  // score digits 8-15
    case 0x38: b->digits[addr & 7] = v & 0x0F; break;        // score digits 0-7
    default: break;
    }
}

// The US board doesn't use the Z80's separate I/O ports.
static uint8_t io_in(void *ctx, uint16_t port) { (void)ctx; (void)port; return 0xFF; }
static void io_out(void *ctx, uint16_t port, uint8_t v) { (void)ctx; (void)port; (void)v; }

// ---------------------------------------------------------------------------
// Connecting an LD-V1000
// ---------------------------------------------------------------------------

static uint8_t ldv_read_status(void *ctx, uint64_t now) { return ldv1000_read_status(ctx, now); }
static void ldv_send_command(void *ctx, uint64_t now, uint8_t b) { ldv1000_write(ctx, now, b); }
static bool ldv_status_strobe(void *ctx, uint64_t now) { return ldv1000_status_strobe(ctx, now); }
static bool ldv_command_strobe(void *ctx, uint64_t now) { return ldv1000_command_strobe(ctx, now); }

DlairLaserdisc dlair_ldv1000_link(Ldv1000 *player)
{
    return (DlairLaserdisc){
        .ctx = player,
        .read_status = ldv_read_status,
        .send_command = ldv_send_command,
        .status_strobe = ldv_status_strobe,
        .command_strobe = ldv_command_strobe,
    };
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

void dlair_reset(Dlair *b, DlairLaserdisc laserdisc)
{
    b->laserdisc = laserdisc;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->ram, 0, sizeof b->ram);
    ay8910_free(&b->ay);
    ay8910_init(&b->ay, DLAIR_CPU_HZ, DLAIR_SOUND_CHIP_HZ, DLAIR_SAMPLE_RATE);
    memset(b->digits, 15, sizeof b->digits);  // blank
    b->ay_select = 0;
    b->ld_latch = 0;
    b->misc = 0xFF;
    b->cycles = 0;
    b->next_irq = IRQ_PERIOD;
    z80_init(&b->cpu, (Z80Bus){.ctx = b, .read = mem_read, .write = mem_write,
                               .in = io_in, .out = io_out});
}

void dlair_run(Dlair *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        if (b->cycles >= b->next_irq) {
            // The timer raises the interrupt line and holds it until the CPU
            // responds. The data bus floats high (FF), which in interrupt
            // mode 0 reads as RST 38h, the same address mode 1 uses.
            b->cpu.irq = true;
            b->cpu.irq_data = 0xFF;
            b->next_irq += IRQ_PERIOD;
        }
        b->cycles += (uint64_t)z80_step(&b->cpu);
        if (b->cpu.irq_taken) {
            b->cpu.irq_taken = false;
            b->cpu.irq = false;
        }
    }
}
