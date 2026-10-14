// framefile.h - map laserdisc frame numbers to MPEG clip files.
//
// A real laserdisc holds ~54,000 frames, each addressed by a frame number.
// Emulators don't keep the disc as one giant video; instead the video is
// split into clips, and a "framefile" (a format inherited from Daphne) says
// which disc frame each clip starts at:
//
//     E:\Emulators\Daphne\mpeg\dragonslair     <- line 1: clip directory
//                                              <- (blank lines are ignored)
//     150     dl-slates.m2v                    <- clip starts at frame 150
//     323     dls00.vob.m2v                    <- clip starts at frame 323
//
// Disc frame F lives in the clip with the largest start frame <= F, at
// offset (F - start) frames into that clip.

#ifndef LV_FRAMEFILE_H
#define LV_FRAMEFILE_H

#include <stdbool.h>

typedef struct {
    int start_frame;  // first disc frame in this clip
    char *path;       // full path to the .m2v file
} FramefileClip;

typedef struct {
    FramefileClip *clips;  // sorted by start_frame
    int clip_count;
} Framefile;

// Load a framefile. Clip names are resolved against video_dir. If video_dir
// is NULL, the directory containing the framefile is used. (The directory
// on line 1 is ignored: in practice it is often a stale absolute path from
// whoever made the files.)
bool framefile_load(Framefile *ff, const char *path, const char *video_dir);
void framefile_free(Framefile *ff);

// Find the clip containing a disc frame. Returns NULL if the frame comes
// before the first clip. *offset receives the frame's index within the clip.
const FramefileClip *framefile_find(const Framefile *ff, int frame, int *offset);

#endif
