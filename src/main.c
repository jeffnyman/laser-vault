// laser-vault: a laserdisc arcade game emulator.
//
// Usage: laser-vault FRAMEFILE [--video-dir DIR]
//                    [--roms ROMS [--fast-boot] [--attract-sound] [--scores]
//                                 [--unlimited-lives] [--free-play]
//                                 [--dsw1 HEX] ... [--dsw4 HEX] [--autoplay]]
//                    [--frame N] [--disc-fps F] [--icon FILE.ico] [--fullscreen]
//
// The window icons are built in (icons/); --icon uses a
// Windows .ico file instead. F11 or Alt+Enter switches fullscreen on and
// off in either mode; --fullscreen starts that way. F12 saves a screenshot
// (a PNG of the picture, and the score panel if it's shown) in the current
// folder.
//
// With --roms (a ROM set such as lair.zip, sae.zip or cliff.zip, or a
// folder of ROM files), it runs the game: Dragon's Lair, Space Ace, Super
// Don Quix-ote, Esh's Aurunmilla or Cliff Hanger, whichever the ROMs hold
// (see games/machine.h). The game's CPU drives its laserdisc player, and the
// keyboard is the control panel. --fast-boot skips the wait while the game spins up the disc,
// starting straight in the attract mode. --attract-sound plays sound in
// every attract cycle (by default, as on the real machine, Dragon's Lair
// and Space Ace only do every few cycles). --scores shows Dragon's Lair's
// and Space Ace's score displays, which on the real cabinet are in the
// bezel, beside the picture. --unlimited-lives turns on the game's own
// unlimited lives setting: the hero still dies, but never loses a life.
// --free-play needs no coins. --dsw1 and --dsw2 set the game's DIP switches
// directly (each bank as a hex byte, replacing the ROM set's defaults; Cliff
// Hanger also has --dsw3 and --dsw4); the options above then change their
// own switches on top. --autoplay lets the
// program play Dragon's Lair or Space Ace (see
// games/cinematronics/autoplay.h); A switches it on and off during a game.
//
//   Arrow keys          joystick
//   Space / Ctrl        action button (the sword)
//   5 / 6               insert a coin (slot 1 / 2)
//   1 / 2               start (1 / 2 players)
//   F1 / F2 / F3        Space Ace: choose Cadet / Captain / Ace
//   S                   show / hide the score displays
//   P / Pause           pause / resume the game
//   A                   autoplay on / off
//   D                   turn combing repair off / on (see video/decomb.h)
//   Escape              quit
//
// Game controllers work too: the d-pad or left stick is the joystick, any
// face button the action button, Back (Select) inserts a coin, Start starts.
//
// Without --roms, you're the game: drive the laserdisc player yourself.
//
//   Space               play / pause
//   S                   stop
//   Left / Right        step one frame back / forward (pauses)
//   Down / Up           search one second back / forward
//   Page Down / Page Up search ten seconds back / forward
//   Home / End          search to the first / last clip
//   D                   turn combing repair off / on (see video/decomb.h)
//   0-9, Enter          type a frame number and search to it
//   Escape              cancel typing, or quit

#include "audio/disc_audio.h"
#include "games/cinematronics/autoplay.h"
#include "games/machine.h"
#include "ldp/ldp.h"
#include "ldp/ldv1000.h"
#include "ui/bezel.h"
#include "ui/icon.h"
#include "util/png.h"
#include "video/disc_video.h"
#include "video/framefile.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// The rate the disc frame numbers count at, without ROMs to say (with them,
// see machine_disc_fps). Dragon's Lair's frame numbers count 24 fps film
// frames; see video/disc_video.h.
#define DEFAULT_DISC_FPS 23.976

// The picture's area in the renderer's coordinates (a 4:3 screen).
#define VIDEO_W 640
#define VIDEO_H 480

typedef struct {
    Framefile framefile;
    DiscVideo *video;
    DiscAudio *audio;  // NULL if there's no sound device
    SDL_AudioStream *chip_sound;  // the game board's sound chip, or NULL
    Ldp ldp;

    // Game mode: the machine, whose laserdisc player drives ldp.
    bool game;
    Machine machine;
    Controls controls; // the control panel, as last read
    bool show_scores;  // the score display panel beside the picture (Cinematronics)
    bool paused;       // game mode: everything frozen
    bool screenshot;   // save the next frame drawn
    bool autoplaying;  // game mode: the program plays the joystick and sword
    CineAutoplay autoplay;
    SDL_Thread *autoplay_thread;    // planning the next move, or NULL
    CineAutoplayJob *autoplay_job;  // what it's planning
    SDL_AtomicInt autoplay_done;    // set by the thread when the plan is ready

    // What the board draws over the picture, if it does.
    bool has_overlay;
    OverlayLayout overlay_layout;
    uint32_t *overlay_pixels;
    SDL_Texture *overlay;
    unsigned overlay_serial;  // the board's serial when it was last drawn

    SDL_Gamepad *gamepads[8];  // game mode: controllers plugged in (NULL = free slot)

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;  // created to match the video's size
    int texture_frame;     // disc frame currently in the texture, or -1

    int fps;            // disc frames per second, rounded (for skipping)
    char typed[16];     // frame number being typed, "" when not typing
    char title[256];    // current window title
} App;

