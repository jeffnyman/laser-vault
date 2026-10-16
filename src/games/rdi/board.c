#include "games/rdi/board.h"
#include "util/roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
//
// Two sets differ in U1, the plug-in card; a third, made by fans, has a U33
// that lets the narrator say the words the original won't. (Names and
// checksums from MAME's thayers.cpp, and the Dragon's Lair Project's notes.)

typedef struct {
    const char *names[2];
    size_t size;
    uint32_t crc;
} RomChip;

static const RomChip u33_chips[2] = {
    {{"tq_u33.bin", NULL}, 0x8000, 0x82DF5D89},
    {{"tq_u33_mod.bin", NULL}, 0x8000, 0x7BE981DF},  // the fans' version
};
static const RomChip u1_chips[2] = {
    {{"tq_u1.bin", NULL}, 0x2000, 0xE8E7F566},
    {{"thayers.u1", "tq_u1.bin"}, 0x2000, 0x33817E25},  // set 2
};
static const RomChip cop_chip = {{"tq_cop.bin", NULL}, 0x400, 0x6748E6B3};

static const char *chip_name(const char *path, const RomChip *chip)
{
    for (int i = 0; i < 2; i++)
        if (chip->names[i] && roms_has(path, chip->names[i])) return chip->names[i];
    return NULL;
}

bool rdi_recognizes(const char *path)
{
    return chip_name(path, &u33_chips[0]) || chip_name(path, &u33_chips[1]) || chip_name(path, &u1_chips[1]);
}

// The set's parent zip, tq.zip in the same folder, for sets that hold only
// the chips they change.
static void parent_path(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
    if (backslash > slash) slash = backslash;
    int dir = slash ? (int)(slash - path + 1) : 0;
    snprintf(out, size, "%.*stq.zip", dir, path);
}

// Read a chip from path, or else from parent. Returns its CRC, or 0 (after
// printing why) if it can't be read.
static uint32_t load_chip(const char *path, const char *parent, const RomChip *chip, uint8_t *dest)
{
    const char *from = path, *name = chip_name(path, chip);
    if (!name) {
        from = parent;
        name = chip_name(parent, chip);
    }
    uint8_t *data = NULL;
    size_t size = 0;
    if (!name || !roms_read(from, name, &data, &size)) {
        fprintf(stderr, "ROMs: %s is missing (looked in %s and in %s)\n", chip->names[0], path, parent);
        return 0;
    }
    if (size != chip->size) {
        fprintf(stderr, "ROMs: %s is %zu bytes, expected %zu\n", name, size, chip->size);
        free(data);
        return 0;
    }
    uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);
    memcpy(dest, data, size);
    free(data);
    return crc ? crc : 1;
}

static void warn_crc(const char *name, uint32_t crc, uint32_t expected)
{
    if (crc != expected)
        fprintf(stderr, "ROMs: warning: %s has CRC %08X, expected %08X (a bad copy or a\n"
                        "modified chip?); trying it anyway\n", name, crc, expected);
}

bool rdi_load_roms(RdiBoard *b, const char *path)
{
    char parent[1024];
    parent_path(path, parent, sizeof parent);
    // U33: the fans' version if the set has it, else the original.
    const RomChip *u33 = chip_name(path, &u33_chips[1]) ? &u33_chips[1] : &u33_chips[0];
    uint32_t crc = load_chip(path, parent, u33, b->rom_u33);
    if (!crc) return false;
    warn_crc(u33->names[0], crc, u33->crc);

    // U1: set 1 or set 2, told apart by checksum (both may be called tq_u1.bin).
    const RomChip *u1 = chip_name(path, &u1_chips[0]) ? &u1_chips[0] : &u1_chips[1];
    crc = load_chip(path, parent, u1, b->rom_u1);
    if (!crc) return false;
    int set = crc == u1_chips[1].crc ? 2 : 1;
    if (set == 1) warn_crc(u1->names[0], crc, u1_chips[0].crc);

    crc = load_chip(path, parent, &cop_chip, b->rom_cop);
    if (!crc) return false;
    warn_crc(cop_chip.names[0], crc, cop_chip.crc);

    b->dswa = 0xFF;  // 110 seconds a coin, 1 coin 1 credit, 3 lives, demo sounds, attract audio always
    b->dswb = 0x0F;  // service mode off, LD-V1000
    printf("ROMs: Thayer's Quest (set %d%s)\n", set, u33 == &u33_chips[1] ? ", with the fans' unfiltered U33" : "");
    return true;
}

