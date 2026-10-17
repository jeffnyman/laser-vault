#include "games/leland/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// The game is one 64 KB chip. Versions 3.xx are for the disc's second
// pressing, which is what's around now; 2.xx for the first, whose frames are
// numbered differently. The European versions drive a Philips player
// instead. (Names and checksums from MAME's dlair2.cpp and Daphne.)

typedef struct {
    const char *name;
    uint32_t crc;
    const char *title;
} RomSet;

static const RomSet sets[] = {
    {"dl2_319.bin", 0xE9453A1B, "Dragon's Lair II: Time Warp (Leland, US version 3.19)"},
    {"dl2_318.bin", 0x64706492, "Dragon's Lair II: Time Warp (Leland, US version 3.18)"},
    {"dl2_315.rom", 0x13EC0600, "Dragon's Lair II: Time Warp (Leland, US version 3.15)"},
    {"dl2_315.bin", 0x13EC0600, "Dragon's Lair II: Time Warp (Leland, US version 3.15)"},
    {"dl2_314.bin", 0xAF92B612, "Dragon's Lair II: Time Warp (Leland, US version 3.14)"},
    {"dl2_300.bin", 0xDEC4F2E3, "Dragon's Lair II: Time Warp (Leland, US version 3.00)"},
};
#define SET_COUNT (int)(sizeof sets / sizeof sets[0])

static const RomSet *find_set(const char *path)
{
    for (int i = 0; i < SET_COUNT; i++)
        if (roms_has(path, sets[i].name)) return &sets[i];
    return NULL;
}

bool leland_recognizes(const char *path)
{
    return find_set(path) != NULL;
}

bool leland_load_roms(LelandBoard *b, const char *path)
{
    const RomSet *set = find_set(path);
    if (!set) return false;
    uint8_t *data = NULL;
    size_t size = 0;
    if (!roms_read(path, set->name, &data, &size)) return false;
    if (size != sizeof b->rom) {
        fprintf(stderr, "ROMs: %s is %zu bytes, expected 65536\n", set->name, size);
        free(data);
        return false;
    }
    uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
    if (crc != set->crc)
        fprintf(stderr, "ROMs: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                        "modified chip?); trying it anyway\n", set->name, crc, set->crc);
    memcpy(b->rom, data, size);
    free(data);
    if (!b->ram) b->ram = malloc(0xF0000);
    if (!b->ram) return false;
    b->title = set->title;
    printf("ROMs: %s\n", set->title);
    return true;
}

void leland_free(LelandBoard *b)
{
    free(b->ram);
    free(b->samples);
    b->ram = NULL;
    b->samples = NULL;
}

const char *leland_game_name(const LelandBoard *b)
{
    (void)b;
    return "Dragon's Lair II";
}

// ---------------------------------------------------------------------------
// The interrupt controller (8259)
// ---------------------------------------------------------------------------
//
// Requests are edge triggered, as the BIOS sets it up: a line going high
// sets its bit in the request register; the CPU takes the highest priority
// request (IRQ 0 first) that isn't masked, unless one of higher or equal
// priority is still being serviced, until the program says it's done
// ("end of interrupt", 20 to port 20).

static void pic_update(LelandBoard *b)
{
    uint8_t req = (uint8_t)(b->pic_irr & ~b->pic_imr);
    bool intr = false;
    for (int n = 0; n < 8; n++) {
        if (b->pic_isr & (1 << n)) break;
        if (req & (1 << n)) {
            intr = true;
            break;
        }
    }
    b->cpu.intr = intr;
}

static void pic_line(LelandBoard *b, int n, bool high)
{
    uint8_t bit = (uint8_t)(1 << n);
    if (high && !(b->pic_lines & bit)) b->pic_irr |= bit;
    if (high) b->pic_lines |= bit;
    else b->pic_lines &= (uint8_t)~bit;
    pic_update(b);
}

static uint8_t pic_acknowledge(void *ctx)
{
    LelandBoard *b = ctx;
    uint8_t req = (uint8_t)(b->pic_irr & ~b->pic_imr);
    for (int n = 0; n < 8; n++)
        if (req & (1 << n)) {
            b->pic_irr &= (uint8_t)~(1 << n);
            if (!b->pic_auto_eoi) b->pic_isr |= (uint8_t)(1 << n);
            pic_update(b);
            return (uint8_t)(b->pic_base + n);
        }
    pic_update(b);
    return (uint8_t)(b->pic_base + 7);  // a request that went away: IRQ 7, as the 8259 does
}