// A DIP switch bank, written as a hex byte ("79", "0x79"). Returns -2 if
// it isn't one.
static int parse_hex_byte(const char *s)
{
    char *end;
    long v = strtol(s, &end, 16);
    return (*s && *end == '\0' && v >= 0 && v <= 0xFF) ? (int)v : -2;
}

static void usage(void)
{
    fprintf(stderr, "usage: laser-vault FRAMEFILE [--video-dir DIR] [--roms ROMS]\n"
                    "                   [--fast-boot] [--attract-sound] [--scores] [--unlimited-lives]\n"
                    "                   [--free-play] [--dsw1 HEX] ... [--dsw4 HEX] [--autoplay]\n"
                    "                   [--frame N] [--disc-fps F] [--icon FILE.ico] [--fullscreen]\n");
}

static void update_title(App *app)
{
    const Ldp *ldp = &app->ldp;
    char title[sizeof app->title];

    if (app->game) {
        // The score display (if the board has one), then what the player is
        // doing.
        char scores[32] = "";
        if (app->machine.kind == MACHINE_CINEMATRONICS) {
            static const char glyphs[] = "0123456789AbCdE ";
            char digits[17];
            for (int i = 0; i < 16; i++) digits[i] = glyphs[app->machine.cine->digits[i]];
            digits[16] = '\0';
            snprintf(scores, sizeof scores, " - [%.8s] [%.8s]", digits, digits + 8);
        }
        snprintf(title, sizeof title, "%s%s - %s, frame %d%s%s%s",
                 machine_game_name(&app->machine), scores, ldp_state_name(ldp->state), ldp->frame,
                 app->paused ? "  [paused]" : "",
                 app->autoplaying ? "  [autoplay]" : "",
                 disc_video_decomb(app->video) ? "" : "  [combing repair off]");
    } else if (app->typed[0]) {
        snprintf(title, sizeof title, "laser-vault - search to frame: %s_", app->typed);
    } else if (ldp->state == LDP_SEARCHING) {
        snprintf(title, sizeof title, "laser-vault - searching to frame %d", ldp->search_target);
    } else {
        int offset = 0;
        const FramefileClip *clip = framefile_find(&app->framefile, ldp->frame, &offset);
        const char *name = "(no clip)";
        if (clip) {
            const char *slash = strrchr(clip->path, '/');
            const char *backslash = strrchr(clip->path, '\\');
            if (backslash > slash) slash = backslash;
            name = slash ? slash + 1 : clip->path;
        }
        snprintf(title, sizeof title, "laser-vault - %s, frame %d  (%s +%d)%s",
                 ldp_state_name(ldp->state), ldp->frame, name, offset,
                 disc_video_decomb(app->video) ? "" : "  [combing repair off]");
    }

    // Titles change up to 24 times a second while playing; only touch the
    // window when there's something new to say.
    if (strcmp(title, app->title) != 0) {
        strcpy(app->title, title);
        SDL_SetWindowTitle(app->window, title);
    }
}

// Copy a decoded MPEG picture into the SDL texture.
static void upload_picture(App *app, const AVFrame *pic)
{
    // MPEG-2 video is stored as YUV 4:2:0: a full-size brightness (Y) plane
    // plus two quarter-size colour planes (U, V). SDL can draw that directly.
    if (pic->format != AV_PIX_FMT_YUV420P) {
        fprintf(stderr, "unexpected pixel format %d\n", pic->format);
        return;
    }
    if (!app->texture) {
        app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_IYUV,
                                         SDL_TEXTUREACCESS_STREAMING, pic->width, pic->height);
    }
    SDL_UpdateYUVTexture(app->texture, NULL,
                         pic->data[0], pic->linesize[0],
                         pic->data[1], pic->linesize[1],
                         pic->data[2], pic->linesize[2]);
}

// "PAUSED" across the middle of the picture, on a translucent band.
static void draw_paused_banner(SDL_Renderer *r)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_RenderFillRect(r, &(SDL_FRect){0, VIDEO_H / 2 - 30, VIDEO_W, 60});
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // SDL's built-in font is 8x8 pixels; draw it three times as big.
    const float scale = 3, text_w = 6 * 8 * scale, text_h = 8 * scale;
    SDL_SetRenderScale(r, scale, scale);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, (VIDEO_W - text_w) / 2 / scale, (VIDEO_H - text_h) / 2 / scale, "PAUSED");
    SDL_SetRenderScale(r, 1, 1);
}

