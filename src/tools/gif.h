// gif.h - a small animated GIF writer.
//
// GIF images can hold at most 256 colours, so writing one takes two steps:
//
// 1. Choose a palette. We count every colour that appears across all the
//    frames (a histogram), then use "median cut" to pick 255 representative
//    colours. One palette for the whole animation avoids colours flickering
//    from frame to frame.
// 2. Write the frames as palette indices, compressed with LZW, the
//    compression scheme the GIF format is built on.
//
// To keep files small, each frame after the first only stores the rectangle
// that changed, with unchanged pixels inside it marked transparent so the
// previous frame shows through. Identical consecutive frames are merged.
// Video is noisy (film grain makes pixels flicker between similar colours),
// so pixels whose colour changed only slightly can be counted as unchanged.

#ifndef LV_GIF_H
#define LV_GIF_H

#include <stdbool.h>
#include <stdint.h>

// Colours are reduced to 5 bits per channel (32,768 possible colours) for
// counting and lookup.
#define GIF_COLOR_KEYS 32768

typedef struct {
    uint32_t counts[GIF_COLOR_KEYS];
} GifHistogram;

typedef struct {
    uint8_t rgb[256][3];
    int count;
    uint8_t lookup[GIF_COLOR_KEYS];  // 5-bit colour -> nearest palette index
} GifPalette;

typedef struct GifWriter GifWriter;

// Add pixels (packed RGB, 3 bytes each) to the histogram.
void gif_histogram_add(GifHistogram *hist, const uint8_t *rgb, int pixel_count);

// Pick up to max_colors (at most 255) colours from the histogram.
void gif_palette_build(GifPalette *pal, const GifHistogram *hist, int max_colors);

// Convert RGB pixels to palette indices.
void gif_palette_map(const GifPalette *pal, const uint8_t *rgb, int pixel_count,
                     uint8_t *indices);

// noise is the largest colour change (distance in RGB, 0-441) still treated
// as "unchanged"; 0 keeps every change.
GifWriter *gif_open(const char *path, int width, int height, const GifPalette *pal,
                    int noise);

// Add a frame of width * height palette indices, shown for delay_cs
// hundredths of a second.
bool gif_add_frame(GifWriter *gif, const uint8_t *indices, int delay_cs);

// Finish the file. Returns false if anything failed to write.
bool gif_close(GifWriter *gif);

#endif
