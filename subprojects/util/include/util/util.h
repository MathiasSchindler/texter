#ifndef UTIL_UTIL_H
#define UTIL_UTIL_H

#include "rt/types.h"

typedef struct util_sv {
  const char* data;
  usize len;
} util_sv;

util_sv util_sv_from_cstr(const char* s);

int util_u64_add_overflow(u64 a, u64 b, u64* out);
int util_u64_mul_overflow(u64 a, u64 b, u64* out);

u64 util_min_u64(u64 a, u64 b);
u64 util_max_u64(u64 a, u64 b);

#endif
