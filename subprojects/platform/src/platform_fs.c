#include "platform/platform.h"

#include "rt/rt.h"

int platform_write_stdout(const char* text) {
  usize len = rt_strlen(text);
  return rt_write_all(1, text, len) < 0 ? -1 : 0;
}

int platform_probe_read_file(const char* path, usize* out_bytes_read) {
  u8 buffer[256];
  long fd = rt_openat(RT_AT_FDCWD, path, RT_O_RDONLY, 0);
  long n;

  if (fd < 0) {
    return -1;
  }

  n = rt_read((int)fd, buffer, sizeof(buffer));
  rt_close((int)fd);

  if (n < 0) {
    return -2;
  }

  *out_bytes_read = (usize)n;
  return 0;
}
