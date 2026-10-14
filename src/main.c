// laser-vault, stage 2: a laserdisc player you drive from the keyboard.
//
// Usage: laser-vault FRAMEFILE [--video-dir DIR] [--frame N] [--disc-fps F]
//
// Keys:
//   Space               play / pause
//   S                   stop
//   Left / Right        step one frame back / forward (pauses)
//   Down / Up           search one second back / forward
//   Page Down / Page Up search ten seconds back / forward
//   Home / End          search to the first / last clip
//   0-9, Enter          type a frame number and search to it
//   Escape              cancel typing, or quit

#include "ldp/ldp.h"
#include "video/disc_video.h"
#include "video/framefile.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The rate the game's disc frame numbers count at. Dragon's Lair's frame
// numbers count 24 fps film frames; see video/disc_video.h.
#define DEFAULT_DISC_FPS 23.976

typedef struct {
    Framefile framefile;
    DiscVideo *video;
    Ldp ldp;

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;  // created to match the video's size
    int texture_frame;     // disc frame currently in the texture, or -1

    int fps;            // disc frames per second, rounded (for skipping)
    char typed[16];     // frame number being typed, "" when not typing
    char title[256];    // current window title
} App;

static void usage(void)
{
    fprintf(stderr, "usage: laser-vault FRAMEFILE [--video-dir DIR] [--frame N] [--disc-fps F]\n");
}

static void update_title(App *app)
{
    const Ldp *ldp = &app->ldp;
    char title[sizeof app->title];

    if (app->typed[0]) {
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
        snprintf(title, sizeof title, "laser-vault - %s, frame %d  (%s +%d)",
                 ldp_state_name(ldp->state), ldp->frame, name, offset);
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

static void draw(App *app)
{
    const Ldp *ldp = &app->ldp;

    // Decode only when the player has moved to a new frame. While searching,
    // ldp->frame stays on the old frame, so the last picture is held.
    if (ldp_has_picture(ldp) && ldp->frame != app->texture_frame) {
        const AVFrame *pic = disc_video_get_frame(app->video, ldp->frame);
        if (pic) upload_picture(app, pic);
        app->texture_frame = pic ? ldp->frame : -1;
    }

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    if (ldp_has_picture(ldp) && app->texture_frame >= 0)
        SDL_RenderTexture(app->renderer, app->texture, NULL, NULL);
    SDL_RenderPresent(app->renderer);
}

// Search relative to where the player is heading, so pressing Up twice
// quickly skips two seconds even though the first search hasn't finished.
static void search_by(App *app, int delta)
{
    const Ldp *ldp = &app->ldp;
    int from = ldp->state == LDP_SEARCHING ? ldp->search_target : ldp->frame;
    ldp_search(&app->ldp, from + delta);
}

// Returns false when the app should quit.
static bool handle_key(App *app, SDL_Keycode key)
{
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
    int start_frame = -1;
    double disc_fps = DEFAULT_DISC_FPS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--video-dir") == 0 && i + 1 < argc) {
            video_dir = argv[++i];
        } else if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            start_frame = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--disc-fps") == 0 && i + 1 < argc) {
            disc_fps = atof(argv[++i]);
        } else if (argv[i][0] != '-' && !framefile_path) {
            framefile_path = argv[i];
        } else {
            usage();
            return 1;
        }
    }
    if (!framefile_path || disc_fps <= 0) {
        usage();
        return 1;
    }

    App app = {0};
    app.fps = (int)(disc_fps + 0.5);
    app.texture_frame = -1;
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
    // Laserdisc video is 720x480 pixels but is shown on a 4:3 screen, so the
    // pixels aren't square. Drawing into a 640x480 logical canvas stretches
    // it to the right shape; letterboxing keeps 4:3 when the window resizes.
    SDL_SetRenderLogicalPresentation(app.renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // With vsync, presenting a frame waits for the monitor's next refresh,
    // which paces the loop below. Without it, sleep a little each pass so
    // the loop doesn't spin a CPU core at 100%.
    bool vsync = SDL_SetRenderVSync(app.renderer, 1);

    app.video = disc_video_open(&app.framefile, disc_fps);
    ldp_init(&app.ldp, disc_fps,
             start_frame >= 0 ? start_frame : app.framefile.clips[0].start_frame);

    // The main loop: handle input, advance the player by however much time
    // has passed, and draw whatever frame it's on.
    bool running = true;
    Uint64 last_ns = SDL_GetTicksNS();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN) running = handle_key(&app, event.key.key);
        }

        Uint64 now_ns = SDL_GetTicksNS();
        double elapsed = (double)(now_ns - last_ns) / 1e9;
        last_ns = now_ns;
        // After a long stall (dragging the window, a slow decode), don't
        // fast-forward to catch up; just carry on from here.
        if (elapsed > 0.25) elapsed = 0.25;

        ldp_update(&app.ldp, elapsed);
        draw(&app);
        update_title(&app);
        if (!vsync) SDL_Delay(5);
    }

    disc_video_close(app.video);
    SDL_DestroyTexture(app.texture);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    framefile_free(&app.framefile);
    return 0;
}
