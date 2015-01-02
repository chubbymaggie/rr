/* -*- Mode: C; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil; -*- */

#ifndef RRUTIL_H
#define RRUTIL_H

#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 2

#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/ethtool.h>
#include <linux/futex.h>
#include <linux/if.h>
#include <linux/limits.h>
#include <linux/perf_event.h>
#include <linux/sockios.h>
#include <linux/videodev2.h>
#include <linux/wireless.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/xattr.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/quota.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <x86intrin.h>

#include <rr/rr.h>

typedef unsigned char uint8_t;

#define ALEN(_a) (sizeof(_a) / sizeof(_a[0]))

#define test_assert(cond) assert("FAILED if not: " && (cond))

#define check_syscall(expected, expr)                                          \
  do {                                                                         \
    int __result = (expr);                                                     \
    if ((expected) != __result) {                                              \
      atomic_printf("syscall failed: got %d, expected %d, errno %d", __result, \
                    (expected), errno);                                        \
      test_assert(0);                                                          \
    }                                                                          \
  } while (0)

#if (defined(__linux__) && (defined(__i386__) || defined(__x86_64__)) &&       \
     defined(_BITS_PTHREADTYPES_H))
#define PTHREAD_SPINLOCK_INITIALIZER (1)
#else
#error "Sorry, pthread_spinlock_t initializer unknown for this arch."
#endif

static pthread_spinlock_t printf_lock = PTHREAD_SPINLOCK_INITIALIZER;

/**
 * Print the printf-like arguments to stdout as atomic-ly as we can
 * manage.  Async-signal-safe.  Does not flush stdio buffers (doing so
 * isn't signal safe).
 */
__attribute__((format(printf, 1,
                      2))) inline static int atomic_printf(const char* fmt,
                                                           ...) {
  va_list args;
  char buf[1024];
  int len;
  ssize_t ret;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
  va_end(args);
  {
    /* NBB: this spin lock isn't strictly signal-safe.
     * However, we're trading one class of fairly frequent
     * spurious failures with stdio for what (should!) be
     * a less frequent class of failures with this
     * non-reentrant spinlock.
     *
     * If your test mysteriously hangs with 100% CPU
     * usage, this is a potential suspect.
     *
     * TODO: it's possible to fix this bug, but not
     * trivial.  Play it by ear. */
    pthread_spin_lock(&printf_lock);
    ret = write(STDOUT_FILENO, buf, len);
    pthread_spin_unlock(&printf_lock);
  }
  return ret;
}

/**
 * Write |str| on its own line to stdout as atomic-ly as we can
 * manage.  Async-signal-safe.  Does not flush stdio buffers (doing so
 * isn't signal safe).
 */
inline static int atomic_puts(const char* str) {
  return atomic_printf("%s\n", str);
}

#define fprintf(...) USE_dont_write_stderr
#define printf(...) USE_atomic_printf_INSTEAD
#define puts(...) USE_atomic_puts_INSTEAD

/**
 * Return the calling task's id.
 */
inline static pid_t sys_gettid(void) { return syscall(SYS_gettid); }

/**
 * Ensure that |len| bytes of |buf| are the same across recording and
 * replay.
 */
inline static void check_data(void* buf, size_t len) {
  syscall(SYS_write, RR_MAGIC_SAVE_DATA_FD, buf, len);
  atomic_printf("Wrote %zu bytes to magic fd\n", len);
}

/**
 * Return the current value of the time-stamp counter.
 */
inline static uint64_t rdtsc(void) { return __rdtsc(); }

static uint64_t GUARD_VALUE = 0xdeadbeeff00dbaad;

/**
 * Allocate 'size' bytes, fill with 'value', and place canary values before
 * and after the allocated block.
 */
inline static void* allocate_guard(size_t size, char value) {
  char* cp =
      (char*)malloc(size + 2 * sizeof(GUARD_VALUE)) + sizeof(GUARD_VALUE);
  memcpy(cp - sizeof(GUARD_VALUE), &GUARD_VALUE, sizeof(GUARD_VALUE));
  memcpy(cp + size, &GUARD_VALUE, sizeof(GUARD_VALUE));
  memset(cp, value, size);
  return cp;
}

/**
 * Verify that canary values before and after the block allocated at 'p'
 * (of size 'size') are still valid.
 */
inline static void verify_guard(size_t size, void* p) {
  char* cp = (char*)p;
  test_assert(
      memcmp(cp - sizeof(GUARD_VALUE), &GUARD_VALUE, sizeof(GUARD_VALUE)) == 0);
  test_assert(memcmp(cp + size, &GUARD_VALUE, sizeof(GUARD_VALUE)) == 0);
}

/**
 * Verify that canary values before and after the block allocated at 'p'
 * (of size 'size') are still valid, and free the block.
 */
inline static void free_guard(size_t size, void* p) {
  verify_guard(size, p);
  free((char*)p - sizeof(GUARD_VALUE));
}

#define ALLOCATE_GUARD(p, v) p = allocate_guard(sizeof(*p), v)
#define VERIFY_GUARD(p) verify_guard(sizeof(*p), p)
#define FREE_GUARD(p) free_guard(sizeof(*p), p)

#endif /* RRUTIL_H */
