#include "odt_core/odt_core.h"
#include "platform/platform.h"
#include "rt/rt.h"
#include "zip/zip.h"

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

static int cstr_eq_n(const char* a, const char* b, usize n) {
  usize i;
  for (i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return a[n] == '\0';
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

static void test_validate_examples(void) {
  static u8 buf[262144];
  usize n = 0;

  CHECK(read_file_all("examples/blank.odt", buf, sizeof(buf), &n) == 0,
        "read blank.odt");
  CHECK(odt_core_validate_package(buf, n) == ODT_OK,
        "validate blank.odt package rules");

  CHECK(read_file_all("examples/test.odt", buf, sizeof(buf), &n) == 0,
        "read test.odt");
  CHECK(odt_core_validate_package(buf, n) == ODT_OK,
        "validate test.odt package rules");
}

static void test_extract_text_examples(void) {
  static u8 buf[262144];
  static char text[262144];
  usize n = 0;
  usize out_len = 0;

  CHECK(read_file_all("examples/test.odt", buf, sizeof(buf), &n) == 0,
        "read test.odt for text extract");
  CHECK(odt_core_extract_plain_text(buf, n, text, sizeof(text), &out_len) == ODT_OK,
        "extract plain text from test.odt");
  CHECK(out_len > 0, "non-empty extracted text");
}

static void test_build_minimal(void) {
  static u8 odt_buf[262144];
  static char text_buf[262144];
  usize odt_len = 0;
  usize text_len = 0;
  zip_archive za;
  zip_entry_view e0;
  zip_entry_view mt;

  const char* sample = "Hello ODT\nSecond line";

  CHECK(odt_core_build_minimal(sample, rt_strlen(sample), odt_buf, sizeof(odt_buf), &odt_len) ==
            ODT_OK,
        "build minimal odt");
  CHECK(odt_core_validate_package(odt_buf, odt_len) == ODT_OK,
        "validate generated odt package rules");

  CHECK(zip_archive_open(&za, odt_buf, odt_len) == ZIP_OK,
        "open generated odt as zip");
  CHECK(zip_archive_get_entry(&za, 0, &e0) == ZIP_OK, "get first generated entry");
  CHECK(cstr_eq_n("mimetype", e0.name, e0.name_len), "mimetype is first entry");
  CHECK(e0.method == ZIP_METHOD_STORE, "mimetype is stored method");

  CHECK(zip_archive_find_entry(&za, "mimetype", &mt) == ZIP_OK,
        "find generated mimetype");
  CHECK(mt.uncomp_size == rt_strlen(ODT_MIMETYPE), "generated mimetype size");

  CHECK(odt_core_extract_plain_text(odt_buf, odt_len, text_buf, sizeof(text_buf), &text_len) ==
            ODT_OK,
        "extract plain text from generated odt");
  CHECK(text_len >= rt_strlen("Hello ODT\nSecond line\n"),
        "generated extracted text length");
}

int phase5_run(void) {
  platform_write_stdout("phase5: running odt_core tests\n");

  test_validate_examples();
  test_extract_text_examples();
  test_build_minimal();

  if (tests_failed != 0) {
    char buf[32];
    usize n;
    platform_write_stdout("phase5: failed tests = ");
    n = rt_u64_to_dec((u64)tests_failed, buf, sizeof(buf));
    rt_write_all(1, buf, n);
    platform_write_stdout("\n");
    return 1;
  }

  platform_write_stdout("phase5: all tests passed\n");
  return 0;
}
