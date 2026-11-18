// Start screen, shown before the game boots (user 2026-10-08, styled after Dusklight's launcher): PLAY,
// SETTINGS, QUIT, game-files status at the lower left. Nothing of the game ships with
// PicRecomp: on first launch the user picks the folder with their own game files (v1.0.0), we check the main
// module's build id, copy the files and build the game's data image; PLAY stays disabled until that worked. The background is a frame of the game's own title movie (from the user's files) once extracted.
//
// Skipped with --play, PICRECOMP_NO_LAUNCHER=1, or any scripted test run (PICRECOMP_INPUT): tests boot straight in.
// The in-game menu's "Title screen" comes back here without leaving the process (runtime/main.c reboots the game).
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <atomic>

#include "imgui.h"
#include "imgui_internal.h"  // (SetKeyOwner)
#include "option_rows.h"
#include "osk.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_sdl3.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "platform.h"
#include "paths.h"
#include "gamebuild.h"
#include "gamelib.h"
#include "importer.h"
#include "nxpkg.h"
#include "touch.h"
#include "romfs_build.h"
#include "settings.h"
#include "input_bind.h"
void controls_bindings_draw(float scale);  // ui/controls_ui.mm
void ui_window_init(void);
SDL_Window *ui_window(void);
// runtime/nvn/vulkan: window frames and ImGui plumbing
bool vk_frame_begin(const float clear[4]);
void vk_frame_end(const char *shot_path);
void *vk_frame_cmd(void);
bool vk_imgui_backend_init(void);
void vk_imgui_backend_shutdown(void);
uint64_t vk_imgui_texture_rgba(const void *rgba, int w, int h);
}

static const uint8_t BUILD_ID[8] = {0x26, 0x07, 0x97, 0x01, 0x8f, 0xef, 0xb8, 0x45};  // PICROSS S v524288 main (docs/STATUS.md)
// The release tag this build is (also names the release files; Makefile / tools/bundle_macos.sh / make_dmg.sh read
// it): alpha_<n> while the game isn't playable, beta_<n> until 1.0, then v<major>.<minor>.<patch>.
#define PICRECOMP_VERSION "alpha_1"

// ------------------------------------------------------------------ update check
// The start screen asks GitHub for the project's releases once per launch (a background curl, which macOS and
// Windows 10+ ship) and offers the best one if it outranks this build: any v1.0.0+ above every beta, every beta
// above every alpha, alphas and betas by number; other tags are ignored. Offline / no curl / anything unexpected: nothing is shown.
// The release feed checked for updates ("" = no update check).
#define RELEASES_URL "https://api.github.com/repos/macaon/picrecomp/releases"
#define RELEASE_PAGE "https://github.com/macaon/picrecomp/releases/tag/"

static long long tag_rank(const std::string &t) {
    int n, a, b, c;
    char end;
    if (sscanf(t.c_str(), "alpha_%d%c", &n, &end) == 1 && n > 0 && n < (1 << 24)) return n;
    if (sscanf(t.c_str(), "beta_%d%c", &n, &end) == 1 && n > 0 && n < (1 << 24)) return (1LL << 24) + n;
    if (sscanf(t.c_str(), "v%d.%d.%d%c", &a, &b, &c, &end) == 3 && a >= 1 && b >= 0 && c >= 0 && b < 4096 && c < 4096)
        return (1LL << 48) + ((long long)a << 24) + ((long long)b << 12) + c;
    return -1;
}

// "Alpha 1" / "Beta 2" / "1.0.0" (with_word: "Version 1.0.0")
static std::string tag_label(const std::string &t, bool with_word) {
    int n;
    char end;
    if (sscanf(t.c_str(), "beta_%d%c", &n, &end) == 1) return "Beta " + std::to_string(n);
    if (sscanf(t.c_str(), "alpha_%d%c", &n, &end) == 1) return "Alpha " + std::to_string(n);
    std::string v = t.size() > 1 && t[0] == 'v' ? t.substr(1) : t;
    return with_word ? "Version " + v : v;
}

static std::string g_update_tag;  // a newer release's tag (g_update_lock)
static pthread_mutex_t g_update_lock = PTHREAD_MUTEX_INITIALIZER;

static void *update_thread(void *) {
    std::string body;
    if (const char *t = getenv("PICRECOMP_UPDATE_TAGS")) {  // (test) "beta_2,v1.0.0": these releases, no network
        for (const char *p = t; *p;) {
            const char *c = strchr(p, ',');
            body += "\"tag_name\": \"" + std::string(p, c ? c - p : strlen(p)) + "\"\n";
            p = c ? c + 1 : p + strlen(p);
        }
    } else {
        if (!*RELEASES_URL) return NULL;
#ifdef _WIN32
        const char *sr = getenv("SystemRoot");
        std::string curl = std::string(sr ? sr : "C:\\Windows") + "\\System32\\curl.exe";
#else
        std::string curl = "/usr/bin/curl";
#endif
        std::string out = std::string(rt_data_root()) + "/cache/releases.json";
        mkdir((std::string(rt_data_root()) + "/cache").c_str(), 0755);
        const char *av[] = {curl.c_str(), "-fsSL", "--max-time", "15", "-H", "Accept: application/vnd.github+json",
                            "-A", "PicRecomp/" PICRECOMP_VERSION, RELEASES_URL, NULL};
        PlatProc p = plat_spawn(av, out.c_str(), false);
        if (!p || plat_proc_wait(p, true) != 0) { remove(out.c_str()); return NULL; }
        if (FILE *f = fopen(out.c_str(), "rb")) {
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0 && body.size() < (8u << 20)) body.append(buf, n);
            fclose(f);
        }
        remove(out.c_str());
    }
    std::string best;
    long long best_rank = tag_rank(PICRECOMP_VERSION);
    for (size_t at = 0; (at = body.find("\"tag_name\"", at)) != std::string::npos;) {
        size_t q1 = body.find('"', body.find(':', at) + 1), q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string tag = body.substr(q1 + 1, q2 - q1 - 1);
        if (tag_rank(tag) > best_rank) best_rank = tag_rank(tag), best = tag;
        at = q2;
    }
    if (!best.empty()) fprintf(stderr, "[launcher] update available: %s (this build %s)\n", best.c_str(), PICRECOMP_VERSION);
    pthread_mutex_lock(&g_update_lock);
    g_update_tag = best;
    pthread_mutex_unlock(&g_update_lock);
    return NULL;
}

// ------------------------------------------------------------------ game files
static std::string g_root;  // extracted game: <root>/exefs, <root>/romfs

static bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// The text of "key": "value" in a flat JSON file ("" if there is none). The manifests (titles/*.json, written next to
// a prepared game as title.json) are flat, so this is all the parsing they need.
static std::string json_str(const std::string &path, const char *key) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return "";
    char buf[2048];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    std::string pat = std::string("\"") + key + "\"";
    const char *k = strstr(buf, pat.c_str());
    if (!k || !(k = strchr(k + pat.size(), ':')) || !(k = strchr(k, '"'))) return "";
    const char *e = strchr(++k, '"');
    return e ? std::string(k, e - k) : "";
}

// The copied game files alone ("" = there), without the game code built from them
static std::string check_copied_files(const std::string &root) {
    FILE *f = fopen((root + "/exefs/main").c_str(), "rb");
    if (!f) return "No game files";
    uint8_t id[8] = {0};
    fseek(f, 0x40, SEEK_SET);
    size_t n = fread(id, 1, 8, f);
    fclose(f);
    std::string want = json_str(root + "/title.json", "main_build_id");  // (a folder prepared before manifests: PICROSS S)
    char have[17] = {0};
    for (int k = 0; k < 8; k++) snprintf(have + 2 * k, 3, "%02x", id[k]);
    if (n != 8 || (want.empty() ? memcmp(id, BUILD_ID, 8) != 0 : want.compare(0, 16, have) != 0))
        return "Unsupported game version (needs the latest update)";
    // the data the game reads (services/fs.c): an image, or the description of where it is in the game package
    if (!file_exists(root + "/romfs.bin") && !file_exists(root + "/romfs.src")) return "Game data missing";
    return "";
}

// "" = ready to play, else what is wrong
static std::string check_game_files(const std::string &root) {
    std::string why = check_copied_files(root);
    if (why.empty() && !gamelib_present(root.c_str())) return "Game code not built yet";
    return why;
}

// ------------------------------------------------------------------ setup (background thread)
// The player brings their own game files: a folder holding exefs/ and romfs/. They're copied into the data root (APFS
// clones / hard links where the file system allows) and the data image the game reads is built from romfs/
// (runtime/romfs_build.c).
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static std::string g_src;           // the picked folder
static std::string g_add_keys;      // its prod.keys, when not beside them
static std::string g_add_dir;       // the folder chosen on the Add a game page
static bool g_add_auto;             // (test) start the import at once
static std::string g_job;           // current setup step / result for the status line
static std::string g_job_hint;      // a failure: what to do about it (shown under it)
static bool g_busy, g_job_failed;
static bool g_confirm_delete;       // game-files page: "Delete the game files?" shown

static void set_job(const std::string &s, bool failed = false, const std::string &hint = "") {
    pthread_mutex_lock(&g_lock);
    g_job = s;
    g_job_failed = failed;
    g_job_hint = hint;
    pthread_mutex_unlock(&g_lock);
}

// Setup failures: a short headline and what to do next (also PICRECOMP_LAUNCHER_SHOT=<f>:fail<1-6>, screenshots)
enum { FAIL_NOT_FOUND = 1, FAIL_UNUSABLE, FAIL_COPY, FAIL_VERSION, FAIL_MISSING, FAIL_BUILD };
static void set_failure(int k) {
    static const char *const head[] = {"", "No game files found in that folder", "Those files can't be used",
                                       "Setup didn't finish", "Wrong version of the game", "Game files incomplete",
                                       "The game code didn't build"};
    static const char *const hint[] = {
        "",
        "Choose the folder that holds the game files.",
        "Compressed packages (.nsz / .xcz) can't be read: decompress them to .nsp / .xci first.",
        "Make sure there's about 12 GB free and try again.",
        "PicRecomp needs version 1.0.0 of the game, without updates.",
        "Some files are missing. Choose the folder again; if it happens again, copy your extracted game files again.",
        "Make sure there's about 2 GB free and try again. If it keeps happening, report it on GitHub."};
    if (k >= 1 && k <= 6) set_job(head[k], true, hint[k]);
}

static bool is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// exefs: the folder with main and main.npdm; romfs: a folder named romfs (the game's data root); or the game package
// itself (package: .nsp / .xci / .nca, read in place by runtime/nxpkg.c) and the keys file beside it (prod.keys).
// Found in the picked folder or a few levels below it (some folder layouts nest them).
static void find_game(const std::string &dir, int depth, std::string &exefs, std::string &romfs, std::string &package,
                      std::string &keys, bool &unusable) {
    if (depth > 4 || (!exefs.empty() && !romfs.empty())) return;
    if (exefs.empty() && file_exists(dir + "/main") && file_exists(dir + "/main.npdm")) exefs = dir;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> sub;
    uint64_t best = 0;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name, lower = n;
        if (n[0] == '.') continue;
        for (auto &ch : lower) ch = (char)tolower((unsigned char)ch);
        std::string ext = lower.size() > 4 ? lower.substr(lower.size() - 4) : "";
        struct stat st;
        if (stat((dir + "/" + n).c_str(), &st)) continue;
        if (ext == ".nsz" || ext == ".xcz") unusable = true;  // (compressed packages: decompress them first)
        if ((ext == ".xci" || ext == ".nsp" || ext == ".nca") && !S_ISDIR(st.st_mode) && (uint64_t)st.st_size > best &&
            package.empty()) {
            best = (uint64_t)st.st_size;  // (the biggest: the base game, not a small update or DLC)
            package = dir + "/" + n;
        }
        if (lower == "prod.keys" && keys.empty()) keys = dir + "/" + n;
        if (S_ISDIR(st.st_mode)) {
            if (lower == "romfs" && romfs.empty()) romfs = dir + "/" + n;
            else sub.push_back(dir + "/" + n);
        }
    }
    closedir(d);
    std::sort(sub.begin(), sub.end());
    for (auto &s : sub) find_game(s, depth + 1, exefs, romfs, package, keys, unusable);
}

