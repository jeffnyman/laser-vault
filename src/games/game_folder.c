#include "games/game_folder.h"

#include "games/machine.h"

#include <SDL3/SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool game_folder_is_folder(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static int compare_names(const void *a, const void *b)
{
    return SDL_strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

// The files in dir matching pattern (such as "*.txt"), sorted by name, so
// the choice never depends on the order the system lists them in. Free
// with SDL_free.
static char **list(const char *dir, const char *pattern, int *count)
{
    char **names = SDL_GlobDirectory(dir, pattern, SDL_GLOB_CASEINSENSITIVE, count);
    if (!names) {
        *count = 0;
        return NULL;
    }
    SDL_qsort(names, (size_t)*count, sizeof *names, compare_names);
    return names;
}

// The folder's name up to its first dot: "astron" for path/to/astron.daphne/.
static void folder_stem(const char *dir, char *out, size_t size)
{
    size_t end = strlen(dir);
    while (end > 0 && (dir[end - 1] == '/' || dir[end - 1] == '\\')) end--;
    size_t start = end;
    while (start > 0 && dir[start - 1] != '/' && dir[start - 1] != '\\') start--;
    size_t n = 0;
    for (size_t i = start; i < end && dir[i] != '.' && n + 1 < size; i++) out[n++] = dir[i];
    out[n] = '\0';
}

// Whether file (a name in the folder) is the folder's name plus an
// extension: astron.txt or astron.zip in astron.daphne.
static bool named_like(const char *file, const char *stem)
{
    size_t n = strlen(stem);
    return n > 0 && SDL_strncasecmp(file, stem, n) == 0 && file[n] == '.' && !strchr(file + n + 1, '.');
}

// Whether the file looks like a framefile: somewhere near the top, a line
// that's a frame number and a clip's file name ("150  dl-slates.m2v"). The
// number can be negative, for a clip that starts before the disc's frame 0
// ("-80  mach3.m2v"). Readme files and the like don't have one.
static bool looks_like_framefile(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    bool found = false;
    for (int lines = 0; !found && lines < 64 && fgets(line, sizeof line, f); lines++) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '-') p++;
        if (!isdigit((unsigned char)*p)) continue;
        while (isdigit((unsigned char)*p)) p++;
        if (*p != ' ' && *p != '\t') continue;
        while (*p == ' ' || *p == '\t') p++;
        const char *name = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        found = p > name && memchr(name, '.', (size_t)(p - name)) != NULL;
    }
    fclose(f);
    return found;
}

#ifdef _WIN32
#define SEPARATOR "\\"
#else
#define SEPARATOR "/"
#endif

static void join(char *out, size_t size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    bool slash = n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\');
    snprintf(out, size, "%s%s%s", dir, slash ? "" : SEPARATOR, name);
}

static bool find_framefile(const char *dir, const char *stem, char *out, size_t size)
{
    int count;
    char **names = list(dir, "*.txt", &count);
    int best = -1;
    char path[1024];
    for (int i = 0; i < count; i++) {
        join(path, sizeof path, dir, names[i]);
        if (!looks_like_framefile(path)) continue;
        if (best < 0 || named_like(names[i], stem)) best = i;
        if (named_like(names[i], stem)) break;
    }
    if (best >= 0) join(out, size, dir, names[best]);
    SDL_free(names);
    return best >= 0;
}

static void find_roms(const char *dir, const char *stem, char *out, size_t size)
{
    // "roms", however it's capitalized (Linux's file names care).
    char roms_dir[1024];
    int count;
    char **names = list(dir, "roms", &count);
    join(roms_dir, sizeof roms_dir, dir, count > 0 ? names[0] : "roms");
    SDL_free(names);
    out[0] = '\0';
    if (!game_folder_is_folder(roms_dir)) return;

    names = list(roms_dir, "*.zip", &count);
    int best = -1;
    char path[1024];
    for (int i = 0; i < count; i++) {
        join(path, sizeof path, roms_dir, names[i]);
        if (!machine_recognizes(path)) continue;
        if (best < 0 || named_like(names[i], stem)) best = i;
        if (named_like(names[i], stem)) break;
    }
    if (best >= 0) {
        join(out, size, roms_dir, names[best]);
    } else if (machine_recognizes(roms_dir) || count == 0) {
        // Loose ROM files, or nothing a board knows: then loading the folder
        // explains what it expected.
        snprintf(out, size, "%s", roms_dir);
    } else {
        // Zips, but none a board knows: load the likeliest, so the message
        // names it.
        int pick = 0;
        for (int i = 0; i < count; i++)
            if (named_like(names[i], stem)) pick = i;
        join(out, size, roms_dir, names[pick]);
    }
    SDL_free(names);
}

bool game_folder_find(const char *dir, char *framefile, size_t framefile_size, char *roms, size_t roms_size)
{
    char stem[256];
    folder_stem(dir, stem, sizeof stem);
    if (!find_framefile(dir, stem, framefile, framefile_size)) {
        fprintf(stderr, "%s has no framefile: a .txt file listing the video clips with\n"
                        "the frame each starts at. A game's folder holds the framefile and the\n"
                        "clips, with the ROMs in a roms folder (see PLAYING.md).\n", dir);
        return false;
    }
    find_roms(dir, stem, roms, roms_size);
    return true;
}
