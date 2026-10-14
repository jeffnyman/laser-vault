#include "games/dlair/dlair.h"
#include "util/zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// The timer interrupt: a 16 MHz crystal divided down by 8 * 16^4, about
// 30.5 times a second. In CPU cycles (16 MHz / 4) that's exactly 131,072.
#define IRQ_PERIOD 131072

// DIP switch defaults (as in MAME): 5 lives, LD-V1000 player selected.
#define DEFAULT_DSW1 0x20
#define DEFAULT_DSW2 0x79

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;
    uint16_t addr;
    uint32_t crc;  // CRC-32 of a known-good dump
} RomChip;

static const RomChip rev_f2[] = {
    {"dl_f2_u1.bin", 0x0000, 0xF5EA3B9D},
    {"dl_f2_u2.bin", 0x2000, 0xDCC1DFF2},
    {"dl_f2_u3.bin", 0x4000, 0xAB514E5B},
    {"dl_f2_u4.bin", 0x6000, 0xF5EC23D2},
};

#define ROM_CHIP_SIZE 0x2000

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

bool dlair_load_roms(Dlair *board, const char *path)
{
    memset(board->rom, 0xFF, sizeof board->rom);  // empty sockets read as FF
    for (size_t i = 0; i < sizeof rev_f2 / sizeof rev_f2[0]; i++) {
        const RomChip *chip = &rev_f2[i];
        uint8_t *data = NULL;
        size_t size = 0;
        if (!load_chip(path, chip->name, &data, &size)) {
            fprintf(stderr, "dlair: cannot find %s in %s\n", chip->name, path);
            return false;
        }
        if (size != ROM_CHIP_SIZE) {
            fprintf(stderr, "dlair: %s is %zu bytes, expected %d\n", chip->name, size, ROM_CHIP_SIZE);
            free(data);
            return false;
        }
        // A wrong checksum usually means a bad dump or a different revision.
        // Warn, but carry on: it might still run.
        uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
        if (crc != chip->crc)
            fprintf(stderr, "dlair: warning: %s has CRC %08X, expected %08X\n", chip->name, crc, chip->crc);
        memcpy(board->rom + chip->addr, data, size);
        free(data);
    }
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
        return b->ay_regs[b->ay_select & 15];
    case 0x08: return read_joystick_port(b);
    case 0x10: return read_system_port(b);
    case 0x20: return b->laserdisc.read_status(b->laserdisc.ctx);
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
        b->laserdisc.send_command(b->laserdisc.ctx, b->ld_latch);
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
    case 0x00: b->ay_regs[b->ay_select & 15] = v; break;  // sound chip data
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
// Running
// ---------------------------------------------------------------------------

void dlair_reset(Dlair *b, DlairLaserdisc laserdisc)
{
    b->laserdisc = laserdisc;
    b->dsw1 = DEFAULT_DSW1;
    b->dsw2 = DEFAULT_DSW2;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->ram, 0, sizeof b->ram);
    memset(b->ay_regs, 0, sizeof b->ay_regs);
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
