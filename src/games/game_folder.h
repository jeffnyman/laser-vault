// Finding a game's files in its folder, the way Daphne/Singe sets are
// usually laid out:
//
//   astron.daphne/
//       astron.txt         the framefile
//       astron512.m2v      the video and its soundtrack, beside it
//       astron512.ogg
//       roms/astron.zip    the ROM set (or several zips, or loose ROM files)
//       sounds/            recorded sound effects, if the game needs them
//
// so that `laser-vault astron.daphne` is enough to play.
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Whether path is a folder (rather than a framefile).
bool game_folder_is_folder(const char *path);

// Find the framefile and the ROM set in the folder at dir. The framefile is
// a .txt file in the folder itself that lists clips; if there are several,
// the one named like the folder ("astron" for astron.daphne) wins. The ROM
// set is the zip in roms/ that a board recognizes (again preferring the
// folder's name), or roms/ itself if it holds loose ROM files a board
// knows. roms is left empty if there's no roms/ folder, which means
// playing the disc without the game. Returns false (after printing why) if
// there's no framefile.
bool game_folder_find(const char *dir, char *framefile, size_t framefile_size, char *roms, size_t roms_size);
