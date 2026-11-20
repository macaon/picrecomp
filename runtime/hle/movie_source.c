#include "movie_source.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>

static int source_read(void *opaque, uint8_t *dst, int count) {
    MovieSource *s = opaque;
    if (count <= 0 || s->pos < 0 || s->pos > s->size) return AVERROR(EINVAL);
    if (s->pos == s->size) return AVERROR_EOF;
    if (s->size - s->pos < count) count = (int)(s->size - s->pos);
    int64_t n = s->read_at(s->opaque, s->pos, dst, count);
    if (n < 0) return n >= INT_MIN ? (int)n : AVERROR(EIO);
    if (!n) return AVERROR_EOF;
    if (n > count) return AVERROR(EIO);
    s->pos += n;
    return (int)n;
}

static int64_t source_seek(void *opaque, int64_t offset, int whence) {
    MovieSource *s = opaque;
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE) return s->size;
    int64_t base;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = s->pos; break;
    case SEEK_END: base = s->size; break;
    default: return AVERROR(EINVAL);
    }
    if (base < 0 || base > s->size || offset < -base || offset > INT64_MAX - base) return AVERROR(EINVAL);
    int64_t next = base + offset;
    if (next > s->size) return AVERROR(EINVAL);
    s->pos = next;
    return next;
}

AVIOContext *movie_source_io(MovieSource *source) {
    if (!source || !source->read_at || source->size < 0 || source->pos < 0 || source->pos > source->size) return NULL;
    uint8_t *buffer = av_malloc(32768);
    if (!buffer) return NULL;
    AVIOContext *io = avio_alloc_context(buffer, 32768, 0, source, source_read, NULL, source_seek);
    if (!io) av_free(buffer);
    return io;
}
