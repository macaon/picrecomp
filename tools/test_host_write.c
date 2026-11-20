// runtime/services/host_write.h: a Write that is only partly done is an error, not a success. A file-size limit makes the
// host accept part of a request and then refuse the rest, as a full disk does.
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "../runtime/services/host_write.h"

int main(void) {
    char path[] = "build/test/hostwriteXXXXXX";  // (a test file only; run from the repository root)
    int fd = mkstemp(path);
    if (fd < 0) return 2;
    unlink(path);
    int fails = 0;
    const char data[16] = "0123456789abcdef";
    if (host_pwrite_all(fd, data, 0, 16) != 0) { puts("FAIL: a plain write"); fails++; }
    char back[16];
    if (pread(fd, back, 16, 0) != 16 || memcmp(back, data, 16)) { puts("FAIL: the written bytes"); fails++; }
    if (host_pwrite_all(fd, data, 4, 0) != 0 || host_pwrite_all(fd, data, 0, -1) != 0) { puts("FAIL: an empty write"); fails++; }
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old, lim;
    getrlimit(RLIMIT_FSIZE, &old);
    lim = old;
    lim.rlim_cur = 4;  // (the soft limit only: it is put back before anything is printed, stdout may be a file)
    setrlimit(RLIMIT_FSIZE, &lim);
    errno = 0;
    int r = host_pwrite_all(fd, data, 100, 8);  // (a first part may go in, the rest cannot)
    int errno_after = errno;
    int sync_failed = host_fsync(fd) != 0;
    setrlimit(RLIMIT_FSIZE, &old);
    if (r == 0) { puts("FAIL: a write the host could not finish reported success"); fails++; }
    else if (errno_after != EFBIG && errno_after != ENOSPC && errno_after != EIO) { printf("FAIL: errno %d\n", errno_after); fails++; }
    if (sync_failed) { puts("FAIL: fsync"); fails++; }
    puts(fails ? "host_write: FAILED" : "host_write: ok");
    return fails != 0;
}
