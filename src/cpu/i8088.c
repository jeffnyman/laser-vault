#include "cpu/i8088.h"

#include <stdio.h>
#include <string.h>

// How instructions are decoded
// ----------------------------
// An instruction is any number of prefix bytes (a segment to use instead
// of the usual one, a repeat for string instructions, LOCK), an opcode
// byte, and then, for instructions with a register or memory operand, a
// "ModRM" byte:
//
//     mod (2 bits)  3: the operand is a register; otherwise it's memory, at
//                   an address from rm, plus no displacement (0), a signed
//                   byte (1) or a word (2) that follows
//     reg (3 bits)  the other operand's register, or more of the opcode
//     rm (3 bits)   the register, or how to make the address:
//                   BX+SI, BX+DI, BP+SI, BP+DI, SI, DI, BP, BX
//                   (with mod 0, BP's place is taken by a plain address)
//
// Addresses made with BP are in the stack segment, the others in the data
// segment, unless a prefix says otherwise. Then any displacement and an
// immediate value. Many opcodes come in pairs, byte and word, told apart
// by the low bit.
//
// Cycle counts are the 8086's from Intel's tables, plus four for each word
// the 8088 moves over its 8-bit bus, and the time to work out an address.

enum { CF = I8088_CF, PF = I8088_PF, AF = I8088_AF, ZF = I8088_ZF, SF = I8088_SF, TF = I8088_TF,
       IF = I8088_IF, DF = I8088_DF, OF = I8088_OF };
enum { AX, CX, DX, BX, SP, BP, SI, DI };
enum { ES, CS, SS, DS };

// The flags that exist; the rest read as 1 (bits 1 and 12-15) or 0.
#define FLAGS_USED 0x0FD5
#define FLAGS_ONES 0xF002

static uint16_t flags_value(uint16_t f) { return (uint16_t)((f & FLAGS_USED) | FLAGS_ONES); }

static uint32_t linear(uint16_t seg, uint16_t off) { return ((uint32_t)seg * 16 + off) & 0xFFFFF; }

static uint8_t rd8(I8088 *c, int seg, uint16_t off)
{
    return c->bus.read(c->bus.ctx, linear(c->s[seg], off));
}

// A word at the end of a segment wraps round to its start.
static uint16_t rd16(I8088 *c, int seg, uint16_t off)
{
    return (uint16_t)(rd8(c, seg, off) | rd8(c, seg, (uint16_t)(off + 1)) << 8);
}

static void wr8(I8088 *c, int seg, uint16_t off, uint8_t v)
{
    c->bus.write(c->bus.ctx, linear(c->s[seg], off), v);
}

static void wr16(I8088 *c, int seg, uint16_t off, uint16_t v)
{
    wr8(c, seg, off, (uint8_t)v);
    wr8(c, seg, (uint16_t)(off + 1), (uint8_t)(v >> 8));
}

static uint8_t fetch8(I8088 *c) { return rd8(c, CS, c->ip++); }

static uint16_t fetch16(I8088 *c)
{
    uint16_t lo = fetch8(c);
    return (uint16_t)(lo | fetch8(c) << 8);
}

// Byte registers: AL, CL, DL, BL are the low halves of AX..BX, AH..BH the high.
static uint8_t get_r8(const I8088 *c, int n)
{
    return n < 4 ? (uint8_t)c->r[n] : (uint8_t)(c->r[n - 4] >> 8);
}

static void set_r8(I8088 *c, int n, uint8_t v)
{
    if (n < 4) c->r[n] = (uint16_t)((c->r[n] & 0xFF00) | v);
    else c->r[n - 4] = (uint16_t)((c->r[n - 4] & 0x00FF) | v << 8);
}

static void push(I8088 *c, uint16_t v)
{
    c->r[SP] -= 2;
    wr16(c, SS, c->r[SP], v);
}

static uint16_t pop(I8088 *c)
{
    uint16_t v = rd16(c, SS, c->r[SP]);
    c->r[SP] += 2;
    return v;
}

// The data segment, or the one a prefix gave.
static int data_seg(const I8088 *c) { return c->seg_prefix >= 0 ? c->seg_prefix : DS; }

static void interrupt(I8088 *c, uint8_t n)
{
    push(c, flags_value(c->flags));
    c->flags &= (uint16_t)~(IF | TF);
    push(c, c->s[CS]);
    push(c, c->ip);
    uint32_t vec = (uint32_t)n * 4;
    c->ip = (uint16_t)(c->bus.read(c->bus.ctx, vec) | c->bus.read(c->bus.ctx, vec + 1) << 8);
    c->s[CS] = (uint16_t)(c->bus.read(c->bus.ctx, vec + 2) | c->bus.read(c->bus.ctx, vec + 3) << 8);
}

// The ModRM byte
// --------------

typedef struct {
    int mod, reg, rm;
    int seg;       // the memory operand's segment and offset, when mod isn't 3
    uint16_t off;
    int cycles;    // time to work out the address
} ModRM;

static void decode_modrm(I8088 *c, ModRM *m)
{
    uint8_t b = fetch8(c);
    m->mod = b >> 6;
    m->reg = (b >> 3) & 7;
    m->rm = b & 7;
    m->seg = DS;
    m->off = 0;
    m->cycles = 0;
    if (m->mod == 3) return;
    uint16_t *r = c->r;
    switch (m->rm) {
    case 0: m->off = (uint16_t)(r[BX] + r[SI]); m->cycles = 7; break;
    case 1: m->off = (uint16_t)(r[BX] + r[DI]); m->cycles = 8; break;
    case 2: m->off = (uint16_t)(r[BP] + r[SI]); m->seg = SS; m->cycles = 8; break;
    case 3: m->off = (uint16_t)(r[BP] + r[DI]); m->seg = SS; m->cycles = 7; break;
    case 4: m->off = r[SI]; m->cycles = 5; break;
    case 5: m->off = r[DI]; m->cycles = 5; break;
    case 6:
        if (m->mod == 0) {
            m->off = fetch16(c);
            m->cycles = 6;
        } else {
            m->off = r[BP];
            m->seg = SS;
            m->cycles = 5;
        }
        break;
    case 7: m->off = r[BX]; m->cycles = 5; break;
    }
    if (m->mod == 1) {
        m->off = (uint16_t)(m->off + (int8_t)fetch8(c));
        m->cycles += 4;
    } else if (m->mod == 2) {
        m->off = (uint16_t)(m->off + fetch16(c));
        m->cycles += 4;
    }
    if (c->seg_prefix >= 0) {
        m->seg = c->seg_prefix;
        m->cycles += 2;
    }
}

static uint8_t get_rm8(I8088 *c, const ModRM *m)
{
    return m->mod == 3 ? get_r8(c, m->rm) : rd8(c, m->seg, m->off);
}

