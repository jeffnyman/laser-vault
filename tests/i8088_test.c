// i8088-test: run the SingleStepTests 8088 tests on the 8088 emulator.
//
// Usage: i8088-test path/to/v2_binary [opcode...]
//
// Daniel Balsom recorded a real 8088 running 10,000 tests per opcode (fewer
// for the slow ones): random register values, random memory, one
// instruction, and the registers and memory afterwards. Opcodes whose
// result depends on the ModRM byte's reg field (80-83, D0-D3, F6, F7, FE,
// FF) have a file for each. The tests are MIT-licensed and not included
// here; get the v2_binary folder (the .MOO.gz files) from
// https://github.com/SingleStepTests/8088
//
// Each test is checked for the registers and memory it leaves; the flags
// Intel leaves undefined for an instruction aren't compared, nor the cycle
// counts. Give opcodes in hex ("D4 F7.6") to run just those. Undefined
// instructions (FE with reg 2-7) are skipped.

#include "cpu/i8088.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint8_t memory[1 << 20];

static uint8_t mem_read(void *ctx, uint32_t addr) { (void)ctx; return memory[addr & 0xFFFFF]; }
static void mem_write(void *ctx, uint32_t addr, uint8_t v) { (void)ctx; memory[addr & 0xFFFFF] = v; }
static uint8_t io_in(void *ctx, uint16_t port) { (void)ctx; (void)port; return 0xFF; }
static void io_out(void *ctx, uint16_t port, uint8_t v) { (void)ctx; (void)port; (void)v; }
static uint8_t inta(void *ctx) { (void)ctx; return 8; }

// The order registers are stored in the tests.
enum { R_AX, R_BX, R_CX, R_DX, R_CS, R_SS, R_DS, R_ES, R_SP, R_BP, R_SI, R_DI, R_IP, R_FLAGS, R_COUNT };
static const char *const reg_names[R_COUNT] = {"ax", "bx", "cx", "dx", "cs", "ss", "ds",
                                               "es", "sp", "bp", "si", "di", "ip", "flags"};

static uint16_t *reg_ptr(I8088 *c, int n)
{
    switch (n) {
    case R_AX: return &c->r[I8088_AX];
    case R_BX: return &c->r[I8088_BX];
    case R_CX: return &c->r[I8088_CX];
    case R_DX: return &c->r[I8088_DX];
    case R_CS: return &c->s[I8088_CS];
    case R_SS: return &c->s[I8088_SS];
    case R_DS: return &c->s[I8088_DS];
    case R_ES: return &c->s[I8088_ES];
    case R_SP: return &c->r[I8088_SP];
    case R_BP: return &c->r[I8088_BP];
    case R_SI: return &c->r[I8088_SI];
    case R_DI: return &c->r[I8088_DI];
    case R_IP: return &c->ip;
    default: return &c->flags;
    }
}

// The flags each instruction leaves undefined (Intel's documentation, as
// the tests' metadata records them). reg is the ModRM reg field, or -1.
static uint16_t undefined_flags(int op, int reg)
{
    const uint16_t A = I8088_AF, O = I8088_OF, S = I8088_SF, Z = I8088_ZF, P = I8088_PF, C = I8088_CF;
    if ((op >= 0x08 && op <= 0x0D) || (op >= 0x20 && op <= 0x25) || (op >= 0x30 && op <= 0x35)) return A;
    switch (op) {
    case 0x27: case 0x2F: return O;
    case 0x37: case 0x3F: return O | S | Z | P;
    case 0x84: case 0x85: case 0xA8: case 0xA9: return A;
    case 0x80: case 0x81: case 0x82: case 0x83: return reg == 1 || reg == 4 || reg == 6 ? A : 0;
    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
        return reg == 6 ? (uint16_t)(O | S | Z | A | P | C) : reg >= 4 ? A : 0;
    case 0xD4: case 0xD5: return O | A | C;
    case 0xF6: case 0xF7:
        if (reg <= 1) return A;
        if (reg == 4 || reg == 5) return S | Z | A | P;
        if (reg >= 6) return O | S | Z | A | P | C;
        return 0;
    }
    return 0;
}

