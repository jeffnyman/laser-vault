#include "cpu/m6502.h"

#include <stdio.h>

// How instructions are decoded
// ----------------------------
// Every opcode is one operation with one addressing mode, so a table gives
// both, with the cycle count from MOS's data sheet. The modes:
//
//   IMP  implied (or the accumulator, for ASL A and friends)
//   IMM  #$12      the byte after the opcode
//   ZP   $12       an address in the zero page
//   ZPX  $12,X     ... plus X, wrapping within the zero page (ZPY with Y)
//   ABS  $1234     a full address
//   ABX  $1234,X   ... plus X (ABY with Y)
//   IND  ($1234)   JMP only: the address stored at $1234
//   IZX  ($12,X)   the address stored in the zero page at $12+X
//   IZY  ($12),Y   the address stored in the zero page at $12, plus Y
//   REL  branches: a signed offset from the next instruction
//
// Reads through ABX, ABY and IZY take a cycle more when adding the index
// carries into the high byte ("crosses a page"); stores and read-modify-
// write operations always take that cycle, and the table counts it.

enum { C = M6502_C, Z = M6502_Z, I = M6502_I, D = M6502_D, B = M6502_B, U = M6502_U, V = M6502_V,
       N = M6502_N };

#define VEC_NMI 0xFFFA
#define VEC_RESET 0xFFFC
#define VEC_IRQ 0xFFFE

enum Mode { IMP, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL };

enum Op {
    XXX,  // undocumented: a NOP here
    ADC, AND, ASL, BCC, BCS, BEQ, BIT, BMI, BNE, BPL, BRK, BVC, BVS, CLC, CLD, CLI, CLV, CMP, CPX,
    CPY, DEC, DEX, DEY, EOR, INC, INX, INY, JMP, JSR, LDA, LDX, LDY, LSR, NOP, ORA, PHA, PHP, PLA,
    PLP, ROL, ROR, RTI, RTS, SBC, SEC, SED, SEI, STA, STX, STY, TAX, TAY, TSX, TXA, TXS, TYA,
};

static const char *const op_names[] = {
    "???", "ADC", "AND", "ASL", "BCC", "BCS", "BEQ", "BIT", "BMI", "BNE", "BPL", "BRK", "BVC",
    "BVS", "CLC", "CLD", "CLI", "CLV", "CMP", "CPX", "CPY", "DEC", "DEX", "DEY", "EOR", "INC",
    "INX", "INY", "JMP", "JSR", "LDA", "LDX", "LDY", "LSR", "NOP", "ORA", "PHA", "PHP", "PLA",
    "PLP", "ROL", "ROR", "RTI", "RTS", "SBC", "SEC", "SED", "SEI", "STA", "STX", "STY", "TAX",
    "TAY", "TSX", "TXA", "TXS", "TYA",
};

typedef struct {
    uint8_t op, mode, cycles;
} Opcode;

