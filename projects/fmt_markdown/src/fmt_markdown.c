#include "fmt_markdown/fmt_markdown.h"

#include "rt/rt.h"

static int append_out(u8* out, usize cap, usize* pos, const char* s, usize n) {
  usize i;
  if (*pos + n > cap) {
    return CONVERT_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    out[*pos + i] = (u8)s[i];
  }
  *pos += n;
  return CONVERT_OK;
}

static int append_char(u8* out, usize cap, usize* pos, char c) {
  if (*pos + 1 > cap) {
    return CONVERT_ERR_INVALID;
  }
  out[*pos] = (u8)c;
  *pos += 1;
  return CONVERT_OK;
}

static int is_digit(char c) {
  return c >= '0' && c <= '9';
}

static int is_space(char c) {
  return c == ' ' || c == '\t';
}

static int copy_sv(fmt_markdown_state* st, const char* src, usize len, doc_model_sv* out) {
  usize i;
  if (st->storage_len + len > FMT_MARKDOWN_MAX_TEXT) {
    return CONVERT_ERR_LIMIT;
  }
  out->data = st->storage + st->storage_len;
  out->len = len;
  for (i = 0; i < len; i++) {
    st->storage[st->storage_len + i] = src[i];
  }
  st->storage_len += len;
  return CONVERT_OK;
}

static int append_storage_bytes(fmt_markdown_state* st, const char* src, usize len) {
  usize i;
  if (st->storage_len + len > FMT_MARKDOWN_MAX_TEXT) {
    return CONVERT_ERR_LIMIT;
  }
  for (i = 0; i < len; i++) {
    st->storage[st->storage_len + i] = src[i];
  }
  st->storage_len += len;
  return CONVERT_OK;
}

static doc_model_inline* alloc_inline(fmt_markdown_state* st) {
  if (st->inline_count >= FMT_MARKDOWN_MAX_INLINES) {
    return 0;
  }
  return &st->inlines[st->inline_count++];
}

static doc_model_inline* alloc_aux_inline(fmt_markdown_state* st) {
  if (st->aux_inline_count >= FMT_MARKDOWN_MAX_AUX_INLINES) {
    return 0;
  }
  return &st->aux_inlines[st->aux_inline_count++];
}

static doc_model_block* alloc_block(fmt_markdown_state* st) {
  if (st->block_count >= FMT_MARKDOWN_MAX_BLOCKS) {
    return 0;
  }
  return &st->blocks[st->block_count++];
}

static doc_model_block* alloc_nested_block(fmt_markdown_state* st) {
  if (st->nested_block_count >= FMT_MARKDOWN_MAX_NESTED_BLOCKS) {
    return 0;
  }
  return &st->nested_blocks[st->nested_block_count++];
}

static void init_inline_base(doc_model_inline* inl, doc_model_inline_kind kind) {
  inl->kind = kind;
  inl->style_id.data = 0;
  inl->style_id.len = 0;
}

