#include "audio/disc_audio.h"

#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Where in a clip's soundtrack does a frame's sound start? The film clips
// are 29.97 fps video, but measuring all 180 of them shows their soundtracks
// are laid out as if the video ran at exactly 30 fps (a 96 s clip's audio is
// 96.160 s long: 2885 frames / 30 = 96.167, not / 29.97 = 96.263). So: find
// the clip's MPEG frame index, as the video does, and divide by 30.
#define VIDEO_FPS (30000.0 / 1001)
#define AUDIO_FPS 30.0

#define BUFFER_SECONDS 0.2   // how much sound to keep queued ahead
#define RESYNC_SECONDS 0.15  // re-seek if sound and picture drift this far apart
#define OUTPUT_CHANNELS 2

struct DiscAudio {
    const Framefile *ff;
    double disc_fps;
    SDL_AudioStream *stream;  // queue of samples for the audio device
    int rate;                 // sample rate of what we're queueing

    // The clip whose soundtrack is open (or that turned out to have none).
    const FramefileClip *clip;
    bool clip_has_audio;
    AVFormatContext *format;
    AVCodecContext *decoder;
    int stream_index;
    AVPacket *packet;
    AVFrame *samples;
    bool draining, at_end;

    bool active;        // sound is queued and playing
    double queued_end;  // clip time (s) right after the last sample queued
    double skip_until;  // after a seek, drop decoded samples before this time
    bool channel1, channel2;

    float *buffer;      // interleaved samples on their way to SDL
    int buffer_frames;
};

static double clip_time(const DiscAudio *da, int offset)
{
    double mpeg_index = offset * VIDEO_FPS / da->disc_fps;
    return mpeg_index / AUDIO_FPS;
}

static void close_clip(DiscAudio *da)
{
    avcodec_free_context(&da->decoder);
    avformat_close_input(&da->format);
    da->clip = NULL;
    da->clip_has_audio = false;
}

