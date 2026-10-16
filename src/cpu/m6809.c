#include "cpu/m6809.h"

#include <stdio.h>

// How instructions are decoded
// ----------------------------
// Most 6809 opcodes split into an operation and an addressing mode:
//
//   00-0F, 40-7F   read-modify-write operations (NEG, COM, LSR, ... CLR) by
//                  the low nibble, on: memory at a direct address (0x),
//                  A (4x), B (5x), an indexed address (6x), an extended
//                  address (7x)
//   80-BF          operations on A (and 16-bit ones on D, X), by the low
//                  nibble, with the operand: immediate (8x), direct (9x),
//                  indexed (Ax), extended (Bx)
//   C0-FF          the same on B (and on D, U)
//   10-3F          everything else: branches, stack, transfers, LEA
//
// Opcodes 10 and 11 are prefixes for a second page of instructions: long
// branches and the compares, loads and stores on D, Y, U and S that didn't
// fit in the first page.
//
// A direct address is one byte, the low half of an address whose high half
// is the DP register. An indexed address is a postbyte naming the register
// and how to use it (see ea_indexed). Cycle counts follow Motorola's
// MC6809 data sheet.

enum { C = M6809_CC_C, V = M6809_CC_V, Z = M6809_CC_Z, N = M6809_CC_N,
       I = M6809_CC_I, H = M6809_CC_H, F = M6809_CC_F, E = M6809_CC_E };

// Interrupt vectors, at the top of the address space.
#define VEC_SWI3 0xFFF2
#define VEC_SWI2 0xFFF4
#define VEC_FIRQ 0xFFF6
#define VEC_IRQ 0xFFF8
#define VEC_SWI 0xFFFA
#define VEC_NMI 0xFFFC
#define VEC_RESET 0xFFFE

// ---------------------------------------------------------------------------
// Bus access and small helpers
// ---------------------------------------------------------------------------

static uint8_t rd(M6809 *c, uint16_t addr) { return c->bus.read(c->bus.ctx, addr); }
static void wr(M6809 *c, uint16_t addr, uint8_t v) { c->bus.write(c->bus.ctx, addr, v); }

// 16-bit values are stored high byte first.
static uint16_t rd16(M6809 *c, uint16_t addr)
{
    return (uint16_t)(rd(c, addr) << 8 | rd(c, (uint16_t)(addr + 1)));
}

static void wr16(M6809 *c, uint16_t addr, uint16_t v)
{
    wr(c, addr, (uint8_t)(v >> 8));
    wr(c, (uint16_t)(addr + 1), (uint8_t)v);
}

static uint8_t fetch(M6809 *c) { return rd(c, c->pc++); }

static uint16_t fetch16(M6809 *c)
{
    uint16_t v = rd16(c, c->pc);
    c->pc += 2;
    return v;
}

static uint16_t get_d(const M6809 *c) { return (uint16_t)(c->a << 8 | c->b); }

static void set_d(M6809 *c, uint16_t v)
{
    c->a = (uint8_t)(v >> 8);
    c->b = (uint8_t)v;
}

static void set_s(M6809 *c, uint16_t v)
{
    c->s = v;
    c->nmi_armed = true;
}

static void set_flag(M6809 *c, uint8_t flag, bool on)
{
    c->cc = (uint8_t)(on ? c->cc | flag : c->cc & ~flag);
}

// N and Z from a result, V cleared: what loads, stores and logic do.
static void flags_nz8(M6809 *c, uint8_t v)
{
    c->cc = (uint8_t)(c->cc & ~(N | Z | V));
    if (v & 0x80) c->cc |= N;
    if (!v) c->cc |= Z;
}

// N and Z alone, for the shifts that leave V be.
static void flags_nz_only(M6809 *c, uint8_t v)
{
    c->cc = (uint8_t)(c->cc & ~(N | Z));
    if (v & 0x80) c->cc |= N;
    if (!v) c->cc |= Z;
}

static void flags_nz16(M6809 *c, uint16_t v)
{
    c->cc = (uint8_t)(c->cc & ~(N | Z | V));
    if (v & 0x8000) c->cc |= N;
    if (!v) c->cc |= Z;
}

// ---------------------------------------------------------------------------
// The stacks
// ---------------------------------------------------------------------------

static void push8(M6809 *c, uint16_t *sp, uint8_t v) { wr(c, --*sp, v); }
static uint8_t pull8(M6809 *c, uint16_t *sp) { return rd(c, (*sp)++); }

static void push16(M6809 *c, uint16_t *sp, uint16_t v)
{
    push8(c, sp, (uint8_t)v);
    push8(c, sp, (uint8_t)(v >> 8));
}

static uint16_t pull16(M6809 *c, uint16_t *sp)
{
    uint16_t hi = pull8(c, sp);
    return (uint16_t)(hi << 8 | pull8(c, sp));
}

