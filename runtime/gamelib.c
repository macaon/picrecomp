// Loads the recompiled game code library (runtime/gamelib.h).
#include "gamelib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "paths.h"
#include "rt.h"

static const GameLibTables *g_tables;
static char g_error[256];
static char g_loaded_dir[1100];  // the game folder the loaded library was chosen for

// The library's id file (<library>.id, written next to it by the build): line 1 its GAMELIB_ID, line 2 the build id of
// the game's main module it was generated from. Read without loading the library. The second line must match the
// game being started: the generated code is only valid for the exact executable it came from.
bool gamelib_game_build_id(const char *game_dir, char out[33]) {
    char path[1100];
    snprintf(path, sizeof path, "%s/exefs/main", game_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned char b[16];
    bool ok = fseek(f, 0x40, SEEK_SET) == 0 && fread(b, 1, 16, f) == 16;
    fclose(f);
    for (int k = 0; ok && k < 16; k++) snprintf(out + 2 * k, 3, "%02x", b[k]);
    return ok;
}

static bool id_matches(const char *lib, const char *game_dir) {
    char path[1100], id[128] = {0};
    snprintf(path, sizeof path, "%s.id", lib);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(id, 1, sizeof id - 1, f);
    fclose(f);
    char *nl = strchr(id, '\n'), *built = NULL;
    if (nl) *nl = 0, built = nl + 1;
    if (strcmp(id, GAMELIB_ID)) return false;
    if (!game_dir) return true;
    char have[33], want[33] = {0};
    if (!built) return false;
    for (n = 0; built[n] && built[n] != '\n' && built[n] != '\r' && n < 32; n++) want[n] = built[n];
    return gamelib_game_build_id(game_dir, have) && !strcmp(have, want);
}

static bool exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// The places to look, in order (gamelib.h). Returns how many were filled.
// Android: the app's internal storage (the data folder is on external storage, mounted without exec), then one packed
// into the APK itself, opened by its bare name (it lives inside the APK: no file to look at, its own id decides).
static int candidates(const char *game_dir, char out[3][1100]) {
    int n = 0;
    const char *env = getenv("PICRECOMP_GAME_LIB");
    if (env && *env) snprintf(out[n++], 1100, "%s", env);
    if (game_dir) snprintf(out[n++], 1100, "%s/%s", game_dir, GAMELIB_NAME);  // (this game's own library first)
    char exe_side[1024];
    if (rt_resource(GAMELIB_NAME, NULL, exe_side, sizeof exe_side) && exists(exe_side))
        snprintf(out[n++], 1100, "%s", exe_side);
    return n;
}

static bool in_apk(const char *c) { return !strchr(c, '/'); }  // (Android: a bare name)

bool gamelib_present(const char *game_dir) {
    if (g_tables) return true;
    char c[3][1100];
    int n = candidates(game_dir, c);
    for (int i = 0; i < n; i++) {
        if (in_apk(c[i])) {  // (loading it is the only way to look: the library stays loaded for the game)
            char why[8];
            if (gamelib_load(NULL, why, sizeof why)) return true;
            continue;
        }
        if (exists(c[i]) && id_matches(c[i], game_dir)) return true;
    }
    return false;
}

static const GameLibTables *open_lib(const char *path) {
    static void *prev;  // the library of the game before (switching games in one process), closed once replaced
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { snprintf(g_error, sizeof g_error, "can't load %s (error %lu)", path, GetLastError()); return NULL; }
    GameLibEntry entry = (GameLibEntry)(void *)GetProcAddress(h, GAMELIB_ENTRY);
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { snprintf(g_error, sizeof g_error, "%s", dlerror()); return NULL; }
    GameLibEntry entry = (GameLibEntry)dlsym(h, GAMELIB_ENTRY);
#endif
    if (!entry) { snprintf(g_error, sizeof g_error, "%s: not game code", path); return NULL; }
    const GameLibTables *t = entry();
    if (!t || t->abi != GAMELIB_ABI || !t->id || strcmp(t->id, GAMELIB_ID)) {
        snprintf(g_error, sizeof g_error, "%s was built for another version", path);
        return NULL;  // (left loaded: nothing of it runs)
    }
    if (prev) {
#ifdef _WIN32
        FreeLibrary((HMODULE)prev);
#else
        dlclose(prev);
#endif
    }
    prev = (void *)h;
    return t;
}

bool gamelib_load(const char *game_dir, char *why, size_t why_size) {
    // One library per game folder: a different folder (the start screen's other game card) loads its own.
    static bool tried;
    bool other = game_dir && g_loaded_dir[0] && strcmp(game_dir, g_loaded_dir);
    if (!tried || other) {
        tried = true;
        g_tables = NULL;
        snprintf(g_loaded_dir, sizeof g_loaded_dir, "%s", game_dir ? game_dir : "");
        snprintf(g_error, sizeof g_error, "the game code hasn't been built: set up the game files on the start screen");
        char c[3][1100];
        int n = candidates(game_dir, c);
        const char *env = getenv("PICRECOMP_GAME_LIB");
        for (int i = 0; i < n && !g_tables; i++) {
            if (!in_apk(c[i]) && !exists(c[i])) continue;
            // PICRECOMP_GAME_LIB / the APK's own: loaded as given, the library's own id decides
            bool given = (i == 0 && env && *env) || in_apk(c[i]);
            if (!given && !id_matches(c[i], game_dir)) {
                snprintf(g_error, sizeof g_error, "%s was built for another version", c[i]);
                continue;
            }
            g_tables = open_lib(c[i]);
            if (g_tables) {
                unsigned long long n = 0;
                for (uint32_t k = 0; k < g_tables->nmodules; k++) n += *g_tables->modules[k].count;
                LOG("[gamelib] %s (%u modules, %llu functions)\n", c[i], g_tables->nmodules, n);
            }
        }
    }
    if (!g_tables && why) snprintf(why, why_size, "%s", g_error);
    return g_tables != NULL;
}

const GameLibTables *gamelib_tables(void) { return g_tables; }
