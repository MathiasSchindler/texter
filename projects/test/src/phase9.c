#include "phase_api.h"
#include "convert_core/convert_core.h"
#include "platform/platform.h"

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

typedef struct fake_adapter_config {
  int import_fail;
  int export_fail;
  int emit_warning;
} fake_adapter_config;

static int fake_import_ok(const convert_format_handler* handler,
                          const u8* input,
                          usize input_len,
                          convert_policy_mode policy,
                          convert_doc_buffer* doc,
                          convert_diagnostics* diags) {
  static const doc_model_inline title_inlines[] = {
      {.kind = DOC_MODEL_INLINE_TEXT,
       .style_id = {0, 0},
       .as.text = {.text = {"Title", 5}}}};
  static const doc_model_block blocks[] = {
      {.kind = DOC_MODEL_BLOCK_HEADING,
       .style_id = {0, 0},
       .as.heading = {.level = 1, .inlines = {.items = title_inlines, .count = 1}}}};

  fake_adapter_config* cfg = (fake_adapter_config*)handler->user;

  (void)input;
  (void)input_len;
  (void)policy;

  if (cfg != 0 && cfg->import_fail) {
    return CONVERT_ERR_UNSUPPORTED;
  }

  doc->doc->metadata.title.data = "Doc";
  doc->doc->metadata.title.len = 3;
  doc->doc->metadata.author.data = 0;
  doc->doc->metadata.author.len = 0;
  doc->doc->metadata.language.data = "en";
  doc->doc->metadata.language.len = 2;
  doc->doc->metadata.created_unix_s = 0;
  doc->doc->metadata.modified_unix_s = 0;
  doc->doc->blocks.items = blocks;
  doc->doc->blocks.count = 1;
  doc->doc->styles = 0;
  doc->doc->style_count = 0;
  doc->doc->assets = 0;
  doc->doc->asset_count = 0;
  doc->doc->extensions = 0;
  doc->doc->extension_count = 0;

  if (cfg != 0 && cfg->emit_warning) {
    (void)convert_diagnostics_push(diags,
                                   CONVERT_DIAG_WARN,
                                   CONVERT_DIAG_LOSSY_DROP,
                                   CONVERT_STAGE_NORMALIZE_IN,
                                   "dropped unsupported token");
  }

  return CONVERT_OK;
}

static int fake_export_ok(const convert_format_handler* handler,
                          const convert_doc_buffer* doc,
                          convert_policy_mode policy,
                          u8* output,
                          usize output_cap,
                          usize* output_len,
                          convert_diagnostics* diags) {
  const char payload[] = "ok";
  fake_adapter_config* cfg = (fake_adapter_config*)handler->user;

  (void)policy;
  (void)doc;
  (void)diags;

  if (cfg != 0 && cfg->export_fail) {
    return CONVERT_ERR_UNSUPPORTED;
  }

  if (output_cap < sizeof(payload) - 1) {
    return CONVERT_ERR_INVALID;
  }

  output[0] = (u8)payload[0];
  output[1] = (u8)payload[1];
  *output_len = sizeof(payload) - 1;
  return CONVERT_OK;
}

static void test_pipeline_ok(void) {
  fake_adapter_config import_cfg = {0, 0, 0};
  fake_adapter_config export_cfg = {0, 0, 0};
  convert_format_handler importer;
  convert_format_handler exporter;
  convert_request req;
  convert_doc_buffer doc_buf;
  convert_diagnostics diags;
  doc_model_document doc;
  u8 out[16];
  usize out_len = 0;
  static const u8 in[] = {'#', ' ', 'x'};
  int rc;

  importer.name = "md";
  importer.import_doc = fake_import_ok;
  importer.export_doc = 0;
  importer.user = &import_cfg;

  exporter.name = "odt";
  exporter.import_doc = 0;
  exporter.export_doc = fake_export_ok;
  exporter.user = &export_cfg;

  req.from_format = "md";
  req.to_format = "odt";
  req.input = in;
  req.input_len = sizeof(in);
  req.output = out;
  req.output_cap = sizeof(out);
  req.output_len = &out_len;
  req.policy = CONVERT_POLICY_STRICT;

  doc_buf.doc = &doc;
  diags.count = 0;
  diags.dropped_count = 0;

  rc = convert_core_run(&req, &importer, &exporter, &doc_buf, &diags);
  CHECK(rc == CONVERT_OK, "convert pipeline strict success");
  CHECK(out_len == 2, "convert output len");
  CHECK(out[0] == (u8)'o' && out[1] == (u8)'k', "convert output bytes");
}