// PSHS/PSHU: push the registers named in mask onto the stack sp points to;
// other is the other stack pointer (U for PSHS, S for PSHU). Returns the
// number of bytes pushed.
static int push_regs(M6809 *c, uint16_t *sp, uint16_t other, uint8_t mask)
{
    int bytes = 0;
    if (mask & 0x80) push16(c, sp, c->pc), bytes += 2;
    if (mask & 0x40) push16(c, sp, other), bytes += 2;
    if (mask & 0x20) push16(c, sp, c->y), bytes += 2;
    if (mask & 0x10) push16(c, sp, c->x), bytes += 2;
    if (mask & 0x08) push8(c, sp, c->dp), bytes++;
    if (mask & 0x04) push8(c, sp, c->b), bytes++;
    if (mask & 0x02) push8(c, sp, c->a), bytes++;
    if (mask & 0x01) push8(c, sp, c->cc), bytes++;
    return bytes;
}

// PULS/PULU: the reverse. Pulling the other stack pointer sets it.
static int pull_regs(M6809 *c, bool system_stack, uint8_t mask)
{
    uint16_t sp = system_stack ? c->s : c->u;
    int bytes = 0;
    if (mask & 0x01) c->cc = pull8(c, &sp), bytes++;
    if (mask & 0x02) c->a = pull8(c, &sp), bytes++;
    if (mask & 0x04) c->b = pull8(c, &sp), bytes++;
    if (mask & 0x08) c->dp = pull8(c, &sp), bytes++;
    if (mask & 0x10) c->x = pull16(c, &sp), bytes += 2;
    if (mask & 0x20) c->y = pull16(c, &sp), bytes += 2;
    uint16_t other = 0;
    if (mask & 0x40) other = pull16(c, &sp), bytes += 2;
    if (mask & 0x80) c->pc = pull16(c, &sp), bytes += 2;
    if (system_stack) {
        set_s(c, sp);
        if (mask & 0x40) c->u = other;
    } else {
        c->u = sp;
        if (mask & 0x40) set_s(c, other);
    }
    return bytes;
}

// Save every register on the system stack, with E set to say so (for an
// NMI, IRQ, SWI or CWAI).
static void push_entire(M6809 *c)
{
    c->cc |= E;
    push_regs(c, &c->s, c->u, 0xFF);
}

// ---------------------------------------------------------------------------
// Addressing
// ---------------------------------------------------------------------------

static uint16_t ea_direct(M6809 *c) { return (uint16_t)(c->dp << 8 | fetch(c)); }

static uint16_t *index_reg(M6809 *c, uint8_t post)
{
    switch ((post >> 5) & 3) {
    case 0: return &c->x;
    case 1: return &c->y;
    case 2: return &c->u;
    default: return &c->s;
    }
}

// An indexed address, from the postbyte that follows the opcode:
//
//     0 r r n n n n n   register + 5-bit signed offset
//     1 r r i t t t t   r = X, Y, U, S; i = indirect; t = the mode:
//
//     0000 ,R+    0001 ,R++   0010 ,-R    0011 ,--R   0100 ,R
//     0101 B,R    0110 A,R    1000 n8,R   1001 n16,R  1011 D,R
//     1100 n8,PC  1101 n16,PC 1111 [n16] (indirect only)
//
// Indirect means the address found is the address of the address. Adds the
// cycles the mode costs to *cycles.
static uint16_t ea_indexed(M6809 *c, int *cycles)
{
    uint8_t post = fetch(c);
    uint16_t *r = index_reg(c, post);
    if (!(post & 0x80)) {
        int offset = post & 0x1F;
        if (offset & 0x10) offset -= 0x20;
        *cycles += 1;
        return (uint16_t)(*r + offset);
    }
    uint16_t ea;
    int extra;
    switch (post & 0x0F) {
    case 0x0: ea = *r; *r += 1; extra = 2; break;
    case 0x1: ea = *r; *r += 2; extra = 3; break;
    case 0x2: *r -= 1; ea = *r; extra = 2; break;
    case 0x3: *r -= 2; ea = *r; extra = 3; break;
    case 0x4: ea = *r; extra = 0; break;
    case 0x5: ea = (uint16_t)(*r + (int8_t)c->b); extra = 1; break;
    case 0x6: ea = (uint16_t)(*r + (int8_t)c->a); extra = 1; break;
    case 0x8: ea = (uint16_t)(*r + (int8_t)fetch(c)); extra = 1; break;
    case 0x9: ea = (uint16_t)(*r + fetch16(c)); extra = 4; break;
    case 0xB: ea = (uint16_t)(*r + get_d(c)); extra = 4; break;
    case 0xC: {
        int8_t offset = (int8_t)fetch(c);
        ea = (uint16_t)(c->pc + offset);
        extra = 1;
        break;
    }
    case 0xD: {
        uint16_t offset = fetch16(c);
        ea = (uint16_t)(c->pc + offset);
        extra = 5;
        break;
    }
    case 0xF:
        ea = fetch16(c);
        *cycles += 5;
        return rd16(c, ea);  // [n16]: always indirect
    default: ea = *r; extra = 0; break;  // 7, A, E: not used by the 6809
    }
    if (post & 0x10) {
        ea = rd16(c, ea);
        extra += 3;
    }
    *cycles += extra;
    return ea;
}

