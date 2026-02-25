#ifndef FMT_MARKDOWN_FMT_MARKDOWN_H
#define FMT_MARKDOWN_FMT_MARKDOWN_H

#include "convert_core/convert_core.h"

#define FMT_MARKDOWN_MAX_BLOCKS 80000
#define FMT_MARKDOWN_MAX_NESTED_BLOCKS 160000
#define FMT_MARKDOWN_MAX_INLINES 160000
#define FMT_MARKDOWN_MAX_AUX_INLINES 240000
#define FMT_MARKDOWN_MAX_TEXT (4U * 1024U * 1024U)
#define FMT_MARKDOWN_MAX_TABLE_ROWS 40000
#define FMT_MARKDOWN_MAX_TABLE_CELLS 320000
#define FMT_MARKDOWN_MAX_TABLE_CELL_BLOCKS 320000

typedef struct fmt_markdown_state {
  convert_format_handler handler;
  doc_model_block blocks[FMT_MARKDOWN_MAX_BLOCKS];
  doc_model_block nested_blocks[FMT_MARKDOWN_MAX_NESTED_BLOCKS];
  doc_model_inline inlines[FMT_MARKDOWN_MAX_INLINES];
  doc_model_inline aux_inlines[FMT_MARKDOWN_MAX_AUX_INLINES];
  doc_model_table_row table_rows[FMT_MARKDOWN_MAX_TABLE_ROWS];
  doc_model_table_cell table_cells[FMT_MARKDOWN_MAX_TABLE_CELLS];
  doc_model_block table_cell_blocks[FMT_MARKDOWN_MAX_TABLE_CELL_BLOCKS];
  char text[FMT_MARKDOWN_MAX_TEXT];
  char storage[FMT_MARKDOWN_MAX_TEXT];
  usize block_count;
  usize nested_block_count;
  usize inline_count;
  usize aux_inline_count;
  usize table_row_count;
  usize table_cell_count;
  usize table_cell_block_count;
  usize text_len;
  usize storage_len;
} fmt_markdown_state;

void fmt_markdown_state_init(fmt_markdown_state* state);
int fmt_markdown_register(convert_registry* registry, fmt_markdown_state* state);

#endif
