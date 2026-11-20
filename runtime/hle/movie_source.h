#pragma once

#include <stdint.h>
#include <libavformat/avio.h>

// A seekable guest stream. The caller keeps the source and opaque state alive until the AVIO context is freed.
typedef struct MovieSource {
    void *opaque;
    int64_t size, pos;
    int64_t (*read_at)(void *, int64_t, uint8_t *, int);
} MovieSource;

// Owns only its av_malloc buffer: free io->buffer with av_freep, then call avio_context_free.
AVIOContext *movie_source_io(MovieSource *source);