static bool build_id_ok(const std::string &main_path) {
    FILE *f = fopen(main_path.c_str(), "rb");
    if (!f) return false;
    uint8_t id[8] = {0};
    fseek(f, 0x40, SEEK_SET);
    size_t n = fread(id, 1, 8, f);
    fclose(f);
    return n == 8 && !memcmp(id, BUILD_ID, 8);
}

// progress (the overlay's bar): the romfs copy, then the image build - each about the size of romfs/
static std::atomic<unsigned long long> g_done, g_total;
// The bar: copying and the data image are the first tenth, building the game code (runtime/gamebuild.c) the rest
static unsigned long long g_phase_base;
static std::atomic<float> g_build_frac{-1};  // -1: not building
static bool g_copy_skipped;                  // (only the game code to build: the bar is all of that)
static void setup_progress(uint64_t done, uint64_t) { g_done = g_phase_base + done; }
static void build_progress(double f, const char *step) {
    g_build_frac = (float)f;
    static const char *last;
    if (step != last) last = step, set_job(step);
}
static float extract_progress(void) {
    unsigned long long t = g_total, d = g_done;
    float b = g_build_frac, f = b >= 0 ? (g_copy_skipped ? b : 0.1f + 0.9f * b) : t ? 0.1f * (float)((double)d / (double)t) : 0;
    return f < 0 ? 0 : f > 0.99f ? 0.99f : f;
}

// Builds the game code from the copied files (a few minutes); false = failed (the job line says so)
static bool build_game_code(void) {
    int cores = SDL_GetNumLogicalCPUCores(), ram_gb = SDL_GetSystemRAM() / 1024;
    GameBuildOpts o = {build_progress, cores < ram_gb - 2 ? cores : ram_gb - 2 > 1 ? ram_gb - 2 : 1};
    char why[256] = "";
    g_build_frac = 0;
    bool ok = gamebuild_run(g_root.c_str(), &o, why, sizeof why);
    if (!ok) {
        fprintf(stderr, "[launcher] game code: %s\n", why);
        set_failure(FAIL_BUILD);
    }
    return ok;
}

static void *extract_thread(void *) {
    std::string src;
    pthread_mutex_lock(&g_lock);
    src = g_src;
    pthread_mutex_unlock(&g_lock);
    g_done = 0, g_total = 0, g_phase_base = 0, g_build_frac = -1, g_copy_skipped = false;
    std::string exefs, romfs, package, keys;
    bool unusable = false;  // (compressed game packages)
    bool cleaned = false;   // (an earlier setup's files removed already: the keys copied in since stay)
    if (src.empty() && check_copied_files(g_root).empty()) {  // copied already (an update needs new game code)
        g_copy_skipped = true;
        if (!build_game_code()) goto done;
        goto check;
    }
    set_job("Looking for the game files...");
    find_game(src, 0, exefs, romfs, package, keys, unusable);
    if ((exefs.empty() || romfs.empty()) && !package.empty()) {  // the game package: ExeFS out, RomFS read in place
        if (keys.empty()) {
            set_job("No prod.keys beside the game package", true, "Put the keys file from your own Switch (prod.keys) in "
                    "the same folder as the game and choose it again.");
            goto done;
        }
        if (!cleaned) rt_remove_game_files(g_root.c_str());
        mkdir(g_root.c_str(), 0755);
        set_job("Reading the game package...");
        char why[256] = "";
        uint64_t tid = 0;
        if (!nx_setup(package.c_str(), keys.c_str(), g_root.c_str(), &tid, why, sizeof why)) {
            fprintf(stderr, "[launcher] %s: %s\n", package.c_str(), why);
            why[0] = (char)toupper((unsigned char)why[0]);
            set_job("Those files can't be used", true, why);
            goto done;
        }
        if (tid != 0x01008CF01BAAC000ull || !build_id_ok(g_root + "/exefs/main")) {
            rt_remove_game_files(g_root.c_str());
            set_failure(FAIL_VERSION);
            goto done;
        }
        if (!build_game_code()) goto done;
        goto check;
    }
    if (exefs.empty() || romfs.empty()) { set_failure(unusable ? FAIL_UNUSABLE : FAIL_NOT_FOUND); goto done; }
    if (!build_id_ok(exefs + "/main")) { set_failure(FAIL_VERSION); goto done; }
    {
        // (the picked folder can't be PicRecomp's own copy: setting up again deletes that first)
        char a[4096], b[4096];
        mkdir(g_root.c_str(), 0755);
        if (realpath(exefs.c_str(), a) && realpath(g_root.c_str(), b) && !strncmp(a, b, strlen(b)) &&
            (a[strlen(b)] == '/' || a[strlen(b)] == '\\' || !a[strlen(b)])) {
            set_failure(FAIL_NOT_FOUND);
            goto done;
        }
        rt_remove_game_files(g_root.c_str());  // (an earlier or interrupted setup's; removes the folder too)
        mkdir(g_root.c_str(), 0755);
        unsigned long long rbytes = romfs_dir_bytes(romfs.c_str());
        g_total = 2 * rbytes;
        set_job("Copying the game files...");
        if (!romfs_copy_tree(exefs.c_str(), (g_root + "/exefs").c_str(), NULL) ||
            !romfs_copy_tree(romfs.c_str(), (g_root + "/romfs").c_str(), setup_progress)) {
            set_failure(FAIL_COPY);
            goto done;
        }
        g_phase_base = rbytes, g_done = rbytes;
        set_job("Building the game data image...");
        char why[256] = "";
        if (!romfs_build((g_root + "/romfs").c_str(), (g_root + "/romfs.bin").c_str(), setup_progress, why, sizeof why)) {
            fprintf(stderr, "[launcher] romfs image: %s\n", why);
            set_failure(FAIL_COPY);
            goto done;
        }
        if (!build_game_code()) goto done;
    }
check:
    {
        std::string why = check_game_files(g_root);
        if (why.empty()) set_job("Game files ready.");
        else set_failure(why.find("version") != std::string::npos ? FAIL_VERSION : FAIL_MISSING);
    }
done:
    pthread_mutex_lock(&g_lock);
    g_busy = false;
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

// ------------------------------------------------------------------ folder dialog (SDL calls back on any thread)
static void SDLCALL picked(void *, const char *const *files, int) {
    if (!files || !files[0]) return;
    pthread_mutex_lock(&g_lock);
    g_src = files[0];
    pthread_mutex_unlock(&g_lock);
}


static void SDLCALL picked_add(void *, const char *const *files, int) {
    if (!files || !files[0]) return;
    pthread_mutex_lock(&g_lock);
    g_add_dir = files[0];
    pthread_mutex_unlock(&g_lock);
}
static void SDLCALL picked_keys(void *, const char *const *files, int) {
    if (!files || !files[0]) return;
    pthread_mutex_lock(&g_lock);
    g_add_keys = files[0];
    pthread_mutex_unlock(&g_lock);
}
// ------------------------------------------------------------------ background: a title movie frame
static uint64_t g_bg;  // ImGui texture id (Vulkan descriptor set)
#define BG_TEXID ((ImTextureID)g_bg)
typedef void *GpuDevice;
static int g_bg_w, g_bg_h;

static void load_background(GpuDevice dev) {
    std::string path = g_root + "/romfs/region_common/movie/title.webm";
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path.c_str(), NULL, NULL) < 0) return;
    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (si >= 0 && avformat_find_stream_info(fmt, NULL) >= 0) {
        AVStream *st = fmt->streams[si];
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext *cc = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(cc, st->codecpar);
        if (codec && avcodec_open2(cc, codec, NULL) == 0) {
            AVPacket *pkt = av_packet_alloc();
            AVFrame *fr = av_frame_alloc();
            int64_t want = (int64_t)(6.0 / av_q2d(st->time_base));  // a few seconds in: the island, not black
            av_seek_frame(fmt, si, want, AVSEEK_FLAG_BACKWARD);
            bool got = false;
            while (!got && av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) == 0)
                    while (avcodec_receive_frame(cc, fr) == 0)
                        if (fr->best_effort_timestamp >= want) { got = true; break; }
                av_packet_unref(pkt);
            }
            if (got) {
                int w = fr->width, h = fr->height;
                std::vector<uint8_t> rgba((size_t)w * h * 4);
                SwsContext *sws = sws_getContext(w, h, (AVPixelFormat)fr->format, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
                uint8_t *dst[1] = {rgba.data()};
                int dst_ls[1] = {w * 4};
                sws_scale(sws, fr->data, fr->linesize, 0, h, dst, dst_ls);
                sws_freeContext(sws);
                (void)dev;
                g_bg = vk_imgui_texture_rgba(rgba.data(), w, h);
                g_bg_w = w, g_bg_h = h;
            }
            av_frame_free(&fr);
            av_packet_free(&pkt);
        }
        avcodec_free_context(&cc);
    }
    avformat_close_input(&fmt);
}

// ------------------------------------------------------------------ game cards
// One card per prepared game: <data root>/games/<name>/ (what tools/picross/prepare.py writes: title.json, icon.jpg, exefs,
// romfs.bin), plus the folder the program was started with, when it holds a game. After the last card comes a "+" card. The picture is the icon the Switch shows (256x256 JPEG).
struct Card { std::string root, name; uint64_t tex = 0; };
static std::vector<Card> g_cards;
static size_t g_card;  // the selected one; g_root is its folder
static bool g_pad_last;  // the last button pressed was a controller's
static std::string g_update_of;  // the game the Add page is updating (Settings > Game files > Update this game); "" = adding one
static bool g_goto_update;       // Settings asks the main loop for the Add page, to update the chosen game
static int g_scan_gen;           // counts scan_cards runs (what is shown for a card, like its version, is read again after one)
static bool g_all_added;  // every game the importer knows (titles/*.json) has a card: nothing left for "+" to add

