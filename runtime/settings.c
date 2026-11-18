#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "input_bind.h"

#define SETTINGS_FILE settings_file()

// PICRECOMP_SETTINGS=<path>: another settings file (test runs never touch the player's picrecomp.ini)
static const char *settings_file(void) {
    const char *e = getenv("PICRECOMP_SETTINGS");
    return e && *e ? e : "picrecomp.ini";
}

Settings g_settings = {
    .aspect = ASPECT_UNLOCKED,
    .fullscreen = false,
    .volume = 1.0f,
    .show_fps = false,
    .upscaler = UPSCALE_SHARP,  // (MetalFX on macOS, AMD FSR 1 on Windows)
};

// The game's own text languages: its RomFS carries these per region (regionUS/USen|USes|USfr|USpt,
// regionEU/EUen|EUde|EUes|EUfr|EUit|EUnl|EUru, regionJP/JPja, regionKR/KRko, regionTW/TWzh, regionCN/CNzh), and the
// game picks one from the system language it reads back (runtime/services/am.c), as on the console. Codes as the
// Switch reports them (nn::settings::LanguageCode).
const char *const g_language_codes[N_LANGUAGES] = {"auto", "ja", "en-US", "en-GB", "fr", "fr-CA", "de", "it", "es",
                                                   "es-419", "nl", "ru", "ko", "zh-CN", "zh-TW", "pt-BR"};
const char *const g_language_names[N_LANGUAGES] = {"Auto", "Japanese", "English (US)", "English (UK)", "French",
                                                   "French (Canada)", "German", "Italian", "Spanish",
                                                   "Spanish (Latin America)", "Dutch", "Russian", "Korean",
                                                   "Chinese (Simplified)", "Chinese (Traditional)",
                                                   "Portuguese (Brazil)"};

// LANG_AUTO: the host's own language (LANG=es_ES.UTF-8 -> es), en-US when it is none of the game's. Linux only:
// macOS / Windows apps get no locale variables from the system, so Auto is English there (pick one in settings).
static const char *host_language(void) {
#ifndef __linux__
    return "en-US";
#endif
    const char *e = getenv("LC_ALL");
    if (!e || !*e) e = getenv("LC_MESSAGES");
    if (!e || !*e) e = getenv("LANG");
    if (!e || !*e || !strcmp(e, "C") || !strcmp(e, "POSIX")) return "en-US";
    char l[8] = {0};
    size_t n = 0;
    for (const char *p = e; *p && *p != '.' && *p != '@' && *p != '_' && n < sizeof l - 1; p++) l[n++] = *p;
    static const struct { const char *loc, *code; } map[] = {{"ja", "ja"}, {"ko", "ko"}, {"ru", "ru"}, {"nl", "nl"},
                                                             {"de", "de"}, {"it", "it"}, {"fr", "fr"}, {"es", "es"},
                                                             {"pt", "pt-BR"}};
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (!strcmp(l, map[i].loc)) return map[i].code;
    if (!strcmp(l, "zh")) return e[n] == '_' && e[n + 1] == 'T' ? "zh-TW" : "zh-CN";  // (TW / HK: traditional)
    return "en-US";
}

// The chosen game's languages as a mask over g_language_codes (bit i: index i); 0 = not known, so all of them
static uint32_t g_game_languages;

bool settings_language_supported(int index) {
    return index <= LANG_AUTO || index >= N_LANGUAGES || !g_game_languages || (g_game_languages >> index & 1);
}

// The metadata's SupportedLanguageFlag (nn::ns::ApplicationControlProperty: one bit per language in this order:
// American English, British English, Japanese, French, German, Latin American Spanish, Spanish, Italian, Dutch,
// Canadian French, Portuguese, Russian, Korean, Traditional Chinese, Simplified Chinese, Brazilian Portuguese)
// -> our indices. Portuguese (Portugal) is served as Brazilian Portuguese, the one Portuguese in the list.
static const int8_t k_flag_to_index[16] = {2, 3, 1, 4, 6, 9, 8, 7, 10, 5, 15, 11, 12, 14, 13, 15};

