#include "odt_cli/cli.h"

#include "convert_core/convert_core.h"
#include "fmt_markdown/fmt_markdown.h"
#include "fmt_odt/fmt_odt.h"
#include "odt_core/odt_core.h"
#include "rt/rt.h"
#include "zip/zip.h"

#ifndef CLI_MAX_FILE_BYTES
#define CLI_MAX_FILE_BYTES (32U * 1024U * 1024U)
#endif

static void write_str(const char* s) {
  rt_write_all(1, s, rt_strlen(s));
}

static void write_str_err(const char* s) {
  rt_write_all(2, s, rt_strlen(s));
}

static void write_u64_fd(int fd, u64 v) {
  char buf[32];
  usize n = rt_u64_to_dec(v, buf, sizeof(buf));
  rt_write_all(fd, buf, n);
}

static void write_u64(u64 v) {
  char buf[32];
  usize n = rt_u64_to_dec(v, buf, sizeof(buf));
  rt_write_all(1, buf, n);
}

static int cstr_eq(const char* a, const char* b) {
  usize i = 0;
  while (a[i] != '\0' && b[i] != '\0') {
    if (a[i] != b[i]) {
      return 0;
    }
    i++;
  }
  return a[i] == '\0' && b[i] == '\0';
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

static void print_usage(void) {
  write_str("usage:\n");
  write_str("  odt_cli validate <in.odt>\n");
  write_str("  odt_cli inspect <in.odt>\n");
  write_str("  odt_cli extract-text <in.odt> [out.txt]\n");
  write_str("  odt_cli create <in.txt> <out.odt>\n");
  write_str("  odt_cli repack <in.odt> <out.odt>\n");
  write_str("  odt_cli convert --from <fmt> --to <fmt> <in> <out>\n");
  write_str("  odt_cli convert --from <fmt> --to odt --template <template.odt> <in> <out>\n");
  write_str("  odt_cli convert --from <fmt> --to <fmt> [--template <template.odt>] [--diag-json] <in> <out>\n");
}

static const char* diag_severity_name(convert_diag_severity s) {
  if (s == CONVERT_DIAG_ERROR) {
    return "error";
  }
  if (s == CONVERT_DIAG_WARN) {
    return "warn";
  }
  return "info";
}

static void print_json_string_err(const char* s) {
  usize i = 0;
  if (s == (const char*)0) {
    rt_write_all(2, "null", 4);
    return;
  }
  rt_write_all(2, "\"", 1);
  while (s[i] != '\0') {
    char c = s[i++];
    if (c == '\\') {
      rt_write_all(2, "\\\\", 2);
    } else if (c == '"') {
      rt_write_all(2, "\\\"", 2);
    } else if (c == '\n') {
      rt_write_all(2, "\\n", 2);
    } else if (c == '\r') {
      rt_write_all(2, "\\r", 2);
    } else if (c == '\t') {
      rt_write_all(2, "\\t", 2);
    } else if ((unsigned char)c < 32U) {
      rt_write_all(2, "?", 1);
    } else {
      rt_write_all(2, &c, 1);
    }
  }
  rt_write_all(2, "\"", 1);
}

static void print_diagnostics_json(const convert_diagnostics* diags) {
  usize i;
  usize errors = 0;
  usize warns = 0;
  usize infos = 0;

  if (diags == 0) {
    write_str_err("{\"errors\":0,\"warnings\":0,\"infos\":0,\"count\":0,\"dropped\":0,\"items\":[]}\n");
    return;
  }

  for (i = 0; i < diags->count; i++) {
    if (diags->items[i].severity == CONVERT_DIAG_ERROR) {
      errors++;
    } else if (diags->items[i].severity == CONVERT_DIAG_WARN) {
      warns++;
    } else {
      infos++;
    }
  }

  write_str_err("{\"errors\":");
  write_u64_fd(2, (u64)errors);
  write_str_err(",\"warnings\":");
  write_u64_fd(2, (u64)warns);
  write_str_err(",\"infos\":");
  write_u64_fd(2, (u64)infos);
  write_str_err(",\"count\":");
  write_u64_fd(2, (u64)diags->count);
  write_str_err(",\"dropped\":");
  write_u64_fd(2, (u64)diags->dropped_count);
  write_str_err(",\"items\":[");

  for (i = 0; i < diags->count; i++) {
    if (i > 0) {
      write_str_err(",");
    }
    write_str_err("{\"severity\":");
    print_json_string_err(diag_severity_name(diags->items[i].severity));
    write_str_err(",\"stage\":");
    write_u64_fd(2, (u64)diags->items[i].stage);
    write_str_err(",\"code\":");
    write_u64_fd(2, (u64)diags->items[i].code);
    write_str_err(",\"message\":");
    print_json_string_err(diags->items[i].message);
    write_str_err("}");
  }
  write_str_err("]}\n");
}

static void print_diagnostics(const convert_diagnostics* diags) {
  usize i;
  if (diags == 0) {
    return;
  }

  for (i = 0; i < diags->count; i++) {
    write_str_err("diag: ");
    if (diags->items[i].severity == CONVERT_DIAG_ERROR) {
      write_str_err("error");
    } else if (diags->items[i].severity == CONVERT_DIAG_WARN) {
      write_str_err("warn");
    } else {
      write_str_err("info");
    }
    write_str_err(": ");
    if (diags->items[i].message != (const char*)0) {
      write_str_err(diags->items[i].message);
    } else {
      write_str_err("(no message)");
    }
    write_str_err("\n");
  }
}

static int cmd_validate(const char* in_path) {
  static u8 odt[CLI_MAX_FILE_BYTES];
  usize n = 0;
  int rc;
  rc = read_file_all(in_path, odt, sizeof(odt), &n);
  if (rc != 0) {
    if (rc == -3) {
      write_str_err("error: input file exceeds configured CLI_MAX_FILE_BYTES\n");
    } else {
      write_str_err("error: failed to read input file\n");
    }
    return 2;
  }
  rc = odt_core_validate_package(odt, n);
  if (rc != ODT_OK) {
    if (rc == ODT_ERR_TOO_LARGE) {
      write_str_err("invalid: package metadata too large for current limits\n");
      return 3;
    }
    write_str_err("invalid: package validation failed\n");
    return 3;
  }
  write_str("ok\n");
  return 0;
}

static int cmd_inspect(const char* in_path) {
  static u8 odt[CLI_MAX_FILE_BYTES];
  zip_archive za;
  u16 n = 0;
  u16 i;
  usize len = 0;

  if (read_file_all(in_path, odt, sizeof(odt), &len) != 0) {
    write_str_err("error: failed to read input file\n");
    return 2;
  }
  if (zip_archive_open(&za, odt, len) != ZIP_OK) {
    write_str_err("error: not a valid zip/odt\n");
    return 3;
  }

  write_str("file: ");
  write_str(in_path);
  write_str("\nsize: ");
  write_u64((u64)len);
  write_str("\n");

  if (odt_core_validate_package(odt, len) == ODT_OK) {
    write_str("package: valid odt core rules\n");
  } else {
    write_str("package: invalid odt core rules\n");
  }

  zip_archive_entry_count(&za, &n);
  write_str("entries: ");
  write_u64((u64)n);
  write_str("\n");

  for (i = 0; i < n; i++) {
    zip_entry_view ze;
    if (zip_archive_get_entry(&za, i, &ze) != ZIP_OK) {
      write_str_err("error: failed while scanning entries\n");
      return 4;
    }
    rt_write_all(1, "  - ", 4);
    rt_write_all(1, ze.name, ze.name_len);
    rt_write_all(1, " (method=", 9);
    write_u64((u64)ze.method);
    rt_write_all(1, ", size=", 7);
    write_u64((u64)ze.uncomp_size);
    rt_write_all(1, ")\n", 2);
  }

  return 0;
}

static int cmd_extract_text(const char* in_path, const char* out_path_or_null) {
  static u8 odt[CLI_MAX_FILE_BYTES];
  static char txt[CLI_MAX_FILE_BYTES];
  usize odt_len = 0;
  usize txt_len = 0;

  if (read_file_all(in_path, odt, sizeof(odt), &odt_len) != 0) {
    write_str_err("error: failed to read input file\n");
    return 2;
  }
  {
    int rc = odt_core_extract_plain_text(odt, odt_len, txt, sizeof(txt), &txt_len);
    if (rc == ODT_ERR_TOO_LARGE) {
      write_str_err("error: content.xml too large for current limits\n");
      return 3;
    }
    if (rc != ODT_OK) {
      write_str_err("error: failed to extract text\n");
      return 3;
    }
  }

  if (out_path_or_null != (const char*)0) {
    if (write_file_all(out_path_or_null, (const u8*)txt, txt_len) != 0) {
      write_str_err("error: failed to write output text\n");
      return 4;
    }
    write_str("ok\n");
    return 0;
  }

  rt_write_all(1, txt, txt_len);
  return 0;
}

static int cmd_create(const char* in_txt, const char* out_odt) {
  static u8 txt[CLI_MAX_FILE_BYTES];
  static u8 odt[CLI_MAX_FILE_BYTES];
  usize txt_len = 0;
  usize odt_len = 0;

  if (read_file_all(in_txt, txt, sizeof(txt), &txt_len) != 0) {
    write_str_err("error: failed to read input text\n");
    return 2;
  }

  if (odt_core_build_minimal((const char*)txt, txt_len, odt, sizeof(odt), &odt_len) != ODT_OK) {
    write_str_err("error: failed to build odt\n");
    return 3;
  }

  if (write_file_all(out_odt, odt, odt_len) != 0) {
    write_str_err("error: failed to write odt output\n");
    return 4;
  }

  write_str("ok\n");
  return 0;
}

static int cmd_repack(const char* in_odt, const char* out_odt) {
  static u8 in_buf[CLI_MAX_FILE_BYTES];
  static u8 out_buf[CLI_MAX_FILE_BYTES];
  static zip_writer zw;
  zip_archive za;
  usize in_len = 0;
  usize out_len = 0;
  u16 entry_count = 0;
  u16 i;

  if (read_file_all(in_odt, in_buf, sizeof(in_buf), &in_len) != 0) {
    write_str_err("error: failed to read input odt\n");
    return 2;
  }
  if (odt_core_validate_package(in_buf, in_len) != ODT_OK) {
    write_str_err("error: input is not a valid odt package\n");
    return 3;
  }
  if (zip_archive_open(&za, in_buf, in_len) != ZIP_OK) {
    write_str_err("error: failed to open input archive\n");
    return 4;
  }
  if (zip_archive_entry_count(&za, &entry_count) != ZIP_OK) {
    write_str_err("error: failed to read entry count\n");
    return 5;
  }

  zip_writer_init(&zw, out_buf, sizeof(out_buf));
  for (i = 0; i < entry_count; i++) {
    zip_entry_view ze;
    char name_buf[ZIP_WRITER_MAX_NAME + 1];
    usize j;

    if (zip_archive_get_entry(&za, i, &ze) != ZIP_OK) {
      write_str_err("error: failed to read input entry\n");
      return 6;
    }
    if (zip_entry_name_is_safe(&ze) != ZIP_OK) {
      write_str_err("error: unsafe input entry path\n");
      return 7;
    }
    if (ze.name_len > ZIP_WRITER_MAX_NAME) {
      write_str_err("error: entry name too long\n");
      return 8;
    }

    for (j = 0; j < ze.name_len; j++) {
      name_buf[j] = ze.name[j];
    }
    name_buf[ze.name_len] = '\0';

    if (zip_writer_add_raw_entry(&zw,
                                 name_buf,
                                 ze.method,
                                 ze.crc32,
                                 ze.comp_size,
                                 ze.uncomp_size,
                                 ze.archive_data + ze.data_pos,
                                 (usize)ze.comp_size) != ZIP_OK) {
      write_str_err("error: failed to write output entry\n");
      return 10;
    }
  }

  if (zip_writer_finish(&zw, &out_len) != ZIP_OK) {
    write_str_err("error: failed to finalize output archive\n");
    return 11;
  }
  if (odt_core_validate_package(out_buf, out_len) != ODT_OK) {
    write_str_err("error: repacked output failed validation\n");
    return 12;
  }
  if (write_file_all(out_odt, out_buf, out_len) != 0) {
    write_str_err("error: failed to write output odt\n");
    return 13;
  }

  write_str("ok\n");
  return 0;
}

static int cmd_convert(const char* from_fmt,
                       const char* to_fmt,
                       const char* template_path_or_null,
                       int diag_json,
                       const char* in_path,
                       const char* out_path) {
  static u8 in_buf[CLI_MAX_FILE_BYTES];
  static u8 out_buf[CLI_MAX_FILE_BYTES];
  static u8 template_buf[CLI_MAX_FILE_BYTES];
  static fmt_markdown_state md_state;
  static fmt_odt_state odt_state;
  convert_registry registry;
  convert_session session;
  convert_diagnostics diags;
  doc_model_document doc;
  convert_request req;
  usize in_len = 0;
  usize out_len = 0;
  usize template_len = 0;
  int rc;

  if (template_path_or_null != (const char*)0) {
    if (!cstr_eq(to_fmt, "odt")) {
      write_str_err("error: --template is only valid with --to odt\n");
      return 7;
    }

    rc = read_file_all(template_path_or_null, template_buf, sizeof(template_buf), &template_len);
    if (rc != 0) {
      if (rc == -3) {
        write_str_err("error: template file exceeds configured CLI_MAX_FILE_BYTES\n");
      } else {
        write_str_err("error: failed to read template file\n");
      }
      return 7;
    }

    if (odt_core_validate_package(template_buf, template_len) != ODT_OK) {
      write_str_err("error: template file is not a valid odt package\n");
      return 7;
    }
  }

  if (read_file_all(in_path, in_buf, sizeof(in_buf), &in_len) != 0) {
    write_str_err("error: failed to read conversion input\n");
    return 2;
  }

  convert_registry_init(&registry);
  if (fmt_markdown_register(&registry, &md_state) != CONVERT_OK ||
      fmt_odt_register(&registry, &odt_state) != CONVERT_OK) {
    write_str_err("error: failed to initialize conversion adapters\n");
    return 3;
  }

  if (template_path_or_null != (const char*)0) {
    fmt_odt_set_template(&odt_state, template_buf, template_len);
  } else {
    fmt_odt_clear_template(&odt_state);
  }

  if (convert_session_init(&session, &doc, &diags) != CONVERT_OK) {
    write_str_err("error: failed to initialize conversion session\n");
    return 4;
  }

  req.from_format = from_fmt;
  req.to_format = to_fmt;
  req.input = in_buf;
  req.input_len = in_len;
  req.output = out_buf;
  req.output_cap = sizeof(out_buf);
  req.output_len = &out_len;
  req.policy = CONVERT_POLICY_LOSSY;

  rc = convert_core_run_with_registry(&req, &registry, &session);
  if (rc != CONVERT_OK) {
    write_str_err("error: conversion failed\n");
    if (diag_json) {
      print_diagnostics_json(&diags);
    } else {
      print_diagnostics(&diags);
    }
    return 5;
  }

  if (diag_json) {
    print_diagnostics_json(&diags);
  }

  if (write_file_all(out_path, out_buf, out_len) != 0) {
    write_str_err("error: failed to write conversion output\n");
    return 6;
  }

  write_str("ok\n");
  return 0;
}

int odt_cli_run(int argc, const char** argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  if (cstr_eq(argv[1], "validate")) {
    if (argc != 3) {
      print_usage();
      return 1;
    }
    return cmd_validate(argv[2]);
  }

  if (cstr_eq(argv[1], "inspect")) {
    if (argc != 3) {
      print_usage();
      return 1;
    }
    return cmd_inspect(argv[2]);
  }

  if (cstr_eq(argv[1], "extract-text")) {
    if (argc == 3) {
      return cmd_extract_text(argv[2], (const char*)0);
    }
    if (argc == 4) {
      return cmd_extract_text(argv[2], argv[3]);
    }
    print_usage();
    return 1;
  }

  if (cstr_eq(argv[1], "create")) {
    if (argc != 4) {
      print_usage();
      return 1;
    }
    return cmd_create(argv[2], argv[3]);
  }

  if (cstr_eq(argv[1], "repack")) {
    if (argc != 4) {
      print_usage();
      return 1;
    }
    return cmd_repack(argv[2], argv[3]);
  }

  if (cstr_eq(argv[1], "convert")) {
    const char* from_fmt = (const char*)0;
    const char* to_fmt = (const char*)0;
    const char* template_path = (const char*)0;
    const char* in_path = (const char*)0;
    const char* out_path = (const char*)0;
    int diag_json = 0;
    int i = 2;

    if (argc < 8) {
      print_usage();
      return 1;
    }

    if (!cstr_eq(argv[i], "--from") || i + 1 >= argc) {
      print_usage();
      return 1;
    }
    from_fmt = argv[i + 1];
    i += 2;

    if (i + 1 >= argc || !cstr_eq(argv[i], "--to")) {
      print_usage();
      return 1;
    }
    to_fmt = argv[i + 1];
    i += 2;

    while (i < argc - 2) {
      if (cstr_eq(argv[i], "--template")) {
        if (i + 1 >= argc - 1 || template_path != (const char*)0) {
          print_usage();
          return 1;
        }
        template_path = argv[i + 1];
        i += 2;
        continue;
      }
      if (cstr_eq(argv[i], "--diag-json")) {
        diag_json = 1;
        i += 1;
        continue;
      }
      print_usage();
      return 1;
    }

    if (i + 2 != argc) {
      print_usage();
      return 1;
    }

    in_path = argv[i];
    out_path = argv[i + 1];
    return cmd_convert(from_fmt, to_fmt, template_path, diag_json, in_path, out_path);
  }

  print_usage();
  return 1;
}
