// recorder.h - write what the emulator shows and plays to a video file.
//
// For recording a game as a video (laser-vault --record): the main program
// hands over each picture it draws and the sound that goes with it, and
// this encodes them into an MP4 file, video and stereo sound together.
//
// The picture is encoded as H.264 where the system offers an encoder for it
// (on Windows, through Media Foundation), or else with FFmpeg's own MPEG-4
// encoder at a high bitrate; the sound as AAC. Either plays anywhere and
// can be uploaded to video sites, which re-encode it anyway.

#ifndef LV_RECORDER_H
#define LV_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Recorder Recorder;

// Open path for writing: width x height pictures (both even) at
// fps_num/fps_den frames a second, with sound at sample_rate. Returns NULL
// (after printing why) if it can't.
Recorder *recorder_open(const char *path, int width, int height, int fps_num, int fps_den, int sample_rate);

// Add the next picture: pixels as 0xXXRRGGBB words, pitch bytes per row.
bool recorder_add_picture(Recorder *r, const uint32_t *pixels, int pitch);

// Add sound: frames pairs of samples (left, right), -1..1. Sound and
// pictures are timed separately; give them in step and they stay in step.
bool recorder_add_sound(Recorder *r, const float *samples, int frames);

// Finish the file (flushing the encoders) and free everything. Returns
// false if anything failed to write.
bool recorder_close(Recorder *r);

// What the picture is encoded with ("H.264" or "MPEG-4"), for messages.
const char *recorder_video_codec(const Recorder *r);

#endif
