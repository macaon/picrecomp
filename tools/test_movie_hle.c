// Exercise the guest ABI adapters without loading a game or native SDK implementation.
#include "../runtime/hle.h"
#undef HLE_FUNC
#define HLE_FUNC(sym, fn) \
    static void fn(Cpu *c); \
    static const HleImplEntry entry_##fn = {sym, fn}; \
    static void fn(Cpu *c)
#undef RT_ON_RESET
#define RT_ON_RESET(fn) static void fn(void)
#include "../runtime/hle/movie.c"

#include <assert.h>

static uint64_t addr(const void *p) { return (uint64_t)(uintptr_t)p; }

static void check_metadata(void) {
    Cpu c = {0};
    uint64_t original = 0, copy = 0, assigned = 0;
    c.x[0] = addr(&original); md_ctor(&c);
    MediaData *src = md(addr(&original));
    int fake_codec;
    src->codecpar = &fake_codec;
    md_set_str(src, "mime", "video/avc");
    md_set_int(src, "width", 580);
    md_set_int(src, "durationUs", INT64_C(0x123456789ab));
    c.x[0] = addr(&copy); c.x[1] = addr(&original); md_copy(&c);
    MediaData *cloned = md(addr(&copy));
    assert(cloned != src && cloned->codecpar == &fake_codec && cloned->n == src->n);
    assert(cloned->e[0].key != src->e[0].key && cloned->e[0].sval != src->e[0].sval);
    c.x[0] = addr(&assigned); md_ctor(&c);
    md_set_str(md(addr(&assigned)), "old", "replaced");
    c.x[0] = addr(&assigned); c.x[1] = addr(&copy); md_assign(&c);
    assert(c.x[0] == addr(&assigned));
    MediaData *dst = md(addr(&assigned));
    assert(dst != cloned && dst->codecpar == &fake_codec && dst->n == 3);
    assert(dst->e[0].key != cloned->e[0].key && dst->e[0].sval != cloned->e[0].sval);
    char *saved_key = dst->e[0].key, *saved_value = dst->e[0].sval;
    c.x[0] = addr(&assigned); c.x[1] = addr(&assigned); md_assign(&c);
    assert(dst->e[0].key == saved_key && dst->e[0].sval == saved_value && dst->codecpar == &fake_codec);
    c.x[0] = addr(&original); md_clear_hle(&c);
    c.x[0] = addr(&copy); md_dtor(&c);
    assert(!copy && src->n == 0 && !src->codecpar);
    uint64_t found_string = 0;
    c.x[0] = addr(&assigned); c.x[1] = addr("mime"); c.x[2] = addr(&found_string); md_find_string(&c);
    assert(c.x[0] == 1 && !strcmp((const char *)(uintptr_t)found_string, "video/avc"));
    int32_t width = -1;
    c.x[0] = addr(&assigned); c.x[1] = addr("width"); c.x[2] = addr(&width); md_find_int32(&c);
    assert(c.x[0] == 1 && width == 580);
    int64_t duration = -1;
    c.x[0] = addr(&assigned); c.x[1] = addr("durationUs"); c.x[2] = addr(&duration); md_find_int64(&c);
    assert(c.x[0] == 1 && duration == INT64_C(0x123456789ab));
    c.x[0] = addr(&assigned); c.x[1] = addr("missing"); md_find_int64(&c);
    assert(c.x[0] == 0 && duration == INT64_C(0x123456789ab));
    c.x[0] = addr(&assigned); c.x[1] = addr("mime"); md_find_int64(&c);
    assert(c.x[0] == 0);
    int32_t rect[4] = {1, 2, 3, 4};
    c.x[0] = addr(&assigned); c.x[1] = addr("crop");
    for (int i = 0; i < 4; i++) c.x[i + 2] = addr(&rect[i]);
    md_find_rect(&c);
    assert(!c.x[0] && rect[0] == 1 && rect[3] == 4);
    c.x[0] = addr(&assigned); md_dtor(&c);
    c.x[0] = addr(&original); md_dtor(&c);
    assert(!original && !assigned);
    assert(!strcmp(entry_md_assign.name, "_ZN5movie9MediaDataaSERKS0_"));
    assert(!strcmp(entry_md_find_int64.name, "_ZNK5movie9MediaData9FindInt64EPKcPl"));
    assert(!strcmp(entry_md_find_rect.name, "_ZNK5movie9MediaData8FindRectEPKcPiS3_S3_S3_"));
    assert(!strcmp(entry_movie_ex_track_config.name, "_ZN5movie9Extractor21GetTrackConfigurationEiPNS_9MediaDataE"));
    assert(!strcmp(entry_movie_ex_track_count.name, "_ZN5movie9Extractor13GetTrackCountEPi"));
    assert(!strcmp(entry_movie_ex_source_path.name, "_ZN5movie9Extractor13SetDataSourceEPKc"));
    assert(!strcmp(entry_movie_ex_dtor.name, "_ZN5movie9ExtractorD1Ev"));
}

