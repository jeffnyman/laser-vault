#include "games/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool machine_recognizes(const char *path)
{
    return uni_recognizes(path) || stern_recognizes(path) || funai_recognizes(path) ||
           konami_recognizes(path) || sega_recognizes(path) || rdi_recognizes(path) ||
           deco_recognizes(path) || leland_recognizes(path) || gottlieb_recognizes(path) ||
           gpw_recognizes(path) || cine_recognizes(path);
}

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
    if (stern_recognizes(path)) {
        m->kind = MACHINE_STERN;
        m->stern = calloc(1, sizeof *m->stern);
        return stern_load_roms(m->stern, path);
    }
    if (funai_recognizes(path)) {
        m->kind = MACHINE_FUNAI;
        m->funai = calloc(1, sizeof *m->funai);
        return funai_load_roms(m->funai, path);
    }
    if (konami_recognizes(path)) {
        m->kind = MACHINE_KONAMI;
        m->konami = calloc(1, sizeof *m->konami);
        return konami_load_roms(m->konami, path);
    }
    if (sega_recognizes(path)) {
        m->kind = MACHINE_SEGA;
        m->sega = calloc(1, sizeof *m->sega);
        return sega_load_roms(m->sega, path);
    }
    if (rdi_recognizes(path)) {
        m->kind = MACHINE_RDI;
        m->rdi = calloc(1, sizeof *m->rdi);
        return rdi_load_roms(m->rdi, path);
    }
    if (deco_recognizes(path)) {
        m->kind = MACHINE_DATAEAST;
        m->deco = calloc(1, sizeof *m->deco);
        return deco_load_roms(m->deco, path);
    }
    if (leland_recognizes(path)) {
        m->kind = MACHINE_LELAND;
        m->leland = calloc(1, sizeof *m->leland);
        return leland_load_roms(m->leland, path);
    }
    if (gottlieb_recognizes(path)) {
        m->kind = MACHINE_GOTTLIEB;
        m->gottlieb = calloc(1, sizeof *m->gottlieb);
        return gottlieb_load_roms(m->gottlieb, path);
    }
    if (gpw_recognizes(path)) {
        m->kind = MACHINE_GPWORLD;
        m->gpw = calloc(1, sizeof *m->gpw);
        return gpw_load_roms(m->gpw, path);
    }
    if (cine_recognizes(path)) {
        m->kind = MACHINE_CINEMATRONICS;
        m->cine = calloc(1, sizeof *m->cine);
        return cine_load_roms(m->cine, path);
    }
    fprintf(stderr, "ROMs: found no ROMs laser-vault knows in %s. It plays Dragon's Lair\n"
                    "(lair.zip: revs D, E, F and F2, and the Dragon's Lair Enhancement 1.1, 2.0\n"
                    "and 2.1 sets), Space Ace (ace.zip: rev A3, and the Space Ace Enhancement,\n"
                    "sae.zip), Super Don Quix-ote (superdq.zip), Esh's Aurunmilla (esh.zip),\n"
                    "Cliff Hanger (cliff.zip), Badlands (badlands.zip), Astron Belt\n"
                    "(astronp.zip), Galaxy Ranger (galaxyrp.zip, or Star Blazer's\n"
                    "sblazerp.zip), Cobra Command (cobra.zip, or Sega's version,\n"
                    "cobraab.zip), Thayer's Quest (tq.zip), Bega's Battle (bega.zip),\n"
                    "Dragon's Lair II (lair2.zip), Us vs. Them (uvt.zip), Road Blaster\n"
                    "(roadblaster.zip), M.A.C.H. 3 (mach3.zip), Goal to Go (gtg.zip) and GP\n"
                    "World (gpworld.zip). A folder of the ROM files works too.\n", path);
    return false;
}