static void pic_write(LelandBoard *b, int port, uint8_t v)
{
    if (port == 0) {
        if (v & 0x10) {  // ICW1: start setting up
            b->pic_imr = 0;
            b->pic_isr = 0;
            b->pic_irr = 0;
            b->pic_icw4 = v & 1;
            b->pic_single = v & 2;
            b->pic_read_isr = false;
            b->pic_init_step = 2;
        } else if (v & 0x08) {  // OCW3: which register port 20 reads
            if (v & 2) b->pic_read_isr = v & 1;
        } else {  // OCW2: end of interrupt
            int command = v >> 5;
            if (command == 1) {  // the highest priority one in service
                for (int n = 0; n < 8; n++)
                    if (b->pic_isr & (1 << n)) {
                        b->pic_isr &= (uint8_t)~(1 << n);
                        break;
                    }
            } else if (command == 3) b->pic_isr &= (uint8_t)~(1 << (v & 7));  // a given one
        }
    } else if (b->pic_init_step == 2) {  // ICW2: the first interrupt number
        b->pic_base = v & 0xF8;
        b->pic_init_step = b->pic_single ? (b->pic_icw4 ? 4 : 0) : 3;
    } else if (b->pic_init_step == 3) {  // ICW3: cascading (not used)
        b->pic_init_step = b->pic_icw4 ? 4 : 0;
    } else if (b->pic_init_step == 4) {  // ICW4
        b->pic_auto_eoi = v & 2;
        b->pic_init_step = 0;
    } else b->pic_imr = v;  // OCW1: the mask
    pic_update(b);
}

// ---------------------------------------------------------------------------
// The timer (8253)
// ---------------------------------------------------------------------------
//
// Three counters, fed at the PC's 1.193182 MHz. Channel 0 interrupts each
// time it counts down (18.2 times a second, as the BIOS leaves it);
// channel 2 makes the speaker's tone. Channel 1 refreshed the PC's memory.

#define PIT_HZ 1193182.0

static uint64_t pit_ticks(const LelandBoard *b) { return (uint64_t)((double)b->cycles * PIT_HZ / LELAND_CPU_HZ); }

static unsigned pit_period(const LelandBoard *b, int ch) { return b->pit[ch].count ? b->pit[ch].count : 65536; }

static uint16_t pit_current(const LelandBoard *b, int ch)
{
    unsigned n = pit_period(b, ch);
    uint64_t elapsed = pit_ticks(b) - b->pit[ch].loaded;
    if ((b->pit[ch].mode & 3) == 3) elapsed *= 2;  // square wave: counts by twos
    return (uint16_t)(n - elapsed % n);
}

// Channel 0's period, in CPU cycles.
static uint64_t irq0_period(const LelandBoard *b)
{
    return (uint64_t)(pit_period(b, 0) * (double)LELAND_CPU_HZ / PIT_HZ);
}

static void pit_load(LelandBoard *b, int ch)
{
    b->pit[ch].loaded = pit_ticks(b);
    if (ch == 0) b->next_irq0 = b->cycles + irq0_period(b);
}

static void pit_write(LelandBoard *b, int port, uint8_t v)
{
    if (port == 3) {
        int ch = v >> 6;
        if (ch == 3) return;
        int access = (v >> 4) & 3;
        if (access == 0) {  // latch the count, to read it
            b->pit[ch].latch = pit_current(b, ch);
            b->pit[ch].latched = true;
            b->pit[ch].high_next = false;
            return;
        }
        b->pit[ch].access = (uint8_t)access;
        b->pit[ch].mode = (v >> 1) & 7;
        b->pit[ch].high_next = false;
        return;
    }
    int ch = port;
    switch (b->pit[ch].access) {
    case 1: b->pit[ch].count = (uint16_t)((b->pit[ch].count & 0xFF00) | v); pit_load(b, ch); break;
    case 2: b->pit[ch].count = (uint16_t)((b->pit[ch].count & 0x00FF) | v << 8); pit_load(b, ch); break;
    default:
        if (!b->pit[ch].high_next) {
            b->pit[ch].low_written = v;
            b->pit[ch].high_next = true;
        } else {
            b->pit[ch].count = (uint16_t)(b->pit[ch].low_written | v << 8);
            b->pit[ch].high_next = false;
            pit_load(b, ch);
        }
        break;
    }
}