static uint16_t get_rm16(I8088 *c, const ModRM *m)
{
    return m->mod == 3 ? c->r[m->rm] : rd16(c, m->seg, m->off);
}

static void set_rm8(I8088 *c, const ModRM *m, uint8_t v)
{
    if (m->mod == 3) set_r8(c, m->rm, v);
    else wr8(c, m->seg, m->off, v);
}

static void set_rm16(I8088 *c, const ModRM *m, uint16_t v)
{
    if (m->mod == 3) c->r[m->rm] = v;
    else wr16(c, m->seg, m->off, v);
}

static uint16_t get_rm(I8088 *c, const ModRM *m, bool w) { return w ? get_rm16(c, m) : get_rm8(c, m); }

static void set_rm(I8088 *c, const ModRM *m, bool w, uint16_t v)
{
    if (w) set_rm16(c, m, v);
    else set_rm8(c, m, (uint8_t)v);
}

static uint16_t get_reg(const I8088 *c, int n, bool w) { return w ? c->r[n] : get_r8(c, n); }

static void set_reg(I8088 *c, int n, bool w, uint16_t v)
{
    if (w) c->r[n] = v;
    else set_r8(c, n, (uint8_t)v);
}

// Cycles for an instruction with a register or memory operand: reg if
// it's a register, else mem plus the address and 4 for each word moved.
static int timing(const ModRM *m, bool w, int reg, int mem, int words)
{
    return m->mod == 3 ? reg : mem + m->cycles + (w ? 4 * words : 0);
}

// Arithmetic
// ----------

static void set_flag(I8088 *c, uint16_t flag, bool on)
{
    if (on) c->flags |= flag;
    else c->flags &= (uint16_t)~flag;
}

static uint16_t parity(uint8_t v)
{
    v ^= v >> 4;
    return (0x6996 >> (v & 15)) & 1 ? 0 : PF;
}

static void set_szp(I8088 *c, uint16_t v, bool w)
{
    uint16_t sign = w ? 0x8000 : 0x80;
    uint16_t mask = w ? 0xFFFF : 0xFF;
    c->flags &= (uint16_t)~(SF | ZF | PF);
    if (v & sign) c->flags |= SF;
    if (!(v & mask)) c->flags |= ZF;
    c->flags |= parity((uint8_t)v);
}

// The eight operations of ADD, OR, ADC, SBB, AND, SUB, XOR, CMP, in the
// order their opcodes number them.
enum { OP_ADD, OP_OR, OP_ADC, OP_SBB, OP_AND, OP_SUB, OP_XOR, OP_CMP };

static uint16_t alu(I8088 *c, int op, uint16_t a, uint16_t b, bool w)
{
    uint32_t mask = w ? 0xFFFF : 0xFF, sign = w ? 0x8000 : 0x80;
    uint32_t r;
    switch (op) {
    case OP_ADD:
    case OP_ADC:
        r = (uint32_t)a + b + (op == OP_ADC && (c->flags & CF));
        set_flag(c, CF, r > mask);
        set_flag(c, OF, (a ^ r) & (b ^ r) & sign);
        set_flag(c, AF, (a ^ b ^ r) & 0x10);
        break;
    case OP_SBB:
    case OP_SUB:
    case OP_CMP:
        r = (uint32_t)a - b - (op == OP_SBB && (c->flags & CF));
        set_flag(c, CF, r & (mask + 1));
        set_flag(c, OF, (a ^ b) & (a ^ r) & sign);
        set_flag(c, AF, (a ^ b ^ r) & 0x10);
        break;
    default:
        r = op == OP_OR ? (uint32_t)(a | b) : op == OP_AND ? (uint32_t)(a & b) : (uint32_t)(a ^ b);
        c->flags &= (uint16_t)~(CF | OF | AF);
        break;
    }
    r &= mask;
    set_szp(c, (uint16_t)r, w);
    return (uint16_t)r;
}

// INC and DEC leave the carry alone.
static uint16_t inc_dec(I8088 *c, uint16_t v, bool dec, bool w)
{
    uint16_t carry = c->flags & CF;
    v = alu(c, dec ? OP_SUB : OP_ADD, v, 1, w);
    c->flags = (uint16_t)((c->flags & ~CF) | carry);
    return v;
}

// Shifts and rotates, by count (not limited, on the 8086). Rotates change
// only CF and OF; a count of 0 changes nothing.
static uint16_t shift(I8088 *c, int op, uint16_t v, int count, bool w)
{
    if (count == 0) return v;
    uint16_t sign = w ? 0x8000 : 0x80, mask = w ? 0xFFFF : 0xFF;
    bool cf = c->flags & CF, of = false;
    for (int i = 0; i < count; i++) {
        bool out;
        switch (op) {
        case 0:  // ROL
            cf = v & sign;
            v = (uint16_t)(((v << 1) | cf) & mask);
            of = ((v & sign) != 0) != cf;
            break;
        case 1:  // ROR
            cf = v & 1;
            v = (uint16_t)((v >> 1) | (cf ? sign : 0));
            of = ((v ^ (v << 1)) & sign) != 0;
            break;
        case 2:  // RCL
            out = v & sign;
            v = (uint16_t)(((v << 1) | cf) & mask);
            cf = out;
            of = ((v & sign) != 0) != cf;
            break;
        case 3:  // RCR
            out = v & 1;
            v = (uint16_t)((v >> 1) | (cf ? sign : 0));
            cf = out;
            of = ((v ^ (v << 1)) & sign) != 0;
            break;
        case 4:  // SHL
            cf = v & sign;
            v = (uint16_t)((v << 1) & mask);
            of = ((v & sign) != 0) != cf;
            break;
        case 5:  // SHR
            of = v & sign;
            cf = v & 1;
            v >>= 1;
            break;
        case 6:  // SETMO (undocumented): all ones
            v = mask;
            cf = of = false;
            break;
        case 7:  // SAR
            cf = v & 1;
            v = (uint16_t)((v >> 1) | (v & sign));
            of = false;
            break;
        }
    }
    set_flag(c, CF, cf);
    set_flag(c, OF, of);
    if (op >= 4) {
        set_szp(c, v, w);
        c->flags &= (uint16_t)~AF;
    }
    return v;
}

// Conditional jumps 70-7F: O, NO, B, AE, E, NE, BE, A, S, NS, P, NP, L, GE, LE, G.
static bool condition(const I8088 *c, int n)
{
    uint16_t f = c->flags;
    bool lt = !(f & SF) != !(f & OF);
    bool r = false;
    switch (n >> 1) {
    case 0: r = f & OF; break;
    case 1: r = f & CF; break;
    case 2: r = f & ZF; break;
    case 3: r = f & (CF | ZF); break;
    case 4: r = f & SF; break;
    case 5: r = f & PF; break;
    case 6: r = lt; break;
    case 7: r = lt || (f & ZF); break;
    }
    return (n & 1) ? !r : r;
}

