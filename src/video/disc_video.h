// disc_video.h - fetch the picture for any laserdisc frame number.
//
// This is the "video side" of a laserdisc: ask for frame N, get a decoded
// picture back. Underneath, it finds the right MPEG-2 clip via the framefile
// and decodes it with FFmpeg.
//
// Seeking is the interesting part. MPEG-2 stores most frames as differences
// from their neighbours; only keyframes (I-frames) can be decoded on their
// own. For now we take the simplest correct approach: to reach frame K of a
// clip, decode from the start of the clip. Moving forward just continues
// decoding, so playing and stepping forward are cheap; jumping backwards
// means starting over from the top of the clip.
//
// Frame rates: disc frame numbers and MPEG frames don't always tick at the
// same rate. Dragon's Lair was animated on film at 24 frames per second and
// its frame numbers count those film frames, but the MPEG clips were made
// from NTSC video at 29.97 frames per second (3:2 pulldown: every 4 film
// frames become 5 video frames). So one second of disc is 24 frame numbers
// but 30 MPEG frames, and frame offsets within a clip must be scaled by
// mpeg_fps / disc_fps (= 1.25) to find the right picture.

#ifndef LV_DISC_VIDEO_H
#define LV_DISC_VIDEO_H

#include "video/framefile.h"

#include <libavutil/frame.h>

typedef struct DiscVideo DiscVideo;

// disc_fps is the rate the game's frame numbers count at (23.976 for
// Dragon's Lair). The framefile must outlive the DiscVideo.
DiscVideo *disc_video_open(const Framefile *ff, double disc_fps);
void disc_video_close(DiscVideo *dv);

// Decode and return the picture for a disc frame, or NULL if there is no
// video there. If the frame is past the end of its clip, the clip's last
// frame is returned. The frame stays valid until the next call.
const AVFrame *disc_video_get_frame(DiscVideo *dv, int frame);

#endif
