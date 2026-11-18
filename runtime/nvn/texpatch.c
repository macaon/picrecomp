// Edits of the game's UI textures on upload (texpatch.h).
//
// The face-button prompts. PICROSS draws A / B / X / Y as a diamond of four circles with the pressed button filled in,
// in the Switch's positions: A right, B bottom, X top, Y left. Four 64x64 DXT5 textures, the same in every game. With
// a button bound elsewhere (the Xbox layout puts A at the bottom) the picture would point at the wrong button, so the
// texture is turned a quarter at a time until its filled circle is where the controller button bound to that input is.
// Unity keeps a texture's rows bottom first, so in the data the picture is upside down: the circles are found from the
// bottom, and a turn that is clockwise on the screen is a counter-clockwise one of the data.
// A DXT5 block can be turned without decoding it (the pixels of a block are permuted, the endpoints stay), so nothing
// is lost and no picture of the game's is stored here: which input a texture shows is read from its own alpha.
#include "texpatch.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "../input_bind.h"

#define ICON 64
#define NVN_BC3_UNORM 0x44
#define NVN_BC3_SRGB 0x48

int texpatch_identify(const NvnTexture *t) {
    return t->target == 1 && t->width == ICON && t->height == ICON && (t->format == NVN_BC3_UNORM || t->format == NVN_BC3_SRGB) ? 0 : -1;
}

uint32_t texpatch_generation(void) {
    uint32_t g = 1;
    for (int i = BIND_A; i <= BIND_Y; i++) g = g * 31 + (uint16_t)g_bind_pad[i];
    return g;
}

// Positions on the diamond, clockwise from the top: 0 top, 1 right, 2 bottom, 3 left
static int position_of(int host) {
    switch (host) {
    case SDL_GAMEPAD_BUTTON_NORTH: return 0;
    case SDL_GAMEPAD_BUTTON_EAST: return 1;
    case SDL_GAMEPAD_BUTTON_SOUTH: return 2;
    case SDL_GAMEPAD_BUTTON_WEST: return 3;
    default: return -1;
    }
}
static const int k_slot_at[4] = {BIND_X, BIND_A, BIND_B, BIND_Y};  // the Switch input each position shows

// The alpha of one texel of a DXT5 block
static int bc3_alpha(const uint8_t *b, int px, int py) {
    int a0 = b[0], a1 = b[1];
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    int idx = (int)((bits >> (3 * (py * 4 + px))) & 7);
    if (idx == 0) return a0;
    if (idx == 1) return a1;
    if (a0 > a1) return ((8 - idx) * a0 + (idx - 1) * a1) / 7;
    return idx == 6 ? 0 : idx == 7 ? 255 : ((6 - idx) * a0 + (idx - 1) * a1) / 5;
}
static int texel_alpha(const uint8_t *blocks, uint32_t row, int x, int y) { return bc3_alpha(blocks + (y / 4) * row + (x / 4) * 16, x % 4, y % 4); }

// Which circle is filled (0..3 as above, as seen on the screen), -1 if this isn't one of the four prompts: the circles'
// centres are filled only in the pressed one, and the rest of the picture is empty at the corners
static int filled_circle(const uint8_t *blocks, uint32_t row) {
    static const int cx[4] = {31, 49, 31, 16}, cy[4] = {14, 31, 48, 31};  // (from the top of the picture)
    int found = -1;
    for (int i = 0; i < 4; i++) {
        int a = texel_alpha(blocks, row, cx[i], ICON - 1 - cy[i]);
        if (a > 200) { if (found >= 0) return -1; found = i; }
        else if (a > 40) return -1;
    }
    for (int i = 0; i < 4; i++)
        if (texel_alpha(blocks, row, (i & 1) ? 61 : 2, (i & 2) ? 61 : 2) > 40) return -1;
    return found;
}

// A quarter turn clockwise, in place, of a square of w x w texels in DXT5 blocks (16 bytes: 8 of alpha, 8 of colour)
static void turn_clockwise(uint8_t *blocks, uint32_t w, uint32_t row) {
    uint32_t n = w / 4;
    uint8_t copy[ICON / 4 * ICON / 4 * 16];
    for (uint32_t y = 0; y < n; y++) memcpy(copy + y * n * 16, blocks + y * row, n * 16);
    for (uint32_t by = 0; by < n; by++)
        for (uint32_t bx = 0; bx < n; bx++) {
            // the destination block (bx, by) comes from the source block (by, n - 1 - bx), its texels turned the same way:
            // destination (px, py) is source (py, 3 - px)
            const uint8_t *s = copy + ((n - 1 - bx) * n + by) * 16;
            uint8_t *d = blocks + by * row + bx * 16;
            memcpy(d, s, 16);  // the endpoints
            uint64_t abits = 0, nbits = 0;
            uint32_t cbits = 0, ncbits = 0;
            for (int i = 0; i < 6; i++) abits |= (uint64_t)s[2 + i] << (8 * i);
            for (int i = 0; i < 4; i++) cbits |= (uint32_t)s[12 + i] << (8 * i);
            for (int py = 0; py < 4; py++)
                for (int px = 0; px < 4; px++) {
                    int to = py * 4 + px, sx = py, sy = 3 - px, si = sy * 4 + sx;  // destination (px, py) is source (py, 3 - px)
                    nbits |= ((abits >> (3 * si)) & 7) << (3 * to);
                    ncbits |= ((cbits >> (2 * si)) & 3) << (2 * to);
                }
            for (int i = 0; i < 6; i++) d[2 + i] = (uint8_t)(nbits >> (8 * i));
            for (int i = 0; i < 4; i++) d[12 + i] = (uint8_t)(ncbits >> (8 * i));
        }
}

void texpatch_apply(int id, uint32_t lv, uint8_t *blocks, uint32_t w, uint32_t h, uint32_t row) {
    (void)id;
    static int turns;  // decided on level 0, used by the smaller levels after it
    if (lv == 0) {
        turns = 0;
        int shown = w == ICON && h == ICON ? filled_circle(blocks, row) : -1;
        int want = shown < 0 ? -1 : position_of(g_bind_pad[k_slot_at[shown]]);
        if (want >= 0) turns = (4 - (want - shown + 4) % 4) % 4;  // (clockwise on the screen: the other way in the data)
    }
    if (!turns || w != h || w % 4 || w > ICON) return;  // (a level under 4 texels shares one block: left as it is)
    for (int i = 0; i < turns; i++) turn_clockwise(blocks, w, row);
}