static void jump_short(I8088 *c, uint8_t rel) { c->ip = (uint16_t)(c->ip + (int8_t)rel); }

// Group 3 (F6, F7): TEST, NOT, NEG, MUL, IMUL, DIV, IDIV.
static int group3(I8088 *c, bool w)
{
    ModRM m;
    decode_modrm(c, &m);
    uint16_t v = get_rm(c, &m, w);
    switch (m.reg) {
    case 0:
    case 1: {  // TEST (1 is an alias)
        uint16_t imm = w ? fetch16(c) : fetch8(c);
        alu(c, OP_AND, v, imm, w);
        return timing(&m, w, 5, 11, 1);
    }
    case 2:  // NOT
        set_rm(c, &m, w, (uint16_t)~v);
        return timing(&m, w, 3, 16, 2);
    case 3:  // NEG
        set_rm(c, &m, w, alu(c, OP_SUB, 0, v, w));
        return timing(&m, w, 3, 16, 2);
    case 4:  // MUL
        if (!w) {
            uint16_t r = (uint16_t)(get_r8(c, 0) * (uint8_t)v);
            c->r[AX] = r;
            set_flag(c, CF | OF, r & 0xFF00);
            set_szp(c, r >> 8, false);
            return timing(&m, w, 73, 79, 0);
        } else {
            uint32_t r = (uint32_t)c->r[AX] * v;
            c->r[AX] = (uint16_t)r;
            c->r[DX] = (uint16_t)(r >> 16);
            set_flag(c, CF | OF, c->r[DX]);
            set_szp(c, c->r[DX], true);
            return timing(&m, w, 125, 131, 1);
        }
    case 5:  // IMUL
        if (!w) {
            int16_t r = (int16_t)((int8_t)get_r8(c, 0) * (int8_t)v);
            c->r[AX] = (uint16_t)r;
            set_flag(c, CF | OF, r != (int8_t)r);
            set_szp(c, (uint16_t)(r >> 8), false);
            return timing(&m, w, 89, 95, 0);
        } else {
            int32_t r = (int32_t)(int16_t)c->r[AX] * (int16_t)v;
            c->r[AX] = (uint16_t)r;
            c->r[DX] = (uint16_t)((uint32_t)r >> 16);
            set_flag(c, CF | OF, r != (int16_t)r);
            set_szp(c, c->r[DX], true);
            return timing(&m, w, 140, 146, 1);
        }
    case 6:  // DIV: a quotient too big (or dividing by 0) is interrupt 0
        if (!w) {
            uint16_t d = c->r[AX];
            if (v == 0 || d / v > 0xFF) {
                interrupt(c, 0);
                return 80;
            }
            c->r[AX] = (uint16_t)((d % v) << 8 | (d / v));
            return timing(&m, w, 85, 91, 0);
        } else {
            uint32_t d = (uint32_t)c->r[DX] << 16 | c->r[AX];
            if (v == 0 || d / v > 0xFFFF) {
                interrupt(c, 0);
                return 80;
            }
            c->r[AX] = (uint16_t)(d / v);
            c->r[DX] = (uint16_t)(d % v);
            return timing(&m, w, 150, 156, 1);
        }
    default:  // IDIV: the 8086's quotients run from -127 to 127 (bytes), -32767 to 32767
        if (!w) {
            int16_t d = (int16_t)c->r[AX];
            int8_t s = (int8_t)v;
            if (s == 0) {
                interrupt(c, 0);
                return 80;
            }
            int q = d / s, rem = d % s;
            if (c->rep) q = -q;  // a quirk of the 8086's microcode
            if (q > 127 || q < -127) {
                interrupt(c, 0);
                return 80;
            }
            c->r[AX] = (uint16_t)((uint8_t)rem << 8 | (uint8_t)q);
            return timing(&m, w, 106, 112, 0);
        } else {
            int32_t d = (int32_t)((uint32_t)c->r[DX] << 16 | c->r[AX]);
            int16_t s = (int16_t)v;
            if (s == 0 || (d == INT32_MIN && s == -1)) {
                interrupt(c, 0);
                return 80;
            }
            int32_t q = d / s, rem = d % s;
            if (c->rep) q = -q;
            if (q > 32767 || q < -32767) {
                interrupt(c, 0);
                return 80;
            }
            c->r[AX] = (uint16_t)q;
            c->r[DX] = (uint16_t)rem;
            return timing(&m, w, 170, 176, 1);
        }
    }
}

// String instructions: MOVS, CMPS, STOS, LODS, SCAS. With a repeat prefix
// they run CX times (CMPS and SCAS also stop when ZF says so). A long run
// stops part way to let an interrupt in, and picks up again after.
static int string_op(I8088 *c, uint8_t op)
{
    bool w = op & 1;
    int16_t step = (int16_t)((c->flags & DF) ? -(w ? 2 : 1) : (w ? 2 : 1));
    int src = data_seg(c);
    int base = op & 0xFE;
    int cycles = 0;
    if (c->rep && c->r[CX] == 0) return 9;
    for (;;) {
        uint16_t a, b;
        switch (base) {
        case 0xA4:  // MOVS
            if (w) wr16(c, ES, c->r[DI], rd16(c, src, c->r[SI]));
            else wr8(c, ES, c->r[DI], rd8(c, src, c->r[SI]));
            c->r[SI] += (uint16_t)step;
            c->r[DI] += (uint16_t)step;
            cycles += w ? 25 : 17;
            break;
        case 0xA6:  // CMPS
            a = w ? rd16(c, src, c->r[SI]) : rd8(c, src, c->r[SI]);
            b = w ? rd16(c, ES, c->r[DI]) : rd8(c, ES, c->r[DI]);
            alu(c, OP_CMP, a, b, w);
            c->r[SI] += (uint16_t)step;
            c->r[DI] += (uint16_t)step;
            cycles += w ? 30 : 22;
            break;
        case 0xAA:  // STOS
            if (w) wr16(c, ES, c->r[DI], c->r[AX]);
            else wr8(c, ES, c->r[DI], get_r8(c, 0));
            c->r[DI] += (uint16_t)step;
            cycles += w ? 14 : 10;
            break;
        case 0xAC:  // LODS
            if (w) c->r[AX] = rd16(c, src, c->r[SI]);
            else set_r8(c, 0, rd8(c, src, c->r[SI]));
            c->r[SI] += (uint16_t)step;
            cycles += w ? 16 : 12;
            break;
        default:  // SCAS
            b = w ? rd16(c, ES, c->r[DI]) : rd8(c, ES, c->r[DI]);
            alu(c, OP_CMP, w ? c->r[AX] : get_r8(c, 0), b, w);
            c->r[DI] += (uint16_t)step;
            cycles += w ? 19 : 15;
            break;
        }
        if (!c->rep) return cycles;
        if (--c->r[CX] == 0) break;
        if (base == 0xA6 || base == 0xAE) {
            bool zf = c->flags & ZF;
            if ((c->rep == 0xF3 && !zf) || (c->rep == 0xF2 && zf)) break;
        }
        if (c->nmi || (c->intr && (c->flags & IF)) || cycles > 4000) {
            c->ip = c->start_ip;  // the rest later
            break;
        }
    }
    return cycles + 9;
}

