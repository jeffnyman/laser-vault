// bezel.h - draw the game board's score displays.
//
// On the arcade cabinet the scores aren't on the screen at all: they're on
// seven-segment LED displays in the bezel around it. The board drives 16
// digits (see games/cinematronics/board.h):
//
//     digits 0-5    player 1 score      digit 6    player 1 lives
//     digits 8-13   player 2 score      digit 7    player 2 lives
//     digits 14-15  credits
//
// (Layout as in MAME's dlair.lay.) This draws them as a panel of LED digits
// with labels, for showing beside the picture.

#ifndef LV_BEZEL_H
#define LV_BEZEL_H

#include <SDL3/SDL.h>

#include <stdint.h>

// Size of the panel in the renderer's coordinates.
#define BEZEL_WIDTH 200
#define BEZEL_HEIGHT 480

// Draw the panel with its top-left corner at (x, y). digits[i] is the value
// the board wrote for digit i: 0-9, 10-14 for A b C d E, 15 for blank.
void bezel_draw(SDL_Renderer *renderer, float x, float y, const uint8_t digits[16]);

#endif
