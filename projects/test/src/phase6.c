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

static void test_validate_and_inspect(void) {
  const char* argv_validate[] = {"odt_cli", "validate", "examples/blank.odt"};
  const char* argv_inspect[] = {"odt_cli", "inspect", "examples/blank.odt"};

  CHECK(odt_cli_run(3, argv_validate) == 0, "cli validate blank.odt");
  CHECK(odt_cli_run(3, argv_inspect) == 0, "cli inspect blank.odt");
}

static void test_extract_text_file(void) {
  static u8 out[262144];
  usize out_len = 0;
  const char* argv_extract[] = {
      "odt_cli", "extract-text", "examples/test.odt", "build/phase6_extract.txt"};

  CHECK(odt_cli_run(4, argv_extract) == 0, "cli extract-text to file");
  CHECK(read_file_all("build/phase6_extract.txt", out, sizeof(out), &out_len) == 0,
        "read extracted text file");
  CHECK(out_len > 0, "extracted text non-empty");
}

static void test_create_and_validate(void) {
  static const char in_txt[] = "Hello from odt_cli\nSecond paragraph\n";
  static u8 out_odt[262144];
  static u8 out_txt[262144];
  usize out_odt_len = 0;
  usize out_txt_len = 0;

  const char* argv_create[] = {
      "odt_cli", "create", "build/phase6_input.txt", "build/phase6_output.odt"};
  const char* argv_validate[] = {"odt_cli", "validate", "build/phase6_output.odt"};
  const char* argv_extract[] = {
      "odt_cli", "extract-text", "build/phase6_output.odt", "build/phase6_output.txt"};

  CHECK(write_file_all("build/phase6_input.txt", (const u8*)in_txt, rt_strlen(in_txt)) == 0,
        "write create input text file");
  CHECK(odt_cli_run(4, argv_create) == 0, "cli create output odt");
  CHECK(odt_cli_run(3, argv_validate) == 0, "cli validate created odt");
  CHECK(read_file_all("build/phase6_output.odt", out_odt, sizeof(out_odt), &out_odt_len) == 0,
        "read created odt bytes");
  CHECK(odt_core_validate_package(out_odt, out_odt_len) == ODT_OK,
        "core validates created odt");

  CHECK(odt_cli_run(4, argv_extract) == 0, "cli extract-text from created odt");
  CHECK(read_file_all("build/phase6_output.txt", out_txt, sizeof(out_txt), &out_txt_len) == 0,
        "read extracted created text");
  CHECK(bytes_contains(out_txt, out_txt_len, "Hello from odt_cli", 18),
        "created text contains first line");
  CHECK(bytes_contains(out_txt, out_txt_len, "Second paragraph", 16),
        "created text contains second line");
}

static void test_repack(void) {
  static u8 src_odt[262144];
  static u8 repacked_odt[262144];
  static u8 repacked_txt[262144];
  usize src_len = 0;
  usize repacked_len = 0;
  usize repacked_txt_len = 0;
  zip_archive src_za;
  zip_archive repacked_za;
  u16 src_count = 0;
  u16 repacked_count = 0;

  const char* argv_repack[] = {
  "odt_cli", "repack", "examples/test.odt", "build/phase6_repacked.odt"};
  const char* argv_validate[] = {"odt_cli", "validate", "build/phase6_repacked.odt"};
  const char* argv_extract[] = {
  "odt_cli", "extract-text", "build/phase6_repacked.odt", "build/phase6_repacked.txt"};

  CHECK(odt_cli_run(4, argv_repack) == 0, "cli repack test.odt");
  CHECK(odt_cli_run(3, argv_validate) == 0, "cli validate repacked odt");

  CHECK(read_file_all("examples/test.odt", src_odt, sizeof(src_odt), &src_len) == 0,
    "read source odt for repack compare");
  CHECK(read_file_all("build/phase6_repacked.odt", repacked_odt, sizeof(repacked_odt),
          &repacked_len) == 0,
    "read repacked odt bytes");

  CHECK(zip_archive_open(&src_za, src_odt, src_len) == ZIP_OK, "open source odt zip");
  CHECK(zip_archive_open(&repacked_za, repacked_odt, repacked_len) == ZIP_OK,
    "open repacked odt zip");
  CHECK(zip_archive_entry_count(&src_za, &src_count) == ZIP_OK, "source entry count");
  CHECK(zip_archive_entry_count(&repacked_za, &repacked_count) == ZIP_OK,
    "repacked entry count");
  CHECK(src_count == repacked_count, "entry count preserved after repack");

  CHECK(odt_cli_run(4, argv_extract) == 0, "cli extract-text from repacked odt");
  CHECK(read_file_all("build/phase6_repacked.txt", repacked_txt, sizeof(repacked_txt),
          &repacked_txt_len) == 0,
    "read repacked extracted text");
  CHECK(bytes_contains(repacked_txt, repacked_txt_len, "This is a test.", 15),
    "repacked text contains expected sentence");
}

