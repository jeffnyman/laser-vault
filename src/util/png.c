#include "util/png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// PNG stores numbers big-endian.
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static bool write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t length)
{
    uint8_t header[8], crc_bytes[4];
    put32(header, length);
    memcpy(header + 4, type, 4);
    uLong crc = crc32(0, header + 4, 4);
    if (length) crc = crc32(crc, data, length);
    put32(crc_bytes, (uint32_t)crc);
    return fwrite(header, 1, 8, f) == 8 && (length == 0 || fwrite(data, 1, length, f) == length) &&
           fwrite(crc_bytes, 1, 4, f) == 4;
}

// The Paeth predictor: whichever of left, above and above-left is closest
// to left + above - above-left.
static uint8_t paeth(uint8_t left, uint8_t above, uint8_t above_left)
{
    int p = left + above - above_left;
    int pa = abs(p - left), pb = abs(p - above), pc = abs(p - above_left);
    if (pa <= pb && pa <= pc) return left;
    return pb <= pc ? above : above_left;
}

// Filter one row (n bytes, 3 per pixel) with filter type t (0 none, 1 left,
// 2 above, 3 average of the two, 4 Paeth); prev is the row above, or zeros.
static void filter_row(int t, const uint8_t *row, const uint8_t *prev, size_t n, uint8_t *out)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t left = i >= 3 ? row[i - 3] : 0;
        uint8_t above = prev[i];
        uint8_t above_left = i >= 3 ? prev[i - 3] : 0;
        uint8_t predict = t == 0 ? 0
                        : t == 1 ? left
                        : t == 2 ? above
                        : t == 3 ? (uint8_t)((left + above) / 2)
                        : paeth(left, above, above_left);
        out[i] = (uint8_t)(row[i] - predict);
    }
}

bool png_write(const char *path, const uint8_t *rgb, int width, int height)
{
    size_t row_bytes = (size_t)width * 3;
    size_t raw_size = (row_bytes + 1) * (size_t)height;  // each row starts with its filter type
    uint8_t *raw = malloc(raw_size);
    uint8_t *zeros = calloc(1, row_bytes);
    uint8_t *trial = malloc(row_bytes);

    // Filter each row with whichever filter leaves the smallest differences
    // (counting each byte as a signed value): the usual rule of thumb for
    // what will compress best.
    for (int y = 0; y < height; y++) {
        const uint8_t *row = rgb + row_bytes * (size_t)y;
        const uint8_t *prev = y > 0 ? row - row_bytes : zeros;
        uint8_t *out = raw + (row_bytes + 1) * (size_t)y;
        long best_cost = -1;
        for (int t = 0; t <= 4; t++) {
            filter_row(t, row, prev, row_bytes, trial);
            long cost = 0;
            for (size_t i = 0; i < row_bytes; i++) cost += abs((int8_t)trial[i]);
            if (best_cost < 0 || cost < best_cost) {
                best_cost = cost;
                out[0] = (uint8_t)t;
                memcpy(out + 1, trial, row_bytes);
            }
        }
    }

    uLongf packed_size = compressBound((uLong)raw_size);
    uint8_t *packed = malloc(packed_size);
    bool ok = compress2(packed, &packed_size, raw, (uLong)raw_size, 9) == Z_OK;

    FILE *f = ok ? fopen(path, "wb") : NULL;
    if (f) {
        static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        uint8_t ihdr[13];
        put32(ihdr, (uint32_t)width);
        put32(ihdr + 4, (uint32_t)height);
        ihdr[8] = 8;   // bits per channel
        ihdr[9] = 2;   // colour type: RGB
        ihdr[10] = 0;  // compression: zlib
        ihdr[11] = 0;  // filtering: per-row filter types, as above
        ihdr[12] = 0;  // not interlaced
        ok = fwrite(signature, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) &&
             write_chunk(f, "IDAT", packed, (uint32_t)packed_size) && write_chunk(f, "IEND", NULL, 0);
        ok = fclose(f) == 0 && ok;
    } else {
        ok = false;
    }
    if (!ok) fprintf(stderr, "png: cannot write %s\n", path);

    free(packed);
    free(trial);
    free(zeros);
    free(raw);
    return ok;
}
