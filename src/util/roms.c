#include "util/roms.h"
#include "util/zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ROM chips are small; nothing in a set is bigger than this.
#define MAX_CHIP_SIZE 0x10000

static bool ends_with_zip(const char *path)
{
    size_t n = strlen(path);
    return n >= 4 && (strcmp(path + n - 4, ".zip") == 0 || strcmp(path + n - 4, ".ZIP") == 0);
}

bool roms_read(const char *path, const char *name, uint8_t **data, size_t *size)
{
    uint32_t zip_crc;
    if (ends_with_zip(path)) return zip_read_file(path, name, data, size, &zip_crc);

    char file[1024];
    snprintf(file, sizeof file, "%s/%s", path, name);
    FILE *f = fopen(file, "rb");
    if (!f) return false;
    *data = malloc(MAX_CHIP_SIZE);
    *size = fread(*data, 1, MAX_CHIP_SIZE, f);
    fclose(f);
    return true;
}

bool roms_has(const char *path, const char *name)
{
    uint8_t *data = NULL;
    size_t size;
    if (!roms_read(path, name, &data, &size)) return false;
    free(data);
    return true;
}
