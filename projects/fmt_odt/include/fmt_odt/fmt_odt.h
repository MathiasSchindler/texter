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
#define FMT_ODT_MAX_TABLE_ROWS 32768
#define FMT_ODT_MAX_TABLE_CELLS 262144

typedef struct fmt_odt_state {
  convert_format_handler handler;
  const u8* template_data;
  usize template_len;
  int use_template;
  doc_model_block blocks[FMT_ODT_MAX_BLOCKS];
  doc_model_block nested_blocks[FMT_ODT_MAX_NESTED_BLOCKS];
  doc_model_block list_item_blocks[FMT_ODT_MAX_LIST_ITEMS];
  doc_model_inline inlines[FMT_ODT_MAX_INLINES];
  doc_model_inline aux_inlines[FMT_ODT_MAX_AUX_INLINES];
  doc_model_table_row table_rows[FMT_ODT_MAX_TABLE_ROWS];
  doc_model_table_cell table_cells[FMT_ODT_MAX_TABLE_CELLS];
  char text[FMT_ODT_MAX_TEXT];
  char plain[FMT_ODT_MAX_TEXT];
  usize block_count;
  usize nested_block_count;
  usize list_item_count;
  usize inline_count;
  usize aux_inline_count;
  usize table_row_count;
  usize table_cell_count;
  usize text_len;
} fmt_odt_state;

void fmt_odt_state_init(fmt_odt_state* state);
void fmt_odt_set_template(fmt_odt_state* state, const u8* template_data, usize template_len);
void fmt_odt_clear_template(fmt_odt_state* state);
int fmt_odt_register(convert_registry* registry, fmt_odt_state* state);

#endif
