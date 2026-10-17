// i8088.h - an Intel 8088 CPU emulator.
//
// The 8088 (1979) is the CPU of the IBM PC: Intel's 16-bit 8086 with an
// 8-bit bus. Leland's board for Dragon's Lair II has one, running at 10 MHz,
// and is built much like a PC.
//
// The 8086 family has eight 16-bit registers: AX, BX, CX and DX can also be
// used as two bytes each (AL and AH, and so on); SP is the stack pointer,
// and BP, SI and DI mostly hold addresses. Memory is a megabyte, reached
// in 64 KB windows called segments: an address is a segment register
// times 16 plus an offset. CS holds the segment the program runs in, SS the
// stack's, DS the data's and ES an extra one. Ports (for devices) are a
// separate space of 64K, reached with IN and OUT.
//
// Like the other CPU emulators, this runs one instruction at a time and
// reports how many clock cycles it took (from Intel's tables, roughly: the
// 8088 fetches instructions ahead of time, which makes exact counts depend
// on what came before). The board supplies memory, ports and an interrupt
// controller.
//
// It covers the 8086's documented instruction set, with the 8086's own ways
// where later CPUs differ (PUSH SP pushes the new value, shift counts
// aren't limited to 31, POP CS works), and the undocumented aliases (60-6F
// for the conditional jumps, C0 and C1 for RET, and so on). There's no
// 8087 floating-point chip: its instructions read their operand and do
// nothing else.

#ifndef LV_I8088_H
#define LV_I8088_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *ctx;  // passed back to every function, e.g. the board
    uint8_t (*read)(void *ctx, uint32_t addr);  // addr is 20 bits
    void (*write)(void *ctx, uint32_t addr, uint8_t value);
    uint8_t (*in)(void *ctx, uint16_t port);
    void (*out)(void *ctx, uint16_t port, uint8_t value);
    // Interrupt acknowledge: the interrupt controller says which interrupt
    // (0-255) the request on the INTR line is for.
    uint8_t (*inta)(void *ctx);
} I8088Bus;

// The general registers, in the order instructions number them.
enum { I8088_AX, I8088_CX, I8088_DX, I8088_BX, I8088_SP, I8088_BP, I8088_SI, I8088_DI };

// The segment registers.
enum { I8088_ES, I8088_CS, I8088_SS, I8088_DS };

// Bits in the flags register.
enum {
    I8088_CF = 0x0001,  // carry
    I8088_PF = 0x0004,  // parity: the low byte of the result has an even number of 1s
    I8088_AF = 0x0010,  // carry out of the low four bits (for BCD)
    I8088_ZF = 0x0040,  // zero
    I8088_SF = 0x0080,  // sign
    I8088_TF = 0x0100,  // trap: interrupt 1 after each instruction
    I8088_IF = 0x0200,  // interrupts enabled
    I8088_DF = 0x0400,  // string instructions go down
    I8088_OF = 0x0800,  // overflow
};

typedef struct {
    uint16_t r[8];  // AX, CX, DX, BX, SP, BP, SI, DI
    uint16_t s[4];  // ES, CS, SS, DS
    uint16_t ip, flags;

    // Interrupt inputs, set by the board.
    bool intr;  // INTR line held active (taken when IF is set)
    bool nmi;   // NMI requested (edge); cleared when taken
    bool halted;  // after HLT, until an interrupt

    I8088Bus bus;

    // While an instruction runs.
    int seg_prefix;  // segment override (-1 for none)
    int rep;         // 0, or the repeat prefix (F2 or F3)
    uint16_t start_ip;  // where the instruction began, prefixes included
    bool no_irq;     // hold off interrupts until after the next instruction
} I8088;

// Set up a CPU connected to the given bus, then reset it.
void i8088_init(I8088 *cpu, I8088Bus bus);

// Reset: CS FFFF, IP 0 (so it starts 16 bytes from the top of memory),
// interrupts disabled.
void i8088_reset(I8088 *cpu);

// Run one instruction (or respond to an interrupt). Returns cycles used.
int i8088_step(I8088 *cpu);

// Write the instruction at seg:off as text into out (at least 64 bytes), as
// "MOV AX,[BX+SI+12]" and the like. Returns its length in bytes. Reads
// through the CPU's bus.
int i8088_disassemble(const I8088 *cpu, uint16_t seg, uint16_t off, char *out);

#endif
