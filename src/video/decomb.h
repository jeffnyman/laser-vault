// decomb.h - find and repair "combing" in video frames.
//
// Laserdisc video is interlaced: each frame is two fields, the even lines
// and the odd lines, captured 1/60 s apart. When film is converted to video
// (3:2 pulldown), some frames end up with their two fields from different
// film frames, so anything moving shows a comb of alternating lines. A CRT
// draws the fields one after the other and nobody notices, but on a modern
// display, which shows the whole frame at once, the comb is plain to see.
//
// In the Dragon's Lair video about one frame in five is combed, and Cliff
// Hanger has long stretches where most frames are; the other discs were
// cleaned up when they were encoded. Usually only the moving part of the
// picture is combed (Dirk, say, over a still background), so the repair
// works pixel by pixel: where a pixel on an odd line stands out from the
// even lines above and below it the same way, as do its neighbours on the
// odd lines above or below, it's replaced by the average of those even
// lines. Everything else is left alone, so still parts keep their detail.

#ifndef LV_DECOMB_H
#define LV_DECOMB_H

#include <libavutil/frame.h>

// Copy the YUV 4:2:0 picture src into dst with its combing repaired, and
// return how many pixels were repaired (0: dst is a plain copy, and src can
// be used as it is). dst must be allocated with the same format and size.
long decomb_repair(AVFrame *dst, const AVFrame *src);

#endif
