// Per-title shader policy and the async queues, without game data or a GPU.
#include "../runtime/nvn/vulkan/exec.c"

#include <assert.h>
#include <unistd.h>

static char fixture[1024];
void rt_fatal_trace(void) { abort(); }

bool rt_resource(const char *name, const char *dev, char *out, size_t size) {
    (void)dev;
    snprintf(out, size, "%s/%s", fixture, name);
    return access(out, R_OK) == 0;
}

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f && fputs(text, f) >= 0 && !fclose(f));
}

static void load_title(const char *text) {
    char path[1200];
    snprintf(path, sizeof path, "%s/title.json", fixture);
    write_text(path, text);
    settings_game_languages(fixture);
}

static atomic_int compiled;
static pthread_t caller;
static void compile_job(void *arg) {
    assert(!pthread_equal(pthread_self(), caller));
    atomic_fetch_add(&compiled, (int)(intptr_t)arg);
}
static void translate_job(void *arg) { run_job(compile_job, arg); }
static void immediate_job(void *arg) {
    (void)arg;
    assert(pthread_equal(pthread_self(), caller));
    atomic_fetch_add(&compiled, 1);
}

int main(void) {
    char temp[] = "/tmp/picrecomp-policy-XXXXXX";
    assert(mkdtemp(temp));
    snprintf(fixture, sizeof fixture, "%s", temp);
    char path[1200];
    snprintf(path, sizeof path, "%s/titles", fixture);
    assert(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/titles/test-piczle.json", fixture);
    write_text(path, "{\"async_shaders\": true}");
    unsetenv("PICRECOMP_ASYNC_SHADERS");
    caller = pthread_self();
    load_title("{\"slug\":\"test-picross\"}");
    assert(!settings_async_shaders() && sync_shaders());
    run_translate(immediate_job, NULL);
    run_job(immediate_job, NULL);
    assert(atomic_load(&compiled) == 2);

    // An older install gets new policy fields from its shipped manifest; translation spawns compile jobs.
    load_title("{\"slug\":\"test-piczle\"}");
    assert(settings_async_shaders() && !sync_shaders());
    for (int i = 0; i < 1000; i++) run_translate(translate_job, (void *)(intptr_t)1);
    for (int i = 0; i < 1000; i++) run_job(compile_job, (void *)(intptr_t)1);
    vk_shader_jobs_wait();
    assert(atomic_load(&compiled) == 2002);
    assert(!jobs_pending && !tjobs_pending);

    // Both switching directions must change the policy in this process, with the old queues fully drained.
    load_title("{\"async_shaders\":false}");
    assert(sync_shaders());
    run_job(immediate_job, NULL);
    assert(atomic_load(&compiled) == 2003);
    load_title("{\"async_shaders\":true}");
    assert(!sync_shaders());
    run_translate(translate_job, (void *)(intptr_t)1);
    vk_shader_jobs_wait();
    assert(atomic_load(&compiled) == 2004);
    setenv("PICRECOMP_ASYNC_SHADERS", "0", 1);
    assert(sync_shaders());
    setenv("PICRECOMP_ASYNC_SHADERS", "1", 1);
    load_title("{}");
    assert(!sync_shaders());
    unsetenv("PICRECOMP_ASYNC_SHADERS");
    assert(sync_shaders());
    load_title("{\"slug\":\"test-piczle\",\"async_shaders\":false}");
    assert(sync_shaders());  // An explicit installed field overrides the manifest.
    load_title("{\"async_shaders\":trueish}");
    assert(sync_shaders());
    load_title("{\"slug\":\"../test-piczle\"}");
    assert(sync_shaders());
    settings_game_languages("/missing/title");
    assert(sync_shaders());
    snprintf(path, sizeof path, "%s/title.json", fixture); assert(!unlink(path));
    snprintf(path, sizeof path, "%s/titles/test-piczle.json", fixture); assert(!unlink(path));
    snprintf(path, sizeof path, "%s/titles", fixture); assert(!rmdir(path));
    assert(!rmdir(fixture));
    puts("shader policy: title switches, overrides, manifest fallback and async teardown passed");
}