static uint8_t in8(I8088 *c, uint16_t port) { return c->bus.in(c->bus.ctx, port); }

static void out8(I8088 *c, uint16_t port, uint8_t v) { c->bus.out(c->bus.ctx, port, v); }

// BCD adjustments, the 8086's way.
static void daa_das(I8088 *c, bool sub)
{
    uint8_t al = get_r8(c, 0), old = al;
    bool af = c->flags & AF, cf = c->flags & CF;
    if ((al & 0x0F) > 9 || af) {
        al = (uint8_t)(sub ? al - 6 : al + 6);
        c->flags |= AF;
    } else c->flags &= (uint16_t)~AF;
    if (old > (af ? 0x9F : 0x99) || cf) {
        al = (uint8_t)(sub ? al - 0x60 : al + 0x60);
        c->flags |= CF;
    } else c->flags &= (uint16_t)~CF;
    set_r8(c, 0, al);
    set_szp(c, al, false);
}

static void aaa_aas(I8088 *c, bool sub)
{
    uint8_t al = get_r8(c, 0), ah = get_r8(c, 4);
    if ((al & 0x0F) > 9 || (c->flags & AF)) {
        al = (uint8_t)(sub ? al - 6 : al + 6);
        ah = (uint8_t)(sub ? ah - 1 : ah + 1);
        c->flags |= AF | CF;
    } else c->flags &= (uint16_t)~(AF | CF);
    set_r8(c, 0, al & 0x0F);
    set_r8(c, 4, ah);
}

