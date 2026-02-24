#include "platform/platform.h"
#include "rt/rt.h"
#include "xml/xml.h"
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

static int sv_eq_cstr(xml_sv sv, const char* s) {
  usize i = 0;
  while (s[i] != '\0') {
    if (i >= sv.len || sv.data[i] != s[i]) {
      return 0;
    }
    i++;
  }
  return i == sv.len;
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

static void test_xml_namespaces(void) {
  const char* doc =
      "<?xml version=\"1.0\"?><root xmlns=\"urn:r\" xmlns:t=\"urn:t\">"
      "<t:a x=\"1\" t:y=\"&lt;v&gt;\">Hi &amp; Bye</t:a><t:b/></root>";
  static xml_reader xr;
  xml_token tok;
  int saw_root = 0;
  int saw_a = 0;
  int saw_text = 0;
  int saw_b_start = 0;
  int saw_b_end = 0;

  xml_reader_init(&xr, doc, rt_strlen(doc));

  for (;;) {
    CHECK(xml_reader_next(&xr, &tok) == XML_OK, "xml_reader_next synthetic");
    if (xml_reader_error(&xr) != XML_OK) {
      break;
    }
    if (tok.kind == XML_TOK_EOF) {
      break;
    }
    if (tok.kind == XML_TOK_START_ELEM && sv_eq_cstr(tok.qname, "root")) {
      saw_root = 1;
      CHECK(sv_eq_cstr(tok.ns_uri, "urn:r"), "root default namespace");
    }
    if (tok.kind == XML_TOK_START_ELEM && sv_eq_cstr(tok.qname, "t:a")) {
      usize i;
      saw_a = 1;
      CHECK(sv_eq_cstr(tok.ns_uri, "urn:t"), "prefixed namespace on element");
      for (i = 0; i < tok.attr_count; i++) {
        if (sv_eq_cstr(tok.attrs[i].qname, "x")) {
          CHECK(sv_eq_cstr(tok.attrs[i].value, "1"), "attr x value");
          CHECK(tok.attrs[i].ns_uri.len == 0, "unprefixed attr has no ns");
        }
        if (sv_eq_cstr(tok.attrs[i].qname, "t:y")) {
          CHECK(sv_eq_cstr(tok.attrs[i].value, "<v>"), "entity decoded attr");
          CHECK(sv_eq_cstr(tok.attrs[i].ns_uri, "urn:t"), "prefixed attr ns");
        }
      }
    }
    if (tok.kind == XML_TOK_TEXT) {
      if (sv_eq_cstr(tok.text, "Hi & Bye")) {
        saw_text = 1;
      }
    }
    if (tok.kind == XML_TOK_START_ELEM && sv_eq_cstr(tok.qname, "t:b")) {
      saw_b_start = 1;
    }
    if (tok.kind == XML_TOK_END_ELEM && sv_eq_cstr(tok.qname, "t:b")) {
      saw_b_end = 1;
    }
  }

  CHECK(xml_reader_error(&xr) == XML_OK, "no synthetic parse errors");
  CHECK(saw_root, "saw root element");
  CHECK(saw_a, "saw namespaced element t:a");
  CHECK(saw_text, "decoded text entity");
  CHECK(saw_b_start && saw_b_end, "self-closing emits start and end");
}

static void test_xml_writer(void) {
  u8 out[512];
  xml_writer xw;
  usize n = 0;

  xml_writer_init(&xw, out, sizeof(out));
  CHECK(xml_writer_decl(&xw) == XML_OK, "writer decl");
  CHECK(xml_writer_start_elem(&xw, "root") == XML_OK, "writer start root");
  CHECK(xml_writer_attr(&xw, "a", "1 & 2") == XML_OK, "writer attr");
  CHECK(xml_writer_start_elem(&xw, "child") == XML_OK, "writer start child");
  CHECK(xml_writer_text(&xw, "<ok> & \"good\"", 13) == XML_OK, "writer text");
  CHECK(xml_writer_end_elem(&xw, "child") == XML_OK, "writer end child");
  CHECK(xml_writer_end_elem(&xw, "root") == XML_OK, "writer end root");
  CHECK(xml_writer_finish(&xw, &n) == XML_OK, "writer finish");

  CHECK(n > 50, "writer output len");
    CHECK(bytes_contains(out, n, "a=\"1 &amp; 2\"", 13),
        "writer escapes attr ampersand");
    CHECK(bytes_contains(out, n, "&lt;ok&gt; &amp; \"good\"", 22),
        "writer escapes text special chars");
}

static void test_parse_odt_content(const char* odt_path) {
  static u8 file_buf[262144];
  static u8 xml_buf[262144];
  usize file_len = 0;
  usize xml_len = 0;
  zip_archive za;
  zip_entry_view ze;
  static xml_reader xr;
  xml_token tok;
  int saw_doc = 0;
  int saw_body = 0;
  usize steps = 0;

  CHECK(read_file_all(odt_path, file_buf, sizeof(file_buf), &file_len) == 0,
        "read odt file");
  CHECK(zip_archive_open(&za, file_buf, file_len) == ZIP_OK, "open odt archive");
  CHECK(zip_archive_find_entry(&za, "content.xml", &ze) == ZIP_OK,
        "find content.xml in odt");
  CHECK(zip_entry_extract(&ze, xml_buf, sizeof(xml_buf), &xml_len) == ZIP_OK,
        "extract content.xml from odt");

  xml_reader_init(&xr, (const char*)xml_buf, xml_len);
  for (;;) {
    CHECK(xml_reader_next(&xr, &tok) == XML_OK, "parse content.xml token");
    if (xml_reader_error(&xr) != XML_OK) {
      break;
    }
    if (tok.kind == XML_TOK_EOF) {
      break;
    }
    steps++;
    if (tok.kind == XML_TOK_START_ELEM && sv_eq_cstr(tok.qname, "office:document-content")) {
      saw_doc = 1;
    }
    if (tok.kind == XML_TOK_START_ELEM && sv_eq_cstr(tok.qname, "office:body")) {
      saw_body = 1;
    }
  }

  CHECK(xml_reader_error(&xr) == XML_OK, "content.xml parse has no error");
  CHECK(steps > 20, "content.xml produced enough tokens");
  CHECK(saw_doc, "saw office:document-content");
  CHECK(saw_body, "saw office:body");
}

int phase4_run(void) {
  platform_write_stdout("phase4: running xml tests\n");

  test_xml_namespaces();
  test_xml_writer();
  test_parse_odt_content("examples/blank.odt");
  test_parse_odt_content("examples/test.odt");

  if (tests_failed != 0) {
    char buf[32];
    usize n;
    platform_write_stdout("phase4: failed tests = ");
    n = rt_u64_to_dec((u64)tests_failed, buf, sizeof(buf));
    rt_write_all(1, buf, n);
    platform_write_stdout("\n");
    return 1;
  }

  platform_write_stdout("phase4: all tests passed\n");
  return 0;
}
