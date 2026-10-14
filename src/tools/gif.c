#include "tools/gif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Palette index reserved for "transparent: keep the previous frame's pixel".
#define TRANSPARENT_INDEX 255

static int color_key(int r, int g, int b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

// Expand a 5-bit channel back to 8 bits (0..31 -> 0..255).
static int expand5(int c)
{
    return (c << 3) | (c >> 2);
}

// ---------------------------------------------------------------------------
// Palette: median cut
// ---------------------------------------------------------------------------

void gif_histogram_add(GifHistogram *hist, const uint8_t *rgb, int pixel_count)
{
    for (int i = 0; i < pixel_count; i++, rgb += 3)
        hist->counts[color_key(rgb[0], rgb[1], rgb[2])]++;
}

typedef struct {
    uint8_t c[3];  // 5-bit r, g, b
    uint32_t count;
} ColorCount;

typedef struct {
    int start, end;  // range in the colours array
    int axis;        // channel with the widest range
    int range;       // width of that range
    uint64_t pixels; // total pixel count
} Box;

static void measure_box(Box *box, const ColorCount *colors)
{
    int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
    box->pixels = 0;
    for (int i = box->start; i < box->end; i++) {
        for (int ch = 0; ch < 3; ch++) {
            if (colors[i].c[ch] < lo[ch]) lo[ch] = colors[i].c[ch];
            if (colors[i].c[ch] > hi[ch]) hi[ch] = colors[i].c[ch];
        }
        box->pixels += colors[i].count;
    }
    box->axis = 0;
    for (int ch = 1; ch < 3; ch++)
        if (hi[ch] - lo[ch] > hi[box->axis] - lo[box->axis]) box->axis = ch;
    box->range = hi[box->axis] - lo[box->axis];
}

static int sort_axis;  // qsort has no context argument in standard C

static int compare_on_axis(const void *a, const void *b)
{
    return ((const ColorCount *)a)->c[sort_axis] - ((const ColorCount *)b)->c[sort_axis];
}

void gif_palette_build(GifPalette *pal, const GifHistogram *hist, int max_colors)
{
    if (max_colors > 255) max_colors = 255;  // index 255 is for transparency

    // List every colour that actually appears.
    ColorCount *colors = malloc(GIF_COLOR_KEYS * sizeof *colors);
    int n = 0;
    for (int key = 0; key < GIF_COLOR_KEYS; key++) {
        if (!hist->counts[key]) continue;
        colors[n].c[0] = (uint8_t)(key >> 10);
        colors[n].c[1] = (uint8_t)((key >> 5) & 31);
        colors[n].c[2] = (uint8_t)(key & 31);
        colors[n].count = hist->counts[key];
        n++;
    }

    // Median cut: start with one box holding every colour. Repeatedly take
    // the box that matters most (many pixels spread over a wide range of
    // colour), and split it in two at the median along its widest channel.
    // Each final box becomes one palette entry.
    Box boxes[256];
    int box_count = 1;
    boxes[0] = (Box){.start = 0, .end = n};
    measure_box(&boxes[0], colors);

    while (box_count < max_colors) {
        int best = -1;
        uint64_t best_score = 0;
        for (int i = 0; i < box_count; i++) {
            uint64_t score = boxes[i].pixels * (uint64_t)boxes[i].range;
            if (boxes[i].end - boxes[i].start > 1 && score > best_score) {
                best = i;
                best_score = score;
            }
        }
        if (best < 0) break;  // every box is a single colour

        Box *box = &boxes[best];
        sort_axis = box->axis;
        qsort(colors + box->start, (size_t)(box->end - box->start), sizeof *colors, compare_on_axis);

        // Split where half the box's pixels are on each side.
        uint64_t half = box->pixels / 2, seen = 0;
        int split = box->start + 1;
        for (int i = box->start; i < box->end - 1; i++) {
            seen += colors[i].count;
            split = i + 1;
            if (seen >= half) break;
        }

        boxes[box_count] = (Box){.start = split, .end = box->end};
        box->end = split;
        measure_box(box, colors);
        measure_box(&boxes[box_count], colors);
        box_count++;
    }

    // Each palette colour is the pixel-weighted average of its box.
    memset(pal, 0, sizeof *pal);
    pal->count = box_count;
    for (int i = 0; i < box_count; i++) {
        uint64_t sum[3] = {0, 0, 0};
        for (int j = boxes[i].start; j < boxes[i].end; j++)
            for (int ch = 0; ch < 3; ch++)
                sum[ch] += (uint64_t)expand5(colors[j].c[ch]) * colors[j].count;
        for (int ch = 0; ch < 3; ch++)
            pal->rgb[i][ch] = boxes[i].pixels ? (uint8_t)(sum[ch] / boxes[i].pixels) : 0;
    }
    free(colors);

    // Precompute the nearest palette entry for every 5-bit colour, so
    // mapping a pixel is a single table lookup.
    for (int key = 0; key < GIF_COLOR_KEYS; key++) {
        int r = expand5(key >> 10), g = expand5((key >> 5) & 31), b = expand5(key & 31);
        int best = 0, best_dist = 1 << 30;
        for (int i = 0; i < pal->count; i++) {
            int dr = r - pal->rgb[i][0], dg = g - pal->rgb[i][1], db = b - pal->rgb[i][2];
            int dist = dr * dr + dg * dg + db * db;
            if (dist < best_dist) {
                best = i;
                best_dist = dist;
            }
        }
        pal->lookup[key] = (uint8_t)best;
    }
}

void gif_palette_map(const GifPalette *pal, const uint8_t *rgb, int pixel_count, uint8_t *indices)
{
    for (int i = 0; i < pixel_count; i++, rgb += 3)
        indices[i] = pal->lookup[color_key(rgb[0], rgb[1], rgb[2])];
}

// ---------------------------------------------------------------------------
// LZW compression
// ---------------------------------------------------------------------------
//
// LZW builds a dictionary of byte sequences as it goes. Codes 0-255 stand
// for single bytes; each time the encoder outputs a code, it adds "that
// sequence plus the next byte" as a new code. Repeated patterns soon become
// single codes. The decoder rebuilds the same dictionary from the codes, so
// it never needs to be stored. Codes start 9 bits wide and grow to 12; when
// the dictionary is full, a "clear" code starts it over.

#define LZW_CLEAR 256
#define LZW_END 257
#define LZW_FIRST_FREE 258
#define LZW_LIMIT 4095  // stay one short of 4096 for older decoders
#define HASH_SIZE 8192  // power of two, comfortably more than 4096 entries

typedef struct {
    FILE *file;
    uint8_t block[255];  // GIF data is written in blocks of up to 255 bytes
    int block_len;
    uint32_t bits;
    int bit_count;
} BitWriter;

static void flush_block(BitWriter *bw)
{
    if (!bw->block_len) return;
    fputc(bw->block_len, bw->file);
    fwrite(bw->block, 1, (size_t)bw->block_len, bw->file);
    bw->block_len = 0;
}

static void put_code(BitWriter *bw, int code, int size)
{
    bw->bits |= (uint32_t)code << bw->bit_count;  // GIF packs bits LSB first
    bw->bit_count += size;
    while (bw->bit_count >= 8) {
        bw->block[bw->block_len++] = (uint8_t)bw->bits;
        if (bw->block_len == 255) flush_block(bw);
        bw->bits >>= 8;
        bw->bit_count -= 8;
    }
}

static void lzw_encode(FILE *file, const uint8_t *data, int n)
{
    // Dictionary: maps (prefix code, next byte) to a code. An open-addressing
    // hash table is simple and fast enough.
    static int32_t keys[HASH_SIZE];
    static int16_t codes[HASH_SIZE];
    memset(keys, 0xFF, sizeof keys);  // all -1: empty

    fputc(8, file);  // minimum code size: palette indices are 8 bits
    BitWriter bw = {.file = file};
    int code_size = 9, next_code = LZW_FIRST_FREE;
    put_code(&bw, LZW_CLEAR, code_size);

    int prefix = data[0];
    for (int i = 1; i < n; i++) {
        int32_t key = (prefix << 8) | data[i];
        uint32_t h = ((uint32_t)key * 2654435761u) >> 19;  // top 13 bits
        while (keys[h] != -1 && keys[h] != key) h = (h + 1) & (HASH_SIZE - 1);
        if (keys[h] == key) {  // known sequence: keep extending it
            prefix = codes[h];
            continue;
        }

        put_code(&bw, prefix, code_size);
        if (next_code < LZW_LIMIT) {
            keys[h] = key;
            codes[h] = (int16_t)next_code++;
            // The decoder lags one code behind, so widen once next_code
            // passes (not reaches) the current limit.
            if (next_code > (1 << code_size)) code_size++;
        } else {
            put_code(&bw, LZW_CLEAR, code_size);
            memset(keys, 0xFF, sizeof keys);
            code_size = 9;
            next_code = LZW_FIRST_FREE;
        }
        prefix = data[i];
    }
    put_code(&bw, prefix, code_size);
    put_code(&bw, LZW_END, code_size);
    if (bw.bit_count) bw.block[bw.block_len++] = (uint8_t)bw.bits;
    flush_block(&bw);
    fputc(0, file);  // zero-length block ends the image data
}

// ---------------------------------------------------------------------------
// GIF file
// ---------------------------------------------------------------------------

struct GifWriter {
    FILE *file;
    int width, height;
    uint8_t *canvas;   // what's on screen after the frames written so far
    uint8_t *pending;  // latest frame, held back in case the next is identical
    uint8_t *scratch;  // sub-image being written
    int pending_delay;
    bool has_canvas, has_pending;
    uint8_t same[256][256];  // same[a][b]: colours a and b are close enough
};

static void put16(FILE *f, int v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

GifWriter *gif_open(const char *path, int width, int height, const GifPalette *pal,
                    int noise)
{
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;

    GifWriter *gif = calloc(1, sizeof *gif);
    gif->file = f;
    gif->width = width;
    gif->height = height;
    gif->canvas = malloc((size_t)width * height);
    gif->pending = malloc((size_t)width * height);
    gif->scratch = malloc((size_t)width * height);

    for (int a = 0; a < pal->count; a++) {
        for (int b = 0; b < pal->count; b++) {
            int dr = pal->rgb[a][0] - pal->rgb[b][0];
            int dg = pal->rgb[a][1] - pal->rgb[b][1];
            int db = pal->rgb[a][2] - pal->rgb[b][2];
            gif->same[a][b] = dr * dr + dg * dg + db * db <= noise * noise;
        }
    }

    fwrite("GIF89a", 1, 6, f);
    put16(f, width);
    put16(f, height);
    fputc(0xF7, f);  // global colour table of 256 entries, 8-bit colour
    fputc(0, f);     // background colour index
    fputc(0, f);     // pixel aspect ratio: not given
    for (int i = 0; i < 256; i++)
        fwrite(i < pal->count ? pal->rgb[i] : (const uint8_t[3]){0, 0, 0}, 1, 3, f);

    // "NETSCAPE2.0" application extension: loop forever.
    fwrite("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 1, 19, f);
    return gif;
}

static void write_pending(GifWriter *gif)
{
    int w = gif->width;
    int x0 = 0, y0 = 0, x1 = w - 1, y1 = gif->height - 1;
    bool transparent = gif->has_canvas;

    if (transparent) {
        // Find the rectangle that changed since the last frame.
        x0 = w; y0 = gif->height; x1 = -1; y1 = -1;
        for (int y = 0; y < gif->height; y++) {
            for (int x = 0; x < w; x++) {
                if (gif->same[gif->pending[y * w + x]][gif->canvas[y * w + x]]) continue;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
        if (x1 < 0) x0 = y0 = x1 = y1 = 0;  // nothing changed: a 1x1 frame
    }

    int sw = x1 - x0 + 1, sh = y1 - y0 + 1;
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int i = (y0 + y) * w + (x0 + x);
            uint8_t idx = gif->pending[i];
            if (transparent && gif->same[idx][gif->canvas[i]]) {
                idx = TRANSPARENT_INDEX;  // close enough: keep what's there
            } else {
                gif->canvas[i] = idx;
            }
            gif->scratch[y * sw + x] = idx;
        }
    }

    FILE *f = gif->file;
    // Graphic control extension: delay, "leave this frame in place" disposal,
    // and the transparent index.
    fwrite("\x21\xF9\x04", 1, 3, f);
    fputc((1 << 2) | (transparent ? 1 : 0), f);
    put16(f, gif->pending_delay);
    fputc(TRANSPARENT_INDEX, f);
    fputc(0, f);

    // Image descriptor, then the compressed pixels.
    fputc(0x2C, f);
    put16(f, x0);
    put16(f, y0);
    put16(f, sw);
    put16(f, sh);
    fputc(0, f);  // no local colour table, not interlaced
    lzw_encode(f, gif->scratch, sw * sh);

    if (!gif->has_canvas) memcpy(gif->canvas, gif->pending, (size_t)w * gif->height);
    gif->has_canvas = true;
}

bool gif_add_frame(GifWriter *gif, const uint8_t *indices, int delay_cs)
{
    size_t size = (size_t)gif->width * gif->height;
    if (gif->has_pending && memcmp(indices, gif->pending, size) == 0) {
        gif->pending_delay += delay_cs;  // same picture: just show it longer
        return true;
    }
    if (gif->has_pending) write_pending(gif);
    memcpy(gif->pending, indices, size);
    gif->pending_delay = delay_cs;
    gif->has_pending = true;
    return !ferror(gif->file);
}

bool gif_close(GifWriter *gif)
{
    if (gif->has_pending) write_pending(gif);
    fputc(0x3B, gif->file);  // trailer
    bool ok = !ferror(gif->file);
    if (fclose(gif->file) != 0) ok = false;
    free(gif->canvas);
    free(gif->pending);
    free(gif->scratch);
    free(gif);
    return ok;
}