void machine_free(Machine *m)
{
    if (m->uni) sn76496_free(&m->uni->sound);
    if (m->cine) ay8910_free(&m->cine->ay);
    if (m->konami) sn76496_free(&m->konami->sound);
    if (m->sega) samples_free(&m->sega->sound);
    if (m->rdi) ssi263_free(&m->rdi->speech);
    if (m->deco) {
        for (int i = 0; i < 2; i++) ay8910_free(&m->deco->ay[i]);
        free(m->deco->samples);
    }
    if (m->leland) leland_free(m->leland);
    if (m->gottlieb) gottlieb_free(m->gottlieb);
    free(m->cine);
    free(m->uni);
    free(m->funai);
    free(m->stern);
    free(m->konami);
    free(m->sega);
    free(m->rdi);
    free(m->deco);
    free(m->leland);
    free(m->gottlieb);
    free(m->gpw);
    *m = (Machine){0};
}

int machine_load_samples(Machine *m, const char *dir, bool *wanted)
{
    *wanted = m->kind == MACHINE_SEGA && sega_sample_example(m->sega);
    return m->kind == MACHINE_SEGA ? sega_load_samples(m->sega, dir) : 0;
}

const char *machine_sample_example(const Machine *m)
{
    return m->kind == MACHINE_SEGA ? sega_sample_example(m->sega) : "";
}

bool machine_has_keyboard(const Machine *m)
{
    return m->kind == MACHINE_RDI;
}

int machine_coins_per_credit(const Machine *m)
{
    if (m->kind == MACHINE_CINEMATRONICS || m->kind == MACHINE_LELAND) return 2;
    if (m->kind == MACHINE_STERN) return m->stern->coins_per_credit;
    if (m->kind == MACHINE_GOTTLIEB && m->gottlieb->game == GOTTLIEB_MACH3) return 2;  // its default switches
    return 1;
}

const char *machine_subtitle(Machine *m)
{
    return m->kind == MACHINE_RDI ? rdi_subtitle(m->rdi) : NULL;
}

const char *machine_game_name(const Machine *m)
{
    if (m->kind == MACHINE_UNIVERSAL) return uni_game_name(m->uni);
    if (m->kind == MACHINE_FUNAI) return funai_game_name(m->funai);
    if (m->kind == MACHINE_STERN) return stern_game_name(m->stern);
    if (m->kind == MACHINE_KONAMI) return konami_game_name(m->konami);
    if (m->kind == MACHINE_SEGA) return sega_game_name(m->sega);
    if (m->kind == MACHINE_RDI) return rdi_game_name(m->rdi);
    if (m->kind == MACHINE_DATAEAST) return deco_game_name(m->deco);
    if (m->kind == MACHINE_LELAND) return leland_game_name(m->leland);
    if (m->kind == MACHINE_GOTTLIEB) return gottlieb_game_name(m->gottlieb);
    if (m->kind == MACHINE_GPWORLD) return gpw_game_name(m->gpw);
    return cine_game_name(m->cine->game);
}

const char *machine_icon_name(const Machine *m)
{
    switch (m->kind) {
    case MACHINE_UNIVERSAL: return "superdq";
    case MACHINE_FUNAI: return "esh";
    case MACHINE_STERN: return m->stern->goal_to_go ? "gtg" : "cliff";
    case MACHINE_KONAMI: return "badlands";
    case MACHINE_SEGA:
        if (m->sega->game == SEGA_COBRA_COMMAND) return "cobra";
        return m->sega->game == SEGA_ASTRON_BELT ? "astron" : "galaxy";  // Star Blazer too
    case MACHINE_RDI: return "thayers";
    case MACHINE_DATAEAST:
        if (m->deco->game == DECO_RBLASTER) return "roadblaster";
        return m->deco->game == DECO_COBRA ? "cobra" : "bega";
    case MACHINE_LELAND: return "dlair2";
    case MACHINE_GOTTLIEB: return m->gottlieb->game == GOTTLIEB_MACH3 ? "mach3" : "uvt";
    case MACHINE_GPWORLD: return "gpworld";
    default: return m->cine->game == CINE_SPACE_ACE ? "spaceace" : "dlair";
    }
}

