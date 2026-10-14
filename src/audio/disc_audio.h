// disc_audio.h - play the laserdisc's soundtrack in step with the picture.
//
// A laserdisc carries two analog audio channels alongside the video. Here
// each video clip foo.m2v has its sound in foo.ogg (Ogg Vorbis, stereo).
// The left channel stands for the disc's audio channel 1 and the right for
// channel 2, which a game can switch on and off.
//
// This module doesn't keep time itself. Each pass of the main loop it's told
// what the player is doing (playing or not, and which frame), and it keeps
// a short buffer of decoded sound queued for the speakers to match. If the
// picture jumps (a search, a new clip) or the sound drifts away from it, it
// seeks the audio to the right place again.

#ifndef LV_DISC_AUDIO_H
#define LV_DISC_AUDIO_H

#include "video/framefile.h"

#include <stdbool.h>

typedef struct DiscAudio DiscAudio;

// Open the default audio output. Returns NULL (after printing why) if there
// isn't one; the caller can carry on without sound. The framefile must
// outlive the DiscAudio. Needs SDL's audio subsystem initialized.
DiscAudio *disc_audio_open(const Framefile *ff, double disc_fps);
void disc_audio_close(DiscAudio *da);

// Make the sound match the player. playing: the disc is playing forward at
// normal speed (sound is silenced otherwise, as on a real player). frame:
// the disc frame being shown. channel1/channel2: the audio channel switches.
void disc_audio_update(DiscAudio *da, bool playing, int frame, bool channel1, bool channel2);

#endif
