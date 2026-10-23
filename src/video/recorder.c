#include "video/recorder.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A video file is a container (here MP4) holding streams (one picture, one
// sound), each made of packets that an encoder produced. The encoders want
// their input in their own formats: the picture as YUV, a brightness plane
// and two half-size colour planes, and the sound in blocks of a fixed number
// of samples, one array per channel. So this converts and buffers, then
// passes each encoded packet to the container in time order.

#define VIDEO_BITRATE 16000000  // generous: video sites re-encode anyway
#define SOUND_BITRATE 192000

struct Recorder {
    AVFormatContext *file;
    AVCodecContext *video, *sound;
    AVStream *video_stream, *sound_stream;
    AVFrame *picture, *block;  // what goes into each encoder
    AVPacket *packet;
    const char *video_codec;
    int64_t pictures;          // pictures given so far (their timestamps)
    int64_t samples;           // samples encoded so far (likewise)
    float *pending;            // sound waiting to fill a block, interleaved
    int pending_frames, pending_capacity;
    bool failed;
};

// Take every packet the encoder has ready and put it in the file.
static void write_packets(Recorder *r, AVCodecContext *enc, AVStream *st)
{
    while (avcodec_receive_packet(enc, r->packet) == 0) {
        av_packet_rescale_ts(r->packet, enc->time_base, st->time_base);
        r->packet->stream_index = st->index;
        if (av_interleaved_write_frame(r->file, r->packet) < 0) r->failed = true;
    }
}

static void encode(Recorder *r, AVCodecContext *enc, AVStream *st, const AVFrame *frame)
{
    if (avcodec_send_frame(enc, frame) < 0) r->failed = true;
    write_packets(r, enc, st);
}

// Does the encoder take this pixel format? (FFmpeg 7.1 changed how to ask;
// older versions, such as Ubuntu 24.04's, have only the list in the codec.)
static bool takes_format(const AVCodecContext *ctx, const AVCodec *codec, enum AVPixelFormat want)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void *list = NULL;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &list, &count) < 0) return false;
    if (!list) return want == AV_PIX_FMT_YUV420P;  // no list: assume the usual
    const enum AVPixelFormat *formats = list;
    for (int i = 0; i < count; i++)
        if (formats[i] == want) return true;
    return false;
#else
    (void)ctx;
    if (!codec->pix_fmts) return want == AV_PIX_FMT_YUV420P;
    for (const enum AVPixelFormat *f = codec->pix_fmts; *f != AV_PIX_FMT_NONE; f++)
        if (*f == want) return true;
    return false;
#endif
}

// Set up a picture encoder by name; NULL if the system doesn't have it or
// it won't open with these settings.
static AVCodecContext *open_video(Recorder *r, const char *name, int width, int height, int fps_num, int fps_den)
{
    const AVCodec *codec = avcodec_find_encoder_by_name(name);
    if (!codec) return NULL;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx) return NULL;
    ctx->width = width;
    ctx->height = height;
    ctx->time_base = (AVRational){fps_den, fps_num};
    ctx->framerate = (AVRational){fps_num, fps_den};
    ctx->bit_rate = VIDEO_BITRATE;
    ctx->gop_size = 15;  // a keyframe every half second
    ctx->sample_aspect_ratio = (AVRational){1, 1};
    if (takes_format(ctx, codec, AV_PIX_FMT_YUV420P)) ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    else if (takes_format(ctx, codec, AV_PIX_FMT_NV12)) ctx->pix_fmt = AV_PIX_FMT_NV12;
    else {
        avcodec_free_context(&ctx);
        return NULL;
    }
    if (r->file->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    // Media Foundation's H.264 encoder: aim for quality rather than speed.
    if (strcmp(name, "h264_mf") == 0) av_opt_set(ctx->priv_data, "scenario", "archive", 0);
    // FFmpeg's MPEG-4 encoder falls well short of a bitrate asked for, so
    // give it a fixed quality instead (a quantizer of 3: close to the
    // original, in files about twice the size).
    if (strcmp(name, "mpeg4") == 0) {
        ctx->flags |= AV_CODEC_FLAG_QSCALE;
        ctx->global_quality = FF_QP2LAMBDA * 3;
    }
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return NULL;
    }
    return ctx;
}

