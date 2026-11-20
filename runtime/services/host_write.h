// Writing a guest's file to the host: what a successful Write / Flush promises.
#pragma once
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

// All of [data, data + size) at off: interrupted and short writes go on until it is all down. 0, or -1 with errno set (a
// save that reports success with part of it missing is the worst kind).
static inline int host_pwrite_all(int fd, const void *data, int64_t off, int64_t size) {
    const uint8_t *p = (const uint8_t *)data;
    while (size > 0) {
        ssize_t n = pwrite(fd, p, (size_t)size, off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (!n) { errno = EIO; return -1; }
        p += n, off += n, size -= n;
    }
    return 0;
}

static inline int host_fsync(int fd) {
    int r;
    do r = fsync(fd); while (r < 0 && errno == EINTR);
    return r;
}
