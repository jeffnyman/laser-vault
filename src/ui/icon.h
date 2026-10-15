// icon.h - load a Windows .ico file as an image for the window icon.
//
// An .ico file is a small container: a directory listing one or more
// images of different sizes, each stored either as a PNG or as a Windows
// bitmap ("DIB"). SDL can read PNG but not the container or the bitmap
// variant used in icons, so this does that part.

#ifndef LV_ICON_H
#define LV_ICON_H

#include <SDL3/SDL.h>

// Load the largest image in an .ico file. Returns NULL (after printing why)
// on failure. Free the surface with SDL_DestroySurface.
SDL_Surface *icon_load(const char *path);

// The same, for .ico data already in memory.
SDL_Surface *icon_load_memory(const void *data, size_t size);

// Icons built into the program from icons/ (see CMakeLists.txt): Dragon's
// Lair's own, and the one the other games share.
extern const unsigned char icon_dlair[];
extern const unsigned long icon_dlair_size;
extern const unsigned char icon_laser[];
extern const unsigned long icon_laser_size;

#endif