static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// Read a whole .gz file. Returns NULL if it can't be opened.
static uint8_t *read_gz(const char *path, size_t *size)
{
    gzFile f = gzopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 24, n = 0;
    uint8_t *buf = malloc(cap);
    for (;;) {
        if (n == cap) buf = realloc(buf, cap *= 2);
        int got = gzread(f, buf + n, (unsigned)(cap - n));
        if (got <= 0) break;
        n += (size_t)got;
    }
    gzclose(f);
    *size = n;
    return buf;
}

typedef struct {
    uint16_t mask;  // which registers are given
    uint16_t regs[R_COUNT];
    const uint8_t *ram;  // entries of 4-byte address, value
    uint32_t ram_count;
} State;

static void parse_state(const uint8_t *p, uint32_t len, State *s)
{
    memset(s, 0, sizeof *s);
    for (uint32_t at = 0; at + 8 <= len;) {
        const uint8_t *type = p + at;
        uint32_t clen = u32(p + at + 4);
        const uint8_t *d = p + at + 8;
        if (!memcmp(type, "REGS", 4)) {
            s->mask = (uint16_t)(d[0] | d[1] << 8);
            int k = 0;
            for (int r = 0; r < R_COUNT; r++)
                if (s->mask & (1 << r)) {
                    s->regs[r] = (uint16_t)(d[2 + 2 * k] | d[3 + 2 * k] << 8);
                    k++;
                }
        } else if (!memcmp(type, "RAM ", 4)) {
            s->ram_count = u32(d);
            s->ram = d + 4;
        }
        at += 8 + clen;
    }
}

static int failures_shown;

// Run one test. Returns true if it passed.
static bool run_test(const uint8_t *p, uint32_t len, int op, int reg)
{
    char name[128] = "";
    State init = {0}, fin = {0};
    for (uint32_t at = 4; at + 8 <= len;) {
        const uint8_t *type = p + at;
        uint32_t clen = u32(p + at + 4);
        const uint8_t *d = p + at + 8;
        if (!memcmp(type, "NAME", 4)) {
            uint32_t n = u32(d);
            if (n >= sizeof name) n = sizeof name - 1;
            memcpy(name, d + 4, n);
            name[n] = '\0';
        } else if (!memcmp(type, "INIT", 4)) parse_state(d, clen, &init);
        else if (!memcmp(type, "FINA", 4)) parse_state(d, clen, &fin);
        at += 8 + clen;
    }

    I8088 cpu;
    i8088_init(&cpu, (I8088Bus){NULL, mem_read, mem_write, io_in, io_out, inta});
    for (int r = 0; r < R_COUNT; r++) *reg_ptr(&cpu, r) = init.regs[r];
    for (uint32_t i = 0; i < init.ram_count; i++) memory[u32(init.ram + 5 * i) & 0xFFFFF] = init.ram[5 * i + 4];
    i8088_step(&cpu);

    uint16_t ignore = undefined_flags(op, reg);
    bool ok = true;
    char why[256] = "";
    for (int r = 0; r < R_COUNT && ok; r++) {
        uint16_t want = (fin.mask & (1 << r)) ? fin.regs[r] : init.regs[r];
        uint16_t got = *reg_ptr(&cpu, r);
        if (r == R_FLAGS) {
            want = (uint16_t)(want & ~ignore);
            got = (uint16_t)(got & ~ignore);
        }
        if (want != got) {
            ok = false;
            snprintf(why, sizeof why, "%s is %04X, should be %04X", reg_names[r], got, want);
        }
    }
    // A division that fails is interrupt 0, which pushes the flags, and
    // those include the undefined ones.
    uint32_t flags_at = 0xFFFFFFFF;
    if ((op == 0xF6 || op == 0xF7) && reg >= 6) {
        uint16_t ss = (fin.mask & (1 << R_SS)) ? fin.regs[R_SS] : init.regs[R_SS];
        uint16_t sp = (fin.mask & (1 << R_SP)) ? fin.regs[R_SP] : init.regs[R_SP];
        flags_at = ((uint32_t)ss * 16 + (uint16_t)(sp + 4)) & 0xFFFFF;
    }
    for (uint32_t i = 0; i < fin.ram_count && ok; i++) {
        uint32_t addr = u32(fin.ram + 5 * i) & 0xFFFFF;
        uint8_t mask = addr == flags_at ? (uint8_t)~ignore
                     : addr == ((flags_at + 1) & 0xFFFFF) ? (uint8_t)(~ignore >> 8) : 0xFF;
        if ((memory[addr] ^ fin.ram[5 * i + 4]) & mask) {
            ok = false;
            snprintf(why, sizeof why, "memory %05X is %02X, should be %02X", addr, memory[addr], fin.ram[5 * i + 4]);
        }
    }
    if (!ok && failures_shown++ < 3) {
        char flags_note[64] = "";
        snprintf(flags_note, sizeof flags_note, " (flags were %04X)", init.regs[R_FLAGS]);
        printf("    %s: %s%s\n", name, why, flags_note);
    }
    return ok;
}