// Open the soundtrack that goes with a clip: same name, .ogg instead of .m2v.
static void open_clip(DiscAudio *da, const FramefileClip *clip)
{
    close_clip(da);
    da->clip = clip;

    char path[1024];
    snprintf(path, sizeof path, "%s", clip->path);
    char *dot = strrchr(path, '.');
    if (dot && !strpbrk(dot, "/\\")) *dot = '\0';
    strncat(path, ".ogg", sizeof path - strlen(path) - 1);

    if (avformat_open_input(&da->format, path, NULL, NULL) < 0) return;  // a silent clip
    const AVCodec *codec = NULL;
    if (avformat_find_stream_info(da->format, NULL) < 0 ||
        (da->stream_index = av_find_best_stream(da->format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) < 0 ||
        !(da->decoder = avcodec_alloc_context3(codec)) ||
        avcodec_parameters_to_context(da->decoder, da->format->streams[da->stream_index]->codecpar) < 0 ||
        avcodec_open2(da->decoder, codec, NULL) < 0) {
        fprintf(stderr, "disc_audio: cannot decode %s\n", path);
        close_clip(da);
        da->clip = clip;
        return;
    }

    // Tell SDL the sample rate we'll be feeding it; it converts to whatever
    // the audio device wants.
    if (da->decoder->sample_rate != da->rate) {
        da->rate = da->decoder->sample_rate;
        SDL_AudioSpec spec = {SDL_AUDIO_F32, OUTPUT_CHANNELS, da->rate};
        SDL_SetAudioStreamFormat(da->stream, &spec, NULL);
    }
    da->clip_has_audio = true;
}

// Jump to time t (seconds) in the current clip's soundtrack.
static void seek(DiscAudio *da, double t)
{
    AVStream *st = da->format->streams[da->stream_index];
    int64_t ts = (int64_t)(t / av_q2d(st->time_base));
    av_seek_frame(da->format, da->stream_index, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(da->decoder);
    da->draining = da->at_end = false;
    da->skip_until = t;
    da->queued_end = t;
}

// Decode the next block of samples into da->samples. False at the end.
static bool decode_next(DiscAudio *da)
{
    for (;;) {
        int ret = avcodec_receive_frame(da->decoder, da->samples);
        if (ret == 0) return true;
        if (ret != AVERROR(EAGAIN) || da->draining) {
            da->at_end = true;
            return false;
        }
        if (av_read_frame(da->format, da->packet) < 0) {
            da->draining = true;
            avcodec_send_packet(da->decoder, NULL);
            continue;
        }
        if (da->packet->stream_index == da->stream_index)
            avcodec_send_packet(da->decoder, da->packet);
        av_packet_unref(da->packet);
    }
}

// Hand a decoded block to SDL: drop anything before skip_until, mute
// switched-off channels, and interleave the channels (left, right, left, ...).
static void queue_samples(DiscAudio *da)
{
    const AVFrame *s = da->samples;
    AVStream *st = da->format->streams[da->stream_index];
    double start = s->pts != AV_NOPTS_VALUE ? s->pts * av_q2d(st->time_base) : da->queued_end;

    int skip = 0;
    if (start < da->skip_until) skip = (int)((da->skip_until - start) * da->rate + 0.5);
    int count = s->nb_samples - skip;
    if (count <= 0) return;

    if (s->format != AV_SAMPLE_FMT_FLTP && s->format != AV_SAMPLE_FMT_FLT) {
        fprintf(stderr, "disc_audio: unsupported sample format %d\n", s->format);
        return;
    }
    if (count > da->buffer_frames) {
        da->buffer = realloc(da->buffer, (size_t)count * OUTPUT_CHANNELS * sizeof(float));
        da->buffer_frames = count;
    }

    int in_channels = s->ch_layout.nb_channels;
    for (int i = 0; i < count; i++) {
        float left, right;
        if (s->format == AV_SAMPLE_FMT_FLTP) {  // one array per channel
            left = ((const float *)s->data[0])[skip + i];
            right = in_channels > 1 ? ((const float *)s->data[1])[skip + i] : left;
        } else {  // already interleaved
            const float *frame = (const float *)s->data[0] + (size_t)(skip + i) * in_channels;
            left = frame[0];
            right = in_channels > 1 ? frame[1] : left;
        }
        da->buffer[2 * i] = da->channel1 ? left : 0.0f;
        da->buffer[2 * i + 1] = da->channel2 ? right : 0.0f;
    }
    SDL_PutAudioStreamData(da->stream, da->buffer, count * OUTPUT_CHANNELS * (int)sizeof(float));
    da->queued_end = fmax(start, da->skip_until) + (double)count / da->rate;
}

static double queued_seconds(const DiscAudio *da)
{
    int bytes = SDL_GetAudioStreamQueued(da->stream);
    return bytes > 0 ? (double)bytes / (da->rate * OUTPUT_CHANNELS * sizeof(float)) : 0.0;
}

static void silence(DiscAudio *da)
{
    if (da->active) SDL_ClearAudioStream(da->stream);
    da->active = false;
}

void disc_audio_update(DiscAudio *da, bool playing, int frame, bool channel1, bool channel2)
{
    int offset = 0;
    const FramefileClip *clip = playing ? framefile_find(da->ff, frame, &offset) : NULL;
    if (!clip) {
        silence(da);
        return;
    }
    if (clip != da->clip) {
        open_clip(da, clip);
        da->active = false;
    }
    if (!da->clip_has_audio) {
        silence(da);
        return;
    }

    // Where the sound should be, and where the listener actually is: the
    // end of what's queued, minus what's still waiting to be played.
    double want = clip_time(da, offset);
    double heard = da->queued_end - queued_seconds(da);

    // Past the end of the soundtrack (the picture can run a little longer):
    // let what's queued finish rather than seeking again and again.
    if (da->active && da->at_end && want >= da->queued_end) return;

    if (!da->active || fabs(heard - want) > RESYNC_SECONDS ||
        channel1 != da->channel1 || channel2 != da->channel2) {
        SDL_ClearAudioStream(da->stream);
        da->channel1 = channel1;
        da->channel2 = channel2;
        seek(da, want);
        da->active = true;
    }

    while (queued_seconds(da) < BUFFER_SECONDS && decode_next(da))
        queue_samples(da);
}

DiscAudio *disc_audio_open(const Framefile *ff, double disc_fps)
{
    DiscAudio *da = calloc(1, sizeof *da);
    if (!da) return NULL;
    da->ff = ff;
    da->disc_fps = disc_fps;
    da->rate = 44100;

    SDL_AudioSpec spec = {SDL_AUDIO_F32, OUTPUT_CHANNELS, da->rate};
    da->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!da->stream) {
        fprintf(stderr, "No sound: cannot open the audio device (%s)\n", SDL_GetError());
        free(da);
        return NULL;
    }
    SDL_ResumeAudioStreamDevice(da->stream);  // devices start paused

    da->packet = av_packet_alloc();
    da->samples = av_frame_alloc();
    return da;
}

void disc_audio_close(DiscAudio *da)
{
    if (!da) return;
    close_clip(da);
    SDL_DestroyAudioStream(da->stream);
    av_packet_free(&da->packet);
    av_frame_free(&da->samples);
    free(da->buffer);
    free(da);
}
