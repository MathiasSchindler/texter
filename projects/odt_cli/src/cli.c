#include "odt_cli/cli.h"

#include "convert_core/convert_core.h"
#include "fmt_markdown/fmt_markdown.h"
#include "fmt_odt/fmt_odt.h"
#include "odt_core/odt_core.h"
#include "rt/rt.h"
#include "zip/zip.h"

#define CLI_MAX_FILE_BYTES (4U * 1024U * 1024U)

static void write_str(const char* s) {
  rt_write_all(1, s, rt_strlen(s));
}

static void write_str_err(const char* s) {
  rt_write_all(2, s, rt_strlen(s));
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
    write_str_err("error: failed to read input file\n");
    return 2;
  }
  rc = odt_core_validate_package(odt, n);
  if (rc != ODT_OK) {
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
  if (odt_core_extract_plain_text(odt, odt_len, txt, sizeof(txt), &txt_len) != ODT_OK) {
    write_str_err("error: failed to extract text\n");
    return 3;
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
  static u8 entry_buf[CLI_MAX_FILE_BYTES];
  zip_archive za;
  zip_writer zw;
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
    usize data_len = 0;
    u16 out_method;
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

    if (zip_entry_extract(&ze, entry_buf, sizeof(entry_buf), &data_len) != ZIP_OK) {
      write_str_err("error: failed to extract input entry\n");
      return 9;
    }

    out_method = (ze.method == ZIP_METHOD_STORE) ? ZIP_METHOD_STORE : ZIP_METHOD_DEFLATE;
    if (zip_writer_add_entry(&zw, name_buf, entry_buf, data_len, out_method) != ZIP_OK) {
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
                       const char* in_path,
                       const char* out_path) {
  static u8 in_buf[CLI_MAX_FILE_BYTES];
  static u8 out_buf[CLI_MAX_FILE_BYTES];
  static fmt_markdown_state md_state;
  static fmt_odt_state odt_state;
  convert_registry registry;
  convert_session session;
  convert_diagnostics diags;
  doc_model_document doc;
  convert_request req;
  usize in_len = 0;
  usize out_len = 0;
  int rc;

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
    print_diagnostics(&diags);
    return 5;
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
    if (argc != 8) {
      print_usage();
      return 1;
    }
    if (!cstr_eq(argv[2], "--from") || !cstr_eq(argv[4], "--to")) {
      print_usage();
      return 1;
    }
    return cmd_convert(argv[3], argv[5], argv[6], argv[7]);
  }

  print_usage();
  return 1;
}
