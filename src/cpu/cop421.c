#include "cpu/cop421.h"

#include <string.h>

// How instructions are decoded
// ----------------------------
// Most instructions are one byte. Two are prefixes for a second byte: 23
// (LDD and XAD, RAM by direct address) and 33 (I/O and the rest). JMP and
// JSR (60-63, 68-6B) carry the low 8 bits of their target in a second
// byte. JID and LQID (FF, BF) take a second byte too, but from the ROM
// address A and RAM(B) point at, not from the next byte.
//
// Fields: "r" (bits 4-5) is xor'ed into Br by the RAM instructions LD, X,
// XIS and XDS, so a program can hop between rows as it goes; "y" (bits
// 0-3) is an immediate value. The semantics follow MAME's COP400 core
// (cop400op.hxx, Curt Coder) and National Semiconductor's COP420 data
// sheet.

static uint8_t ram_r(const Cop421 *c) { return c->ram[c->b & 0x3F]; }
static void ram_w(Cop421 *c, uint8_t v) { c->ram[c->b & 0x3F] = v & 0x0F; }

static uint8_t fetch(Cop421 *c)
{
    uint8_t v = c->rom[c->pc & 0x3FF];
    c->pc = (c->pc + 1) & 0x3FF;
    return v;
}

static void push(Cop421 *c, uint16_t v)
{
    c->sc = c->sb;
    c->sb = c->sa;
    c->sa = v;
}

static void pop(Cop421 *c)
{
    c->pc = c->sa;
    c->sa = c->sb;
    c->sb = c->sc;
}

static void write_q(Cop421 *c, uint8_t v)
{
    c->q = v;
    if (c->en & 0x04) c->bus.write_l(c->bus.ctx, v);
}

static void write_en(Cop421 *c, uint8_t v)
{
    c->en = v & 0x0F;
    if (c->en & 0x04) c->bus.write_l(c->bus.ctx, c->q);  // Q drives L from now
}

static void write_g(Cop421 *c, uint8_t v)
{
    c->g = v & 0x0F;
    c->bus.write_g(c->bus.ctx, c->g);
}

static void write_d(Cop421 *c, uint8_t v)
{
    c->d = v & 0x0F;
    c->bus.write_d(c->bus.ctx, c->d);
}

static void set_so(Cop421 *c, int level)
{
    if (level == c->so) return;
    c->so = level;
    c->bus.write_so(c->bus.ctx, level);
}

// Once per instruction cycle: the serial line and the timer.
static void tick(Cop421 *c)
{
    int si = c->bus.read_si(c->bus.ctx) ? 1 : 0;
    if (c->en & 0x01) {
        // Counter mode: SIO counts down on each falling edge of SI (seen
        // as two highs then two lows), and SO is EN bit 3.
        c->si_history = (uint8_t)(((c->si_history << 1) | si) & 0x0F);
        if (c->si_history == 0x0C) c->sio = (c->sio - 1) & 0x0F;
        set_so(c, (c->en >> 3) & 1);
    } else {
        // Shift register mode: SI shifts in at the bottom, and SO is the
        // top bit (when EN bit 3 lets it out).
        c->sio = (uint8_t)(((c->sio << 1) | si) & 0x0F);
        set_so(c, (c->en & 0x08) ? (c->sio >> 3) & 1 : 0);
    }

    // The timer counts every fourth cycle, so it wraps every 1024.
    if (++c->t_prescale == 4) {
        c->t_prescale = 0;
        if (++c->t == 0) c->skt_latch = true;
    }
}

void cop421_init(Cop421 *c, Cop421Bus bus, const uint8_t rom[1024])
{
    memset(c, 0, sizeof *c);
    c->bus = bus;
    memcpy(c->rom, rom, sizeof c->rom);
    cop421_reset(c);
}

void cop421_reset(Cop421 *c)
{
    c->pc = 0;
    c->a = c->b = c->c = 0;
    c->sa = c->sb = c->sc = 0;
    c->q = 0;
    c->sio = 0;
    c->si_history = 0;
    c->skl = 1;
    c->t = 0;
    c->t_prescale = 0;
    c->skt_latch = true;
    c->skip = false;
    c->lbi_run = false;
    c->so = -1;  // so the first level is passed on
    write_en(c, 0);
    write_d(c, 0);
    write_g(c, 0);
    set_so(c, 0);
}

// The length in bytes of the instruction starting with op (for skipping it).
static int length(uint8_t op)
{
    return (op == 0x23 || op == 0x33 || (op >= 0x60 && op <= 0x63) || (op >= 0x68 && op <= 0x6B)) ? 2 : 1;
}

// ASC and CASC: add with carry, skipping on a carry out.
static void add_carry(Cop421 *c, int sum)
{
    if (sum > 0x0F) {
        c->c = 1;
        c->skip = true;
    } else {
        c->c = 0;
    }
    c->a = (uint8_t)(sum & 0x0F);
}

