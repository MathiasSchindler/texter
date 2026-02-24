#ifndef FMT_MARKDOWN_FMT_MARKDOWN_H
#define FMT_MARKDOWN_FMT_MARKDOWN_H

#include "convert_core/convert_core.h"

#define FMT_MARKDOWN_MAX_BLOCKS 256
#define FMT_MARKDOWN_MAX_NESTED_BLOCKS 512
#define FMT_MARKDOWN_MAX_INLINES 1024
#define FMT_MARKDOWN_MAX_AUX_INLINES 1024
#define FMT_MARKDOWN_MAX_TEXT 262144

typedef struct fmt_markdown_state {
  convert_format_handler handler;
  doc_model_block blocks[FMT_MARKDOWN_MAX_BLOCKS];
  doc_model_block nested_blocks[FMT_MARKDOWN_MAX_NESTED_BLOCKS];
  doc_model_inline inlines[FMT_MARKDOWN_MAX_INLINES];
  doc_model_inline aux_inlines[FMT_MARKDOWN_MAX_AUX_INLINES];
  char text[FMT_MARKDOWN_MAX_TEXT];
  char storage[FMT_MARKDOWN_MAX_TEXT];
  usize block_count;
  usize nested_block_count;
  usize inline_count;
  usize aux_inline_count;
  usize text_len;
  usize storage_len;
} fmt_markdown_state;

void fmt_markdown_state_init(fmt_markdown_state* state);
int fmt_markdown_register(convert_registry* registry, fmt_markdown_state* state);

#endif