static uint8_t pit_read(LelandBoard *b, int ch)
{
    uint16_t v = b->pit[ch].latched ? b->pit[ch].latch : pit_current(b, ch);
    uint8_t out;
    if (b->pit[ch].access == 1) out = (uint8_t)v;
    else if (b->pit[ch].access == 2) out = (uint8_t)(v >> 8);
    else {
        out = b->pit[ch].high_next ? (uint8_t)(v >> 8) : (uint8_t)v;
        b->pit[ch].high_next = !b->pit[ch].high_next;
        if (b->pit[ch].high_next) return out;
    }
    b->pit[ch].latched = false;
    return out;
}

// ---------------------------------------------------------------------------
// The serial port (8250)
// ---------------------------------------------------------------------------
//
// The game sets 9600 baud, 8 bits, no parity, and has the port interrupt
// when a byte comes in from the player. A byte takes ten bit times on the
// line each way.

static uint64_t char_cycles(const LelandBoard *b)
{
    unsigned divisor = (unsigned)(b->dll | b->dlm << 8);
    if (divisor == 0) divisor = 1;
    return (uint64_t)(10.0 * 16 * divisor * LELAND_CPU_HZ / 1843200.0);
}

static void uart_irq(LelandBoard *b)
{
    bool pending = ((b->ier & 1) && b->rx_ready) || ((b->ier & 2) && b->thre_pending);
    pic_line(b, 3, pending && (b->mcr & 0x08));  // OUT2 connects the interrupt, as on a PC
}

static void uart_send(LelandBoard *b, uint8_t v)
{
    b->tsr = v;
    b->tsr_busy = true;
    b->tsr_done = b->cycles + char_cycles(b);
}

static void uart_update(LelandBoard *b)
{
    if (b->tsr_busy && b->cycles >= b->tsr_done) {
        b->tsr_busy = false;
        ldp1000_write(b->player, b->cycles, b->tsr);
        if (b->thr_full) {
            b->thr_full = false;
            uart_send(b, b->thr);
            b->thre_pending = true;
        }
    }
    if (!b->rx_ready && b->cycles >= b->next_rx && ldp1000_has_reply(b->player, b->cycles)) {
        b->rx_data = ldp1000_read(b->player, b->cycles);
        b->rx_ready = true;
        b->next_rx = b->cycles + char_cycles(b);
    }
    uart_irq(b);
}

static uint8_t uart_read(LelandBoard *b, int reg)
{
    bool dlab = b->lcr & 0x80;
    uint8_t v = 0xFF;
    uart_update(b);
    switch (reg) {
    case 0:
        if (dlab) return b->dll;
        v = b->rx_data;
        b->rx_ready = false;
        break;
    case 1: return dlab ? b->dlm : b->ier;
    case 2:  // which interrupt
        if ((b->ier & 1) && b->rx_ready) v = 0x04;
        else if ((b->ier & 2) && b->thre_pending) {
            v = 0x02;
            b->thre_pending = false;
        } else v = 0x01;
        break;
    case 3: return b->lcr;
    case 4: return b->mcr;
    case 5:  // line status: data ready, transmitter empty
        v = (uint8_t)((b->rx_ready ? 0x01 : 0) | (b->thr_full ? 0 : 0x20) |
                      (!b->thr_full && !b->tsr_busy ? 0x40 : 0));
        return v;
    case 6: return 0x30;  // modem status: the player is ready (CTS, DSR)
    case 7: return b->scr;
    }
    uart_irq(b);
    return v;
}

static void uart_write(LelandBoard *b, int reg, uint8_t v)
{
    bool dlab = b->lcr & 0x80;
    uart_update(b);
    switch (reg) {
    case 0:
        if (dlab) b->dll = v;
        else if (!b->tsr_busy) {
            uart_send(b, v);
            b->thre_pending = true;
        } else {
            b->thr = v;
            b->thr_full = true;
            b->thre_pending = false;
        }
        break;
    case 1:
        if (dlab) b->dlm = v;
        else {
            if ((v & 2) && !(b->ier & 2) && !b->thr_full) b->thre_pending = true;
            b->ier = v & 0x0F;
        }
        break;
    case 3: b->lcr = v; break;
    case 4: b->mcr = v; break;
    case 7: b->scr = v; break;
    }
    uart_irq(b);
}

