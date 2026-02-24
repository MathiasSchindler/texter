#include "phase_api.h"
#include "convert_core/convert_core.h"
#include "fmt_markdown/fmt_markdown.h"
#include "fmt_odt/fmt_odt.h"
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

static int bytes_contains(const u8* hay, usize hay_len, const char* needle, usize needle_len) {
  usize i;
  if (needle_len == 0) {
    return 1;
  }
  if (hay_len < needle_len) {
    return 0;
  }
  for (i = 0; i + needle_len <= hay_len; i++) {
    usize j;
    int match = 1;
    for (j = 0; j < needle_len; j++) {
      if (hay[i + j] != (u8)needle[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      return 1;
    }
  }
  return 0;
}

static usize bytes_count(const u8* hay, usize hay_len, const char* needle, usize needle_len) {
  usize i;
  usize count = 0;
  if (needle_len == 0 || hay_len < needle_len) {
    return 0;
  }
  for (i = 0; i + needle_len <= hay_len; i++) {
    usize j;
    int match = 1;
    for (j = 0; j < needle_len; j++) {
      if (hay[i + j] != (u8)needle[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      count++;
    }
  }
  return count;
}

static int read_file_all(const char* path, u8* dst, usize cap, usize* out_len) {
  long fd = rt_openat(-100, path, RT_O_RDONLY, 0);
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

static void build_registry(convert_registry* registry,
                           fmt_markdown_state* md_state,
                           fmt_odt_state* odt_state) {
  CHECK(fmt_markdown_register(registry, md_state) == CONVERT_OK,
        "register markdown adapter");
  CHECK(fmt_odt_register(registry, odt_state) == CONVERT_OK,
        "register odt adapter");
}

static void test_md_to_odt_convert(void) {
  static const u8 md_input[] =
      "## Hello\n"
      "- First\n"
      "- Second\n"
      "  - Nested\n"
      "This is *italic* and **bold** with `code` and [link](https://example.com).\n"
      "```c\n"
      "int x = 1;\n"
      "```\n"
      "> quoted\n";
  static u8 odt_out[262144];
  static u8 md_roundtrip[262144];
  static u8 content_xml[262144];
  static u8 styles_xml[262144];
  static fmt_markdown_state md_state;
  static fmt_odt_state odt_state;
  usize odt_out_len = 0;
  usize md_roundtrip_len = 0;
  usize content_len = 0;
  usize styles_len = 0;

  convert_registry registry;
  convert_session session;
  convert_request req;
  convert_diagnostics diags;
  doc_model_document doc;
  zip_archive za;
  zip_entry_view content_entry;
  zip_entry_view styles_entry;
  int rc;

  convert_registry_init(&registry);
  build_registry(&registry, &md_state, &odt_state);
  CHECK(convert_session_init(&session, &doc, &diags) == CONVERT_OK,
        "session init for md->odt");

  req.from_format = "md";
  req.to_format = "odt";
  req.input = md_input;
  req.input_len = sizeof(md_input) - 1;
  req.output = odt_out;
  req.output_cap = sizeof(odt_out);
  req.output_len = &odt_out_len;
  req.policy = CONVERT_POLICY_LOSSY;

  rc = convert_core_run_with_registry(&req, &registry, &session);
  CHECK(rc == CONVERT_OK, "convert md->odt with registry");
  CHECK(odt_out_len > 0, "md->odt output bytes");
  CHECK(odt_core_validate_package(odt_out, odt_out_len) == ODT_OK,
        "md->odt output is valid odt package");

    CHECK(zip_archive_open(&za, odt_out, odt_out_len) == ZIP_OK,
      "open converted md->odt zip");
    CHECK(zip_archive_find_entry(&za, "content.xml", &content_entry) == ZIP_OK,
      "find converted content.xml");
      CHECK(zip_archive_find_entry(&za, "styles.xml", &styles_entry) == ZIP_OK,
        "find converted styles.xml");
    CHECK(zip_entry_extract(&content_entry, content_xml, sizeof(content_xml), &content_len) == ZIP_OK,
      "extract converted content.xml");
      CHECK(zip_entry_extract(&styles_entry, styles_xml, sizeof(styles_xml), &styles_len) == ZIP_OK,
        "extract converted styles.xml");
    CHECK(bytes_contains(content_xml, content_len, "<text:h", 7),
      "converted content.xml has heading node");
    CHECK(bytes_contains(content_xml, content_len, "<text:list", 10),
      "converted content.xml has list node");
        CHECK(bytes_contains(content_xml, content_len, "text:style-name=\"Heading_20_2\"", 29),
        "converted content.xml references heading paragraph style");
        CHECK(bytes_contains(content_xml, content_len, "text:style-name=\"L1\"", 20),
        "converted content.xml references list style");
      CHECK(bytes_contains(content_xml, content_len, "<text:span", 10),
        "converted content.xml has span node for inline styling");
      CHECK(bytes_contains(content_xml, content_len, "<text:a", 7),
        "converted content.xml has link node");
      CHECK(bytes_contains(content_xml, content_len, "text:style-name=\"Preformatted_20_Text\"", 37),
        "converted content.xml has code paragraph style");
      CHECK(bytes_contains(content_xml, content_len, "text:style-name=\"Quotations\"", 28),
        "converted content.xml has quote paragraph style");
      CHECK(bytes_count(content_xml, content_len, "<text:list", 10) >= 2,
        "converted content.xml has nested list structure");
      CHECK(bytes_contains(styles_xml, styles_len, "style:name=\"Heading_20_2\"", 24),
        "styles.xml defines heading style");
      CHECK(bytes_contains(styles_xml, styles_len, "style:name=\"L1\"", 15),
        "styles.xml defines bullet list style");
      CHECK(bytes_contains(styles_xml, styles_len, "style:name=\"Preformatted_20_Text\"", 32),
        "styles.xml defines code paragraph style");
      CHECK(bytes_contains(styles_xml, styles_len, "style:name=\"Quotations\"", 23),
        "styles.xml defines quote paragraph style");

        req.from_format = "odt";
        req.to_format = "md";
        req.input = odt_out;
        req.input_len = odt_out_len;
        req.output = md_roundtrip;
        req.output_cap = sizeof(md_roundtrip);
        req.output_len = &md_roundtrip_len;
        req.policy = CONVERT_POLICY_LOSSY;

        rc = convert_core_run_with_registry(&req, &registry, &session);
        CHECK(rc == CONVERT_OK, "convert generated odt->md roundtrip");
        CHECK(bytes_contains(md_roundtrip, md_roundtrip_len, "## Hello", 8),
          "roundtrip markdown preserves heading");
        CHECK(bytes_contains(md_roundtrip, md_roundtrip_len, "- First", 7),
          "roundtrip markdown preserves list item");
        CHECK(bytes_contains(md_roundtrip, md_roundtrip_len, "**bold**", 8),
          "roundtrip markdown preserves strong emphasis");
        CHECK(bytes_contains(md_roundtrip, md_roundtrip_len, "`code`", 6),
          "roundtrip markdown preserves code span");
        CHECK(bytes_contains(md_roundtrip, md_roundtrip_len, "[link](https://example.com)", 27),
          "roundtrip markdown preserves link");
        CHECK(bytes_contains(md_roundtrip, md_roundtrip_len, "```", 3),
          "roundtrip markdown preserves code block");
        CHECK(bytes_contains(md_roundtrip, md_roundtrip_len, "> quoted", 8),
          "roundtrip markdown preserves blockquote");
}

static void test_odt_to_md_convert(void) {
  static u8 odt_in[262144];
  static u8 md_out[262144];
  static fmt_markdown_state md_state;
  static fmt_odt_state odt_state;
  usize odt_in_len = 0;
  usize md_out_len = 0;

  convert_registry registry;
  convert_session session;
  convert_request req;
  convert_diagnostics diags;
  doc_model_document doc;
  int rc;

  CHECK(read_file_all("examples/test.odt", odt_in, sizeof(odt_in), &odt_in_len) == 0,
        "read examples/test.odt for odt->md");

  convert_registry_init(&registry);
  build_registry(&registry, &md_state, &odt_state);
  CHECK(convert_session_init(&session, &doc, &diags) == CONVERT_OK,
        "session init for odt->md");

  req.from_format = "odt";
  req.to_format = "md";
  req.input = odt_in;
  req.input_len = odt_in_len;
  req.output = md_out;
  req.output_cap = sizeof(md_out);
  req.output_len = &md_out_len;
  req.policy = CONVERT_POLICY_LOSSY;

  rc = convert_core_run_with_registry(&req, &registry, &session);
  CHECK(rc == CONVERT_OK, "convert odt->md with registry");
  CHECK(md_out_len > 0, "odt->md output bytes");
  CHECK(bytes_contains(md_out, md_out_len, "This is a test.", 15),
        "odt->md contains expected text");
}

int phase10_run(void) {
  platform_write_stdout("phase10: running adapter registration tests\n");

  test_md_to_odt_convert();
  test_odt_to_md_convert();

  if (tests_failed != 0) {
    platform_write_stdout("phase10: tests failed\n");
    return 1;
  }

  platform_write_stdout("phase10: all tests passed\n");
  return 0;
}