const char *rdi_game_name(const RdiBoard *b)
{
    (void)b;
    return "Thayer's Quest";
}

// DIP switches A: bits 0-2 the time a coin buys (all off: free play), bit 3
// coinage, bit 4 lives (clear: 5), bit 5 demo sounds, bit 6 attract audio
// every time (clear: one in eight).
void rdi_set(RdiBoard *b, RdiSetting setting, bool on)
{
    switch (setting) {
    case RDI_FREE_PLAY:
        if (on) b->dswa &= (uint8_t)~0x07;
        else if (!(b->dswa & 0x07)) b->dswa |= 0x07;
        break;
    case RDI_EXTRA_LIVES:
        if (on) b->dswa &= (uint8_t)~0x10;
        else b->dswa |= 0x10;
        break;
    case RDI_QUIET_ATTRACT:
        if (on) b->dswa &= (uint8_t)~0x20;
        else b->dswa |= 0x20;
        break;
    }
}

bool rdi_get(const RdiBoard *b, RdiSetting setting)
{
    switch (setting) {
    case RDI_FREE_PLAY: return !(b->dswa & 0x07);
    case RDI_EXTRA_LIVES: return !(b->dswa & 0x10);
    default: return !(b->dswa & 0x20);
    }
}

// ---------------------------------------------------------------------------
// The keyboard
// ---------------------------------------------------------------------------
//
// The COP421 clocks the keyboard with its SO line, and reads it on SI. Each
// row goes as ten bits, a bit per falling clock edge: 1, 1, 0, 1, a 1 for
// the last row (so the COP knows where it is), the row's four keys from
// the top, then 0. Then the next row.

static void keyboard_clock(void *ctx, int level)
{
    RdiBoard *b = ctx;
    if (level == b->kb_clock) return;
    b->kb_clock = level;
    if (level) return;  // falling edges only
    switch (++b->kb_bit) {
    case 1: case 3: b->kb_data = 1; break;
    case 2: case 9: b->kb_data = 0; break;
    case 4: b->kb_data = b->kb_row == 9; break;
    case 10:
        b->kb_bit = 0;
        b->kb_data = 1;
        b->kb_row = (b->kb_row + 1) % 10;
        b->kb_shift = 0;
        for (int i = 0; i < 4; i++)
            if (b->inputs.keys[b->kb_row * 4 + i]) b->kb_shift |= (uint8_t)(1 << i);
        break;
    default:
        b->kb_data = (b->kb_shift >> 3) & 1;
        b->kb_shift = (uint8_t)(b->kb_shift << 1);
        break;
    }
}

static int keyboard_data(void *ctx)
{
    return ((RdiBoard *)ctx)->kb_data;
}

// ---------------------------------------------------------------------------
// The COP421's ports
// ---------------------------------------------------------------------------

static uint8_t cop_read_l(void *ctx)
{
    RdiBoard *b = ctx;
    return b->cop_latch_out ? 0 : b->cop_latch;
}

static void cop_write_l(void *ctx, uint8_t v) { ((RdiBoard *)ctx)->cop_l = v; }
static uint8_t cop_read_g(void *ctx) { return ((RdiBoard *)ctx)->cop_command; }

static void cop_write_g(void *ctx, uint8_t v)
{
    ((RdiBoard *)ctx)->cop_latch_out = v & 0x08;
}

// D0 low: the timer interrupt; D1 low: a key is ready. (They stay until the
// Z80 acknowledges them.)
static void cop_write_d(void *ctx, uint8_t v)
{
    RdiBoard *b = ctx;
    if (!(v & 1)) b->timer_int = false;
    if (!(v & 2)) b->key_int = false;
}

