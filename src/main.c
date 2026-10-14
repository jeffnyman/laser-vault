// laser-vault, stage 1: show any laserdisc frame by number.
//
// Usage: laser-vault FRAMEFILE [--video-dir DIR] [--frame N] [--disc-fps F]
//
// Keys:
//   Left / Right        step one frame
//   Down / Up           step one second
//   Page Down / Page Up step ten seconds
//   Home / End          first / last clip
//   0-9, Enter          type a frame number and jump to it
//   Escape              cancel typing, or quit

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

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;  // created to match the video's size

    int fps;            // disc frames per second, rounded (for stepping)
    int frame;          // disc frame being shown
    char typed[16];     // frame number being typed, "" when not typing
} App;

static void usage(void)
{
    fprintf(stderr, "usage: laser-vault FRAMEFILE [--video-dir DIR] [--frame N]\n");
}

static void update_title(App *app)
{
    int offset = 0;
    const FramefileClip *clip = framefile_find(&app->framefile, app->frame, &offset);
    const char *name = "(no clip)";
    if (clip) {
        const char *slash = strrchr(clip->path, '/');
        const char *backslash = strrchr(clip->path, '\\');
        if (backslash > slash) slash = backslash;
        name = slash ? slash + 1 : clip->path;
    }

    char title[256];
    if (app->typed[0])
        snprintf(title, sizeof title, "laser-vault - go to frame: %s_", app->typed);
    else
        snprintf(title, sizeof title, "laser-vault - frame %d  (%s +%d)", app->frame, name, offset);
    SDL_SetWindowTitle(app->window, title);
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

static void show_frame(App *app)
{
    const AVFrame *pic = disc_video_get_frame(app->video, app->frame);

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    if (pic) {
        upload_picture(app, pic);
        if (app->texture) SDL_RenderTexture(app->renderer, app->texture, NULL, NULL);
    }
    SDL_RenderPresent(app->renderer);
    update_title(app);
}

static void go_to(App *app, int frame)
{
    if (frame < 0) frame = 0;
    app->frame = frame;
    show_frame(app);
}

// Returns false when the app should quit.
static bool handle_key(App *app, SDL_Keycode key)
{
    size_t typed_len = strlen(app->typed);
    const Framefile *ff = &app->framefile;

    if (key >= SDLK_0 && key <= SDLK_9) {
        if (typed_len + 1 < sizeof app->typed) {
            app->typed[typed_len] = (char)('0' + (key - SDLK_0));
            app->typed[typed_len + 1] = '\0';
        }
        update_title(app);
        return true;
    }

    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (typed_len) {
            int frame = atoi(app->typed);
            app->typed[0] = '\0';
            go_to(app, frame);
        }
        break;
    case SDLK_BACKSPACE:
        if (typed_len) app->typed[typed_len - 1] = '\0';
        update_title(app);
        break;
    case SDLK_ESCAPE:
        if (!typed_len) return false;
        app->typed[0] = '\0';
        update_title(app);
        break;
    case SDLK_RIGHT:    go_to(app, app->frame + 1); break;
    case SDLK_LEFT:     go_to(app, app->frame - 1); break;
    case SDLK_UP:       go_to(app, app->frame + app->fps); break;
    case SDLK_DOWN:     go_to(app, app->frame - app->fps); break;
    case SDLK_PAGEUP:   go_to(app, app->frame + 10 * app->fps); break;
    case SDLK_PAGEDOWN: go_to(app, app->frame - 10 * app->fps); break;
    case SDLK_HOME:     go_to(app, ff->clips[0].start_frame); break;
    case SDLK_END:      go_to(app, ff->clips[ff->clip_count - 1].start_frame); break;
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

    app.video = disc_video_open(&app.framefile, disc_fps);
    go_to(&app, start_frame >= 0 ? start_frame : app.framefile.clips[0].start_frame);

    // Nothing moves on its own yet, so just wait for input.
    bool running = true;
    SDL_Event event;
    while (running && SDL_WaitEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            running = handle_key(&app, event.key.key);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_EXPOSED:
            show_frame(&app);
            break;
        default:
            break;
        }
    }

    disc_video_close(app.video);
    SDL_DestroyTexture(app.texture);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    framefile_free(&app.framefile);
    return 0;
}
