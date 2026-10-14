// decomb.h - detect and repair "combed" video frames.
//
// Laserdisc video is interlaced: each frame is two fields, the even lines
// and the odd lines, captured 1/60 s apart. When film is converted to video
// (3:2 pulldown), some frames end up with their two fields from different
// film frames, so anything moving shows a comb of alternating lines. A CRT
// draws the fields one after the other and nobody notices, but on a modern
// display, which shows the whole frame at once, the comb is plain to see.
//
// In the Dragon's Lair video about one frame in five is combed, and not in
// a regular pattern, so each picture is checked on its own. Combed pictures
// are rebuilt from their even field alone: each odd line is replaced by the
// average of the even lines above and below it. That loses a little vertical
// detail on those frames, which is much less noticeable than the comb.

#ifndef LV_DECOMB_H
#define LV_DECOMB_H

#include <stdbool.h>

#include <libavutil/frame.h>

// Does this YUV 4:2:0 picture show combing?
bool decomb_is_combed(const AVFrame *pic);

// Copy src into dst, rebuilding the odd lines from the even ones. dst must
// be allocated with the same format and size as src.
void decomb_rebuild_from_even_field(AVFrame *dst, const AVFrame *src);

#endif