// The 33 group: I/O, tests of G, LEI, and the two-byte LBI.
static void group33(Cop421 *c, uint8_t op)
{
    switch (op) {
    case 0x01: if (!(c->bus.read_g(c->bus.ctx) & 0x01)) c->skip = true; return;  // SKGBZ0
    case 0x11: if (!(c->bus.read_g(c->bus.ctx) & 0x02)) c->skip = true; return;  // SKGBZ1
    case 0x03: if (!(c->bus.read_g(c->bus.ctx) & 0x04)) c->skip = true; return;  // SKGBZ2
    case 0x13: if (!(c->bus.read_g(c->bus.ctx) & 0x08)) c->skip = true; return;  // SKGBZ3
    case 0x21: if (!(c->bus.read_g(c->bus.ctx) & 0x0F)) c->skip = true; return;  // SKGZ
    case 0x28: c->a = 0; return;                                                 // ININ (no IN pins)
    case 0x29: c->a = 0; return;                                                 // INIL (no IN pins)
    case 0x2A: c->a = c->bus.read_g(c->bus.ctx) & 0x0F; return;                  // ING
    case 0x2C: ram_w(c, c->q >> 4); c->a = c->q & 0x0F; return;                  // CQMA
    case 0x2E: {                                                                 // INL
        uint8_t l = c->bus.read_l(c->bus.ctx);
        ram_w(c, l >> 4);
        c->a = l & 0x0F;
        return;
    }
    case 0x3A: write_g(c, ram_r(c)); return;                                     // OMG
    case 0x3C: write_q(c, (uint8_t)(c->a << 4 | ram_r(c))); return;              // CAMQ
    case 0x3E: write_d(c, c->b & 0x0F); return;                                  // OBD
    default: break;
    }
    if (op >= 0x50 && op <= 0x5F) write_g(c, op & 0x0F);                         // OGI
    else if (op >= 0x60 && op <= 0x6F) write_en(c, op & 0x0F);                   // LEI
    else if (op >= 0x80 && op <= 0xBF) c->b = op & 0x3F;                         // LBI (two-byte)
}

