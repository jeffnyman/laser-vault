// m6502-test: run Klaus Dormann's 6502 functional test.
//
// Usage: m6502-test path/to/6502_functional_test.bin
//
// The test runs every documented 6502 instruction in every addressing
// mode, checking the results and flags, decimal mode included. It's a
// 64 KB memory image, loaded at 0000 and started at 0400. When a check
// fails, the test stops in a loop on itself ("JMP *" or a branch to
// itself); when everything passes, it does the same at address 3469. A run
// takes about 96 million clock cycles.
//
// The test is GPL-licensed and not included here. It's available from
// https://github.com/Klaus2m5/6502_65C02_functional_tests
// (bin_files/6502_functional_test.bin; the .lst file beside it shows the
// test at any address it stops at).

#include "cpu/m6502.h"

#include <stdio.h>

#define START 0x0400
#define SUCCESS 0x3469

static uint8_t memory[65536];

static uint8_t mem_read(void *ctx, uint16_t addr) { (void)ctx; return memory[addr]; }
static void mem_write(void *ctx, uint16_t addr, uint8_t v) { (void)ctx; memory[addr] = v; }

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: m6502-test path/to/6502_functional_test.bin\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    size_t n = fread(memory, 1, sizeof memory, f);
    fclose(f);
    if (n != sizeof memory) {
        fprintf(stderr, "%s is %zu bytes; expected a 64 KB memory image\n", argv[1], n);
        return 2;
    }

    M6502 cpu;
    m6502_init(&cpu, (M6502Bus){.ctx = NULL, .read = mem_read, .write = mem_write});
    cpu.pc = START;
    unsigned long long cycles = 0;
    for (;;) {
        uint16_t pc = cpu.pc;
        cycles += (unsigned)m6502_step(&cpu);
        if (cpu.pc != pc) continue;
        // Stuck on one instruction: the test has finished, one way or the other.
        char text[32];
        m6502_disassemble(&cpu, pc, text);
        if (pc == SUCCESS) {
            printf("passed, after %llu cycles\n", cycles);
            return 0;
        }
        printf("FAILED at %04X (%s), after %llu cycles: A=%02X X=%02X Y=%02X P=%02X S=%02X\n", pc, text, cycles,
               cpu.a, cpu.x, cpu.y, cpu.p, cpu.s);
        return 1;
    }
}
