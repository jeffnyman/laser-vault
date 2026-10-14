#include "video/disc_video.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct DiscVideo {
    const Framefile *ff;
    double disc_fps;  // rate of disc frame numbers

    // The clip currently open, and FFmpeg's state for reading it.
    const FramefileClip *clip;
    AVFormatContext *format;  // reads packets out of the .m2v file
    AVCodecContext *decoder;  // turns packets into pictures
    int stream_index;
    double mpeg_fps;          // rate of pictures in the clip
    AVPacket *packet;
    AVFrame *picture;   // the most recently decoded picture
    AVFrame *scratch;   // receives pictures from the decoder

    int picture_index;  // index within the clip of `picture`, or -1 if none
    bool draining;      // reached end of file; decoder is flushing
};

static void close_clip(DiscVideo *dv)
{
    avcodec_free_context(&dv->decoder);
    avformat_close_input(&dv->format);
    dv->clip = NULL;
    dv->picture_index = -1;
    dv->draining = false;
}

static bool open_clip(DiscVideo *dv, const FramefileClip *clip)
{
    close_clip(dv);

    if (avformat_open_input(&dv->format, clip->path, NULL, NULL) < 0) {
        fprintf(stderr, "disc_video: cannot open %s\n", clip->path);
        return false;
    }
    if (avformat_find_stream_info(dv->format, NULL) < 0) goto fail;

    const AVCodec *codec = NULL;
    dv->stream_index = av_find_best_stream(dv->format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (dv->stream_index < 0) goto fail;

    AVStream *stream = dv->format->streams[dv->stream_index];
    AVRational rate = stream->avg_frame_rate.num ? stream->avg_frame_rate : stream->r_frame_rate;
    dv->mpeg_fps = rate.num ? av_q2d(rate) : dv->disc_fps;

    dv->decoder = avcodec_alloc_context3(codec);
    if (!dv->decoder) goto fail;
    avcodec_parameters_to_context(dv->decoder, stream->codecpar);
    dv->decoder->thread_count = 0;  // let FFmpeg pick a sensible thread count
    if (avcodec_open2(dv->decoder, codec, NULL) < 0) goto fail;

    dv->clip = clip;
    return true;

fail:
    fprintf(stderr, "disc_video: cannot decode %s\n", clip->path);
    close_clip(dv);
    return false;
}

// Decode the next picture of the current clip into dv->picture.
// Returns false at the end of the clip.
static bool decode_next(DiscVideo *dv)
{
    for (;;) {
        // A decoder may need several packets before it outputs a picture,
        // and may hold pictures back (B-frames are displayed before the
        // frames they depend on), so: ask for a picture first, and feed it
        // more data only when it says it needs some.
        int ret = avcodec_receive_frame(dv->decoder, dv->scratch);
        if (ret == 0) {
            av_frame_unref(dv->picture);
            av_frame_move_ref(dv->picture, dv->scratch);
            dv->picture_index++;
            return true;
        }
        if (ret != AVERROR(EAGAIN) || dv->draining) return false;

        if (av_read_frame(dv->format, dv->packet) < 0) {
            // End of file: a NULL packet tells the decoder to flush out any
            // pictures it is still holding.
            dv->draining = true;
            avcodec_send_packet(dv->decoder, NULL);
            continue;
        }
        if (dv->packet->stream_index == dv->stream_index)
            avcodec_send_packet(dv->decoder, dv->packet);
        av_packet_unref(dv->packet);
    }
}

DiscVideo *disc_video_open(const Framefile *ff, double disc_fps)
{
    DiscVideo *dv = calloc(1, sizeof *dv);
    if (!dv) return NULL;
    dv->ff = ff;
    dv->disc_fps = disc_fps;
    dv->packet = av_packet_alloc();
    dv->picture = av_frame_alloc();
    dv->scratch = av_frame_alloc();
    dv->picture_index = -1;
    av_log_set_level(AV_LOG_ERROR);
    return dv;
}

void disc_video_close(DiscVideo *dv)
{
    if (!dv) return;
    close_clip(dv);
    av_packet_free(&dv->packet);
    av_frame_free(&dv->picture);
    av_frame_free(&dv->scratch);
    free(dv);
}

const AVFrame *disc_video_get_frame(DiscVideo *dv, int frame)
{
    int offset;
    const FramefileClip *clip = framefile_find(dv->ff, frame, &offset);
    if (!clip) return NULL;

    if (clip != dv->clip && !open_clip(dv, clip)) return NULL;

    // Convert the disc frame offset into a picture index within the clip.
    int target = (int)(offset * dv->mpeg_fps / dv->disc_fps + 0.5);

    // A picture behind us: start decoding again from the top of the clip.
    if (target < dv->picture_index && !open_clip(dv, clip)) return NULL;

    while (dv->picture_index < target) {
        if (!decode_next(dv)) break;  // past the end: keep the last picture
    }
    return dv->picture_index >= 0 ? dv->picture : NULL;
}
