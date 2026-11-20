// FFmpeg demux/decode through the same seekable callback adapter used for Unity's bundled movies.
#include "../runtime/hle/movie_source.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>

typedef struct { const uint8_t *data; int64_t size; int reads; int short_read; int error; } Memory;

static int64_t memory_read(void *opaque, int64_t offset, uint8_t *dst, int count) {
    Memory *m = opaque;
    m->reads++;
    if (m->error) return m->error;
    assert(offset >= 0 && offset <= m->size && count <= m->size - offset);
    if (m->short_read && count > m->short_read) count = m->short_read;
    memcpy(dst, m->data + offset, (size_t)count);
    return count;
}

static void free_io(AVIOContext **io) {
    av_freep(&(*io)->buffer);
    avio_context_free(io);
}

static void check_callbacks(void) {
    const uint8_t data[] = {10, 20, 30, 40, 50};
    Memory m = {.data = data, .size = sizeof data, .short_read = 2};
    MovieSource source = {&m, sizeof data, 0, memory_read};
    AVIOContext *io = movie_source_io(&source);
    assert(io);
    uint8_t dst[16] = {0};
    assert(io->read_packet(io->opaque, dst, sizeof dst) == 2);
    assert(source.pos == 2 && !memcmp(dst, data, 2));
    assert(io->seek(io->opaque, 0, AVSEEK_SIZE) == 5 && source.pos == 2);
    assert(io->seek(io->opaque, -1, SEEK_CUR) == 1);
    assert(io->seek(io->opaque, -1, SEEK_END | AVSEEK_FORCE) == 4);
    assert(io->read_packet(io->opaque, dst, sizeof dst) == 1 && dst[0] == 50);
    assert(io->read_packet(io->opaque, dst, sizeof dst) == AVERROR_EOF);
    assert(source.pos == 5 && m.reads == 2);
    assert(io->seek(io->opaque, -1, SEEK_SET) < 0);
    assert(io->seek(io->opaque, 1, SEEK_END) < 0);
    assert(io->seek(io->opaque, INT64_MAX, SEEK_CUR) < 0);
    assert(io->seek(io->opaque, INT64_MIN, SEEK_CUR) < 0);
    assert(io->seek(io->opaque, 0, 12345) < 0 && source.pos == 5);
    assert(io->seek(io->opaque, 0, SEEK_SET) == 0);
    m.error = AVERROR(EIO);
    assert(io->read_packet(io->opaque, dst, sizeof dst) == AVERROR(EIO) && source.pos == 0);
    m.error = 0;
    assert(io->read_packet(io->opaque, dst, 0) < 0);
    free_io(&io);
    source.size = 0;
    io = movie_source_io(&source);
    assert(io && io->read_packet(io->opaque, dst, 1) == AVERROR_EOF);
    free_io(&io);
    source.size = -1;
    assert(!movie_source_io(&source));
    assert(!movie_source_io(NULL));
}

static int receive_frames(AVCodecContext *codec, AVFrame *frame) {
    int n = 0, ret;
    while ((ret = avcodec_receive_frame(codec, frame)) >= 0) {
        assert(frame->width == 64 && frame->height == 48);
        // The synthetic test pattern is not a black frame.
        int lo = 255, hi = 0;
        for (int y = 0; y < frame->height; y++)
            for (int x = 0; x < frame->width; x++) {
                int v = frame->data[0][y * frame->linesize[0] + x];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        assert(hi - lo > 100);
        n++;
        av_frame_unref(frame);
    }
    assert(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
    return n;
}

static int decode_all(AVFormatContext *fmt, AVCodecContext *codec, int stream) {
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    assert(packet && frame);
    int count = 0, ret;
    while ((ret = av_read_frame(fmt, packet)) >= 0) {
        if (packet->stream_index == stream) {
            assert(avcodec_send_packet(codec, packet) >= 0);
            count += receive_frames(codec, frame);
        }
        av_packet_unref(packet);
    }
    assert(ret == AVERROR_EOF);
    assert(avcodec_send_packet(codec, NULL) >= 0);
    count += receive_frames(codec, frame);
    av_packet_free(&packet);
    av_frame_free(&frame);
    return count;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    check_callbacks();
    FILE *file = fopen(argv[1], "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size > 0 && !fseek(file, 0, SEEK_SET));
    uint8_t *data = malloc((size_t)size);
    assert(data && fread(data, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    Memory memory = {.data = data, .size = size, .short_read = 97};
    MovieSource source = {&memory, size, 0, memory_read};
    AVIOContext *io = movie_source_io(&source);
    AVFormatContext *fmt = avformat_alloc_context();
    assert(io && fmt);
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    assert(avformat_open_input(&fmt, NULL, NULL, NULL) >= 0);
    assert(avformat_find_stream_info(fmt, NULL) >= 0);
    int stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    assert(stream >= 0);
    const AVCodec *decoder = avcodec_find_decoder(fmt->streams[stream]->codecpar->codec_id);
    AVCodecContext *codec = avcodec_alloc_context3(decoder);
    assert(codec && avcodec_parameters_to_context(codec, fmt->streams[stream]->codecpar) >= 0);
    assert(avcodec_open2(codec, decoder, NULL) >= 0);
    int frames = decode_all(fmt, codec, stream);
    assert(frames == 12 && memory.reads > 1);
    assert(av_seek_frame(fmt, stream, 0, AVSEEK_FLAG_BACKWARD) >= 0);
    avcodec_flush_buffers(codec);
    assert(decode_all(fmt, codec, stream) == frames);
    avcodec_free_context(&codec);
    avformat_close_input(&fmt);
    free_io(&io);
    free(data);
    puts("movie source: callbacks, bounds, MP4 decode and seek passed");
    return 0;
}
