#include "ui/icon.h"

#include <stdio.h>
#include <string.h>

// .ico files store numbers little-endian.
static uint32_t get16(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (get16(p + 2) << 16); }

// Decode an icon's Windows bitmap. It's a BMP without the file header:
//
//   - a 40-byte header (width, then height *doubled*, bits per pixel, ...);
//   - for 1, 4 and 8 bits per pixel, a palette of blue-green-red-unused;
//   - the pixels, bottom row first, each row padded to a multiple of 4 bytes;
//   - then the "AND mask": one bit per pixel, 1 = transparent, also bottom
//     row first and padded. (The doubled height counts this second image.)
static SDL_Surface *decode_dib(const uint8_t *data, size_t size)
{
    if (size < 40 || get32(data) < 40) return NULL;
    int width = (int)get32(data + 4);
    int height = (int)get32(data + 8) / 2;
    int bpp = (int)get16(data + 14);
    if (get32(data + 16) != 0 || width <= 0 || height <= 0) return NULL;  // compressed or odd
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return NULL;

    uint32_t colors = get32(data + 32);
    if (bpp <= 8 && colors == 0) colors = 1u << bpp;
    const uint8_t *palette = data + get32(data);
    const uint8_t *pixels = palette + (bpp <= 8 ? colors * 4 : 0);
    size_t row_bytes = (size_t)((width * bpp + 31) / 32) * 4;
    const uint8_t *mask = pixels + row_bytes * (size_t)height;
    size_t mask_row_bytes = (size_t)((width + 31) / 32) * 4;
    if (mask + mask_row_bytes * (size_t)height > data + size) return NULL;

    // 32-bit icons carry their own alpha; older ones rely on the mask.
    bool has_alpha = false;
    if (bpp == 32)
        for (int i = 0; i < width * height && !has_alpha; i++) has_alpha = pixels[i * 4 + 3] != 0;

    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return NULL;
    for (int y = 0; y < height; y++) {
        const uint8_t *row = pixels + row_bytes * (size_t)(height - 1 - y);  // bottom-up
        const uint8_t *mask_row = mask + mask_row_bytes * (size_t)(height - 1 - y);
        uint8_t *out = (uint8_t *)surface->pixels + (size_t)y * (size_t)surface->pitch;
        for (int x = 0; x < width; x++) {
            uint8_t b, g, r, a = 255;
            if (bpp >= 24) {
                const uint8_t *px = row + x * (bpp / 8);
                b = px[0]; g = px[1]; r = px[2];
                if (has_alpha) a = px[3];
            } else {
                int bit = x * bpp;
                int index = (row[bit / 8] >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1);
                const uint8_t *c = palette + index * 4;
                b = c[0]; g = c[1]; r = c[2];
            }
            if (!has_alpha && (mask_row[x / 8] & (0x80 >> (x % 8)))) a = 0;
            out[x * 4 + 0] = r;
            out[x * 4 + 1] = g;
            out[x * 4 + 2] = b;
            out[x * 4 + 3] = a;
        }
    }
    return surface;
}

SDL_Surface *icon_load_memory(const void *contents, size_t size)
{
    const uint8_t *file = contents;

    // Header: reserved (0), type (1 = icon), number of images. Then one
    // 16-byte directory entry per image: width, height (0 means 256), ...,
    // data size, data offset.
    SDL_Surface *surface = NULL;
    if (size >= 6 && get16(file) == 0 && get16(file + 2) == 1) {
        uint32_t count = get16(file + 4);
        const uint8_t *best = NULL;
        int best_size = 0;
        for (uint32_t i = 0; i < count && 6 + 16 * (i + 1) <= size; i++) {
            const uint8_t *entry = file + 6 + 16 * i;
            int w = entry[0] ? entry[0] : 256;
            if (w > best_size && get32(entry + 12) + get32(entry + 8) <= size) {
                best = entry;
                best_size = w;
            }
        }
        if (best) {
            const uint8_t *data = file + get32(best + 12);
            size_t data_size = get32(best + 8);
            static const uint8_t png_signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
            if (data_size >= 8 && memcmp(data, png_signature, 8) == 0) {
                // SDL reads PNG from version 3.4 on; with an older SDL,
                // icons need their images stored as bitmaps.
#if SDL_VERSION_ATLEAST(3, 4, 0)
                surface = SDL_LoadPNG_IO(SDL_IOFromConstMem(data, data_size), true);
#endif
            } else {
                surface = decode_dib(data, data_size);
            }
        }
    }
    return surface;
}

SDL_Surface *icon_load(const char *path)
{
    size_t size;
    void *file = SDL_LoadFile(path, &size);
    if (!file) {
        fprintf(stderr, "icon: cannot read %s\n", path);
        return NULL;
    }
    SDL_Surface *surface = icon_load_memory(file, size);
    if (!surface) fprintf(stderr, "icon: %s is not an icon this can read\n", path);
    SDL_free(file);
    return surface;
}
