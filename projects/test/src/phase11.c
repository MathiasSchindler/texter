#include "phase_api.h"
#include "odt_cli/cli.h"
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

typedef struct md_metrics {
  usize bytes;
  usize headings;
  usize lists;
  usize tables;
  usize table_sep_lines;
  usize code_fence_lines;
  usize links;
  usize notes;
  usize footnote_refs;
  usize footnote_defs;
  usize placeholders;
  usize max_non_url_line;
  usize raw_xml_like_tokens;
  usize toc_refheading_links;
  usize stage_labels;
  usize note_quote_lines;
  usize table_caption_interleaves;
  usize table_consecutive_separators;
} md_metrics;

static usize append_cstr(char* dst, usize cap, usize pos, const char* src) {
  usize i = 0;
  while (src[i] != '\0' && pos + 1 < cap) {
    dst[pos++] = src[i++];
  }
  if (cap > 0) {
    dst[pos < cap ? pos : (cap - 1)] = '\0';
  }
  return pos;
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

static usize line_count_prefix(const u8* buf, usize len, const char* pfx, usize pfx_len) {
  usize i = 0;
  usize c = 0;
  usize line_start = 1;

  while (i < len) {
    if (line_start && i + pfx_len <= len) {
      usize j;
      int match = 1;
      for (j = 0; j < pfx_len; j++) {
        if (buf[i + j] != (u8)pfx[j]) {
          match = 0;
          break;
        }
      }
      if (match) {
        c++;
      }
    }
    if (buf[i] == (u8)'\n') {
      line_start = 1;
    } else {
      line_start = 0;
    }
    i++;
  }

  return c;
}

static int line_has_url(const u8* buf, usize start, usize end) {
  usize i;
  if (end <= start) {
    return 0;
  }
  for (i = start; i + 7 <= end; i++) {
    if (buf[i] == (u8)'h' && buf[i + 1] == (u8)'t' && buf[i + 2] == (u8)'t' &&
        buf[i + 3] == (u8)'p' && buf[i + 4] == (u8)':' && buf[i + 5] == (u8)'/' &&
        buf[i + 6] == (u8)'/') {
      return 1;
    }
  }
  for (i = start; i + 8 <= end; i++) {
    if (buf[i] == (u8)'h' && buf[i + 1] == (u8)'t' && buf[i + 2] == (u8)'t' &&
        buf[i + 3] == (u8)'p' && buf[i + 4] == (u8)'s' && buf[i + 5] == (u8)':' &&
        buf[i + 6] == (u8)'/' && buf[i + 7] == (u8)'/') {
      return 1;
    }
  }
  return 0;
}

static int line_starts_with_structured_marker(const u8* buf, usize start, usize end) {
  usize i = start;
  if (end <= start) {
    return 0;
  }
  while (i < end && buf[i] == (u8)' ') {
    i++;
  }
  if (i >= end) {
    return 0;
  }
  if (buf[i] == (u8)'#' || buf[i] == (u8)'|' || buf[i] == (u8)'>' || buf[i] == (u8)'`' ||
      buf[i] == (u8)':') {
    return 1;
  }
  if (buf[i] == (u8)'-' || buf[i] == (u8)'*') {
    return (i + 1 < end && buf[i + 1] == (u8)' ');
  }
  if (buf[i] >= (u8)'0' && buf[i] <= (u8)'9') {
    usize j = i;
    while (j < end && buf[j] >= (u8)'0' && buf[j] <= (u8)'9') {
      j++;
    }
    if (j + 1 < end && buf[j] == (u8)'.' && buf[j + 1] == (u8)' ') {
      return 1;
    }
  }
  return 0;
}

static usize max_plain_line_len_excluding_urls(const u8* buf, usize len) {
  usize i = 0;
  usize line_start = 0;
  usize max_len = 0;
  int in_code_fence = 0;
  while (i <= len) {
    if (i == len || buf[i] == (u8)'\n') {
      usize line_end = i;
      usize line_len = line_end - line_start;
      if (!in_code_fence && !line_starts_with_structured_marker(buf, line_start, line_end) &&
          !line_has_url(buf, line_start, line_end) && line_len > max_len) {
        max_len = line_len;
      }
      if (line_end >= line_start + 3 && buf[line_start] == (u8)'`' && buf[line_start + 1] == (u8)'`' &&
          buf[line_start + 2] == (u8)'`') {
        in_code_fence = !in_code_fence;
      }
      line_start = i + 1;
    }
    i++;
  }
  return max_len;
}

static int is_xml_name_char(u8 c) {
  return (c >= (u8)'a' && c <= (u8)'z') || (c >= (u8)'A' && c <= (u8)'Z') ||
         (c >= (u8)'0' && c <= (u8)'9') || c == (u8)':' || c == (u8)'_' || c == (u8)'-';
}

static usize count_raw_xml_like_tokens(const u8* buf, usize len) {
  usize i;
  usize count = 0;
  for (i = 0; i + 2 < len; i++) {
    if (buf[i] != (u8)'<') {
      continue;
    }
    if (i > 0 && buf[i - 1] == (u8)'\\') {
      continue;
    }
    if (!is_xml_name_char(buf[i + 1])) {
      continue;
    }
    {
      usize j = i + 1;
      while (j < len && is_xml_name_char(buf[j])) {
        j++;
      }
      if (j < len && buf[j] == (u8)'>') {
        count++;
        i = j;
      }
    }
  }
  return count;
}

static usize count_footnote_definitions(const u8* buf, usize len) {
  usize i = 0;
  usize line_start = 1;
  usize count = 0;
  while (i + 4 < len) {
    if (line_start && buf[i] == (u8)'[' && buf[i + 1] == (u8)'^') {
      usize j = i + 2;
      while (j < len && buf[j] >= (u8)'0' && buf[j] <= (u8)'9') {
        j++;
      }
      if (j + 2 < len && j > i + 2 && buf[j] == (u8)']' && buf[j + 1] == (u8)':' &&
          buf[j + 2] == (u8)' ') {
        count++;
      }
    }
    line_start = (buf[i] == (u8)'\n');
    i++;
  }
  return count;
}

static usize count_footnote_references(const u8* buf, usize len) {
  usize i;
  usize count = 0;
  for (i = 0; i + 3 < len; i++) {
    if (buf[i] == (u8)'[' && buf[i + 1] == (u8)'^') {
      usize j = i + 2;
      while (j < len && buf[j] >= (u8)'0' && buf[j] <= (u8)'9') {
        j++;
      }
      if (j < len && j > i + 2 && buf[j] == (u8)']') {
        if (j + 1 < len && buf[j + 1] == (u8)':') {
          continue;
        }
        count++;
      }
    }
  }
  return count;
}

static int line_starts_with_lit(const u8* line, usize len, const char* lit) {
  usize i = 0;
  while (lit[i] != '\0') {
    if (i >= len || line[i] != (u8)lit[i]) {
      return 0;
    }
    i++;
  }
  return 1;
}

static int line_contains_lit(const u8* line, usize len, const char* lit) {
  usize i;
  usize lit_len = 0;
  while (lit[lit_len] != '\0') {
    lit_len++;
  }
  if (lit_len == 0 || len < lit_len) {
    return 0;
  }
  for (i = 0; i + lit_len <= len; i++) {
    usize j;
    int match = 1;
    for (j = 0; j < lit_len; j++) {
      if (line[i + j] != (u8)lit[j]) {
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

static usize count_toc_refheading_links(const u8* buf, usize len) {
  usize i = 0;
  usize count = 0;
  usize line_start = 0;
  while (i <= len) {
    if (i == len || buf[i] == (u8)'\n') {
      usize line_end = i;
      usize line_len = line_end - line_start;
      if (line_starts_with_lit(buf + line_start, line_len, "[") &&
          line_contains_lit(buf + line_start, line_len, "](#__RefHeading")) {
        count++;
      }
      line_start = i + 1;
    }
    i++;
  }
  return count;
}

static usize count_stage_labels(const u8* buf, usize len) {
  usize i = 0;
  usize count = 0;
  usize line_start = 0;
  while (i <= len) {
    if (i == len || buf[i] == (u8)'\n') {
      usize line_end = i;
      usize line_len = line_end - line_start;
      if ((line_len == 11 && line_starts_with_lit(buf + line_start, line_len, "This stage:")) ||
          (line_len == 15 && line_starts_with_lit(buf + line_start, line_len, "Previous stage:")) ||
          (line_len == 13 && line_starts_with_lit(buf + line_start, line_len, "Latest stage:"))) {
        count++;
      }
      line_start = i + 1;
    }
    i++;
  }
  return count;
}

static int line_is_table_row(const u8* line, usize len) {
  return len >= 2 && line[0] == (u8)'|' && line[1] == (u8)' ';
}

static int line_is_table_sep(const u8* line, usize len) {
  static const char* pfx = "| --- |";
  return line_starts_with_lit(line, len, pfx);
}

static int line_is_table_caption(const u8* line, usize len) {
  return line_starts_with_lit(line, len, "Table") && line_contains_lit(line, len, " - ");
}

static void collect_table_layout_metrics(const u8* buf,
                                         usize len,
                                         usize* out_interleaves,
                                         usize* out_consecutive_seps) {
  usize i = 0;
  usize line_start = 0;
  usize interleaves = 0;
  usize consecutive_seps = 0;
  int prev_was_sep = 0;
  int prev_was_table = 0;

  while (i <= len) {
    if (i == len || buf[i] == (u8)'\n') {
      usize line_end = i;
      usize line_len = line_end - line_start;
      int is_blank = (line_len == 0);
      int is_table = line_is_table_row(buf + line_start, line_len);
      int is_sep = line_is_table_sep(buf + line_start, line_len);
      int is_caption = line_is_table_caption(buf + line_start, line_len);

      if (is_sep && prev_was_sep) {
        consecutive_seps++;
      }
      if (is_caption && prev_was_table) {
        usize ns = i + 1;
        usize ne = ns;
        while (ne < len && buf[ne] != (u8)'\n') {
          ne++;
        }
        if (ns < len && ne > ns && line_is_table_row(buf + ns, ne - ns)) {
          interleaves++;
        }
      }

      if (is_blank) {
        prev_was_sep = 0;
        prev_was_table = 0;
      } else {
        prev_was_sep = is_sep;
        prev_was_table = is_table;
      }

      line_start = i + 1;
    }
    i++;
  }

  *out_interleaves = interleaves;
  *out_consecutive_seps = consecutive_seps;
}

static int collect_md_metrics(const char* path, md_metrics* out) {
  static u8 buf[8 * 1024 * 1024];
  usize n = 0;
  if (read_file_all(path, buf, sizeof(buf), &n) != 0) {
    return -1;
  }

  out->bytes = n;
  out->headings = line_count_prefix(buf, n, "#", 1);
  out->lists = line_count_prefix(buf, n, "- ", 2) + line_count_prefix(buf, n, "1. ", 3);
  out->tables = line_count_prefix(buf, n, "```table", 8) + line_count_prefix(buf, n, "| ", 2);
  out->table_sep_lines = line_count_prefix(buf, n, "| --- |", 7);
  out->code_fence_lines = line_count_prefix(buf, n, "```", 3);
  out->links = bytes_count(buf, n, "](", 2);
  out->notes = bytes_count(buf, n, "[note:", 6);
  out->footnote_refs = count_footnote_references(buf, n);
  out->footnote_defs = count_footnote_definitions(buf, n);
  out->placeholders = bytes_count(buf, n, "[odt:unsupported ", 17);
  out->max_non_url_line = max_plain_line_len_excluding_urls(buf, n);
  out->raw_xml_like_tokens = count_raw_xml_like_tokens(buf, n);
  out->toc_refheading_links = count_toc_refheading_links(buf, n);
  out->stage_labels = count_stage_labels(buf, n);
  out->note_quote_lines = line_count_prefix(buf, n, "> Note:", 7);
  collect_table_layout_metrics(buf,
                               n,
                               &out->table_caption_interleaves,
                               &out->table_consecutive_separators);
  return 0;
}

static usize abs_diff(usize a, usize b) {
  return (a > b) ? (a - b) : (b - a);
}

static void check_metric_drift(const md_metrics* a,
                               const md_metrics* b,
                               usize num,
                               usize den,
                               const char* msg) {
  usize base = (a->bytes > 0) ? a->bytes : 1;
  usize diff = abs_diff(a->bytes, b->bytes);
  CHECK(diff * den <= base * num, msg);
}

static void run_standard_doc_case(const char* id,
                                  const char* odt_path,
                                  usize min_headings,
                                  usize min_code_fence_lines,
                                  usize min_footnote_defs,
                                  usize max_toc_refheading_links,
                                  usize min_note_quote_lines) {
  static char md_path[128];
  static char txt_path[128];
  static char repack_path[128];
  static char rt_odt_path[128];
  static char md2_path[128];
  static char msg[128];
  const char* argv_validate[3];
  const char* argv_extract[4];
  const char* argv_convert[8];
  const char* argv_repack[4];
  const char* argv_validate_repack[3];
  const char* argv_validate_rt[3];
  const char* argv_md_to_odt[8];
  const char* argv_convert2[8];
  md_metrics m0;
  md_metrics m1;

  {
    usize n = 0;
    n = append_cstr(md_path, sizeof(md_path), n, "build/phase11_");
    n = append_cstr(md_path, sizeof(md_path), n, id);
    n = append_cstr(md_path, sizeof(md_path), n, ".md");

    n = 0;
    n = append_cstr(txt_path, sizeof(txt_path), n, "build/phase11_");
    n = append_cstr(txt_path, sizeof(txt_path), n, id);
    n = append_cstr(txt_path, sizeof(txt_path), n, ".txt");

    n = 0;
    n = append_cstr(repack_path, sizeof(repack_path), n, "build/phase11_");
    n = append_cstr(repack_path, sizeof(repack_path), n, id);
    n = append_cstr(repack_path, sizeof(repack_path), n, ".repack.odt");

    n = 0;
    n = append_cstr(rt_odt_path, sizeof(rt_odt_path), n, "build/phase11_");
    n = append_cstr(rt_odt_path, sizeof(rt_odt_path), n, id);
    n = append_cstr(rt_odt_path, sizeof(rt_odt_path), n, ".rt.odt");

    n = 0;
    n = append_cstr(md2_path, sizeof(md2_path), n, "build/phase11_");
    n = append_cstr(md2_path, sizeof(md2_path), n, id);
    n = append_cstr(md2_path, sizeof(md2_path), n, ".second.md");
  }

  argv_validate[0] = "odt_cli";
  argv_validate[1] = "validate";
  argv_validate[2] = odt_path;

  argv_extract[0] = "odt_cli";
  argv_extract[1] = "extract-text";
  argv_extract[2] = odt_path;
  argv_extract[3] = txt_path;

  argv_convert[0] = "odt_cli";
  argv_convert[1] = "convert";
  argv_convert[2] = "--from";
  argv_convert[3] = "odt";
  argv_convert[4] = "--to";
  argv_convert[5] = "md";
  argv_convert[6] = odt_path;
  argv_convert[7] = md_path;

  argv_repack[0] = "odt_cli";
  argv_repack[1] = "repack";
  argv_repack[2] = odt_path;
  argv_repack[3] = repack_path;

  argv_validate_repack[0] = "odt_cli";
  argv_validate_repack[1] = "validate";
  argv_validate_repack[2] = repack_path;

  argv_md_to_odt[0] = "odt_cli";
  argv_md_to_odt[1] = "convert";
  argv_md_to_odt[2] = "--from";
  argv_md_to_odt[3] = "md";
  argv_md_to_odt[4] = "--to";
  argv_md_to_odt[5] = "odt";
  argv_md_to_odt[6] = md_path;
  argv_md_to_odt[7] = rt_odt_path;

  argv_validate_rt[0] = "odt_cli";
  argv_validate_rt[1] = "validate";
  argv_validate_rt[2] = rt_odt_path;

  argv_convert2[0] = "odt_cli";
  argv_convert2[1] = "convert";
  argv_convert2[2] = "--from";
  argv_convert2[3] = "odt";
  argv_convert2[4] = "--to";
  argv_convert2[5] = "md";
  argv_convert2[6] = rt_odt_path;
  argv_convert2[7] = md2_path;

  CHECK(odt_cli_run(3, argv_validate) == 0, "phase11: validate original");
  CHECK(odt_cli_run(4, argv_extract) == 0, "phase11: extract-text");
  CHECK(odt_cli_run(8, argv_convert) == 0, "phase11: convert odt->md");
  CHECK(odt_cli_run(4, argv_repack) == 0, "phase11: repack");
  CHECK(odt_cli_run(3, argv_validate_repack) == 0, "phase11: validate repack");

  CHECK(collect_md_metrics(md_path, &m0) == 0, "phase11: collect source metrics");
  CHECK(m0.bytes > 0, "phase11: md output non-empty");
  CHECK(m0.headings >= min_headings, "phase11: headings threshold");
  CHECK(m0.placeholders <= 8, "phase11: placeholder cap");
  CHECK(m0.max_non_url_line <= 240, "phase11: wrapped non-url lines");
  CHECK(m0.raw_xml_like_tokens <= 16, "phase11: raw xml-like token cap");
  CHECK(m0.table_sep_lines <= m0.tables + 4, "phase11: table separator normalization");
  CHECK(m0.code_fence_lines >= min_code_fence_lines, "phase11: grammar code fence minimum");
  CHECK((m0.code_fence_lines % 2) == 0, "phase11: balanced code fences");
  CHECK(m0.notes == 0, "phase11: note marker migration to footnotes");
  CHECK(m0.footnote_defs >= min_footnote_defs, "phase11: footnote definition minimum");
  CHECK(m0.footnote_refs >= m0.footnote_defs, "phase11: footnote refs >= defs");
  CHECK(m0.toc_refheading_links <= max_toc_refheading_links, "phase11: toc anchor noise cap");
  CHECK(m0.stage_labels <= 3, "phase11: stage labels deduplicated");
  CHECK(m0.note_quote_lines >= min_note_quote_lines, "phase11: note-like paragraphs mapped to quote");
  CHECK(m0.table_caption_interleaves == 0, "phase11: table caption outside table blocks");
  CHECK(m0.table_consecutive_separators == 0, "phase11: no consecutive table separator rows");

  CHECK(odt_cli_run(8, argv_md_to_odt) == 0, "phase11: convert md->odt");
  CHECK(odt_cli_run(3, argv_validate_rt) == 0, "phase11: validate roundtrip odt");
  CHECK(odt_cli_run(8, argv_convert2) == 0, "phase11: convert roundtrip odt->md");
  CHECK(collect_md_metrics(md2_path, &m1) == 0, "phase11: collect roundtrip metrics");

  CHECK(m1.bytes > 0, "phase11: roundtrip md non-empty");
  CHECK(abs_diff(m0.headings, m1.headings) <= 8, "phase11: heading drift");
  CHECK(abs_diff(m0.tables, m1.tables) <= 8, "phase11: table drift");
  CHECK(abs_diff(m0.links, m1.links) <= 32, "phase11: link drift");
  CHECK(m1.max_non_url_line <= 240, "phase11: wrapped non-url lines roundtrip");
  CHECK(m1.raw_xml_like_tokens <= 16, "phase11: raw xml-like token cap roundtrip");
  CHECK(m1.table_sep_lines <= m1.tables + 4, "phase11: table separator normalization roundtrip");
  CHECK(m1.code_fence_lines >= min_code_fence_lines, "phase11: grammar code fence minimum roundtrip");
  CHECK((m1.code_fence_lines % 2) == 0, "phase11: balanced code fences roundtrip");
  CHECK(m1.notes == 0, "phase11: note marker migration to footnotes roundtrip");
  CHECK(m1.footnote_defs >= min_footnote_defs, "phase11: footnote definition minimum roundtrip");
  CHECK(m1.footnote_refs >= m1.footnote_defs, "phase11: footnote refs >= defs roundtrip");
  CHECK(m1.toc_refheading_links <= max_toc_refheading_links, "phase11: toc anchor noise cap roundtrip");
  CHECK(m1.stage_labels <= 3, "phase11: stage labels deduplicated roundtrip");
    CHECK(m1.note_quote_lines >= min_note_quote_lines,
      "phase11: note-like paragraphs mapped to quote roundtrip");
        CHECK(m1.table_caption_interleaves == 0,
      "phase11: table caption outside table blocks roundtrip");
        CHECK(m1.table_consecutive_separators == 0,
      "phase11: no consecutive table separator rows roundtrip");

  check_metric_drift(&m0, &m1, 3, 2, "phase11: byte drift <= 150%");

  {
    usize n = 0;
    n = append_cstr(msg, sizeof(msg), n, "phase11: metrics ");
    n = append_cstr(msg, sizeof(msg), n, id);
    n = append_cstr(msg, sizeof(msg), n, " ok h=");
    {
      char num[32];
      usize d = rt_u64_to_dec((u64)m0.headings, num, sizeof(num));
      usize j;
      for (j = 0; j < d && n + 1 < sizeof(msg); j++) {
        msg[n++] = num[j];
      }
      msg[n] = '\0';
    }
    n = append_cstr(msg, sizeof(msg), n, " t=");
    {
      char num[32];
      usize d = rt_u64_to_dec((u64)m0.tables, num, sizeof(num));
      usize j;
      for (j = 0; j < d && n + 1 < sizeof(msg); j++) {
        msg[n++] = num[j];
      }
      msg[n] = '\0';
    }
    n = append_cstr(msg, sizeof(msg), n, " l=");
    {
      char num[32];
      usize d = rt_u64_to_dec((u64)m0.links, num, sizeof(num));
      usize j;
      for (j = 0; j < d && n + 1 < sizeof(msg); j++) {
        msg[n++] = num[j];
      }
      msg[n] = '\0';
    }
    (void)n;
    platform_write_stdout(msg);
    platform_write_stdout("\n");
  }
}

int phase11_run(void) {
  platform_write_stdout("phase11: running golden corpus and fidelity checks\n");

  run_standard_doc_case("part1",
                        "standard/part1-introduction/OpenDocument-v1.4-os-part1-introduction.odt",
                        4,
                        0,
                        0,
                        24,
                        0);
  run_standard_doc_case("part2",
                        "standard/part2-packages/OpenDocument-v1.4-os-part2-packages.odt",
                        20,
                        0,
                        0,
                        24,
                        4);
  run_standard_doc_case("part3",
                        "standard/part3-schema/OpenDocument-v1.4-os-part3-schema.odt",
                        100,
                        20,
                        0,
                        24,
                        12);
  run_standard_doc_case("part4",
                        "standard/part4-formula/OpenDocument-v1.4-os-part4-formula.odt",
                        100,
                        0,
                        0,
                        24,
                        8);

  if (tests_failed != 0) {
    char buf[32];
    usize n;
    platform_write_stdout("phase11: failed tests = ");
    n = rt_u64_to_dec((u64)tests_failed, buf, sizeof(buf));
    rt_write_all(1, buf, n);
    platform_write_stdout("\n");
    return 1;
  }

  platform_write_stdout("phase11: all tests passed\n");
  return 0;
}
