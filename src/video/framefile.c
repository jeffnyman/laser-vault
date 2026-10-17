#include "video/framefile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#endif

static char *join_path(const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    size_t len = dir_len + 1 + strlen(name) + 1;
    char *out = malloc(len);
    if (!out) return NULL;
    bool has_sep = dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\');
    snprintf(out, len, "%s%s%s", dir, has_sep || dir_len == 0 ? "" : "/", name);
    return out;
}

// Directory part of a path ("a/b/c.txt" -> "a/b"; "c.txt" -> "").
static char *dir_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash > slash) slash = backslash;
    size_t len = slash ? (size_t)(slash - path) : 0;
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

// The path to a clip. Framefiles are often made on Windows, where a name's
// capitals don't matter, and don't always match the files
// ("CH_640x480_24p.m2v" for ch_640x480_24p.m2v); elsewhere, if the name
// as written isn't there, use the file whose name differs only in case.
static char *clip_path(const char *dir, const char *name)
{
    char *path = join_path(dir, name);
#ifndef _WIN32
    struct stat st;
    if (!path || stat(path, &st) == 0 || strchr(name, '/')) return path;
    DIR *d = opendir(*dir ? dir : ".");
    if (!d) return path;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcasecmp(e->d_name, name) == 0) {
            free(path);
            path = join_path(dir, e->d_name);
            break;
        }
    }
    closedir(d);
#endif
    return path;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static int compare_clips(const void *a, const void *b)
{
    const FramefileClip *ca = a, *cb = b;
    return (ca->start_frame > cb->start_frame) - (ca->start_frame < cb->start_frame);
}

bool framefile_load(Framefile *ff, const char *path, const char *video_dir)
{
    memset(ff, 0, sizeof *ff);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "framefile: cannot open %s\n", path);
        return false;
    }

    char *dir = video_dir ? strdup(video_dir) : dir_of(path);
    int capacity = 0;
    bool first_line = true;
    char line[1024];

    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (first_line) {  // the clip directory; ignored, see header
            first_line = false;
            continue;
        }
        if (*s == '\0') continue;

        // "<start frame> <whitespace> <file name>"
        char *name;
        long start = strtol(s, &name, 10);
        name = trim(name);
        if (name == s || *name == '\0') {
            fprintf(stderr, "framefile: skipping malformed line: %s\n", s);
            continue;
        }

        if (ff->clip_count == capacity) {
            capacity = capacity ? capacity * 2 : 64;
            ff->clips = realloc(ff->clips, (size_t)capacity * sizeof *ff->clips);
        }
        ff->clips[ff->clip_count].start_frame = (int)start;
        ff->clips[ff->clip_count].path = clip_path(dir, name);
        ff->clip_count++;
    }

    fclose(f);
    free(dir);

    if (ff->clip_count == 0) {
        fprintf(stderr, "framefile: no clips found in %s\n", path);
        framefile_free(ff);
        return false;
    }
    qsort(ff->clips, (size_t)ff->clip_count, sizeof *ff->clips, compare_clips);
    return true;
}

void framefile_free(Framefile *ff)
{
    for (int i = 0; i < ff->clip_count; i++) free(ff->clips[i].path);
    free(ff->clips);
    memset(ff, 0, sizeof *ff);
}

const FramefileClip *framefile_find(const Framefile *ff, int frame, int *offset)
{
    // Binary search for the last clip with start_frame <= frame.
    int lo = 0, hi = ff->clip_count - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ff->clips[mid].start_frame <= frame) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (found < 0) return NULL;
    *offset = frame - ff->clips[found].start_frame;
    return &ff->clips[found];
}
