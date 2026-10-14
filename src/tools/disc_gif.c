// disc-gif: turn a range of laserdisc frames into an animated GIF.
//
// Usage: disc-gif FRAMEFILE --from N --to N -o OUT.gif
//                 [--video-dir DIR] [--width W] [--fps F] [--noise N]
//                 [--disc-fps F]
//
// Example: disc-gif dragonslair.txt --video-dir mpeg --from 530 --to 715 -o intro.gif
//
// Frames are decoded with the same code the emulator uses, scaled to a 4:3
// picture W pixels wide, and sampled at F frames per second (default 12,
// which suits film-sourced animation and keeps the file small). --noise sets
// how big a colour change must be to count as a change (default 24); higher
// values make smaller files but can leave smudges behind moving objects.

#include "tools/gif.h"
#include "video/disc_video.h"
#include "video/framefile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_DISC_FPS 23.976  // see video/disc_video.h

static void usage(void)
{
    fprintf(stderr,
            "usage: disc-gif FRAMEFILE --from N --to N -o OUT.gif\n"
            "                [--video-dir DIR] [--width W] [--fps F] [--noise N]\n"
            "                [--disc-fps F]\n");
}

static uint8_t clamp255(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

// Scale a decoded YUV 4:2:0 picture to width x height RGB pixels.
//
// Each output pixel averages the block of source pixels it covers (a "box
// filter"). The source is 720x480 with non-square pixels; squeezing it into
// a 4:3 output gives the picture its proper shape.
static void picture_to_rgb(const AVFrame *pic, int width, int height, uint8_t *rgb)
{
    for (int dy = 0; dy < height; dy++) {
        int sy0 = dy * pic->height / height;
        int sy1 = (dy + 1) * pic->height / height;
        if (sy1 <= sy0) sy1 = sy0 + 1;

        for (int dx = 0; dx < width; dx++) {
            int sx0 = dx * pic->width / width;
            int sx1 = (dx + 1) * pic->width / width;
            if (sx1 <= sx0) sx1 = sx0 + 1;

            // Average brightness (Y) at full resolution, and colour (U, V)
            // at half resolution, where 4:2:0 stores it.
            int y_sum = 0, u_sum = 0, v_sum = 0, n = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                const uint8_t *y_row = pic->data[0] + sy * pic->linesize[0];
                const uint8_t *u_row = pic->data[1] + (sy / 2) * pic->linesize[1];
                const uint8_t *v_row = pic->data[2] + (sy / 2) * pic->linesize[2];
                for (int sx = sx0; sx < sx1; sx++) {
                    y_sum += y_row[sx];
                    u_sum += u_row[sx / 2];
                    v_sum += v_row[sx / 2];
                    n++;
                }
            }

            // YUV to RGB with the standard-definition (BT.601) formula.
            // MPEG video uses "studio range": black is Y=16, white is Y=235.
            double y = (double)y_sum / n - 16;
            double u = (double)u_sum / n - 128;
            double v = (double)v_sum / n - 128;
            uint8_t *out = rgb + 3 * (dy * width + dx);
            out[0] = clamp255((int)(1.164 * y + 1.596 * v + 0.5));
            out[1] = clamp255((int)(1.164 * y - 0.392 * u - 0.813 * v + 0.5));
            out[2] = clamp255((int)(1.164 * y + 2.017 * u + 0.5));
        }
    }
}

int main(int argc, char **argv)
{
    const char *framefile_path = NULL, *video_dir = NULL, *out_path = NULL;
    int from = -1, to = -1, width = 480;
    int noise = 24;
    double fps = 12, disc_fps = DEFAULT_DISC_FPS;

    for (int i = 1; i < argc; i++) {
        bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--video-dir") == 0 && has_value) video_dir = argv[++i];
        else if (strcmp(argv[i], "--from") == 0 && has_value) from = atoi(argv[++i]);
        else if (strcmp(argv[i], "--to") == 0 && has_value) to = atoi(argv[++i]);
        else if (strcmp(argv[i], "-o") == 0 && has_value) out_path = argv[++i];
        else if (strcmp(argv[i], "--width") == 0 && has_value) width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--fps") == 0 && has_value) fps = atof(argv[++i]);
        else if (strcmp(argv[i], "--noise") == 0 && has_value) noise = atoi(argv[++i]);
        else if (strcmp(argv[i], "--disc-fps") == 0 && has_value) disc_fps = atof(argv[++i]);
        else if (argv[i][0] != '-' && !framefile_path) framefile_path = argv[i];
        else {
            usage();
            return 1;
        }
    }
    if (!framefile_path || !out_path || from < 0 || to < from || width < 16 || fps <= 0 || disc_fps <= 0) {
        usage();
        return 1;
    }
    int height = width * 3 / 4;

    Framefile ff;
    if (!framefile_load(&ff, framefile_path, video_dir)) return 1;
    DiscVideo *video = disc_video_open(&ff, disc_fps);

    // Which disc frames to sample: every disc_fps / fps frames.
    int count = (int)((to - from) * fps / disc_fps) + 1;
    size_t frame_bytes = (size_t)width * height * 3;
    uint8_t *rgb = malloc(frame_bytes * count);
    if (!rgb) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    // Pass 1: decode every frame, and count colours for the palette.
    GifHistogram *hist = calloc(1, sizeof *hist);
    for (int i = 0; i < count; i++) {
        int frame = from + (int)(i * disc_fps / fps + 0.5);
        const AVFrame *pic = disc_video_get_frame(video, frame);
        if (!pic || pic->format != AV_PIX_FMT_YUV420P) {
            fprintf(stderr, "no picture for frame %d\n", frame);
            return 1;
        }
        picture_to_rgb(pic, width, height, rgb + i * frame_bytes);
        gif_histogram_add(hist, rgb + i * frame_bytes, width * height);
    }

    GifPalette *pal = malloc(sizeof *pal);
    gif_palette_build(pal, hist, 255);

    // Pass 2: write the frames. GIF delays are in whole hundredths of a
    // second, so compute each from the ideal timeline to avoid drift
    // (at 12 fps, delays alternate between 8 and 9).
    GifWriter *gif = gif_open(out_path, width, height, pal, noise);
    if (!gif) {
        fprintf(stderr, "cannot create %s\n", out_path);
        return 1;
    }
    uint8_t *indices = malloc((size_t)width * height);
    for (int i = 0; i < count; i++) {
        int delay = (int)((i + 1) * 100 / fps + 0.5) - (int)(i * 100 / fps + 0.5);
        gif_palette_map(pal, rgb + i * frame_bytes, width * height, indices);
        gif_add_frame(gif, indices, delay);
    }
    if (!gif_close(gif)) {
        fprintf(stderr, "error writing %s\n", out_path);
        return 1;
    }

    printf("Wrote %s: %d frames (%d to %d), %dx%d, %d colours\n",
           out_path, count, from, to, width, height, pal->count);

    free(indices);
    free(pal);
    free(hist);
    free(rgb);
    disc_video_close(video);
    framefile_free(&ff);
    return 0;
}