// Save what's drawn in the window as a PNG: the picture (and the score panel,
// if shown) at the size it fills the window, without the bars around it.
// Named for the date and time, in the current folder.
static void save_screenshot(App *app)
{
    SDL_Renderer *r = app->renderer;
    // Read the window's real pixels, so step out of the logical coordinates
    // for a moment.
    SDL_FRect area;
    SDL_GetRenderLogicalPresentationRect(r, &area);
    int logical_w, logical_h;
    SDL_RendererLogicalPresentation mode;
    SDL_GetRenderLogicalPresentation(r, &logical_w, &logical_h, &mode);
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_Rect rect = {(int)(area.x + 0.5f), (int)(area.y + 0.5f), (int)(area.w + 0.5f), (int)(area.h + 0.5f)};
    SDL_Surface *shot = SDL_RenderReadPixels(r, &rect);
    SDL_SetRenderLogicalPresentation(r, logical_w, logical_h, mode);
    SDL_Surface *rgb = shot ? SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGB24) : NULL;
    SDL_DestroySurface(shot);
    if (!rgb) {
        fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
        return;
    }

    // RGB rows may be padded; the PNG writer wants them packed.
    size_t row_bytes = (size_t)rgb->w * 3;
    uint8_t *pixels = malloc(row_bytes * (size_t)rgb->h);
    for (int y = 0; y < rgb->h; y++)
        memcpy(pixels + row_bytes * (size_t)y, (const uint8_t *)rgb->pixels + (size_t)y * (size_t)rgb->pitch, row_bytes);

    char name[64];
    time_t now = time(NULL);
    strftime(name, sizeof name, "laser-vault-%Y%m%d-%H%M%S.png", localtime(&now));
    for (int n = 2; n < 100; n++) {  // two in the same second: add a number
        FILE *f = fopen(name, "rb");
        if (!f) break;
        fclose(f);
        strftime(name, sizeof name, "laser-vault-%Y%m%d-%H%M%S", localtime(&now));
        snprintf(name + strlen(name), sizeof name - strlen(name), "-%d.png", n);
    }
    if (png_write(name, pixels, rgb->w, rgb->h)) printf("Screenshot: %s (%dx%d)\n", name, rgb->w, rgb->h);
    fflush(stdout);
    free(pixels);
    SDL_DestroySurface(rgb);
}

// The board's graphics, over the picture where they belong. They're redrawn
// only when the board says they may have changed. (They show even while the
// player is searching, when the picture is blank, as on the real machine.)
static void draw_overlay(App *app)
{
    const OverlayLayout *l = &app->overlay_layout;
    if (!app->overlay) {
        app->overlay = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         l->width, l->height);
        if (!app->overlay) {
            fprintf(stderr, "Cannot draw the overlay: %s\n", SDL_GetError());
            app->has_overlay = false;
            return;
        }
        SDL_SetTextureBlendMode(app->overlay, SDL_BLENDMODE_BLEND);
        // Its pixels are big and blocky; keep them crisp rather than blurred.
        SDL_SetTextureScaleMode(app->overlay, SDL_SCALEMODE_PIXELART);
        app->overlay_serial = machine_overlay_serial(&app->machine) - 1;  // draw it the first time
    }
    unsigned serial = machine_overlay_serial(&app->machine);
    if (serial != app->overlay_serial) {
        machine_draw_overlay(&app->machine, app->overlay_pixels);
        SDL_UpdateTexture(app->overlay, NULL, app->overlay_pixels, l->width * (int)sizeof(uint32_t));
        app->overlay_serial = serial;
    }
    SDL_RenderTexture(app->renderer, app->overlay, NULL,
                      &(SDL_FRect){l->x * VIDEO_W, l->y * VIDEO_H, l->w * VIDEO_W, l->h * VIDEO_H});
}

static void draw(App *app)
{
    const Ldp *ldp = &app->ldp;

    // Decode only when the player has moved to a new frame. While searching,
    // ldp->frame stays on the old frame, so the last picture is held (unless
    // the player blanks the screen while searching, as the LD-V1000 does).
    if (ldp_has_picture(ldp) && ldp->frame != app->texture_frame) {
        const AVFrame *pic = disc_video_get_frame(app->video, ldp->frame);
        if (pic) upload_picture(app, pic);
        app->texture_frame = pic ? ldp->frame : -1;
    }

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    if (ldp_has_picture(ldp) && app->texture_frame >= 0)
        SDL_RenderTexture(app->renderer, app->texture, NULL, &(SDL_FRect){0, 0, VIDEO_W, VIDEO_H});
    if (app->has_overlay) draw_overlay(app);
    if (app->game && app->show_scores && app->machine.kind == MACHINE_CINEMATRONICS) {
        // Space Ace writes A for a blank digit, so only its digits are shown,
        // along with its skill-level lamps (whose signals the board takes
        // off the display; see games/cinematronics/board.c).
        const CineBoard *board = app->machine.cine;
        uint8_t digits[16];
        memcpy(digits, board->digits, sizeof digits);
        int lamps = -1;
        if (board->game == CINE_SPACE_ACE) {
            for (int i = 0; i < 16; i++)
                if (digits[i] > 9) digits[i] = 15;
            lamps = board->lamps;
        }
        bezel_draw(app->renderer, VIDEO_W, 0, digits, lamps);
    }
    // (Before the PAUSED banner, so pausing to line up a shot keeps it out.)
    if (app->screenshot) {
        save_screenshot(app);
        app->screenshot = false;
    }
    if (app->paused) draw_paused_banner(app->renderer);
    SDL_RenderPresent(app->renderer);
}