// The add-on content the game's title.json lists ("addons": [1, 2, ...], the importer found their packages): aoc:u
static char g_title_text[2048];  // the title.json read at boot (settings_title_int)

// A number from the game's title.json (its manifest's fields): `"key": 123`, or def
int settings_title_int(const char *key, int def) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *k = strstr(g_title_text, pat);
    if (!k || !(k = strchr(k + strlen(pat), ':'))) return def;
    char *end;
    long v = strtol(k + 1, &end, 10);
    return end == k + 1 ? def : (int)v;
}

static uint32_t g_addons[64];
static int g_addon_count;

int settings_addons(const uint32_t **list) {
    *list = g_addons;
    return g_addon_count;
}

static void load_addons(const char *text) {
    g_addon_count = 0;
    const char *k = strstr(text, "\"addons\"");
    if (!k || !(k = strchr(k, '['))) return;
    for (k++; g_addon_count < 64 && *k && *k != ']'; ) {
        char *end;
        unsigned long v = strtoul(k, &end, 10);
        if (end == k) { k++; continue; }
        g_addons[g_addon_count++] = (uint32_t)v;
        k = end;
    }
}

void settings_game_languages(const char *game_root) {
    g_game_languages = 0;
    g_addon_count = 0;
    g_title_text[0] = 0;
    char path[1100], text[2048] = {0};
    snprintf(path, sizeof path, "%s/title.json", game_root);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    memcpy(g_title_text, text, sizeof g_title_text);
    load_addons(text);
    const char *k = strstr(text, "\"language_flags\"");
    if (!k || !(k = strchr(k + 16, ':'))) return;
    for (k++; *k == ' ' || *k == '"'; k++) {}
    unsigned long flags = strtoul(k, NULL, 10);
    for (int bit = 0; bit < 16; bit++)
        if (flags >> bit & 1) g_game_languages |= 1u << k_flag_to_index[bit];
}

// The language to serve: the setting (or the host's) when the game has it, else English (US), else its first
static const char *supported_language(const char *code) {
    for (int i = 1; i < N_LANGUAGES; i++)
        if (!strcmp(code, g_language_codes[i]) && settings_language_supported(i)) return code;
    if (settings_language_supported(2)) return g_language_codes[2];
    for (int i = 1; i < N_LANGUAGES; i++)
        if (settings_language_supported(i)) return g_language_codes[i];
    return code;
}

const char *settings_language(void) {
    int i = g_settings.language;
    return supported_language(i > LANG_AUTO && i < N_LANGUAGES ? g_language_codes[i] : host_language());
}

const char *settings_language_name(void) {
    const char *c = settings_language();
    for (int i = 0; i < N_LANGUAGES; i++)
        if (!strcmp(c, g_language_codes[i])) return g_language_names[i];
    return "English";
}

