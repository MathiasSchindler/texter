#include "phase_api.h"
#include "convert_core/convert_core.h"
#include "fmt_markdown/fmt_markdown.h"
#include "fmt_odt/fmt_odt.h"
#include "odt_cli/cli.h"
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

static int write_file_all(const char* path, const u8* src, usize n) {
  long fd = rt_openat(-100, path, RT_O_WRONLY | RT_O_CREAT | RT_O_TRUNC, 0644);
  if (fd < 0) {
    return -1;
  }
  if (rt_write_all((int)fd, src, n) != (long)n) {
    rt_close((int)fd);
    return -2;
  }
  rt_close((int)fd);
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

static void test_meltdown_role_mapping_cli(void) {
  static const char md_input[] =
  "# Role title\n"
  "Body paragraph.\n";
  static const char profile_input[] =
  "profile: role-map\n"
  "version: 1\n"
  "roles:\n"
  "  Title:\n"
  "recognize:\n"
  "  Title:\n"
  "    match:\n"
  "      - heading(level=1)\n"
  "    where:\n"
  "      - position == first\n"
  "    priority: 100\n"
  "layout:\n"
  "  Title:\n"
    "    style: MeltdownTitleStyle\n"
    "present:\n"
    "  page:\n"
    "    orientation: landscape\n"
    "    margins:\n"
    "      left: 31mm\n"
    "  styles:\n"
    "    MeltdownTitleStyle:\n"
    "      family: paragraph\n"
    "      based-on: Heading_20_1\n"
    "      margin-left: 31mm\n";
  static u8 out_odt[262144];
  static u8 content_xml[131072];
  static u8 styles_xml[131072];
  usize out_odt_len = 0;
  usize content_len = 0;
  usize styles_len = 0;
  zip_archive za;
  zip_entry_view content_entry;
  zip_entry_view styles_entry;

  const char* argv_convert[] = {
  "odt_cli", "convert", "--from", "md", "--to", "odt", "--template",
  "build/phase10_role_map.meltdown", "build/phase10_role_map.md", "build/phase10_role_map.odt"};
  const char* argv_validate[] = {"odt_cli", "validate", "build/phase10_role_map.odt"};

  CHECK(write_file_all("build/phase10_role_map.md", (const u8*)md_input, rt_strlen(md_input)) == 0,
    "phase10 meltdown: write md fixture");
  CHECK(write_file_all("build/phase10_role_map.meltdown",
           (const u8*)profile_input,
           rt_strlen(profile_input)) == 0,
    "phase10 meltdown: write profile fixture");
  CHECK(odt_cli_run(10, argv_convert) == 0, "phase10 meltdown: convert md->odt with profile");
  CHECK(odt_cli_run(3, argv_validate) == 0, "phase10 meltdown: validate generated odt");

  CHECK(read_file_all("build/phase10_role_map.odt", out_odt, sizeof(out_odt), &out_odt_len) == 0,
    "phase10 meltdown: read output odt");
  CHECK(zip_archive_open(&za, out_odt, out_odt_len) == ZIP_OK,
    "phase10 meltdown: open output odt zip");
  CHECK(zip_archive_find_entry(&za, "content.xml", &content_entry) == ZIP_OK,
    "phase10 meltdown: find content.xml");
    CHECK(zip_archive_find_entry(&za, "styles.xml", &styles_entry) == ZIP_OK,
      "phase10 meltdown: find styles.xml");
  CHECK(zip_entry_extract(&content_entry, content_xml, sizeof(content_xml), &content_len) == ZIP_OK,
    "phase10 meltdown: extract content.xml");
    CHECK(zip_entry_extract(&styles_entry, styles_xml, sizeof(styles_xml), &styles_len) == ZIP_OK,
      "phase10 meltdown: extract styles.xml");
  CHECK(bytes_contains(content_xml,
           content_len,
           "text:style-name=\"MeltdownTitleStyle\"",
           36),
    "phase10 meltdown: mapped style applied to heading role");
    CHECK(bytes_contains(styles_xml,
             styles_len,
             "style:name=\"MeltdownTitleStyle\"",
               rt_strlen("style:name=\"MeltdownTitleStyle\"")),
      "phase10 meltdown: present style materialized in styles.xml");
    CHECK(bytes_contains(styles_xml,
             styles_len,
             "fo:margin-left=\"31mm\"",
               rt_strlen("fo:margin-left=\"31mm\"")),
      "phase10 meltdown: present style property materialized");
    CHECK(bytes_contains(styles_xml,
             styles_len,
             "style:print-orientation=\"landscape\"",
               rt_strlen("style:print-orientation=\"landscape\"")),
      "phase10 meltdown: present page orientation materialized");
}

  static void test_odt_semantic_structures(void) {
    static const char content_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document-content"
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
    " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
    " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
    " office:version=\"1.4\">"
    "<office:body><office:text>"
    "<text:section text:name=\"Semantics\">"
    "<text:p text:style-name=\"Text_20_body\">"
    "before "
    "<text:bookmark-start text:name=\"bk1\"/>"
    " and <text:a xlink:href=\"https://example.com\">link</text:a>"
    "<text:note><text:note-body><text:p>note body</text:p></text:note-body></text:note>"
    "</text:p>"
    "<text:p><draw:frame><draw:image xlink:href=\"Pictures/test.png\"/></draw:frame></text:p>"
    "<table:table table:name=\"T1\">"
    "<table:table-row><table:table-cell><text:p>H1</text:p></table:table-cell><table:table-cell><text:p>H2</text:p></table:table-cell></table:table-row>"
    "<table:table-row><table:table-cell><text:p>A</text:p></table:table-cell><table:table-cell><text:p>B</text:p></table:table-cell></table:table-row>"
    "</table:table>"
    "</text:section>"
    "</office:text></office:body></office:document-content>";

    static const char styles_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " office:version=\"1.4\"><office:styles/></office:document-styles>";

    static u8 odt_out[1048576];
    static u8 md_out[1048576];
    static fmt_markdown_state md_state;
    static fmt_odt_state odt_state;
    convert_registry registry;
    convert_session session;
    convert_request req;
    convert_diagnostics diags;
    doc_model_document doc;
    static zip_writer zw;
    usize odt_len = 0;
    usize md_len = 0;
    int rc;

    zip_writer_init(&zw, odt_out, sizeof(odt_out));
    CHECK(zip_writer_add_entry(&zw,
               "mimetype",
               (const u8*)ODT_MIMETYPE,
               rt_strlen(ODT_MIMETYPE),
               ZIP_METHOD_STORE) == ZIP_OK,
      "semantic fixture: add mimetype");
    CHECK(zip_writer_add_entry(&zw,
               "content.xml",
               (const u8*)content_xml,
               rt_strlen(content_xml),
               ZIP_METHOD_DEFLATE) == ZIP_OK,
      "semantic fixture: add content.xml");
    CHECK(zip_writer_add_entry(&zw,
               "styles.xml",
               (const u8*)styles_xml,
               rt_strlen(styles_xml),
               ZIP_METHOD_DEFLATE) == ZIP_OK,
      "semantic fixture: add styles.xml");
    CHECK(zip_writer_add_entry(&zw,
               "meta.xml",
               (const u8*)ODT_META_DOC_MINIMAL,
               rt_strlen(ODT_META_DOC_MINIMAL),
               ZIP_METHOD_DEFLATE) == ZIP_OK,
      "semantic fixture: add meta.xml");
    CHECK(zip_writer_add_entry(&zw,
               "META-INF/manifest.xml",
               (const u8*)ODT_MANIFEST_DOC_MINIMAL,
               rt_strlen(ODT_MANIFEST_DOC_MINIMAL),
               ZIP_METHOD_DEFLATE) == ZIP_OK,
      "semantic fixture: add manifest");
    CHECK(zip_writer_finish(&zw, &odt_len) == ZIP_OK, "semantic fixture: finish odt");

    convert_registry_init(&registry);
    build_registry(&registry, &md_state, &odt_state);
    CHECK(convert_session_init(&session, &doc, &diags) == CONVERT_OK,
      "semantic fixture: session init");

    req.from_format = "odt";
    req.to_format = "md";
    req.input = odt_out;
    req.input_len = odt_len;
    req.output = md_out;
    req.output_cap = sizeof(md_out);
    req.output_len = &md_len;
    req.policy = CONVERT_POLICY_LOSSY;

    rc = convert_core_run_with_registry(&req, &registry, &session);
    CHECK(rc == CONVERT_OK, "semantic fixture: odt->md convert");
    CHECK(md_len > 0, "semantic fixture: markdown output non-empty");

    CHECK(bytes_contains(md_out, md_len, "## Section: Semantics", 20),
      "semantic import keeps section boundary");
    CHECK(bytes_contains(md_out, md_len, "[link](https://example.com)", 27),
      "semantic import keeps link");
    CHECK(bytes_contains(md_out, md_len, "{#bk1}", 6),
      "semantic import keeps bookmark anchor");
    CHECK(bytes_contains(md_out, md_len, "[^1]", 4),
      "semantic import keeps footnote reference");
    CHECK(bytes_contains(md_out, md_len, "[^1]: note body", 14),
      "semantic import keeps footnote definition");
    CHECK(bytes_contains(md_out, md_len, "![image](Pictures/test.png)", 27),
      "semantic import keeps image placeholder");
    CHECK(bytes_contains(md_out, md_len, "| --- |", 7),
      "semantic import maps table to structured markdown table");
  }

int phase10_run(void) {
  platform_write_stdout("phase10: running adapter registration tests\n");

  test_md_to_odt_convert();
  test_odt_to_md_convert();
  test_odt_semantic_structures();
  test_meltdown_role_mapping_cli();

  if (tests_failed != 0) {
    platform_write_stdout("phase10: tests failed\n");
    return 1;
  }

  platform_write_stdout("phase10: all tests passed\n");
  return 0;
}
