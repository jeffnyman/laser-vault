#include "video/decomb.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// How far a pixel must stand out from both even lines around it (on the 0-255
// luma scale) to count as part of a comb: a strong tooth can start a combed
// area, a weak one only extends an area it touches (where a moving figure's
// comb fades out over a plain background, say). Chosen by looking at combed
// and clean frames from all five discs: a lower strong threshold picks up
// film grain; without the weak one, faint edges of moving figures stay
// combed.
#define STRONG_TOOTH 20
#define WEAK_TOOTH 8

// How strongly the pixel at x on an odd line is a comb tooth: 2 strong, 1
// weak, 0 not. It must be brighter than both even lines around it, or darker
// than both, while those two lines agree with each other: where they don't,
// it's an edge in the picture, not a comb.
static int tooth(const uint8_t *above, const uint8_t *line, const uint8_t *below, int x)
{
    int up = line[x] - above[x], down = line[x] - below[x];
    if ((up > 0) != (down > 0)) return 0;
    int by = abs(up) < abs(down) ? abs(up) : abs(down);         // how far it stands out
    int edge = abs(above[x] - below[x]), mean = (abs(up) + abs(down)) / 2;
    if (by > STRONG_TOOTH && edge < mean - STRONG_TOOTH) return 2;
    if (by > WEAK_TOOTH && edge < mean - WEAK_TOOTH) return 1;
    return 0;
}

// Is any of mask[x-reach .. x+reach] set?
static bool near(const uint8_t *mask, int width, int x, int reach)
{
    for (int i = x - reach; i <= x + reach; i++)
        if (i >= 0 && i < width && mask[i]) return true;
    return false;
}

static void copy_plane(uint8_t *dst, int dst_stride, const uint8_t *src, int src_stride, int width, int height)
{
    for (int y = 0; y < height; y++) memcpy(dst + y * dst_stride, src + y * src_stride, (size_t)width);
}

long decomb_repair(AVFrame *dst, const AVFrame *src)
{
    int w = src->width, h = src->height;
    int rows = (h - 1) / 2;  // odd lines with an even line below them
    if (rows < 3) return 0;
    const uint8_t *luma = src->data[0];
    int stride = src->linesize[0];

    // Find the teeth, one row of the mask per odd line.
    size_t n = (size_t)rows * (size_t)w;
    uint8_t *teeth = calloc(n, 1);
    uint8_t *comb = calloc(n, 1);
    uint8_t *fix = calloc(n, 1);
    int *todo = malloc(n * sizeof *todo);
    long repaired = 0;
    if (!teeth || !comb || !fix || !todo) goto done;
    long strong = 0;
    for (int r = 0; r < rows; r++) {
        int y = 2 * r + 1;
        const uint8_t *above = luma + (y - 1) * stride, *line = luma + y * stride, *below = luma + (y + 1) * stride;
        for (int x = 0; x < w; x++) {
            teeth[r * w + x] = (uint8_t)tooth(above, line, below, x);
            strong += teeth[r * w + x] == 2;
        }
    }
    if (!strong) goto done;

    // A combed area starts at a strong tooth with another tooth beside it on
    // the odd line above or below: a real comb runs down several lines of
    // the field, while a tooth on its own is more likely a thin horizontal
    // line in the drawing. From there the area takes in every tooth, weak or
    // strong, it touches (within two pixels across and one odd line down or
    // up), and so on outwards.
    int count = 0;
    for (int r = 0; r < rows; r++) {
        for (int x = 0; x < w; x++) {
            if (teeth[r * w + x] != 2) continue;
            bool supported = (r > 0 && near(teeth + (r - 1) * w, w, x, 1)) ||
                             (r + 1 < rows && near(teeth + (r + 1) * w, w, x, 1));
            if (supported && !comb[r * w + x]) comb[r * w + x] = 1, todo[count++] = r * w + x;
        }
    }
    while (count > 0) {
        int i = todo[--count], r = i / w, x = i % w;
        for (int rr = r - 1; rr <= r + 1; rr++) {
            for (int xx = x - 2; xx <= x + 2; xx++) {
                if (rr < 0 || rr >= rows || xx < 0 || xx >= w) continue;
                int j = rr * w + xx;
                if (teeth[j] && !comb[j]) comb[j] = 1, todo[count++] = j;
            }
        }
    }

    // Repair the combed area and its surroundings (two pixels across, one
    // odd line up and down), to take in the comb's faintest fringes.
    for (int r = 0; r < rows; r++) {
        for (int x = 0; x < w; x++) {
            if (!comb[r * w + x]) continue;
            for (int rr = r - 1; rr <= r + 1; rr++) {
                if (rr < 0 || rr >= rows) continue;
                for (int xx = x - 2; xx <= x + 2; xx++)
                    if (xx >= 0 && xx < w && !fix[rr * w + xx]) fix[rr * w + xx] = 1, repaired++;
            }
        }
    }
    if (!repaired) goto done;

    // Rebuild the marked pixels from the even lines around them. In
    // interlaced 4:2:0 video the colour planes alternate between fields line
    // by line too, at half size: an odd colour line is repaired where the
    // luma beside it (on the odd luma lines just above and below) was.
    for (int plane = 0; plane < 3; plane++) {
        int pw = plane ? (w + 1) / 2 : w, ph = plane ? (h + 1) / 2 : h;
        int ss = src->linesize[plane], ds = dst->linesize[plane];
        copy_plane(dst->data[plane], ds, src->data[plane], ss, pw, ph);
        for (int y = 1; y + 1 < ph; y += 2) {
            const uint8_t *above = src->data[plane] + (y - 1) * ss, *below = src->data[plane] + (y + 1) * ss;
            uint8_t *out = dst->data[plane] + y * ds;
            for (int x = 0; x < pw; x++) {
                bool marked;
                if (!plane) {
                    marked = fix[(y / 2) * w + x];
                } else {
                    // Odd colour line y lies between odd luma lines 2y-1 and 2y+1.
                    int r0 = y - 1, r1 = y;
                    marked = false;
                    for (int r = r0; r <= r1 && !marked; r++)
                        if (r >= 0 && r < rows) marked = near(fix + r * w, w, 2 * x, 1);
                }
                if (marked) out[x] = (uint8_t)((above[x] + below[x] + 1) / 2);
            }
        }
    }
done:
    free(teeth);
    free(comb);
    free(fix);
    free(todo);
    return repaired;
}
