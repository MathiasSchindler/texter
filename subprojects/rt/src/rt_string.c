#include "rt/rt.h"

usize rt_strlen(const char* s) {
  usize n = 0;
  while (s[n] != '\0') {
    n++;
  }
  return n;
}

long rt_write_all(int fd, const void* buf, usize count) {
  const char* p = (const char*)buf;
  usize done = 0;
  while (done < count) {
    long n = rt_write(fd, p + done, count - done);
    if (n < 0) {
      return n;
    }
    if (n == 0) {
      break;
    }
    done += (usize)n;
  }
  return (long)done;
}

usize rt_u64_to_dec(u64 value, char* out, usize out_cap) {
  char tmp[32];
  usize i = 0;
  usize j;

  if (out_cap == 0) {
    return 0;
  }

  if (value == 0) {
    out[0] = '0';
    return 1;
  }

  while (value != 0 && i < sizeof(tmp)) {
    tmp[i++] = (char)('0' + (value % 10));
    value /= 10;
  }

  if (i > out_cap) {
    i = out_cap;
  }

  for (j = 0; j < i; j++) {
    out[j] = tmp[i - 1 - j];
  }

  return i;
}