// Lay out the window: the picture, plus the score panel to its right when
// it's shown. Keeps the window's height and adjusts its width to fit.
static void set_layout(App *app)
{
    bool scores = app->game && app->show_scores && app->machine.kind == MACHINE_CINEMATRONICS;
    int width = VIDEO_W + (scores ? BEZEL_WIDTH : 0);
    // Laserdisc video is 720x480 pixels but is shown on a 4:3 screen, so the
    // pixels aren't square. Drawing it into a 640x480 area of the logical
    // canvas stretches it to the right shape; letterboxing keeps the
    // proportions when the window is resized.
    SDL_SetRenderLogicalPresentation(app->renderer, width, VIDEO_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    // Fullscreen, the letterboxing does it all. (The main loop lays out the
    // window again on leaving fullscreen.)
    if (SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN) return;
    int w, h;
    SDL_GetWindowSize(app->window, &w, &h);
    SDL_SetWindowSize(app->window, h * width / VIDEO_H, h);
}

static void toggle_fullscreen(App *app)
{
    bool full = !(SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN);
    SDL_SetWindowFullscreen(app->window, full);
    if (full) SDL_HideCursor();
    else SDL_ShowCursor();
}

// Search relative to where the player is heading, so pressing Up twice
// quickly skips two seconds even though the first search hasn't finished.
static void search_by(App *app, int delta)
{
    const Ldp *ldp = &app->ldp;
    int from = ldp->state == LDP_SEARCHING ? ldp->search_target : ldp->frame;
    ldp_search(&app->ldp, from + delta);
}

// Game controllers come and go while the program runs. (Those already
// plugged in at start-up arrive as "added" events too.)
static void gamepad_added(App *app, SDL_JoystickID id)
{
    for (int i = 0; i < (int)SDL_arraysize(app->gamepads); i++) {
        if (!app->gamepads[i]) {
            app->gamepads[i] = SDL_OpenGamepad(id);
            if (app->gamepads[i]) printf("Gamepad connected: %s\n", SDL_GetGamepadName(app->gamepads[i]));
            return;
        }
    }
}

static void gamepad_removed(App *app, SDL_JoystickID id)
{
    for (int i = 0; i < (int)SDL_arraysize(app->gamepads); i++) {
        if (app->gamepads[i] && SDL_GetGamepadID(app->gamepads[i]) == id) {
            printf("Gamepad disconnected: %s\n", SDL_GetGamepadName(app->gamepads[i]));
            SDL_CloseGamepad(app->gamepads[i]);
            app->gamepads[i] = NULL;
        }
    }
}

// Add a controller to the control panel: the d-pad or left stick is the
// joystick, any face button the action button, Back (Select) inserts a coin
// and Start starts a one-player game. For Space Ace's skill levels, the left
// shoulder button is Cadet, the right one Captain and the right trigger Ace.
static void read_gamepad(SDL_Gamepad *pad, Controls *in)
{
    const int tilt = 16384;  // the stick counts as pushed half way out
    int x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    int y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    in->up = in->up || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -tilt;
    in->down = in->down || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > tilt;
    in->left = in->left || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -tilt;
    in->right = in->right || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > tilt;
    // Bottom and left face buttons are the first button, right and top the
    // second; a game with one button takes either.
    in->button = in->button || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH) ||
                SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST);
    in->button2 = in->button2 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) ||
                 SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH);
    in->coin1 = in->coin1 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
    in->start1 = in->start1 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
    in->skill[0] = in->skill[0] || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    in->skill[1] = in->skill[1] || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    in->skill[2] = in->skill[2] || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > tilt;
}

// Autoplay plans each move on its own thread, so the picture never waits
// for it (see games/cinematronics/autoplay.h).
static int SDLCALL autoplay_thread(void *data)
{
    App *app = data;
    cine_autoplay_think(app->autoplay_job);
    SDL_SetAtomicInt(&app->autoplay_done, 1);
    return 0;
}

// Collect the thread's plan, if it's ready (or wait for it). The plan is
// used only if use is true: not when autoplay has just been switched off.
static void finish_autoplay_thread(App *app, bool wait, bool use)
{
    if (!app->autoplay_thread) return;
    if (!wait && !SDL_GetAtomicInt(&app->autoplay_done)) return;
    SDL_WaitThread(app->autoplay_thread, NULL);
    app->autoplay_thread = NULL;
    SDL_SetAtomicInt(&app->autoplay_done, 0);
    if (use) cine_autoplay_answer(&app->autoplay, app->autoplay_job);
}

// Run the board for the given number of cycles. With autoplay on, stop at
// each of its presses and releases, so every move lands on the cycle it was
// planned for, and start planning whenever the game writes new moves.
static void run_board(App *app, uint64_t cycles)
{
    Machine *m = &app->machine;
    uint64_t end = machine_cycles(m) + cycles;
    while (machine_cycles(m) < end) {
        uint64_t stop = end;
        if (app->autoplaying) {
            finish_autoplay_thread(app, false, true);
            CineAutoplayJob *job = cine_autoplay_update(&app->autoplay, m->cine, &app->ldp, &m->ldv1000);
            if (job) {
                app->autoplay_job = job;
                app->autoplay_thread = SDL_CreateThread(autoplay_thread, "autoplay", app);
                if (!app->autoplay_thread) {  // no threads: plan here, and wait
                    cine_autoplay_think(job);
                    cine_autoplay_answer(&app->autoplay, job);
                    continue;
                }
            }
            uint64_t next = cine_autoplay_next_event(&app->autoplay);
            if (next > m->cine->cycles && next < stop) stop = next;
        }
        machine_run(m, stop - machine_cycles(m));
        machine_update_player(m);
    }
}