static uint64_t load_icon(const std::string &path) {
    AVFormatContext *fmt = NULL;
    uint64_t tex = 0;
    if (avformat_open_input(&fmt, path.c_str(), NULL, NULL) < 0) return 0;
    if (avformat_find_stream_info(fmt, NULL) >= 0 && fmt->nb_streams) {
        const AVCodec *codec = avcodec_find_decoder(fmt->streams[0]->codecpar->codec_id);
        AVCodecContext *cc = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(cc, fmt->streams[0]->codecpar);
        AVPacket *pkt = av_packet_alloc();
        AVFrame *fr = av_frame_alloc();
        if (codec && avcodec_open2(cc, codec, NULL) == 0 && av_read_frame(fmt, pkt) >= 0 &&
            avcodec_send_packet(cc, pkt) == 0 && (avcodec_receive_frame(cc, fr) == 0 ||
            (avcodec_send_packet(cc, NULL), avcodec_receive_frame(cc, fr) == 0))) {
            int w = fr->width, h = fr->height;
            std::vector<uint8_t> rgba((size_t)w * h * 4);
            SwsContext *sws = sws_getContext(w, h, (AVPixelFormat)fr->format, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
            uint8_t *dst[1] = {rgba.data()};
            int dst_ls[1] = {w * 4};
            sws_scale(sws, fr->data, fr->linesize, 0, h, dst, dst_ls);
            sws_freeContext(sws);
            tex = vk_imgui_texture_rgba(rgba.data(), w, h);
        }
        av_frame_free(&fr);
        av_packet_free(&pkt);
        avcodec_free_context(&cc);
    }
    avformat_close_input(&fmt);
    return tex;
}

static void scan_cards(const std::string &current) {
    std::vector<std::string> roots;
    std::string dir = std::string(rt_data_root()) + "/games";
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d))
            if (e->d_name[0] != '.' && check_copied_files(dir + "/" + e->d_name).empty()) {
                char real[4096];  // (the same folder reached by another path is one card)
                if (realpath((dir + "/" + e->d_name).c_str(), real)) roots.push_back(real);
            }
        closedir(d);
    }
    if (check_copied_files(current).empty() && std::find(roots.begin(), roots.end(), current) == roots.end())
        roots.push_back(current);
    std::sort(roots.begin(), roots.end());
    g_cards.clear();
    g_scan_gen++;
    bool found = false;
    for (auto &r : roots) {
        Card c;
        c.root = r;
        c.name = json_str(r + "/title.json", "name");
        if (c.name.empty()) c.name = "PICROSS S";  // (a folder prepared before the manifests)
        c.tex = load_icon(r + "/icon.jpg");
        if (r == current) g_card = g_cards.size(), found = true;
        g_cards.push_back(c);
    }
    // Release order: S, S2 .. S8, then S+ (the last to come out: "+" sorts after the digits)
    auto key = [](const Card &c) { std::string k = c.name; for (auto &ch : k) if (ch == '+') ch = '~'; return k; };
    std::stable_sort(g_cards.begin(), g_cards.end(), [&](const Card &a, const Card &b) { return key(a) < key(b); });
    for (size_t i = 0; i < g_cards.size(); i++)
        if (g_cards[i].root == current) g_card = i;
    if (!found && !g_cards.empty()) g_card = 0, g_root = g_cards[0].root;  // (the folder given isn't a game)
    // The games the importer supports are the manifests next to it (a checkout; without them "+" stays).
    char probe[1100];
    g_all_added = false;
    if (rt_resource(NULL, "titles/picross-s.json", probe, sizeof probe)) {
        std::string titles(probe, strlen(probe) - strlen("picross-s.json"));
        int supported = 0, added = 0;
        if (DIR *d = opendir(titles.c_str())) {
            while (dirent *e = readdir(d)) {
                size_t n = strlen(e->d_name);
                if (n < 6 || strcmp(e->d_name + n - 5, ".json")) continue;
                std::string slug = json_str(titles + e->d_name, "slug");
                supported++;
                for (auto &c : g_cards)
                    if (json_str(c.root + "/title.json", "slug") == slug) { added++; break; }
            }
            closedir(d);
        }
        g_all_added = supported > 0 && added == supported;
    }
}

// ------------------------------------------------------------------ screen
static ImFont *g_font_title, *g_font_menu, *g_font_small;
enum Page { MAIN, SETTINGS, SETUP, ADD };  // SETTINGS: the in-game menu's tabs (runtime/ui/menu.mm)
static int g_refocus = MAIN;  // back on the main page: select the item of the page we came from (controllers)

// A big menu line: gold + a marker when focused / hovered (keyboard, gamepad or mouse), grey when disabled
static bool g_menu_small;  // sub pages: buttons in the small font
void menu_tabs_draw(float scale, ImFont *font, ImFont *font_big, bool in_game, void (*prelaunch)(float));  // menu.mm
void menu_tabs_focus(bool first_tab);
extern int g_tab_request_ext;
extern "C" void menu_apply_window_requests(void *sdl_window);
int g_start_screen;  // the start screen is up (no game loaded: host code must not read its memory)
static float g_menu_gap = 0.35f;  // space under each item, in line heights

// The first PLAY ever (settings.menu_tip_seen): a note on opening the PicRecomp menu in game (it isn't obvious),
// OK starts the game. Returns true once OK is pressed.
static bool g_tip_test;  // (PICRECOMP_LAUNCHER_SHOT=<frame>:tip)
// First PLAY ever, before the menu note: authentic 16:9 or the unlocked (fill the window) aspect ratio.
static bool aspect_prompt(float s) {
    bool done = false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.8f, 760 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36 * s, 28 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.03f, 0.04f, 0.05f, 0.94f));
    bool open = ImGui::BeginPopupModal("##aspect", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImGui::SetWindowFontScale(1.25f);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("How would you like to play?");
        ImGui::Spacing();
        ImGui::TextDisabled("You can change this any time in Settings.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 14 * s));
        float bw = 300 * s, gap = 20 * s;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 2 * bw - gap) * 0.5f);
        int pick = -1;
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Authentic 16:9", ImVec2(bw, 44 * s))) pick = ASPECT_ORIGINAL;
        ImGui::SameLine(0, gap);
        if (ImGui::Button("Unlocked (fill the screen)", ImVec2(bw, 44 * s))) pick = ASPECT_UNLOCKED;
        if (pick >= 0) {
            g_settings.aspect = pick;
            g_settings.aspect_asked = true;
            settings_save();
            ImGui::CloseCurrentPopup();
            done = true;
        }
        ImGui::EndPopup();
    }
    return done;
}

static bool menu_tip(float s) {
    bool go = false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.8f, 760 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36 * s, 28 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.03f, 0.04f, 0.05f, 0.94f));
    bool open = ImGui::BeginPopupModal("##menutip", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImGui::SetWindowFontScale(1.25f);
        // the controller's home button first (what players reach for: the PS button on a DualSense), the combo second
        static const char *const home[] = {"Home button", "Xbox button", "PlayStation button"};  // (bind_label_family)
        static const char *const pad[] = {"Minus + Plus", "View + Menu", "Create + Options"};
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("PicRecomp has its own menu for settings, controls and more.");
        ImGui::Spacing();
        int fam = bind_label_family() % 3;
        ImGui::Text("Open it in game with F1 or Esc, or on a controller with the %s (or %s together).", home[fam], pad[fam]);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 14 * s));
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 180 * s) * 0.5f);
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("OK", ImVec2(180 * s, 44 * s)) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
            g_settings.menu_tip_seen = true;
            settings_save();
            ImGui::CloseCurrentPopup();
            go = true;
        }
        ImGui::EndPopup();
    }
    return go;
}

// Start-screen text: a soft drop shadow, then the text with a top-to-bottom colour gradient (top == bottom: flat)
static void text_fx(ImDrawList *dl, ImFont *font, float size, ImVec2 p, ImU32 top, ImU32 bottom, const char *t,
                    float shadow) {
    if (shadow > 0)
        for (float d : {shadow, shadow * 0.5f}) dl->AddText(font, size, ImVec2(p.x + d, p.y + d), IM_COL32(4, 8, 30, 110), t);
    int v0 = dl->VtxBuffer.Size;
    dl->AddText(font, size, p, top, t);
    if (top == bottom) return;
    float y0 = p.y + size * 0.2f, y1 = p.y + size * 0.85f;
    for (int i = v0; i < dl->VtxBuffer.Size; i++) {
        float f = (dl->VtxBuffer[i].pos.y - y0) / (y1 - y0);
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bottom);
        dl->VtxBuffer[i].col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f,
                                                                     a.z + (b.z - a.z) * f, a.w + (b.w - a.w) * f));
    }
}

// A four-point sparkle (PLAY's twinkle): k = 0..1 grows and fades it
static void sparkle(ImDrawList *dl, ImVec2 c, float r, float k, float spin) {
    if (k <= 0) return;
    ImVec2 pts[8];
    for (int i = 0; i < 8; i++) {
        float a = spin + i * 3.14159265f / 4, rr = (i & 1 ? r * 0.22f : r) * k;
        pts[i] = ImVec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
    }
    dl->AddCircleFilled(c, r * 0.55f * k, IM_COL32(255, 230, 150, (int)(60 * k)));  // soft glow
    dl->AddConvexPolyFilled(pts, 8, IM_COL32(255, 248, 215, (int)(235 * k)));
}

// split .. split_end: that part of the label in its own colour
static bool menu_item(const char *label, bool enabled, bool *hovered = NULL, const char *split = NULL, ImU32 col2 = 0,
                      const char *split_end = NULL) {
    ImGui::PushFont(g_menu_small ? g_font_small : g_font_menu);
    ImGui::BeginDisabled(!enabled);
    ImVec2 at = ImGui::GetCursorScreenPos();
    // (the ID from the label's fixed part: a new ID loses the selection)
    ImGui::PushID(label, split ? split : label + strlen(label));
    ImVec2 sz = ImGui::CalcTextSize(label);
    // (EnableNav: otherwise an InvisibleButton can't be reached with the d-pad / arrow keys - ImGui 1.91)
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));  // (the hover band shows the selection, no box)
    bool clicked = ImGui::InvisibleButton("##b", ImVec2(sz.x + 40, sz.y + 6), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    bool hot = ImGui::IsItemFocused() || ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered) *hovered = hot;
    ImU32 col = !enabled ? IM_COL32(170, 175, 200, 140) : hot ? IM_COL32(255, 214, 64, 255) : IM_COL32(250, 250, 255, 255);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (hot && enabled) {  // a shadow band behind the item (like the version corner's), feathered, with a warm glow
        float x0 = ImGui::GetWindowPos().x, x1 = at.x + sz.x * 0.75f, x2 = at.x + sz.x + sz.y * 3.5f;
        float f = sz.y * 0.22f, y0 = at.y - f * 0.4f, y1 = y0 + f, y3 = at.y + sz.y + f * 0.2f, y2 = y3 - f;
        ImU32 c = IM_COL32(6, 10, 34, 150), g = IM_COL32(255, 190, 80, 34), n = IM_COL32(6, 10, 34, 0), ng = IM_COL32(255, 190, 80, 0);
        for (int pass = 0; pass < 2; pass++) {  // dark band, then the glow over it
            ImU32 a = pass ? g : c, z = pass ? ng : n;
            dl->AddRectFilledMultiColor(ImVec2(x0, y1), ImVec2(x1, y2), a, a, a, a);
            dl->AddRectFilledMultiColor(ImVec2(x1, y1), ImVec2(x2, y2), a, z, z, a);
            dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), z, z, a, a);  // (feathered top / bottom)
            dl->AddRectFilledMultiColor(ImVec2(x1, y0), ImVec2(x2, y1), z, z, z, a);
            dl->AddRectFilledMultiColor(ImVec2(x0, y2), ImVec2(x1, y3), a, a, z, z);
            dl->AddRectFilledMultiColor(ImVec2(x1, y2), ImVec2(x2, y3), a, z, z, z);
        }
    }
    ImU32 col_b = hot && enabled ? IM_COL32(255, 160, 50, 255) : col;
    if (split && enabled) {
        std::string head(label, split - label);
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), at, col, col_b, head.c_str(), sz.y * 0.05f);
        std::string mid = split_end ? std::string(split, split_end - split) : std::string(split);
        ImVec2 p2(at.x + ImGui::CalcTextSize(label, split).x, at.y);
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), p2, col2, col2, mid.c_str(), sz.y * 0.05f);
        if (split_end)
            text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + ImGui::CalcTextSize(label, split_end).x, at.y),
                    col, col, split_end, sz.y * 0.05f);
    } else
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), at, col, col_b, label, enabled ? sz.y * 0.05f : 0);
    if (g_menu_gap >= 0) ImGui::Dummy(ImVec2(0, sz.y * g_menu_gap));  // line spacing
    else ImGui::SetCursorPosY(ImGui::GetCursorPosY() + sz.y * g_menu_gap);  // (tighter than the font's line height)
    ImGui::EndDisabled();
    ImGui::PopFont();
    return clicked;
}

