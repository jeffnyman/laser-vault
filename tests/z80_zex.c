// z80-zex: run the ZEXDOC / ZEXALL Z80 instruction exercisers.
//
// Usage: z80-zex path/to/zexdoc.com
//
// ZEXDOC and ZEXALL, by Frank D. Cringle, run every Z80 instruction over a
// large set of inputs and compare a checksum of the results with one
// recorded on a real Z80. ZEXDOC checks the documented flags; ZEXALL also
// checks the undocumented ones. Each test prints "OK" or "ERROR". A full
// run takes several billion clock cycles.
//
// They're CP/M programs, so this pretends to be just enough of CP/M:
//   - the program is loaded at 0x0100, where CP/M programs start;
//   - address 0x0006 holds the top of usable memory (the program sets its
//     stack from it);
//   - a CALL 5 is a request to the operating system: C=2 prints the
//     character in E, C=9 prints the '$'-terminated string at DE;
//   - jumping to 0x0000 means the program has finished.
//
// The programs are GPL-licensed and not included here. They're available
// from https://github.com/agn453/ZEXALL (zexdoc.com, zexall.com).

#include "cpu/z80.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint8_t memory[65536];

static uint8_t mem_read(void *ctx, uint16_t addr) { (void)ctx; return memory[addr]; }
static void mem_write(void *ctx, uint16_t addr, uint8_t v) { (void)ctx; memory[addr] = v; }
static uint8_t io_in(void *ctx, uint16_t port) { (void)ctx; (void)port; return 0xFF; }
static void io_out(void *ctx, uint16_t port, uint8_t v) { (void)ctx; (void)port; (void)v; }

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: z80-zex path/to/zexdoc.com\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    size_t size = fread(memory + 0x100, 1, sizeof memory - 0x100, f);
    fclose(f);
    printf("Loaded %zu bytes\n", size);

    memory[0x0005] = 0xC9;  // RET, so a CALL 5 returns after we handle it
    memory[0x0006] = 0x00;  // top of memory: 0xF000 (little-endian)
    memory[0x0007] = 0xF0;

    Z80 cpu;
    z80_init(&cpu, (Z80Bus){.read = mem_read, .write = mem_write, .in = io_in, .out = io_out});
    cpu.pc = 0x0100;

    unsigned long long cycles = 0;
    time_t start = time(NULL);
    for (;;) {
        if (cpu.pc == 0x0005) {  // CP/M system call
            if (cpu.c == 2) {
                putchar(cpu.e);
            } else if (cpu.c == 9) {
                for (uint16_t a = z80_de(&cpu); memory[a] != '$'; a++) putchar(memory[a]);
            }
            fflush(stdout);
        } else if (cpu.pc == 0x0000) {  // program finished
            break;
        }
        cycles += (unsigned long long)z80_step(&cpu);
    }

    printf("\n%llu T-states in %lld s\n", cycles, (long long)(time(NULL) - start));
    return 0;
}
