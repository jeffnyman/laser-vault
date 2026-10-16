// m6809.h - a Motorola 6809 CPU emulator.
//
// The 6809 (1978) is an 8-bit CPU with some 16-bit muscle: two accumulators
// that pair up as a 16-bit one, two index registers, two stack pointers and
// addressing modes rich enough to make it a favourite of arcade makers in
// the early 1980s (Williams and Konami among them). Konami's Badlands runs
// on one at 1.536 MHz. Like the Z80 emulator, this runs one instruction at
// a time and reports how many clock cycles it took.
//
// Unlike the Z80 there is no separate I/O space: devices sit in the memory
// map, so the board supplies just two functions, reading and writing memory.
//
// There are three interrupt inputs: NMI (non-maskable, taken on an edge),
// IRQ and FIRQ ("fast" IRQ, which saves only PC and CC). The last two are
// levels: an interrupt is taken while the line is held and the CC register
// doesn't mask it.

#ifndef LV_M6809_H
#define LV_M6809_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *ctx;  // passed back to every function, e.g. the board
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
} M6809Bus;

// Bits in the CC (condition code) register.
enum {
    M6809_CC_C = 0x01,  // carry (or borrow)
    M6809_CC_V = 0x02,  // overflow
    M6809_CC_Z = 0x04,  // zero
    M6809_CC_N = 0x08,  // negative (bit 7, or bit 15, of the result)
    M6809_CC_I = 0x10,  // IRQ masked
    M6809_CC_H = 0x20,  // half carry (carry out of bit 3, for DAA)
    M6809_CC_F = 0x40,  // FIRQ masked
    M6809_CC_E = 0x80,  // "entire": all registers were saved on the stack
};

// What the CPU is waiting for, if anything.
typedef enum {
    M6809_RUNNING,
    M6809_SYNC,  // SYNC: idle until any interrupt line is active
    M6809_CWAI,  // CWAI: state saved on the stack; idle until an interrupt
} M6809Wait;

typedef struct {
    uint8_t a, b;     // accumulators; together they are D (A the high byte)
    uint8_t dp;       // direct page: the high byte of a direct address
    uint8_t cc;       // condition codes
    uint16_t x, y;    // index registers
    uint16_t u, s;    // user and system stack pointers
    uint16_t pc;

    M6809Wait wait;
    bool nmi_armed;   // NMI is ignored after reset until S is first loaded

    // Interrupt inputs, set by the board.
    bool nmi;         // NMI requested (edge); cleared when taken
    bool irq, firq;   // lines held active
    bool irq_taken, firq_taken;  // set when the CPU takes one, so a board
                                 // that holds the line until then can let go

    M6809Bus bus;
} M6809;

// Set up a CPU connected to the given bus, then reset it.
void m6809_init(M6809 *cpu, M6809Bus bus);

// Reset: interrupts masked, PC from the reset vector at FFFE.
void m6809_reset(M6809 *cpu);

// Run one instruction (or respond to an interrupt). Returns cycles used.
int m6809_step(M6809 *cpu);

// Write one instruction at addr as text into out (at least 32 bytes), as
// "LDA #$10" and the like. Returns its length in bytes. Reads through the
// CPU's bus, so mind reads that have side effects.
int m6809_disassemble(const M6809 *cpu, uint16_t addr, char *out);

#endif