// Controllers, as in the in-game menu (runtime/ui/menu.mm): read straight from SDL, Nintendo layout - the right face
// button (A) confirms, the bottom one (B) goes back; d-pad / left stick move. (ImGui's own SDL gamepad handling is
// switched off: it used the Xbox layout and, per the user, didn't work on the start screen.)
static ImGuiKey launcher_pad_key(Uint8 b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceDown;   // A: activate
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceRight; // B: back
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

static void launcher_pad_open_all(void) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
}

// true when the event was a controller event (handled here)
static bool launcher_pad_event(const SDL_Event &e) {
    ImGuiIO &io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(e.gdevice.which);
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        ImGuiKey k = launcher_pad_key(e.gbutton.button);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        float v = e.gaxis.value / 32767.0f;
        // the left stick also works as the d-pad (ImGui moves between items with the d-pad only; the stick scrolls):
        // pressed past half way, released under 0.3
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            static bool held[4];  // left, right, up, down
            bool x = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
            int neg = x ? 0 : 2, pos = neg + 1;
            const ImGuiKey keys[4] = {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadUp,
                                      ImGuiKey_GamepadDpadDown};
            for (int i : {neg, pos}) {
                float m = i == neg ? -v : v;
                bool now = held[i] ? m > 0.3f : m > 0.5f;
                if (now != held[i]) { held[i] = now; io.AddKeyEvent(keys[i], now); }
            }
        }
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, v < -0.3f, v < -0.3f ? -v : 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, v > 0.3f, v > 0.3f ? v : 0);
        } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, v < -0.3f, v < -0.3f ? -v : 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, v > 0.3f, v > 0.3f ? v : 0);
        }
        return true;
    }
    default: return false;
    }
}

// Returns when PLAY is chosen; QUIT exits.
// Settings' first tab, on the start screen only: the extracted game files (size, delete) and the user's own Switch
// fonts (import / back to the substitute)
// Open a folder in Finder / Explorer (relative paths are in the data root, the working directory)
static void reveal_folder(const std::string &dir) {
    std::string path = dir;
    bool abs = !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    if (!abs) path = std::string(rt_data_root()) + "/" + path;
    std::string url = "file://";
    if (path[0] != '/') url += "/";  // (Windows: file:///C:/...)
    for (char ch : path) {
        if (ch == '\\') ch = '/';
        if (ch == ' ') url += "%20";
        else if (ch == '%') url += "%25";
        else if (ch == '#') url += "%23";
        else url += ch;
    }
    SDL_OpenURL(url.c_str());
}

