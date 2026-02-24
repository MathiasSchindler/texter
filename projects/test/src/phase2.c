#include "deflate/deflate.h"
#include "platform/platform.h"
#include "rt/rt.h"

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

static u32 xorshift32(u32* state) {
  u32 x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static int bytes_equal(const u8* a, const u8* b, usize n) {
  usize i;
  for (i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

static u16 rd_le16(const u8* p) {
  return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 rd_le32(const u8* p) {
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int read_file_all(const char* path, u8* dst, usize cap, usize* out_len) {
  long fd = rt_openat(-100, path, 0, 0);
  usize off = 0;
  if (fd < 0) {
    return -1;
  }
  for (;;) {
    long n = rt_read((int)fd, dst + off, cap - off);
    if (n < 0) {
      rt_close((int)fd);
      return -2;
    }
    if (n == 0) {
      break;
    }
    off += (usize)n;
    if (off == cap) {
      rt_close((int)fd);
      return -3;
    }
  }
  rt_close((int)fd);
  *out_len = off;
  return 0;
}

static void test_examples_odt_inflate(const char* path) {
  u8 file_buf[262144];
  u8 out_buf[262144];
  usize file_len = 0;
  usize cd_pos;
  usize cd_end;
  int found_deflate = 0;
  int inflated_deflate = 0;
  usize eocd_pos = (usize)-1;
  usize scan_start;
  u16 total_entries;
  u32 cd_size;
  u32 cd_offset;
  u16 entry_idx;

  if (read_file_all(path, file_buf, sizeof(file_buf), &file_len) != 0) {
    test_fail("read examples odt file");
    return;
  }

  if (file_len < 22) {
    test_fail("zip file too small");
    return;
  }

  scan_start = (file_len > 70000U) ? (file_len - 70000U) : 0U;
  {
    usize p = file_len - 22;
    for (;;) {
      if (rd_le32(file_buf + p) == 0x06054B50U) {
        eocd_pos = p;
        break;
      }
      if (p == scan_start) {
        break;
      }
      p--;
    }
  }

  if (eocd_pos == (usize)-1) {
    test_fail("zip EOCD not found");
    return;
  }

  total_entries = rd_le16(file_buf + eocd_pos + 10);
  cd_size = rd_le32(file_buf + eocd_pos + 12);
  cd_offset = rd_le32(file_buf + eocd_pos + 16);

  cd_pos = (usize)cd_offset;
  cd_end = cd_pos + (usize)cd_size;
  if (cd_end > file_len) {
    test_fail("zip central directory out of bounds");
    return;
  }

  for (entry_idx = 0; entry_idx < total_entries && cd_pos + 46 <= cd_end; entry_idx++) {
    u32 csig = rd_le32(file_buf + cd_pos);
    u16 method;
    u32 comp_size;
    u32 uncomp_size;
    u16 name_len;
    u16 extra_len;
    u16 comment_len;
    u32 local_off;

    if (csig != 0x02014B50U) {
      test_fail("zip central entry signature mismatch");
      return;
    }

    method = rd_le16(file_buf + cd_pos + 10);
    comp_size = rd_le32(file_buf + cd_pos + 20);
    uncomp_size = rd_le32(file_buf + cd_pos + 24);
    name_len = rd_le16(file_buf + cd_pos + 28);
    extra_len = rd_le16(file_buf + cd_pos + 30);
    comment_len = rd_le16(file_buf + cd_pos + 32);
    local_off = rd_le32(file_buf + cd_pos + 42);

    if (method == 8U) {
      usize lpos = (usize)local_off;
      usize data_pos;
      u16 lname_len;
      u16 lextra_len;
      usize out_len = 0;
      int rc;

      if (lpos + 30 > file_len || rd_le32(file_buf + lpos) != 0x04034B50U) {
        test_fail("zip local header invalid from central entry");
        return;
      }

      lname_len = rd_le16(file_buf + lpos + 26);
      lextra_len = rd_le16(file_buf + lpos + 28);
      data_pos = lpos + 30 + (usize)lname_len + (usize)lextra_len;

      if (data_pos + (usize)comp_size > file_len) {
        test_fail("zip local compressed data out of bounds");
        return;
      }

      found_deflate = 1;
      rc = deflate_inflate(file_buf + data_pos, (usize)comp_size, out_buf,
                           sizeof(out_buf), &out_len);
      if (rc == DEFLATE_OK && out_len == (usize)uncomp_size) {
        inflated_deflate = 1;
      }
    }

    cd_pos += 46 + (usize)name_len + (usize)extra_len + (usize)comment_len;
  }

  CHECK(found_deflate == 1, "examples contain deflate entries");
  CHECK(inflated_deflate == 1, "inflate works on examples deflate entry");
}

static void test_known_vectors(void) {
  static const u8 hello_src[] = {'h', 'e', 'l', 'l', 'o'};
  static const u8 hello_deflate[] = {0x01, 0x05, 0x00, 0xFA, 0xFF, 'h', 'e', 'l', 'l', 'o'};
  u8 out[64];
  usize out_len = 0;
  int rc;

  rc = deflate_inflate(hello_deflate, sizeof(hello_deflate), out, sizeof(out), &out_len);
  CHECK(rc == DEFLATE_OK, "inflate hello known vector rc");
  CHECK(out_len == sizeof(hello_src), "inflate hello known vector len");
  CHECK(bytes_equal(out, hello_src, sizeof(hello_src)), "inflate hello known vector bytes");

  {
    static const u8 empty_deflate[] = {0x01, 0x00, 0x00, 0xFF, 0xFF};
    usize empty_len = 123;
    rc = deflate_inflate(empty_deflate, sizeof(empty_deflate), out, sizeof(out), &empty_len);
    CHECK(rc == DEFLATE_OK, "inflate empty known vector rc");
    CHECK(empty_len == 0, "inflate empty known vector len");
  }

  {
    u8 comp[64];
    usize comp_len = 0;
    rc = deflate_compress(hello_src, sizeof(hello_src), DEFLATE_LEVEL_STORE_ONLY, comp,
                          sizeof(comp), &comp_len);
    CHECK(rc == DEFLATE_OK, "compress hello rc");
    CHECK(comp_len == sizeof(hello_deflate), "compress hello len");
    CHECK(bytes_equal(comp, hello_deflate, sizeof(hello_deflate)), "compress hello vector");
  }
}

static void test_error_paths(void) {
  static const u8 bad_nlen[] = {0x01, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c'};
  u8 out[16];
  usize out_len;
  int rc;

  out_len = 0;
  rc = deflate_inflate(bad_nlen, sizeof(bad_nlen), out, sizeof(out), &out_len);
  CHECK(rc == DEFLATE_ERR_INVALID_STREAM, "inflate detects bad NLEN");

  {
    static const u8 hello_deflate[] = {0x01, 0x05, 0x00, 0xFA, 0xFF, 'h', 'e', 'l', 'l', 'o'};
    out_len = 0;
    rc = deflate_inflate(hello_deflate, sizeof(hello_deflate), out, 3, &out_len);
    CHECK(rc == DEFLATE_ERR_DST_TOO_SMALL, "inflate dst too small");
  }
}

static void test_roundtrip_property(void) {
  u8 src[70000];
  u8 comp[71000];
  u8 out[70000];
  u32 rng = 0x12345678U;
  usize case_idx;

  for (case_idx = 0; case_idx < 250; case_idx++) {
    usize n = (usize)(xorshift32(&rng) % 70000U);
    usize i;
    usize comp_len = 0;
    usize out_len = 0;
    int rc;

    for (i = 0; i < n; i++) {
      src[i] = (u8)(xorshift32(&rng) & 0xFFU);
    }

    rc = deflate_compress(src, n, DEFLATE_LEVEL_STORE_ONLY, comp, sizeof(comp), &comp_len);
    CHECK(rc == DEFLATE_OK, "property compress rc");

    rc = deflate_inflate(comp, comp_len, out, sizeof(out), &out_len);
    CHECK(rc == DEFLATE_OK, "property inflate rc");
    CHECK(out_len == n, "property size match");
    CHECK(bytes_equal(src, out, n), "property bytes match");
  }
}

static void test_best_mode(void) {
  static const u8 text[] =
      "This text should compress better with fixed Huffman coding than plain stored blocks.";
  u8 comp_best[256];
  u8 comp_store[256];
  u8 out[256];
  usize comp_best_len = 0;
  usize comp_store_len = 0;
  usize out_len = 0;
  int rc;

  rc = deflate_compress(text, sizeof(text) - 1, DEFLATE_LEVEL_STORE_ONLY, comp_store,
                        sizeof(comp_store), &comp_store_len);
  CHECK(rc == DEFLATE_OK, "store compress for best-mode compare");

  rc = deflate_compress(text, sizeof(text) - 1, DEFLATE_LEVEL_BEST, comp_best,
                        sizeof(comp_best), &comp_best_len);
  CHECK(rc == DEFLATE_OK, "best compress rc");
  CHECK(comp_best_len <= comp_store_len, "best output no larger than store output");

  rc = deflate_inflate(comp_best, comp_best_len, out, sizeof(out), &out_len);
  CHECK(rc == DEFLATE_OK, "inflate best output rc");
  CHECK(out_len == sizeof(text) - 1, "inflate best output len");
  CHECK(bytes_equal(out, text, sizeof(text) - 1), "inflate best output bytes");
}

int phase2_run(void) {
  platform_write_stdout("phase2: running deflate tests\n");

  test_known_vectors();
  test_error_paths();
  test_roundtrip_property();
  test_best_mode();
  test_examples_odt_inflate("examples/blank.odt");
  test_examples_odt_inflate("examples/test.odt");

  if (tests_failed != 0) {
    char buf[32];
    usize n;
    platform_write_stdout("phase2: failed tests = ");
    n = rt_u64_to_dec((u64)tests_failed, buf, sizeof(buf));
    rt_write_all(1, buf, n);
    platform_write_stdout("\n");
    return 1;
  }

  platform_write_stdout("phase2: all tests passed\n");
  return 0;
}