double machine_cpu_hz(const Machine *m)
{
    if (m->kind == MACHINE_UNIVERSAL) return UNI_CPU_HZ;
    if (m->kind == MACHINE_FUNAI) return FUNAI_CPU_HZ;
    if (m->kind == MACHINE_STERN) return STERN_CPU_HZ;
    if (m->kind == MACHINE_KONAMI) return KONAMI_CPU_HZ;
    if (m->kind == MACHINE_SEGA) return SEGA_CPU_HZ;
    if (m->kind == MACHINE_RDI) return RDI_CPU_HZ;
    if (m->kind == MACHINE_DATAEAST) return DECO_CPU_HZ;
    if (m->kind == MACHINE_LELAND) return LELAND_CPU_HZ;
    if (m->kind == MACHINE_GOTTLIEB) return GOTTLIEB_CPU_HZ;
    if (m->kind == MACHINE_GPWORLD) return GPW_CPU_HZ;
    return CINE_CPU_HZ;
}

uint64_t machine_cycles(const Machine *m)
{
    if (m->kind == MACHINE_UNIVERSAL) return m->uni->cycles;
    if (m->kind == MACHINE_FUNAI) return m->funai->cycles;
    if (m->kind == MACHINE_STERN) return m->stern->cycles;
    if (m->kind == MACHINE_KONAMI) return m->konami->cycles;
    if (m->kind == MACHINE_SEGA) return m->sega->cycles;
    if (m->kind == MACHINE_RDI) return m->rdi->cycles;
    if (m->kind == MACHINE_DATAEAST) return m->deco->cycles;
    if (m->kind == MACHINE_LELAND) return m->leland->cycles;
    if (m->kind == MACHINE_GOTTLIEB) return m->gottlieb->cycles;
    if (m->kind == MACHINE_GPWORLD) return m->gpw->cycles;
    return m->cine->cycles;
}

double machine_disc_fps(const Machine *m)
{
    return m->kind == MACHINE_CINEMATRONICS ? 23.976 : 29.97;
}

bool machine_uses_pr8210(const Machine *m)
{
    return m->kind == MACHINE_STERN || m->kind == MACHINE_GOTTLIEB;
}

// Sega's player, for the Sega board's versions made for it.
static bool uses_vip(const Machine *m)
{
    return m->kind == MACHINE_SEGA && m->sega->serial;
}

void machine_reset(Machine *m, Ldp *ldp)
{
    if (machine_uses_pr8210(m)) pr8210_init(&m->pr8210, ldp, machine_cpu_hz(m));
    else if (uses_vip(m)) vip9500sg_init(&m->vip, ldp, machine_cpu_hz(m));
    else if (m->kind == MACHINE_DATAEAST) ldp1000_init(&m->ldp1000, ldp, machine_cpu_hz(m));
    else if (m->kind == MACHINE_LELAND) ldp1450_init(&m->ldp1000, ldp, machine_cpu_hz(m));
    else ldv1000_init(&m->ldv1000, ldp, machine_cpu_hz(m));
    if (m->kind == MACHINE_UNIVERSAL) uni_reset(m->uni, &m->ldv1000);
    else if (m->kind == MACHINE_FUNAI) funai_reset(m->funai, &m->ldv1000);
    else if (m->kind == MACHINE_STERN) stern_reset(m->stern, &m->pr8210);
    else if (m->kind == MACHINE_KONAMI) {
        m->ldv1000.konami_rom = true;
        konami_reset(m->konami, &m->ldv1000);
    }
    else if (m->kind == MACHINE_SEGA) sega_reset(m->sega, &m->ldv1000, &m->vip);
    else if (m->kind == MACHINE_RDI) rdi_reset(m->rdi, &m->ldv1000);
    else if (m->kind == MACHINE_DATAEAST) deco_reset(m->deco, &m->ldp1000);
    else if (m->kind == MACHINE_LELAND) leland_reset(m->leland, &m->ldp1000);
    else if (m->kind == MACHINE_GOTTLIEB) gottlieb_reset(m->gottlieb, &m->pr8210);
    else if (m->kind == MACHINE_GPWORLD) gpw_reset(m->gpw, &m->ldv1000);
    else cine_reset(m->cine, cine_ldv1000_link(&m->ldv1000));
}