static usize find_substr(const char* s, usize start, usize len, const char* needle, usize needle_len) {
  usize i;
  if (needle_len == 0 || len < needle_len) {
    return len;
  }
  for (i = start; i + needle_len <= len; i++) {
    usize j;
    int match = 1;
    for (j = 0; j < needle_len; j++) {
      if (s[i + j] != needle[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      return i;
    }
  }
  return len;
}

static int parse_inline_line(fmt_markdown_state* st,
                             const char* s,
                             usize len,
                             doc_model_inline_list* out,
                             convert_diagnostics* diags) {
  usize start_inline = st->inline_count;
  usize i = 0;

  while (i < len) {
    if (i + 1 < len && s[i] == '!' && s[i + 1] == '[') {
      usize close_bracket = find_substr(s, i + 2, len, "]", 1);
      if (close_bracket + 1 < len && s[close_bracket + 1] == '(') {
        usize close_paren = find_substr(s, close_bracket + 2, len, ")", 1);
        if (close_paren < len) {
          doc_model_inline* inl = alloc_inline(st);
          if (inl == 0) {
            return CONVERT_ERR_LIMIT;
          }
          init_inline_base(inl, DOC_MODEL_INLINE_IMAGE);
          if (copy_sv(st, s + i + 2, close_bracket - (i + 2), &inl->as.image.alt) != CONVERT_OK ||
              copy_sv(st,
                      s + close_bracket + 2,
                      close_paren - (close_bracket + 2),
                      &inl->as.image.asset_id) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          i = close_paren + 1;
          continue;
        }
      }
    }

    if (s[i] == '[') {
      usize close_bracket = find_substr(s, i + 1, len, "]", 1);
      if (close_bracket + 1 < len && s[close_bracket + 1] == '(') {
        usize close_paren = find_substr(s, close_bracket + 2, len, ")", 1);
        if (close_paren < len) {
          doc_model_inline* link = alloc_inline(st);
          doc_model_inline* child = alloc_aux_inline(st);
          if (link == 0 || child == 0) {
            return CONVERT_ERR_LIMIT;
          }
          init_inline_base(child, DOC_MODEL_INLINE_TEXT);
          if (copy_sv(st, s + i + 1, close_bracket - (i + 1), &child->as.text.text) != CONVERT_OK ||
              copy_sv(st, s + close_bracket + 2, close_paren - (close_bracket + 2), &link->as.link.href) !=
                  CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          init_inline_base(link, DOC_MODEL_INLINE_LINK);
          link->as.link.title.data = 0;
          link->as.link.title.len = 0;
          link->as.link.children.items = child;
          link->as.link.children.count = 1;
          i = close_paren + 1;
          continue;
        }
      }
    }

    if (i + 1 < len && s[i] == '*' && s[i + 1] == '*') {
      usize close = find_substr(s, i + 2, len, "**", 2);
      if (close < len) {
        doc_model_inline* strong = alloc_inline(st);
        doc_model_inline* child = alloc_aux_inline(st);
        if (strong == 0 || child == 0) {
          return CONVERT_ERR_LIMIT;
        }
        init_inline_base(child, DOC_MODEL_INLINE_TEXT);
        if (copy_sv(st, s + i + 2, close - (i + 2), &child->as.text.text) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        init_inline_base(strong, DOC_MODEL_INLINE_STRONG);
        strong->as.strong.children.items = child;
        strong->as.strong.children.count = 1;
        i = close + 2;
        continue;
      }
    }

    if (s[i] == '*') {
      usize close = find_substr(s, i + 1, len, "*", 1);
      if (close < len) {
        doc_model_inline* emph = alloc_inline(st);
        doc_model_inline* child = alloc_aux_inline(st);
        if (emph == 0 || child == 0) {
          return CONVERT_ERR_LIMIT;
        }
        init_inline_base(child, DOC_MODEL_INLINE_TEXT);
        if (copy_sv(st, s + i + 1, close - (i + 1), &child->as.text.text) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        init_inline_base(emph, DOC_MODEL_INLINE_EMPHASIS);
        emph->as.emphasis.children.items = child;
        emph->as.emphasis.children.count = 1;
        i = close + 1;
        continue;
      }
    }

    if (s[i] == '`') {
      usize close = find_substr(s, i + 1, len, "`", 1);
      if (close < len && close > i + 1) {
        doc_model_inline* code = alloc_inline(st);
        if (code == 0) {
          return CONVERT_ERR_LIMIT;
        }
        init_inline_base(code, DOC_MODEL_INLINE_CODE_SPAN);
        if (copy_sv(st, s + i + 1, close - (i + 1), &code->as.code_span.text) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        i = close + 1;
        continue;
      }
    }

    {
      usize j = i;
      doc_model_inline* text_inl;
      while (j < len && !(j + 1 < len && s[j] == '!' && s[j + 1] == '[') && s[j] != '[' &&
             !(j + 1 < len && s[j] == '*' && s[j + 1] == '*') && s[j] != '*' && s[j] != '`') {
        j++;
      }
      if (j == i) {
        /* Unmatched marker: treat current char as plain text and advance. */
        j = i + 1;
      }
      text_inl = alloc_inline(st);
      if (text_inl == 0) {
        return CONVERT_ERR_LIMIT;
      }
      init_inline_base(text_inl, DOC_MODEL_INLINE_TEXT);
      if (copy_sv(st, s + i, j - i, &text_inl->as.text.text) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      i = j;
    }
  }

  out->items = &st->inlines[start_inline];
  out->count = st->inline_count - start_inline;

  if (out->count == 0) {
    (void)convert_diagnostics_push(diags,
                                   CONVERT_DIAG_WARN,
                                   CONVERT_DIAG_UNSUPPORTED_CONSTRUCT,
                                   CONVERT_STAGE_NORMALIZE_IN,
                                   "markdown import: empty inline line");
  }

  return CONVERT_OK;
}

static int parse_heading_level(const char* s, usize len, u8* out_level, usize* out_content_pos) {
  usize i = 0;
  while (i < len && s[i] == '#') {
    i++;
  }
  if (i == 0 || i > 6 || i >= len || s[i] != ' ') {
    return 0;
  }
  *out_level = (u8)i;
  *out_content_pos = i + 1;
  return 1;
}

static int parse_list_marker(const char* s,
                             usize len,
                             int* out_ordered,
                             usize* out_content_pos) {
  usize i = 0;

  if (len >= 2 && (s[0] == '-' || s[0] == '*') && s[1] == ' ') {
    *out_ordered = 0;
    *out_content_pos = 2;
    return 1;
  }

  while (i < len && is_digit(s[i])) {
    i++;
  }
  if (i > 0 && i + 1 < len && s[i] == '.' && s[i + 1] == ' ') {
    *out_ordered = 1;
    *out_content_pos = i + 2;
    return 1;
  }

  return 0;
}

static int md_import(const convert_format_handler* handler,
                     const u8* input,
                     usize input_len,
                     convert_policy_mode policy,
                     convert_doc_buffer* doc,
                     convert_diagnostics* diags) {
  fmt_markdown_state* st = (fmt_markdown_state*)handler->user;
  usize line_start = 0;
  usize i;

  (void)policy;

  if (st == 0 || doc == 0 || doc->doc == 0) {
    return CONVERT_ERR_INVALID;
  }
  if (input_len > FMT_MARKDOWN_MAX_TEXT) {
    return CONVERT_ERR_LIMIT;
  }

  st->block_count = 0;
  st->nested_block_count = 0;
  st->inline_count = 0;
  st->aux_inline_count = 0;
  st->text_len = 0;
  st->storage_len = 0;

  for (i = 0; i < input_len; i++) {
    st->text[st->text_len++] = (char)input[i];
  }

  i = 0;
  while (i <= st->text_len) {
    if (i == st->text_len || st->text[i] == '\n') {
      usize len = i - line_start;
      const char* line = st->text + line_start;
      usize trimmed_start = 0;
      usize trimmed_len = len;

      while (trimmed_start < trimmed_len && is_space(line[trimmed_start])) {
        trimmed_start++;
      }
      line += trimmed_start;
      trimmed_len -= trimmed_start;

      if (trimmed_len > 0) {
        u8 heading_level = 0;
        usize heading_content_pos = 0;
        int is_ordered = 0;
        usize list_content_pos = 0;

        if (trimmed_len >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') {
          doc_model_block* blk = alloc_block(st);
          usize cursor = i + 1;
          usize code_start = st->storage_len;
          const char* lang = line + 3;
          usize lang_len = trimmed_len - 3;
          doc_model_sv code_text;
          doc_model_sv code_lang;

          if (blk == 0) {
            return CONVERT_ERR_LIMIT;
          }

          while (cursor <= st->text_len) {
            if (cursor == st->text_len || st->text[cursor] == '\n') {
              usize ls = i + 1;
              usize le = cursor;
              usize ll = le - ls;
              const char* lp = st->text + ls;
              usize t = 0;

              while (t < ll && is_space(lp[t])) {
                t++;
              }
              if (ll - t >= 3 && lp[t] == '`' && lp[t + 1] == '`' && lp[t + 2] == '`') {
                i = cursor;
                break;
              }

              if (append_storage_bytes(st, lp, ll) != CONVERT_OK ||
                  append_storage_bytes(st, "\n", 1) != CONVERT_OK) {
                return CONVERT_ERR_LIMIT;
              }
              i = cursor;
            }
            cursor++;
          }

          code_text.data = st->storage + code_start;
          code_text.len = st->storage_len - code_start;
          if (copy_sv(st, lang, lang_len, &code_lang) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }

          blk->kind = DOC_MODEL_BLOCK_CODE_BLOCK;
          blk->style_id.data = 0;
          blk->style_id.len = 0;
          blk->as.code_block.language = code_lang;
          blk->as.code_block.text = code_text;
        } else if (line[0] == '>') {
          doc_model_block* quote_blk = alloc_block(st);
          doc_model_block* para_blk = alloc_nested_block(st);
          doc_model_inline_list inl_list;
          usize qoff = (trimmed_len >= 2 && line[1] == ' ') ? 2 : 1;

          if (quote_blk == 0 || para_blk == 0) {
            return CONVERT_ERR_LIMIT;
          }

          if (parse_inline_line(st, line + qoff, trimmed_len - qoff, &inl_list, diags) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }

          para_blk->kind = DOC_MODEL_BLOCK_PARAGRAPH;
          para_blk->style_id.data = 0;
          para_blk->style_id.len = 0;
          para_blk->as.paragraph.inlines = inl_list;

          quote_blk->kind = DOC_MODEL_BLOCK_QUOTE;
          quote_blk->style_id.data = 0;
          quote_blk->style_id.len = 0;
          quote_blk->as.quote.blocks.items = para_blk;
          quote_blk->as.quote.blocks.count = 1;

        } else if (parse_heading_level(line, trimmed_len, &heading_level, &heading_content_pos)) {
          doc_model_block* blk = alloc_block(st);
          doc_model_inline_list inl_list;
          if (blk == 0) {
            return CONVERT_ERR_LIMIT;
          }
          if (parse_inline_line(st,
                                line + heading_content_pos,
                                trimmed_len - heading_content_pos,
                                &inl_list,
                                diags) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          blk->kind = DOC_MODEL_BLOCK_HEADING;
          blk->style_id.data = 0;
          blk->style_id.len = 0;
          blk->as.heading.level = heading_level;
          blk->as.heading.inlines = inl_list;
        } else if (parse_list_marker(line, trimmed_len, &is_ordered, &list_content_pos)) {
          const char* run_lines[128];
          usize run_lens[128];
          usize run_indent[128];
          usize run_count = 0;
          usize cursor;
          usize line_end = i;
          doc_model_block* list_blk;
          usize item_start;
          usize j;
          usize base_indent = trimmed_start;

          run_lines[run_count] = line + list_content_pos;
          run_lens[run_count] = trimmed_len - list_content_pos;
          run_indent[run_count] = trimmed_start;
          run_count++;

          cursor = i + 1;
          while (cursor <= st->text_len) {
            if (cursor == st->text_len || st->text[cursor] == '\n') {
              usize ls = line_end + 1;
              usize le = cursor;
              usize ll = le - ls;
              const char* lp = st->text + ls;
              usize t = 0;
              int ord = 0;
              usize content = 0;

              while (t < ll && is_space(lp[t])) {
                t++;
              }
              lp += t;
              ll -= t;

              if (ll == 0 || !parse_list_marker(lp, ll, &ord, &content) || ord != is_ordered ||
                  run_count >= 128) {
                break;
              }

              run_lines[run_count] = lp + content;
              run_lens[run_count] = ll - content;
              run_indent[run_count] = t;
              run_count++;
              line_end = cursor;
            }
            cursor++;
          }

          list_blk = alloc_block(st);
          if (list_blk == 0) {
            return CONVERT_ERR_LIMIT;
          }

          item_start = st->nested_block_count;
          for (j = 0; j < run_count; j++) {
            doc_model_block* item_blk = alloc_nested_block(st);
            if (item_blk == 0) {
              return CONVERT_ERR_LIMIT;
            }
            item_blk->kind = DOC_MODEL_BLOCK_LIST_ITEM;
            if (run_indent[j] > base_indent) {
              item_blk->style_id.data = "li-n1";
              item_blk->style_id.len = 5;
            } else {
              item_blk->style_id.data = "li-n0";
              item_blk->style_id.len = 5;
            }
            item_blk->as.list_item.blocks.items = 0;
            item_blk->as.list_item.blocks.count = 0;
          }

          for (j = 0; j < run_count; j++) {
            doc_model_block* para_blk = alloc_nested_block(st);
            doc_model_inline_list inl_list;
            doc_model_block* item_blk = &st->nested_blocks[item_start + j];
            if (para_blk == 0) {
              return CONVERT_ERR_LIMIT;
            }
            if (parse_inline_line(st, run_lines[j], run_lens[j], &inl_list, diags) != CONVERT_OK) {
              return CONVERT_ERR_LIMIT;
            }
            para_blk->kind = DOC_MODEL_BLOCK_PARAGRAPH;
            para_blk->style_id.data = 0;
            para_blk->style_id.len = 0;
            para_blk->as.paragraph.inlines = inl_list;

            item_blk->as.list_item.blocks.items = para_blk;
            item_blk->as.list_item.blocks.count = 1;
          }

          list_blk->kind = DOC_MODEL_BLOCK_LIST;
          list_blk->style_id.data = 0;
          list_blk->style_id.len = 0;
          list_blk->as.list.ordered = (u8)is_ordered;
          list_blk->as.list.items.items = &st->nested_blocks[item_start];
          list_blk->as.list.items.count = run_count;

          i = line_end;
        } else {
          doc_model_block* blk = alloc_block(st);
          doc_model_inline_list inl_list;
          if (blk == 0) {
            return CONVERT_ERR_LIMIT;
          }
          if (parse_inline_line(st, line, trimmed_len, &inl_list, diags) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          blk->kind = DOC_MODEL_BLOCK_PARAGRAPH;
          blk->style_id.data = 0;
          blk->style_id.len = 0;
          blk->as.paragraph.inlines = inl_list;
        }
      }

      line_start = i + 1;
    }
    i++;
  }

  doc->doc->metadata.title.data = 0;
  doc->doc->metadata.title.len = 0;
  doc->doc->metadata.author.data = 0;
  doc->doc->metadata.author.len = 0;
  doc->doc->metadata.language.data = "en";
  doc->doc->metadata.language.len = 2;
  doc->doc->metadata.created_unix_s = 0;
  doc->doc->metadata.modified_unix_s = 0;
  doc->doc->blocks.items = st->blocks;
  doc->doc->blocks.count = st->block_count;
  doc->doc->styles = 0;
  doc->doc->style_count = 0;
  doc->doc->assets = 0;
  doc->doc->asset_count = 0;
  doc->doc->extensions = 0;
  doc->doc->extension_count = 0;

  if (st->block_count == 0) {
    (void)convert_diagnostics_push(diags,
                                   CONVERT_DIAG_WARN,
                                   CONVERT_DIAG_UNSUPPORTED_CONSTRUCT,
                                   CONVERT_STAGE_NORMALIZE_IN,
                                   "markdown import: empty document");
  }

  return CONVERT_OK;
}

static int export_inline(const doc_model_inline* inl,
                         u8* output,
                         usize output_cap,
                         usize* pos,
                         convert_diagnostics* diags) {
  usize i;

  switch (inl->kind) {
    case DOC_MODEL_INLINE_TEXT:
      return append_out(output, output_cap, pos, inl->as.text.text.data, inl->as.text.text.len);

    case DOC_MODEL_INLINE_CODE_SPAN:
      if (append_char(output, output_cap, pos, '`') != CONVERT_OK ||
          append_out(output,
                     output_cap,
                     pos,
                     inl->as.code_span.text.data,
                     inl->as.code_span.text.len) != CONVERT_OK ||
          append_char(output, output_cap, pos, '`') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;

    case DOC_MODEL_INLINE_EMPHASIS:
      if (append_char(output, output_cap, pos, '*') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      for (i = 0; i < inl->as.emphasis.children.count; i++) {
        if (export_inline(&inl->as.emphasis.children.items[i], output, output_cap, pos, diags) !=
            CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }
      return append_char(output, output_cap, pos, '*');

    case DOC_MODEL_INLINE_STRONG:
      if (append_char(output, output_cap, pos, '*') != CONVERT_OK ||
          append_char(output, output_cap, pos, '*') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      for (i = 0; i < inl->as.strong.children.count; i++) {
        if (export_inline(&inl->as.strong.children.items[i], output, output_cap, pos, diags) !=
            CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }
      if (append_char(output, output_cap, pos, '*') != CONVERT_OK ||
          append_char(output, output_cap, pos, '*') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;

    case DOC_MODEL_INLINE_LINK:
      if (append_char(output, output_cap, pos, '[') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      for (i = 0; i < inl->as.link.children.count; i++) {
        if (export_inline(&inl->as.link.children.items[i], output, output_cap, pos, diags) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }
      if (append_char(output, output_cap, pos, ']') != CONVERT_OK ||
          append_char(output, output_cap, pos, '(') != CONVERT_OK ||
          append_out(output, output_cap, pos, inl->as.link.href.data, inl->as.link.href.len) !=
              CONVERT_OK ||
          append_char(output, output_cap, pos, ')') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;

    case DOC_MODEL_INLINE_IMAGE:
      if (append_char(output, output_cap, pos, '!') != CONVERT_OK ||
          append_char(output, output_cap, pos, '[') != CONVERT_OK ||
          append_out(output, output_cap, pos, inl->as.image.alt.data, inl->as.image.alt.len) !=
              CONVERT_OK ||
          append_char(output, output_cap, pos, ']') != CONVERT_OK ||
          append_char(output, output_cap, pos, '(') != CONVERT_OK ||
          append_out(output,
                     output_cap,
                     pos,
                     inl->as.image.asset_id.data,
                     inl->as.image.asset_id.len) != CONVERT_OK ||
          append_char(output, output_cap, pos, ')') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;

    case DOC_MODEL_INLINE_LINE_BREAK:
      return append_char(output, output_cap, pos, '\n');

    default:
      (void)convert_diagnostics_push(diags,
                                     CONVERT_DIAG_WARN,
                                     CONVERT_DIAG_UNSUPPORTED_CONSTRUCT,
                                     CONVERT_STAGE_NORMALIZE_OUT,
                                     "markdown export: unsupported inline skipped");
      return CONVERT_OK;
  }
}

static int export_inline_list(const doc_model_inline_list* inlines,
                              u8* output,
                              usize output_cap,
                              usize* pos,
                              convert_diagnostics* diags) {
  usize i;
  for (i = 0; i < inlines->count; i++) {
    if (export_inline(&inlines->items[i], output, output_cap, pos, diags) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }
  return CONVERT_OK;
}

static int append_indent(u8* output, usize output_cap, usize* pos, usize indent) {
  usize i;
  for (i = 0; i < indent; i++) {
    if (append_char(output, output_cap, pos, ' ') != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }
  return CONVERT_OK;
}

static int export_block_markdown(const doc_model_block* blk,
                                 u8* output,
                                 usize output_cap,
                                 usize* pos,
                                 usize indent,
                                 convert_diagnostics* diags);

static int export_list_markdown(const doc_model_block* blk,
                                u8* output,
                                usize output_cap,
                                usize* pos,
                                usize indent,
                                convert_diagnostics* diags) {
  usize j;
  for (j = 0; j < blk->as.list.items.count; j++) {
    const doc_model_block* item = &blk->as.list.items.items[j];
    usize k;

    if (append_indent(output, output_cap, pos, indent) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }

    if (blk->as.list.ordered) {
      char num[32];
      usize num_len = rt_u64_to_dec((u64)(j + 1), num, sizeof(num));
      if (append_out(output, output_cap, pos, num, num_len) != CONVERT_OK ||
          append_out(output, output_cap, pos, ". ", 2) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
    } else {
      if (append_out(output, output_cap, pos, "- ", 2) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
    }

    if (item->kind == DOC_MODEL_BLOCK_LIST_ITEM && item->as.list_item.blocks.count > 0 &&
        item->as.list_item.blocks.items[0].kind == DOC_MODEL_BLOCK_PARAGRAPH) {
      if (export_inline_list(&item->as.list_item.blocks.items[0].as.paragraph.inlines,
                             output,
                             output_cap,
                             pos,
                             diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
    }
    if (append_char(output, output_cap, pos, '\n') != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }

    if (item->kind == DOC_MODEL_BLOCK_LIST_ITEM) {
      for (k = 1; k < item->as.list_item.blocks.count; k++) {
        const doc_model_block* child = &item->as.list_item.blocks.items[k];
        if (child->kind == DOC_MODEL_BLOCK_LIST) {
          if (export_list_markdown(child, output, output_cap, pos, indent + 2, diags) != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
        } else if (child->kind == DOC_MODEL_BLOCK_PARAGRAPH) {
          if (append_indent(output, output_cap, pos, indent + 2) != CONVERT_OK ||
              export_inline_list(&child->as.paragraph.inlines, output, output_cap, pos, diags) !=
                  CONVERT_OK ||
              append_char(output, output_cap, pos, '\n') != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
        }
      }
    }
  }
  return CONVERT_OK;
}

static int export_block_markdown(const doc_model_block* blk,
                                 u8* output,
                                 usize output_cap,
                                 usize* pos,
                                 usize indent,
                                 convert_diagnostics* diags) {
  if (blk->kind == DOC_MODEL_BLOCK_HEADING) {
    u8 level = blk->as.heading.level;
    usize k;
    if (level < 1 || level > 6) {
      level = 1;
    }
    if (append_indent(output, output_cap, pos, indent) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    for (k = 0; k < (usize)level; k++) {
      if (append_char(output, output_cap, pos, '#') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
    }
    if (append_char(output, output_cap, pos, ' ') != CONVERT_OK ||
        export_inline_list(&blk->as.heading.inlines, output, output_cap, pos, diags) != CONVERT_OK ||
        append_char(output, output_cap, pos, '\n') != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    return CONVERT_OK;
  }

  if (blk->kind == DOC_MODEL_BLOCK_PARAGRAPH) {
    if (append_indent(output, output_cap, pos, indent) != CONVERT_OK ||
        export_inline_list(&blk->as.paragraph.inlines, output, output_cap, pos, diags) != CONVERT_OK ||
        append_char(output, output_cap, pos, '\n') != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    return CONVERT_OK;
  }

  if (blk->kind == DOC_MODEL_BLOCK_LIST) {
    return export_list_markdown(blk, output, output_cap, pos, indent, diags);
  }

  if (blk->kind == DOC_MODEL_BLOCK_CODE_BLOCK) {
    if (append_indent(output, output_cap, pos, indent) != CONVERT_OK ||
        append_out(output, output_cap, pos, "```", 3) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (blk->as.code_block.language.len > 0 &&
        append_out(output,
                   output_cap,
                   pos,
                   blk->as.code_block.language.data,
                   blk->as.code_block.language.len) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (append_char(output, output_cap, pos, '\n') != CONVERT_OK ||
        append_out(output,
                   output_cap,
                   pos,
                   blk->as.code_block.text.data,
                   blk->as.code_block.text.len) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (blk->as.code_block.text.len == 0 ||
        blk->as.code_block.text.data[blk->as.code_block.text.len - 1] != '\n') {
      if (append_char(output, output_cap, pos, '\n') != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
    }
    if (append_indent(output, output_cap, pos, indent) != CONVERT_OK ||
        append_out(output, output_cap, pos, "```\n", 4) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    return CONVERT_OK;
  }

  if (blk->kind == DOC_MODEL_BLOCK_QUOTE) {
    usize i;
    for (i = 0; i < blk->as.quote.blocks.count; i++) {
      const doc_model_block* qb = &blk->as.quote.blocks.items[i];
      if (qb->kind == DOC_MODEL_BLOCK_PARAGRAPH) {
        if (append_indent(output, output_cap, pos, indent) != CONVERT_OK ||
            append_out(output, output_cap, pos, "> ", 2) != CONVERT_OK ||
            export_inline_list(&qb->as.paragraph.inlines, output, output_cap, pos, diags) != CONVERT_OK ||
            append_char(output, output_cap, pos, '\n') != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      } else if (export_block_markdown(qb, output, output_cap, pos, indent + 2, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
    }
    return CONVERT_OK;
  }

  (void)convert_diagnostics_push(diags,
                                 CONVERT_DIAG_WARN,
                                 CONVERT_DIAG_UNSUPPORTED_CONSTRUCT,
                                 CONVERT_STAGE_NORMALIZE_OUT,
                                 "markdown export: unsupported block skipped");
  return CONVERT_OK;
}

static int md_export(const convert_format_handler* handler,
                     const convert_doc_buffer* doc,
                     convert_policy_mode policy,
                     u8* output,
                     usize output_cap,
                     usize* output_len,
                     convert_diagnostics* diags) {
  usize i;
  usize pos = 0;

  (void)handler;
  (void)policy;

  if (doc == 0 || doc->doc == 0 || output == 0 || output_len == 0) {
    return CONVERT_ERR_INVALID;
  }

  for (i = 0; i < doc->doc->blocks.count; i++) {
    if (export_block_markdown(&doc->doc->blocks.items[i], output, output_cap, &pos, 0, diags) !=
        CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  *output_len = pos;
  return CONVERT_OK;
}

void fmt_markdown_state_init(fmt_markdown_state* state) {
  if (state == 0) {
    return;
  }

  state->handler.name = "md";
  state->handler.import_doc = md_import;
  state->handler.export_doc = md_export;
  state->handler.user = state;
  state->block_count = 0;
  state->nested_block_count = 0;
  state->inline_count = 0;
  state->aux_inline_count = 0;
  state->text_len = 0;
  state->storage_len = 0;
}

int fmt_markdown_register(convert_registry* registry, fmt_markdown_state* state) {
  if (registry == 0 || state == 0) {
    return CONVERT_ERR_INVALID;
  }
  fmt_markdown_state_init(state);
  return convert_registry_register(registry, &state->handler);
}