// One instruction, from its opcode (prefixes already taken).
static int execute(I8088 *c, uint8_t op)
{
    ModRM m;
    bool w = op & 1;

    // 00-3F: ADD, OR, ADC, SBB, AND, SUB, XOR, CMP in six forms each.
    if (op < 0x40 && (op & 7) < 6) {
        int aop = op >> 3;
        uint16_t r;
        switch (op & 7) {
        case 0:
        case 1:  // r/m, reg
            decode_modrm(c, &m);
            r = alu(c, aop, get_rm(c, &m, w), get_reg(c, m.reg, w), w);
            if (aop != OP_CMP) set_rm(c, &m, w, r);
            return timing(&m, w, 3, aop == OP_CMP ? 9 : 16, aop == OP_CMP ? 1 : 2);
        case 2:
        case 3:  // reg, r/m
            decode_modrm(c, &m);
            r = alu(c, aop, get_reg(c, m.reg, w), get_rm(c, &m, w), w);
            if (aop != OP_CMP) set_reg(c, m.reg, w, r);
            return timing(&m, w, 3, 9, 1);
        case 4:  // AL, immediate
            r = alu(c, aop, get_r8(c, 0), fetch8(c), false);
            if (aop != OP_CMP) set_r8(c, 0, (uint8_t)r);
            return 4;
        default:  // AX, immediate
            r = alu(c, aop, c->r[AX], fetch16(c), true);
            if (aop != OP_CMP) c->r[AX] = r;
            return 4;
        }
    }

    switch (op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E:  // PUSH segment
        push(c, c->s[op >> 3]);
        return 14;
    case 0x07: case 0x0F: case 0x17: case 0x1F:  // POP segment (POP CS works, on the 8086)
        c->s[op >> 3] = pop(c);
        if (op == 0x17) c->no_irq = true;
        return 12;
    case 0x27: daa_das(c, false); return 4;  // DAA
    case 0x2F: daa_das(c, true); return 4;   // DAS
    case 0x37: aaa_aas(c, false); return 8;  // AAA
    case 0x3F: aaa_aas(c, true); return 8;   // AAS

    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        c->r[op & 7] = inc_dec(c, c->r[op & 7], false, true);  // INC reg
        return 3;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        c->r[op & 7] = inc_dec(c, c->r[op & 7], true, true);  // DEC reg
        return 3;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        if (op == 0x54) {  // PUSH SP pushes the value after the push, on the 8086
            c->r[SP] -= 2;
            wr16(c, SS, c->r[SP], c->r[SP]);
        } else push(c, c->r[op & 7]);
        return 15;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        c->r[op & 7] = pop(c);  // POP reg
        return 12;

    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        uint8_t rel = fetch8(c);  // Jcc (60-6F are aliases, on the 8086)
        if (!condition(c, op & 15)) return 4;
        jump_short(c, rel);
        return 16;
    }

    case 0x80: case 0x81: case 0x82: case 0x83: {  // ALU r/m, immediate (82 is 80; 83 a signed byte)
        decode_modrm(c, &m);
        uint16_t a = get_rm(c, &m, w);
        uint16_t imm = op == 0x81 ? fetch16(c) : op == 0x83 ? (uint16_t)(int8_t)fetch8(c) : fetch8(c);
        uint16_t r = alu(c, m.reg, a, imm, w);
        if (m.reg != OP_CMP) set_rm(c, &m, w, r);
        return timing(&m, w, 4, m.reg == OP_CMP ? 10 : 17, m.reg == OP_CMP ? 1 : 2);
    }
    case 0x84: case 0x85:  // TEST r/m, reg
        decode_modrm(c, &m);
        alu(c, OP_AND, get_rm(c, &m, w), get_reg(c, m.reg, w), w);
        return timing(&m, w, 3, 9, 1);
    case 0x86: case 0x87: {  // XCHG r/m, reg
        decode_modrm(c, &m);
        uint16_t a = get_rm(c, &m, w);
        set_rm(c, &m, w, get_reg(c, m.reg, w));
        set_reg(c, m.reg, w, a);
        return timing(&m, w, 4, 17, 2);
    }
    case 0x88: case 0x89:  // MOV r/m, reg
        decode_modrm(c, &m);
        set_rm(c, &m, w, get_reg(c, m.reg, w));
        return timing(&m, w, 2, 9, 1);
    case 0x8A: case 0x8B:  // MOV reg, r/m
        decode_modrm(c, &m);
        set_reg(c, m.reg, w, get_rm(c, &m, w));
        return timing(&m, w, 2, 8, 1);
    case 0x8C:  // MOV r/m, segment
        decode_modrm(c, &m);
        set_rm16(c, &m, c->s[m.reg & 3]);
        return timing(&m, true, 2, 9, 1);
    case 0x8D:  // LEA
        decode_modrm(c, &m);
        c->r[m.reg] = m.off;
        return 2 + m.cycles;
    case 0x8E:  // MOV segment, r/m
        decode_modrm(c, &m);
        c->s[m.reg & 3] = get_rm16(c, &m);
        if ((m.reg & 3) == SS) c->no_irq = true;
        return timing(&m, true, 2, 8, 1);
    case 0x8F:  // POP r/m
        decode_modrm(c, &m);
        set_rm16(c, &m, pop(c));
        return timing(&m, true, 12, 25, 1);

    case 0x90: return 3;  // NOP (XCHG AX,AX)
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        uint16_t t = c->r[AX];  // XCHG AX, reg
        c->r[AX] = c->r[op & 7];
        c->r[op & 7] = t;
        return 3;
    }
    case 0x98: c->r[AX] = (uint16_t)(int8_t)get_r8(c, 0); return 2;           // CBW
    case 0x99: c->r[DX] = (c->r[AX] & 0x8000) ? 0xFFFF : 0; return 5;        // CWD
    case 0x9A: {  // CALL far
        uint16_t off = fetch16(c), seg = fetch16(c);
        push(c, c->s[CS]);
        push(c, c->ip);
        c->s[CS] = seg;
        c->ip = off;
        return 36;
    }
    case 0x9B: return 3;  // WAIT (for an 8087 that isn't there)
    case 0x9C: push(c, flags_value(c->flags)); return 14;  // PUSHF
    case 0x9D: c->flags = flags_value(pop(c)); return 12;  // POPF
    case 0x9E:  // SAHF
        c->flags = flags_value((uint16_t)((c->flags & 0xFF00) | (get_r8(c, 4) & (SF | ZF | AF | PF | CF))));
        return 4;
    case 0x9F: set_r8(c, 4, (uint8_t)flags_value(c->flags)); return 4;  // LAHF

    case 0xA0: case 0xA1: {  // MOV AL/AX, [address]
        uint16_t off = fetch16(c);
        if (w) c->r[AX] = rd16(c, data_seg(c), off);
        else set_r8(c, 0, rd8(c, data_seg(c), off));
        return w ? 14 : 10;
    }
    case 0xA2: case 0xA3: {  // MOV [address], AL/AX
        uint16_t off = fetch16(c);
        if (w) wr16(c, data_seg(c), off, c->r[AX]);
        else wr8(c, data_seg(c), off, get_r8(c, 0));
        return w ? 14 : 10;
    }
    case 0xA4: case 0xA5: case 0xA6: case 0xA7:
    case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        return string_op(c, op);
    case 0xA8: alu(c, OP_AND, get_r8(c, 0), fetch8(c), false); return 4;  // TEST AL, imm
    case 0xA9: alu(c, OP_AND, c->r[AX], fetch16(c), true); return 4;      // TEST AX, imm

    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        set_r8(c, op & 7, fetch8(c));  // MOV reg8, imm
        return 4;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        c->r[op & 7] = fetch16(c);  // MOV reg16, imm
        return 4;

    case 0xC0: case 0xC2: {  // RET imm (C0 is an alias)
        uint16_t n = fetch16(c);
        c->ip = pop(c);
        c->r[SP] += n;
        return 24;
    }
    case 0xC1: case 0xC3: c->ip = pop(c); return 20;  // RET
    case 0xC4: case 0xC5:  // LES, LDS
        decode_modrm(c, &m);
        c->r[m.reg] = rd16(c, m.seg, m.off);
        c->s[op == 0xC4 ? ES : DS] = rd16(c, m.seg, (uint16_t)(m.off + 2));
        return 24 + m.cycles;
    case 0xC6: case 0xC7:  // MOV r/m, imm
        decode_modrm(c, &m);
        set_rm(c, &m, w, w ? fetch16(c) : fetch8(c));
        return timing(&m, w, 4, 10, 1);
    case 0xC8: case 0xCA: {  // RETF imm (C8 is an alias)
        uint16_t n = fetch16(c);
        c->ip = pop(c);
        c->s[CS] = pop(c);
        c->r[SP] += n;
        return 33;
    }
    case 0xC9: case 0xCB:  // RETF
        c->ip = pop(c);
        c->s[CS] = pop(c);
        return 34;
    case 0xCC: interrupt(c, 3); return 52;  // INT 3
    case 0xCD: interrupt(c, fetch8(c)); return 51;  // INT n
    case 0xCE:  // INTO
        if (!(c->flags & OF)) return 4;
        interrupt(c, 4);
        return 53;
    case 0xCF:  // IRET
        c->ip = pop(c);
        c->s[CS] = pop(c);
        c->flags = flags_value(pop(c));
        return 32;

    case 0xD0: case 0xD1: case 0xD2: case 0xD3: {  // shifts and rotates, by 1 or CL
        decode_modrm(c, &m);
        int count = op >= 0xD2 ? get_r8(c, 1) : 1;
        set_rm(c, &m, w, shift(c, m.reg, get_rm(c, &m, w), count, w));
        return op >= 0xD2 ? timing(&m, w, 8, 20, 2) + 4 * count : timing(&m, w, 2, 15, 2);
    }
    case 0xD4: {  // AAM
        uint8_t n = fetch8(c), al = get_r8(c, 0);
        if (n == 0) {
            set_szp(c, 0, false);  // as a real 8088 leaves them
            c->flags &= (uint16_t)~(OF | AF | CF);
            interrupt(c, 0);
            return 83;
        }
        set_r8(c, 4, al / n);
        set_r8(c, 0, al % n);
        set_szp(c, get_r8(c, 0), false);
        return 83;
    }
    case 0xD5: {  // AAD
        uint8_t n = fetch8(c);
        uint8_t al = (uint8_t)(get_r8(c, 0) + get_r8(c, 4) * n);
        c->r[AX] = al;
        set_szp(c, al, false);
        return 60;
    }
    case 0xD6: set_r8(c, 0, (c->flags & CF) ? 0xFF : 0); return 4;  // SALC (undocumented)
    case 0xD7:  // XLAT
        set_r8(c, 0, rd8(c, data_seg(c), (uint16_t)(c->r[BX] + get_r8(c, 0))));
        return 11;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        decode_modrm(c, &m);  // ESC: for the 8087, which isn't here
        if (m.mod != 3) rd8(c, m.seg, m.off);
        return 8 + m.cycles;

    case 0xE0: case 0xE1: case 0xE2: {  // LOOPNZ, LOOPZ, LOOP
        uint8_t rel = fetch8(c);
        bool go = --c->r[CX] != 0;
        if (op == 0xE0) go = go && !(c->flags & ZF);
        if (op == 0xE1) go = go && (c->flags & ZF);
        if (!go) return 5;
        jump_short(c, rel);
        return 17;
    }
    case 0xE3: {  // JCXZ
        uint8_t rel = fetch8(c);
        if (c->r[CX] != 0) return 6;
        jump_short(c, rel);
        return 18;
    }
    case 0xE4: set_r8(c, 0, in8(c, fetch8(c))); return 10;  // IN AL, port
    case 0xE5: {  // IN AX, port
        uint8_t port = fetch8(c);
        c->r[AX] = (uint16_t)(in8(c, port) | in8(c, (uint16_t)(port + 1)) << 8);
        return 14;
    }
    case 0xE6: out8(c, fetch8(c), get_r8(c, 0)); return 10;  // OUT port, AL
    case 0xE7: {  // OUT port, AX
        uint8_t port = fetch8(c);
        out8(c, port, (uint8_t)c->r[AX]);
        out8(c, (uint16_t)(port + 1), (uint8_t)(c->r[AX] >> 8));
        return 14;
    }
    case 0xE8: {  // CALL near
        uint16_t rel = fetch16(c);
        push(c, c->ip);
        c->ip = (uint16_t)(c->ip + rel);
        return 23;
    }
    case 0xE9: {  // JMP near
        uint16_t rel = fetch16(c);
        c->ip = (uint16_t)(c->ip + rel);
        return 15;
    }
    case 0xEA: {  // JMP far
        uint16_t off = fetch16(c), seg = fetch16(c);
        c->s[CS] = seg;
        c->ip = off;
        return 15;
    }
    case 0xEB: jump_short(c, fetch8(c)); return 15;  // JMP short
    case 0xEC: set_r8(c, 0, in8(c, c->r[DX])); return 8;  // IN AL, DX
    case 0xED:  // IN AX, DX
        c->r[AX] = (uint16_t)(in8(c, c->r[DX]) | in8(c, (uint16_t)(c->r[DX] + 1)) << 8);
        return 12;
    case 0xEE: out8(c, c->r[DX], get_r8(c, 0)); return 8;  // OUT DX, AL
    case 0xEF:  // OUT DX, AX
        out8(c, c->r[DX], (uint8_t)c->r[AX]);
        out8(c, (uint16_t)(c->r[DX] + 1), (uint8_t)(c->r[AX] >> 8));
        return 12;

    case 0xF4: c->halted = true; return 2;  // HLT
    case 0xF5: c->flags ^= CF; return 2;    // CMC
    case 0xF6: case 0xF7: return group3(c, w);
    case 0xF8: c->flags &= (uint16_t)~CF; return 2;  // CLC
    case 0xF9: c->flags |= CF; return 2;             // STC
    case 0xFA: c->flags &= (uint16_t)~IF; return 2;  // CLI
    case 0xFB:  // STI: interrupts come in after the next instruction
        if (!(c->flags & IF)) c->no_irq = true;
        c->flags |= IF;
        return 2;
    case 0xFC: c->flags &= (uint16_t)~DF; return 2;  // CLD
    case 0xFD: c->flags |= DF; return 2;             // STD
    case 0xFE:  // INC, DEC r/m8 (the rest of the group is undefined: left alone)
        decode_modrm(c, &m);
        if (m.reg < 2) set_rm8(c, &m, (uint8_t)inc_dec(c, get_rm8(c, &m), m.reg == 1, false));
        return timing(&m, false, 3, 15, 0);
    case 0xFF: {
        decode_modrm(c, &m);
        uint16_t v = get_rm16(c, &m);
        switch (m.reg) {
        case 0:
        case 1:  // INC, DEC
            set_rm16(c, &m, inc_dec(c, v, m.reg == 1, true));
            return timing(&m, true, 3, 15, 2);
        case 2:  // CALL near, indirect
            push(c, c->ip);
            c->ip = v;
            return timing(&m, true, 20, 29, 2);
        case 3: {  // CALL far, indirect
            uint16_t seg = rd16(c, m.seg, (uint16_t)(m.off + 2));
            push(c, c->s[CS]);
            push(c, c->ip);
            c->s[CS] = seg;
            c->ip = v;
            return 53 + m.cycles;
        }
        case 4:  // JMP near, indirect
            c->ip = v;
            return timing(&m, true, 11, 18, 1);
        case 5:  // JMP far, indirect
            c->s[CS] = rd16(c, m.seg, (uint16_t)(m.off + 2));
            c->ip = v;
            return 24 + m.cycles;
        default:  // PUSH r/m (7 is an alias); PUSH SP pushes the new value
            if (m.mod == 3 && m.rm == SP) {
                c->r[SP] -= 2;
                wr16(c, SS, c->r[SP], c->r[SP]);
            } else push(c, v);
            return timing(&m, true, 15, 24, 1);
        }
    }
    }
    return 2;
}

