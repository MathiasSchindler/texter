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
  usize links;
  usize notes;
  usize placeholders;
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
  out->links = bytes_count(buf, n, "](", 2);
  out->notes = bytes_count(buf, n, "[note:", 6);
  out->placeholders = bytes_count(buf, n, "[odt:unsupported ", 17);
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

static void run_standard_doc_case(const char* id, const char* odt_path, usize min_headings) {
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
  CHECK(m0.placeholders <= 64, "phase11: placeholder cap");

  CHECK(odt_cli_run(8, argv_md_to_odt) == 0, "phase11: convert md->odt");
  CHECK(odt_cli_run(3, argv_validate_rt) == 0, "phase11: validate roundtrip odt");
  CHECK(odt_cli_run(8, argv_convert2) == 0, "phase11: convert roundtrip odt->md");
  CHECK(collect_md_metrics(md2_path, &m1) == 0, "phase11: collect roundtrip metrics");

  CHECK(m1.bytes > 0, "phase11: roundtrip md non-empty");
  CHECK(abs_diff(m0.headings, m1.headings) <= 64, "phase11: heading drift");
  CHECK(abs_diff(m0.tables, m1.tables) <= 64, "phase11: table drift");
  CHECK(abs_diff(m0.links, m1.links) <= 1024, "phase11: link drift");

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
                        4);
  run_standard_doc_case("part2",
                        "standard/part2-packages/OpenDocument-v1.4-os-part2-packages.odt",
                        20);
  run_standard_doc_case("part3",
                        "standard/part3-schema/OpenDocument-v1.4-os-part3-schema.odt",
                        100);
  run_standard_doc_case("part4",
                        "standard/part4-formula/OpenDocument-v1.4-os-part4-formula.odt",
                        100);

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
