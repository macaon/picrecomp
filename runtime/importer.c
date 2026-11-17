#include "importer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "paths.h"
#include "platform.h"

static void poll_log(ImportState *s);
static PlatProc g_proc;
static int g_exit = -1;  // of the last one, once it ended
static char g_log[1100];
static char g_error[256];

bool importer_start(const char *folder, const char *keys) {
    g_error[0] = 0;
    if (g_proc) return false;
    g_exit = -1;
    static bool hooked;
    if (!hooked) hooked = true, atexit(importer_cancel);  // (quitting mid-import must not leave it running)
    char script[1100];
    if (!rt_resource(NULL, "tools/picross/install.py", script, sizeof script)) {
        snprintf(g_error, sizeof g_error, "tools/picross/install.py was not found next to PicRecomp");
        return false;
    }
    char games[1100];
    snprintf(games, sizeof games, "%s/games", rt_data_root());
    snprintf(g_log, sizeof g_log, "%s/import.log", rt_data_root());
    const char *const argv[] = {
#ifdef _WIN32
        "python",
#else
        "/usr/bin/env", "python3",
#endif
        script, "--folder", folder, "--games", games, keys ? "--prod-keys" : NULL, keys, NULL};
    g_proc = plat_spawn(argv, g_log, false);
    if (!g_proc) snprintf(g_error, sizeof g_error, "could not start python3");
    return g_proc != 0;
}

void importer_poll(ImportState *s) {
    static ImportState last;  // the log is read ten times a second, not every frame
    static uint64_t at;
    uint64_t now = plat_now_ns();
    if (now - at < 100000000ull && at) { *s = last; return; }
    at = now;
    poll_log(s);
    last = *s;
}

static void poll_log(ImportState *s) {
    memset(s, 0, sizeof *s);
    s->steps = 1;
    if (!g_log[0]) {
        if (g_error[0]) s->failed = true, snprintf(s->error, sizeof s->error, "%s", g_error);
        return;
    }
    FILE *f = fopen(g_log, "rb");
    char line[512], last[512] = "";
    while (f && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        int n, total, at = 0;
        if (sscanf(line, "@step %d/%d %n", &n, &total, &at) >= 2 && at && n >= 1 && n <= IMPORT_MAX_STEPS) {
            s->step = n, s->steps = total;
            snprintf(s->texts[n - 1], sizeof s->texts[0], "%s", line + at);
            s->frac = 0;
        } else if (!strncmp(line, "@progress ", 10)) {
            long done = 0, total = 0;
            if (sscanf(line + 10, "%ld/%ld", &done, &total) == 2 && total > 0) s->frac = (float)done / (float)total;
        } else if (!strncmp(line, "@error ", 7)) {
            snprintf(s->error, sizeof s->error, "%s", line + 7);
        }
        if (line[0]) snprintf(last, sizeof last, "%s", line);
    }
    if (f) fclose(f);
    if (g_proc) {
        int r = plat_proc_wait(g_proc, false);
        if (r < 0) { s->running = true; return; }
        g_proc = 0, g_exit = r;
    }
    s->done = g_exit == 0;
    s->failed = g_exit != 0;
    if (s->failed && !s->error[0]) snprintf(s->error, sizeof s->error, "%s", last[0] ? last : "the import stopped");
}

void importer_cancel(void) {
    if (g_proc) plat_proc_kill(g_proc);
    g_proc = 0;
    g_log[0] = 0;
}