// Opcodes left out are undocumented, and run as one-byte, two-cycle NOPs.
static const Opcode opcodes[256] = {
    [0x00] = {BRK, IMP, 7}, [0x01] = {ORA, IZX, 6}, [0x05] = {ORA, ZP, 3},  [0x06] = {ASL, ZP, 5},
    [0x08] = {PHP, IMP, 3}, [0x09] = {ORA, IMM, 2}, [0x0A] = {ASL, IMP, 2}, [0x0D] = {ORA, ABS, 4},
    [0x0E] = {ASL, ABS, 6},
    [0x10] = {BPL, REL, 2}, [0x11] = {ORA, IZY, 5}, [0x15] = {ORA, ZPX, 4}, [0x16] = {ASL, ZPX, 6},
    [0x18] = {CLC, IMP, 2}, [0x19] = {ORA, ABY, 4}, [0x1D] = {ORA, ABX, 4}, [0x1E] = {ASL, ABX, 7},
    [0x20] = {JSR, ABS, 6}, [0x21] = {AND, IZX, 6}, [0x24] = {BIT, ZP, 3},  [0x25] = {AND, ZP, 3},
    [0x26] = {ROL, ZP, 5},  [0x28] = {PLP, IMP, 4}, [0x29] = {AND, IMM, 2}, [0x2A] = {ROL, IMP, 2},
    [0x2C] = {BIT, ABS, 4}, [0x2D] = {AND, ABS, 4}, [0x2E] = {ROL, ABS, 6},
    [0x30] = {BMI, REL, 2}, [0x31] = {AND, IZY, 5}, [0x35] = {AND, ZPX, 4}, [0x36] = {ROL, ZPX, 6},
    [0x38] = {SEC, IMP, 2}, [0x39] = {AND, ABY, 4}, [0x3D] = {AND, ABX, 4}, [0x3E] = {ROL, ABX, 7},
    [0x40] = {RTI, IMP, 6}, [0x41] = {EOR, IZX, 6}, [0x45] = {EOR, ZP, 3},  [0x46] = {LSR, ZP, 5},
    [0x48] = {PHA, IMP, 3}, [0x49] = {EOR, IMM, 2}, [0x4A] = {LSR, IMP, 2}, [0x4C] = {JMP, ABS, 3},
    [0x4D] = {EOR, ABS, 4}, [0x4E] = {LSR, ABS, 6},
    [0x50] = {BVC, REL, 2}, [0x51] = {EOR, IZY, 5}, [0x55] = {EOR, ZPX, 4}, [0x56] = {LSR, ZPX, 6},
    [0x58] = {CLI, IMP, 2}, [0x59] = {EOR, ABY, 4}, [0x5D] = {EOR, ABX, 4}, [0x5E] = {LSR, ABX, 7},
    [0x60] = {RTS, IMP, 6}, [0x61] = {ADC, IZX, 6}, [0x65] = {ADC, ZP, 3},  [0x66] = {ROR, ZP, 5},
    [0x68] = {PLA, IMP, 4}, [0x69] = {ADC, IMM, 2}, [0x6A] = {ROR, IMP, 2}, [0x6C] = {JMP, IND, 5},
    [0x6D] = {ADC, ABS, 4}, [0x6E] = {ROR, ABS, 6},
    [0x70] = {BVS, REL, 2}, [0x71] = {ADC, IZY, 5}, [0x75] = {ADC, ZPX, 4}, [0x76] = {ROR, ZPX, 6},
    [0x78] = {SEI, IMP, 2}, [0x79] = {ADC, ABY, 4}, [0x7D] = {ADC, ABX, 4}, [0x7E] = {ROR, ABX, 7},
    [0x81] = {STA, IZX, 6}, [0x84] = {STY, ZP, 3},  [0x85] = {STA, ZP, 3},  [0x86] = {STX, ZP, 3},
    [0x88] = {DEY, IMP, 2}, [0x8A] = {TXA, IMP, 2}, [0x8C] = {STY, ABS, 4}, [0x8D] = {STA, ABS, 4},
    [0x8E] = {STX, ABS, 4},
    [0x90] = {BCC, REL, 2}, [0x91] = {STA, IZY, 6}, [0x94] = {STY, ZPX, 4}, [0x95] = {STA, ZPX, 4},
    [0x96] = {STX, ZPY, 4}, [0x98] = {TYA, IMP, 2}, [0x99] = {STA, ABY, 5}, [0x9A] = {TXS, IMP, 2},
    [0x9D] = {STA, ABX, 5},
    [0xA0] = {LDY, IMM, 2}, [0xA1] = {LDA, IZX, 6}, [0xA2] = {LDX, IMM, 2}, [0xA4] = {LDY, ZP, 3},
    [0xA5] = {LDA, ZP, 3},  [0xA6] = {LDX, ZP, 3},  [0xA8] = {TAY, IMP, 2}, [0xA9] = {LDA, IMM, 2},
    [0xAA] = {TAX, IMP, 2}, [0xAC] = {LDY, ABS, 4}, [0xAD] = {LDA, ABS, 4}, [0xAE] = {LDX, ABS, 4},
    [0xB0] = {BCS, REL, 2}, [0xB1] = {LDA, IZY, 5}, [0xB4] = {LDY, ZPX, 4}, [0xB5] = {LDA, ZPX, 4},
    [0xB6] = {LDX, ZPY, 4}, [0xB8] = {CLV, IMP, 2}, [0xB9] = {LDA, ABY, 4}, [0xBA] = {TSX, IMP, 2},
    [0xBC] = {LDY, ABX, 4}, [0xBD] = {LDA, ABX, 4}, [0xBE] = {LDX, ABY, 4},
    [0xC0] = {CPY, IMM, 2}, [0xC1] = {CMP, IZX, 6}, [0xC4] = {CPY, ZP, 3},  [0xC5] = {CMP, ZP, 3},
    [0xC6] = {DEC, ZP, 5},  [0xC8] = {INY, IMP, 2}, [0xC9] = {CMP, IMM, 2}, [0xCA] = {DEX, IMP, 2},
    [0xCC] = {CPY, ABS, 4}, [0xCD] = {CMP, ABS, 4}, [0xCE] = {DEC, ABS, 6},
    [0xD0] = {BNE, REL, 2}, [0xD1] = {CMP, IZY, 5}, [0xD5] = {CMP, ZPX, 4}, [0xD6] = {DEC, ZPX, 6},
    [0xD8] = {CLD, IMP, 2}, [0xD9] = {CMP, ABY, 4}, [0xDD] = {CMP, ABX, 4}, [0xDE] = {DEC, ABX, 7},
    [0xE0] = {CPX, IMM, 2}, [0xE1] = {SBC, IZX, 6}, [0xE4] = {CPX, ZP, 3},  [0xE5] = {SBC, ZP, 3},
    [0xE6] = {INC, ZP, 5},  [0xE8] = {INX, IMP, 2}, [0xE9] = {SBC, IMM, 2}, [0xEA] = {NOP, IMP, 2},
    [0xEC] = {CPX, ABS, 4}, [0xED] = {SBC, ABS, 4}, [0xEE] = {INC, ABS, 6},
    [0xF0] = {BEQ, REL, 2}, [0xF1] = {SBC, IZY, 5}, [0xF5] = {SBC, ZPX, 4}, [0xF6] = {INC, ZPX, 6},
    [0xF8] = {SED, IMP, 2}, [0xF9] = {SBC, ABY, 4}, [0xFD] = {SBC, ABX, 4}, [0xFE] = {INC, ABX, 7},
};