// The control panel, read from the keyboard and any controllers every pass
// of the main loop.
static void read_game_inputs(App *app)
{
    // Number keys work on the top row or the numeric keypad.
    const bool *key = SDL_GetKeyboardState(NULL);
    Controls *in = &app->controls;
    Controls before = *in;
    in->up = key[SDL_SCANCODE_UP];
    in->down = key[SDL_SCANCODE_DOWN];
    in->left = key[SDL_SCANCODE_LEFT];
    in->right = key[SDL_SCANCODE_RIGHT];
    in->button = key[SDL_SCANCODE_SPACE] || key[SDL_SCANCODE_LCTRL] || key[SDL_SCANCODE_RCTRL];
    in->button2 = key[SDL_SCANCODE_LSHIFT] || key[SDL_SCANCODE_RSHIFT];
    in->coin1 = key[SDL_SCANCODE_5] || key[SDL_SCANCODE_KP_5];
    in->coin2 = key[SDL_SCANCODE_6] || key[SDL_SCANCODE_KP_6];
    in->start1 = key[SDL_SCANCODE_1] || key[SDL_SCANCODE_KP_1];
    in->start2 = key[SDL_SCANCODE_2] || key[SDL_SCANCODE_KP_2];
    in->skill[0] = key[SDL_SCANCODE_F1];
    in->skill[1] = key[SDL_SCANCODE_F2];
    in->skill[2] = key[SDL_SCANCODE_F3];
    // (A pad just unplugged reads its stick as pushed all the way up until
    // the event saying it's gone arrives, so check it's still there.)
    for (int i = 0; i < (int)SDL_arraysize(app->gamepads); i++)
        if (app->gamepads[i] && SDL_GamepadConnected(app->gamepads[i])) read_gamepad(app->gamepads[i], in);

    machine_set_controls(&app->machine, in);

    // Say so on the console when a button goes down or the stick moves, so
    // it's easy to tell whether the keyboard is reaching the game.
    double t = (double)machine_cycles(&app->machine) / machine_cpu_hz(&app->machine);
    if (in->coin1 && !before.coin1) printf("%6.1f s  coin inserted (slot 1)\n", t);
    if (in->coin2 && !before.coin2) printf("%6.1f s  coin inserted (slot 2)\n", t);
    if (in->start1 && !before.start1) printf("%6.1f s  start (1 player)\n", t);
    if (in->start2 && !before.start2) printf("%6.1f s  start (2 players)\n", t);
    if (in->up && !before.up) printf("%6.1f s  stick: up\n", t);
    if (in->down && !before.down) printf("%6.1f s  stick: down\n", t);
    if (in->left && !before.left) printf("%6.1f s  stick: left\n", t);
    if (in->right && !before.right) printf("%6.1f s  stick: right\n", t);
    const char *button = app->machine.kind == MACHINE_CINEMATRONICS ? "sword" : "button";
    if (app->machine.kind == MACHINE_FUNAI || app->machine.kind == MACHINE_STERN) {
        bool stern = app->machine.kind == MACHINE_STERN;
        if (in->button && !before.button) printf("%6.1f s  %s\n", t, stern ? "hands" : "button 1");
        if (in->button2 && !before.button2) printf("%6.1f s  %s\n", t, stern ? "feet" : "button 2");
    } else if ((in->button || in->button2) && !(before.button || before.button2)) {
        printf("%6.1f s  %s\n", t, button);
    }
    if (app->machine.kind == MACHINE_CINEMATRONICS && app->machine.cine->game == CINE_SPACE_ACE) {
        static const char *skills[3] = {"Cadet", "Captain", "Ace"};
        for (int i = 0; i < 3; i++)
            if (in->skill[i] && !before.skill[i]) printf("%6.1f s  skill: %s\n", t, skills[i]);
    }
    fflush(stdout);
}

// Run the game as fast as possible, without drawing, until the attract mode
// starts: the game's first search has finished and the disc is playing.
// That skips the ~35 s the game spends spinning up the disc. Gives up after
// two minutes of game time, in case the game never gets there.
static void fast_boot(App *app)
{
    Machine *m = &app->machine;
    double hz = machine_cpu_hz(m);
    const uint64_t limit = (uint64_t)(120 * hz);
    bool searched = false;
    while (machine_cycles(m) < limit) {
        machine_run(m, (uint64_t)(hz / 100));  // 10 ms; searches take longer
        machine_update_player(m);
        if (app->ldp.state == LDP_SEARCHING) searched = true;
        else if (searched && app->ldp.state == LDP_PLAYING) break;
    }
    printf("Fast boot: skipped %.1f s of start-up.\n", (double)machine_cycles(m) / hz);
}