static void check_buffer(void) {
    Cpu c = {0};
    uint8_t bytes[64] = {0};
    MBuffer b = {0};
    c.x[0] = addr(&b); c.x[1] = addr(bytes); c.x[2] = sizeof bytes; buf_ctor_ext(&c);
    c.x[0] = addr(&b); c.x[1] = 7; c.x[2] = 11; buf_set_range(&c);
    c.x[0] = addr(&b); buf_data(&c); assert(c.x[0] == addr(bytes + 7));
    c.x[0] = addr(&b); buf_base(&c); assert(c.x[0] == addr(bytes));
    c.x[0] = addr(&b); buf_size(&c); assert(c.x[0] == 11);
    c.x[0] = addr(&b); buf_offset(&c); assert(c.x[0] == 7);
    assert(!strcmp(entry_buf_data.name, "_ZNK5movie6Buffer4DataEv"));
}

static const uint8_t fake_bytes[] = {1, 3, 5, 7, 9};
static int fake_reads, fake_closes, fake_read_result = 3;
static uint64_t fake_reader_address;

// Dispatch a synthetic guest vtable and deliberately clobber all CPU state, as a real guest call may do.
void guest_call(Cpu *c, uint64_t function) {
    assert(c->x[0] == fake_reader_address);
    uint64_t result = 1;
    switch (function) {
    case 102:
        assert(!strcmp((const char *)(uintptr_t)c->x[1], "archive:/synthetic.resource"));
        *(uint64_t *)(uintptr_t)c->x[2] = 0xabc;
        break;
    case 103: {
        assert(c->x[1] == 0xabc && c->x[2] == 1 && c->x[4] == 3);
        if (fake_read_result > 0 && fake_read_result <= 3)
            memcpy((void *)(uintptr_t)c->x[3], fake_bytes + c->x[2], (size_t)fake_read_result);
        fake_reads++;
        result = (uint64_t)fake_read_result;
        break;
    }
    case 104: assert(c->x[1] == 0xabc); fake_closes++; break;
    case 105:
        assert(c->x[1] == 0xabc);
        *(int64_t *)(uintptr_t)c->x[2] = sizeof fake_bytes;
        break;
    default: assert(0);
    }
    memset(c, 0xa5, sizeof *c);
    c->x[0] = result;
}