static void test_pipeline_ok_with_session(void) {
  fake_adapter_config import_cfg = {0, 0, 0};
  fake_adapter_config export_cfg = {0, 0, 0};
  convert_format_handler importer;
  convert_format_handler exporter;
  convert_request req;
  convert_session session;
  convert_diagnostics diags;
  doc_model_document doc;
  u8 out[16];
  usize out_len = 0;
  int rc;

  importer.name = "md";
  importer.import_doc = fake_import_ok;
  importer.export_doc = 0;
  importer.user = &import_cfg;

  exporter.name = "odt";
  exporter.import_doc = 0;
  exporter.export_doc = fake_export_ok;
  exporter.user = &export_cfg;

  req.from_format = "md";
  req.to_format = "odt";
  req.input = 0;
  req.input_len = 0;
  req.output = out;
  req.output_cap = sizeof(out);
  req.output_len = &out_len;
  req.policy = CONVERT_POLICY_STRICT;

  rc = convert_session_init(&session, &doc, &diags);
  CHECK(rc == CONVERT_OK, "session init should succeed");

  rc = convert_core_run_session(&req, &importer, &exporter, &session);
  CHECK(rc == CONVERT_OK, "convert session run success");
  CHECK(out_len == 2, "session output len");
}

static void test_registry_lookup_and_run(void) {
  fake_adapter_config import_cfg = {0, 0, 0};
  fake_adapter_config export_cfg = {0, 0, 0};
  convert_format_handler importer;
  convert_format_handler exporter;
  convert_registry registry;
  convert_request req;
  convert_session session;
  convert_diagnostics diags;
  doc_model_document doc;
  u8 out[16];
  usize out_len = 0;
  int rc;

  importer.name = "md";
  importer.import_doc = fake_import_ok;
  importer.export_doc = 0;
  importer.user = &import_cfg;

  exporter.name = "odt";
  exporter.import_doc = 0;
  exporter.export_doc = fake_export_ok;
  exporter.user = &export_cfg;

  convert_registry_init(&registry);
  rc = convert_registry_register(&registry, &importer);
  CHECK(rc == CONVERT_OK, "register importer");
  rc = convert_registry_register(&registry, &exporter);
  CHECK(rc == CONVERT_OK, "register exporter");
  rc = convert_registry_register(&registry, &importer);
  CHECK(rc == CONVERT_ERR_INVALID, "duplicate handler name should fail");

  CHECK(convert_registry_find(&registry, "md") == &importer, "find md handler");
  CHECK(convert_registry_find(&registry, "odt") == &exporter, "find odt handler");
  CHECK(convert_registry_find(&registry, "docx") == 0, "missing handler returns null");

  req.from_format = "md";
  req.to_format = "odt";
  req.input = 0;
  req.input_len = 0;
  req.output = out;
  req.output_cap = sizeof(out);
  req.output_len = &out_len;
  req.policy = CONVERT_POLICY_STRICT;

  rc = convert_session_init(&session, &doc, &diags);
  CHECK(rc == CONVERT_OK, "session init for registry run");

  rc = convert_core_run_with_registry(&req, &registry, &session);
  CHECK(rc == CONVERT_OK, "registry-based run success");

  req.to_format = "html";
  rc = convert_core_run_with_registry(&req, &registry, &session);
  CHECK(rc == CONVERT_ERR_NOT_FOUND, "registry-based run with missing handler fails");
}

static void test_invalid_request(void) {
  convert_format_handler importer;
  convert_format_handler exporter;
  convert_request req;
  convert_doc_buffer doc_buf;
  convert_diagnostics diags;
  doc_model_document doc;
  u8 out[16];
  usize out_len = 0;
  int rc;

  importer.name = "md";
  importer.import_doc = fake_import_ok;
  importer.export_doc = 0;
  importer.user = 0;

  exporter.name = "odt";
  exporter.import_doc = 0;
  exporter.export_doc = fake_export_ok;
  exporter.user = 0;

  req.from_format = "txt";
  req.to_format = "odt";
  req.input = 0;
  req.input_len = 0;
  req.output = out;
  req.output_cap = sizeof(out);
  req.output_len = &out_len;
  req.policy = CONVERT_POLICY_STRICT;

  doc_buf.doc = &doc;
  diags.count = 0;
  diags.dropped_count = 0;

  rc = convert_core_run(&req, &importer, &exporter, &doc_buf, &diags);
  CHECK(rc == CONVERT_ERR_INVALID, "mismatched request/importer must fail");
  CHECK(diags.count > 0, "invalid request should produce diagnostic");
}

