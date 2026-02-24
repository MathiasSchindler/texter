#include "arena/arena.h"
#include "crc32/crc32.h"
#include "platform/platform.h"
#include "rt/rt.h"
#include "util/util.h"

static int tests_failed = 0;

static void test_fail(const char* msg) {
  platform_write_stdout("FAIL: ");
  platform_write_stdout(msg);
  platform_write_stdout("\n");
  tests_failed++;
}

#define CHECK(cond, msg)           \
  do {                             \
    if (!(cond)) {                 \
      test_fail(msg);              \
    }                              \
  } while (0)

static void test_util(void) {
  u64 out = 0;
  util_sv sv = util_sv_from_cstr("abc");

  CHECK(sv.len == 3, "util_sv_from_cstr length");
  CHECK(util_min_u64(2, 9) == 2, "util_min_u64");
  CHECK(util_max_u64(2, 9) == 9, "util_max_u64");

  CHECK(util_u64_add_overflow(1, 2, &out) == 0 && out == 3, "u64 add basic");
  CHECK(util_u64_add_overflow(~(u64)0, 1, &out) == 1, "u64 add overflow");

  CHECK(util_u64_mul_overflow(7, 9, &out) == 0 && out == 63, "u64 mul basic");
  CHECK(util_u64_mul_overflow(~(u64)0, 2, &out) == 1, "u64 mul overflow");

  {
    u64 a;
    for (a = 0; a < 10000; a++) {
      u64 b;
      for (b = 0; b < 100; b++) {
        u64 r;
        int ov = util_u64_add_overflow(a, b, &r);
        CHECK(ov == 0, "u64 add exhaustive no overflow range");
        CHECK(r == a + b, "u64 add exhaustive exact value");
      }
    }
  }
}

static void test_arena(void) {
  u8 backing[128];
  arena_state a;
  void* p1;
  void* p2;

  arena_init(&a, backing, sizeof(backing));
  p1 = arena_alloc(&a, 8, 8);
  p2 = arena_alloc(&a, 16, 16);

  CHECK(p1 != 0, "arena first alloc");
  CHECK(p2 != 0, "arena second alloc");
  CHECK((((usize)p1) & 7U) == 0, "arena align 8");
  CHECK((((usize)p2) & 15U) == 0, "arena align 16");

  CHECK(arena_alloc(&a, 500, 1) == 0, "arena oom");

  arena_reset(&a);
  CHECK(a.off == 0, "arena reset offset");
  CHECK(arena_alloc(&a, 64, 8) != 0, "arena alloc after reset");
}

static void test_crc32(void) {
  static const u8 v1[] = "123456789";
  static const u8 v2[] = "The quick brown fox jumps over the lazy dog";
  u32 c;
  u8 b;

  CHECK(crc32_compute((const u8*)"", 0) == 0x00000000U, "crc32 empty");
  CHECK(crc32_compute(v1, 9) == 0xCBF43926U, "crc32 123456789");
  CHECK(crc32_compute(v2, sizeof(v2) - 1) == 0x414FA339U,
        "crc32 quick brown fox");

  c = crc32_init();
  c = crc32_update(c, v1, 4);
  c = crc32_update(c, v1 + 4, 5);
  c = crc32_final(c);
  CHECK(c == 0xCBF43926U, "crc32 chunked update");

  for (b = 0; b < 255; b++) {
    u32 single = crc32_compute(&b, 1);
    u32 step = crc32_final(crc32_update(crc32_init(), &b, 1));
    CHECK(single == step, "crc32 single-byte consistency");
  }
}

int phase1_run(void) {
  platform_write_stdout("phase1: running tests\n");

  test_util();
  test_arena();
  test_crc32();

  if (tests_failed != 0) {
    char buf[32];
    usize n;
    platform_write_stdout("phase1: failed tests = ");
    n = rt_u64_to_dec((u64)tests_failed, buf, sizeof(buf));
    rt_write_all(1, buf, n);
    platform_write_stdout("\n");
    return 1;
  }

  platform_write_stdout("phase1: all tests passed\n");
  return 0;
}