static void test_convert(void) {
  static const char md_input[] =
      "## Converted\n"
      "{.Heading_20_2}\n"
      "From *markdown* with **strong**, `code`, [link](https://example.com), ![alt](img.png), {.TCode|styled}\n"
      "- first\n"
      "- second\n"
      "\n"
      "| H1 | H2 |\n"
      "| --- | --- |\n"
      "| A *x* | B |\n"
      "{.TableStyle}\n"
      ":::note\n"
      "Use extension directives for fidelity.\n"
      ":::\n"
      ":::figure Pictures/sample.png\n"
      "Figure caption text\n"
      ":::\n";
  static const char invalid_template_text[] = "this is not an odt zip package\n";
    static const char valid_meltdown_template[] =
      "profile: eu-oj\n"
      "version: 1\n"
      "roles:\n"
      "  Title:\n"
        "  RecitalIntro:\n"
      "  Recital:\n"
        "regions:\n"
        "  Recitals:\n"
        "    start:\n"
        "      - RecitalIntro\n"
      "recognize:\n"
      "  Title:\n"
      "    match:\n"
      "      - heading(level=1)\n"
      "    where:\n"
        "      - position == first\n"
        "      - before RecitalIntro\n"
      "    priority: 100\n"
        "  RecitalIntro:\n"
        "    match:\n"
        "      - paragraph\n"
        "    where:\n"
        "      - startswith \"Whereas:\"\n"
        "    priority: 60\n"
      "  Recital:\n"
      "    match:\n"
      "      - paragraph\n"
      "    where:\n"
      "      - regex '^\\\\(\\\\d+\\\\)'\n"
        "      - in Recitals\n"
      "    priority: 50\n"
      "layout:\n"
      "  Title:\n"
      "    style: OJTitle\n"
        "  RecitalIntro:\n"
        "    style: OJRecitalIntro\n"
      "  Recital:\n"
      "    style: OJRecital\n";
    static const char invalid_meltdown_template[] =
      "profile: bad\n"
      "version: 1\n"
      "roles:\n"
      "  Title:\n"
      "recognize:\n"
      "  Title:\n"
      "    match:\n"
      "      - heading(level=1)\n"
      "    where:\n"
      "      - regex [unterminated\n"
      "    priority: 100\n"
      "layout:\n"
      "  Title:\n"
      "    style: OJTitle\n";
  static u8 md_roundtrip[262144];
  static u8 converted_odt[262144];
  static u8 converted_content[131072];
  usize md_roundtrip_len = 0;
  usize converted_odt_len = 0;
  usize converted_content_len = 0;
  static u8 md_to_md[262144];
  usize md_to_md_len = 0;
  static u8 templated_odt[262144];
  static u8 templated_manifest[32768];
  usize templated_odt_len = 0;
  usize templated_manifest_len = 0;
  zip_archive templated_za;
  zip_entry_view templated_settings;
  zip_entry_view templated_styles;
  zip_entry_view templated_manifest_entry;
  zip_archive converted_za;
  zip_entry_view converted_content_entry;

  const char* argv_md_to_odt[] = {
      "odt_cli", "convert", "--from", "md", "--to", "odt", "build/phase6_convert.md",
      "build/phase6_convert.odt"};
    const char* argv_md_to_odt_diag_json[] = {
      "odt_cli", "convert", "--from", "md", "--to", "odt", "--diag-json",
      "build/phase6_convert.md", "build/phase6_convert_diag_json.odt"};
  const char* argv_validate[] = {"odt_cli", "validate", "build/phase6_convert.odt"};
  const char* argv_odt_to_md[] = {
      "odt_cli", "convert", "--from", "odt", "--to", "md", "examples/test.odt",
      "build/phase6_convert_out.md"};
  const char* argv_md_to_md[] = {
      "odt_cli", "convert", "--from", "md", "--to", "md", "build/phase6_convert.md",
      "build/phase6_convert_md_roundtrip.md"};
  const char* argv_md_to_odt_template[] = {
      "odt_cli", "convert", "--from", "md", "--to", "odt", "--template", "examples/blank.odt",
      "build/phase6_convert.md", "build/phase6_convert_templated.odt"};
    const char* argv_md_to_odt_template_diag_json[] = {
      "odt_cli", "convert", "--from", "md", "--to", "odt", "--template", "examples/blank.odt",
      "--diag-json", "build/phase6_convert.md", "build/phase6_convert_templated_diag_json.odt"};
    const char* argv_validate_templated[] = {
      "odt_cli", "validate", "build/phase6_convert_templated.odt"};
  const char* argv_md_to_md_template[] = {
      "odt_cli", "convert", "--from", "md", "--to", "md", "--template", "examples/blank.odt",
      "build/phase6_convert.md", "build/phase6_convert_md_template.md"};
    const char* argv_md_to_odt_template_missing[] = {
      "odt_cli", "convert", "--from", "md", "--to", "odt", "--template", "build/no_such_template.odt",
      "build/phase6_convert.md", "build/phase6_convert_templated_missing.odt"};
    const char* argv_md_to_odt_template_invalid[] = {
      "odt_cli", "convert", "--from", "md", "--to", "odt", "--template", "build/phase6_invalid_template.odt",
      "build/phase6_convert.md", "build/phase6_convert_templated_invalid.odt"};
  const char* argv_md_to_odt_template_meltdown_valid[] = {
      "odt_cli", "convert", "--from", "md", "--to", "odt", "--template", "build/phase6_valid_template.meltdown",
      "build/phase6_convert.md", "build/phase6_convert_meltdown_valid.odt"};
  const char* argv_md_to_odt_template_meltdown_invalid[] = {
      "odt_cli", "convert", "--from", "md", "--to", "odt", "--template", "build/phase6_invalid_template.meltdown",
      "build/phase6_convert.md", "build/phase6_convert_meltdown_invalid.odt"};

  CHECK(write_file_all("build/phase6_convert.md", (const u8*)md_input, rt_strlen(md_input)) == 0,
        "write convert markdown input file");
    CHECK(write_file_all("build/phase6_invalid_template.odt",
             (const u8*)invalid_template_text,
             rt_strlen(invalid_template_text)) == 0,
      "write invalid template fixture file");
  CHECK(write_file_all("build/phase6_valid_template.meltdown",
                       (const u8*)valid_meltdown_template,
                       rt_strlen(valid_meltdown_template)) == 0,
        "write valid meltdown template fixture file");
  CHECK(write_file_all("build/phase6_invalid_template.meltdown",
                       (const u8*)invalid_meltdown_template,
                       rt_strlen(invalid_meltdown_template)) == 0,
        "write invalid meltdown template fixture file");

  CHECK(odt_cli_run(8, argv_md_to_odt) == 0, "cli convert md->odt");
    CHECK(odt_cli_run(9, argv_md_to_odt_diag_json) == 0,
      "cli convert md->odt with --diag-json");
  CHECK(odt_cli_run(3, argv_validate) == 0, "cli validate converted odt");
    CHECK(read_file_all("build/phase6_convert.odt", converted_odt, sizeof(converted_odt), &converted_odt_len) == 0,
      "read converted odt for style checks");
    CHECK(zip_archive_open(&converted_za, converted_odt, converted_odt_len) == ZIP_OK,
      "open converted odt zip for style checks");
    CHECK(zip_archive_find_entry(&converted_za, "content.xml", &converted_content_entry) == ZIP_OK,
      "find content.xml in converted odt");
    CHECK(zip_entry_extract(&converted_content_entry,
            converted_content,
            sizeof(converted_content),
            &converted_content_len) == ZIP_OK,
      "extract content.xml for style checks");
    CHECK(bytes_contains(converted_content,
             converted_content_len,
             "table:style-name=\"TableStyle\"",
             29),
      "md->odt applies block style override for table");

  CHECK(odt_cli_run(8, argv_md_to_md) == 0, "cli convert md->md");
  CHECK(read_file_all("build/phase6_convert_md_roundtrip.md", md_to_md, sizeof(md_to_md),
                      &md_to_md_len) == 0,
        "read converted md->md output");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "## Converted", 12),
        "md->md keeps heading level");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "{.Heading_20_2}", 15),
        "md->md keeps heading style attr");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "*markdown*", 10), "md->md keeps emphasis");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "**strong**", 10), "md->md keeps strong");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "`code`", 6), "md->md keeps code span");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "[link](https://example.com)", 27),
        "md->md keeps link");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "![alt](img.png)", 15), "md->md keeps image");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "- first", 7), "md->md keeps list item 1");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "- second", 8), "md->md keeps list item 2");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "{.TCode|styled}", 15),
        "md->md keeps inline style span");
  CHECK(bytes_contains(md_to_md, md_to_md_len, ":::note", 7),
        "md->md keeps admonition directive");
  CHECK(bytes_contains(md_to_md, md_to_md_len, ":::figure Pictures/sample.png", 28),
        "md->md keeps figure directive");
  CHECK(bytes_contains(md_to_md, md_to_md_len, "Figure caption text", 19),
        "md->md keeps figure caption");

    CHECK(odt_cli_run(10, argv_md_to_odt_template) == 0, "cli convert md->odt with template");
    CHECK(odt_cli_run(11, argv_md_to_odt_template_diag_json) == 0,
      "cli convert md->odt with template and --diag-json");
    CHECK(odt_cli_run(3, argv_validate_templated) == 0, "cli validate templated converted odt");
    CHECK(read_file_all("build/phase6_convert_templated.odt",
            templated_odt,
            sizeof(templated_odt),
            &templated_odt_len) == 0,
      "read templated converted odt");
    CHECK(zip_archive_open(&templated_za, templated_odt, templated_odt_len) == ZIP_OK,
      "open templated converted odt zip");
    CHECK(zip_archive_find_entry(&templated_za, "settings.xml", &templated_settings) == ZIP_OK,
      "templated output keeps settings.xml from template");
    CHECK(zip_archive_find_entry(&templated_za, "styles.xml", &templated_styles) == ZIP_OK,
      "templated output keeps styles.xml from template");
      CHECK(zip_archive_find_entry(&templated_za, "META-INF/manifest.xml", &templated_manifest_entry) == ZIP_OK,
        "templated output contains manifest.xml");
      CHECK(zip_entry_extract(&templated_manifest_entry,
              templated_manifest,
              sizeof(templated_manifest),
              &templated_manifest_len) == ZIP_OK,
        "extract templated output manifest.xml");
      CHECK(bytes_contains(templated_manifest,
               templated_manifest_len,
               "manifest:full-path=\"content.xml\"",
               32),
        "templated output manifest contains content.xml entry");
      CHECK(bytes_contains(templated_manifest,
               templated_manifest_len,
               "manifest:full-path=\"settings.xml\"",
               33),
        "templated output manifest contains settings.xml entry");
  CHECK(odt_cli_run(10, argv_md_to_md_template) != 0,
        "cli convert rejects --template when --to is not odt");
    CHECK(odt_cli_run(10, argv_md_to_odt_template_missing) != 0,
      "cli convert fails when template path is missing");
    CHECK(odt_cli_run(10, argv_md_to_odt_template_invalid) != 0,
      "cli convert fails when template is not a valid odt");
  CHECK(odt_cli_run(10, argv_md_to_odt_template_meltdown_valid) == 9,
        "cli convert validates meltdown profile in M0 and returns parser-only status");
  CHECK(odt_cli_run(10, argv_md_to_odt_template_meltdown_invalid) == 8,
        "cli convert rejects malformed meltdown profile");

  CHECK(odt_cli_run(8, argv_odt_to_md) == 0, "cli convert odt->md");
  CHECK(read_file_all("build/phase6_convert_out.md", md_roundtrip, sizeof(md_roundtrip),
                      &md_roundtrip_len) == 0,
        "read converted markdown output");
  CHECK(bytes_contains(md_roundtrip, md_roundtrip_len, "This is a test.", 15),
        "converted markdown contains expected text");
}

int phase6_run(void) {
  platform_write_stdout("phase6: running odt_cli tests\n");

  test_validate_and_inspect();
  test_extract_text_file();
  test_create_and_validate();
  test_repack();
  test_convert();

  if (tests_failed != 0) {
    char buf[32];
    usize n;
    platform_write_stdout("phase6: failed tests = ");
    n = rt_u64_to_dec((u64)tests_failed, buf, sizeof(buf));
    rt_write_all(1, buf, n);
    platform_write_stdout("\n");
    return 1;
  }

  platform_write_stdout("phase6: all tests passed\n");
  return 0;
}