void machine_update_player(Machine *m)
{
    if (machine_uses_pr8210(m)) pr8210_update(&m->pr8210, machine_cycles(m));
    else if (uses_vip(m)) vip9500sg_update(&m->vip, machine_cycles(m));
    else if (m->kind == MACHINE_DATAEAST || m->kind == MACHINE_LELAND) ldp1000_update(&m->ldp1000, machine_cycles(m));
    else ldv1000_update(&m->ldv1000, machine_cycles(m));
}

bool machine_audio_on(const Machine *m, int channel)
{
    if (machine_uses_pr8210(m)) return pr8210_audio_on(&m->pr8210, channel);
    if (uses_vip(m)) return vip9500sg_audio_on(&m->vip, channel);
    if (m->kind == MACHINE_DATAEAST || m->kind == MACHINE_LELAND) return ldp1000_audio_on(&m->ldp1000, channel);
    if (m->kind == MACHINE_KONAMI && !konami_disc_audio_on(m->konami)) return false;
    return ldv1000_audio_on(&m->ldv1000, channel);
}

void machine_run(Machine *m, uint64_t cycles)
{
    if (m->kind == MACHINE_UNIVERSAL) uni_run(m->uni, cycles);
    else if (m->kind == MACHINE_FUNAI) funai_run(m->funai, cycles);
    else if (m->kind == MACHINE_STERN) stern_run(m->stern, cycles);
    else if (m->kind == MACHINE_KONAMI) konami_run(m->konami, cycles);
    else if (m->kind == MACHINE_SEGA) sega_run(m->sega, cycles);
    else if (m->kind == MACHINE_RDI) rdi_run(m->rdi, cycles);
    else if (m->kind == MACHINE_DATAEAST) deco_run(m->deco, cycles);
    else if (m->kind == MACHINE_LELAND) leland_run(m->leland, cycles);
    else if (m->kind == MACHINE_GOTTLIEB) gottlieb_run(m->gottlieb, cycles);
    else if (m->kind == MACHINE_GPWORLD) gpw_run(m->gpw, cycles);
    else cine_run(m->cine, cycles);
}

