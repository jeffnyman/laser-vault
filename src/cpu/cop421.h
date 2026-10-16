// cop421.h - a National Semiconductor COP421 microcontroller emulator.
//
// The COP400 family are tiny 4-bit microcontrollers, with their program
// and data inside the chip: the COP421 has 1 KB of ROM and 64 four-bit
// words of RAM. Thayer's Quest has one to read its keyboard and pass the
// keys to the main Z80.
//
// It's an odd machine next to the Z80. The accumulator A, the carry C and
// every RAM word are 4 bits. RAM is addressed only through the B register
// (Br, the row, in bits 4-5; Bd, the column, in bits 0-3), and most
// instructions that touch RAM also step B or flip bits of Br, so walking
// through RAM takes no extra instructions. Many instructions "skip": they
// make the next instruction do nothing. The program counter is 10 bits,
// and there is a three-level return stack (SA, SB, SC).
//
// To the outside it has ports, not a bus: L (8 bits, in or out), G (4
// bits), D (4 bits out), and a serial line (SI in, SO and SK out) with a
// 4-bit shift register, SIO. The board supplies a function for each.
// There's also a timer that a program tests with SKT.
//
// Each instruction is one or two bytes, and takes one instruction cycle
// per byte.

#ifndef LV_COP421_H
#define LV_COP421_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *ctx;  // passed back to every function, e.g. the board
    uint8_t (*read_l)(void *ctx);
    void (*write_l)(void *ctx, uint8_t value);  // while EN bit 2 drives L
    uint8_t (*read_g)(void *ctx);
    void (*write_g)(void *ctx, uint8_t value);
    void (*write_d)(void *ctx, uint8_t value);
    int (*read_si)(void *ctx);
    void (*write_so)(void *ctx, int level);
} Cop421Bus;

typedef struct {
    uint8_t rom[1024];
    uint8_t ram[64];   // 4 bits each

    uint16_t pc;       // 10 bits
    uint16_t sa, sb, sc;  // return stack: SA is the top
    uint8_t a;         // accumulator, 4 bits
    uint8_t b;         // RAM address: Br (bits 4-5) and Bd (bits 0-3)
    uint8_t c;         // carry
    uint8_t en;        // bit 0: SIO a counter (1) or shift register (0);
                       // bit 2: Q drives the L port; bit 3: SO from SIO
    uint8_t g, d, q;   // output latches: G, D and Q (which drives L)
    uint8_t sio;       // serial shift register (or counter), 4 bits
    uint8_t skl;       // SK output latch
    uint8_t si_history;  // SI's last few levels, for counter mode
    int so;            // level on SO

    uint8_t t;         // timer: counts instruction cycles / 4
    uint8_t t_prescale;
    bool skt_latch;    // set when the timer wraps; SKT tests and clears it

    bool skip;         // the next instruction is skipped
    bool lbi_run;      // the last instruction was an LBI (so another is skipped)

    Cop421Bus bus;
} Cop421;

// Set up a chip connected to the given ports, with its program, and reset it.
void cop421_init(Cop421 *cpu, Cop421Bus bus, const uint8_t rom[1024]);
void cop421_reset(Cop421 *cpu);

// Run one instruction. Returns the instruction cycles it took (1 or 2).
int cop421_step(Cop421 *cpu);

#endif