// ---------------------------------------------------------------------------
// Bus access and small helpers
// ---------------------------------------------------------------------------

static uint8_t rd(M6502 *c, uint16_t addr) { return c->bus.read(c->bus.ctx, addr); }
static void wr(M6502 *c, uint16_t addr, uint8_t v) { c->bus.write(c->bus.ctx, addr, v); }

// 16-bit values are stored low byte first.
static uint16_t rd16(M6502 *c, uint16_t addr)
{
    return (uint16_t)(rd(c, addr) | rd(c, (uint16_t)(addr + 1)) << 8);
}

// A 16-bit value in the zero page; the second byte wraps within it.
static uint16_t rd16_zp(M6502 *c, uint8_t addr)
{
    return (uint16_t)(rd(c, addr) | rd(c, (uint8_t)(addr + 1)) << 8);
}

static uint8_t fetch(M6502 *c) { return rd(c, c->pc++); }

static uint16_t fetch16(M6502 *c)
{
    uint16_t v = rd16(c, c->pc);
    c->pc += 2;
    return v;
}

static void push(M6502 *c, uint8_t v) { wr(c, (uint16_t)(0x100 | c->s--), v); }
static uint8_t pull(M6502 *c) { return rd(c, (uint16_t)(0x100 | ++c->s)); }

static void push16(M6502 *c, uint16_t v)
{
    push(c, (uint8_t)(v >> 8));
    push(c, (uint8_t)v);
}

static uint16_t pull16(M6502 *c)
{
    uint8_t lo = pull(c);
    return (uint16_t)(lo | pull(c) << 8);
}

static void set_nz(M6502 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(N | Z)) | (v & N) | (v ? 0 : Z));
}

