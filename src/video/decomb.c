#include "video/decomb.h"

#include <stdlib.h>
#include <string.h>

bool decomb_is_combed(const AVFrame *pic)
{
    // Compare each even line with its neighbours from the other field (the
    // lines directly above and below) and with its neighbours from its own
    // field (two lines away). In a clean picture both look alike, because
    // nearby lines show nearly the same thing. In a combed picture the other
    // field shows a different moment, so it differs much more. Measured on
    // Dragon's Lair, clean frames score around 0.6 and combed ones around 1.8.
    // Sampling every other pixel is plenty and halves the work.
    const uint8_t *luma = pic->data[0];
    int stride = pic->linesize[0];
    long long other_field = 0, same_field = 0;

    for (int y = 2; y < pic->height - 2; y += 2) {
        const uint8_t *above = luma + (y - 1) * stride;
        const uint8_t *line = luma + y * stride;
        const uint8_t *below = luma + (y + 1) * stride;
        const uint8_t *two_below = luma + (y + 2) * stride;
        for (int x = 0; x < pic->width; x += 2) {
            other_field += abs(line[x] - above[x]) + abs(line[x] - below[x]);
            same_field += abs(line[x] - two_below[x]) + abs(above[x] - below[x]);
        }
    }
    return other_field > same_field;
}

static void rebuild_plane(uint8_t *dst, int dst_stride, const uint8_t *src, int src_stride,
                          int width, int height)
{
    for (int y = 0; y < height; y++) {
        uint8_t *out = dst + y * dst_stride;
        const uint8_t *even = src + (y & ~1) * src_stride;
        if (y % 2 == 0 || y + 1 >= height) {
            memcpy(out, even, (size_t)width);  // even line (or the last line)
        } else {
            const uint8_t *next_even = src + (y + 1) * src_stride;
            for (int x = 0; x < width; x++)
                out[x] = (uint8_t)((even[x] + next_even[x] + 1) / 2);
        }
    }
}

void decomb_rebuild_from_even_field(AVFrame *dst, const AVFrame *src)
{
    // In interlaced 4:2:0 video the colour planes alternate between fields
    // line by line too, so they get the same treatment at half size.
    rebuild_plane(dst->data[0], dst->linesize[0], src->data[0], src->linesize[0],
                  src->width, src->height);
    for (int plane = 1; plane <= 2; plane++) {
        rebuild_plane(dst->data[plane], dst->linesize[plane], src->data[plane], src->linesize[plane],
                      (src->width + 1) / 2, (src->height + 1) / 2);
    }
}