// ---------------------------------------------------------------------------
// The Z80's address space and I/O
// ---------------------------------------------------------------------------

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    RdiBoard *b = ctx;
    if (addr < 0x8000) return b->rom_u33[addr];
    if (addr < 0xC000) return b->ram[addr - 0x8000];
    if (addr < 0xE000) return b->rom_u1[addr - 0xC000];
    return 0xFF;
}

static void mem_write(void *ctx, uint16_t addr, uint8_t v)
{
    RdiBoard *b = ctx;
    if (addr >= 0x8000 && addr < 0xC000) b->ram[addr - 0x8000] = v;
}

static uint8_t io_in(void *ctx, uint16_t port)
{
    RdiBoard *b = ctx;
    uint8_t p = port & 0xFF;
    if (p < 0x08) return ssi263_read(&b->speech, b->cycles);
    switch (p) {
    case 0x40:
        // Bit 3 is tied high, and so (as MAME has it) is bit 6, the U1
        // card's presence.
        return (uint8_t)(0x48 | (b->key_int ? 0x20 : 0) | (b->timer_int ? 0x10 : 0) |
                         (ssi263_requesting(&b->speech, b->cycles) ? 0 : 0x04));
    case 0x80: return b->cop_latch_out ? b->cop_l : b->cop_latch;
    case 0xF0: return ldv1000_read_status(b->player, b->cycles);
    case 0xF1: {
        uint8_t v = b->dswb & 0x0F;
        if (!b->inputs.coin1) v |= 0x10;
        if (!b->inputs.coin2) v |= 0x20;
        if (!ldv1000_status_strobe(b->player, b->cycles)) v |= 0x40;
        if (!ldv1000_command_strobe(b->player, b->cycles)) v |= 0x80;
        return v;
    }
    case 0xF2: return b->dswa;
    default: return 0xFF;
    }
}