// Run a file's tests. Returns -1 if it isn't there, else the failures.
static long run_file(const char *dir, int op, int reg)
{
    char path[1024];
    if (reg < 0) snprintf(path, sizeof path, "%s/%02X.MOO.gz", dir, op);
    else snprintf(path, sizeof path, "%s/%02X.%d.MOO.gz", dir, op, reg);
    size_t size;
    uint8_t *buf = read_gz(path, &size);
    if (!buf) return -1;
    long tests = 0, failed = 0;
    failures_shown = 0;
    char label[16];
    if (reg < 0) snprintf(label, sizeof label, "%02X", op);
    else snprintf(label, sizeof label, "%02X.%d", op, reg);
    for (size_t at = 0; at + 8 <= size;) {
        uint32_t len = u32(buf + at + 4);
        if (!memcmp(buf + at, "TEST", 4)) {
            tests++;
            if (!run_test(buf + at + 8, len, op, reg)) failed++;
        }
        at += 8 + len;
    }
    free(buf);
    if (failed) printf("%-5s %ld of %ld failed\n", label, failed, tests);
    return failed;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: i8088-test path/to/v2_binary [opcode...]\n");
        return 2;
    }
    const char *dir = argv[1];
    int files = 0, bad_files = 0;
    long failed = 0;
    for (int op = 0; op < 256; op++) {
        for (int reg = -1; reg < 8; reg++) {
            if (op == 0xFE && reg >= 2) continue;  // undefined
            if (argc > 2) {
                char a[16], b[16];
                snprintf(a, sizeof a, "%02X", op);
                snprintf(b, sizeof b, "%02X.%d", op, reg);
                bool wanted = false;
                for (int i = 2; i < argc; i++)
                    if ((reg < 0 && !strcmp(argv[i], a)) || (reg >= 0 && (!strcmp(argv[i], b) || !strcmp(argv[i], a))))
                        wanted = true;
                if (!wanted) continue;
            }
            long f = run_file(dir, op, reg);
            if (f < 0) continue;
            files++;
            if (f) bad_files++;
            failed += f;
        }
    }
    if (files == 0) {
        fprintf(stderr, "no tests found in %s (expected files like 00.MOO.gz)\n", dir);
        return 2;
    }
    if (failed) {
        printf("FAILED: %ld tests, in %d of %d files\n", failed, bad_files, files);
        return 1;
    }
    printf("passed: all %d files\n", files);
    return 0;
}
