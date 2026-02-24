#include "rt/rt.h"
#include "rt/syscall.h"

void rt_exit(int code) {
  rt_syscall1(RT_NR_EXIT, code);
  __builtin_unreachable();
}

long rt_openat(int dirfd, const char* path, int flags, int mode) {
  return rt_syscall4(RT_NR_OPENAT, dirfd, (long)path, flags, mode);
}

long rt_read(int fd, void* buf, usize count) {
  return rt_syscall3(RT_NR_READ, fd, (long)buf, (long)count);
}

long rt_write(int fd, const void* buf, usize count) {
  return rt_syscall3(RT_NR_WRITE, fd, (long)buf, (long)count);
}

long rt_close(int fd) {
  return rt_syscall1(RT_NR_CLOSE, fd);
}
