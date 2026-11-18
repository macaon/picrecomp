// Edits of the game's UI textures, made as they are uploaded (runtime/nvn/texpatch.c). Shared by the renderer backends.
// PICROSS: the face-button prompts follow the controller bindings (see texpatch.c).
#pragma once
#include <stdint.h>

#include "nvn.h"

// -1: leave the texture alone; else an id that is passed back to texpatch_apply (the texture is "a prompt": uploaded
// again whenever texpatch_generation() has changed)
int texpatch_identify(const NvnTexture *t);
// Changes when the look of the prompts changes (a face button was bound elsewhere)
uint32_t texpatch_generation(void);
// The texture's block data as deswizzled (one level), edited in place: blocks, the level's size in texels, bytes per row
// of blocks. Called for level 0 first.
void texpatch_apply(int id, uint32_t lv, uint8_t *blocks, uint32_t w, uint32_t h, uint32_t row);