static void io_out(void *ctx, uint16_t port, uint8_t v)
{
    RdiBoard *b = ctx;
    uint8_t p = port & 0xFF;
    if (p < 0x08) {
        ssi263_write(&b->speech, b->cycles, p, v);
        if (p == 0 && (v & 0x3F)) b->last_spoken = b->cycles;
        return;
    }
    switch (p) {
    case 0x20: b->cop_command = (v >> 5) & 0x07; break;
    case 0x80: b->cop_latch = v; break;
    case 0xA0: b->timer_int = true; break;
    case 0xC0: b->key_int = true; break;
    case 0xF3:
        // The periodic interrupt: acknowledged, and back in 8.25 ms
        // (1.1 x 750K x 0.01 uF).
        b->periodic_int = true;
        b->periodic_due = b->cycles + (uint64_t)RDI_CPU_HZ * 825 / 100000;
        break;
    case 0xF4:
        b->ld_out = v;
        if (b->ld_control & 0x20) ldv1000_write(b->player, b->cycles, v);
        break;
    case 0xF5:
        // Bit 5 puts the latch on the link; the player takes it at once.
        if ((v & 0x20) && !(b->ld_control & 0x20)) ldv1000_write(b->player, b->cycles, b->ld_out);
        b->ld_control = v;
        break;
    case 0xF6: case 0xF7:
        b->digits[((p & 1) << 3) | ((v >> 4) & 7)] = v & 0x0F;
        break;
    default: break;  // 40: card control
    }
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// The COP421 runs at 2 MHz / 32: an instruction cycle every 64 Z80 cycles.
#define COP_CYCLE (RDI_CPU_HZ / 62500)

void rdi_reset(RdiBoard *b, Ldv1000 *player)
{
    b->player = player;
    memset(&b->inputs, 0, sizeof b->inputs);
    memset(b->ram, 0, sizeof b->ram);
    for (int i = 0; i < 16; i++) b->digits[i] = 15;
    b->ld_out = 0xFF;
    b->ld_control = 0;
    b->cop_latch = b->cop_l = b->cop_command = 0;
    b->cop_latch_out = false;
    b->kb_clock = 0;
    b->kb_data = 1;
    b->kb_bit = 0;
    b->kb_row = 0;
    b->kb_shift = 0;
    b->timer_int = b->key_int = true;
    b->periodic_int = false;  // active from power-on, until first acknowledged
    b->periodic_due = 0;
    b->cycles = 0;
    b->cop_cycles = 0;
    b->last_spoken = 0;
    b->subtitle[0] = '\0';
    b->subtitle_next[0] = '\0';
    b->subtitle_since = 0;
    ssi263_init(&b->speech, RDI_CPU_HZ, RDI_SAMPLE_RATE);
    cop421_init(&b->cop,
                (Cop421Bus){.ctx = b, .read_l = cop_read_l, .write_l = cop_write_l, .read_g = cop_read_g,
                            .write_g = cop_write_g, .write_d = cop_write_d, .read_si = keyboard_data,
                            .write_so = keyboard_clock},
                b->rom_cop);
    z80_init(&b->cpu, (Z80Bus){.ctx = b, .read = mem_read, .write = mem_write, .in = io_in, .out = io_out});
}

void rdi_run(RdiBoard *b, uint64_t cycles)
{
    uint64_t end = b->cycles + cycles;
    while (b->cycles < end) {
        if (b->periodic_due && b->cycles >= b->periodic_due) {
            b->periodic_due = 0;
            b->periodic_int = false;
        }
        while (b->cop_cycles <= b->cycles) b->cop_cycles += (uint64_t)cop421_step(&b->cop) * COP_CYCLE;
        b->cpu.irq = !b->timer_int || !b->key_int || !b->periodic_int || ssi263_requesting(&b->speech, b->cycles);
        b->cpu.irq_data = 0xFF;
        b->cycles += (uint64_t)z80_step(&b->cpu);
    }
}

// ---------------------------------------------------------------------------
// Subtitles
// ---------------------------------------------------------------------------
//
// The game's lines are spelled for its text-to-speech rules, not for
// reading: "PL/EEZ/" makes the chip say "please". Stress marks are slashes,
// the player's name is in brackets, and dashes join syllables or words
// ("CAAN-TTIN-U", "ON-MY"). For subtitles the marks go, dash-joined pieces
// that make a word the game knows are joined, the game's sound-spellings
// are turned back into English from this list (made from the word table in
// its ROMs), and spoken numbers ("1 HUNDRED 7 TEE 8") become words. Words
// not on the list are shown as the game spells them.
//
// The game writes each line into its buffer a word or so at a time as it
// speaks, sometimes over the end of the last one, so a version of the line
// is shown only once it has held still for a moment.

#define TEXT_BUFFER 0xA500
#define TEXT_LENGTH 0xA6D3
#define SUBTITLE_HOLD (2 * RDI_CPU_HZ)        // shown for 2 s after the narrator stops
#define SUBTITLE_SETTLE (RDI_CPU_HZ * 6 / 100)  // a version must hold for 60 ms

static const char *const spellings[][2] = {
    {"PLEEZ", "PLEASE"}, {"YER", "YOUR"}, {"WUZ", "WAS"}, {"WUZNT", "WASN'T"}, {"DUZ", "DOES"},
    {"WERNT", "WEREN'T"}, {"WUN", "ONE"}, {"WWORD", "WORD"}, {"SZAY", "SAY"}, {"HAV", "HAVE"}, {"HAZ", "HAS"},
    {"GIHV", "GIVE"}, {"KEYBORD", "KEYBOARD"}, {"KEEEE", "KEY"}, {"INEE", "ANY"}, {"TOO", "TO"},
    {"CAANTTINU", "CONTINUE"}, {"UHUHP", "UP"}, {"UHP", "UP"}, {"SPELLYER", "SPELL YOUR"},
    {"INISHALIZING", "INITIALIZING"}, {"INISHAL", "INITIAL"}, {"SYSTUM", "SYSTEM"}, {"SCKOOR", "SCORE"},
    {"POINTSS", "POINTS"}, {"LYVELY", "LIVELY"}, {"ACKT", "ACT"}, {"RILLAX", "RELAX"}, {"SHSHY", "SHY"},
    {"SEAING", "SEEING"}, {"BLUD", "BLOOD"}, {"CRYSTULL", "CRYSTAL"}, {"KWOED", "QUOID"},
    {"MEHDALYUHN", "MEDALLION"}, {"SSEPTER", "SCEPTER"}, {"LAZER", "LASER"}, {"ANUTHOR", "ANOTHER"},
    {"NUTHING", "NOTHING"}, {"DIFFRENT", "DIFFERENT"}, {"DISCRIBE", "DESCRIBE"}, {"FUHNCKSHUN", "FUNCTION"},
    {"INSTRUCCSHUNS", "INSTRUCTIONS"}, {"MESSIDJ", "MESSAGE"}, {"FRASE", "PHRASE"}, {"TWELVV", "TWELVE"},
    {"TTHERD", "THIRD"}, {"THROO", "THROUGH"}, {"WOONDED", "WOUNDED"}, {"CUNSISTANTLY", "CONSISTENTLY"},
    {"CUNTROAL", "CONTROL"}, {"IMERJ", "EMERGE"}, {"KRE8EVLY", "CREATIVELY"}, {"LAANNG", "LONG"},
    {"LEENK", "LINK"}, {"OWWN", "OWN"}, {"OALD", "OLD"}, {"CKOLD", "COLD"}, {"CKOIN", "COIN"}, {"CKAN", "CAN"},
    {"CKOIN'S", "COINS"}, {"CKAN'T", "CAN'T"}, {"CKALMLY", "CALMLY"}, {"DDON'T", "DON'T"}, {"DROPPP", "DROP"},
    {"DROPPTT", "DROPPED"}, {"PPRINTT", "PRINT"}, {"TESST", "TEST"}, {"BURDDEHN", "BURDEN"},
    {"JAHNUARY", "JANUARY"}, {"FEBHUARY", "FEBRUARY"}, {"APRUL", "APRIL"}, {"MUNDAY", "MONDAY"},
    {"TOOZDAY", "TUESDAY"}, {"WENZDAY", "WEDNESDAY"}, {"THURZDAY", "THURSDAY"}, {"CAAN", "CAN"},
    {"U", "YOU"}, {"UR", "YOU'RE"}, {"UH", "A"}, {"DDTHANCK", "THANK"}, {"CKAUST", "COST"}, {"AIRER", "ERROR"},
    {"AWFF", "OFF"}, {"BAHAHCK", "BACK"}, {"BIN", "BEEN"}, {"BOANESS", "BONUS"}, {"BOE", "BOW"},
    {"BWAIR", "BEWARE"}, {"CKALMM", "CALM"}, {"CLIPH", "CLIFF"}, {"CUNNGRADJULLASHUNS", "CONGRATULATIONS"},
    {"DAYTT", "DATE"}, {"DCEMBER", "DECEMBER"}, {"DOO", "DO"}, {"DOR", "DOOR"}, {"ELEVN", "ELEVEN"},
    {"FAHAST", "FAST"}, {"FLAYCKE", "FLAKY"}, {"GIHLD", "GUILD"}, {"HEEEALED", "HEALED"}, {"HEEEE", "HE"},
    {"HY", "HI"}, {"IMERJNCEE", "EMERGENCY"}, {"KLEAR", "CLEAR"}, {"LAWST", "LOST"}, {"LERERN", "LEARN"},
    {"LYVZ", "LIVES"}, {"MINETT", "MINUTE"}, {"NAAOW", "NOW"}, {"NNU", "NEW"}, {"ONIX", "ONYX"}, {"OV", "OF"},
    {"PAASCEHBL", "POSSIBLE"}, {"PAHATH", "PATH"}, {"PLUCG", "PLUG"}, {"QOOT", "CUTE"},
    {"RECUGNISHUN", "RECOGNITION"}, {"ROOHD", "RUDE"}, {"SHERER", "SURE"}, {"SOE", "SO"}, {"SOERD", "SWORD"},
    {"STAHAHPHFF", "STAFF"}, {"SUHUHUHCKERERERER", "SUCKER"}, {"THOUSEND", "THOUSAND"}, {"USSOOM", "ASSUME"},
    {"WOENT", "WON'T"}, {"XPECT", "EXPECT"}, {"XPLNASHON", "EXPLANATION"}, {"YEER", "YEAR"},
    // Words the game stores in pieces, which are English once joined.
    {"AMULET", "AMULET"}, {"ENTERTAINMENT", "ENTERTAINMENT"}, {"HALCYON", "HALCYON"}, {"ITEM", "ITEM"},
    {"NOVEMBER", "NOVEMBER"}, {"OASIS", "OASIS"}, {"REGULAR", "REGULAR"}, {"RELIC", "RELIC"},
    {"REPLAY", "REPLAY"}, {"RESTARTING", "RESTARTING"}, {"SHIMMERING", "SHIMMERING"}, {"START", "START"},
    {"UPDATE", "UPDATE"}, {"ITEM'S", "ITEMS"},
};

static const char *english(const char *word)
{
    for (size_t i = 0; i < sizeof spellings / sizeof spellings[0]; i++)
        if (!strcmp(spellings[i][0], word)) return spellings[i][1];
    return NULL;
}

// The words of a line, each with any punctuation after it.
#define MAX_WORDS 48
typedef struct {
    char text[48];
    char punct;  // ',', '?', '.', '!' or 0
} Word;

static void add_word(Word *words, int *count, const char *text)
{
    if (*count >= MAX_WORDS || !*text) return;
    snprintf(words[*count].text, sizeof words[0].text, "%s", text);
    words[*count].punct = 0;
    (*count)++;
}

// One run of dash-joined pieces ("CAAN", "TTIN", "U"): the longest runs of
// pieces that make a known word are joined and turned to English; other
// pieces stay as they are, as words of their own.
static void add_run(Word *words, int *count, char pieces[][48], int n)
{
    for (int i = 0; i < n;) {
        int best = -1;
        const char *word = NULL;
        char joined[160] = "";
        for (int j = i; j < n; j++) {
            if (strlen(joined) + strlen(pieces[j]) + 1 > sizeof joined) break;
            strcat(joined, pieces[j]);
            const char *e = english(joined);
            if (e) best = j, word = e;
        }
        if (best < 0) {
            add_word(words, count, pieces[i]);
            i++;
        } else {
            add_word(words, count, word);
            i = best + 1;
        }
    }
}

// Is word part of a number (so a 4 after it is "four", not "for")?
static bool in_number(const char *word)
{
    size_t n = strlen(word);
    return !strcmp(word, "HUNDRED") || !strcmp(word, "THOUSAND") || (n > 2 && !strcmp(word + n - 2, "TY"));
}

// Spoken numbers: the game says "1 HUNDRED 7 TEE 8" and "6 TEEN", and "TO"
// for two before HUNDRED and THOUSAND. Digits become words, and a digit
// before TEE or TEEN becomes the tens or the teens. A lone 4 is "for" ("4
// ME").
static void spell_numbers(Word *words, int *count)
{
    static const char *const ones[10] = {"ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE"};
    static const char *const tens[10] = {"", "TEN", "TWENTY", "THIRTY", "FORTY", "FIFTY", "SIXTY", "SEVENTY", "EIGHTY", "NINETY"};
    static const char *const teens[10] = {"TEN", "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN", "SEVENTEEN", "EIGHTEEN", "NINETEEN"};
    // A number on its own is the narrator echoing a key: 2 and 4, not "to"
    // and "for".
    if (*count == 1) {
        const char *t = words[0].text;
        if (!strcmp(t, "TO")) snprintf(words[0].text, sizeof words[0].text, "TWO");
        else if (strlen(t) == 1 && t[0] >= '0' && t[0] <= '9')
            snprintf(words[0].text, sizeof words[0].text, "%s", ones[t[0] - '0']);
        return;
    }
    for (int i = 0; i < *count; i++) {
        Word *w = &words[i];
        const char *next = i + 1 < *count ? words[i + 1].text : "";
        if (!strcmp(w->text, "C")) {  // "C YOUR ITEMS": the letter for "see"
            snprintf(w->text, sizeof w->text, "SEE");
            continue;
        }
        if (!strcmp(w->text, "O") && !strcmp(next, "K") && !w->punct) {  // "O K"
            snprintf(w->text, sizeof w->text, "OK");
            w->punct = words[i + 1].punct;
            memmove(&words[i + 1], &words[i + 2], sizeof *words * (size_t)(*count - i - 2));
            (*count)--;
            continue;
        }
        bool big = !strcmp(next, "HUNDRED") || !strcmp(next, "THOUSAND");
        bool tee = !strcmp(next, "TEE"), teen = !strcmp(next, "TEEN");
        int digit = -1;
        if (strlen(w->text) == 1 && w->text[0] >= '0' && w->text[0] <= '9') digit = w->text[0] - '0';
        else if (!strcmp(w->text, "TO") && big) digit = 2;
        else if (!strcmp(w->text, "TWEN") && tee) digit = 2;
        else if (!strcmp(w->text, "THIR") && (tee || teen)) digit = 3;
        else if (!strcmp(w->text, "FIF") && (tee || teen)) digit = 5;
        if (digit < 0) continue;
        if ((tee || teen) && !words[i].punct) {
            snprintf(w->text, sizeof w->text, "%s", tee ? tens[digit] : teens[digit]);
            w->punct = words[i + 1].punct;
            memmove(&words[i + 1], &words[i + 2], sizeof *words * (size_t)(*count - i - 2));
            (*count)--;
        } else if (digit == 4 && !big && !(i > 0 && in_number(words[i - 1].text))) {
            snprintf(w->text, sizeof w->text, "FOR");
        } else {
            snprintf(w->text, sizeof w->text, "%s", ones[digit]);
        }
    }
}

// Read the line in the game's buffer and write it out for reading.
static void read_line(const RdiBoard *b, char *out, size_t size)
{
    Word words[MAX_WORDS];
    int count = 0;
    char pieces[16][48];
    int n = 0, plen = 0;
    int len = b->ram[TEXT_LENGTH - 0x8000];
    for (int i = 0; i <= len; i++) {
        char ch = i < len ? (char)b->ram[TEXT_BUFFER - 0x8000 + i] : ' ';
        if (ch == '/' || ch == '(' || ch == ')') continue;  // stress marks, the player's name
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '\'') {
            if (plen < 47 && n < 16) pieces[n][plen++] = ch;
            continue;
        }
        if (plen && n < 16) pieces[n][plen] = '\0', n++, plen = 0;
        if (ch == '-') continue;  // the run goes on
        add_run(words, &count, pieces, n);  // a space or punctuation ends it
        n = 0;
        if ((ch == ',' || ch == '?' || ch == '.' || ch == '!') && count > 0 && !words[count - 1].punct)
            words[count - 1].punct = ch;
    }
    spell_numbers(words, &count);

    size_t pos = 0;
    out[0] = '\0';
    for (int i = 0; i < count; i++) {
        // No pause after "THE", "A" or "AN" (the game puts one before some
        // item names).
        bool article = !strcmp(words[i].text, "THE") || !strcmp(words[i].text, "A") || !strcmp(words[i].text, "AN");
        int written = snprintf(out + pos, size - pos, "%s%s", pos ? " " : "", words[i].text);
        if (written < 0 || (size_t)written >= size - pos) break;
        pos += (size_t)written;
        if (words[i].punct && !article && pos + 1 < size) out[pos++] = words[i].punct, out[pos] = '\0';
    }
}

const char *rdi_subtitle(RdiBoard *b)
{
    if (!b->last_spoken || b->cycles - b->last_spoken > SUBTITLE_HOLD) return NULL;
    char line[sizeof b->subtitle];
    read_line(b, line, sizeof line);
    if (strcmp(line, b->subtitle_next)) {
        snprintf(b->subtitle_next, sizeof b->subtitle_next, "%s", line);
        b->subtitle_since = b->cycles;
    } else if (b->cycles - b->subtitle_since >= SUBTITLE_SETTLE) {
        snprintf(b->subtitle, sizeof b->subtitle, "%s", line);
    }
    return b->subtitle[0] ? b->subtitle : NULL;
}
