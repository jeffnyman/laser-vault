// m6502.h - a MOS Technology 6502 CPU emulator.
//
// The 6502 (1975) is the 8-bit CPU of the Apple II, the Commodore 64, the
// NES and countless arcade boards; Data East's laserdisc board has two, one
// for the game and one for its sound. It's a lean design: an accumulator A,
// two index registers X and Y, an 8-bit stack pointer (the stack is page
// 1, 0100-01FF), and a status register P. What it lacks in registers it
// makes up in addressing modes, the cheapest of which reach the first 256
// bytes of memory, the "zero page", which programs use as extra registers.
//
// Like the other CPU emulators, this runs one instruction at a time and
// reports how many clock cycles it took. Devices are in the memory map,
// so the board supplies just two functions, reading and writing memory.
//
// It covers the documented instruction set, with the NMOS 6502's decimal
// mode (for BCD arithmetic); the undocumented opcodes act as NOPs of the
// right length.

#ifndef LV_M6502_H
#define LV_M6502_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *ctx;  // passed back to every function, e.g. the board
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
} M6502Bus;

// Bits in the P (status) register.
enum {
    M6502_C = 0x01,  // carry
    M6502_Z = 0x02,  // zero
    M6502_I = 0x04,  // IRQ disabled
    M6502_D = 0x08,  // decimal mode
    M6502_B = 0x10,  // (only in the copy pushed by BRK or PHP)
    M6502_U = 0x20,  // unused, always 1
    M6502_V = 0x40,  // overflow
    M6502_N = 0x80,  // negative
};

typedef struct {
    uint8_t a, x, y, s, p;
    uint16_t pc;

    // Interrupt inputs, set by the board.
    bool nmi;        // NMI requested (edge); cleared when taken
    bool irq;        // IRQ line held active
    bool irq_taken;  // set when the CPU takes an IRQ, so a board that
                     // holds the line until then can let go

    M6502Bus bus;
} M6502;

// Set up a CPU connected to the given bus, then reset it.
void m6502_init(M6502 *cpu, M6502Bus bus);

// Reset: interrupts disabled, PC from the reset vector at FFFC.
void m6502_reset(M6502 *cpu);

// Run one instruction (or respond to an interrupt). Returns cycles used.
int m6502_step(M6502 *cpu);

// Write one instruction at addr as text into out (at least 32 bytes), as
// "LDA $1234,X" and the like. Returns its length in bytes. Reads through
// the CPU's bus, so mind reads that have side effects.
int m6502_disassemble(const M6502 *cpu, uint16_t addr, char *out);

#endif