void settings_load(void) {
    static bool loaded;
    if (loaded) return;
    loaded = true;
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    char line[256];
    bool tip_key = false, aspect_key = false;
    while (fgets(line, sizeof line, f)) {
        char key[64];
        char val[128];
        if (sscanf(line, " %63[^= ] = %127[^\n]", key, val) < 1) continue;
        if (!strcmp(key, "aspect")) g_settings.aspect = !strcmp(val, "original") ? ASPECT_ORIGINAL : ASPECT_UNLOCKED;
        else if (!strcmp(key, "fullscreen")) g_settings.fullscreen = atoi(val) != 0;
        else if (!strcmp(key, "volume")) g_settings.volume = (float)atof(val);
        else if (!strcmp(key, "show_fps")) g_settings.show_fps = atoi(val) != 0;
        else if (!strcmp(key, "resolution")) {
            int n = atoi(val);
            g_settings.res_mode = !strcmp(val, "dynamic") ? RES_DYNAMIC : (!strcmp(val, "75%") || !strcmp(val, "67%")) ? RES_Q75 :
                                  !strcmp(val, "50%") ? RES_Q50 : !strcmp(val, "25%") ? RES_Q25 :
                                  (n >= 1 && n <= 4) ? n : n > 4 ? 4 : RES_MATCH;  // (5x-8x were removed)
        }
        else if (!strcmp(key, "upscaler")) g_settings.upscaler = !strcmp(val, "bilinear") ? UPSCALE_BILINEAR : UPSCALE_SHARP;  // (metalfx / fsr1: this OS's)
        else if (!strcmp(key, "antialias")) g_settings.antialias = !strcmp(val, "off") ? AA_OFF : AA_SMAA;  // ("fxaa": SMAA now)
        else if (!strcmp(key, "language")) {  // ("auto" or one of the game's codes; anything else: auto)
            g_settings.language = LANG_AUTO;
            for (int i = 1; i < N_LANGUAGES; i++)
                if (!strcmp(val, g_language_codes[i])) g_settings.language = i;
        }
        else if (!strcmp(key, "menu_tip_seen")) g_settings.menu_tip_seen = atoi(val) != 0, tip_key = true;
        else if (!strcmp(key, "aspect_asked")) g_settings.aspect_asked = atoi(val) != 0, aspect_key = true;
        else bind_settings_load(key, val);  // bind_pad_* / bind_key_*: runtime/hle/bindings.c
    }
    fclose(f);
    if (!tip_key) g_settings.menu_tip_seen = true;  // (a settings file from before the note: not a first launch)
    if (!aspect_key) g_settings.aspect_asked = true;  // (likewise: the aspect ratio was chosen already)
    if (g_settings.volume < 0) g_settings.volume = 0;
    if (g_settings.volume > 2) g_settings.volume = 2;
    LOG("[settings] loaded %s\n", SETTINGS_FILE);
}

void settings_save(void) {
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) { LOG("[settings] cannot write %s\n", SETTINGS_FILE); return; }
    fprintf(f, "# PicRecomp settings (edited from the in-game menu: F1, Esc, or Minus+Plus on a controller)\n");
    fprintf(f, "aspect = %s\n", g_settings.aspect == ASPECT_ORIGINAL ? "original" : "unlocked");
    fprintf(f, "fullscreen = %d\n", g_settings.fullscreen);
    fprintf(f, "volume = %.2f\n", g_settings.volume);
    fprintf(f, "show_fps = %d\n", g_settings.show_fps);
    fprintf(f, "upscaler = %s\n", g_settings.upscaler == UPSCALE_METALFX ? "metalfx" : g_settings.upscaler == UPSCALE_FSR1 ? "fsr1" : "bilinear");
    fprintf(f, "antialias = %s\n", g_settings.antialias == AA_SMAA ? "smaa" : "off");
    if (g_settings.res_mode == RES_DYNAMIC) fprintf(f, "resolution = dynamic\n");
    else if (g_settings.res_mode == RES_Q75) fprintf(f, "resolution = 75%%\n");
    else if (g_settings.res_mode == RES_Q25) fprintf(f, "resolution = 25%%\n");
    else if (g_settings.res_mode == RES_Q50) fprintf(f, "resolution = 50%%\n");
    else if (g_settings.res_mode >= 1 && g_settings.res_mode <= 8) fprintf(f, "resolution = %dx\n", g_settings.res_mode);
    else fprintf(f, "resolution = match\n");
    fprintf(f, "language = %s\n", g_language_codes[g_settings.language > LANG_AUTO && g_settings.language < N_LANGUAGES
                                                     ? g_settings.language : LANG_AUTO]);
    fprintf(f, "aspect_asked = %d\n", g_settings.aspect_asked);
    fprintf(f, "menu_tip_seen = %d\n", g_settings.menu_tip_seen);
    bind_settings_save(f);
    fclose(f);
}