static void test_lossy_warning_allowed(void) {
  fake_adapter_config import_cfg = {0, 0, 1};
  fake_adapter_config export_cfg = {0, 0, 0};
  convert_format_handler importer;
  convert_format_handler exporter;
  convert_request req;
  convert_doc_buffer doc_buf;
  convert_diagnostics diags;
  doc_model_document doc;
  u8 out[16];
  usize out_len = 0;
  int rc;

  importer.name = "md";
  importer.import_doc = fake_import_ok;
  importer.export_doc = 0;
  importer.user = &import_cfg;

  exporter.name = "odt";
  exporter.import_doc = 0;
  exporter.export_doc = fake_export_ok;
  exporter.user = &export_cfg;

  req.from_format = "md";
  req.to_format = "odt";
  req.input = 0;
  req.input_len = 0;
  req.output = out;
  req.output_cap = sizeof(out);
  req.output_len = &out_len;
  req.policy = CONVERT_POLICY_LOSSY;

  doc_buf.doc = &doc;
  diags.count = 0;
  diags.dropped_count = 0;

  rc = convert_core_run(&req, &importer, &exporter, &doc_buf, &diags);
  CHECK(rc == CONVERT_OK, "lossy mode allows warnings");
  CHECK(diags.count > 0, "lossy warning should be present");
}

static void test_roundtrip_safe_warning_allowed(void) {
  fake_adapter_config import_cfg = {0, 0, 1};
  fake_adapter_config export_cfg = {0, 0, 0};
  convert_format_handler importer;
  convert_format_handler exporter;
  convert_request req;
  convert_doc_buffer doc_buf;
  convert_diagnostics diags;
  doc_model_document doc;
  u8 out[16];
  usize out_len = 0;
  int rc;

  importer.name = "odt";
  importer.import_doc = fake_import_ok;
  importer.export_doc = 0;
  importer.user = &import_cfg;

  exporter.name = "md";
  exporter.import_doc = 0;
  exporter.export_doc = fake_export_ok;
  exporter.user = &export_cfg;

  req.from_format = "odt";
  req.to_format = "md";
  req.input = 0;
  req.input_len = 0;
  req.output = out;
  req.output_cap = sizeof(out);
  req.output_len = &out_len;
  req.policy = CONVERT_POLICY_ROUNDTRIP_SAFE;

  doc_buf.doc = &doc;
  diags.count = 0;
  diags.dropped_count = 0;

  rc = convert_core_run(&req, &importer, &exporter, &doc_buf, &diags);
  CHECK(rc == CONVERT_OK, "roundtrip-safe mode allows warnings");
}

static void test_handler_failure_propagates(void) {
  fake_adapter_config import_cfg = {1, 0, 0};
  fake_adapter_config export_cfg = {0, 0, 0};
  convert_format_handler importer;
  convert_format_handler exporter;
  convert_request req;
  convert_doc_buffer doc_buf;
  convert_diagnostics diags;
  doc_model_document doc;
  u8 out[16];
  usize out_len = 0;
  int rc;

  importer.name = "md";
  importer.import_doc = fake_import_ok;
  importer.export_doc = 0;
  importer.user = &import_cfg;

  exporter.name = "odt";
  exporter.import_doc = 0;
  exporter.export_doc = fake_export_ok;
  exporter.user = &export_cfg;

  req.from_format = "md";
  req.to_format = "odt";
  req.input = 0;
  req.input_len = 0;
  req.output = out;
  req.output_cap = sizeof(out);
  req.output_len = &out_len;
  req.policy = CONVERT_POLICY_STRICT;

  doc_buf.doc = &doc;
  diags.count = 0;
  diags.dropped_count = 0;

  rc = convert_core_run(&req, &importer, &exporter, &doc_buf, &diags);
  CHECK(rc == CONVERT_ERR_UNSUPPORTED, "import failure propagates");
  CHECK(diags.count > 0, "import failure emits diagnostic");
}

int phase9_run(void) {
  platform_write_stdout("phase9: running convert_core tests\n");

  test_pipeline_ok();
  test_pipeline_ok_with_session();
  test_registry_lookup_and_run();
  test_invalid_request();
  test_lossy_warning_allowed();
  test_roundtrip_safe_warning_allowed();
  test_handler_failure_propagates();

  if (tests_failed != 0) {
    platform_write_stdout("phase9: tests failed\n");
    return 1;
  }

  platform_write_stdout("phase9: all tests passed\n");
  return 0;
}