static SDL_Window *g_launcher_win;
static bool g_files_deleted;  // set here: the start screen goes back to setting the game files up
static void prelaunch_tab(float sc) {
    const ImVec4 gold(0.93f, 0.76f, 0.33f, 1);
    auto heading = [&](const char *t) {
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(gold, "%s", t);
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();
    };
    heading("Game files");
    static double gb = -1;
    static std::string gb_root;
    static int gb_gen = -1;
    if (gb < 0 || gb_root != g_root || gb_gen != g_scan_gen) gb = rt_game_files_size(g_root.c_str()) / 1e9, gb_root = g_root, gb_gen = g_scan_gen;
    std::string title = g_card < g_cards.size() ? g_cards[g_card].name : "", version = json_str(g_root + "/title.json", "display_version");
    std::string tid = json_str(g_root + "/title.json", "base_id");
    std::string saves = tid.empty() ? "" : std::string(rt_data_root()) + "/save/" + tid;
    struct stat sv;
    bool have_saves = !saves.empty() && stat(saves.c_str(), &sv) == 0;
    {   // what is installed
        auto row = [&](const char *label, const std::string &value) {
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(190 * sc);
            ImGui::TextUnformatted(value.c_str());
        };
        char size[32];
        snprintf(size, sizeof size, gb >= 0.1 ? "%.1f GB" : "%.0f MB", gb >= 0.1 ? gb : gb * 1000);
        row("Title", title.empty() ? "-" : title);
        row("Version", version.empty() ? "-" : version);
        row("Size on disk", size);
        ImGui::Spacing();
    }
#ifdef _WIN32
    if (ImGui::Button("Show in Explorer")) reveal_folder(g_root);
#elif defined(__linux__)
    if (ImGui::Button("Open folder")) reveal_folder(g_root);
#else
    if (ImGui::Button("Show in Finder")) reveal_folder(g_root);
#endif
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show the game's files");
    ImGui::SameLine();
    ImGui::BeginDisabled(!have_saves);
    if (ImGui::Button("Open save folder")) reveal_folder(saves);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(have_saves ? "Show this game's saves" : "No saves yet");
    ImGui::SameLine();
    if (ImGui::Button("Update this game")) g_goto_update = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a newer version");
    ImGui::SameLine();
    static bool shot_del = getenv("PICRECOMP_LAUNCHER_SHOT") && strstr(getenv("PICRECOMP_LAUNCHER_SHOT"), ":askdelete");
    if (ImGui::Button("Delete game files") || shot_del) ImGui::OpenPopup("##delfiles"), shot_del = false;  // (test: :askdelete)
    if (ImGui::BeginPopupModal("##delfiles", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(ImVec4(1, 0.72f, 0.4f, 1), "Are you sure?");
        ImGui::PopFont();
        ImGui::Text("This deletes PicRecomp's copy of the game files (%.1f GB).", gb);
        ImGui::TextDisabled("Saves and settings are kept. Your own game files aren't touched.");
        if (rt_dev_mode()) {  // (a source checkout: the files are the project's own extracted/)
            bool abs = !g_root.empty() && (g_root[0] == '/' || (g_root.size() > 1 && g_root[1] == ':'));
            std::string where = abs ? g_root : std::string(rt_data_root()) + "/" + g_root;
            ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Dev checkout: this is %s", where.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(220 * sc, 0))) {
            bool ok = rt_remove_game_files(g_root.c_str());  // (runtime/paths.c: only what the extraction made)
            set_job(ok ? "Game files deleted." : "Could not delete the game files (not in PicRecomp's data folder?).", !ok);
            g_files_deleted = true, gb = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Cancel", ImVec2(220 * sc, 0)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Update this game builds a newer version from your own packages; the current one keeps working "
                        "until it is ready. Saves are always kept.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Spacing();
}

extern "C" void launcher_run(const char *root) {
    g_root = root;
    g_start_screen = 1;
    static bool update_checked;  // (once per launch, not again after a "Title screen" reboot)
    if (!update_checked && !getenv("PICRECOMP_NO_UPDATE_CHECK")) {
        update_checked = true;
        pthread_t t;
        if (pthread_create(&t, NULL, update_thread, NULL) == 0) pthread_detach(t);
    }
    ui_window_init();
    SDL_Window *win = ui_window();
    g_launcher_win = win;
    GpuDevice dev = NULL;

    IMGUI_CHECKVERSION();
    ImGuiContext *prev_ctx = ImGui::GetCurrentContext();  // the in-game menu's, when back here after a reboot
    ImGuiContext *ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;
    // fonts belong to this context: back here via "Title screen" (in-place reboot) the previous run's are gone with
    // its context, so load them again (a stale pointer here aborted in PushFont - user, 0.1 RC)
    g_font_small = g_font_menu = g_font_title = NULL;
    // Baloo 2 SemiBold (OFL, shipped: third_party/fonts/Baloo2) - rounded, game-like; it runs small, hence the sizes
    char baloo[1024];
    std::string own = rt_resource("Baloo2-SemiBold.ttf", "third_party/fonts/Baloo2/Baloo2-SemiBold.ttf", baloo, sizeof baloo) ? baloo : "";
    const char *fonts[] = {getenv("PICRECOMP_LAUNCHER_FONT") ? getenv("PICRECOMP_LAUNCHER_FONT") : "",  // (test)
                           own.c_str(), "fonts/nintendo_udsgr_std_003.ttf", "/System/Library/Fonts/Avenir Next.ttc",
                           "/System/Library/Fonts/Helvetica.ttc", "C:/Windows/Fonts/segoeui.ttf"};
    static const ImWchar ranges[] = {0x20, 0xFF, 0x2039, 0x203A, 0x2192, 0x2192, 0x2713, 0x2713, 0x25B8, 0x25B8, 0};  // + ‹ › → ✓ ▸
    for (const char *p : fonts) {
        if (g_font_menu || !*p || !file_exists(p)) continue;
        bool b2 = p == own.c_str() || getenv("PICRECOMP_LAUNCHER_FONT");
        g_font_small = io.Fonts->AddFontFromFileTTF(p, 30 * (b2 ? 1.75f : 1), NULL, ranges);
        g_font_menu = io.Fonts->AddFontFromFileTTF(p, 52 * (b2 ? 1.85f : 1), NULL, ranges);
        g_font_title = io.Fonts->AddFontFromFileTTF(p, 92 * (b2 ? 2.3f : 1), NULL, ranges);
    }
    if (!g_font_menu) g_font_small = g_font_menu = g_font_title = io.Fonts->AddFontDefault();
    ImGuiStyle &st = ImGui::GetStyle();
    st.Colors[ImGuiCol_Header] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.08f);
    st.Colors[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, 0.14f);
    st.Colors[ImGuiCol_NavCursor] = ImVec4(0.95f, 0.78f, 0.35f, 1);
    st.FrameRounding = st.WindowRounding = 10;
    ImGui_ImplSDL3_InitForVulkan(win);
    vk_imgui_backend_init();
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, NULL, 0);  // (controllers: launcher_pad_event)
    launcher_pad_open_all();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    scan_cards(g_root);
    std::string status = check_game_files(g_root);
    if (status.empty()) load_background(dev);
    Page page = MAIN;
    bool play = false;
    // PICRECOMP_LAUNCHER_SHOT=<frame>[:<page>] (test): save build/launcher.png at that frame and exit; page: settings[:<tab>] / askdelete / setup
    long shot_at = getenv("PICRECOMP_LAUNCHER_SHOT") ? atol(getenv("PICRECOMP_LAUNCHER_SHOT")) : 0, frame = 0;
    if (shot_at) {
        const char *pg = strchr(getenv("PICRECOMP_LAUNCHER_SHOT"), ':');
        if (pg && (!strncmp(pg + 1, "settings", 8) || !strcmp(pg + 1, "askdelete"))) page = SETTINGS, menu_tabs_focus(true);
        if (pg && !strcmp(pg + 1, "setup")) page = SETUP;
        if (pg && !strcmp(pg + 1, "add")) page = ADD;
        if (const char *d = getenv("PICRECOMP_LAUNCHER_IMPORT")) {  // (test) <folder>[:<keys file>]
            g_add_dir = d, g_add_auto = true, page = ADD;
            if (const char *k = strchr(d, '|')) g_add_keys = k + 1, g_add_dir.resize(k - d);
        }
        if (pg && !strncmp(pg + 1, "fail", 4)) page = SETUP, set_failure(atoi(pg + 5));  // (the failure messages)
        if (pg && !strncmp(pg + 1, "settings", 8) && pg[9] == ':') g_tab_request_ext = atoi(pg + 10);  // settings:<tab>
        if (pg && !strcmp(pg + 1, "delete")) page = SETUP, g_confirm_delete = true;
        if (pg && !strcmp(pg + 1, "tip")) g_settings.menu_tip_seen = false, g_tip_test = true;
        if (pg && !strcmp(pg + 1, "aspect")) g_settings.aspect_asked = g_settings.menu_tip_seen = false, g_tip_test = true;
    }

    static int runs;  // (test: =once - only the first time, so a "Title screen" reboot lands on the start screen)
    const char *ap = getenv("PICRECOMP_LAUNCHER_AUTOPLAY");
    if (ap && status.empty() && !(runs && !strcmp(ap, "once"))) play = true;  // (test: PLAY at once)
    runs++;
    if (const char *t = getenv("PICRECOMP_LAUNCHER_EXTRACT")) {  // (test) <folder>: set the game files up, log, exit
        g_src = t;
        g_busy = true;
        extract_thread(NULL);
        fprintf(stderr, "[launcher] setup: %s -> %s\n", g_job.c_str(), check_game_files(g_root).empty() ? "READY" : "NOT READY");
        exit(0);
    }
    if (const char *t = getenv("PICRECOMP_LAUNCHER_SETUP")) {  // (test) <folder>: Set up started on the start screen
        g_src = t;
        g_busy = true;
        page = SETUP;
        pthread_t th;
        pthread_create(&th, NULL, extract_thread, NULL);
        pthread_detach(th);
    }
    if (getenv("PICRECOMP_LAUNCHER_DELETE")) {  // (test) what DELETE GAME FILES does, logged; then the start screen
        double gb = rt_game_files_size(g_root.c_str()) / 1e9;
        bool ok = rt_remove_game_files(g_root.c_str());
        status = check_game_files(g_root);
        fprintf(stderr, "[launcher] delete %.1f GB: %s -> %s\n", gb, ok ? "ok" : "refused", status.c_str());
        if (!status.empty()) { g_bg = 0; page = SETUP; }
    }
    while (!play) {
        menu_apply_window_requests(win);  // (the Video tab's Fullscreen)
        if (g_files_deleted) {  // (Settings > Game files deleted a game: back to the cards that are left, or the "+")
            g_files_deleted = false;
            bind_capture_cancel();
            settings_save();
            scan_cards(g_root);  // (the deleted game's card goes; the first one left is chosen)
            status = check_game_files(g_root);
            g_bg = 0;
            if (status.empty()) load_background(dev);
            page = MAIN;
        }
        {   // (test) PICRECOMP_LAUNCHER_KEYS="frame:<SDL key name | click_<x>_<y>>,...": a key pressed (or a left click) on the start screen at that frame, held two
            static const char *next = getenv("PICRECOMP_LAUNCHER_KEYS");
            static long tick, release_at = -1;
            static SDL_Scancode held = SDL_SCANCODE_UNKNOWN;
            tick++;
            auto push = [&](SDL_Scancode sc, bool down) {
                SDL_Event k = {};
                k.key.windowID = SDL_GetWindowID(win);
                k.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                k.key.scancode = sc, k.key.down = down, k.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
                SDL_PushEvent(&k);
            };
            long at;
            char name[64];
            static float click_x, click_y;
            static long click_up = -1, click_down = -1;
            auto mouse = [&](SDL_EventType type) {
                SDL_Event m = {};
                m.type = type;
                if (type == SDL_EVENT_MOUSE_MOTION) m.motion.windowID = SDL_GetWindowID(win), m.motion.x = click_x, m.motion.y = click_y;
                else m.button.windowID = SDL_GetWindowID(win), m.button.button = SDL_BUTTON_LEFT, m.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                     m.button.x = click_x, m.button.y = click_y;
                SDL_PushEvent(&m);
            };
            if (click_down >= 0 && tick >= click_down) mouse(SDL_EVENT_MOUSE_BUTTON_DOWN), click_up = tick + 3, click_down = -1;  // (the mouse
            if (click_up >= 0 && tick >= click_up) mouse(SDL_EVENT_MOUSE_BUTTON_UP), click_up = -1;                              //  moved there first)
            if (held != SDL_SCANCODE_UNKNOWN && tick >= release_at) push(held, false), held = SDL_SCANCODE_UNKNOWN;
            if (next && *next && held == SDL_SCANCODE_UNKNOWN && click_up < 0 && click_down < 0 && sscanf(next, "%ld:%63[^,]", &at, name) == 2 && tick >= at) {
                float cx, cy;
                if (sscanf(name, "click_%f_%f", &cx, &cy) == 2) {  // "click_<x>_<y>": a left click there (window pixels)
                    click_x = cx, click_y = cy;
                    mouse(SDL_EVENT_MOUSE_MOTION);
                    click_down = tick + 2;
                } else {
                    held = SDL_GetScancodeFromName(name);
                    if (held != SDL_SCANCODE_UNKNOWN) push(held, true), release_at = tick + 2;
                }
                next = strchr(next, ',');
                if (next) next++;
            }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (page == SETTINGS && bind_capture_event(&e)) continue;  // (waiting for a new binding: controls_ui.mm)
            if (!launcher_pad_event(e)) ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) g_pad_last = true;  // (the hint line names that device's buttons)
            else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) g_pad_last = false;
            if (e.type == SDL_EVENT_QUIT) exit(0);
        }
        // extraction finished: re-check, load the background
        pthread_mutex_lock(&g_lock);
        bool busy = g_busy;
        pthread_mutex_unlock(&g_lock);
        static bool was_busy;
        if (was_busy && !busy) {
            status = check_game_files(g_root);
            if (status.empty()) { load_background(dev); page = MAIN; }
        }
        was_busy = busy;

        {
            // one frame per refresh of the window's display: the swapchain presents without waiting (mailbox, for the
            // game's own pacing: device.c), so the start screen alone would draw as fast as the GPU goes
            static uint64_t next_frame;
            const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
            uint64_t period = (uint64_t)(1e9 / (dm && dm->refresh_rate > 1 ? dm->refresh_rate : 60.0f)), now = plat_now_ns();
            bool on_time = next_frame > now && next_frame - now <= period;  // (else late, or the rate changed: start over)
            if (on_time) plat_sleep_until_ns(next_frame);
            next_frame = (on_time ? next_frame : now) + period;
            static const float clear_col[4] = {0.04f, 0.05f, 0.06f, 1};
            if (!vk_frame_begin(clear_col)) { SDL_Delay(16); continue; }
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            // the backend (manual mode, no gamepads of its own) clears HasGamepad every frame, and ImGui then drops the
            // controller presses launcher_pad_event fed in and turns gamepad navigation off: set it again while a
            // controller is connected
            if (SDL_HasGamepad()) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            ImGui::NewFrame();

            ImVec2 ds = io.DisplaySize;
            float s = ds.y / 720.0f;  // layout scale (fonts are built for ~2x)
            ImGui::GetIO().FontGlobalScale = s * 0.5f;
            ImDrawList *bg = ImGui::GetBackgroundDrawList();
            if (g_bg) {  // cover the window, keep the aspect
                float ia = (float)g_bg_w / g_bg_h, wa = ds.x / ds.y;
                ImVec2 uv0(0, 0), uv1(1, 1);
                if (wa > ia) { float v = ia / wa; uv0.y = (1 - v) / 2; uv1.y = 1 - uv0.y; }
                else { float u = wa / ia; uv0.x = (1 - u) / 2; uv1.x = 1 - uv0.x; }
                bg->AddImage(BG_TEXID, ImVec2(0, 0), ds, uv0, uv1);
            } else {
                bg->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(18, 34, 40, 255), IM_COL32(40, 70, 64, 255),
                                            IM_COL32(12, 20, 24, 255), IM_COL32(8, 12, 16, 255));
            }
            {  // a shadow banner fading in from the left behind the menu, and one from the right behind the version
                const float stops[][2] = {{0.0f, 225}, {0.3f, 215}, {0.44f, 165}, {0.55f, 95}, {0.64f, 35}, {0.74f, 0}};
                for (int i = 0; i + 1 < (int)(sizeof stops / sizeof stops[0]); i++) {
                    ImU32 a = IM_COL32(6, 10, 34, (int)stops[i][1]), b = IM_COL32(6, 10, 34, (int)stops[i + 1][1]);
                    bg->AddRectFilledMultiColor(ImVec2(ds.x * stops[i][0], 0), ImVec2(ds.x * stops[i + 1][0], ds.y), a, b, b, a);
                }
                float y0 = ds.y - 190 * s, y1 = ds.y - 135 * s, y2 = ds.y - 40 * s, y3 = ds.y, x0r = ds.x * 0.5f;
                ImU32 c0 = IM_COL32(6, 10, 34, 0), c1 = IM_COL32(6, 10, 34, 235);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y0), ImVec2(ds.x, y1), c0, c0, c1, c0);  // (feathered top)
                bg->AddRectFilledMultiColor(ImVec2(x0r, y1), ImVec2(ds.x, y2), c0, c1, c1, c0);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y2), ImVec2(ds.x, y3), c0, c1, c0, c0);  // (feathered bottom)
            }

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ds);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::Begin("##launcher", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::SetScrollX(0);  // (the start screen never scrolls; a stray nav move mustn't shift it)
            ImGui::SetScrollY(0);
            float x0 = 90 * s;
            ImGui::PushFont(g_font_title);
            if (page == MAIN) {  // gold to orange, shadowed, top middle (not behind the Settings / Game Files windows)
                const char *t = "PicRecomp";
                ImVec2 tsz = ImGui::CalcTextSize(t);
                ImGui::SetCursorPos(ImVec2((ds.x - tsz.x) * 0.5f, 44 * s));
                ImVec2 p = ImGui::GetCursorScreenPos();
                text_fx(ImGui::GetWindowDrawList(), ImGui::GetFont(), ImGui::GetFontSize(), p, IM_COL32(255, 232, 120, 255),
                        IM_COL32(255, 138, 48, 255), t, 3 * s);
                ImGui::Dummy(tsz);
            }
            ImGui::PopFont();
            ImGui::SetCursorPos(ImVec2(x0, 285 * s));
            // typing in a text field / the on-screen keyboard (osk.h): B / Esc are for it, not "back" (from the frame
            // before too, so the Esc that ends the typing doesn't also leave the page)
            static bool typed_before;
            bool typing = typed_before || ui_text_busy();

            // The main page: the games are the menu. The chosen card follows the cursor (d-pad, arrows, mouse click); A /
            // Enter / Start play it, Select / - / F1 open Settings, "+" adds a game; SETTINGS and QUIT are small links
            // at the lower right (mouse, or down from the cards).
            static Page last_page = MAIN;
            bool pad_hint = g_pad_last;  // the last input was a controller's (the hint line names its buttons)
            bool refocus = last_page != MAIN;  // (back from another page: the cursor returns to the chosen card)
            last_page = page;
            static bool first_frame = true;
            refocus |= first_frame;
            first_frame = false;
            bool launch = false;
            if (page == MAIN) {
                // At most 5 to a row (the "+" counts), more wrap to a second row on the same columns. "+" is left out
                // when every supported game is already here. The block is centred under the title.
                size_t slots = g_cards.size() + (g_all_added ? 0 : 1), per_row = 5, cols = std::min(slots, per_row);
                size_t rows = (slots + per_row - 1) / per_row;
                float cs = (rows > 1 ? 150 : 184) * s, gap = (rows > 1 ? 26 : 30) * s, row_gap = 18 * s, row_h = cs + 36 * s + row_gap;
                float gw = (float)cols * cs + (float)(cols - 1) * gap, gh = (float)rows * row_h - row_gap;
                float top = 150 * s, bottom = ds.y - 150 * s;
                float cx = (ds.x - gw) * 0.5f, cy = top + fmaxf(0, (bottom - top - gh) * 0.5f);
                ImDrawList *dl = ImGui::GetWindowDrawList();
                for (size_t i = 0; i < slots; i++) {
                    bool plus = i == g_cards.size();
                    size_t in_row = std::min(per_row, slots - i / per_row * per_row);  // (each row is centred on its own)
                    float row_x = cx + (gw - ((float)in_row * cs + (float)(in_row - 1) * gap)) * 0.5f;
                    ImGui::SetCursorPos(ImVec2(row_x + (float)(i % per_row) * (cs + gap), cy + (float)(i / per_row) * row_h));
                    ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::PushID((int)i);
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    if (refocus && (plus ? g_cards.empty() : i == g_card)) ImGui::SetKeyboardFocusHere();
                    bool clicked = ImGui::InvisibleButton("##card", ImVec2(cs, cs + 36 * s), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), hot = focused || hovered,
                         sel = !plus && i == g_card;
                    ImGui::PopID();
                    if (plus) {  // a blank card with a +
                        ImVec2 br(at.x + cs, at.y + cs), m((at.x + br.x) * 0.5f, (at.y + br.y) * 0.5f);
                        ImU32 col = hot ? IM_COL32(255, 214, 64, 255) : IM_COL32(250, 250, 255, 170);
                        dl->AddRectFilled(at, br, IM_COL32(30, 44, 70, hot ? 200 : 120), 14 * s);
                        dl->AddRect(at, br, col, 14 * s, 0, 2 * s);
                        dl->AddLine(ImVec2(m.x - cs * 0.16f, m.y), ImVec2(m.x + cs * 0.16f, m.y), col, 5 * s);
                        dl->AddLine(ImVec2(m.x, m.y - cs * 0.16f), ImVec2(m.x, m.y + cs * 0.16f), col, 5 * s);
                        ImGui::PushFont(g_font_small);
                        float tw = ImGui::CalcTextSize("Add a game").x;
                        dl->AddText(ImVec2(at.x + (cs - tw) * 0.5f, br.y + 6 * s), col, "Add a game");
                        ImGui::PopFont();
                        if (clicked) {
                            mkdir((std::string(rt_data_root()) + "/games").c_str(), 0755);
                            g_update_of.clear();
                            page = ADD;
                        }
                        continue;
                    }
                    if ((focused || clicked) && !sel && !busy) {  // the cursor chooses; so does a click
                        g_card = i;
                        g_root = g_cards[i].root;
                        status = check_game_files(g_root);
                        g_bg = 0;
                        if (status.empty()) load_background(dev);
                        sel = true;
                    }
                    // A / Enter on the card play it; with the mouse a click chooses (the Play button below starts it), a
                    // double click plays
                    bool by_mouse = clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                    if (!busy && ((clicked && !by_mouse) || (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))))
                        launch = status.empty();
                    ImVec2 br(at.x + cs, at.y + cs);
                    if (g_cards[i].tex) dl->AddImageRounded((ImTextureID)g_cards[i].tex, at, br, ImVec2(0, 0), ImVec2(1, 1),
                                                            sel || hot ? IM_COL32_WHITE : IM_COL32(200, 200, 210, 255), 14 * s);
                    else dl->AddRectFilled(at, br, IM_COL32(30, 44, 70, 230), 14 * s);
                    dl->AddRect(at, br, sel ? IM_COL32(255, 214, 64, 255) : hot ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 40),
                                14 * s, 0, (sel ? 4 : 2) * s);
                    ImGui::PushFont(g_font_small);
                    float tw = ImGui::CalcTextSize(g_cards[i].name.c_str()).x;
                    dl->AddText(ImVec2(at.x + (cs - tw) * 0.5f, br.y + 6 * s), sel ? IM_COL32(255, 214, 64, 255) : IM_COL32(250, 250, 255, 255),
                                g_cards[i].name.c_str());
                    ImGui::PopFont();
                }
                // lower right: SETTINGS and QUIT as small links (the cards are the menu)
                auto link = [&](const char *label, float right) {
                    ImGui::PushFont(g_font_small);
                    ImVec2 sz = ImGui::CalcTextSize(label), p(ImGui::GetWindowPos().x + right - sz.x, ImGui::GetWindowPos().y + ds.y - 70 * s);
                    ImGui::SetCursorScreenPos(p);
                    ImGui::PushID(label);
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    bool c = ImGui::InvisibleButton("##link", sz, ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemFocused() || ImGui::IsItemHovered();
                    ImGui::PopID();
                    ImU32 col = hot ? IM_COL32(255, 214, 64, 255) : IM_COL32(250, 250, 255, 230), col_b = hot ? IM_COL32(255, 160, 50, 255) : col;
                    text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), p, col, col_b, label, 1.5f * s);
                    if (hot) dl->AddLine(ImVec2(p.x, p.y + sz.y), ImVec2(p.x + sz.x, p.y + sz.y), col, 1.5f * s);
                    ImGui::PopFont();
                    return c;
                };
                float right = ds.x - 60 * s;
                if (link("QUIT", right)) exit(0);
                bool settings_link = false;
                // the bottom line, centred: a Play button (lit while the chosen game can be played; the key that does it
                // is on it, for the last device used) and the Settings hint, both clickable
                {
                    ImGui::PushFont(g_font_small);
                    const char *play_key = pad_hint ? "Start" : "Enter", *set_key = pad_hint ? "Select" : "F1";
                    bool ready = status.empty() && !busy;
                    float kw = 10 * s, padx = 18 * s, pady = 7 * s, sp = 40 * s;
                    ImVec2 pk = ImGui::CalcTextSize(play_key), pw = ImGui::CalcTextSize("Play");
                    ImVec2 sk = ImGui::CalcTextSize(set_key), sw = ImGui::CalcTextSize("Settings");
                    float play_w = pk.x + kw + pw.x + 2 * padx, play_h = pk.y + 2 * pady, set_w = sk.x + kw + sw.x;
                    float x = ImGui::GetWindowPos().x + (ds.x - (play_w + sp + set_w)) * 0.5f;
                    float y = ImGui::GetWindowPos().y + ds.y - 70 * s;
                    {  // Play
                        ImVec2 a(x, y - pady), b(x + play_w, y - pady + play_h);
                        ImGui::SetCursorScreenPos(a);
                        bool click = ImGui::InvisibleButton("##play", ImVec2(play_w, play_h));
                        bool hot = ready && ImGui::IsItemHovered();
                        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImU32 fill = !ready ? IM_COL32(255, 255, 255, 14) : hot ? IM_COL32(255, 214, 64, 255) : IM_COL32(255, 214, 64, 46);
                        ImU32 edge = !ready ? IM_COL32(255, 255, 255, 50) : IM_COL32(255, 214, 64, 255);
                        ImU32 key_col = !ready ? IM_COL32(170, 175, 200, 140) : hot ? IM_COL32(40, 28, 0, 255) : IM_COL32(255, 214, 64, 255);
                        ImU32 txt_col = !ready ? IM_COL32(170, 175, 200, 140) : hot ? IM_COL32(20, 14, 0, 255) : IM_COL32(255, 255, 255, 255);
                        dl->AddRectFilled(a, b, fill, play_h * 0.5f);
                        dl->AddRect(a, b, edge, play_h * 0.5f, 0, 2 * s);
                        dl->AddText(ImVec2(x + padx, y), key_col, play_key);
                        dl->AddText(ImVec2(x + padx + pk.x + kw, y), txt_col, "Play");
                        if (click && ready) launch = true;
                    }
                    x += play_w + sp;
                    {  // Settings: the mouse's way in (a click opens it)
                        ImGui::SetCursorScreenPos(ImVec2(x, y));
                        if (ImGui::InvisibleButton("##settings_hint", ImVec2(sk.x + kw + sw.x, sk.y))) settings_link = true;
                        bool hot = ImGui::IsItemHovered();
                        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        dl->AddText(ImVec2(x, y), IM_COL32(255, 214, 64, 255), set_key);
                        dl->AddText(ImVec2(x + sk.x + kw, y), hot ? IM_COL32(255, 238, 150, 255) : IM_COL32(235, 238, 250, 235), "Settings");
                        if (hot) dl->AddLine(ImVec2(x, y + sk.y), ImVec2(x + sk.x + kw + sw.x, y + sk.y), IM_COL32(255, 214, 64, 255), 1.5f * s);
                    }
                    ImGui::PopFont();
                }
                bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
                if (!popup && status.empty() && !busy && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) launch = true;
                bool want_settings = settings_link;
                if (!popup && (ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false) || ImGui::IsKeyPressed(ImGuiKey_F1, false) ||
                               ImGui::IsKeyPressed(ImGuiKey_Minus, false)))
                    want_settings = true;
                {
                    bool pressed = launch;
                    // first PLAY ever: the aspect ratio question, then how to reach our menu, then the game
                    if (pressed) {
                        if (!g_settings.aspect_asked) ImGui::OpenPopup("##aspect");
                        else if (!g_settings.menu_tip_seen) ImGui::OpenPopup("##menutip");
                        else play = true;
                    }
                    if (g_tip_test) ImGui::OpenPopup(g_settings.aspect_asked ? "##menutip" : "##aspect"), g_tip_test = false;
                    if (aspect_prompt(s)) {
                        if (g_settings.menu_tip_seen) play = true;
                        else ImGui::OpenPopup("##menutip");
                    }
                    if (menu_tip(s)) play = true;
                }
                if (want_settings) {
                    static bool opened;
                    menu_tabs_focus(!opened);  // (first time this session: Prelaunch; later: the selected tab)
                    opened = true;
                    page = SETTINGS;
                }
            } else if (page == ADD) {
                // (no title here and a tight line spacing: the ten steps, the bar and the buttons fit without scrolling)
                ImGui::SetCursorPos(ImVec2(x0, 56 * s));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 3 * s));
                ImportState is;
                importer_poll(&is);
                bool idle = !is.running && !is.done && !is.failed;
                pthread_mutex_lock(&g_lock);
                std::string dir = g_add_dir, keys = g_add_keys;
                pthread_mutex_unlock(&g_lock);
                if (g_add_auto && !dir.empty() && idle) g_add_auto = false, importer_start(dir.c_str(), keys.empty() ? NULL : keys.c_str());  // (test)
                auto text = [&](const char *fmt, const char *arg, ImVec4 col) {
                    ImGui::SetCursorPosX(x0);
                    ImGui::PushTextWrapPos(ds.x * 0.55f);
                    ImGui::TextColored(col, fmt, arg);
                    ImGui::PopTextWrapPos();
                };
                const ImVec4 white(1, 1, 1, 1), gold(0.93f, 0.76f, 0.33f, 1), grey(0.7f, 0.74f, 0.85f, 1);
                ImGui::PushFont(g_font_small);
                if (idle) {
                    if (g_update_of.empty()) {
                        text("%s", "Add a game", gold);
                        text("%s", "Choose the folder with your game's two .nsp files, the base game and its update. "
                             "Your prod.keys is read from that folder or from ~/.switch, or choose it below. Adding a game "
                             "builds its code and takes about ten minutes. Nothing leaves your computer.", white);
                    } else {
                        text("Update %s", g_update_of.c_str(), gold);
                        text("%s", "Choose the folder with the game's base .nsp and its newest update (with several updates in "
                             "it, the newest is used). The new version is built beside the one you have, which keeps working "
                             "until the new one is ready; saves are kept. It takes a few minutes and uses your own packages: "
                             "nothing leaves your computer.", white);
                    }
                    text("%s", dir.empty() ? "No folder chosen yet." : dir.c_str(), grey);
                    if (!keys.empty()) text("keys: %s", keys.c_str(), grey);
                } else {
                    bool upd = !g_update_of.empty();
                    text("%s", is.failed ? "The import failed" : is.done ? (upd ? "Game updated" : "Game added")
                                                                         : (upd ? "Updating the game..." : "Adding the game..."),
                         is.failed ? ImVec4(1, 0.5f, 0.4f, 1) : gold);
                    ImGui::Dummy(ImVec2(0, 6 * s));
                    for (int k = 0; k < is.steps && k < IMPORT_MAX_STEPS; k++) {
                        if (!is.texts[k][0]) continue;
                        bool cur = is.running && k + 1 == is.step;
                        bool bad = is.failed && k + 1 == is.step;
                        ImGui::SetCursorPosX(x0);
                        ImVec2 m = ImGui::GetCursorScreenPos();
                        float fs = ImGui::GetFontSize(), cy = m.y + fs * 0.55f, u = fs * 0.5f;
                        ImDrawList *mk = ImGui::GetWindowDrawList();
                        if (cur) mk->AddCircleFilled(ImVec2(m.x + u * 0.6f, cy), u * 0.38f, IM_COL32(255, 200, 80, 255));
                        else if (bad) mk->AddCircleFilled(ImVec2(m.x + u * 0.6f, cy), u * 0.38f, IM_COL32(255, 128, 100, 255));
                        else {  // done: a check mark
                            ImU32 g = IM_COL32(120, 220, 120, 255);
                            mk->AddLine(ImVec2(m.x, cy), ImVec2(m.x + u * 0.4f, cy + u * 0.4f), g, fs * 0.12f);
                            mk->AddLine(ImVec2(m.x + u * 0.4f, cy + u * 0.4f), ImVec2(m.x + u * 1.2f, cy - u * 0.5f), g, fs * 0.12f);
                        }
                        ImGui::SetCursorPosX(x0 + fs * 1.3f);
                        if (cur && is.frac > 0) ImGui::TextColored(gold, "%s  %d%%", is.texts[k], (int)(is.frac * 100));
                        else ImGui::TextColored(cur ? gold : grey, "%s", is.texts[k]);
                    }
                    // one segment per step: done ones filled, the running one pulsing (the script reports steps, not percent)
                    ImGui::Dummy(ImVec2(0, 10 * s));
                    ImGui::SetCursorPosX(x0);
                    ImVec2 at = ImGui::GetCursorScreenPos();
                    float bw = ds.x * 0.45f, bh = 14 * s, sw = bw / (float)is.steps, t = (float)ImGui::GetTime();
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    for (int k = 0; k < is.steps; k++) {
                        bool done_k = is.done || k + 1 < is.step, run_k = is.running && k + 1 == is.step;
                        float a = done_k ? 1.0f : run_k ? 0.45f + 0.4f * sinf(t * 5) * 0.5f + 0.2f : 0.0f;
                        if (run_k && is.frac > 0) a = 1.0f;
                        ImU32 col = is.failed && k + 1 == is.step ? IM_COL32(255, 128, 100, 255) : IM_COL32(255, 200, 80, (int)(255 * a));
                        ImVec2 p0(at.x + k * sw + 2 * s, at.y), p1(at.x + (k + 1) * sw - 2 * s, at.y + bh);
                        dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 30), 4 * s);
                        if (run_k && is.frac > 0) p1.x = p0.x + (p1.x - p0.x) * (is.frac > 1 ? 1 : is.frac);  // (how far in)
                        if (a > 0 || (is.failed && k + 1 == is.step)) dl->AddRectFilled(p0, p1, col, 4 * s);
                    }
                    ImGui::Dummy(ImVec2(bw, bh + 8 * s));
                    if (is.failed) text("%s", is.error, ImVec4(1, 0.65f, 0.55f, 1));
                    if (is.done) text("%s", g_update_of.empty() ? "It is in your library now. Press Back to see it."
                                                                : "The new version is in your library now. Press Back to continue.", white);
                }
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 8 * s));
                ImGui::SetCursorPosX(x0);
                if (idle) {
                    if (menu_item("CHOOSE FOLDER", true)) SDL_ShowOpenFolderDialog(picked_add, NULL, win, NULL, false);
                    ImGui::SetCursorPosX(x0);
                    if (menu_item("CHOOSE KEYS FILE", true)) SDL_ShowOpenFileDialog(picked_keys, NULL, win, NULL, 0, NULL, false);
                    ImGui::SetCursorPosX(x0);
                    if (menu_item("IMPORT", !dir.empty())) importer_start(dir.c_str(), keys.empty() ? NULL : keys.c_str());
                    ImGui::SetCursorPosX(x0);
                }
                bool back = false;
                if (is.running) {
                    if (menu_item("CANCEL", true)) importer_cancel();
                } else {
                    back = menu_item("BACK", true) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                           ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
                }
                if (back) {
                    importer_cancel();  // (forgets a finished one)
                    page = MAIN;
                    g_update_of.clear();
                    scan_cards(g_root);
                    status = check_game_files(g_root);
                    g_bg = 0;
                    if (status.empty()) load_background(dev);
                }
                ImGui::PopStyleVar();
            } else if (page == SETTINGS) {
                // the in-game menu's tabs (runtime/ui/menu.mm), in a window like the in-game one; tabs that need a
                // loaded game say so
                float sc = ds.y / 1080.0f, w = fminf(ds.x * 0.9f, 1500 * sc), h = ds.y - 2 * 18 * sc;  // (all the height: a slim margin)
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ds.x,
                                                          ImGui::GetWindowPos().y + ds.y), IM_COL32(0, 0, 0, 110));
                ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetCursorScreenPos(ImVec2(wp.x + (ds.x - w) * 0.5f, wp.y + (ds.y - h) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.09f, 0.08f, 0.96f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * sc);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * sc, 24 * sc));
                ImGui::BeginChild("##settings", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PushFont(g_font_small);
                ImGui::PushFont(g_font_menu);
                ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "Settings");
                ImGui::PopFont();
                bool back;
                {  // close: a drawn X in the corner (B / Esc too)
                    float bx = 44 * sc;
                    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    back = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    ImU32 col = hot ? IM_COL32(236, 194, 84, 255) : IM_COL32(220, 216, 205, 255);
                    float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * sc * 1.5f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (hot) dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(255, 255, 255, 20), 8 * sc);
                    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
                    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
                }
                bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);  // (before the pages run, like the menu)
                menu_tabs_draw(sc, g_font_small, g_font_menu, false, prelaunch_tab);
                if (g_goto_update) {  // Game files > Update this game: the Add page, for this game
                    g_goto_update = false;
                    g_update_of = g_card < g_cards.size() ? g_cards[g_card].name : "";
                    bind_capture_cancel();
                    settings_save();
                    mkdir((std::string(rt_data_root()) + "/games").c_str(), 0755);
                    page = ADD;
                    back = false;
                }
                int cap_dev;
                bind_capture_active(&cap_dev, NULL, NULL);
                if (!popup && !typing && cap_dev < 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                    (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    back = true;
                ImGui::PopFont();
                ImGui::EndChild();
                if (back) {
                    bind_capture_cancel();
                    settings_save();
                    page = MAIN, g_refocus = SETTINGS;
                }
            } else {
                // SETUP: the game files, in a window like Settings (option rows: runtime/ui/option_rows.h) - choose the
                // folder with the game files, set up; once set up, delete. Each row's explanation on the right.
                float sc = ds.y / 1080.0f, w = fminf(ds.x * 0.9f, 1500 * sc), h = ds.y - 2 * 18 * sc;
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ds.x,
                                                          ImGui::GetWindowPos().y + ds.y), IM_COL32(0, 0, 0, 110));
                ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetCursorScreenPos(ImVec2(wp.x + (ds.x - w) * 0.5f, wp.y + (ds.y - h) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.09f, 0.08f, 0.96f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * sc);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * sc, 24 * sc));
                ImGui::BeginChild("##setup", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PushFont(g_font_small);
                ImGui::PushFont(g_font_menu);
                ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "Game Files");
                ImGui::PopFont();
                bool back = false;
                if (!busy) {  // close: the drawn X in the corner, as Settings (B / Esc too)
                    float bx = 44 * sc;
                    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    back = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    ImU32 col = hot ? IM_COL32(236, 194, 84, 255) : IM_COL32(220, 216, 205, 255);
                    float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * sc * 1.5f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (hot) dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(255, 255, 255, 20), 8 * sc);
                    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
                    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
                }
                ImGui::Spacing();

                pthread_mutex_lock(&g_lock);
                std::string src = g_src, job = g_job, job_hint = g_job_hint;
                bool failed = g_job_failed;
                pthread_mutex_unlock(&g_lock);
                bool ready = status.empty() && !busy;  // (set up: the window offers to delete them)
                static double files_gb = -1;
                // chosen: "Selected" with a green check (long paths ran over the label); the path is in the
                // explanation, and choosing again opens the picker again
                std::string src_v = src.empty() ? "Choose..." : "Selected";
                auto check_left_of_value = [&](const std::string &v) {
                    if (v != "Selected") return;
                    ImVec2 rb = ImGui::GetItemRectMax(), ra = ImGui::GetItemRectMin();
                    float fs = ImGui::GetFontSize(), vw = ImGui::CalcTextSize(v.c_str()).x;
                    float x = rb.x - 16 * sc - vw - fs * 0.95f, cy = (ra.y + rb.y) * 0.5f, u = fs * 0.62f;
                    ImU32 g = IM_COL32(120, 220, 120, 255);
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddLine(ImVec2(x, cy), ImVec2(x + u * 0.35f, cy + u * 0.35f), g, fs * 0.11f);
                    dl->AddLine(ImVec2(x + u * 0.35f, cy + u * 0.35f), ImVec2(x + u, cy - u * 0.4f), g, fs * 0.11f);
                };
                std::string src_d = "The folder with your own copy of a PICROSS game for Nintendo Switch (S, S2 to S8), "
                                    "with its latest update: the .nsp files with the prod.keys of your Switch beside them, "
                                    "or the extracted exefs / romfs folders. The game isn't included with PicRecomp.";
                if (!src.empty()) src_d += "\n\nChosen: " + (src.compare(0, 8, "android:") ? src : src.substr(8));
                // the step that's running / its result, under the rows and in the Set up row's explanation
                const std::string &job_line = job;
                std::string ex_d = "Copies the game files PicRecomp needs and recompiles the game from them. This only "
                                   "needs to be done once and may take a while. Your own files aren't changed.";
                if (!job.empty()) ex_d += "\n\n" + job + (job_hint.empty() ? "" : ".\n" + job_hint);

                // the controller's cursor starts on the first row (not the X), a few frames while the window lays out
                static int seen_frame = -10, focus_first;  // (not drawn last frame: the window has just opened)
                if (seen_frame != ImGui::GetFrameCount() - 1) focus_first = 4;
                seen_frame = ImGui::GetFrameCount();
                auto first_row = [&] {
                    if (focus_first > 0 && focus_first--) ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                };
                opt_begin("##files", sc);
                if (!ready) {
                    opt_section("Your copy of the game");
                    if (busy) opt_disable_next();
                    if (opt_action("Game files folder", src_v.c_str(), src_d.c_str(), true))
                        SDL_ShowOpenFolderDialog(picked, NULL, win, NULL, false);
                    check_left_of_value(src_v);
                    first_row();
                    opt_section("Set up");
                    if (busy || (src.empty() && !check_copied_files(g_root).empty())) opt_disable_next();
                    if (opt_action("Set up game files", busy ? "Working..." : "", ex_d.c_str(), true)) {
                        pthread_mutex_lock(&g_lock);
                        g_busy = true;
                        pthread_mutex_unlock(&g_lock);
                        pthread_t t;
                        pthread_create(&t, NULL, extract_thread, NULL);
                        pthread_detach(t);
                    }
                    files_gb = -1;  // (re-measured after a setup)
                } else {
                    // DELETE: frees PicRecomp's copy (runtime/paths.c removes only what the setup made, only inside
                    // the data root). Saves, settings and the user's own files stay.
                    if (files_gb < 0) files_gb = rt_game_files_size(g_root.c_str()) / 1e9;
                    char gb[32];
                    snprintf(gb, sizeof gb, "%.1f GB", files_gb);
                    opt_section("Game files");
                    if (!g_confirm_delete) {
                        if (opt_action("Delete game files", gb, "Frees the space PicRecomp's copy of the game files takes. "
                                       "Saves and settings are kept, and your own game files aren't touched: you can "
                                       "set the game files up again from them.", true))
                            g_confirm_delete = true;
                        first_row();
                    } else {
                        if (opt_action("Yes, delete them", gb, "Deletes the extracted game files now.", true)) {
                            bool ok = rt_remove_game_files(g_root.c_str());
                            status = check_game_files(g_root);
                            if (ok) set_job("Game files deleted.");
                            else set_job("Couldn't delete the game files", true, "They aren't in PicRecomp's data folder, "
                                         "so they were left alone.");
                            if (!status.empty()) g_bg = 0;
                            g_confirm_delete = false, files_gb = -1;
                        }
                        if (opt_action("Cancel", "", "Keeps the game files.", true)) g_confirm_delete = false;
                    }
                }
                if (!job_line.empty()) {  // the current step / result
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(failed ? ImVec4(1, 0.5f, 0.4f, 1) : busy ? ImVec4(0.93f, 0.76f, 0.33f, 1)
                                              : ImVec4(0.45f, 0.95f, 0.55f, 1), "%s", job_line.c_str());
                    if (failed && !job_hint.empty())  // (what to do next, in the rows' own text colour)
                        ImGui::TextColored(ImVec4(0.86f, 0.85f, 0.80f, 1), "%s", job_hint.c_str());
                    ImGui::PopTextWrapPos();
                }
                opt_end();
                if (g_confirm_delete && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                         ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    g_confirm_delete = false;  // (B / Esc: back out of the question first)
                else if (!busy && !typing && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                              ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    back = true;
                ImGui::PopFont();
                ImGui::EndChild();
                if (back && !busy) {
                    settings_save();
                    g_confirm_delete = false;
                    page = MAIN, g_refocus = SETUP;
                }
                static bool test_busy = getenv("PICRECOMP_LAUNCHER_SHOT") && strstr(getenv("PICRECOMP_LAUNCHER_SHOT"), ":extracting");
                if (busy || test_busy) {  // extracting: everything dimmed, a progress bar in front (input waits)
                    ImDrawList *fg = ImGui::GetForegroundDrawList();
                    ImVec2 o = ImGui::GetMainViewport()->Pos;
                    fg->AddRectFilled(o, ImVec2(o.x + ds.x, o.y + ds.y), IM_COL32(0, 0, 0, 170));
                    float f = test_busy ? 0.42f : extract_progress();
                    // (while the game code is built: what's happening, on whose files - runtime/gamebuild.c)
                    bool recompiling = g_build_frac >= 0;
                    const char *title = recompiling ? "Recompiling the game" : "Setting up the game files";
                    static const char *const notes[] = {"This only needs to be done once and may take a while."};
                    int nnotes = recompiling ? 1 : 0;
                    float tfs = g_font_menu->FontSize * 0.75f, sfs = g_font_small->FontSize * 0.85f;
                    ImVec2 ts = g_font_menu->CalcTextSizeA(tfs, FLT_MAX, 0, title);
                    float line_h = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, "A").y;
                    float pw = fminf(ds.x * 0.6f, 900 * sc), bar_y = 36 * sc + ts.y + 30 * sc;
                    for (int i = 0; i < nnotes; i++)  // (wide enough for its text, within the window)
                        pw = fmaxf(pw, g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, notes[i]).x + 96 * sc);
                    pw = fminf(fmaxf(pw, ts.x + 96 * sc), ds.x - 32 * sc);
                    float ph = bar_y + 24 * sc + 26 * sc + line_h + (nnotes ? 14 * sc + nnotes * line_h : 0) + 40 * sc;
                    ImVec2 a(o.x + (ds.x - pw) * 0.5f, o.y + (ds.y - ph) * 0.5f), b(a.x + pw, a.y + ph);
                    fg->AddRectFilled(a, b, IM_COL32(15, 23, 20, 250), 14 * sc);
                    fg->AddRect(a, b, IM_COL32(236, 194, 84, 140), 14 * sc, 0, 1.5f * sc);
                    fg->AddText(g_font_menu, tfs, ImVec2((a.x + b.x - ts.x) * 0.5f, a.y + 36 * sc), IM_COL32(236, 194, 84, 255), title);
                    ImVec2 ba(a.x + 48 * sc, a.y + bar_y), bb(b.x - 48 * sc, a.y + bar_y + 24 * sc);
                    fg->AddRectFilled(ba, bb, IM_COL32(255, 255, 255, 25), 12 * sc);
                    if (f > 0) fg->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * f, bb.y), IM_COL32(236, 194, 84, 255), 12 * sc);
                    char line[200];
                    snprintf(line, sizeof line, "%s   %d%%", test_busy ? "Setting up the game files..." : job.c_str(),
                             (int)(f * 100));
                    ImVec2 ls = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, line);
                    fg->AddText(g_font_small, sfs, ImVec2((a.x + b.x - ls.x) * 0.5f, bb.y + 26 * sc), IM_COL32(220, 216, 205, 255), line);
                    for (int i = 0; i < nnotes; i++) {
                        ImVec2 ns = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, notes[i]);
                        fg->AddText(g_font_small, sfs, ImVec2((a.x + b.x - ns.x) * 0.5f, bb.y + 26 * sc + (i + 1) * line_h + 14 * sc),
                                    IM_COL32(150, 148, 140, 255), notes[i]);
                    }
                }
            }

            // a problem with the chosen game (lower left) and the PicRecomp version (lower right) - not under the Settings / Game Files windows
            if (page == MAIN) {
            ImGui::PushFont(g_font_small);
            ImGui::SetCursorPos(ImVec2(x0, ds.y - 118 * s));
            if (status.empty()) {
                // (nothing: a game that is ready needs no line; its version is in Settings > Game files)
            } else {
                ImGui::TextColored(ImVec4(1, 0.72f, 0.4f, 1), "!  %s", busy ? "Setting up the game files..." : g_cards.empty() ? "No game added" : status.c_str());
                ImGui::SetCursorPosX(x0);
                ImGui::TextDisabled(g_cards.empty() ? "Choose + to add a game." : "This game can't be played yet.");
            }
            if (const char *f = getenv("PICRECOMP_LAUNCHER_FONT")) {  // (test) which typeface this window shows
                const char *b = strrchr(f, '/');
                ImGui::GetWindowDrawList()->AddText(ImVec2(ImGui::GetWindowPos().x + 20 * s, ImGui::GetWindowPos().y + 16 * s),
                                                    IM_COL32(255, 255, 255, 255), b ? b + 1 : f);
            }
            {  // version, and a newer release when the update check found one (lower right). The background is
               // always the title movie's still: bright clouds here, under a shadow banner from the right
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImFont *font = ImGui::GetFont();
                float fs = ImGui::GetFontSize(), right = ImGui::GetWindowPos().x + ds.x - 60 * s;
                auto text = [&](ImVec2 p, float size, ImU32 col, const char *t, bool bold) {
                    for (float d : {2.5f, 1.5f, 0.8f}) dl->AddText(font, size, ImVec2(p.x + d * s, p.y + d * s), IM_COL32(4, 8, 30, 150), t);
                    dl->AddText(font, size, p, col, t);
                    if (bold) dl->AddText(font, size, ImVec2(p.x + 0.7f * s, p.y), col, t);
                };
                std::string ver = tag_label(PICRECOMP_VERSION, true);
                float vs = fs * 1.4f, y = ImGui::GetWindowPos().y + ds.y - 134 * s;
                ImVec2 vsz = font->CalcTextSizeA(vs, FLT_MAX, 0, ver.c_str());
                text(ImVec2(right - vsz.x, y), vs, IM_COL32(255, 255, 255, 255), ver.c_str(), false);
                pthread_mutex_lock(&g_update_lock);
                std::string upd = g_update_tag;
                pthread_mutex_unlock(&g_update_lock);
                if (!upd.empty()) {
                    const char *arrow = font->FindGlyphNoFallback(0x2192) ? " \xe2\x86\x92" : " \xe2\x96\xb8";
                    std::string pre = "Update available!  ", link = "DOWNLOAD " + tag_label(upd, false) + arrow;
                    float wp = font->CalcTextSizeA(fs, FLT_MAX, 0, pre.c_str()).x;
                    ImVec2 lsz = font->CalcTextSizeA(fs, FLT_MAX, 0, link.c_str());
                    ImVec2 lp(right - lsz.x, y + vsz.y + 2 * s);
                    text(ImVec2(lp.x - wp, lp.y), fs, IM_COL32(200, 214, 250, 255), pre.c_str(), false);
                    ImGui::SetCursorScreenPos(lp);
                    bool click = ImGui::InvisibleButton("update", lsz);
                    bool hover = ImGui::IsItemHovered();
                    // the menu's hover colours (yellow to orange), lighter while the link itself is hovered
                    ImU32 col = hover ? IM_COL32(255, 238, 150, 255) : IM_COL32(255, 214, 64, 255);
                    ImU32 col_b = hover ? IM_COL32(255, 196, 110, 255) : IM_COL32(255, 160, 50, 255);
                    text_fx(dl, font, fs, lp, col, col_b, link.c_str(), 1.5f * s);
                    text_fx(dl, font, fs, ImVec2(lp.x + 0.7f * s, lp.y), col, col_b, link.c_str(), 0);  // (faux bold)
                    if (hover) {
                        dl->AddLine(ImVec2(lp.x, lp.y + lsz.y), ImVec2(lp.x + lsz.x, lp.y + lsz.y), col, 1.5f * s);
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    if (click) SDL_OpenURL((RELEASE_PAGE + upd).c_str());
                }
            }
            ImGui::PopFont();
            }
            ImGui::End();
            ImGui::PopStyleColor();
            osk_draw(g_font_menu, s * 0.7f);  // (a text field activated with the controller)
            typed_before = ui_text_busy();

            if (getenv("PICRECOMP_TOUCH_PREVIEW")) touch_preview(), touch_draw();  // (test: the controls' look)
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)vk_frame_cmd());
            bool shot = shot_at && ++frame == shot_at;
            vk_frame_end(shot ? getenv("PICRECOMP_SHOT_PATH") ? getenv("PICRECOMP_SHOT_PATH") : "build/launcher.png" : NULL);
            if (shot) exit(0);
        }
    }
    // play: hand the window to the game
    settings_save();
    vk_imgui_backend_shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prev_ctx);
    g_bg = 0;
    g_start_screen = 0;
}

extern "C" const char *launcher_root(void) { return g_root.c_str(); }  // the card chosen when PLAY was pressed