int cop421_step(Cop421 *c)
{
    uint8_t op = fetch(c);
    int cycles = length(op);

    if (c->skip) {
        c->skip = false;
        c->lbi_run = false;
        if (cycles == 2) fetch(c);
        for (int i = 0; i < cycles; i++) tick(c);
        return cycles;
    }

    // A run of LBIs: only the first counts (so a program can jump into
    // the middle of a run to choose where B points).
    bool lbi = op < 0x40 && (op & 0x08);
    bool lbi2 = op == 0x33 && (c->rom[c->pc] & 0xC0) == 0x80;
    if ((lbi || lbi2) && c->lbi_run) {
        if (lbi2) fetch(c);
        for (int i = 0; i < cycles; i++) tick(c);
        return cycles;
    }
    c->lbi_run = lbi || lbi2;

    uint8_t low = op & 0x0F, r = op & 0x30;
    switch (op) {
    case 0x00: c->a = 0; break;                                                  // CLRA
    case 0x01: if (!(ram_r(c) & 0x01)) c->skip = true; break;                    // SKMBZ0
    case 0x11: if (!(ram_r(c) & 0x02)) c->skip = true; break;                    // SKMBZ1
    case 0x03: if (!(ram_r(c) & 0x04)) c->skip = true; break;                    // SKMBZ2
    case 0x13: if (!(ram_r(c) & 0x08)) c->skip = true; break;                    // SKMBZ3
    case 0x02: c->a ^= ram_r(c); break;                                          // XOR
    case 0x10: add_carry(c, (c->a ^ 0x0F) + ram_r(c) + c->c); break;             // CASC
    case 0x12: {                                                                 // XABR
        uint8_t row = c->a & 0x03;
        c->a = (uint8_t)(c->b >> 4);
        c->b = (uint8_t)(row << 4 | (c->b & 0x0F));
        break;
    }
    case 0x20: if (c->c) c->skip = true; break;                                  // SKC
    case 0x21: if (c->a == ram_r(c)) c->skip = true; break;                      // SKE
    case 0x22: c->c = 1; break;                                                  // SC
    case 0x23: {                                                                 // LDD, XAD
        uint8_t operand = fetch(c);
        uint8_t addr = operand & 0x3F;
        if (operand < 0x40) {
            c->a = c->ram[addr];
        } else if (operand >= 0x80 && operand < 0xC0) {
            uint8_t t = c->a;
            c->a = c->ram[addr];
            c->ram[addr] = t;
        }
        break;
    }
    case 0x30: add_carry(c, c->a + c->c + ram_r(c)); break;                      // ASC
    case 0x31: c->a = (c->a + ram_r(c)) & 0x0F; break;                           // ADD
    case 0x32: c->c = 0; break;                                                  // RC
    case 0x33: group33(c, fetch(c)); break;
    case 0x40: c->a ^= 0x0F; break;                                              // COMP
    case 0x41: if (c->skt_latch) { c->skt_latch = false; c->skip = true; } break;  // SKT
    case 0x42: ram_w(c, ram_r(c) & 0x0B); break;                                 // RMB2
    case 0x43: ram_w(c, ram_r(c) & 0x07); break;                                 // RMB3
    case 0x44: break;                                                            // NOP
    case 0x45: ram_w(c, ram_r(c) & 0x0D); break;                                 // RMB1
    case 0x46: ram_w(c, ram_r(c) | 0x04); break;                                 // SMB2
    case 0x47: ram_w(c, ram_r(c) | 0x02); break;                                 // SMB1
    case 0x48: pop(c); break;                                                    // RET
    case 0x49: pop(c); c->skip = true; break;                                    // RETSK
    case 0x4A: c->a = (c->a + 10) & 0x0F; break;                                 // ADT
    case 0x4B: ram_w(c, ram_r(c) | 0x08); break;                                 // SMB3
    case 0x4C: ram_w(c, ram_r(c) & 0x0E); break;                                 // RMB0
    case 0x4D: ram_w(c, ram_r(c) | 0x01); break;                                 // SMB0
    case 0x4E: c->a = c->b & 0x0F; break;                                        // CBA
    case 0x4F: {                                                                 // XAS
        uint8_t t = c->sio;
        c->sio = c->a;
        c->a = t;
        c->skl = c->c;
        break;
    }
    case 0x50: c->b = (uint8_t)((c->b & 0x30) | c->a); break;                    // CAB
    case 0xBF: {                                                                 // LQID
        push(c, c->pc);
        uint16_t addr = (uint16_t)((c->pc & 0x300) | c->a << 4 | ram_r(c));
        write_q(c, c->rom[addr]);
        pop(c);
        cycles = 2;
        break;
    }
    case 0xFF: {                                                                 // JID
        uint16_t addr = (uint16_t)((c->pc & 0x300) | c->a << 4 | ram_r(c));
        c->pc = (uint16_t)((addr & 0x300) | c->rom[addr]);
        cycles = 2;
        break;
    }
    default:
        if (op < 0x40 && (low >= 0x04 && low <= 0x07)) {                         // XIS LD X XDS
            uint8_t m = ram_r(c);
            if (low == 0x05) {                                                   // LD
                c->a = m;
            } else {
                ram_w(c, c->a);
                c->a = m;
            }
            if (low == 0x04 || low == 0x07) {                                    // XIS, XDS
                uint8_t bd = (uint8_t)((c->b + (low == 0x04 ? 1 : -1)) & 0x0F);
                c->b = (uint8_t)((c->b & 0x30) | bd);
                if (bd == (low == 0x04 ? 0x00 : 0x0F)) c->skip = true;
            }
            c->b ^= r;
        } else if (op < 0x40 && low >= 0x08) {                                   // LBI
            c->b = (uint8_t)(r | ((low + 1) & 0x0F));
        } else if (op >= 0x51 && op <= 0x5F) {                                   // AISC
            int sum = c->a + low;
            if (sum > 0x0F) c->skip = true;
            c->a = (uint8_t)(sum & 0x0F);
        } else if (op >= 0x60 && op <= 0x63) {                                   // JMP
            uint8_t lo = fetch(c);
            c->pc = (uint16_t)(((op & 0x03) << 8) | lo);
        } else if (op >= 0x68 && op <= 0x6B) {                                   // JSR
            uint8_t lo = fetch(c);
            push(c, c->pc);
            c->pc = (uint16_t)(((op & 0x03) << 8) | lo);
        } else if (op >= 0x70 && op <= 0x7F) {                                   // STII
            ram_w(c, low);
            c->b = (uint8_t)((c->b & 0x30) | ((c->b + 1) & 0x0F));
        } else if (op >= 0x80) {                                                 // JP, JSRP
            if ((c->pc >> 6) == 2 || (c->pc >> 6) == 3) {
                // In the subroutine pages (080-0FF): a jump within them.
                c->pc = (uint16_t)((c->pc & 0x380) | (op & 0x7F));
            } else if ((op & 0xC0) == 0xC0) {
                c->pc = (uint16_t)((c->pc & 0x3C0) | (op & 0x3F));               // within this page
            } else {
                push(c, c->pc);                                                  // JSRP: call into page 2
                c->pc = (uint16_t)(0x80 | (op & 0x3F));
            }
        }
        break;  // anything else isn't used by the COP421
    }
    for (int i = 0; i < cycles; i++) tick(c);
    return cycles;
}
