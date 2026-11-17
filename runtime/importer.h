// Adds a game to the library: runs tools/picross/prepare.py on the folder holding the user's packages and reports the
// step it is on, read from the script's "@step <n>/<total> <text>" lines (runtime/importer.c).
#pragma once
#include <stdbool.h>

#define IMPORT_MAX_STEPS 12

typedef struct {
    bool running, done, failed;
    int step, steps;                       // the current step (1-based) of how many
    char texts[IMPORT_MAX_STEPS][96];      // the steps seen so far, in order; the last is the current one
    char error[256];                       // failed: why
} ImportState;

bool importer_start(const char *folder, const char *keys);  // keys: a prod.keys file, or NULL to look beside the packages. false: one is running already, or the script wasn't found (error in out)
void importer_poll(ImportState *out);      // call every frame; cheap
void importer_cancel(void);                // stops it and removes its temporary files with it