// Pass the sound chip's newly generated samples to the speakers. The chip
// makes sound at the pace of the emulated board, the speakers take it at the
// pace of the real clock; if they drift apart and too much piles up, drop it
// rather than let the beeps lag further and further behind.
static void play_chip_sound(App *app)
{
    const float *samples;
    int n = machine_take_samples(&app->machine, &samples);
    int max_bytes = machine_sample_rate(&app->machine) / 4 * (int)sizeof(float);  // 0.25 s
    if (SDL_GetAudioStreamQueued(app->chip_sound) > max_bytes) SDL_ClearAudioStream(app->chip_sound);
    if (n > 0) SDL_PutAudioStreamData(app->chip_sound, samples, n * (int)sizeof(float));
}

static void toggle_decomb(App *app)
{
    disc_video_set_decomb(app->video, !disc_video_decomb(app->video));
    app->texture_frame = -1;  // fetch the picture again
}

// Returns false when the app should quit.
static bool handle_key(App *app, SDL_Keycode key, SDL_Keymod mod)
{
    if (key == SDLK_F11 || (key == SDLK_RETURN && (mod & SDL_KMOD_ALT))) {
        toggle_fullscreen(app);
        return true;
    }
    if (key == SDLK_F12) {
        app->screenshot = true;  // taken when the next frame is drawn
        return true;
    }
    if (app->game) {  // the rest of the keyboard is the control panel
        if (key == SDLK_ESCAPE) return false;
        if (key == SDLK_D) toggle_decomb(app);
        if (key == SDLK_S && app->machine.kind == MACHINE_CINEMATRONICS) {
            app->show_scores = !app->show_scores;
            set_layout(app);
        }
        if (key == SDLK_P || key == SDLK_PAUSE) {
            app->paused = !app->paused;
            // Cut the sound at once, including what's already queued. (The
            // soundtrack is silenced by the main loop and picks up from the
            // right place on resume.)
            if (app->paused && app->chip_sound) SDL_ClearAudioStream(app->chip_sound);
        }
        if (key == SDLK_A && app->machine.kind == MACHINE_CINEMATRONICS) {
            app->autoplaying = !app->autoplaying;
            // A plan in the works is for the moment autoplay stopped; drop it.
            finish_autoplay_thread(app, true, false);
            if (app->autoplaying) cine_autoplay_resume(&app->autoplay);
            printf("Autoplay %s\n", app->autoplaying ? "on" : "off");
            fflush(stdout);
        }
        return true;
    }

    size_t typed_len = strlen(app->typed);
    const Framefile *ff = &app->framefile;
    Ldp *ldp = &app->ldp;

    if (key >= SDLK_0 && key <= SDLK_9) {
        if (typed_len + 1 < sizeof app->typed) {
            app->typed[typed_len] = (char)('0' + (key - SDLK_0));
            app->typed[typed_len + 1] = '\0';
        }
        return true;
    }

    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (typed_len) {
            ldp_search(ldp, atoi(app->typed));
            app->typed[0] = '\0';
        }
        break;
    case SDLK_BACKSPACE:
        if (typed_len) app->typed[typed_len - 1] = '\0';
        break;
    case SDLK_ESCAPE:
        if (!typed_len) return false;
        app->typed[0] = '\0';
        break;
    case SDLK_SPACE:
        if (ldp->state == LDP_PLAYING) ldp_pause(ldp);
        else ldp_play(ldp);
        break;
    case SDLK_S:        ldp_stop(ldp); break;
    case SDLK_D:        toggle_decomb(app); break;
    case SDLK_RIGHT:    ldp_step(ldp, +1); break;
    case SDLK_LEFT:     ldp_step(ldp, -1); break;
    case SDLK_UP:       search_by(app, app->fps); break;
    case SDLK_DOWN:     search_by(app, -app->fps); break;
    case SDLK_PAGEUP:   search_by(app, 10 * app->fps); break;
    case SDLK_PAGEDOWN: search_by(app, -10 * app->fps); break;
    case SDLK_HOME:     ldp_search(ldp, ff->clips[0].start_frame); break;
    case SDLK_END:      ldp_search(ldp, ff->clips[ff->clip_count - 1].start_frame); break;
    default: break;
    }
    return true;
}