void i8088_init(I8088 *cpu, I8088Bus bus)
{
    memset(cpu, 0, sizeof *cpu);
    cpu->bus = bus;
    i8088_reset(cpu);
}

void i8088_reset(I8088 *cpu)
{
    memset(cpu->r, 0, sizeof cpu->r);
    memset(cpu->s, 0, sizeof cpu->s);
    cpu->s[CS] = 0xFFFF;
    cpu->ip = 0;
    cpu->flags = flags_value(0);
    cpu->nmi = cpu->halted = cpu->no_irq = false;
}

int i8088_step(I8088 *c)
{
    if (!c->no_irq) {
        if (c->nmi) {
            c->nmi = false;
            c->halted = false;
            interrupt(c, 2);
            return 50;
        }
        if (c->intr && (c->flags & IF)) {
            c->halted = false;
            interrupt(c, c->bus.inta(c->bus.ctx));
            return 61;
        }
    }
    c->no_irq = false;
    if (c->halted) return 4;

    bool trap = c->flags & TF;
    c->start_ip = c->ip;
    c->seg_prefix = -1;
    c->rep = 0;
    int cycles = 0;
    for (;;) {
        uint8_t op = fetch8(c);
        if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) c->seg_prefix = (op >> 3) & 3;
        else if (op == 0xF2 || op == 0xF3) c->rep = op;
        else if (op != 0xF0 && op != 0xF1) {  // LOCK (F1 an alias) does nothing here
            cycles += execute(c, op);
            break;
        }
        cycles += 2;
    }
    if (trap && !c->no_irq) interrupt(c, 1);
    return cycles;
}

// The disassembler
// ----------------

static const char *const reg8_names[8] = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
static const char *const reg16_names[8] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
static const char *const seg_names[4] = {"ES", "CS", "SS", "DS"};
static const char *const alu_names[8] = {"ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"};
static const char *const shift_names[8] = {"ROL", "ROR", "RCL", "RCR", "SHL", "SHR", "SETMO", "SAR"};
static const char *const group3_names[8] = {"TEST", "TEST", "NOT", "NEG", "MUL", "IMUL", "DIV", "IDIV"};
static const char *const group5_names[8] = {"INC", "DEC", "CALL", "CALL FAR", "JMP", "JMP FAR", "PUSH", "PUSH"};
static const char *const jcc_names[16] = {"JO", "JNO", "JB", "JAE", "JE", "JNE", "JBE", "JA",
                                          "JS", "JNS", "JP", "JNP", "JL", "JGE", "JLE", "JG"};
