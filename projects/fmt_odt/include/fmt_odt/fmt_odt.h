#ifndef FMT_ODT_FMT_ODT_H
#define FMT_ODT_FMT_ODT_H

#include "convert_core/convert_core.h"
#include "odt_core/odt_core.h"

#define FMT_ODT_MAX_BLOCKS 131072
#define FMT_ODT_MAX_NESTED_BLOCKS 262144
#define FMT_ODT_MAX_LIST_ITEMS 262144
#define FMT_ODT_MAX_INLINES 262144
#define FMT_ODT_MAX_AUX_INLINES 524288
#define FMT_ODT_MAX_TEXT ODT_CORE_MAX_CONTENT_XML_BYTES

typedef struct fmt_odt_state {
  convert_format_handler handler;
  doc_model_block blocks[FMT_ODT_MAX_BLOCKS];
  doc_model_block nested_blocks[FMT_ODT_MAX_NESTED_BLOCKS];
  doc_model_block list_item_blocks[FMT_ODT_MAX_LIST_ITEMS];
  doc_model_inline inlines[FMT_ODT_MAX_INLINES];
  doc_model_inline aux_inlines[FMT_ODT_MAX_AUX_INLINES];
  char text[FMT_ODT_MAX_TEXT];
  char plain[FMT_ODT_MAX_TEXT];
  usize block_count;
  usize nested_block_count;
  usize list_item_count;
  usize inline_count;
  usize aux_inline_count;
  usize text_len;
} fmt_odt_state;

void fmt_odt_state_init(fmt_odt_state* state);
int fmt_odt_register(convert_registry* registry, fmt_odt_state* state);

#endif