int main(int argc, char **argv)
{
    const char *framefile_path = NULL;
    const char *video_dir = NULL;
    const char *roms = NULL;
    bool want_fast_boot = false;
    bool attract_sound = false;
    bool unlimited_lives = false;
    bool free_play = false;
    bool autoplay = false;
    int dsw[4] = {-1, -1, -1, -1};  // DIP switch banks, -1 = the ROM set's defaults
    bool show_scores = false;
    bool fullscreen = false;
    const char *icon_path = NULL;
    int start_frame = -1;
    double disc_fps = 0;  // 0 = the game's own rate, or the default without ROMs

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--video-dir") == 0 && i + 1 < argc) {
            video_dir = argv[++i];
        } else if (strcmp(argv[i], "--roms") == 0 && i + 1 < argc) {
            roms = argv[++i];
        } else if (strcmp(argv[i], "--icon") == 0 && i + 1 < argc) {
            icon_path = argv[++i];
        } else if (strcmp(argv[i], "--scores") == 0) {
            show_scores = true;
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            fullscreen = true;
        } else if (strcmp(argv[i], "--attract-sound") == 0) {
            attract_sound = true;
        } else if (strcmp(argv[i], "--unlimited-lives") == 0) {
            unlimited_lives = true;
        } else if (strcmp(argv[i], "--autoplay") == 0) {
            autoplay = true;
        } else if (strcmp(argv[i], "--free-play") == 0) {
            free_play = true;
        } else if (strncmp(argv[i], "--dsw", 5) == 0 && argv[i][5] >= '1' && argv[i][5] <= '4' &&
                   argv[i][6] == '\0' && i + 1 < argc) {
            int bank = argv[i][5] - '1';
            dsw[bank] = parse_hex_byte(argv[++i]);
            if (dsw[bank] == -2) {
                usage();
                return 1;
            }
        } else if (strcmp(argv[i], "--fast-boot") == 0) {
            want_fast_boot = true;
        } else if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            start_frame = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--disc-fps") == 0 && i + 1 < argc) {
            disc_fps = atof(argv[++i]);
            if (disc_fps <= 0) disc_fps = -1;
        } else if (argv[i][0] != '-' && !framefile_path) {
            framefile_path = argv[i];
        } else {
            usage();
            return 1;
        }
    }
    if (!framefile_path) {
        fprintf(stderr, "laser-vault needs a framefile: the list that maps disc frame numbers\n"
                        "to video clips (for Dragon's Lair, dragonslair.txt).\n\n");
        usage();
        return 1;
    }
    if (disc_fps < 0) {
        usage();
        return 1;
    }

    App app = {0};
    app.texture_frame = -1;
    if (roms) {
        app.game = true;
        if (!machine_load_roms(&app.machine, roms)) return 1;
        if (!disc_fps) disc_fps = machine_disc_fps(&app.machine);
        app.has_overlay = machine_overlay_layout(&app.machine, &app.overlay_layout);
        if (app.has_overlay)
            app.overlay_pixels = calloc((size_t)app.overlay_layout.width * (size_t)app.overlay_layout.height,
                                        sizeof(uint32_t));
    }
    if (!disc_fps) disc_fps = DEFAULT_DISC_FPS;
    app.fps = (int)(disc_fps + 0.5);
    if (!framefile_load(&app.framefile, framefile_path, video_dir)) return 1;
    printf("Loaded %d clips, frames %d to %d+\n", app.framefile.clip_count,
           app.framefile.clips[0].start_frame,
           app.framefile.clips[app.framefile.clip_count - 1].start_frame);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_CreateWindowAndRenderer("laser-vault", 960, 720, SDL_WINDOW_RESIZABLE,
                                     &app.window, &app.renderer)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    app.show_scores = show_scores;
    set_layout(&app);
    if (fullscreen) toggle_fullscreen(&app);
    // Controllers announce themselves through the event loop once this is up.
    if (app.game && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        fprintf(stderr, "No gamepads: %s\n", SDL_GetError());

    // The window icon, built into the program: Dragon's Lair has its own,
    // and other games share one (most laserdisc game packs come with the
    // same icon). --icon names another.
    SDL_Surface *icon = NULL;
    if (icon_path) icon = icon_load(icon_path);
    else if (app.game && app.machine.kind == MACHINE_CINEMATRONICS && app.machine.cine->game == CINE_DRAGONS_LAIR)
        icon = icon_load_memory(icon_dlair, icon_dlair_size);
    else if (app.game) icon = icon_load_memory(icon_laser, icon_laser_size);
    if (icon) {
        SDL_SetWindowIcon(app.window, icon);
        SDL_DestroySurface(icon);
    }

    // With vsync, presenting a frame waits for the monitor's next refresh,
    // which paces the loop below. Without it, sleep a little each pass so
    // the loop doesn't spin a CPU core at 100%.
    bool vsync = SDL_SetRenderVSync(app.renderer, 1);

    app.video = disc_video_open(&app.framefile, disc_fps);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        app.audio = disc_audio_open(&app.framefile, disc_fps);
    else
        fprintf(stderr, "No sound: %s\n", SDL_GetError());
    ldp_init(&app.ldp, disc_fps,
             start_frame >= 0 ? start_frame : app.framefile.clips[0].start_frame);
    if (app.game) {
        Machine *m = &app.machine;
        bool cinematronics = m->kind == MACHINE_CINEMATRONICS;
        // The board's laserdisc player takes over the player model.
        machine_reset(m, &app.ldp);
        // DIP switch settings, as an operator would set them in the cabinet.
        // First any banks given whole, then the named options.
        bool dsw_given = false;
        for (int bank = 0; bank < 4; bank++) {
            if (dsw[bank] < 0) continue;
            dsw_given = true;
            if (machine_dsw(m, bank + 1)) *machine_dsw(m, bank + 1) = (uint8_t)dsw[bank];
            else printf("%s has no DIP switch bank %d; --dsw%d does nothing.\n", machine_game_name(m), bank + 1,
                        bank + 1);
        }
        if (machine_dsw(m, 1)) {
            // (Each ROM set knows which switch each named option is.)
            if (attract_sound) machine_set(m, MACHINE_QUIET_ATTRACT, false);
            if (unlimited_lives) machine_set(m, MACHINE_UNLIMITED_LIVES, true);
            if (free_play) machine_set(m, MACHINE_FREE_PLAY, true);
            printf("DIP switches:");
            for (int bank = 1; bank <= 4 && machine_dsw(m, bank); bank++)
                printf("%s DSW%d %02X", bank > 1 ? "," : "", bank, *machine_dsw(m, bank));
            printf("\n");
        } else if (attract_sound || unlimited_lives || free_play) {
            printf("%s has no DIP switches (it keeps its settings in memory), so\n"
                   "--free-play, --unlimited-lives and --attract-sound do nothing.\n", machine_game_name(m));
        }
        if (want_fast_boot) {
            fast_boot(&app);
        } else if (cinematronics) {
            printf("%s is starting. Like the real machine, it waits about 20 s for the\n"
                   "disc to spin up; the attract mode starts after about 35 s. (Use\n"
                   "--fast-boot to skip the wait.)\n", machine_game_name(m));
        } else {
            printf("%s is starting. (Use --fast-boot to start straight in the attract\n"
                   "mode.)\n", machine_game_name(m));
        }
        if (machine_get(m, MACHINE_FREE_PLAY))
            printf("With the game window active, press 1 to start (free play).\n");
        else if (dsw_given)  // the coinage may not be the default
            printf("With the game window active, press 5 to insert coins, then 1 to start.\n");
        else if (cinematronics)
            printf("With the game window active, press 5 twice (two coins make one credit),\n"
                   "then 1 to start.\n");
        else
            printf("With the game window active, press 5 to insert a coin, then 1 to start.\n");
        if (cinematronics && m->cine->game == CINE_SPACE_ACE)
            printf("Then choose a skill level: F1 Cadet, F2 Captain or F3 Ace.\n");
        if (m->kind == MACHINE_STERN)
            printf("Space is Cliff's hands, Shift his feet, the arrow keys the joystick.\n");
        if (cinematronics) {
            cine_autoplay_init(&app.autoplay, true);
            app.autoplaying = autoplay;
            if (autoplay) printf("Autoplay is on: once a game starts, the program plays (A turns it off).\n");
        } else if (autoplay) {
            printf("Autoplay only knows Dragon's Lair and Space Ace, so it stays off.\n");
        }
        fflush(stdout);

        // The board's sound chip gets its own audio stream; SDL mixes it with
        // the laserdisc's. Start it now, after any fast boot, so the beeps
        // from the skipped start-up don't all play at once.
        if (app.audio) {
            SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, machine_sample_rate(m)};
            app.chip_sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
            if (app.chip_sound) {
                SDL_ResumeAudioStreamDevice(app.chip_sound);
                machine_start_sound(m);
            }
        }
    }

    // The main loop: handle input, advance by however much time has passed,
    // and draw whatever frame the player is on.
    bool running = true;
    Uint64 last_ns = SDL_GetTicksNS();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN) running = handle_key(&app, event.key.key, event.key.mod);
            else if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) set_layout(&app);
            else if (event.type == SDL_EVENT_GAMEPAD_ADDED && app.game) gamepad_added(&app, event.gdevice.which);
            else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) gamepad_removed(&app, event.gdevice.which);
        }

        Uint64 now_ns = SDL_GetTicksNS();
        double elapsed = (double)(now_ns - last_ns) / 1e9;
        last_ns = now_ns;
        // After a long stall (dragging the window, a slow decode), don't
        // fast-forward to catch up; just carry on from here.
        if (elapsed > 0.25) elapsed = 0.25;

        if (app.game) {
            // Run the game's CPU for the same stretch of time. The player
            // keeps itself in step with the board's clock, so while paused,
            // when the board doesn't run, the disc stands still too.
            if (!app.paused) {
                read_game_inputs(&app);
                run_board(&app, (uint64_t)(elapsed * machine_cpu_hz(&app.machine)));
                if (app.chip_sound) play_chip_sound(&app);
            }
        } else {
            ldp_update(&app.ldp, elapsed);
        }
        draw(&app);

        // Sound plays only while the disc plays at normal speed. In game
        // mode the game can switch the disc's two audio channels on and off,
        // or play the disc silently (see machine_audio_on).
        if (app.audio) {
            bool channel1 = app.game ? machine_audio_on(&app.machine, 0) : true;
            bool channel2 = app.game ? machine_audio_on(&app.machine, 1) : true;
            disc_audio_update(app.audio, app.ldp.state == LDP_PLAYING && !app.paused,
                              app.ldp.frame, channel1, channel2);
        }
        update_title(&app);
        if (!vsync) SDL_Delay(5);
    }

    for (int i = 0; i < (int)SDL_arraysize(app.gamepads); i++)
        if (app.gamepads[i]) SDL_CloseGamepad(app.gamepads[i]);
    SDL_DestroyAudioStream(app.chip_sound);
    disc_audio_close(app.audio);
    disc_video_close(app.video);
    SDL_DestroyTexture(app.texture);
    SDL_DestroyTexture(app.overlay);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    framefile_free(&app.framefile);
    if (app.game) {
        finish_autoplay_thread(&app, true, false);
        cine_autoplay_free(&app.autoplay);
        machine_free(&app.machine);
    }
    free(app.overlay_pixels);
    return 0;
}