static const char *const ea_names[8] = {"BX+SI", "BX+DI", "BP+SI", "BP+DI", "SI", "DI", "BP", "BX"};

typedef struct {
    const I8088 *cpu;
    uint16_t seg, off, start;
    int prefix;  // segment override, or -1
} Dis;

static uint8_t dis8(Dis *d)
{
    return d->cpu->bus.read(d->cpu->bus.ctx, linear(d->seg, d->off++));
}

static uint16_t dis16(Dis *d)
{
    uint16_t lo = dis8(d);
    return (uint16_t)(lo | dis8(d) << 8);
}

// The ModRM operand as text; reg gets the reg field. size: 1 byte, 2 word,
// saying so for memory ("BYTE [BX]"); 3 byte, 4 word, when the other operand
// makes that plain.
static void dis_modrm(Dis *d, int size, int *reg, char *out, size_t n)
{
    uint8_t b = dis8(d);
    int mod = b >> 6, rm = b & 7;
    *reg = (b >> 3) & 7;
    if (mod == 3) {
        snprintf(out, n, "%s", size == 1 || size == 3 ? reg8_names[rm] : reg16_names[rm]);
        return;
    }
    char ea[24];
    if (mod == 0 && rm == 6) snprintf(ea, sizeof ea, "%04X", dis16(d));
    else if (mod == 1) {
        int8_t disp = (int8_t)dis8(d);
        snprintf(ea, sizeof ea, "%s%c%X", ea_names[rm], disp < 0 ? '-' : '+', disp < 0 ? -disp : disp);
    } else if (mod == 2) snprintf(ea, sizeof ea, "%s+%04X", ea_names[rm], dis16(d));
    else snprintf(ea, sizeof ea, "%s", ea_names[rm]);
    snprintf(out, n, "%s%s%s[%s]", size == 1 ? "BYTE " : size == 2 ? "WORD " : "",
             d->prefix >= 0 ? seg_names[d->prefix] : "", d->prefix >= 0 ? ":" : "", ea);
}