// ---------------------------------------------------------------------------
// The EEPROM
// ---------------------------------------------------------------------------
//
// A serial EEPROM of 16-bit words, like a 93C66: with chip select high,
// each rising clock edge takes a bit, starting with a 1, then a 2-bit
// command and an 8-bit address:
//
//     10 aaaaaaaa           read: the word comes out, top bit first, one
//                           bit a clock (after a 0)
//     01 aaaaaaaa + 16 bits write
//     11 aaaaaaaa           erase
//     00 11xxxxxx, 00 00xxxxxx   allow, forbid writing
//     00 10xxxxxx, 00 01xxxxxx + 16 bits   erase all, write all
//
// Writing takes no time here: the data output says "ready" at once.

static void eeprom_write(LelandBoard *b, uint8_t v)
{
    bool cs = v & 4, clk = v & 2, di = v & 1;
    bool rising = clk && !(b->port202 & 2);
    if (!cs) {
        b->ee_bits = 0;
        b->ee_shift = 0;
        b->ee_op = -1;
        b->ee_out_bits = 0;
        b->ee_do = true;
        return;
    }
    if (!rising) return;
    if (b->ee_out_bits > 0) {
        b->ee_do = (b->ee_out >> 15) & 1;
        b->ee_out = (uint16_t)(b->ee_out << 1);
        b->ee_out_bits--;
        return;
    }
    if (b->ee_bits == 0 && !di) return;  // waiting for the start bit
    b->ee_shift = (b->ee_shift << 1) | di;
    b->ee_bits++;
    if (b->ee_bits == 11) {
        b->ee_op = (b->ee_shift >> 8) & 3;
        b->ee_addr = b->ee_shift & 0xFF;
        switch (b->ee_op) {
        case 2:  // read
            b->ee_out = b->eeprom[b->ee_addr];
            b->ee_out_bits = 16;
            b->ee_do = false;
            break;
        case 3:  // erase
            if (b->eeprom_writable) b->eeprom[b->ee_addr] = 0xFFFF;
            b->ee_do = true;
            break;
        case 0:
            switch (b->ee_addr >> 6) {
            case 3: b->eeprom_writable = true; break;
            case 0: b->eeprom_writable = false; break;
            case 2:
                if (b->eeprom_writable) memset(b->eeprom, 0xFF, sizeof b->eeprom);
                break;
            }
            break;
        }
    } else if (b->ee_bits == 27) {
        uint16_t data = (uint16_t)b->ee_shift;
        if (b->eeprom_writable) {
            if (b->ee_op == 1) b->eeprom[b->ee_addr] = data;
            else if (b->ee_op == 0 && (b->ee_addr >> 6) == 1)
                for (int i = 0; i < 256; i++) b->eeprom[i] = data;
        }
        b->ee_do = true;
    }
}

// ---------------------------------------------------------------------------
// Memory and ports
// ---------------------------------------------------------------------------

static uint8_t mem_read(void *ctx, uint32_t addr)
{
    LelandBoard *b = ctx;
    return addr >= 0xF0000 ? b->rom[addr - 0xF0000] : b->ram[addr];
}

static void mem_write(void *ctx, uint32_t addr, uint8_t v)
{
    LelandBoard *b = ctx;
    if (addr < 0xF0000) b->ram[addr] = v;
}

static uint8_t port_in(void *ctx, uint16_t port)
{
    LelandBoard *b = ctx;
    const LelandInputs *in = &b->inputs;
    switch (port) {
    case 0x20: return b->pic_read_isr ? b->pic_isr : b->pic_irr;
    case 0x21: return b->pic_imr;
    case 0x40: case 0x41: case 0x42: return pit_read(b, port - 0x40);
    case 0x61: return b->port61;
    case 0x201:
        return (uint8_t)~((in->up ? 0x01 : 0) | (in->down ? 0x02 : 0) | (in->left ? 0x04 : 0) |
                          (in->right ? 0x08 : 0) | (in->start1 ? 0x10 : 0) | (in->start2 ? 0x20 : 0) |
                          (in->sword ? 0x40 : 0) | (in->service ? 0x80 : 0));
    case 0x202: {
        uint8_t v = b->ee_do ? 0x01 : 0;
        if (!(b->port202 & 0x40))  // the coin counter shows a coin in each slot that has one
            for (int i = 0; i < 2; i++)
                if (b->coins_waiting[i] > 0) {
                    v |= (uint8_t)(0x04 << i);
                    b->coins_seen[i] = 1;
                }
        return v;
    }
    }
    if (port >= 0x2F8 && port <= 0x2FF) return uart_read(b, port - 0x2F8);
    return 0xFF;
}

