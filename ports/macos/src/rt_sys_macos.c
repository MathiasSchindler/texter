#include "rt/rt.h"

#include <fcntl.h>
#include <unistd.h>

void rt_exit(int code) {
  _exit(code);
}

long rt_openat(int dirfd, const char* path, int flags, int mode) {
  int native_dirfd = (dirfd == RT_AT_FDCWD) ? AT_FDCWD : dirfd;
  int native_flags = 0;

  if ((flags & RT_O_RDWR) == RT_O_RDWR) {
    native_flags |= O_RDWR;
  } else if ((flags & RT_O_WRONLY) != 0) {
    native_flags |= O_WRONLY;
  } else {
    native_flags |= O_RDONLY;
  }

  if ((flags & RT_O_CREAT) != 0) {
    native_flags |= O_CREAT;
  }
  if ((flags & RT_O_TRUNC) != 0) {
    native_flags |= O_TRUNC;
  }

  return (long)openat(native_dirfd, path, native_flags, mode);
}

long rt_read(int fd, void* buf, usize count) {
  return (long)read(fd, buf, count);
}

long rt_write(int fd, const void* buf, usize count) {
  return (long)write(fd, buf, count);
}

long rt_close(int fd) {
  return (long)close(fd);
}
