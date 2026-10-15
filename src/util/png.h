// png.h - write a picture as a PNG file.
//
// A PNG file is an 8-byte signature followed by "chunks": a length, a
// four-letter type, the data, and a CRC-32 of the type and data. Three are
// enough: IHDR (size and pixel format), IDAT (the pixels, compressed with
// zlib) and IEND. Before compressing, each row of pixels is "filtered":
// stored as its difference from a prediction (the pixel to the left, the one
// above, ...), which makes pictures compress far better.

#ifndef LV_PNG_H
#define LV_PNG_H

#include <stdbool.h>
#include <stdint.h>

// Write width x height pixels, 3 bytes (red, green, blue) each, row by row
// from the top, to path. Returns false (after printing why) on failure.
bool png_write(const char *path, const uint8_t *rgb, int width, int height);

#endif
