#include "phase_api.h"
#include "doc_model/doc_model.h"
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

static doc_model_sv sv_from_cstr(const char* s) {
  doc_model_sv sv;
  usize n = 0;
  while (s[n] != '\0') {
    n++;
  }
  sv.data = s;
  sv.len = n;
  return sv;
}

static void test_validate_document_ok(void) {
  static const doc_model_inline heading_inline[] = {
      {.kind = DOC_MODEL_INLINE_TEXT,
       .style_id = {0, 0},
       .as.text = {.text = {"Title", 5}}}};

  static const doc_model_block blocks[] = {
      {.kind = DOC_MODEL_BLOCK_HEADING,
       .style_id = {0, 0},
       .as.heading = {.level = 1,
                      .inlines = {.items = heading_inline,
                                  .count = sizeof(heading_inline) / sizeof(heading_inline[0])}}}};

  static const doc_model_style styles[] = {
      {.id = {"heading-1", 9},
       .parent_id = {0, 0},
       .role = {"heading", 7},
       .target = DOC_MODEL_STYLE_BLOCK}};

  static const u8 image_bytes[] = {1, 2, 3};
  static const doc_model_asset assets[] = {
      {.id = {"asset-1", 7},
       .kind = DOC_MODEL_ASSET_EMBEDDED,
       .mime_type = {"image/png", 9},
       .source_uri = {0, 0},
       .bytes = image_bytes,
       .bytes_len = sizeof(image_bytes)}};

  doc_model_document doc;
  doc_model_validation_error err;
  int rc;

  doc.metadata.title = sv_from_cstr("Demo");
  doc.metadata.author = sv_from_cstr("Author");
  doc.metadata.language = sv_from_cstr("en");
  doc.metadata.created_unix_s = 0;
  doc.metadata.modified_unix_s = 0;
  doc.blocks.items = blocks;
  doc.blocks.count = sizeof(blocks) / sizeof(blocks[0]);
  doc.styles = styles;
  doc.style_count = sizeof(styles) / sizeof(styles[0]);
  doc.assets = assets;
  doc.asset_count = sizeof(assets) / sizeof(assets[0]);
  doc.extensions = 0;
  doc.extension_count = 0;

  err.code = DOC_MODEL_VALIDATION_NONE;
  err.field = 0;
  err.index = 0;
  rc = doc_model_validate_document(&doc, &err);

  CHECK(rc == DOC_MODEL_OK, "valid document should pass");
}

static void test_validate_heading_level(void) {
  static const doc_model_inline inlines[] = {
      {.kind = DOC_MODEL_INLINE_TEXT,
       .style_id = {0, 0},
       .as.text = {.text = {"Bad", 3}}}};

  static const doc_model_block blocks[] = {
      {.kind = DOC_MODEL_BLOCK_HEADING,
       .style_id = {0, 0},
       .as.heading = {.level = 9,
                      .inlines = {.items = inlines,
                                  .count = sizeof(inlines) / sizeof(inlines[0])}}}};

  doc_model_validation_error err;
  int rc;

  err.code = DOC_MODEL_VALIDATION_NONE;
  err.field = 0;
  err.index = 0;
  rc = doc_model_validate_blocks(blocks, sizeof(blocks) / sizeof(blocks[0]), &err);

  CHECK(rc == DOC_MODEL_ERR_INVALID, "bad heading level should fail");
  CHECK(err.code == DOC_MODEL_VALIDATION_BAD_HEADING_LEVEL,
        "bad heading level should set validation code");
}

static void test_validate_link_and_image(void) {
  static const doc_model_inline bad_link[] = {
      {.kind = DOC_MODEL_INLINE_LINK,
       .style_id = {0, 0},
       .as.link = {.href = {0, 0}, .title = {0, 0}, .children = {0, 0}}}};

  static const doc_model_inline bad_image[] = {
      {.kind = DOC_MODEL_INLINE_IMAGE,
       .style_id = {0, 0},
       .as.image = {.asset_id = {0, 0}, .alt = {0, 0}}}};

  doc_model_validation_error err;
  int rc;

  err.code = DOC_MODEL_VALIDATION_NONE;
  err.field = 0;
  err.index = 0;
  rc = doc_model_validate_blocks(
      &(doc_model_block){.kind = DOC_MODEL_BLOCK_PARAGRAPH,
                         .style_id = {0, 0},
                         .as.paragraph = {.inlines = {.items = bad_link, .count = 1}}},
      1,
      &err);

  CHECK(rc == DOC_MODEL_ERR_INVALID, "link without href should fail");
  CHECK(err.code == DOC_MODEL_VALIDATION_BAD_LINK, "link failure code");

  err.code = DOC_MODEL_VALIDATION_NONE;
  err.field = 0;
  err.index = 0;
  rc = doc_model_validate_blocks(
      &(doc_model_block){.kind = DOC_MODEL_BLOCK_PARAGRAPH,
                         .style_id = {0, 0},
                         .as.paragraph = {.inlines = {.items = bad_image, .count = 1}}},
      1,
      &err);

  CHECK(rc == DOC_MODEL_ERR_INVALID, "image without asset id should fail");
  CHECK(err.code == DOC_MODEL_VALIDATION_BAD_IMAGE, "image failure code");
}

static void test_validate_styles_and_assets(void) {
  static const doc_model_style bad_styles[] = {
      {.id = {0, 0},
       .parent_id = {0, 0},
       .role = {"paragraph", 9},
       .target = DOC_MODEL_STYLE_BLOCK}};

  static const doc_model_asset bad_assets[] = {
      {.id = {"asset-x", 7},
       .kind = DOC_MODEL_ASSET_EXTERNAL,
       .mime_type = {"image/png", 9},
       .source_uri = {0, 0},
       .bytes = 0,
       .bytes_len = 0}};

  doc_model_validation_error err;
  int rc;

  err.code = DOC_MODEL_VALIDATION_NONE;
  err.field = 0;
  err.index = 0;
  rc = doc_model_validate_styles(bad_styles, 1, &err);
  CHECK(rc == DOC_MODEL_ERR_INVALID, "style without id should fail");
  CHECK(err.code == DOC_MODEL_VALIDATION_BAD_STYLE, "style failure code");

  err.code = DOC_MODEL_VALIDATION_NONE;
  err.field = 0;
  err.index = 0;
  rc = doc_model_validate_assets(bad_assets, 1, &err);
  CHECK(rc == DOC_MODEL_ERR_INVALID, "external asset without uri should fail");
  CHECK(err.code == DOC_MODEL_VALIDATION_EMPTY_REQUIRED, "asset failure code");
}

int phase8_run(void) {
  platform_write_stdout("phase8: running doc_model tests\n");

  test_validate_document_ok();
  test_validate_heading_level();
  test_validate_link_and_image();
  test_validate_styles_and_assets();

  if (tests_failed != 0) {
    platform_write_stdout("phase8: tests failed\n");
    return 1;
  }

  platform_write_stdout("phase8: all tests passed\n");
  return 0;
}
