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

static int bytes_equal(const u8* a, const u8* b, usize n) {
  usize i;
  for (i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
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

static void test_archive_basic(const char* path) {
  u8 file_buf[262144];
  u8 out_buf[262144];
  usize file_len = 0;
  zip_archive za;
  zip_entry_view ze;
  usize out_len = 0;
  u16 entry_count = 0;

  CHECK(read_file_all(path, file_buf, sizeof(file_buf), &file_len) == 0,
        "read example archive");
  CHECK(zip_archive_open(&za, file_buf, file_len) == ZIP_OK, "zip_archive_open");
  CHECK(zip_archive_entry_count(&za, &entry_count) == ZIP_OK, "zip_archive_entry_count");
  CHECK(entry_count >= 5, "zip entry count reasonable");

  CHECK(zip_archive_find_entry(&za, "mimetype", &ze) == ZIP_OK,
        "find mimetype entry");
  CHECK(zip_entry_name_is_safe(&ze) == ZIP_OK, "mimetype path safety");
  CHECK(zip_entry_extract(&ze, out_buf, sizeof(out_buf), &out_len) == ZIP_OK,
        "extract mimetype");
  CHECK(out_len == 39, "mimetype length");
  CHECK(bytes_equal(out_buf,
                    (const u8*)"application/vnd.oasis.opendocument.text",
                    39),
        "mimetype content");

  CHECK(zip_archive_find_entry(&za, "content.xml", &ze) == ZIP_OK,
        "find content.xml entry");
  CHECK(zip_entry_name_is_safe(&ze) == ZIP_OK, "content.xml path safety");
  CHECK(zip_entry_extract(&ze, out_buf, sizeof(out_buf), &out_len) == ZIP_OK,
        "extract content.xml");
  CHECK(out_len > 200, "content.xml length");
}

static void test_repack_blank_odt(void) {
  u8 src_buf[262144];
  u8 repacked[262144];
  u8 extract_buf[262144];
  usize src_len = 0;
  usize repacked_len = 0;
  zip_archive src;
  zip_archive dst;
  zip_writer zw;
  u16 n = 0;
  u16 i;

  CHECK(read_file_all("examples/blank.odt", src_buf, sizeof(src_buf), &src_len) == 0,
        "read blank.odt");
  CHECK(zip_archive_open(&src, src_buf, src_len) == ZIP_OK, "open source archive");
  CHECK(zip_archive_entry_count(&src, &n) == ZIP_OK, "source entry count");

  zip_writer_init(&zw, repacked, sizeof(repacked));
  for (i = 0; i < n; i++) {
    zip_entry_view ze;
    char name_buf[ZIP_WRITER_MAX_NAME + 1];
    usize out_len;
    int method;
    usize j;
    CHECK(zip_archive_get_entry(&src, i, &ze) == ZIP_OK, "get source entry");
    CHECK(zip_entry_name_is_safe(&ze) == ZIP_OK, "source entry name safe");
    CHECK(zip_entry_extract(&ze, extract_buf, sizeof(extract_buf), &out_len) == ZIP_OK,
          "extract source entry");

    CHECK(ze.name_len <= ZIP_WRITER_MAX_NAME, "source name length for repack");
    for (j = 0; j < ze.name_len; j++) {
      name_buf[j] = ze.name[j];
    }
    name_buf[ze.name_len] = '\0';

    method = (ze.method == ZIP_METHOD_STORE) ? ZIP_METHOD_STORE : ZIP_METHOD_DEFLATE;
    CHECK(zip_writer_add_entry(&zw, name_buf, extract_buf, out_len, (u16)method) == ZIP_OK,
          "add repacked entry");
  }

  CHECK(zip_writer_finish(&zw, &repacked_len) == ZIP_OK, "finish repacked zip");
  CHECK(zip_archive_open(&dst, repacked, repacked_len) == ZIP_OK,
        "open repacked archive");

  {
    zip_entry_view ze;
    usize out_len;
    CHECK(zip_archive_find_entry(&dst, "mimetype", &ze) == ZIP_OK,
          "find mimetype in repacked");
    CHECK(zip_entry_extract(&ze, extract_buf, sizeof(extract_buf), &out_len) == ZIP_OK,
          "extract mimetype from repacked");
    CHECK(out_len == 39, "repacked mimetype len");
  }

  {
    zip_entry_view ze;
    usize out_len;
    CHECK(zip_archive_find_entry(&dst, "content.xml", &ze) == ZIP_OK,
          "find content.xml in repacked");
    CHECK(zip_entry_extract(&ze, extract_buf, sizeof(extract_buf), &out_len) == ZIP_OK,
          "extract content.xml from repacked");
    CHECK(out_len > 200, "repacked content.xml len");
  }
}

int phase3_run(void) {
  platform_write_stdout("phase3: running zip tests\n");

  test_archive_basic("examples/blank.odt");
  test_archive_basic("examples/test.odt");
  test_repack_blank_odt();

  if (tests_failed != 0) {
    char buf[32];
    usize n;
    platform_write_stdout("phase3: failed tests = ");
    n = rt_u64_to_dec((u64)tests_failed, buf, sizeof(buf));
    rt_write_all(1, buf, n);
    platform_write_stdout("\n");
    return 1;
  }

  platform_write_stdout("phase3: all tests passed\n");
  return 0;
}
