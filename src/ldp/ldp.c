#include "ldp/ldp.h"

// Real players took anywhere from a fraction of a second to a couple of
// seconds to search, depending on how far the laser had to travel. A short
// fixed delay is enough for now; it gives the video decoder time to catch
// up, and the game's CPU will be able to see the "searching" state.
#define DEFAULT_SEARCH_SECONDS 0.1

void ldp_init(Ldp *ldp, double fps, int first_frame)
{
    *ldp = (Ldp){
        .state = LDP_PAUSED,
        .frame = first_frame,
        .fps = fps,
        .search_seconds = DEFAULT_SEARCH_SECONDS,
    };
}

void ldp_play(Ldp *ldp)
{
    if (ldp->state == LDP_SEARCHING) return;  // busy; a real player ignores it
    if (ldp->state != LDP_PLAYING) ldp->frame_clock = 0;
    ldp->state = LDP_PLAYING;
}

void ldp_pause(Ldp *ldp)
{
    if (ldp->state == LDP_PLAYING) ldp->state = LDP_PAUSED;
}

void ldp_stop(Ldp *ldp)
{
    ldp->state = LDP_STOPPED;
}

void ldp_search(Ldp *ldp, int frame)
{
    if (frame < 0) frame = 0;
    ldp->state = LDP_SEARCHING;
    ldp->search_target = frame;
    ldp->search_clock = ldp->search_seconds;
}

void ldp_step(Ldp *ldp, int delta)
{
    if (ldp->state == LDP_STOPPED || ldp->state == LDP_SEARCHING) return;
    ldp->state = LDP_PAUSED;
    ldp->frame += delta;
    if (ldp->frame < 0) ldp->frame = 0;
}

void ldp_update(Ldp *ldp, double seconds)
{
    switch (ldp->state) {
    case LDP_PLAYING: {
        // Advance one frame per 1/fps seconds. Keeping the leftover time
        // (instead of resetting the clock each frame) keeps playback at the
        // right speed even though updates don't line up with frames.
        double frame_time = 1.0 / ldp->fps;
        ldp->frame_clock += seconds;
        while (ldp->frame_clock >= frame_time) {
            ldp->frame_clock -= frame_time;
            ldp->frame++;
        }
        break;
    }
    case LDP_SEARCHING:
        ldp->search_clock -= seconds;
        if (ldp->search_clock <= 0) {
            ldp->frame = ldp->search_target;
            ldp->state = LDP_PAUSED;  // a search ends on a still frame
        }
        break;
    case LDP_PAUSED:
    case LDP_STOPPED:
        break;
    }
}

bool ldp_has_picture(const Ldp *ldp)
{
    return ldp->state != LDP_STOPPED;
}

const char *ldp_state_name(LdpState state)
{
    switch (state) {
    case LDP_STOPPED:   return "stopped";
    case LDP_PAUSED:    return "paused";
    case LDP_PLAYING:   return "playing";
    case LDP_SEARCHING: return "searching";
    }
    return "?";
}