static void check_reader(void) {
    Cpu cpu, saved;
    memset(&cpu, 0x36, sizeof cpu);
    saved = cpu;
    uint64_t vtable[6] = {100, 101, 102, 103, 104, 105};
    uint64_t reader = addr(vtable);
    uint8_t scratch[MOVIE_IO_SIZE + 16] = {0};
    Extractor e = {.io_cpu = &cpu, .reader = addr(&reader), .reader_scratch = scratch};
    fake_reader_address = addr(&reader);
    assert(ex_reader_call(&e, 2, addr("archive:/synthetic.resource"), addr(scratch), 0, 0) == 1);
    e.reader_handle = *(uint64_t *)scratch;
    assert(e.reader_handle == 0xabc && !memcmp(&cpu, &saved, sizeof cpu));
    ex_reader_call(&e, 5, e.reader_handle, addr(scratch + 8), 0, 0);
    assert(*(int64_t *)(scratch + 8) == sizeof fake_bytes && !memcmp(&cpu, &saved, sizeof cpu));
    uint8_t dst[3] = {0};
    assert(ex_reader_read(&e, 1, dst, 3) == 3 && !memcmp(dst, fake_bytes + 1, 3));
    assert(!memcmp(&cpu, &saved, sizeof cpu));
    fake_read_result = -1;
    assert(ex_reader_read(&e, 1, dst, 3) == AVERROR(EIO));
    fake_read_result = 4;
    assert(ex_reader_read(&e, 1, dst, 3) == AVERROR(EIO));
    fake_read_result = 0;
    assert(ex_reader_read(&e, 1, dst, 3) == 0);
    assert(fake_reads == 4);
    ex_reader_call(&e, 4, e.reader_handle, 0, 0, 0);
    assert(fake_closes == 1 && !memcmp(&cpu, &saved, sizeof cpu));
}

// The included movie implementation also has a texture-output path; these tests exercise CPU buffers only.
uint64_t renderer_present_count(void) {
    assert(!"CPU-output regression test must not query the renderer");
    return 0;
}

void renderer_upload_rgba(NvnTexture *texture, const void *rgba, uint32_t width, uint32_t height, uint32_t stride) {
    (void)texture; (void)rgba; (void)width; (void)height; (void)stride;
    assert(!"CPU-output regression test must not upload a texture");
}

static void check_output_buffer_with_format(void) {
    Decoder decoder = {.sample_rate = 48000, .channels = 2, .out_cap = 32};
    uint8_t bytes[32] = {1, 2, 3};
    decoder.out_mem[1] = bytes;
    decoder.out_size[1] = 3;
    decoder.out_busy[1] = true;
    uint64_t object[2] = {0, addr(&decoder)}, metadata = 0;
    MBuffer buffer = {0};
    Cpu c = {0};
    c.x[0] = addr(object); c.x[1] = 1; c.x[2] = addr(&buffer); c.x[3] = addr(&metadata);
    dec_get_output_buffer_with_format(&c);
    assert(c.x[0] == 0 && buffer.base == addr(bytes) && buffer.size == 3 && buffer.capacity == 32);
    assert(decoder.out_busy[1]);  // The caller releases separately, after consuming the data.
    int32_t sample_rate = 0;
    c.x[0] = addr(&metadata); c.x[1] = addr("sample-rate"); c.x[2] = addr(&sample_rate);
    md_find_int32(&c);
    assert(c.x[0] == 1 && sample_rate == 48000);
    decoder.video = true;
    decoder.width = 580; decoder.height = 326; decoder.stride = 640;
    c.x[0] = addr(object); c.x[1] = 1; c.x[2] = addr(&buffer); c.x[3] = addr(&metadata);
    dec_get_output_buffer_with_format(&c);
    assert(!c.x[0] && decoder.out_busy[1]);
    int32_t width = 0;
    c.x[0] = addr(&metadata); c.x[1] = addr("width"); c.x[2] = addr(&width);
    md_find_int32(&c);
    assert(c.x[0] == 1 && width == 580);
    c.x[0] = addr(object); c.x[1] = NOUT; c.x[2] = addr(&buffer); c.x[3] = addr(&metadata);
    dec_get_output_buffer_with_format(&c);
    assert(c.x[0] != 0 && decoder.out_busy[1]);
    c.x[0] = addr(&metadata); md_dtor(&c);
}

static int32_t metadata_int(uint64_t *metadata, const char *key) {
    Cpu c = {0};
    int32_t value = -1;
    c.x[0] = addr(metadata); c.x[1] = addr(key); c.x[2] = addr(&value);
    md_find_int32(&c);
    assert(c.x[0] == 1);
    return value;
}