Recorder *recorder_open(const char *path, int width, int height, int fps_num, int fps_den, int sample_rate)
{
    Recorder *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    if (avformat_alloc_output_context2(&r->file, NULL, NULL, path) < 0 || !r->file) {
        fprintf(stderr, "Recording: can't make a video file called %s (try a name ending in .mp4)\n", path);
        free(r);
        return NULL;
    }

    // The picture: H.264 if the system has an encoder that will open, else
    // FFmpeg's own MPEG-4 Part 2, which is always there.
    r->video_codec = "H.264";
    r->video = open_video(r, "h264_mf", width, height, fps_num, fps_den);
    if (!r->video) {
        r->video_codec = "MPEG-4";
        r->video = open_video(r, "mpeg4", width, height, fps_num, fps_den);
    }

    // The sound: AAC, stereo.
    const AVCodec *aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (aac && (r->sound = avcodec_alloc_context3(aac))) {
        r->sound->sample_rate = sample_rate;
        r->sound->sample_fmt = AV_SAMPLE_FMT_FLTP;
        r->sound->bit_rate = SOUND_BITRATE;
        r->sound->time_base = (AVRational){1, sample_rate};
        av_channel_layout_default(&r->sound->ch_layout, 2);
        if (r->file->oformat->flags & AVFMT_GLOBALHEADER) r->sound->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(r->sound, aac, NULL) < 0) avcodec_free_context(&r->sound);
    }
    if (!r->video || !r->sound) {
        fprintf(stderr, "Recording: can't set up the %s encoder\n", r->video ? "sound" : "picture");
        recorder_close(r);
        return NULL;
    }

    r->video_stream = avformat_new_stream(r->file, NULL);
    r->sound_stream = avformat_new_stream(r->file, NULL);
    r->picture = av_frame_alloc();
    r->block = av_frame_alloc();
    r->packet = av_packet_alloc();
    if (!r->video_stream || !r->sound_stream || !r->picture || !r->block || !r->packet) {
        recorder_close(r);
        return NULL;
    }
    avcodec_parameters_from_context(r->video_stream->codecpar, r->video);
    avcodec_parameters_from_context(r->sound_stream->codecpar, r->sound);
    r->video_stream->time_base = r->video->time_base;
    r->sound_stream->time_base = r->sound->time_base;

    r->picture->format = r->video->pix_fmt;
    r->picture->width = width;
    r->picture->height = height;
    r->block->format = AV_SAMPLE_FMT_FLTP;
    r->block->sample_rate = sample_rate;
    r->block->nb_samples = r->sound->frame_size ? r->sound->frame_size : 1024;
    av_channel_layout_copy(&r->block->ch_layout, &r->sound->ch_layout);
    if (av_frame_get_buffer(r->picture, 0) < 0 || av_frame_get_buffer(r->block, 0) < 0 ||
        avio_open(&r->file->pb, path, AVIO_FLAG_WRITE) < 0 || avformat_write_header(r->file, NULL) < 0) {
        fprintf(stderr, "Recording: can't write %s\n", path);
        recorder_close(r);
        return NULL;
    }
    return r;
}

const char *recorder_video_codec(const Recorder *r)
{
    return r->video_codec;
}

// RGB to YUV, as studio video has it (BT.601, brightness 16-235 and colour
// 16-240), in fixed point. Each colour sample stands for a 2x2 block of
// pixels, so it's taken from their average.
static inline uint8_t luma(int red, int green, int blue)
{
    return (uint8_t)((66 * red + 129 * green + 25 * blue + 128 + (16 << 8)) >> 8);
}

