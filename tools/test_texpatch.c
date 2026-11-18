// Test of runtime/nvn/texpatch.c: the face-button prompts turned to the bound controller button. Builds a DXT5 picture of
// each of the four prompts (a diamond of four circles, one filled), turns it for a binding, and checks the result
// against the same picture turned pixel by pixel. tools/test_texpatch.sh builds and runs it.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/input_bind.h"
#include "../runtime/nvn/texpatch.h"

int16_t g_bind_pad[BIND_PAD_COUNT];
int16_t g_bind_key[BIND_KEY_COUNT];

enum { N = 64 };
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// The four prompts' circles (top, right, bottom, left); a filled one has radius 9, a ring 9 with a hole of 5
static const int cx[4] = {31, 49, 31, 16}, cy[4] = {14, 31, 48, 31};
static void picture(uint8_t alpha[N][N], int filled, int size) {  // size: 64, 32, 16...
    int step = N / size;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            int a = 0;
            for (int i = 0; i < 4; i++) {
                int dx = x * step - cx[i], dy = y * step - cy[i], d2 = dx * dx + dy * dy;
                if (d2 <= 81 && (i == filled || d2 >= 25)) a = 255;
            }
            alpha[y][x] = (uint8_t)a;
        }
}
// DXT5: alpha endpoints 255 and 0 (indices 0 / 1), white colour. The data is the picture upside down (Unity keeps a
// texture's rows bottom first), as the game's own are.
static void encode(const uint8_t alpha[N][N], int size, uint8_t *blocks) {
    int nb = size / 4;
    for (int by = 0; by < nb; by++)
        for (int bx = 0; bx < nb; bx++) {
            uint8_t *b = blocks + (by * nb + bx) * 16;
            memset(b, 0, 16);
            b[0] = 255, b[1] = 0;
            uint64_t bits = 0;
            for (int i = 0; i < 16; i++) bits |= (uint64_t)(alpha[size - 1 - (by * 4 + i / 4)][bx * 4 + i % 4] ? 0 : 1) << (3 * i);
            for (int i = 0; i < 6; i++) b[2 + i] = (uint8_t)(bits >> (8 * i));
            b[8] = b[9] = b[10] = b[11] = 0xFF;  // both colour endpoints white
        }
}
static int decode_alpha(const uint8_t *blocks, int size, int x, int y) {  // (x, y) on the screen: from the top of the picture
    y = size - 1 - y;
    const uint8_t *b = blocks + ((y / 4) * (size / 4) + x / 4) * 16;
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    int idx = (int)((bits >> (3 * ((y % 4) * 4 + x % 4))) & 7);
    return idx == 0 ? b[0] : idx == 1 ? b[1] : -1;  // (this picture only uses the two endpoints)
}
static void bind_all(int a, int b, int x, int y) {
    g_bind_pad[BIND_A] = (int16_t)a, g_bind_pad[BIND_B] = (int16_t)b, g_bind_pad[BIND_X] = (int16_t)x, g_bind_pad[BIND_Y] = (int16_t)y;
}
static int filled_at(const uint8_t *blocks, int size) {
    int found = -1, step = N / size;
    for (int i = 0; i < 4; i++)
        if (decode_alpha(blocks, size, cx[i] / step, cy[i] / step) == 255) found = found < 0 ? i : 9;
    return found;
}

int main(void) {
    static uint8_t alpha[N][N], turned[N][N], blocks[N * N], work[N * N];
    NvnTexture t = {.target = 1, .format = 0x44, .width = N, .height = N};
    CHECK(texpatch_identify(&t) >= 0, "a 64x64 DXT5 texture is a candidate");
    t.format = 0x45;
    CHECK(texpatch_identify(&t) < 0, "another format isn't");
    t.format = 0x44, t.width = 32;
    CHECK(texpatch_identify(&t) < 0, "another size isn't");

    // Switch layout: nothing changes. Xbox layout: A bottom, B right, X left, Y top.
    // Icon i shows: 0 top = X, 1 right = A, 2 bottom = B, 3 left = Y.
    struct { int a, b, x, y; const char *name; int want[4]; } layouts[] = {
        {SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST, "Switch", {0, 1, 2, 3}},
        {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, "Xbox", {3, 2, 1, 0}},
        {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST, "A moved", {0, 2, 2, 3}},
    };
    for (unsigned l = 0; l < sizeof layouts / sizeof *layouts; l++) {
        bind_all(layouts[l].a, layouts[l].b, layouts[l].x, layouts[l].y);
        for (int i = 0; i < 4; i++) {
            picture(alpha, i, N);
            encode(alpha, N, blocks);
            memcpy(work, blocks, sizeof blocks);
            texpatch_apply(0, 0, work, N, N, N / 4 * 16);
            int at = filled_at(work, N);
            CHECK(at == layouts[l].want[i], "%s layout: icon %d filled at %d, expected %d", layouts[l].name, i, at, layouts[l].want[i]);
            // against the same picture turned by pixels (clockwise: destination (x, y) is source (y, N - 1 - x))
            int turns = (layouts[l].want[i] - i + 4) % 4;
            memcpy(turned, alpha, sizeof alpha);
            for (int k = 0; k < turns; k++) {
                static uint8_t next[N][N];
                for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) next[y][x] = turned[N - 1 - x][y];
                memcpy(turned, next, sizeof next);
            }
            int bad = 0;
            for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) bad += decode_alpha(work, N, x, y) != turned[y][x];
            CHECK(!bad, "%s layout: icon %d differs from the turned picture in %d texels", layouts[l].name, i, bad);
        }
    }
    // the smaller levels turn with level 0 (and nothing when level 0 is left alone)
    bind_all(SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH);
    picture(alpha, 1, N), encode(alpha, N, blocks), memcpy(work, blocks, sizeof blocks);
    texpatch_apply(0, 0, work, N, N, N / 4 * 16);
    picture(alpha, 1, 32), encode(alpha, 32, blocks), memcpy(work, blocks, 32 * 32);
    texpatch_apply(0, 1, work, 32, 32, 32 / 4 * 16);
    CHECK(filled_at(work, 32) == 2, "level 1 turned with level 0 (filled at %d)", filled_at(work, 32));
    // a texture that is not one of the four prompts is left as it is
    memset(alpha, 255, sizeof alpha), encode(alpha, N, blocks), memcpy(work, blocks, sizeof blocks);
    texpatch_apply(0, 0, work, N, N, N / 4 * 16);
    CHECK(!memcmp(work, blocks, sizeof blocks), "an opaque square is left alone");
    memset(alpha, 0, sizeof alpha), encode(alpha, N, blocks), memcpy(work, blocks, sizeof blocks);
    texpatch_apply(0, 0, work, N, N, N / 4 * 16);
    CHECK(!memcmp(work, blocks, sizeof blocks), "an empty picture is left alone");
    // a face button bound to a shoulder: its prompt is left as it is
    bind_all(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST);
    picture(alpha, 1, N), encode(alpha, N, blocks), memcpy(work, blocks, sizeof blocks);
    texpatch_apply(0, 0, work, N, N, N / 4 * 16);
    CHECK(!memcmp(work, blocks, sizeof blocks), "A on a shoulder button: its prompt is left alone");
    // the generation changes with the bindings
    uint32_t g1 = texpatch_generation();
    bind_all(SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST);
    CHECK(texpatch_generation() != g1, "the generation follows the bindings");
    printf(fails ? "%d FAILED\n" : "texpatch: all checks passed\n", fails);
    return fails != 0;
}