static void check_nv12_client_copy(int width, int height) {
    int stride = (width + 1) & ~1, slice = (height + 1) & ~1;
    size_t size = (size_t)stride * slice * 3 / 2;
    uint8_t *output = malloc(size + 16), *client = malloc(size + 16), *expected = malloc(size);
    assert(output && client && expected);
    memset(output, 0xcc, size + 16); memset(expected, 0xcc, size); memset(client, 0xee, size + 16);
    AVFrame *frame = av_frame_alloc();
    assert(frame);
    frame->width = width; frame->height = height; frame->format = AV_PIX_FMT_YUV420P;
    assert(av_frame_get_buffer(frame, 32) >= 0);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            expected[y * stride + x] = frame->data[0][y * frame->linesize[0] + x] = (uint8_t)(16 + (x + y) % 220);
    for (int y = 0; y < (height + 1) / 2; y++)
        for (int x = 0; x < (width + 1) / 2; x++) {
            uint8_t u = (uint8_t)(40 + x % 80), v = (uint8_t)(140 + y % 80);
            frame->data[1][y * frame->linesize[1] + x] = u;
            frame->data[2][y * frame->linesize[2] + x] = v;
            expected[stride * slice + y * stride + 2 * x] = u;
            expected[stride * slice + y * stride + 2 * x + 1] = v;
        }
    Decoder decoder = {.video = true, .width = width, .height = height, .stride = stride, .out_cap = (uint32_t)size};
    decoder.out_mem[0] = output; decoder.out_mem[1] = output;
    assert(emit_frame(&decoder, frame, 123456, 0));
    assert(decoder.out_size[0] == size && !memcmp(output, expected, size));
    for (size_t i = size; i < size + 16; i++) assert(output[i] == 0xcc);
    uint64_t object[2] = {0, addr(&decoder)}, metadata = 0;
    MBuffer buffer = {.base = addr(client), .capacity = size, .offset = 3};
    Cpu c = {0};
    c.x[0] = addr(object); c.x[1] = 0; c.x[2] = addr(&buffer); c.x[3] = addr(&metadata);
    dec_get_output_buffer_with_format(&c);
    assert(!c.x[0] && buffer.base == addr(client) && buffer.capacity == size && buffer.offset == 0 && buffer.size == (int)size);
    assert(!memcmp(client, expected, size) && decoder.out_busy[0]);
    for (size_t i = size; i < size + 16; i++) assert(client[i] == 0xee);
    assert(metadata_int(&metadata, "width") == width && metadata_int(&metadata, "height") == height);
    assert(metadata_int(&metadata, "slice-height") == slice && metadata_int(&metadata, "nv12-y-stride") == stride);
    assert(metadata_int(&metadata, "nv12-y-offset") == 0 && metadata_int(&metadata, "nv12-uv-offset") == stride * slice);
    memset(client, 0xee, size);
    buffer.capacity = size - 1;
    c.x[0] = addr(object); c.x[1] = 0; c.x[2] = addr(&buffer); c.x[3] = addr(&metadata);
    dec_get_output_buffer_with_format(&c);
    assert(c.x[0] != 0 && buffer.base == addr(client));
    for (size_t i = 0; i < size; i++) assert(client[i] == 0xee);
    assert(emit_frame(&decoder, NULL, 234567, MOVIE_EOS));
    assert(decoder.out_size[1] == 0);
    buffer.capacity = size;
    c.x[0] = addr(object); c.x[1] = 1; c.x[2] = addr(&buffer); c.x[3] = addr(&metadata);
    dec_get_output_buffer_with_format(&c);
    assert(!c.x[0] && buffer.size == 0 && buffer.base == addr(client) && decoder.out_busy[1]);
    for (size_t i = 0; i < size; i++) assert(client[i] == 0xee);
    c.x[0] = addr(&metadata); md_dtor(&c);
    av_frame_free(&frame);
    free(output); free(client); free(expected);
}

int main(void) {
    check_metadata();
    check_buffer();
    check_reader();
    check_output_buffer_with_format();
    check_nv12_client_copy(580, 326);
    check_nv12_client_copy(3, 3);
    puts("movie HLE: metadata, guest reader callbacks and packed NV12 client output passed");
    return 0;
}
