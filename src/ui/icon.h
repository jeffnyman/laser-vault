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

// Icons built into the program from icons/, each called by its file's name
// ("dlair" for icons/dlair.ico); CMakeLists.txt lists them in
// EMBEDDED_ICONS and makes the table.
typedef struct {
    const char *name;
    const unsigned char *data;
    const unsigned long *size;
} EmbeddedIcon;

extern const EmbeddedIcon embedded_icons[];
extern const int embedded_icon_count;

// Load the built-in icon called name. Returns NULL if there's none.
SDL_Surface *icon_load_embedded(const char *name);

#endif