int i8088_disassemble(const I8088 *cpu, uint16_t seg, uint16_t off, char *out)
{
    Dis d = {cpu, seg, off, off, -1};
    const char *rep = "";
    uint8_t op;
    for (;;) {
        op = dis8(&d);
        if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) d.prefix = (op >> 3) & 3;
        else if (op == 0xF2) rep = "REPNE ";
        else if (op == 0xF3) rep = "REP ";
        else if (op != 0xF0 && op != 0xF1) break;
    }
    bool w = op & 1;
    int reg;
    char rm[40];
    const int n = 64;

    if (op < 0x40 && (op & 7) < 6) {
        const char *name = alu_names[op >> 3];
        switch (op & 7) {
        case 0: case 1:
            dis_modrm(&d, w ? 4 : 3, &reg, rm, sizeof rm);
            snprintf(out, n, "%s %s,%s", name, rm, w ? reg16_names[reg] : reg8_names[reg]);
            break;
        case 2: case 3:
            dis_modrm(&d, w ? 4 : 3, &reg, rm, sizeof rm);
            snprintf(out, n, "%s %s,%s", name, w ? reg16_names[reg] : reg8_names[reg], rm);
            break;
        case 4: snprintf(out, n, "%s AL,%02X", name, dis8(&d)); break;
        default: snprintf(out, n, "%s AX,%04X", name, dis16(&d)); break;
        }
        return (uint16_t)(d.off - d.start);
    }

    switch (op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E: snprintf(out, n, "PUSH %s", seg_names[op >> 3]); break;
    case 0x07: case 0x0F: case 0x17: case 0x1F: snprintf(out, n, "POP %s", seg_names[op >> 3]); break;
    case 0x27: snprintf(out, n, "DAA"); break;
    case 0x2F: snprintf(out, n, "DAS"); break;
    case 0x37: snprintf(out, n, "AAA"); break;
    case 0x3F: snprintf(out, n, "AAS"); break;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        snprintf(out, n, "INC %s", reg16_names[op & 7]);
        break;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        snprintf(out, n, "DEC %s", reg16_names[op & 7]);
        break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        snprintf(out, n, "PUSH %s", reg16_names[op & 7]);
        break;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        snprintf(out, n, "POP %s", reg16_names[op & 7]);
        break;
    case 0x80: case 0x81: case 0x82: case 0x83: {
        dis_modrm(&d, w ? 2 : 1, &reg, rm, sizeof rm);
        if (op == 0x81) snprintf(out, n, "%s %s,%04X", alu_names[reg], rm, dis16(&d));
        else if (op == 0x83) {
            int8_t v = (int8_t)dis8(&d);
            snprintf(out, n, "%s %s,%s%02X", alu_names[reg], rm, v < 0 ? "-" : "", v < 0 ? -v : v);
        } else snprintf(out, n, "%s %s,%02X", alu_names[reg], rm, dis8(&d));
        break;
    }
    case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89:
        dis_modrm(&d, w ? 4 : 3, &reg, rm, sizeof rm);
        snprintf(out, n, "%s %s,%s", op < 0x86 ? "TEST" : op < 0x88 ? "XCHG" : "MOV", rm,
                 w ? reg16_names[reg] : reg8_names[reg]);
        break;
    case 0x8A: case 0x8B:
        dis_modrm(&d, w ? 4 : 3, &reg, rm, sizeof rm);
        snprintf(out, n, "MOV %s,%s", w ? reg16_names[reg] : reg8_names[reg], rm);
        break;
    case 0x8C:
        dis_modrm(&d, 2, &reg, rm, sizeof rm);
        snprintf(out, n, "MOV %s,%s", rm, seg_names[reg & 3]);
        break;
    case 0x8D:
        dis_modrm(&d, 4, &reg, rm, sizeof rm);
        snprintf(out, n, "LEA %s,%s", reg16_names[reg], rm);
        break;
    case 0x8E:
        dis_modrm(&d, 2, &reg, rm, sizeof rm);
        snprintf(out, n, "MOV %s,%s", seg_names[reg & 3], rm);
        break;
    case 0x8F:
        dis_modrm(&d, 2, &reg, rm, sizeof rm);
        snprintf(out, n, "POP %s", rm);
        break;
    case 0x90: snprintf(out, n, "NOP"); break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        snprintf(out, n, "XCHG AX,%s", reg16_names[op & 7]);
        break;
    case 0x98: snprintf(out, n, "CBW"); break;
    case 0x99: snprintf(out, n, "CWD"); break;
    case 0x9A: {
        uint16_t o = dis16(&d), s = dis16(&d);
        snprintf(out, n, "CALL %04X:%04X", s, o);
        break;
    }
    case 0x9B: snprintf(out, n, "WAIT"); break;
    case 0x9C: snprintf(out, n, "PUSHF"); break;
    case 0x9D: snprintf(out, n, "POPF"); break;
    case 0x9E: snprintf(out, n, "SAHF"); break;
    case 0x9F: snprintf(out, n, "LAHF"); break;
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: {
        uint16_t a = dis16(&d);
        char mem[24];
        snprintf(mem, sizeof mem, "%s%s[%04X]", d.prefix >= 0 ? seg_names[d.prefix] : "",
                 d.prefix >= 0 ? ":" : "", a);
        if (op < 0xA2) snprintf(out, n, "MOV %s,%s", w ? "AX" : "AL", mem);
        else snprintf(out, n, "MOV %s,%s", mem, w ? "AX" : "AL");
        break;
    }
    case 0xA4: case 0xA5: snprintf(out, n, "%sMOVS%c", rep, w ? 'W' : 'B'); break;
    case 0xA6: case 0xA7: snprintf(out, n, "%sCMPS%c", rep, w ? 'W' : 'B'); break;
    case 0xA8: snprintf(out, n, "TEST AL,%02X", dis8(&d)); break;
    case 0xA9: snprintf(out, n, "TEST AX,%04X", dis16(&d)); break;
    case 0xAA: case 0xAB: snprintf(out, n, "%sSTOS%c", rep, w ? 'W' : 'B'); break;
    case 0xAC: case 0xAD: snprintf(out, n, "%sLODS%c", rep, w ? 'W' : 'B'); break;
    case 0xAE: case 0xAF: snprintf(out, n, "%sSCAS%c", rep, w ? 'W' : 'B'); break;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        snprintf(out, n, "MOV %s,%02X", reg8_names[op & 7], dis8(&d));
        break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        snprintf(out, n, "MOV %s,%04X", reg16_names[op & 7], dis16(&d));
        break;
    case 0xC0: case 0xC2: snprintf(out, n, "RET %04X", dis16(&d)); break;
    case 0xC1: case 0xC3: snprintf(out, n, "RET"); break;
    case 0xC4: case 0xC5:
        dis_modrm(&d, 4, &reg, rm, sizeof rm);
        snprintf(out, n, "%s %s,%s", op == 0xC4 ? "LES" : "LDS", reg16_names[reg], rm);
        break;
    case 0xC6:
        dis_modrm(&d, 1, &reg, rm, sizeof rm);
        snprintf(out, n, "MOV %s,%02X", rm, dis8(&d));
        break;
    case 0xC7:
        dis_modrm(&d, 2, &reg, rm, sizeof rm);
        snprintf(out, n, "MOV %s,%04X", rm, dis16(&d));
        break;
    case 0xC8: case 0xCA: snprintf(out, n, "RETF %04X", dis16(&d)); break;
    case 0xC9: case 0xCB: snprintf(out, n, "RETF"); break;
    case 0xCC: snprintf(out, n, "INT 3"); break;
    case 0xCD: snprintf(out, n, "INT %02X", dis8(&d)); break;
    case 0xCE: snprintf(out, n, "INTO"); break;
    case 0xCF: snprintf(out, n, "IRET"); break;
    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
        dis_modrm(&d, w ? 2 : 1, &reg, rm, sizeof rm);
        snprintf(out, n, "%s %s,%s", shift_names[reg], rm, op >= 0xD2 ? "CL" : "1");
        break;
    case 0xD4: snprintf(out, n, "AAM %02X", dis8(&d)); break;
    case 0xD5: snprintf(out, n, "AAD %02X", dis8(&d)); break;
    case 0xD6: snprintf(out, n, "SALC"); break;
    case 0xD7: snprintf(out, n, "XLAT"); break;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        dis_modrm(&d, 4, &reg, rm, sizeof rm);
        snprintf(out, n, "ESC %s", rm);
        break;
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xEB: {
        static const char *const names[] = {"LOOPNZ", "LOOPZ", "LOOP", "JCXZ"};
        int8_t rel = (int8_t)dis8(&d);
        snprintf(out, n, "%s %04X", op == 0xEB ? "JMP" : names[op & 3], (uint16_t)(d.off + rel));
        break;
    }
    case 0xE4: snprintf(out, n, "IN AL,%02X", dis8(&d)); break;
    case 0xE5: snprintf(out, n, "IN AX,%02X", dis8(&d)); break;
    case 0xE6: snprintf(out, n, "OUT %02X,AL", dis8(&d)); break;
    case 0xE7: snprintf(out, n, "OUT %02X,AX", dis8(&d)); break;
    case 0xE8: case 0xE9: {
        uint16_t rel = dis16(&d);
        snprintf(out, n, "%s %04X", op == 0xE8 ? "CALL" : "JMP", (uint16_t)(d.off + rel));
        break;
    }
    case 0xEA: {
        uint16_t o = dis16(&d), s = dis16(&d);
        snprintf(out, n, "JMP %04X:%04X", s, o);
        break;
    }
    case 0xEC: snprintf(out, n, "IN AL,DX"); break;
    case 0xED: snprintf(out, n, "IN AX,DX"); break;
    case 0xEE: snprintf(out, n, "OUT DX,AL"); break;
    case 0xEF: snprintf(out, n, "OUT DX,AX"); break;
    case 0xF4: snprintf(out, n, "HLT"); break;
    case 0xF5: snprintf(out, n, "CMC"); break;
    case 0xF6: case 0xF7:
        dis_modrm(&d, w ? 2 : 1, &reg, rm, sizeof rm);
        if (reg < 2) {
            if (w) snprintf(out, n, "TEST %s,%04X", rm, dis16(&d));
            else snprintf(out, n, "TEST %s,%02X", rm, dis8(&d));
        } else snprintf(out, n, "%s %s", group3_names[reg], rm);
        break;
    case 0xF8: snprintf(out, n, "CLC"); break;
    case 0xF9: snprintf(out, n, "STC"); break;
    case 0xFA: snprintf(out, n, "CLI"); break;
    case 0xFB: snprintf(out, n, "STI"); break;
    case 0xFC: snprintf(out, n, "CLD"); break;
    case 0xFD: snprintf(out, n, "STD"); break;
    case 0xFE:
        dis_modrm(&d, 1, &reg, rm, sizeof rm);
        snprintf(out, n, "%s %s", reg == 0 ? "INC" : reg == 1 ? "DEC" : "???", rm);
        break;
    case 0xFF:
        dis_modrm(&d, 2, &reg, rm, sizeof rm);
        snprintf(out, n, "%s %s", group5_names[reg], rm);
        break;
    default:
        if (op >= 0x60 && op <= 0x7F) {
            int8_t rel = (int8_t)dis8(&d);
            snprintf(out, n, "%s %04X", jcc_names[op & 15], (uint16_t)(d.off + rel));
        } else snprintf(out, n, "DB %02X", op);
        break;
    }
    return (uint16_t)(d.off - d.start);
}
