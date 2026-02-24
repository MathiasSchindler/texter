#include "platform/platform.h"
#include "rt/rt.h"
#include "unicode/unicode.h"

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

static void test_utf8_validate(void) {
  static const u8 ok_ascii[] = {'h', 'i'};
  static const u8 ok_2b[] = {0xC3, 0xBC}; /* U+00FC */
  static const u8 ok_3b[] = {0xE2, 0x82, 0xAC}; /* U+20AC */
  static const u8 ok_4b[] = {0xF0, 0x9F, 0x99, 0x82}; /* U+1F642 */

  static const u8 bad_overlong_2[] = {0xC0, 0xAF};
  static const u8 bad_overlong_3[] = {0xE0, 0x80, 0xAF};
  static const u8 bad_surrogate[] = {0xED, 0xA0, 0x80};
  static const u8 bad_trunc[] = {0xE2, 0x82};
  static const u8 bad_cont[] = {0x80};

  CHECK(unicode_utf8_validate(ok_ascii, sizeof(ok_ascii)) == UNICODE_OK,
        "validate ascii");
  CHECK(unicode_utf8_validate(ok_2b, sizeof(ok_2b)) == UNICODE_OK,
        "validate 2-byte utf8");
  CHECK(unicode_utf8_validate(ok_3b, sizeof(ok_3b)) == UNICODE_OK,
        "validate 3-byte utf8");
  CHECK(unicode_utf8_validate(ok_4b, sizeof(ok_4b)) == UNICODE_OK,
        "validate 4-byte utf8");

  CHECK(unicode_utf8_validate(bad_overlong_2, sizeof(bad_overlong_2)) == UNICODE_ERR_INVALID,
        "reject overlong 2-byte");
  CHECK(unicode_utf8_validate(bad_overlong_3, sizeof(bad_overlong_3)) == UNICODE_ERR_INVALID,
        "reject overlong 3-byte");
  CHECK(unicode_utf8_validate(bad_surrogate, sizeof(bad_surrogate)) == UNICODE_ERR_INVALID,
        "reject surrogate encoding");
  CHECK(unicode_utf8_validate(bad_trunc, sizeof(bad_trunc)) == UNICODE_ERR_INVALID,
        "reject truncated sequence");
  CHECK(unicode_utf8_validate(bad_cont, sizeof(bad_cont)) == UNICODE_ERR_INVALID,
        "reject stray continuation");
}

static void test_decode_encode_roundtrip(void) {
  u8 buf[8];
  usize n = 0;
  u32 cp = 0;
  usize consumed = 0;

  CHECK(unicode_utf8_encode_one(0x24U, buf, sizeof(buf), &n) == UNICODE_OK,
        "encode dollar");
  CHECK(n == 1 && buf[0] == 0x24U, "encoded dollar bytes");
  CHECK(unicode_utf8_decode_one(buf, n, &cp, &consumed) == UNICODE_OK,
        "decode dollar");
  CHECK(cp == 0x24U && consumed == 1, "decoded dollar value");

  CHECK(unicode_utf8_encode_one(0x20ACU, buf, sizeof(buf), &n) == UNICODE_OK,
        "encode euro");
  CHECK(n == 3, "encoded euro width");
  CHECK(unicode_utf8_decode_one(buf, n, &cp, &consumed) == UNICODE_OK,
        "decode euro");
  CHECK(cp == 0x20ACU && consumed == 3, "decoded euro value");

  CHECK(unicode_utf8_encode_one(0x1F642U, buf, sizeof(buf), &n) == UNICODE_OK,
        "encode smile");
  CHECK(n == 4, "encoded smile width");
  CHECK(unicode_utf8_decode_one(buf, n, &cp, &consumed) == UNICODE_OK,
        "decode smile");
  CHECK(cp == 0x1F642U && consumed == 4, "decoded smile value");

  {
    static const u8 expected[] = {0xF0, 0x9F, 0x99, 0x82};
    CHECK(bytes_equal(buf, expected, 4), "encoded smile bytes exact");
  }

  CHECK(unicode_utf8_encode_one(0xD800U, buf, sizeof(buf), &n) == UNICODE_ERR_INVALID,
        "reject surrogate code point for encoding");
  CHECK(unicode_utf8_encode_one(0x110000U, buf, sizeof(buf), &n) == UNICODE_ERR_INVALID,
        "reject code point above unicode max");
  CHECK(unicode_utf8_encode_one(0x20ACU, buf, 2, &n) == UNICODE_ERR_DST_TOO_SMALL,
        "reject too-small dst buffer");
}

static void test_xml_classification(void) {
  CHECK(unicode_is_xml_whitespace((u32)' '), "space is xml ws");
  CHECK(unicode_is_xml_whitespace((u32)'\n'), "lf is xml ws");
  CHECK(!unicode_is_xml_whitespace((u32)'A'), "A not xml ws");

  CHECK(unicode_is_xml_name_start((u32)'A'), "A is name start");
  CHECK(unicode_is_xml_name_start((u32)'_'), "_ is name start");
  CHECK(unicode_is_xml_name_start((u32)':'), ": is name start");
  CHECK(!unicode_is_xml_name_start((u32)'1'), "1 not name start");

  CHECK(unicode_is_xml_name_char((u32)'1'), "1 is name char");
  CHECK(unicode_is_xml_name_char((u32)'-'), "- is name char");
  CHECK(unicode_is_xml_name_char((u32)'.'), ". is name char");
  CHECK(!unicode_is_xml_name_char((u32)' '), "space not name char");

  CHECK(unicode_is_xml_name_start(0x03A9U), "Omega is name start for MVP non-ascii");
}

int phase7_run(void) {
  platform_write_stdout("phase7: running unicode tests\n");

  test_utf8_validate();
  test_decode_encode_roundtrip();
  test_xml_classification();

  if (tests_failed != 0) {
    char buf[32];
    usize n;
    platform_write_stdout("phase7: failed tests = ");
    n = rt_u64_to_dec((u64)tests_failed, buf, sizeof(buf));
    rt_write_all(1, buf, n);
    platform_write_stdout("\n");
    return 1;
  }

  platform_write_stdout("phase7: all tests passed\n");
  return 0;
}