static void set_flag(M6502 *c, uint8_t flag, bool on)
{
    c->p = (uint8_t)(on ? c->p | flag : c->p & ~flag);
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

static void adc(M6502 *c, uint8_t m)
{
    int carry = c->p & C;
    if (c->p & D) {
        // Decimal: add the digits, carrying past 9. As on the NMOS 6502, Z
        // comes from the binary sum, and N and V from the sum after the low
        // digit's adjustment.
        int lo = (c->a & 0x0F) + (m & 0x0F) + carry;
        int hi = (c->a & 0xF0) + (m & 0xF0);
        set_flag(c, Z, ((c->a + m + carry) & 0xFF) == 0);
        if (lo > 9) {
            lo += 6;
            hi += 0x10;
        }
        set_flag(c, N, hi & 0x80);
        set_flag(c, V, ~(c->a ^ m) & (c->a ^ hi) & 0x80);
        if (hi > 0x90) hi += 0x60;
        set_flag(c, C, hi > 0xFF);
        c->a = (uint8_t)((lo & 0x0F) | (hi & 0xF0));
        return;
    }
    int sum = c->a + m + carry;
    set_flag(c, V, ~(c->a ^ m) & (c->a ^ sum) & 0x80);
    set_flag(c, C, sum > 0xFF);
    c->a = (uint8_t)sum;
    set_nz(c, c->a);
}

static void sbc(M6502 *c, uint8_t m)
{
    int borrow = !(c->p & C);
    int diff = c->a - m - borrow;
    // The flags come from the binary subtraction, in decimal mode too.
    set_flag(c, V, (c->a ^ m) & (c->a ^ diff) & 0x80);
    set_flag(c, C, diff >= 0);
    uint8_t result = (uint8_t)diff;
    set_nz(c, result);
    if (c->p & D) {
        int lo = (c->a & 0x0F) - (m & 0x0F) - borrow;
        int hi = (c->a & 0xF0) - (m & 0xF0);
        if (lo & 0x10) {
            lo -= 6;
            hi -= 0x10;
        }
        if (hi & 0x100) hi -= 0x60;
        result = (uint8_t)((lo & 0x0F) | (hi & 0xF0));
    }
    c->a = result;
}

static void compare(M6502 *c, uint8_t reg, uint8_t m)
{
    set_flag(c, C, reg >= m);
    set_nz(c, (uint8_t)(reg - m));
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

void m6502_init(M6502 *cpu, M6502Bus bus)
{
    *cpu = (M6502){.bus = bus};
    m6502_reset(cpu);
}

void m6502_reset(M6502 *cpu)
{
    cpu->a = cpu->x = cpu->y = 0;
    cpu->s = 0xFD;
    cpu->p = U | I;
    cpu->nmi = cpu->irq = cpu->irq_taken = false;
    cpu->pc = rd16(cpu, VEC_RESET);
}

static int interrupt(M6502 *c, uint16_t vector)
{
    push16(c, c->pc);
    push(c, (uint8_t)((c->p & ~B) | U));
    c->p |= I;
    c->pc = rd16(c, vector);
    return 7;
}

// Branch if cond: a cycle more if taken, another if it lands in a
// different page.
static int branch(M6502 *c, bool cond, uint16_t target)
{
    if (!cond) return 0;
    int extra = (target & 0xFF00) != (c->pc & 0xFF00) ? 2 : 1;
    c->pc = target;
    return extra;
}

int m6502_step(M6502 *c)
{
    if (c->nmi) {
        c->nmi = false;
        return interrupt(c, VEC_NMI);
    }
    if (c->irq && !(c->p & I)) {
        c->irq_taken = true;
        return interrupt(c, VEC_IRQ);
    }

    Opcode o = opcodes[fetch(c)];
    int cycles = o.cycles ? o.cycles : 2;

    // Work out the operand's address.
    uint16_t addr = 0;
    bool crossed = false;
    switch (o.mode) {
    case IMP: break;
    case IMM: addr = c->pc++; break;
    case ZP: addr = fetch(c); break;
    case ZPX: addr = (uint8_t)(fetch(c) + c->x); break;
    case ZPY: addr = (uint8_t)(fetch(c) + c->y); break;
    case ABS: addr = fetch16(c); break;
    case ABX:
    case ABY: {
        uint16_t base = fetch16(c);
        addr = (uint16_t)(base + (o.mode == ABX ? c->x : c->y));
        crossed = (addr ^ base) & 0xFF00;
        break;
    }
    case IND: {
        // The second byte of the pointer comes from the same page: JMP
        // ($12FF) reads 12FF and 1200, a famous quirk.
        uint16_t ptr = fetch16(c);
        addr = (uint16_t)(rd(c, ptr) | rd(c, (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0xFF))) << 8);
        break;
    }
    case IZX: addr = rd16_zp(c, (uint8_t)(fetch(c) + c->x)); break;
    case IZY: {
        uint16_t base = rd16_zp(c, fetch(c));
        addr = (uint16_t)(base + c->y);
        crossed = (addr ^ base) & 0xFF00;
        break;
    }
    case REL: {
        int8_t offset = (int8_t)fetch(c);
        addr = (uint16_t)(c->pc + offset);
        break;
    }
    }

    // Reads take a cycle more across a page.
    int read_extra = crossed ? 1 : 0;
    uint8_t m, r;
    switch (o.op) {
    case XXX:
    case NOP: break;

    case LDA: c->a = rd(c, addr); set_nz(c, c->a); cycles += read_extra; break;
    case LDX: c->x = rd(c, addr); set_nz(c, c->x); cycles += read_extra; break;
    case LDY: c->y = rd(c, addr); set_nz(c, c->y); cycles += read_extra; break;
    case STA: wr(c, addr, c->a); break;
    case STX: wr(c, addr, c->x); break;
    case STY: wr(c, addr, c->y); break;

    case ADC: adc(c, rd(c, addr)); cycles += read_extra; break;
    case SBC: sbc(c, rd(c, addr)); cycles += read_extra; break;
    case AND: c->a &= rd(c, addr); set_nz(c, c->a); cycles += read_extra; break;
    case ORA: c->a |= rd(c, addr); set_nz(c, c->a); cycles += read_extra; break;
    case EOR: c->a ^= rd(c, addr); set_nz(c, c->a); cycles += read_extra; break;
    case CMP: compare(c, c->a, rd(c, addr)); cycles += read_extra; break;
    case CPX: compare(c, c->x, rd(c, addr)); break;
    case CPY: compare(c, c->y, rd(c, addr)); break;
    case BIT:
        m = rd(c, addr);
        c->p = (uint8_t)((c->p & ~(N | V | Z)) | (m & (N | V)) | ((c->a & m) ? 0 : Z));
        break;

    // Shifts and rotates: on A in implied mode, else on memory.
    case ASL:
    case LSR:
    case ROL:
    case ROR:
        m = o.mode == IMP ? c->a : rd(c, addr);
        if (o.op == ASL) r = (uint8_t)(m << 1);
        else if (o.op == LSR) r = m >> 1;
        else if (o.op == ROL) r = (uint8_t)(m << 1 | (c->p & C));
        else r = (uint8_t)(m >> 1 | (c->p & C) << 7);
        set_flag(c, C, (o.op == ASL || o.op == ROL) ? m & 0x80 : m & 0x01);
        set_nz(c, r);
        if (o.mode == IMP) c->a = r;
        else wr(c, addr, r);
        break;
    case INC: r = (uint8_t)(rd(c, addr) + 1); wr(c, addr, r); set_nz(c, r); break;
    case DEC: r = (uint8_t)(rd(c, addr) - 1); wr(c, addr, r); set_nz(c, r); break;
    case INX: set_nz(c, ++c->x); break;
    case INY: set_nz(c, ++c->y); break;
    case DEX: set_nz(c, --c->x); break;
    case DEY: set_nz(c, --c->y); break;

    case TAX: c->x = c->a; set_nz(c, c->x); break;
    case TAY: c->y = c->a; set_nz(c, c->y); break;
    case TXA: c->a = c->x; set_nz(c, c->a); break;
    case TYA: c->a = c->y; set_nz(c, c->a); break;
    case TSX: c->x = c->s; set_nz(c, c->x); break;
    case TXS: c->s = c->x; break;

    case PHA: push(c, c->a); break;
    case PHP: push(c, (uint8_t)(c->p | B | U)); break;
    case PLA: c->a = pull(c); set_nz(c, c->a); break;
    case PLP: c->p = (uint8_t)((pull(c) & ~B) | U); break;

    case CLC: c->p &= (uint8_t)~C; break;
    case SEC: c->p |= C; break;
    case CLI: c->p &= (uint8_t)~I; break;
    case SEI: c->p |= I; break;
    case CLV: c->p &= (uint8_t)~V; break;
    case CLD: c->p &= (uint8_t)~D; break;
    case SED: c->p |= D; break;

    case BPL: cycles += branch(c, !(c->p & N), addr); break;
    case BMI: cycles += branch(c, c->p & N, addr); break;
    case BVC: cycles += branch(c, !(c->p & V), addr); break;
    case BVS: cycles += branch(c, c->p & V, addr); break;
    case BCC: cycles += branch(c, !(c->p & C), addr); break;
    case BCS: cycles += branch(c, c->p & C, addr); break;
    case BNE: cycles += branch(c, !(c->p & Z), addr); break;
    case BEQ: cycles += branch(c, c->p & Z, addr); break;

    case JMP: c->pc = addr; break;
    case JSR:
        // The return address pushed is the last byte of the JSR.
        push16(c, (uint16_t)(c->pc - 1));
        c->pc = addr;
        break;
    case RTS: c->pc = (uint16_t)(pull16(c) + 1); break;
    case RTI:
        c->p = (uint8_t)((pull(c) & ~B) | U);
        c->pc = pull16(c);
        break;
    case BRK:
        // BRK skips a padding byte, and pushes P with B set so the handler
        // can tell it from an IRQ.
        push16(c, (uint16_t)(c->pc + 1));
        push(c, (uint8_t)(c->p | B | U));
        c->p |= I;
        c->pc = rd16(c, VEC_IRQ);
        break;
    }
    return cycles;
}

// ---------------------------------------------------------------------------
// Disassembler
// ---------------------------------------------------------------------------

int m6502_disassemble(const M6502 *cpu, uint16_t addr, char *out)
{
    M6502 *c = (M6502 *)cpu;
    Opcode o = opcodes[rd(c, addr)];
    const char *name = op_names[o.op];
    uint8_t lo = rd(c, (uint16_t)(addr + 1));
    uint16_t word = (uint16_t)(lo | rd(c, (uint16_t)(addr + 2)) << 8);
    switch (o.mode) {
    case IMM: snprintf(out, 32, "%s #$%02X", name, lo); return 2;
    case ZP: snprintf(out, 32, "%s $%02X", name, lo); return 2;
    case ZPX: snprintf(out, 32, "%s $%02X,X", name, lo); return 2;
    case ZPY: snprintf(out, 32, "%s $%02X,Y", name, lo); return 2;
    case ABS: snprintf(out, 32, "%s $%04X", name, word); return 3;
    case ABX: snprintf(out, 32, "%s $%04X,X", name, word); return 3;
    case ABY: snprintf(out, 32, "%s $%04X,Y", name, word); return 3;
    case IND: snprintf(out, 32, "%s ($%04X)", name, word); return 3;
    case IZX: snprintf(out, 32, "%s ($%02X,X)", name, lo); return 2;
    case IZY: snprintf(out, 32, "%s ($%02X),Y", name, lo); return 2;
    case REL: snprintf(out, 32, "%s $%04X", name, (uint16_t)(addr + 2 + (int8_t)lo)); return 2;
    default:
        if (o.op == ASL || o.op == LSR || o.op == ROL || o.op == ROR) snprintf(out, 32, "%s A", name);
        else snprintf(out, 32, "%s", name);
        return 1;
    }
}
