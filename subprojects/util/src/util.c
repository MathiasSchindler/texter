#include "util/util.h"

#include "rt/rt.h"

util_sv util_sv_from_cstr(const char* s) {
  util_sv out;
  out.data = s;
  out.len = rt_strlen(s);
  return out;
}

int util_u64_add_overflow(u64 a, u64 b, u64* out) {
  u64 r = a + b;
  *out = r;
  return r < a;
}

int util_u64_mul_overflow(u64 a, u64 b, u64* out) {
  if (a == 0 || b == 0) {
    *out = 0;
    return 0;
  }

  if (a > (~(u64)0) / b) {
    *out = 0;
    return 1;
  }

  *out = a * b;
  return 0;
}

u64 util_min_u64(u64 a, u64 b) {
  return a < b ? a : b;
}

u64 util_max_u64(u64 a, u64 b) {
  return a > b ? a : b;
}
