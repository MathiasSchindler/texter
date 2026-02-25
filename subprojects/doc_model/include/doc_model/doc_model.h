#ifndef DOC_MODEL_DOC_MODEL_H
#define DOC_MODEL_DOC_MODEL_H

#include "rt/types.h"

#define DOC_MODEL_OK 0
#define DOC_MODEL_ERR_INVALID (-1)
#define DOC_MODEL_ERR_LIMIT (-2)

#define DOC_MODEL_MAX_VALIDATE_DEPTH 64

typedef struct doc_model_sv {
  const char* data;
  usize len;
} doc_model_sv;

typedef enum doc_model_validation_code {
  DOC_MODEL_VALIDATION_NONE = 0,
  DOC_MODEL_VALIDATION_NULL_POINTER = 1,
  DOC_MODEL_VALIDATION_EMPTY_REQUIRED = 2,
  DOC_MODEL_VALIDATION_BAD_ENUM = 3,
  DOC_MODEL_VALIDATION_BAD_HEADING_LEVEL = 4,
  DOC_MODEL_VALIDATION_BAD_LINK = 5,
  DOC_MODEL_VALIDATION_BAD_IMAGE = 6,
  DOC_MODEL_VALIDATION_BAD_TABLE = 7,
  DOC_MODEL_VALIDATION_BAD_STYLE = 8,
  DOC_MODEL_VALIDATION_DEPTH_LIMIT = 9
} doc_model_validation_code;

typedef struct doc_model_validation_error {
  doc_model_validation_code code;
  const char* field;
  usize index;
} doc_model_validation_error;

typedef struct doc_model_metadata {
  doc_model_sv title;
  doc_model_sv author;
  doc_model_sv language;
  i64 created_unix_s;
  i64 modified_unix_s;
} doc_model_metadata;

typedef enum doc_model_block_kind {
  DOC_MODEL_BLOCK_PARAGRAPH = 1,
  DOC_MODEL_BLOCK_HEADING = 2,
  DOC_MODEL_BLOCK_LIST = 3,
  DOC_MODEL_BLOCK_LIST_ITEM = 4,
  DOC_MODEL_BLOCK_QUOTE = 5,
  DOC_MODEL_BLOCK_CODE_BLOCK = 6,
  DOC_MODEL_BLOCK_TABLE = 7,
  DOC_MODEL_BLOCK_ADMONITION = 8,
  DOC_MODEL_BLOCK_FIGURE = 9
} doc_model_block_kind;

typedef enum doc_model_inline_kind {
  DOC_MODEL_INLINE_TEXT = 1,
  DOC_MODEL_INLINE_EMPHASIS = 2,
  DOC_MODEL_INLINE_STRONG = 3,
  DOC_MODEL_INLINE_CODE_SPAN = 4,
  DOC_MODEL_INLINE_LINK = 5,
  DOC_MODEL_INLINE_IMAGE = 6,
  DOC_MODEL_INLINE_LINE_BREAK = 7
} doc_model_inline_kind;

typedef enum doc_model_style_target {
  DOC_MODEL_STYLE_BLOCK = 1,
  DOC_MODEL_STYLE_INLINE = 2
} doc_model_style_target;

typedef enum doc_model_asset_kind {
  DOC_MODEL_ASSET_EMBEDDED = 1,
  DOC_MODEL_ASSET_EXTERNAL = 2
} doc_model_asset_kind;

typedef struct doc_model_inline doc_model_inline;
typedef struct doc_model_block doc_model_block;

typedef struct doc_model_inline_list {
  const doc_model_inline* items;
  usize count;
} doc_model_inline_list;

typedef struct doc_model_block_list {
  const doc_model_block* items;
  usize count;
} doc_model_block_list;

typedef struct doc_model_table_cell {
  doc_model_block_list blocks;
  u8 is_header;
} doc_model_table_cell;

typedef struct doc_model_table_row {
  const doc_model_table_cell* cells;
  usize cell_count;
} doc_model_table_row;

struct doc_model_inline {
  doc_model_inline_kind kind;
  doc_model_sv style_id;

  union {
    struct {
      doc_model_sv text;
    } text;
    struct {
      doc_model_inline_list children;
    } emphasis;
    struct {
      doc_model_inline_list children;
    } strong;
    struct {
      doc_model_sv text;
    } code_span;
    struct {
      doc_model_sv href;
      doc_model_sv title;
      doc_model_inline_list children;
    } link;
    struct {
      doc_model_sv asset_id;
      doc_model_sv alt;
    } image;
  } as;
};

struct doc_model_block {
  doc_model_block_kind kind;
  doc_model_sv style_id;

  union {
    struct {
      doc_model_inline_list inlines;
    } paragraph;
    struct {
      u8 level;
      doc_model_inline_list inlines;
    } heading;
    struct {
      u8 ordered;
      doc_model_block_list items;
    } list;
    struct {
      doc_model_block_list blocks;
    } list_item;
    struct {
      doc_model_block_list blocks;
    } quote;
    struct {
      doc_model_sv language;
      doc_model_sv text;
    } code_block;
    struct {
      const doc_model_table_row* rows;
      usize row_count;
    } table;
    struct {
      doc_model_sv kind;
      doc_model_block_list blocks;
    } admonition;
    struct {
      doc_model_sv asset_id;
      doc_model_sv caption;
    } figure;
  } as;
};

typedef struct doc_model_style {
  doc_model_sv id;
  doc_model_sv parent_id;
  doc_model_sv role;
  doc_model_style_target target;
} doc_model_style;

typedef struct doc_model_asset {
  doc_model_sv id;
  doc_model_asset_kind kind;
  doc_model_sv mime_type;
  doc_model_sv source_uri;
  const u8* bytes;
  usize bytes_len;
} doc_model_asset;

typedef struct doc_model_extension_payload {
  doc_model_sv namespace_uri;
  doc_model_sv key;
  doc_model_sv data;
} doc_model_extension_payload;

typedef struct doc_model_document {
  doc_model_metadata metadata;
  doc_model_block_list blocks;
  const doc_model_style* styles;
  usize style_count;
  const doc_model_asset* assets;
  usize asset_count;
  const doc_model_extension_payload* extensions;
  usize extension_count;
} doc_model_document;

int doc_model_validate_metadata(const doc_model_metadata* metadata,
                                doc_model_validation_error* err);
int doc_model_validate_blocks(const doc_model_block* blocks,
                              usize count,
                              doc_model_validation_error* err);
int doc_model_validate_styles(const doc_model_style* styles,
                              usize count,
                              doc_model_validation_error* err);
int doc_model_validate_assets(const doc_model_asset* assets,
                              usize count,
                              doc_model_validation_error* err);
int doc_model_validate_document(const doc_model_document* doc,
                                doc_model_validation_error* err);

#endif