void machine_set_controls(Machine *m, const Controls *c)
{
    bool button = c->button || c->button2;  // for games with one button
    if (m->kind == MACHINE_STERN) {
        m->stern->inputs = (SternInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right,
            .hand = c->button, .feet = c->button2,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else if (m->kind == MACHINE_RDI) {
        m->rdi->inputs.coin1 = c->coin1;
        m->rdi->inputs.coin2 = c->coin2;
        memcpy(m->rdi->inputs.keys, c->keyboard, sizeof m->rdi->inputs.keys);
    } else if (m->kind == MACHINE_SEGA) {
        m->sega->inputs = (SegaInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right,
            // Cobra Command has two buttons; the others take either as their one.
            .fire = m->sega->game == SEGA_COBRA_COMMAND ? c->button : button, .fire2 = c->button2,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else if (m->kind == MACHINE_DATAEAST) {
        m->deco->inputs = (DecoInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right,
            .button1 = c->button, .button2 = c->button2, .button3 = c->button3,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else if (m->kind == MACHINE_LELAND) {
        m->leland->inputs = (LelandInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right, .sword = button,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else if (m->kind == MACHINE_GOTTLIEB) {
        // Us vs. Them and M.A.C.H. 3 start a game on the fire button (Us vs.
        // Them never reads the start buttons), so the start key fires too.
        m->gottlieb->inputs = (GottliebInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right,
            .button1 = c->button || c->start1, .button2 = c->button2, .button3 = c->button3,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else if (m->kind == MACHINE_GPWORLD) {
        // A driving game: the arrows steer and move the gear lever, the
        // button is the accelerator and the second button the brake.
        m->gpw->inputs = (GpwInputs){
            .left = c->left, .right = c->right, .gear_up = c->up, .gear_down = c->down,
            .gas = c->button, .brake = c->button2,
            .start1 = c->start1, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else if (m->kind == MACHINE_KONAMI) {
        m->konami->inputs = (KonamiInputs){
            .fire = button, .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else if (m->kind == MACHINE_FUNAI) {
        m->funai->inputs = (FunaiInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right,
            .button1 = c->button, .button2 = c->button2,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else if (m->kind == MACHINE_UNIVERSAL) {
        m->uni->inputs = (UniInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right, .button = button,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
        };
    } else {
        m->cine->inputs = (CineInputs){
            .up = c->up, .down = c->down, .left = c->left, .right = c->right, .sword = button,
            .start1 = c->start1, .start2 = c->start2, .coin1 = c->coin1, .coin2 = c->coin2,
            .cadet = c->skill[0], .captain = c->skill[1], .ace = c->skill[2],
        };
    }
}

uint8_t *machine_dsw(Machine *m, int bank)
{
    if (m->kind == MACHINE_STERN) return bank >= 1 && bank <= 4 ? &m->stern->dsw[bank - 1] : NULL;
    if (m->kind == MACHINE_GOTTLIEB) return bank == 1 ? &m->gottlieb->dsw : NULL;
    if (m->kind == MACHINE_GPWORLD) return bank == 1 ? &m->gpw->dsw1 : bank == 2 ? &m->gpw->dsw2 : NULL;
    if (bank < 1 || bank > 2 || m->kind == MACHINE_FUNAI || m->kind == MACHINE_LELAND) return NULL;
    if (m->kind == MACHINE_KONAMI) return bank == 1 ? &m->konami->dsw1 : &m->konami->dsw2;
    if (m->kind == MACHINE_SEGA) return bank == 1 ? &m->sega->dsw1 : &m->sega->dsw2;
    if (m->kind == MACHINE_RDI) return bank == 1 ? &m->rdi->dswa : &m->rdi->dswb;
    if (m->kind == MACHINE_DATAEAST) return bank == 1 ? &m->deco->dsw1 : &m->deco->dsw2;
    if (m->kind == MACHINE_UNIVERSAL) return bank == 1 ? &m->uni->dsw1 : &m->uni->dsw2;
    return bank == 1 ? &m->cine->dsw1 : &m->cine->dsw2;
}

// The boards' own names for the settings.
static const UniSetting uni_settings[] = {
    [MACHINE_FREE_PLAY] = UNI_FREE_PLAY,
    [MACHINE_UNLIMITED_LIVES] = UNI_UNLIMITED_LIVES,
    [MACHINE_QUIET_ATTRACT] = UNI_QUIET_ATTRACT,
};
static const SternSetting stern_settings[] = {
    [MACHINE_FREE_PLAY] = STERN_FREE_PLAY,
    [MACHINE_UNLIMITED_LIVES] = STERN_UNLIMITED_LIVES,
    [MACHINE_QUIET_ATTRACT] = STERN_QUIET_ATTRACT,
};
static const CineSetting cine_settings[] = {
    [MACHINE_FREE_PLAY] = CINE_FREE_PLAY,
    [MACHINE_UNLIMITED_LIVES] = CINE_UNLIMITED_LIVES,
    [MACHINE_QUIET_ATTRACT] = CINE_QUIET_ATTRACT,
};

void machine_set(Machine *m, MachineSetting setting, bool on)
{
    if (m->kind == MACHINE_UNIVERSAL) uni_set(m->uni, uni_settings[setting], on);
    else if (m->kind == MACHINE_STERN) stern_set(m->stern, stern_settings[setting], on);
    else if (m->kind == MACHINE_KONAMI && setting != MACHINE_UNLIMITED_LIVES)
        konami_set(m->konami, setting == MACHINE_FREE_PLAY ? KONAMI_FREE_PLAY : KONAMI_QUIET_ATTRACT, on);
    else if (m->kind == MACHINE_RDI && setting != MACHINE_UNLIMITED_LIVES)
        rdi_set(m->rdi, setting == MACHINE_FREE_PLAY ? RDI_FREE_PLAY : RDI_QUIET_ATTRACT, on);
    else if (m->kind == MACHINE_SEGA && setting != MACHINE_UNLIMITED_LIVES)
        sega_set(m->sega, setting == MACHINE_FREE_PLAY ? SEGA_FREE_PLAY : SEGA_QUIET_ATTRACT, on);
    else if (m->kind == MACHINE_DATAEAST && setting != MACHINE_UNLIMITED_LIVES)
        deco_set(m->deco, setting == MACHINE_FREE_PLAY ? DECO_FREE_PLAY : DECO_QUIET_ATTRACT, on);
    else if (m->kind == MACHINE_GOTTLIEB && setting == MACHINE_FREE_PLAY)
        gottlieb_set(m->gottlieb, GOTTLIEB_FREE_PLAY, on);
    else if (m->kind == MACHINE_GPWORLD && setting == MACHINE_QUIET_ATTRACT)
        gpw_set_quiet_attract(m->gpw, on);
    else if (m->kind == MACHINE_CINEMATRONICS) cine_set(m->cine, cine_settings[setting], on);
}

bool machine_get(const Machine *m, MachineSetting setting)
{
    if (m->kind == MACHINE_UNIVERSAL) return uni_get(m->uni, uni_settings[setting]);
    if (m->kind == MACHINE_FUNAI || m->kind == MACHINE_LELAND) return false;
    if (m->kind == MACHINE_GOTTLIEB)
        return setting == MACHINE_FREE_PLAY && gottlieb_get(m->gottlieb, GOTTLIEB_FREE_PLAY);
    if (m->kind == MACHINE_GPWORLD) return setting == MACHINE_QUIET_ATTRACT && gpw_quiet_attract(m->gpw);
    if (m->kind == MACHINE_STERN) return stern_get(m->stern, stern_settings[setting]);
    if (m->kind == MACHINE_RDI) {
        if (setting == MACHINE_UNLIMITED_LIVES) return false;
        return rdi_get(m->rdi, setting == MACHINE_FREE_PLAY ? RDI_FREE_PLAY : RDI_QUIET_ATTRACT);
    }
    if (m->kind == MACHINE_SEGA) {
        if (setting == MACHINE_UNLIMITED_LIVES) return false;
        return sega_get(m->sega, setting == MACHINE_FREE_PLAY ? SEGA_FREE_PLAY : SEGA_QUIET_ATTRACT);
    }
    if (m->kind == MACHINE_DATAEAST) {
        if (setting == MACHINE_UNLIMITED_LIVES) return false;
        return deco_get(m->deco, setting == MACHINE_FREE_PLAY ? DECO_FREE_PLAY : DECO_QUIET_ATTRACT);
    }
    if (m->kind == MACHINE_KONAMI) {
        if (setting == MACHINE_UNLIMITED_LIVES) return false;
        return konami_get(m->konami, setting == MACHINE_FREE_PLAY ? KONAMI_FREE_PLAY : KONAMI_QUIET_ATTRACT);
    }
    return cine_get(m->cine, cine_settings[setting]);
}

int machine_sample_rate(const Machine *m)
{
    if (m->kind == MACHINE_UNIVERSAL) return UNI_SAMPLE_RATE;
    if (m->kind == MACHINE_FUNAI) return FUNAI_SAMPLE_RATE;
    if (m->kind == MACHINE_STERN) return STERN_SAMPLE_RATE;
    if (m->kind == MACHINE_KONAMI) return KONAMI_SAMPLE_RATE;
    if (m->kind == MACHINE_SEGA) return SEGA_SAMPLE_RATE;
    if (m->kind == MACHINE_RDI) return RDI_SAMPLE_RATE;
    if (m->kind == MACHINE_DATAEAST) return DECO_SAMPLE_RATE;
    if (m->kind == MACHINE_LELAND) return LELAND_SAMPLE_RATE;
    if (m->kind == MACHINE_GOTTLIEB) return GOTTLIEB_SAMPLE_RATE;
    if (m->kind == MACHINE_GPWORLD) return GPW_SAMPLE_RATE;
    return CINE_SAMPLE_RATE;
}

void machine_start_sound(Machine *m)
{
    if (m->kind == MACHINE_UNIVERSAL) sn76496_set_rendering(&m->uni->sound, true, m->uni->cycles);
    else if (m->kind == MACHINE_FUNAI) funai_start_sound(m->funai);
    else if (m->kind == MACHINE_STERN) stern_start_sound(m->stern);
    else if (m->kind == MACHINE_KONAMI) sn76496_set_rendering(&m->konami->sound, true, m->konami->cycles);
    else if (m->kind == MACHINE_SEGA) samples_start(&m->sega->sound, m->sega->cycles);
    else if (m->kind == MACHINE_RDI) ssi263_start(&m->rdi->speech, m->rdi->cycles);
    else if (m->kind == MACHINE_DATAEAST) deco_start_sound(m->deco);
    else if (m->kind == MACHINE_LELAND) leland_start_sound(m->leland);
    else if (m->kind == MACHINE_GOTTLIEB) gottlieb_start_sound(m->gottlieb);
    else if (m->kind == MACHINE_GPWORLD) {}  // no sound (see games/sega/gpworld.h)
    else ay8910_set_rendering(&m->cine->ay, true, m->cine->cycles);
}

int machine_take_samples(Machine *m, const float **samples)
{
    if (m->kind == MACHINE_UNIVERSAL) {
        sn76496_run(&m->uni->sound, m->uni->cycles);
        return sn76496_take_samples(&m->uni->sound, samples);
    }
    if (m->kind == MACHINE_FUNAI) return funai_take_samples(m->funai, samples);
    if (m->kind == MACHINE_STERN) return stern_take_samples(m->stern, samples);
    if (m->kind == MACHINE_DATAEAST) return deco_take_samples(m->deco, samples);
    if (m->kind == MACHINE_LELAND) return leland_take_samples(m->leland, samples);
    if (m->kind == MACHINE_GOTTLIEB) return gottlieb_take_samples(m->gottlieb, samples);
    if (m->kind == MACHINE_GPWORLD) return 0;
    if (m->kind == MACHINE_RDI) {
        ssi263_run(&m->rdi->speech, m->rdi->cycles);
        return ssi263_take_samples(&m->rdi->speech, samples);
    }
    if (m->kind == MACHINE_SEGA) {
        samples_run(&m->sega->sound, m->sega->cycles);
        return samples_take(&m->sega->sound, samples);
    }
    if (m->kind == MACHINE_KONAMI) {
        sn76496_run(&m->konami->sound, m->konami->cycles);
        return sn76496_take_samples(&m->konami->sound, samples);
    }
    ay8910_run(&m->cine->ay, m->cine->cycles);
    return ay8910_take_samples(&m->cine->ay, samples);
}

bool machine_overlay_layout(const Machine *m, OverlayLayout *layout)
{
    if (m->kind == MACHINE_STERN) {
        // The video chip's picture and border, as much as a TV shows.
        *layout = (OverlayLayout){.width = TMS9928_W, .height = TMS9928_H, .x = 0, .y = 0, .w = 1, .h = 1};
        return true;
    }
    if (m->kind == MACHINE_SEGA) {
        *layout = (OverlayLayout){.width = SEGA_OVERLAY_W, .height = SEGA_OVERLAY_H, .x = 0, .y = 0, .w = 1, .h = 1};
        return true;
    }
    if (m->kind == MACHINE_DATAEAST) {
        *layout = (OverlayLayout){.width = DECO_OVERLAY_W, .height = DECO_OVERLAY_H, .x = 0, .y = 0, .w = 1, .h = 1};
        return true;
    }
    if (m->kind == MACHINE_LELAND) {
        // The player's text, over the whole picture.
        *layout = (OverlayLayout){.width = LDP1450_TEXT_W, .height = LDP1450_TEXT_H, .x = 0, .y = 0, .w = 1, .h = 1};
        return true;
    }
    if (m->kind == MACHINE_GPWORLD) {
        *layout = (OverlayLayout){.width = GPW_OVERLAY_W, .height = GPW_OVERLAY_H, .x = 0, .y = 0, .w = 1, .h = 1};
        return true;
    }
    if (m->kind == MACHINE_GOTTLIEB) {
        *layout = (OverlayLayout){.width = GOTTLIEB_OVERLAY_W, .height = GOTTLIEB_OVERLAY_H, .x = 0, .y = 0, .w = 1, .h = 1};
        return true;
    }
    if (m->kind == MACHINE_KONAMI) {
        // The visible part of the tile grid covers the picture.
        *layout = (OverlayLayout){.width = KONAMI_OVERLAY_W, .height = KONAMI_OVERLAY_H, .x = 0, .y = 0, .w = 1, .h = 1};
        return true;
    }
    if (m->kind == MACHINE_FUNAI) {
        // The tiles cover the whole picture, as far as is known.
        *layout = (OverlayLayout){.width = FUNAI_OVERLAY_W, .height = FUNAI_OVERLAY_H, .x = 0, .y = 0, .w = 1, .h = 1};
        return true;
    }
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
    if (m->kind == MACHINE_UNIVERSAL) return m->uni->overlay_serial;
    if (m->kind == MACHINE_FUNAI) return m->funai->overlay_serial;
    if (m->kind == MACHINE_STERN) return m->stern->vdp.serial;
    if (m->kind == MACHINE_KONAMI) return m->konami->overlay_serial;
    if (m->kind == MACHINE_SEGA) return m->sega->overlay_serial;
    if (m->kind == MACHINE_DATAEAST) return m->deco->overlay_serial;
    if (m->kind == MACHINE_LELAND) return m->ldp1000.text_serial;
    if (m->kind == MACHINE_GOTTLIEB) return m->gottlieb->overlay_serial;
    if (m->kind == MACHINE_GPWORLD) return m->gpw->overlay_serial;
    return 0;
}

void machine_draw_overlay(const Machine *m, uint32_t *pixels)
{
    if (m->kind == MACHINE_UNIVERSAL) uni_draw_overlay(m->uni, pixels);
    else if (m->kind == MACHINE_FUNAI) funai_draw_overlay(m->funai, pixels);
    else if (m->kind == MACHINE_STERN) stern_draw_overlay(m->stern, pixels);
    else if (m->kind == MACHINE_KONAMI) konami_draw_overlay(m->konami, pixels);
    else if (m->kind == MACHINE_SEGA) sega_draw_overlay(m->sega, pixels);
    else if (m->kind == MACHINE_DATAEAST) deco_draw_overlay(m->deco, pixels);
    else if (m->kind == MACHINE_LELAND) ldp1450_draw_text(&m->ldp1000, pixels);
    else if (m->kind == MACHINE_GOTTLIEB) gottlieb_draw_overlay(m->gottlieb, pixels);
    else if (m->kind == MACHINE_GPWORLD) gpw_draw_overlay(m->gpw, pixels);
}
