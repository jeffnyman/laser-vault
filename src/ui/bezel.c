#include "ui/bezel.h"

#include <string.h>

// Which segments light up for each value the board can send. Segments are
// the bits gfedcba:
//
//      aaa
//     f   b
//      ggg
//     e   c
//      ddd
//
// 0-9 are digits, 10-14 the letters A b C d E, and 15 is blank (as in MAME).
static const uint8_t segment_map[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7C, 0x07,
    0x7F, 0x67, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x00,
};

#define DIGIT_W 20.0f
#define DIGIT_H 34.0f
#define DIGIT_GAP 6.0f
#define SEGMENT 4.0f  // segment thickness

// One seven-segment digit. Unlit segments are drawn faintly, as on a real
// LED display, where you can see them when they're off.
static void draw_digit(SDL_Renderer *r, float x, float y, uint8_t value)
{
    const float w = DIGIT_W, h = DIGIT_H, t = SEGMENT, half = h / 2;
    const float side = half - t - t / 2;  // length of a vertical segment
    const SDL_FRect segments[7] = {
        {x + t, y, w - 2 * t, t},                  // a: top
        {x + w - t, y + t, t, side},               // b: top right
        {x + w - t, y + half + t / 2, t, side},    // c: bottom right
        {x + t, y + h - t, w - 2 * t, t},          // d: bottom
        {x, y + half + t / 2, t, side},            // e: bottom left
        {x, y + t, t, side},                       // f: top left
        {x + t, y + half - t / 2, w - 2 * t, t},   // g: middle
    };
    uint8_t lit = segment_map[value & 15];
    for (int s = 0; s < 7; s++) {
        if (lit & (1 << s)) SDL_SetRenderDrawColor(r, 255, 70, 30, 255);
        else SDL_SetRenderDrawColor(r, 50, 14, 8, 255);
        SDL_RenderFillRect(r, &segments[s]);
    }
}

// A row of consecutive digits from the board.
static void draw_digits(SDL_Renderer *r, float x, float y, const uint8_t *digits, int count)
{
    for (int i = 0; i < count; i++) draw_digit(r, x + i * (DIGIT_W + DIGIT_GAP), y, digits[i]);
}

static void draw_label(SDL_Renderer *r, float x, float y, const char *text)
{
    SDL_SetRenderDrawColor(r, 230, 205, 15, 255);
    SDL_RenderDebugText(r, x, y, text);  // SDL's built-in 8x8 font
}

// A row of three lamps with their names under them, centered on the panel.
static void draw_lamps(SDL_Renderer *r, float x, float y, int lit)
{
    static const char *names[3] = {"CADET", "CAPTAIN", "ACE"};
    for (int i = 0; i < 3; i++) {
        float center = x + BEZEL_WIDTH / 2 + (i - 1) * 54;
        if (lit & (1 << i)) SDL_SetRenderDrawColor(r, 255, 235, 150, 255);
        else SDL_SetRenderDrawColor(r, 55, 50, 32, 255);
        SDL_RenderFillRect(r, &(SDL_FRect){center - 14, y, 28, 16});
        draw_label(r, center - 4 * (float)strlen(names[i]), y + 24, names[i]);
    }
}

void bezel_draw(SDL_Renderer *r, float x, float y, const uint8_t digits[16], int lamps)
{
    SDL_SetRenderDrawColor(r, 16, 16, 16, 255);
    SDL_RenderFillRect(r, &(SDL_FRect){x, y, BEZEL_WIDTH, BEZEL_HEIGHT});

    float left = x + 22;
    const float score_width = 6 * DIGIT_W + 5 * DIGIT_GAP;

    draw_label(r, left, y + 40, "PLAYER 1 SCORE");
    draw_digits(r, left, y + 56, &digits[0], 6);
    draw_digit(r, left + score_width - DIGIT_W, y + 104, digits[6]);
    draw_label(r, left, y + 117, "LIVES");

    draw_label(r, left, y + 190, "PLAYER 2 SCORE");
    draw_digits(r, left, y + 206, &digits[8], 6);
    draw_digit(r, left + score_width - DIGIT_W, y + 254, digits[7]);
    draw_label(r, left, y + 267, "LIVES");

    draw_label(r, left, y + 357, "CREDITS");
    draw_digits(r, left + score_width - 2 * DIGIT_W - DIGIT_GAP, y + 344, &digits[14], 2);

    if (lamps >= 0) draw_lamps(r, x, y + 410, lamps);
}