// The address for addressing mode m (1 direct, 2 indexed, 3 extended).
static uint16_t ea_mode(M6809 *c, int m, int *cycles)
{
    if (m == 1) return ea_direct(c);
    if (m == 2) return ea_indexed(c, cycles);
    return fetch16(c);
}

// The operand for mode m (0 immediate, or an address as above).
static uint8_t operand8(M6809 *c, int m, int *cycles)
{
    return m == 0 ? fetch(c) : rd(c, ea_mode(c, m, cycles));
}

static uint16_t operand16(M6809 *c, int m, int *cycles)
{
    return m == 0 ? fetch16(c) : rd16(c, ea_mode(c, m, cycles));
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

static uint8_t add8(M6809 *c, uint8_t a, uint8_t b, int carry)
{
    unsigned r = (unsigned)a + b + (unsigned)carry;
    c->cc = (uint8_t)(c->cc & ~(H | N | Z | V | C));
    if ((a ^ b ^ r) & 0x10) c->cc |= H;
    if (r & 0x80) c->cc |= N;
    if (!(r & 0xFF)) c->cc |= Z;
    if (~(a ^ b) & (a ^ r) & 0x80) c->cc |= V;
    if (r & 0x100) c->cc |= C;
    return (uint8_t)r;
}

static uint8_t sub8(M6809 *c, uint8_t a, uint8_t b, int borrow)
{
    unsigned r = (unsigned)a - b - (unsigned)borrow;
    c->cc = (uint8_t)(c->cc & ~(N | Z | V | C));
    if (r & 0x80) c->cc |= N;
    if (!(r & 0xFF)) c->cc |= Z;
    if ((a ^ b) & (a ^ r) & 0x80) c->cc |= V;
    if (r & 0x100) c->cc |= C;
    return (uint8_t)r;
}

static uint16_t add16(M6809 *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    c->cc = (uint8_t)(c->cc & ~(N | Z | V | C));
    if (r & 0x8000) c->cc |= N;
    if (!(r & 0xFFFF)) c->cc |= Z;
    if (~(a ^ b) & (a ^ r) & 0x8000) c->cc |= V;
    if (r & 0x10000) c->cc |= C;
    return (uint16_t)r;
}

static uint16_t sub16(M6809 *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b;
    c->cc = (uint8_t)(c->cc & ~(N | Z | V | C));
    if (r & 0x8000) c->cc |= N;
    if (!(r & 0xFFFF)) c->cc |= Z;
    if ((a ^ b) & (a ^ r) & 0x8000) c->cc |= V;
    if (r & 0x10000) c->cc |= C;
    return (uint16_t)r;
}

// The read-modify-write operations, by the low nibble of the opcode. The
// unused nibbles 1, 2, 5 and B act (as on the real chip, near enough) like
// their neighbours.
static uint8_t rmw(M6809 *c, int op, uint8_t v)
{
    uint8_t r;
    switch (op) {
    case 0x0: case 0x1:  // NEG
        r = sub8(c, 0, v, 0);
        return r;
    case 0x2: case 0x3:  // COM
        r = (uint8_t)~v;
        flags_nz8(c, r);
        c->cc |= C;
        return r;
    case 0x4: case 0x5:  // LSR
        r = v >> 1;
        flags_nz_only(c, r);
        set_flag(c, C, v & 1);
        return r;
    case 0x6:  // ROR
        r = (uint8_t)((c->cc & C) << 7 | v >> 1);
        flags_nz_only(c, r);
        set_flag(c, C, v & 1);
        return r;
    case 0x7:  // ASR
        r = (uint8_t)((v & 0x80) | v >> 1);
        flags_nz_only(c, r);
        set_flag(c, C, v & 1);
        return r;
    case 0x8:  // ASL (LSL)
        r = (uint8_t)(v << 1);
        flags_nz8(c, r);
        set_flag(c, V, (v ^ r) & 0x80);
        set_flag(c, C, v & 0x80);
        return r;
    case 0x9:  // ROL
        r = (uint8_t)(v << 1 | (c->cc & C));
        flags_nz8(c, r);
        set_flag(c, V, (v ^ r) & 0x80);
        set_flag(c, C, v & 0x80);
        return r;
    case 0xA: case 0xB:  // DEC
        r = (uint8_t)(v - 1);
        flags_nz8(c, r);
        set_flag(c, V, v == 0x80);
        return r;
    case 0xC:  // INC
        r = (uint8_t)(v + 1);
        flags_nz8(c, r);
        set_flag(c, V, v == 0x7F);
        return r;
    case 0xD:  // TST
        flags_nz8(c, v);
        return v;
    default:  // F: CLR
        c->cc = (uint8_t)((c->cc & ~(N | V | C)) | Z);
        return 0;
    }
}

// The 8-bit operations of 80-FF, by the low nibble (all but 3, 7, C-F),
// on accumulator *acc.
static void alu8(M6809 *c, int op, uint8_t *acc, uint8_t v)
{
    switch (op) {
    case 0x0: *acc = sub8(c, *acc, v, 0); break;                  // SUB
    case 0x1: sub8(c, *acc, v, 0); break;                         // CMP
    case 0x2: *acc = sub8(c, *acc, v, c->cc & C); break;          // SBC
    case 0x4: *acc &= v; flags_nz8(c, *acc); break;               // AND
    case 0x5: flags_nz8(c, *acc & v); break;                      // BIT
    case 0x6: *acc = v; flags_nz8(c, v); break;                   // LD
    case 0x8: *acc ^= v; flags_nz8(c, *acc); break;               // EOR
    case 0x9: *acc = add8(c, *acc, v, c->cc & C); break;          // ADC
    case 0xA: *acc |= v; flags_nz8(c, *acc); break;               // OR
    case 0xB: *acc = add8(c, *acc, v, 0); break;                  // ADD
    default: break;
    }
}

static void daa(M6809 *c)
{
    uint8_t fix = 0, hi = c->a & 0xF0, lo = c->a & 0x0F;
    if (lo > 9 || (c->cc & H)) fix |= 0x06;
    if ((hi > 0x80 && lo > 9) || hi > 0x90 || (c->cc & C)) fix |= 0x60;
    unsigned r = (unsigned)c->a + fix;
    c->a = (uint8_t)r;
    flags_nz8(c, c->a);
    if (r & 0x100) c->cc |= C;
}

// The branch conditions, by the low nibble of the opcode.
static bool condition(const M6809 *c, int cond)
{
    uint8_t cc = c->cc;
    bool n = cc & N, z = cc & Z, v = cc & V, carry = cc & C;
    bool result;
    switch (cond >> 1) {
    case 0: result = true; break;                 // BRA / BRN
    case 1: result = !(carry || z); break;        // BHI / BLS
    case 2: result = !carry; break;               // BCC / BCS
    case 3: result = !z; break;                   // BNE / BEQ
    case 4: result = !v; break;                   // BVC / BVS
    case 5: result = !n; break;                   // BPL / BMI
    case 6: result = n == v; break;               // BGE / BLT
    default: result = !z && n == v; break;        // BGT / BLE
    }
    return (cond & 1) ? !result : result;
}

// TFR and EXG name registers by number: 16-bit ones 0-5, 8-bit ones 8-B.
static uint16_t get_reg(const M6809 *c, int n)
{
    switch (n) {
    case 0x0: return get_d(c);
    case 0x1: return c->x;
    case 0x2: return c->y;
    case 0x3: return c->u;
    case 0x4: return c->s;
    case 0x5: return c->pc;
    case 0x8: return (uint16_t)(0xFF00 | c->a);
    case 0x9: return (uint16_t)(0xFF00 | c->b);
    case 0xA: return (uint16_t)(0xFF00 | c->cc);
    case 0xB: return (uint16_t)(0xFF00 | c->dp);
    default: return 0xFFFF;
    }
}

static void set_reg(M6809 *c, int n, uint16_t v)
{
    switch (n) {
    case 0x0: set_d(c, v); break;
    case 0x1: c->x = v; break;
    case 0x2: c->y = v; break;
    case 0x3: c->u = v; break;
    case 0x4: set_s(c, v); break;
    case 0x5: c->pc = v; break;
    case 0x8: c->a = (uint8_t)v; break;
    case 0x9: c->b = (uint8_t)v; break;
    case 0xA: c->cc = (uint8_t)v; break;
    case 0xB: c->dp = (uint8_t)v; break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------

void m6809_init(M6809 *c, M6809Bus bus)
{
    c->bus = bus;
    m6809_reset(c);
}

void m6809_reset(M6809 *c)
{
    c->dp = 0;
    c->cc = I | F;
    c->wait = M6809_RUNNING;
    c->nmi_armed = false;
    c->nmi = c->irq = c->firq = false;
    c->irq_taken = c->firq_taken = false;
    c->pc = rd16(c, VEC_RESET);
}

// Take an interrupt if one is due. Returns the cycles it took, or 0.
static int interrupts(M6809 *c)
{
    if (c->nmi && !c->nmi_armed) c->nmi = false;  // ignored until S is set
    bool cwai = c->wait == M6809_CWAI;            // registers already saved
    if (c->nmi) {
        c->nmi = false;
        if (!cwai) push_entire(c);
        c->wait = M6809_RUNNING;
        c->cc |= I | F;
        c->pc = rd16(c, VEC_NMI);
        return cwai ? 7 : 19;
    }
    if (c->firq && !(c->cc & F)) {
        c->firq_taken = true;
        if (!cwai) {
            c->cc &= (uint8_t)~E;
            push16(c, &c->s, c->pc);
            push8(c, &c->s, c->cc);
        }
        c->wait = M6809_RUNNING;
        c->cc |= I | F;
        c->pc = rd16(c, VEC_FIRQ);
        return cwai ? 7 : 10;
    }
    if (c->irq && !(c->cc & I)) {
        c->irq_taken = true;
        if (!cwai) push_entire(c);
        c->wait = M6809_RUNNING;
        c->cc |= I;
        c->pc = rd16(c, VEC_IRQ);
        return cwai ? 7 : 19;
    }
    // SYNC ends on any interrupt line, even a masked one (and then the
    // program carries on after the SYNC).
    if (c->wait == M6809_SYNC && (c->irq || c->firq)) c->wait = M6809_RUNNING;
    return 0;
}

// ---------------------------------------------------------------------------
// Instructions
// ---------------------------------------------------------------------------

// Base cycle counts by addressing mode (immediate, direct, indexed,
// extended), before what an indexed mode adds.
static const int cyc_alu8[4] = {2, 4, 4, 5};    // 8-bit ops, ST8 (no imm)
static const int cyc_alu16[4] = {4, 6, 6, 7};   // SUBD, ADDD, CMPX
static const int cyc_ld16[4] = {3, 5, 5, 6};    // LDX, LDD, LDU, ST16 (no imm)
static const int cyc_jsr[4] = {7, 7, 7, 8};     // BSR (in the imm slot), JSR

// Instructions on page 2 (after 10) and page 3 (after 11): the compares,
// loads and stores of the registers page 1 had no room for, a cycle
// slower. Returns the cycles, or 0 if op isn't one of them.
static int page23(M6809 *c, int page, uint8_t op)
{
    int m = (op >> 4) & 3, low = op & 0x0F, cycles = 0;
    if (op >= 0x80 && (low == 0x3 || low == 0xC)) {
        uint16_t reg;
        if (page == 2) reg = low == 0x3 ? get_d(c) : c->y;
        else reg = low == 0x3 ? c->u : c->s;
        cycles = cyc_alu16[m] + 1;
        sub16(c, reg, operand16(c, m, &cycles));  // CMPD, CMPY, CMPU, CMPS
        return cycles;
    }
    if (page == 3) return 0;
    if (op >= 0x80 && (low == 0xE || low == 0xF)) {
        bool s_reg = op & 0x40;  // LDS/STS, else LDY/STY
        cycles = cyc_ld16[m] + 1;
        if (low == 0xE) {
            uint16_t v = operand16(c, m, &cycles);
            if (s_reg) set_s(c, v);
            else c->y = v;
            flags_nz16(c, v);
        } else {
            if (m == 0) return 0;
            uint16_t v = s_reg ? c->s : c->y;
            wr16(c, ea_mode(c, m, &cycles), v);
            flags_nz16(c, v);
        }
        return cycles;
    }
    if (op >= 0x21 && op <= 0x2F) {  // long branches
        uint16_t offset = fetch16(c);
        if (condition(c, op & 0x0F)) {
            c->pc = (uint16_t)(c->pc + offset);
            return 6;
        }
        return 5;
    }
    return 0;
}

static int execute(M6809 *c, uint8_t op);

static int prefixed(M6809 *c, int page)
{
    uint8_t op = fetch(c);
    if (op == 0x3F) {  // SWI2, SWI3: like SWI, but interrupts stay unmasked
        push_entire(c);
        c->pc = rd16(c, page == 2 ? VEC_SWI2 : VEC_SWI3);
        return 20;
    }
    int cycles = page23(c, page, op);
    if (cycles) return cycles;
    return 1 + execute(c, op);  // not a page 2/3 instruction: the chip ignores the prefix
}

// Opcodes 10-3F.
static int misc(M6809 *c, uint8_t op)
{
    int cycles;
    switch (op) {
    case 0x10: return prefixed(c, 2);
    case 0x11: return prefixed(c, 3);
    case 0x12: return 2;  // NOP
    case 0x13: c->wait = M6809_SYNC; return 4;
    case 0x16: {  // LBRA
        uint16_t offset = fetch16(c);
        c->pc = (uint16_t)(c->pc + offset);
        return 5;
    }
    case 0x17: {  // LBSR
        uint16_t offset = fetch16(c);
        push16(c, &c->s, c->pc);
        c->pc = (uint16_t)(c->pc + offset);
        return 9;
    }
    case 0x19: daa(c); return 2;
    case 0x1A: c->cc |= fetch(c); return 3;   // ORCC
    case 0x1C: c->cc &= fetch(c); return 3;   // ANDCC
    case 0x1D:  // SEX
        c->a = (c->b & 0x80) ? 0xFF : 0x00;
        flags_nz16(c, get_d(c));
        return 2;
    case 0x1E: {  // EXG
        uint8_t post = fetch(c);
        uint16_t r1 = get_reg(c, post >> 4), r2 = get_reg(c, post & 0x0F);
        set_reg(c, post >> 4, r2);
        set_reg(c, post & 0x0F, r1);
        return 8;
    }
    case 0x1F: {  // TFR
        uint8_t post = fetch(c);
        set_reg(c, post & 0x0F, get_reg(c, post >> 4));
        return 6;
    }
    case 0x30: case 0x31: case 0x32: case 0x33: {  // LEAX, LEAY, LEAS, LEAU
        cycles = 4;
        uint16_t ea = ea_indexed(c, &cycles);
        if (op == 0x30) c->x = ea, set_flag(c, Z, !ea);
        else if (op == 0x31) c->y = ea, set_flag(c, Z, !ea);
        else if (op == 0x32) set_s(c, ea);
        else c->u = ea;
        return cycles;
    }
    case 0x34: return 5 + push_regs(c, &c->s, c->u, fetch(c));  // PSHS
    case 0x35: return 5 + pull_regs(c, true, fetch(c));         // PULS
    case 0x36: return 5 + push_regs(c, &c->u, c->s, fetch(c));  // PSHU
    case 0x37: return 5 + pull_regs(c, false, fetch(c));        // PULU
    case 0x39: c->pc = pull16(c, &c->s); return 5;              // RTS
    case 0x3A: c->x = (uint16_t)(c->x + c->b); return 3;        // ABX
    case 0x3B:  // RTI
        c->cc = pull8(c, &c->s);
        if (c->cc & E) {
            pull_regs(c, true, 0xFE);
            return 15;
        }
        c->pc = pull16(c, &c->s);
        return 6;
    case 0x3C:  // CWAI
        c->cc &= fetch(c);
        push_entire(c);
        c->wait = M6809_CWAI;
        return 20;
    case 0x3D: {  // MUL
        uint16_t r = (uint16_t)(c->a * c->b);
        set_d(c, r);
        set_flag(c, Z, !r);
        set_flag(c, C, r & 0x80);
        return 11;
    }
    case 0x3F:  // SWI
        push_entire(c);
        c->cc |= I | F;
        c->pc = rd16(c, VEC_SWI);
        return 19;
    default:
        if (op >= 0x20 && op <= 0x2F) {  // short branches
            int8_t offset = (int8_t)fetch(c);
            if (condition(c, op & 0x0F)) c->pc = (uint16_t)(c->pc + offset);
            return 3;
        }
        return 2;  // 14, 15, 18, 1B, 38, 3E: not used by the 6809
    }
}

static int execute(M6809 *c, uint8_t op)
{
    int hi = op >> 4, low = op & 0x0F;
    if (hi >= 0x1 && hi <= 0x3) return misc(c, op);

    // Read-modify-write: A, B, or memory.
    if (hi == 0x4 || hi == 0x5) {
        uint8_t *acc = hi == 0x4 ? &c->a : &c->b;
        if (low != 0xE) *acc = rmw(c, low, *acc);
        return 2;
    }
    if (hi == 0x0 || hi == 0x6 || hi == 0x7) {
        int m = hi == 0x0 ? 1 : hi == 0x6 ? 2 : 3;
        int cycles = low == 0xE ? (m == 3 ? 4 : 3) : (m == 3 ? 7 : 6);
        uint16_t ea = ea_mode(c, m, &cycles);
        if (low == 0xE) {  // JMP
            c->pc = ea;
            return cycles;
        }
        uint8_t v = rd(c, ea);
        uint8_t r = rmw(c, low, v);
        if (low != 0xD) wr(c, ea, r);  // TST only reads
        return cycles;
    }

    // 80-FF: operations on A (and D, X) or B (and D, U).
    bool b_side = op & 0x40;
    int m = hi & 3, cycles;
    uint8_t *acc = b_side ? &c->b : &c->a;
    switch (low) {
    case 0x3: {  // SUBD, ADDD
        cycles = cyc_alu16[m];
        uint16_t v = operand16(c, m, &cycles);
        set_d(c, b_side ? add16(c, get_d(c), v) : sub16(c, get_d(c), v));
        return cycles;
    }
    case 0x7:  // STA, STB
        if (m == 0) return 2;
        cycles = cyc_alu8[m];
        wr(c, ea_mode(c, m, &cycles), *acc);
        flags_nz8(c, *acc);
        return cycles;
    case 0xC:
        if (b_side) {  // LDD
            cycles = cyc_ld16[m];
            set_d(c, operand16(c, m, &cycles));
            flags_nz16(c, get_d(c));
        } else {  // CMPX
            cycles = cyc_alu16[m];
            sub16(c, c->x, operand16(c, m, &cycles));
        }
        return cycles;
    case 0xD:
        if (b_side) {  // STD
            if (m == 0) return 2;
            cycles = cyc_ld16[m];
            wr16(c, ea_mode(c, m, &cycles), get_d(c));
            flags_nz16(c, get_d(c));
            return cycles;
        }
        cycles = cyc_jsr[m];
        if (m == 0) {  // BSR
            int8_t offset = (int8_t)fetch(c);
            push16(c, &c->s, c->pc);
            c->pc = (uint16_t)(c->pc + offset);
        } else {  // JSR
            uint16_t ea = ea_mode(c, m, &cycles);
            push16(c, &c->s, c->pc);
            c->pc = ea;
        }
        return cycles;
    case 0xE: {  // LDX, LDU
        cycles = cyc_ld16[m];
        uint16_t v = operand16(c, m, &cycles);
        if (b_side) c->u = v;
        else c->x = v;
        flags_nz16(c, v);
        return cycles;
    }
    case 0xF: {  // STX, STU
        if (m == 0) return 2;
        cycles = cyc_ld16[m];
        uint16_t v = b_side ? c->u : c->x;
        wr16(c, ea_mode(c, m, &cycles), v);
        flags_nz16(c, v);
        return cycles;
    }
    default:
        cycles = cyc_alu8[m];
        alu8(c, low, acc, operand8(c, m, &cycles));
        return cycles;
    }
}

int m6809_step(M6809 *c)
{
    int cycles = interrupts(c);
    if (cycles) return cycles;
    if (c->wait != M6809_RUNNING) return 1;  // idle, waiting for an interrupt
    return execute(c, fetch(c));
}

// ---------------------------------------------------------------------------
// Disassembly
// ---------------------------------------------------------------------------

static uint8_t peek(const M6809 *c, uint16_t addr) { return c->bus.read(c->bus.ctx, addr); }

static uint16_t peek16(const M6809 *c, uint16_t addr)
{
    return (uint16_t)(peek(c, addr) << 8 | peek(c, (uint16_t)(addr + 1)));
}

static const char *const rmw_names[16] = {
    "NEG", "NEG?", "COM?", "COM", "LSR", "LSR?", "ROR", "ASR",
    "ASL", "ROL", "DEC", "DEC?", "INC", "TST", "JMP", "CLR",
};
static const char *const a_names[16] = {
    "SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA", "STA",
    "EORA", "ADCA", "ORA", "ADDA", "CMPX", "JSR", "LDX", "STX",
};
static const char *const b_names[16] = {
    "SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB", "STB",
    "EORB", "ADCB", "ORB", "ADDB", "LDD", "STD", "LDU", "STU",
};
static const char *const branch_names[16] = {
    "BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ",
    "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE",
};
static const char *const reg_names[16] = {
    "D", "X", "Y", "U", "S", "PC", "?", "?", "A", "B", "CC", "DP", "?", "?", "?", "?",
};

// An indexed operand, from the postbyte at *addr (advanced past it).
static void dis_indexed(const M6809 *c, uint16_t *addr, char *out, size_t size)
{
    static const char regs[4] = {'X', 'Y', 'U', 'S'};
    uint8_t post = peek(c, (*addr)++);
    char r = regs[(post >> 5) & 3];
    if (!(post & 0x80)) {
        int offset = post & 0x1F;
        if (offset & 0x10) offset -= 0x20;
        snprintf(out, size, "%d,%c", offset, r);
        return;
    }
    char inner[24];
    switch (post & 0x0F) {
    case 0x0: snprintf(inner, sizeof inner, ",%c+", r); break;
    case 0x1: snprintf(inner, sizeof inner, ",%c++", r); break;
    case 0x2: snprintf(inner, sizeof inner, ",-%c", r); break;
    case 0x3: snprintf(inner, sizeof inner, ",--%c", r); break;
    case 0x4: snprintf(inner, sizeof inner, ",%c", r); break;
    case 0x5: snprintf(inner, sizeof inner, "B,%c", r); break;
    case 0x6: snprintf(inner, sizeof inner, "A,%c", r); break;
    case 0x8: snprintf(inner, sizeof inner, "%d,%c", (int8_t)peek(c, (*addr)++), r); break;
    case 0x9: snprintf(inner, sizeof inner, "$%04X,%c", peek16(c, *addr), r); *addr += 2; break;
    case 0xB: snprintf(inner, sizeof inner, "D,%c", r); break;
    case 0xC: {
        int8_t offset = (int8_t)peek(c, (*addr)++);
        snprintf(inner, sizeof inner, "$%04X,PCR", (uint16_t)(*addr + offset));
        break;
    }
    case 0xD: {
        uint16_t offset = peek16(c, *addr);
        *addr += 2;
        snprintf(inner, sizeof inner, "$%04X,PCR", (uint16_t)(*addr + offset));
        break;
    }
    case 0xF: snprintf(inner, sizeof inner, "$%04X", peek16(c, *addr)); *addr += 2; break;
    default: snprintf(inner, sizeof inner, "?,%c", r); break;
    }
    if (post & 0x10) snprintf(out, size, "[%s]", inner);
    else snprintf(out, size, "%s", inner);
}

// A register list for PSH/PUL; other is the other stack's name.
static void dis_reglist(uint8_t mask, const char *other, char *out, size_t size)
{
    static const char *const names[8] = {"CC", "A", "B", "DP", "X", "Y", NULL, "PC"};
    size_t len = 0;
    out[0] = 0;
    for (int bit = 0; bit < 8; bit++) {
        if (!(mask & (1 << bit))) continue;
        const char *name = bit == 6 ? other : names[bit];
        len += (size_t)snprintf(out + len, size - len, "%s%s", len ? "," : "", name);
        if (len >= size) break;
    }
}

// The operand for addressing mode m (0 immediate, 1 direct, 2 indexed,
// 3 extended); wide: a 16-bit immediate.
static void dis_operand(const M6809 *c, int m, bool wide, uint16_t *addr, char *out, size_t size)
{
    switch (m) {
    case 0:
        if (wide) snprintf(out, size, "#$%04X", peek16(c, *addr)), *addr += 2;
        else snprintf(out, size, "#$%02X", peek(c, (*addr)++));
        break;
    case 1: snprintf(out, size, "<$%02X", peek(c, (*addr)++)); break;
    case 2: dis_indexed(c, addr, out, size); break;
    default: snprintf(out, size, "$%04X", peek16(c, *addr)); *addr += 2; break;
    }
}

int m6809_disassemble(const M6809 *c, uint16_t start, char *out)
{
    uint16_t addr = start;
    uint8_t op = peek(c, addr++);
    int page = 1;
    if (op == 0x10 || op == 0x11) {
        page = op == 0x10 ? 2 : 3;
        op = peek(c, addr++);
    }
    int hi = op >> 4, low = op & 0x0F;
    char arg[48] = "";
    const char *name = "???";

    if (page > 1) {
        int m = hi & 3;
        if (op == 0x3F) {
            name = page == 2 ? "SWI2" : "SWI3";
        } else if (page == 2 && op >= 0x21 && op <= 0x2F) {
            static char lname[8];
            snprintf(lname, sizeof lname, "L%s", branch_names[low]);
            name = lname;
            uint16_t offset = peek16(c, addr);
            addr += 2;
            snprintf(arg, sizeof arg, "$%04X", (uint16_t)(addr + offset));
        } else if (op >= 0x80 && (low == 0x3 || low == 0xC)) {
            static const char *const cmp[2][2] = {{"CMPD", "CMPY"}, {"CMPU", "CMPS"}};
            name = cmp[page - 2][low == 0xC];
            dis_operand(c, m, true, &addr, arg, sizeof arg);
        } else if (page == 2 && op >= 0x80 && (low == 0xE || low == 0xF)) {
            static const char *const ldst[2][2] = {{"LDY", "STY"}, {"LDS", "STS"}};
            name = ldst[(op & 0x40) != 0][low == 0xF];
            dis_operand(c, m, true, &addr, arg, sizeof arg);
        }
    } else if (hi == 0x4 || hi == 0x5) {
        static char rname[8];
        snprintf(rname, sizeof rname, "%s%c", rmw_names[low], hi == 0x4 ? 'A' : 'B');
        name = rname;
    } else if (hi == 0x0 || hi == 0x6 || hi == 0x7) {
        name = rmw_names[low];
        dis_operand(c, hi == 0x0 ? 1 : hi == 0x6 ? 2 : 3, false, &addr, arg, sizeof arg);
    } else if (hi >= 0x8) {
        bool b_side = op & 0x40;
        int m = hi & 3;
        name = b_side ? b_names[low] : a_names[low];
        if (!b_side && op == 0x8D) {
            name = "BSR";
            int8_t offset = (int8_t)peek(c, addr++);
            snprintf(arg, sizeof arg, "$%04X", (uint16_t)(addr + offset));
        } else {
            bool wide = low == 0x3 || low >= 0xC;
            dis_operand(c, m, wide, &addr, arg, sizeof arg);
        }
    } else if (hi == 0x2) {
        name = branch_names[low];
        int8_t offset = (int8_t)peek(c, addr++);
        snprintf(arg, sizeof arg, "$%04X", (uint16_t)(addr + offset));
    } else {
        switch (op) {
        case 0x12: name = "NOP"; break;
        case 0x13: name = "SYNC"; break;
        case 0x16: case 0x17: {
            name = op == 0x16 ? "LBRA" : "LBSR";
            uint16_t offset = peek16(c, addr);
            addr += 2;
            snprintf(arg, sizeof arg, "$%04X", (uint16_t)(addr + offset));
            break;
        }
        case 0x19: name = "DAA"; break;
        case 0x1A: name = "ORCC"; snprintf(arg, sizeof arg, "#$%02X", peek(c, addr++)); break;
        case 0x1C: name = "ANDCC"; snprintf(arg, sizeof arg, "#$%02X", peek(c, addr++)); break;
        case 0x1D: name = "SEX"; break;
        case 0x1E: case 0x1F: {
            name = op == 0x1E ? "EXG" : "TFR";
            uint8_t post = peek(c, addr++);
            snprintf(arg, sizeof arg, "%s,%s", reg_names[post >> 4], reg_names[post & 0x0F]);
            break;
        }
        case 0x30: case 0x31: case 0x32: case 0x33: {
            static const char *const lea[4] = {"LEAX", "LEAY", "LEAS", "LEAU"};
            name = lea[low];
            dis_indexed(c, &addr, arg, sizeof arg);
            break;
        }
        case 0x34: case 0x35: case 0x36: case 0x37: {
            static const char *const stack[4] = {"PSHS", "PULS", "PSHU", "PULU"};
            name = stack[low - 4];
            dis_reglist(peek(c, addr++), low < 6 ? "U" : "S", arg, sizeof arg);
            break;
        }
        case 0x39: name = "RTS"; break;
        case 0x3A: name = "ABX"; break;
        case 0x3B: name = "RTI"; break;
        case 0x3C: name = "CWAI"; snprintf(arg, sizeof arg, "#$%02X", peek(c, addr++)); break;
        case 0x3D: name = "MUL"; break;
        case 0x3F: name = "SWI"; break;
        default: break;
        }
    }
    if (arg[0]) snprintf(out, 32, "%-6s%s", name, arg);
    else snprintf(out, 32, "%s", name);
    return (uint16_t)(addr - start);
}