bool recorder_add_picture(Recorder *r, const uint32_t *pixels, int pitch)
{
    if (av_frame_make_writable(r->picture) < 0) return false;
    AVFrame *f = r->picture;
    bool nv12 = f->format == AV_PIX_FMT_NV12;  // colour interleaved in one plane
    for (int y = 0; y < f->height; y += 2) {
        const uint32_t *row0 = (const uint32_t *)((const uint8_t *)pixels + (size_t)y * pitch);
        const uint32_t *row1 = (const uint32_t *)((const uint8_t *)pixels + (size_t)(y + 1) * pitch);
        uint8_t *y0 = f->data[0] + (size_t)y * f->linesize[0];
        uint8_t *y1 = y0 + f->linesize[0];
        uint8_t *u = f->data[1] + (size_t)(y / 2) * f->linesize[1];
        uint8_t *v = nv12 ? NULL : f->data[2] + (size_t)(y / 2) * f->linesize[2];
        for (int x = 0; x < f->width; x += 2) {
            uint32_t p[4] = {row0[x], row0[x + 1], row1[x], row1[x + 1]};
            int red = 0, green = 0, blue = 0;
            for (int i = 0; i < 4; i++) {
                int pr = (p[i] >> 16) & 0xFF, pg = (p[i] >> 8) & 0xFF, pb = p[i] & 0xFF;
                uint8_t lum = luma(pr, pg, pb);
                if (i == 0) y0[x] = lum;
                else if (i == 1) y0[x + 1] = lum;
                else if (i == 2) y1[x] = lum;
                else y1[x + 1] = lum;
                red += pr;
                green += pg;
                blue += pb;
            }
            red /= 4;
            green /= 4;
            blue /= 4;
            uint8_t cb = (uint8_t)((-38 * red - 74 * green + 112 * blue + 128 + (128 << 8)) >> 8);
            uint8_t cr = (uint8_t)((112 * red - 94 * green - 18 * blue + 128 + (128 << 8)) >> 8);
            if (nv12) {
                u[x] = cb;
                u[x + 1] = cr;
            } else {
                u[x / 2] = cb;
                v[x / 2] = cr;
            }
        }
    }
    f->pts = r->pictures++;
    f->quality = r->video->global_quality;  // (the fixed quality, where there is one)
    encode(r, r->video, r->video_stream, f);
    return !r->failed;
}

// Encode one full block of sound from the front of what's pending.
static void encode_block(Recorder *r, int frames)
{
    AVFrame *b = r->block;
    av_frame_make_writable(b);
    float *left = (float *)b->data[0], *right = (float *)b->data[1];
    for (int i = 0; i < b->nb_samples; i++) {
        left[i] = i < frames ? r->pending[2 * i] : 0.0f;
        right[i] = i < frames ? r->pending[2 * i + 1] : 0.0f;
    }
    b->pts = r->samples;
    r->samples += b->nb_samples;
    encode(r, r->sound, r->sound_stream, b);
}

bool recorder_add_sound(Recorder *r, const float *samples, int frames)
{
    if (r->pending_frames + frames > r->pending_capacity) {
        r->pending_capacity = (r->pending_frames + frames) * 2;
        float *bigger = realloc(r->pending, (size_t)r->pending_capacity * 2 * sizeof(float));
        if (!bigger) return false;
        r->pending = bigger;
    }
    memcpy(r->pending + 2 * r->pending_frames, samples, (size_t)frames * 2 * sizeof(float));
    r->pending_frames += frames;
    int block = r->block->nb_samples;
    while (r->pending_frames >= block) {
        encode_block(r, block);
        r->pending_frames -= block;
        memmove(r->pending, r->pending + 2 * block, (size_t)r->pending_frames * 2 * sizeof(float));
    }
    return !r->failed;
}

bool recorder_close(Recorder *r)
{
    if (!r) return false;
    bool ok = false;
    if (r->file && r->file->pb) {
        if (r->pending_frames > 0) encode_block(r, r->pending_frames);  // the last, padded with silence
        encode(r, r->video, r->video_stream, NULL);  // flush
        encode(r, r->sound, r->sound_stream, NULL);
        ok = av_write_trailer(r->file) == 0 && !r->failed;
        avio_closep(&r->file->pb);
    }
    avcodec_free_context(&r->video);
    avcodec_free_context(&r->sound);
    av_frame_free(&r->picture);
    av_frame_free(&r->block);
    av_packet_free(&r->packet);
    avformat_free_context(r->file);
    free(r->pending);
    free(r);
    return ok;
}