static void port_out(void *ctx, uint16_t port, uint8_t v)
{
    LelandBoard *b = ctx;
    switch (port) {
    case 0x20: case 0x21: pic_write(b, port - 0x20, v); return;
    case 0x40: case 0x41: case 0x42: case 0x43: pit_write(b, port - 0x40, v); return;
    case 0x61: b->port61 = v; return;
    case 0x202:
        if ((v & 0x40) && !(b->port202 & 0x40))  // the game took the coins it saw
            for (int i = 0; i < 2; i++) {
                b->coins_waiting[i] -= b->coins_seen[i];
                b->coins_seen[i] = 0;
            }
        eeprom_write(b, v);
        b->port202 = v;
        return;
    }
    if (port >= 0x2F8 && port <= 0x2FF) uart_write(b, port - 0x2F8, v);
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

void leland_reset(LelandBoard *b, Ldp1000 *player)
{
    uint8_t *ram = b->ram;
    float *samples = b->samples;
    int capacity = b->sample_capacity;
    uint8_t rom[sizeof b->rom];
    memcpy(rom, b->rom, sizeof rom);
    const char *title = b->title;
    memset(b, 0, sizeof *b);
    b->ram = ram;
    b->samples = samples;
    b->sample_capacity = capacity;
    memcpy(b->rom, rom, sizeof rom);
    b->title = title;
    memset(b->ram, 0, 0xF0000);
    memset(b->eeprom, 0xFF, sizeof b->eeprom);  // blank: the game sets it up
    b->ee_do = true;
    b->ee_op = -1;
    b->pic_imr = 0xFF;
    b->pic_base = 8;
    b->player = player;
    i8088_init(&b->cpu, (I8088Bus){b, mem_read, mem_write, port_in, port_out, pic_acknowledge});
}

static void make_samples(LelandBoard *b, int cycles)
{
    b->sample_clock -= cycles;
    while (b->sample_clock <= 0) {
        b->sample_clock += (double)LELAND_CPU_HZ / LELAND_SAMPLE_RATE;
        float s = 0;
        if ((b->port61 & 3) == 3 && (b->pit[2].mode & 3) == 3) {
            double freq = PIT_HZ / pit_period(b, 2);
            if (freq < LELAND_SAMPLE_RATE / 2) {
                b->tone_phase += freq / LELAND_SAMPLE_RATE;
                b->tone_phase -= (int)b->tone_phase;
                s = b->tone_phase < 0.5 ? 0.15f : -0.15f;
            }
        }
        if (b->sample_count == b->sample_capacity) {
            int cap = b->sample_capacity ? b->sample_capacity * 2 : 4096;
            float *p = realloc(b->samples, (size_t)cap * sizeof *p);
            if (!p) return;
            b->samples = p;
            b->sample_capacity = cap;
        }
        b->samples[b->sample_count++] = s;
    }
}

void leland_run(LelandBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    uint64_t next_uart = b->cycles;
    for (int i = 0; i < 2; i++) {
        bool in = i ? b->inputs.coin2 : b->inputs.coin1;
        if (in && !b->coin_was_in[i]) b->coins_waiting[i]++;
        b->coin_was_in[i] = in;
    }
    while (b->cycles < end) {
        if (b->next_irq0 && b->cycles >= b->next_irq0) {
            pic_line(b, 0, true);
            pic_line(b, 0, false);
            b->next_irq0 += irq0_period(b);
        }
        if (b->cycles >= next_uart) {
            uart_update(b);
            next_uart = b->cycles + 500;
        }
        int n = i8088_step(&b->cpu);
        b->cycles += (uint64_t)n;
        if (b->sound_on) make_samples(b, n);
    }
}

void leland_start_sound(LelandBoard *b)
{
    b->sound_on = true;
    b->sample_count = 0;
}

int leland_take_samples(LelandBoard *b, const float **samples)
{
    *samples = b->samples;
    int n = b->sample_count;
    b->sample_count = 0;
    return n;
}
