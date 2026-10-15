#include "games/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

bool machine_load_roms(Machine *m, const char *path)
{
    *m = (Machine){0};
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "ROMs: cannot find %s\n", path);
        return false;
    }
    if (uni_recognizes(path)) {
        m->kind = MACHINE_UNIVERSAL;
        m->uni = calloc(1, sizeof *m->uni);
        return uni_load_roms(m->uni, path);
    }
    if (cine_recognizes(path)) {
        m->kind = MACHINE_CINEMATRONICS;
        m->cine = calloc(1, sizeof *m->cine);
        return cine_load_roms(m->cine, path);
    }
    fprintf(stderr, "ROMs: found no ROMs laser-vault knows in %s. It plays Dragon's Lair\n"
                    "(lair.zip: revs D, E, F and F2, and the Dragon's Lair Enhancement 1.1 and\n"
                    "2.0 sets), Space Ace (ace.zip: rev A3, and the Space Ace Enhancement,\n"
                    "sae.zip) and Super Don Quix-ote (superdq.zip). A folder of the ROM files\n"
                    "works too.\n", path);
    return false;
}

void machine_free(Machine *m)
{
    if (m->uni) sn76496_free(&m->uni->sound);
    if (m->cine) ay8910_free(&m->cine->ay);
    free(m->cine);
    free(m->uni);
    *m = (Machine){0};
}

const char *machine_game_name(const Machine *m)
{
    return m->kind == MACHINE_UNIVERSAL ? uni_game_name(m->uni) : cine_game_name(m->cine->game);
}

double machine_cpu_hz(const Machine *m)
{
    return m->kind == MACHINE_UNIVERSAL ? UNI_CPU_HZ : CINE_CPU_HZ;
}

uint64_t machine_cycles(const Machine *m)
{
    return m->kind == MACHINE_UNIVERSAL ? m->uni->cycles : m->cine->cycles;
}

double machine_disc_fps(const Machine *m)
{
    return m->kind == MACHINE_UNIVERSAL ? 29.97 : 23.976;
}

void machine_reset(Machine *m, Ldv1000 *player)
{
    if (m->kind == MACHINE_UNIVERSAL) uni_reset(m->uni, player);
    else cine_reset(m->cine, cine_ldv1000_link(player));
}

void machine_run(Machine *m, uint64_t cycles)
{
    if (m->kind == MACHINE_UNIVERSAL) uni_run(m->uni, cycles);
    else cine_run(m->cine, cycles);
}

void machine_set_controls(Machine *m, const Controls *c)
{
    if (m->kind == MACHINE_UNIVERSAL) {
        m->uni->inputs = (UniInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right, .button = c->button,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else {
        m->cine->inputs = (CineInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right, .sword = c->button,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
            .cadet = c->skill[0], .captain = c->skill[1], .ace = c->skill[2],
        };
    }
}

uint8_t *machine_dsw(Machine *m, int bank)
{
    if (m->kind == MACHINE_UNIVERSAL) return bank == 1 ? &m->uni->dsw1 : &m->uni->dsw2;
    return bank == 1 ? &m->cine->dsw1 : &m->cine->dsw2;
}

// The boards' own names for the settings.
static const UniSetting uni_settings[] = {
    [MACHINE_FREE_PLAY] = UNI_FREE_PLAY,
    [MACHINE_UNLIMITED_LIVES] = UNI_UNLIMITED_LIVES,
    [MACHINE_QUIET_ATTRACT] = UNI_QUIET_ATTRACT,
};
static const CineSetting cine_settings[] = {
    [MACHINE_FREE_PLAY] = CINE_FREE_PLAY,
    [MACHINE_UNLIMITED_LIVES] = CINE_UNLIMITED_LIVES,
    [MACHINE_QUIET_ATTRACT] = CINE_QUIET_ATTRACT,
};

void machine_set(Machine *m, MachineSetting setting, bool on)
{
    if (m->kind == MACHINE_UNIVERSAL) uni_set(m->uni, uni_settings[setting], on);
    else cine_set(m->cine, cine_settings[setting], on);
}

bool machine_get(const Machine *m, MachineSetting setting)
{
    if (m->kind == MACHINE_UNIVERSAL) return uni_get(m->uni, uni_settings[setting]);
    return cine_get(m->cine, cine_settings[setting]);
}

int machine_sample_rate(const Machine *m)
{
    return m->kind == MACHINE_UNIVERSAL ? UNI_SAMPLE_RATE : CINE_SAMPLE_RATE;
}

void machine_start_sound(Machine *m)
{
    if (m->kind == MACHINE_UNIVERSAL) sn76496_set_rendering(&m->uni->sound, true, m->uni->cycles);
    else ay8910_set_rendering(&m->cine->ay, true, m->cine->cycles);
}

int machine_take_samples(Machine *m, const float **samples)
{
    if (m->kind == MACHINE_UNIVERSAL) {
        sn76496_run(&m->uni->sound, m->uni->cycles);
        return sn76496_take_samples(&m->uni->sound, samples);
    }
    ay8910_run(&m->cine->ay, m->cine->cycles);
    return ay8910_take_samples(&m->cine->ay, samples);
}

bool machine_overlay_layout(const Machine *m, OverlayLayout *layout)
{
    if (m->kind != MACHINE_UNIVERSAL) return false;
    *layout = (OverlayLayout){
        .width = UNI_OVERLAY_W,
        .height = UNI_OVERLAY_H,
        .x = (1 - UNI_OVERLAY_AREA_W) / 2,
        .y = (1 - UNI_OVERLAY_AREA_H) / 2,
        .w = UNI_OVERLAY_AREA_W,
        .h = UNI_OVERLAY_AREA_H,
    };
    return true;
}

unsigned machine_overlay_serial(const Machine *m)
{
    return m->kind == MACHINE_UNIVERSAL ? m->uni->overlay_serial : 0;
}

void machine_draw_overlay(const Machine *m, uint32_t *pixels)
{
    if (m->kind == MACHINE_UNIVERSAL) uni_draw_overlay(m->uni, pixels);
}
