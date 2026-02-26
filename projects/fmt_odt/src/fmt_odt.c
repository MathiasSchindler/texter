#include "fmt_odt/fmt_odt.h"

#include "deflate/deflate.h"
#include "odt_core/odt_core.h"
#include "rt/rt.h"
#include "xml/xml.h"
#include "zip/zip.h"

static int sv_to_cstr(doc_model_sv sv, char* dst, usize cap) {
  usize i;
  if (sv.len + 1 > cap) {
    return CONVERT_ERR_LIMIT;
  }
  if (sv.len > 0 && sv.data == 0) {
    return CONVERT_ERR_INVALID;
  }
  for (i = 0; i < sv.len; i++) {
    dst[i] = sv.data[i];
  }
  dst[sv.len] = '\0';
  return CONVERT_OK;
}

static int xml_sv_eq_cstr(xml_sv sv, const char* s) {
  usize i = 0;
  while (s[i] != '\0') {
    if (i >= sv.len || sv.data[i] != s[i]) {
      return 0;
    }
    i++;
  }
  return i == sv.len;
}

static int xml_sv_starts_with_cstr(xml_sv sv, const char* s) {
  usize i = 0;
  while (s[i] != '\0') {
    if (i >= sv.len || sv.data[i] != s[i]) {
      return 0;
    }
    i++;
  }
  return 1;
}

static int cstr_eq(const char* a, const char* b) {
  usize i = 0;
  while (a[i] != '\0' || b[i] != '\0') {
    if (a[i] != b[i]) {
      return 0;
    }
    i++;
  }
  return 1;
}

static int text_is_recital(const char* s, usize len) {
  usize i = 1;
  if (len < 4 || s == 0 || s[0] != '(') {
    return 0;
  }
  while (i < len && s[i] >= '0' && s[i] <= '9') {
    i++;
  }
  if (i <= 1 || i + 1 >= len) {
    return 0;
  }
  if (s[i] != ')' || s[i + 1] != ' ') {
    return 0;
  }
  return 1;
}

static int text_eq_literal(const char* s, usize len, const char* lit) {
  usize i = 0;
  if (s == 0 || lit == 0) {
    return 0;
  }
  while (lit[i] != '\0') {
    if (i >= len || s[i] != lit[i]) {
      return 0;
    }
    i++;
  }
  return i == len;
}

static int text_starts_with_literal(const char* s, usize len, const char* lit) {
  usize i = 0;
  if (s == 0 || lit == 0) {
    return 0;
  }
  while (lit[i] != '\0') {
    if (i >= len || s[i] != lit[i]) {
      return 0;
    }
    i++;
  }
  return 1;
}

static char ascii_lower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c + ('a' - 'A'));
  }
  return c;
}

static int text_icontains_literal(const char* s, usize len, const char* lit) {
  usize i;
  usize lit_len = 0;
  if (s == 0 || lit == 0) {
    return 0;
  }
  while (lit[lit_len] != '\0') {
    lit_len++;
  }
  if (lit_len == 0 || len < lit_len) {
    return 0;
  }
  for (i = 0; i + lit_len <= len; i++) {
    usize j;
    int match = 1;
    for (j = 0; j < lit_len; j++) {
      if (ascii_lower(s[i + j]) != ascii_lower(lit[j])) {
        match = 0;
        break;
      }
    }
    if (match) {
      return 1;
    }
  }
  return 0;
}

static int text_looks_like_email_line(const char* s, usize len) {
  usize i;
  int has_at = 0;
  int has_dot = 0;
  if (s == 0 || len == 0 || len > 160) {
    return 0;
  }
  for (i = 0; i < len; i++) {
    if (s[i] == '@') {
      has_at = 1;
    }
    if (s[i] == '.') {
      has_dot = 1;
    }
  }
  return has_at && has_dot;
}

static int text_looks_like_date_line(const char* s, usize len) {
  static const char* months[] = {"january",   "february", "march",    "april",
                                 "may",       "june",     "july",     "august",
                                 "september", "october",  "november", "december"};
  usize i;
  int has_digit = 0;
  if (s == 0 || len < 8 || len > 64) {
    return 0;
  }
  for (i = 0; i < len; i++) {
    if (s[i] >= '0' && s[i] <= '9') {
      has_digit = 1;
      break;
    }
  }
  if (!has_digit) {
    return 0;
  }
  for (i = 0; i < (sizeof(months) / sizeof(months[0])); i++) {
    if (text_icontains_literal(s, len, months[i])) {
      return 1;
    }
  }
  return 0;
}

static int text_looks_like_author_line(const char* s, usize len) {
  usize i;
  int comma_count = 0;
  int has_and = 0;
  if (s == 0 || len < 8 || len > 180) {
    return 0;
  }
  if (text_icontains_literal(s, len, "@")) {
    return 0;
  }
  for (i = 0; i < len; i++) {
    if (s[i] == ',') {
      comma_count++;
    }
  }
  has_and = text_icontains_literal(s, len, " and ");
  return comma_count >= 1 && has_and;
}

static int text_looks_like_sender_address_line(const char* s, usize len) {
  usize i;
  int has_comma = 0;
  int has_digit = 0;
  int run_digits = 0;
  int has_long_digit_run = 0;
  if (s == 0 || len == 0 || len > 96) {
    return 0;
  }
  if (text_icontains_literal(s, len, "street") || text_icontains_literal(s, len, "avenue") ||
      text_icontains_literal(s, len, "road") || text_icontains_literal(s, len, "tel:") ||
      text_icontains_literal(s, len, "phone")) {
    return 1;
  }
  for (i = 0; i < len; i++) {
    if (s[i] == ',') {
      has_comma = 1;
    }
    if (s[i] >= '0' && s[i] <= '9') {
      has_digit = 1;
      run_digits++;
      if (run_digits >= 4) {
        has_long_digit_run = 1;
      }
    } else {
      run_digits = 0;
    }
  }
  if (has_comma && has_digit) {
    return 1;
  }
  return has_long_digit_run;
}

static int inline_icontains_literal(const doc_model_inline* inl, const char* lit) {
  usize i;
  if (inl == 0 || lit == 0) {
    return 0;
  }
  switch (inl->kind) {
    case DOC_MODEL_INLINE_TEXT:
      return text_icontains_literal(inl->as.text.text.data, inl->as.text.text.len, lit);
    case DOC_MODEL_INLINE_CODE_SPAN:
      return text_icontains_literal(inl->as.code_span.text.data, inl->as.code_span.text.len, lit);
    case DOC_MODEL_INLINE_EMPHASIS:
      for (i = 0; i < inl->as.emphasis.children.count; i++) {
        if (inline_icontains_literal(&inl->as.emphasis.children.items[i], lit)) {
          return 1;
        }
      }
      return 0;
    case DOC_MODEL_INLINE_STRONG:
      for (i = 0; i < inl->as.strong.children.count; i++) {
        if (inline_icontains_literal(&inl->as.strong.children.items[i], lit)) {
          return 1;
        }
      }
      return 0;
    case DOC_MODEL_INLINE_LINK:
      for (i = 0; i < inl->as.link.children.count; i++) {
        if (inline_icontains_literal(&inl->as.link.children.items[i], lit)) {
          return 1;
        }
      }
      return 0;
    default:
      return 0;
  }
}

static int heading_icontains_literal(const doc_model_block* blk, const char* lit) {
  usize i;
  if (blk == 0 || blk->kind != DOC_MODEL_BLOCK_HEADING || lit == 0) {
    return 0;
  }
  for (i = 0; i < blk->as.heading.inlines.count; i++) {
    if (inline_icontains_literal(&blk->as.heading.inlines.items[i], lit)) {
      return 1;
    }
  }
  return 0;
}

static usize find_two_column_start_index(const doc_model_document* doc) {
  usize i;
  if (doc == 0) {
    return 0;
  }
  for (i = 0; i < doc->blocks.count; i++) {
    const doc_model_block* blk = &doc->blocks.items[i];
    if (blk->kind != DOC_MODEL_BLOCK_HEADING) {
      continue;
    }
    if (blk->as.heading.level == 2 && heading_icontains_literal(blk, "introduction")) {
      return i;
    }
  }
  return doc->blocks.count; // Return total count instead of first_level2
}

static int paragraph_single_text(const doc_model_block* blk, doc_model_sv* out_text) {
  const doc_model_inline_list* inlines;
  if (blk == 0 || out_text == 0 || blk->kind != DOC_MODEL_BLOCK_PARAGRAPH) {
    return 0;
  }
  inlines = &blk->as.paragraph.inlines;
  if (inlines->count != 1 || inlines->items[0].kind != DOC_MODEL_INLINE_TEXT) {
    return 0;
  }
  *out_text = inlines->items[0].as.text.text;
  return 1;
}

static const char* inferred_paragraph_style(const doc_model_block* blk) {
  doc_model_sv t;
  if (!paragraph_single_text(blk, &t) || t.data == 0) {
    return "Text_20_body";
  }
  if (text_eq_literal(t.data, t.len, "---")) {
    return "OJSeparator";
  }
  if (text_eq_literal(t.data, t.len, "Whereas:")) {
    return "OJWhereasLabel";
  }
  if (text_eq_literal(t.data, t.len, "For the Commission")) {
    return "OJSignatureLine";
  }
  if (text_eq_literal(t.data, t.len, "The President")) {
    return "OJSignatureTitle";
  }
  if (text_is_recital(t.data, t.len)) {
    return "OJRecital";
  }

  if (text_starts_with_literal(t.data, t.len, "Dear ")) {
    return "LetterSalutation";
  }
  if (text_eq_literal(t.data, t.len, "Sincerely,") || text_eq_literal(t.data, t.len, "Sincerely")) {
    return "LetterClosing";
  }

  if (text_looks_like_sender_address_line(t.data, t.len)) {
    return "Affiliation";
  }

  if (text_looks_like_email_line(t.data, t.len)) {
    return "EmailLine";
  }
  if (text_looks_like_date_line(t.data, t.len)) {
    return "DateLine";
  }
  if (t.len <= 120 &&
      (text_icontains_literal(t.data, t.len, "institute") ||
       text_icontains_literal(t.data, t.len, "department") ||
       text_icontains_literal(t.data, t.len, "university") ||
       text_icontains_literal(t.data, t.len, "laboratory") ||
       text_icontains_literal(t.data, t.len, "school"))) {
    return "Affiliation";
  }
  if (text_icontains_literal(t.data, t.len, "this abstract") ||
      text_icontains_literal(t.data, t.len, "the abstract should")) {
    return "AbstractParagraph";
  }
  if (text_looks_like_author_line(t.data, t.len)) {
    return "AuthorLine";
  }

  return "Text_20_body";
}

static const char k_diag_plain_extract_failed[] = "odt import: plain extract failed";
static const char k_diag_plain_extract_too_large[] = "odt import: plain extract too large";
static const char k_diag_plain_fallback[] = "odt import: semantic parse failed, using plain text";
static const char k_diag_link_empty[] = "odt export: empty link href";
static const char k_diag_link_too_large[] = "odt export: link href too large";
static const char k_diag_image_dropped[] = "odt export: image dropped";
static const char k_diag_inline_unsupported[] = "odt export: unsupported inline skipped";
static const char k_diag_list_child_unsupported[] = "odt export: non-list-item child skipped";
static const char k_diag_block_unsupported[] = "odt export: unsupported block skipped";
static const char k_diag_import_placeholder[] = "odt import: unsupported construct represented as placeholder";

static void diag_push_lossy_warn(convert_diagnostics* diags,
                                 convert_stage stage,
                                 const char* message) {
  (void)convert_diagnostics_push(
      diags, CONVERT_DIAG_WARN, CONVERT_DIAG_LOSSY_DROP, stage, message);
}

static void diag_push_unsupported_warn(convert_diagnostics* diags,
                                       convert_stage stage,
                                       const char* message) {
  (void)convert_diagnostics_push(
      diags, CONVERT_DIAG_WARN, CONVERT_DIAG_UNSUPPORTED_CONSTRUCT, stage, message);
}

static int style_eq(const xml_attr* style_attr, const char* expected) {
  return style_attr != 0 && xml_sv_eq_cstr(style_attr->value, expected);
}

/* ODT style names are often auto-generated and noisy in markdown output. */
static void clear_style_id(doc_model_sv* style_id) {
  style_id->data = 0;
  style_id->len = 0;
}

static const char* heading_style_for_level(u8 level) {
  if (level == 2) {
    return "Heading_20_2";
  }
  if (level == 3) {
    return "Heading_20_3";
  }
  if (level == 4) {
    return "Heading_20_4";
  }
  if (level == 5) {
    return "Heading_20_5";
  }
  if (level == 6) {
    return "Heading_20_6";
  }
  return "Heading_20_1";
}

static const xml_attr* find_attr_qname(const xml_token* tok, const char* qname) {
  usize i;
  for (i = 0; i < tok->attr_count; i++) {
    if (xml_sv_eq_cstr(tok->attrs[i].qname, qname)) {
      return &tok->attrs[i];
    }
  }
  return 0;
}

static int parse_u8_decimal(xml_sv sv, u8* out) {
  usize i;
  u32 v = 0;
  if (sv.len == 0) {
    return CONVERT_ERR_INVALID;
  }
  for (i = 0; i < sv.len; i++) {
    char c = sv.data[i];
    if (c < '0' || c > '9') {
      return CONVERT_ERR_INVALID;
    }
    v = (v * 10U) + (u32)(c - '0');
    if (v > 255U) {
      return CONVERT_ERR_INVALID;
    }
  }
  *out = (u8)v;
  return CONVERT_OK;
}

static int append_import_text(fmt_odt_state* st, const char* src, usize len, doc_model_sv* out) {
  usize i;
  if (len > 0 && src == 0) {
    return CONVERT_ERR_INVALID;
  }
  if (st->text_len + len > FMT_ODT_MAX_TEXT) {
    return CONVERT_ERR_LIMIT;
  }
  if (out != 0) {
    out->data = st->text + st->text_len;
    out->len = len;
  }
  for (i = 0; i < len; i++) {
    st->text[st->text_len + i] = src[i];
  }
  st->text_len += len;
  return CONVERT_OK;
}

static int alloc_block_at(fmt_odt_state* st, int nested, doc_model_block** out) {
  if (nested) {
    if (st->nested_block_count >= FMT_ODT_MAX_NESTED_BLOCKS) {
      return CONVERT_ERR_LIMIT;
    }
    *out = &st->nested_blocks[st->nested_block_count++];
    return CONVERT_OK;
  }
  if (st->block_count >= FMT_ODT_MAX_BLOCKS) {
    return CONVERT_ERR_LIMIT;
  }
  *out = &st->blocks[st->block_count++];
  return CONVERT_OK;
}

static int alloc_inline_at(fmt_odt_state* st, int aux, doc_model_inline** out) {
  if (aux) {
    if (st->aux_inline_count >= FMT_ODT_MAX_AUX_INLINES) {
      return CONVERT_ERR_LIMIT;
    }
    *out = &st->aux_inlines[st->aux_inline_count++];
    return CONVERT_OK;
  }
  if (st->inline_count >= FMT_ODT_MAX_INLINES) {
    return CONVERT_ERR_LIMIT;
  }
  *out = &st->inlines[st->inline_count++];
  return CONVERT_OK;
}

static int append_placeholder_text(fmt_odt_state* st,
                                   const char* prefix,
                                   xml_sv qname,
                                   const char* suffix,
                                   doc_model_sv* out_text) {
  usize start = st->text_len;
  if (append_import_text(st, prefix, rt_strlen(prefix), 0) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  if (append_import_text(st, qname.data, qname.len, 0) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  if (append_import_text(st, suffix, rt_strlen(suffix), 0) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  out_text->data = st->text + start;
  out_text->len = st->text_len - start;
  return CONVERT_OK;
}

static int emit_placeholder_paragraph(fmt_odt_state* st,
                                      int nested_target,
                                      xml_sv qname,
                                      convert_diagnostics* diags) {
  doc_model_block* blk;
  doc_model_inline* inl;
  doc_model_sv sv;

  if (append_placeholder_text(st, "[odt:unsupported ", qname, "]", &sv) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  if (alloc_inline_at(st, 0, &inl) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  if (alloc_block_at(st, nested_target, &blk) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }

  inl->kind = DOC_MODEL_INLINE_TEXT;
  inl->style_id.data = 0;
  inl->style_id.len = 0;
  inl->as.text.text = sv;

  blk->kind = DOC_MODEL_BLOCK_PARAGRAPH;
  blk->style_id.data = 0;
  blk->style_id.len = 0;
  blk->as.paragraph.inlines.items = inl;
  blk->as.paragraph.inlines.count = 1;

  diag_push_unsupported_warn(diags, CONVERT_STAGE_PARSE, k_diag_import_placeholder);
  return CONVERT_OK;
}

static int collect_plain_text_unknown_elem(xml_reader* xr, fmt_odt_state* st) {
  usize depth = 1;
  while (depth > 0) {
    xml_token tok;
    if (xml_reader_next(xr, &tok) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_TEXT) {
      if (append_import_text(st, tok.text.data, tok.text.len, 0) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      continue;
    }
    if (tok.kind == XML_TOK_START_ELEM) {
      if (xml_sv_eq_cstr(tok.qname, "text:line-break")) {
        if (append_import_text(st, "\n", 1, 0) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
      }
      depth++;
      continue;
    }
    if (tok.kind == XML_TOK_END_ELEM) {
      depth--;
    }
  }
  return CONVERT_OK;
}

static int collect_plain_text_until_end(xml_reader* xr,
                                        fmt_odt_state* st,
                                        const char* end_qname,
                                        doc_model_sv* out_text) {
  usize start = st->text_len;
  for (;;) {
    xml_token tok;
    if (xml_reader_next(xr, &tok) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_TEXT) {
      if (append_import_text(st, tok.text.data, tok.text.len, 0) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      continue;
    }
    if (tok.kind == XML_TOK_START_ELEM) {
      if (xml_sv_eq_cstr(tok.qname, "text:line-break")) {
        if (append_import_text(st, "\n", 1, 0) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        continue;
      }
      if (xml_sv_eq_cstr(tok.qname, "text:s")) {
        const xml_attr* c_attr = find_attr_qname(&tok, "text:c");
        u8 count = 1;
        usize i;
        if (c_attr != 0 && parse_u8_decimal(c_attr->value, &count) != CONVERT_OK) {
          count = 1;
        }
        for (i = 0; i < (usize)count; i++) {
          if (append_import_text(st, " ", 1, 0) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
        }
        continue;
      }
      if (collect_plain_text_unknown_elem(xr, st) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }
    if (tok.kind == XML_TOK_END_ELEM) {
      if (end_qname[0] == '\0' || xml_sv_eq_cstr(tok.qname, end_qname)) {
        break;
      }
    }
  }
  out_text->data = st->text + start;
  out_text->len = st->text_len - start;
  return CONVERT_OK;
}

static int parse_inline_until_end(xml_reader* xr,
                                  fmt_odt_state* st,
                                  const char* end_qname,
                                  int aux_target,
                                  usize* out_count,
                                  convert_diagnostics* diags) {
  usize start_inline = aux_target ? st->aux_inline_count : st->inline_count;

  for (;;) {
    xml_token tok;
    if (xml_reader_next(xr, &tok) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      return CONVERT_ERR_INVALID;
    }

    if (tok.kind == XML_TOK_TEXT) {
      doc_model_inline* inl;
      doc_model_sv sv;
      if (tok.text.len == 0) {
        continue;
      }
      if (append_import_text(st, tok.text.data, tok.text.len, &sv) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      if (alloc_inline_at(st, aux_target, &inl) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      inl->kind = DOC_MODEL_INLINE_TEXT;
      inl->style_id.data = 0;
      inl->style_id.len = 0;
      inl->as.text.text = sv;
      continue;
    }

    if (tok.kind == XML_TOK_START_ELEM) {
      if (xml_sv_eq_cstr(tok.qname, "text:line-break")) {
        doc_model_inline* inl;
        if (alloc_inline_at(st, aux_target, &inl) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        inl->kind = DOC_MODEL_INLINE_LINE_BREAK;
        inl->style_id.data = 0;
        inl->style_id.len = 0;
        continue;
      }

      if (xml_sv_eq_cstr(tok.qname, "text:s")) {
        const xml_attr* c_attr = find_attr_qname(&tok, "text:c");
        u8 count = 1;
        usize i;
        if (c_attr != 0 && parse_u8_decimal(c_attr->value, &count) != CONVERT_OK) {
          count = 1;
        }
        for (i = 0; i < (usize)count; i++) {
          doc_model_inline* inl;
          doc_model_sv sv;
          if (append_import_text(st, " ", 1, &sv) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          if (alloc_inline_at(st, aux_target, &inl) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          inl->kind = DOC_MODEL_INLINE_TEXT;
          inl->style_id.data = 0;
          inl->style_id.len = 0;
          inl->as.text.text = sv;
        }
        continue;
      }

      if (xml_sv_eq_cstr(tok.qname, "text:span")) {
        const xml_attr* style_attr = find_attr_qname(&tok, "text:style-name");
        int is_emph = 0;
        int is_strong = 0;
        int is_code = 0;

        if (style_attr != 0) {
          is_emph = style_eq(style_attr, "Emphasis") || style_eq(style_attr, "TEmph");
          is_strong = style_eq(style_attr, "Strong_20_Emphasis") || style_eq(style_attr, "TStrong");
          is_code = style_eq(style_attr, "Source_20_Text") || style_eq(style_attr, "TCode");
        }

        if (is_emph || is_strong) {
          doc_model_inline* wrapper;
          usize child_start = st->aux_inline_count;
          usize child_count = 0;
          if (alloc_inline_at(st, aux_target, &wrapper) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          if (parse_inline_until_end(xr, st, "text:span", 1, &child_count, diags) != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
          wrapper->kind = is_emph ? DOC_MODEL_INLINE_EMPHASIS : DOC_MODEL_INLINE_STRONG;
          wrapper->style_id.data = 0;
          wrapper->style_id.len = 0;
          if (is_emph) {
            wrapper->as.emphasis.children.items = &st->aux_inlines[child_start];
            wrapper->as.emphasis.children.count = child_count;
          } else {
            wrapper->as.strong.children.items = &st->aux_inlines[child_start];
            wrapper->as.strong.children.count = child_count;
          }
          continue;
        }

        if (is_code) {
          doc_model_inline* code;
          doc_model_sv text_sv;
          if (collect_plain_text_until_end(xr, st, "text:span", &text_sv) != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
          if (text_sv.len == 0) {
            continue;
          }
          if (alloc_inline_at(st, aux_target, &code) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          code->kind = DOC_MODEL_INLINE_CODE_SPAN;
          code->style_id.data = 0;
          code->style_id.len = 0;
          code->as.code_span.text = text_sv;
          continue;
        }

        if (parse_inline_until_end(xr, st, "text:span", aux_target, 0, diags) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
        continue;
      }

      if (xml_sv_eq_cstr(tok.qname, "text:a")) {
        doc_model_inline* link;
        const xml_attr* href_attr = find_attr_qname(&tok, "xlink:href");
        usize child_start = st->aux_inline_count;
        usize child_count = 0;
        doc_model_sv href_sv;

        if (href_attr == 0 || href_attr->value.len == 0) {
          if (parse_inline_until_end(xr, st, "text:a", aux_target, 0, diags) != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
          continue;
        }

        if (append_import_text(st,
                               href_attr != 0 ? href_attr->value.data : "",
                               href_attr != 0 ? href_attr->value.len : 0,
                               &href_sv) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        if (alloc_inline_at(st, aux_target, &link) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        if (parse_inline_until_end(xr, st, "text:a", 1, &child_count, diags) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }

        link->kind = DOC_MODEL_INLINE_LINK;
        link->style_id.data = 0;
        link->style_id.len = 0;
        link->as.link.href = href_sv;
        link->as.link.title.data = 0;
        link->as.link.title.len = 0;
        link->as.link.children.items = &st->aux_inlines[child_start];
        link->as.link.children.count = child_count;
        continue;
      }

      if (xml_sv_eq_cstr(tok.qname, "text:note")) {
        doc_model_inline* inl;
        doc_model_sv note_text;
        doc_model_sv wrapped;
        usize wrap_start;
        if (collect_plain_text_until_end(xr, st, "text:note", &note_text) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
        wrap_start = st->text_len;
        if (append_import_text(st, "[note: ", 7, 0) != CONVERT_OK ||
            append_import_text(st, note_text.data, note_text.len, 0) != CONVERT_OK ||
            append_import_text(st, "]", 1, 0) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        wrapped.data = st->text + wrap_start;
        wrapped.len = st->text_len - wrap_start;
        if (alloc_inline_at(st, aux_target, &inl) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        inl->kind = DOC_MODEL_INLINE_CODE_SPAN;
        inl->style_id.data = 0;
        inl->style_id.len = 0;
        inl->as.code_span.text = wrapped;
        continue;
      }

      if (xml_sv_eq_cstr(tok.qname, "text:bookmark-start") ||
          xml_sv_eq_cstr(tok.qname, "text:bookmark")) {
        const xml_attr* name_attr = find_attr_qname(&tok, "text:name");
        doc_model_inline* inl;
        doc_model_sv wrapped;

        /* Auto-generated heading/list anchors are noisy in markdown body text. */
        if (name_attr != 0 &&
            (xml_sv_starts_with_cstr(name_attr->value, "__RefHeading") ||
             xml_sv_starts_with_cstr(name_attr->value, "line-"))) {
          continue;
        }

        usize wrap_start = st->text_len;
        if (append_import_text(st, "{#", 2, 0) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        if (name_attr != 0 && append_import_text(st, name_attr->value.data, name_attr->value.len, 0) !=
                              CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        if (append_import_text(st, "}", 1, 0) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        wrapped.data = st->text + wrap_start;
        wrapped.len = st->text_len - wrap_start;
        if (alloc_inline_at(st, aux_target, &inl) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        inl->kind = DOC_MODEL_INLINE_TEXT;
        inl->style_id.data = 0;
        inl->style_id.len = 0;
        inl->as.text.text = wrapped;
        continue;
      }

      if (xml_sv_eq_cstr(tok.qname, "draw:frame")) {
        doc_model_inline* inl;
        doc_model_sv href_sv;
        int found_href = 0;
        usize depth = 1;
        while (depth > 0) {
          xml_token ftok;
          if (xml_reader_next(xr, &ftok) != XML_OK) {
            return CONVERT_ERR_INVALID;
          }
          if (ftok.kind == XML_TOK_EOF) {
            return CONVERT_ERR_INVALID;
          }
          if (ftok.kind == XML_TOK_START_ELEM) {
            if (xml_sv_eq_cstr(ftok.qname, "draw:image")) {
              const xml_attr* href_attr = find_attr_qname(&ftok, "xlink:href");
              if (href_attr != 0 &&
                  append_import_text(st, href_attr->value.data, href_attr->value.len, &href_sv) ==
                      CONVERT_OK) {
                found_href = 1;
              }
            }
            depth++;
          } else if (ftok.kind == XML_TOK_END_ELEM) {
            depth--;
          }
        }

        if (alloc_inline_at(st, aux_target, &inl) != CONVERT_OK) {
          return CONVERT_ERR_LIMIT;
        }
        if (found_href) {
          doc_model_sv alt;
          if (append_import_text(st, "image", 5, &alt) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          inl->kind = DOC_MODEL_INLINE_IMAGE;
          inl->style_id.data = 0;
          inl->style_id.len = 0;
          inl->as.image.asset_id = href_sv;
          inl->as.image.alt = alt;
        } else {
          doc_model_sv placeholder;
          if (append_import_text(st, "[image]", 7, &placeholder) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          inl->kind = DOC_MODEL_INLINE_TEXT;
          inl->style_id.data = 0;
          inl->style_id.len = 0;
          inl->as.text.text = placeholder;
        }
        continue;
      }

      if (xml_sv_eq_cstr(tok.qname, "text:p") && cstr_eq(end_qname, "table:table-cell")) {
        if (parse_inline_until_end(xr, st, "text:p", aux_target, 0, diags) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
        continue;
      }

      if (collect_plain_text_unknown_elem(xr, st) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }

    if (tok.kind == XML_TOK_END_ELEM && xml_sv_eq_cstr(tok.qname, end_qname)) {
      break;
    }
  }

  if (out_count != 0) {
    *out_count = (aux_target ? st->aux_inline_count : st->inline_count) - start_inline;
  }
  (void)diags;
  return CONVERT_OK;
}

static int parse_paragraph_start(xml_reader* xr,
                                 fmt_odt_state* st,
                                 const xml_token* start,
                                 int nested_target,
                                 convert_diagnostics* diags);

static int parse_heading_start(xml_reader* xr,
                               fmt_odt_state* st,
                               const xml_token* start,
                               int nested_target,
                               convert_diagnostics* diags);

static int parse_block_stream_until(xml_reader* xr,
                                    fmt_odt_state* st,
                                    int nested_target,
                                    const char* end_qname,
                                    convert_diagnostics* diags);

static int parse_section_start(xml_reader* xr,
                               fmt_odt_state* st,
                               const xml_token* start,
                               int nested_target,
                               convert_diagnostics* diags);

static int parse_table_start(xml_reader* xr,
                             fmt_odt_state* st,
                             const xml_token* start,
                             int nested_target,
                             convert_diagnostics* diags);

static int parse_list_start(xml_reader* xr,
                            fmt_odt_state* st,
                            const xml_token* start,
                            int nested_target,
                            convert_diagnostics* diags);

static int parse_list_item_start(xml_reader* xr,
                                 fmt_odt_state* st,
                                 doc_model_block* item,
                                 convert_diagnostics* diags) {
  usize child_start = st->nested_block_count;

  item->kind = DOC_MODEL_BLOCK_LIST_ITEM;
  item->style_id.data = 0;
  item->style_id.len = 0;

  for (;;) {
    xml_token tok;
    if (xml_reader_next(xr, &tok) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_END_ELEM && xml_sv_eq_cstr(tok.qname, "text:list-item")) {
      break;
    }
    if (tok.kind != XML_TOK_START_ELEM) {
      continue;
    }
    if (xml_sv_eq_cstr(tok.qname, "text:p")) {
      if (parse_paragraph_start(xr, st, &tok, 1, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }
    if (xml_sv_eq_cstr(tok.qname, "text:list")) {
      doc_model_sv nested_text;
      doc_model_block* para_blk;
      doc_model_inline* inl;
      doc_model_sv prefixed;

      if (collect_plain_text_until_end(xr, st, "text:list", &nested_text) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      if (append_import_text(st, "- ", 2, 0) != CONVERT_OK ||
          append_import_text(st, nested_text.data, nested_text.len, &prefixed) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      if (st->nested_block_count >= FMT_ODT_MAX_NESTED_BLOCKS || st->inline_count >= FMT_ODT_MAX_INLINES) {
        return CONVERT_ERR_LIMIT;
      }
      para_blk = &st->nested_blocks[st->nested_block_count++];
      inl = &st->inlines[st->inline_count++];
      inl->kind = DOC_MODEL_INLINE_TEXT;
      inl->style_id.data = 0;
      inl->style_id.len = 0;
      inl->as.text.text = prefixed;
      para_blk->kind = DOC_MODEL_BLOCK_PARAGRAPH;
      para_blk->style_id.data = 0;
      para_blk->style_id.len = 0;
      para_blk->as.paragraph.inlines.items = inl;
      para_blk->as.paragraph.inlines.count = 1;
      continue;
    }
    if (xml_sv_eq_cstr(tok.qname, "text:h")) {
      if (parse_heading_start(xr, st, &tok, 1, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }
    if (collect_plain_text_unknown_elem(xr, st) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  item->as.list_item.blocks.items = &st->nested_blocks[child_start];
  item->as.list_item.blocks.count = st->nested_block_count - child_start;
  return CONVERT_OK;
}

static int parse_list_start(xml_reader* xr,
                            fmt_odt_state* st,
                            const xml_token* start,
                            int nested_target,
                            convert_diagnostics* diags) {
  doc_model_block* list_blk;
  const xml_attr* style_attr = find_attr_qname(start, "text:style-name");
  usize item_start = st->list_item_count;

  if (alloc_block_at(st, nested_target, &list_blk) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  list_blk->kind = DOC_MODEL_BLOCK_LIST;
  clear_style_id(&list_blk->style_id);
  list_blk->as.list.ordered = style_eq(style_attr, "LOrdered") ? 1 : 0;

  for (;;) {
    xml_token tok;
    if (xml_reader_next(xr, &tok) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_END_ELEM && xml_sv_eq_cstr(tok.qname, "text:list")) {
      break;
    }
    if (tok.kind == XML_TOK_START_ELEM && xml_sv_eq_cstr(tok.qname, "text:list-item")) {
      doc_model_block* item;
      if (st->list_item_count >= FMT_ODT_MAX_LIST_ITEMS) {
        return CONVERT_ERR_LIMIT;
      }
      item = &st->list_item_blocks[st->list_item_count++];
      if (parse_list_item_start(xr, st, item, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }
  }

  list_blk->as.list.items.items = &st->list_item_blocks[item_start];
  list_blk->as.list.items.count = st->list_item_count - item_start;
  return CONVERT_OK;
}

static int parse_heading_start(xml_reader* xr,
                               fmt_odt_state* st,
                               const xml_token* start,
                               int nested_target,
                               convert_diagnostics* diags) {
  doc_model_block* blk;
  const xml_attr* level_attr = find_attr_qname(start, "text:outline-level");
  u8 level = 1;
  usize inl_start = st->inline_count;
  usize inl_count = 0;

  if (level_attr != 0 && parse_u8_decimal(level_attr->value, &level) != CONVERT_OK) {
    level = 1;
  }
  if (level < 1 || level > 6) {
    level = 1;
  }

  if (parse_inline_until_end(xr, st, "text:h", 0, &inl_count, diags) != CONVERT_OK) {
    return CONVERT_ERR_INVALID;
  }
  if (alloc_block_at(st, nested_target, &blk) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }

  blk->kind = DOC_MODEL_BLOCK_HEADING;
  clear_style_id(&blk->style_id);
  blk->as.heading.level = level;
  blk->as.heading.inlines.items = &st->inlines[inl_start];
  blk->as.heading.inlines.count = inl_count;
  return CONVERT_OK;
}

static int parse_paragraph_start(xml_reader* xr,
                                 fmt_odt_state* st,
                                 const xml_token* start,
                                 int nested_target,
                                 convert_diagnostics* diags) {
  const xml_attr* style_attr = find_attr_qname(start, "text:style-name");
  int is_code = 0;
  int is_quote = 0;

  if (style_attr != 0) {
        is_code = style_eq(style_attr, "Preformatted_20_Text") || style_eq(style_attr, "PCode");
        is_quote = style_eq(style_attr, "Quotations") || style_eq(style_attr, "PQuote");
  }

  if (is_code) {
    doc_model_block* blk;
    doc_model_sv code_text;
    if (collect_plain_text_until_end(xr, st, "text:p", &code_text) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (alloc_block_at(st, nested_target, &blk) != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    blk->kind = DOC_MODEL_BLOCK_CODE_BLOCK;
    clear_style_id(&blk->style_id);
    blk->as.code_block.language.data = 0;
    blk->as.code_block.language.len = 0;
    blk->as.code_block.text = code_text;
    return CONVERT_OK;
  }

  if (is_quote) {
    if (nested_target) {
      is_quote = 0;
    }
  }

  if (is_quote) {
    doc_model_block* quote_blk;
    doc_model_block* para_blk;
    usize inl_start = st->inline_count;
    usize inl_count = 0;
    usize nested_start = st->nested_block_count;

    if (parse_inline_until_end(xr, st, "text:p", 0, &inl_count, diags) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (st->nested_block_count >= FMT_ODT_MAX_NESTED_BLOCKS) {
      return CONVERT_ERR_LIMIT;
    }
    para_blk = &st->nested_blocks[st->nested_block_count++];
    para_blk->kind = DOC_MODEL_BLOCK_PARAGRAPH;
    para_blk->style_id.data = 0;
    para_blk->style_id.len = 0;
    para_blk->as.paragraph.inlines.items = &st->inlines[inl_start];
    para_blk->as.paragraph.inlines.count = inl_count;

    if (alloc_block_at(st, nested_target, &quote_blk) != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    quote_blk->kind = DOC_MODEL_BLOCK_QUOTE;
    clear_style_id(&quote_blk->style_id);
    quote_blk->as.quote.blocks.items = &st->nested_blocks[nested_start];
    quote_blk->as.quote.blocks.count = st->nested_block_count - nested_start;
    return CONVERT_OK;
  }

  {
    doc_model_block* blk;
    usize inl_start = st->inline_count;
    usize inl_count = 0;
    if (parse_inline_until_end(xr, st, "text:p", 0, &inl_count, diags) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (alloc_block_at(st, nested_target, &blk) != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    blk->kind = DOC_MODEL_BLOCK_PARAGRAPH;
    clear_style_id(&blk->style_id);
    blk->as.paragraph.inlines.items = &st->inlines[inl_start];
    blk->as.paragraph.inlines.count = inl_count;
  }
  return CONVERT_OK;
}

static int parse_table_start(xml_reader* xr,
                             fmt_odt_state* st,
                             const xml_token* start,
                             int nested_target,
                             convert_diagnostics* diags) {
  doc_model_block* blk;
  usize row_start = st->table_row_count;
  (void)start;

  for (;;) {
    xml_token tok;
    if (xml_reader_next(xr, &tok) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_END_ELEM && xml_sv_eq_cstr(tok.qname, "table:table")) {
      break;
    }
    if (tok.kind != XML_TOK_START_ELEM) {
      continue;
    }

    if (xml_sv_eq_cstr(tok.qname, "table:table-row")) {
      usize row_cell_start = st->table_cell_count;
      for (;;) {
        xml_token ctok;
        if (xml_reader_next(xr, &ctok) != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
        if (ctok.kind == XML_TOK_EOF) {
          return CONVERT_ERR_INVALID;
        }
        if (ctok.kind == XML_TOK_END_ELEM && xml_sv_eq_cstr(ctok.qname, "table:table-row")) {
          break;
        }
        if (ctok.kind != XML_TOK_START_ELEM) {
          continue;
        }

        if (xml_sv_eq_cstr(ctok.qname, "table:table-cell")) {
          doc_model_table_cell* cell;
          doc_model_block* para;
          usize inl_start = st->inline_count;
          usize inl_count = 0;
          if (parse_inline_until_end(xr, st, "table:table-cell", 0, &inl_count, diags) != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
          if (st->table_cell_count >= FMT_ODT_MAX_TABLE_CELLS ||
              st->nested_block_count >= FMT_ODT_MAX_NESTED_BLOCKS) {
            return CONVERT_ERR_LIMIT;
          }
          cell = &st->table_cells[st->table_cell_count++];
          para = &st->nested_blocks[st->nested_block_count++];

          para->kind = DOC_MODEL_BLOCK_PARAGRAPH;
          para->style_id.data = 0;
          para->style_id.len = 0;
          para->as.paragraph.inlines.items = (inl_count > 0) ? &st->inlines[inl_start] : 0;
          para->as.paragraph.inlines.count = inl_count;

          cell->is_header = (st->table_row_count == row_start) ? 1 : 0;
          cell->blocks.items = para;
          cell->blocks.count = 1;
          continue;
        }

        if (xml_sv_eq_cstr(ctok.qname, "table:covered-table-cell")) {
          doc_model_table_cell* cell;
          doc_model_block* para;
          if (st->table_cell_count >= FMT_ODT_MAX_TABLE_CELLS ||
              st->nested_block_count >= FMT_ODT_MAX_NESTED_BLOCKS) {
            return CONVERT_ERR_LIMIT;
          }
          cell = &st->table_cells[st->table_cell_count++];
          para = &st->nested_blocks[st->nested_block_count++];

          para->kind = DOC_MODEL_BLOCK_PARAGRAPH;
          para->style_id.data = 0;
          para->style_id.len = 0;
          para->as.paragraph.inlines.items = 0;
          para->as.paragraph.inlines.count = 0;

          cell->is_header = (st->table_row_count == row_start) ? 1 : 0;
          cell->blocks.items = para;
          cell->blocks.count = 1;
          if (collect_plain_text_unknown_elem(xr, st) != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
          continue;
        }

        if (collect_plain_text_unknown_elem(xr, st) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }

      if (st->table_cell_count > row_cell_start) {
        doc_model_table_row* row;
        if (st->table_row_count >= FMT_ODT_MAX_TABLE_ROWS) {
          return CONVERT_ERR_LIMIT;
        }
        row = &st->table_rows[st->table_row_count++];
        row->cells = &st->table_cells[row_cell_start];
        row->cell_count = st->table_cell_count - row_cell_start;
      }
      continue;
    }

    if (collect_plain_text_unknown_elem(xr, st) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (st->table_row_count == row_start) {
    return emit_placeholder_paragraph(st, nested_target, start->qname, diags);
  }

  if (alloc_block_at(st, nested_target, &blk) != CONVERT_OK) {
    return CONVERT_ERR_LIMIT;
  }
  blk->kind = DOC_MODEL_BLOCK_TABLE;
  blk->style_id.data = 0;
  blk->style_id.len = 0;
  blk->as.table.rows = &st->table_rows[row_start];
  blk->as.table.row_count = st->table_row_count - row_start;
  return CONVERT_OK;
}

static int parse_section_start(xml_reader* xr,
                               fmt_odt_state* st,
                               const xml_token* start,
                               int nested_target,
                               convert_diagnostics* diags) {
  const xml_attr* name_attr = find_attr_qname(start, "text:name");
  if (name_attr != 0) {
    doc_model_block* hblk;
    doc_model_inline* hinl;
    doc_model_sv htext;
    usize hstart = st->text_len;
    if (append_import_text(st, "Section: ", 9, 0) != CONVERT_OK ||
        append_import_text(st, name_attr->value.data, name_attr->value.len, 0) != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    htext.data = st->text + hstart;
    htext.len = st->text_len - hstart;
    if (alloc_inline_at(st, 0, &hinl) != CONVERT_OK || alloc_block_at(st, nested_target, &hblk) != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    hinl->kind = DOC_MODEL_INLINE_TEXT;
    hinl->style_id.data = 0;
    hinl->style_id.len = 0;
    hinl->as.text.text = htext;
    hblk->kind = DOC_MODEL_BLOCK_HEADING;
    hblk->style_id.data = 0;
    hblk->style_id.len = 0;
    hblk->as.heading.level = 2;
    hblk->as.heading.inlines.items = hinl;
    hblk->as.heading.inlines.count = 1;
  }
  return parse_block_stream_until(xr, st, nested_target, "text:section", diags);
}

static int parse_block_stream_until(xml_reader* xr,
                                    fmt_odt_state* st,
                                    int nested_target,
                                    const char* end_qname,
                                    convert_diagnostics* diags) {
  for (;;) {
    xml_token tok;
    if (xml_reader_next(xr, &tok) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      return end_qname[0] == '\0' ? CONVERT_OK : CONVERT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_END_ELEM && end_qname[0] != '\0' && xml_sv_eq_cstr(tok.qname, end_qname)) {
      return CONVERT_OK;
    }
    if (tok.kind != XML_TOK_START_ELEM) {
      continue;
    }
    if (xml_sv_eq_cstr(tok.qname, "text:p")) {
      if (parse_paragraph_start(xr, st, &tok, nested_target, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }
    if (xml_sv_eq_cstr(tok.qname, "text:h")) {
      if (parse_heading_start(xr, st, &tok, nested_target, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }
    if (xml_sv_eq_cstr(tok.qname, "text:list")) {
      if (parse_list_start(xr, st, &tok, nested_target, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }

    if (xml_sv_eq_cstr(tok.qname, "text:section")) {
      if (parse_section_start(xr, st, &tok, nested_target, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }

    if (xml_sv_eq_cstr(tok.qname, "table:table")) {
      if (parse_table_start(xr, st, &tok, nested_target, diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }

    if (xml_sv_eq_cstr(tok.qname, "text:table-of-content")) {
      doc_model_block* hblk;
      doc_model_inline* hinl;
      doc_model_sv htxt;
      if (append_import_text(st, "Table of Contents", 17, &htxt) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      if (alloc_inline_at(st, 0, &hinl) != CONVERT_OK ||
          alloc_block_at(st, nested_target, &hblk) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      hinl->kind = DOC_MODEL_INLINE_TEXT;
      hinl->style_id.data = 0;
      hinl->style_id.len = 0;
      hinl->as.text.text = htxt;
      hblk->kind = DOC_MODEL_BLOCK_HEADING;
      hblk->style_id.data = 0;
      hblk->style_id.len = 0;
      hblk->as.heading.level = 2;
      hblk->as.heading.inlines.items = hinl;
      hblk->as.heading.inlines.count = 1;

      if (parse_block_stream_until(xr, st, nested_target, "text:table-of-content", diags) !=
          CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }

    if (xml_sv_eq_cstr(tok.qname, "text:table-of-content-source") ||
        xml_sv_eq_cstr(tok.qname, "text:index-title-template") ||
        xml_sv_eq_cstr(tok.qname, "text:table-of-content-entry-template") ||
        xml_sv_eq_cstr(tok.qname, "text:index-title")) {
      if (collect_plain_text_unknown_elem(xr, st) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }

    if (xml_sv_eq_cstr(tok.qname, "text:index-body")) {
      if (parse_block_stream_until(xr, st, nested_target, "text:index-body", diags) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
      continue;
    }

    if (emit_placeholder_paragraph(st, nested_target, tok.qname, diags) != CONVERT_OK) {
      return CONVERT_ERR_LIMIT;
    }
    if (collect_plain_text_unknown_elem(xr, st) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }
}

static int parse_office_text(xml_reader* xr, fmt_odt_state* st, convert_diagnostics* diags) {
  return parse_block_stream_until(xr, st, 0, "office:text", diags);
}

static int odt_import_plain_text(fmt_odt_state* st,
                                 const u8* input,
                                 usize input_len,
                                 convert_diagnostics* diags) {
  usize out_len = 0;
  int odt_rc;

  odt_rc = odt_core_extract_plain_text(input, input_len, st->text, sizeof(st->text), &out_len);
  if (odt_rc != ODT_OK) {
    const char* message =
      (odt_rc == ODT_ERR_TOO_LARGE) ? k_diag_plain_extract_too_large : k_diag_plain_extract_failed;
    (void)convert_diagnostics_push(diags,
                     CONVERT_DIAG_ERROR,
                     CONVERT_DIAG_HANDLER_FAILURE,
                     CONVERT_STAGE_PARSE,
                     message);
    return CONVERT_ERR_INVALID;
  }

  st->text_len = out_len;
  st->block_count = 0;
  st->inline_count = 0;

  if (st->text_len > 0) {
    doc_model_inline* inl;
    doc_model_block* blk;

    if (st->block_count >= FMT_ODT_MAX_BLOCKS || st->inline_count >= FMT_ODT_MAX_INLINES) {
      return CONVERT_ERR_LIMIT;
    }

    inl = &st->inlines[st->inline_count++];
    inl->kind = DOC_MODEL_INLINE_TEXT;
    inl->style_id.data = 0;
    inl->style_id.len = 0;
    inl->as.text.text.data = st->text;
    inl->as.text.text.len = st->text_len;

    blk = &st->blocks[st->block_count++];
    blk->kind = DOC_MODEL_BLOCK_PARAGRAPH;
    blk->style_id.data = 0;
    blk->style_id.len = 0;
    blk->as.paragraph.inlines.items = inl;
    blk->as.paragraph.inlines.count = 1;
  }
  return CONVERT_OK;
}

static int is_nested_list_item(const doc_model_block* item) {
  return item->style_id.len == 5 && item->style_id.data != 0 && item->style_id.data[0] == 'l' &&
         item->style_id.data[1] == 'i' && item->style_id.data[2] == '-' && item->style_id.data[3] == 'n' &&
         item->style_id.data[4] == '1';
}

static int odt_import(const convert_format_handler* handler,
                      const u8* input,
                      usize input_len,
                      convert_policy_mode policy,
                      convert_doc_buffer* doc,
                      convert_diagnostics* diags) {
  fmt_odt_state* st = (fmt_odt_state*)handler->user;
  zip_archive za;
  zip_entry_view content_entry;
  usize content_len = 0;
  int parsed_semantic = 0;

  (void)policy;

  if (st == 0 || doc == 0 || doc->doc == 0) {
    return CONVERT_ERR_INVALID;
  }

  st->block_count = 0;
  st->nested_block_count = 0;
  st->list_item_count = 0;
  st->inline_count = 0;
  st->aux_inline_count = 0;
  st->table_row_count = 0;
  st->table_cell_count = 0;
  st->text_len = 0;

  if (zip_archive_open(&za, input, input_len) == ZIP_OK &&
      zip_archive_find_entry(&za, "content.xml", &content_entry) == ZIP_OK &&
      zip_entry_extract(&content_entry, (u8*)st->plain, sizeof(st->plain), &content_len) == ZIP_OK) {
    xml_reader xr;
    xml_reader_init(&xr, st->plain, content_len);
    for (;;) {
      xml_token tok;
      if (xml_reader_next(&xr, &tok) != XML_OK) {
        break;
      }
      if (tok.kind == XML_TOK_EOF) {
        break;
      }
      if (tok.kind == XML_TOK_START_ELEM && xml_sv_eq_cstr(tok.qname, "office:text")) {
        if (parse_office_text(&xr, st, diags) == CONVERT_OK) {
          parsed_semantic = 1;
        }
        break;
      }
    }
  }

  if (!parsed_semantic) {
    diag_push_lossy_warn(diags, CONVERT_STAGE_PARSE, k_diag_plain_fallback);
    if (odt_import_plain_text(st, input, input_len, diags) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
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

  return CONVERT_OK;
}

static int write_inline_text(const doc_model_inline* inl,
                             xml_writer* xw,
                             convert_diagnostics* diags) {
  usize i;

  /* Resolve a style name from style_id, falling back to a hardcoded default. */
  char style_buf[256];
  const char* style_name = 0;

#define RESOLVE_STYLE_OR_FAIL(sv, fallback)                                               \
  do {                                                                                    \
    if ((sv).data != 0 && (sv).len > 0) {                                                 \
      if (sv_to_cstr((sv), style_buf, sizeof(style_buf)) != CONVERT_OK) {                \
        return CONVERT_ERR_LIMIT;                                                         \
      }                                                                                   \
      style_name = style_buf;                                                             \
    } else {                                                                              \
      style_name = (fallback);                                                            \
    }                                                                                     \
  } while (0)

  switch (inl->kind) {
    case DOC_MODEL_INLINE_TEXT:
      if (inl->style_id.data != 0 && inl->style_id.len > 0) {
        RESOLVE_STYLE_OR_FAIL(inl->style_id, "");
        if (xml_writer_start_elem(xw, "text:span") != XML_OK ||
            xml_writer_attr(xw, "text:style-name", style_name) != XML_OK ||
            xml_writer_text(xw, inl->as.text.text.data, inl->as.text.text.len) != XML_OK ||
            xml_writer_end_elem(xw, "text:span") != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
        return CONVERT_OK;
      }
      return (xml_writer_text(xw, inl->as.text.text.data, inl->as.text.text.len) == XML_OK)
               ? CONVERT_OK
               : CONVERT_ERR_INVALID;

    case DOC_MODEL_INLINE_CODE_SPAN:
      RESOLVE_STYLE_OR_FAIL(inl->style_id, "Source_20_Text");
      if (xml_writer_start_elem(xw, "text:span") != XML_OK ||
          xml_writer_attr(xw, "text:style-name", style_name) != XML_OK ||
          xml_writer_text(xw, inl->as.code_span.text.data, inl->as.code_span.text.len) != XML_OK ||
          xml_writer_end_elem(xw, "text:span") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;

    case DOC_MODEL_INLINE_EMPHASIS:
      RESOLVE_STYLE_OR_FAIL(inl->style_id, "Emphasis");
      if (xml_writer_start_elem(xw, "text:span") != XML_OK ||
          xml_writer_attr(xw, "text:style-name", style_name) != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      for (i = 0; i < inl->as.emphasis.children.count; i++) {
        if (write_inline_text(&inl->as.emphasis.children.items[i], xw, diags) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }
      if (xml_writer_end_elem(xw, "text:span") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;

    case DOC_MODEL_INLINE_STRONG:
      RESOLVE_STYLE_OR_FAIL(inl->style_id, "Strong_20_Emphasis");
      if (xml_writer_start_elem(xw, "text:span") != XML_OK ||
          xml_writer_attr(xw, "text:style-name", style_name) != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      for (i = 0; i < inl->as.strong.children.count; i++) {
        if (write_inline_text(&inl->as.strong.children.items[i], xw, diags) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }
      if (xml_writer_end_elem(xw, "text:span") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;

    case DOC_MODEL_INLINE_LINK: {
      char href_buf[1024];
      if (inl->as.link.href.data == 0 || inl->as.link.href.len == 0) {
        diag_push_lossy_warn(diags, CONVERT_STAGE_NORMALIZE_OUT, k_diag_link_empty);
        return CONVERT_OK;
      }
      if (sv_to_cstr(inl->as.link.href, href_buf, sizeof(href_buf)) != CONVERT_OK) {
        diag_push_lossy_warn(diags, CONVERT_STAGE_NORMALIZE_OUT, k_diag_link_too_large);
        return CONVERT_OK;
      }
      RESOLVE_STYLE_OR_FAIL(inl->style_id, "Internet_20_link");
      if (xml_writer_start_elem(xw, "text:a") != XML_OK ||
          xml_writer_attr(xw, "text:style-name", style_name) != XML_OK ||
          xml_writer_attr(xw, "xlink:type", "simple") != XML_OK ||
          xml_writer_attr(xw, "xlink:href", href_buf) != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      for (i = 0; i < inl->as.link.children.count; i++) {
        if (write_inline_text(&inl->as.link.children.items[i], xw, diags) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }
      if (xml_writer_end_elem(xw, "text:a") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;
    }

    case DOC_MODEL_INLINE_IMAGE:
      {
        char href_buf[1024];
        if (inl->as.image.asset_id.data == 0 || inl->as.image.asset_id.len == 0 ||
            sv_to_cstr(inl->as.image.asset_id, href_buf, sizeof(href_buf)) != CONVERT_OK) {
          diag_push_lossy_warn(diags, CONVERT_STAGE_NORMALIZE_OUT, k_diag_image_dropped);
          return CONVERT_OK;
        }
        if (xml_writer_start_elem(xw, "draw:frame") != XML_OK ||
            xml_writer_attr(xw, "text:anchor-type", "as-char") != XML_OK ||
            xml_writer_start_elem(xw, "draw:image") != XML_OK ||
            xml_writer_attr(xw, "xlink:type", "simple") != XML_OK ||
            xml_writer_attr(xw, "xlink:show", "embed") != XML_OK ||
            xml_writer_attr(xw, "xlink:actuate", "onLoad") != XML_OK ||
            xml_writer_attr(xw, "xlink:href", href_buf) != XML_OK ||
            xml_writer_end_elem(xw, "draw:image") != XML_OK ||
            xml_writer_end_elem(xw, "draw:frame") != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
        return CONVERT_OK;
      }

    case DOC_MODEL_INLINE_LINE_BREAK:
      if (xml_writer_start_elem(xw, "text:line-break") != XML_OK ||
          xml_writer_end_elem(xw, "text:line-break") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      return CONVERT_OK;

    default:
      diag_push_unsupported_warn(diags, CONVERT_STAGE_NORMALIZE_OUT, k_diag_inline_unsupported);
      return CONVERT_OK;
  }

#undef RESOLVE_STYLE_OR_FAIL
}

static int write_inline_list(const doc_model_inline_list* inlines,
                             xml_writer* xw,
                             convert_diagnostics* diags) {
  usize i;
  for (i = 0; i < inlines->count; i++) {
    if (write_inline_text(&inlines->items[i], xw, diags) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }
  return CONVERT_OK;
}

static int write_block(const doc_model_block* blk,
                       xml_writer* xw,
                       convert_diagnostics* diags,
                       usize depth) {
  char style_buf[256];
  const char* style_name;

#define RESOLVE_BLOCK_STYLE(sv, fallback)                                                 \
  do {                                                                                    \
    if ((sv).data != 0 && (sv).len > 0) {                                                 \
      if (sv_to_cstr((sv), style_buf, sizeof(style_buf)) != CONVERT_OK) {                \
        return CONVERT_ERR_LIMIT;                                                         \
      }                                                                                   \
      style_name = style_buf;                                                             \
    } else {                                                                              \
      style_name = (fallback);                                                            \
    }                                                                                     \
  } while (0)

  if (depth > 32) {
    return CONVERT_ERR_LIMIT;
  }

  if (blk->kind == DOC_MODEL_BLOCK_PARAGRAPH) {
    RESOLVE_BLOCK_STYLE(blk->style_id, inferred_paragraph_style(blk));
    if (xml_writer_start_elem(xw, "text:p") != XML_OK ||
        xml_writer_attr(xw, "text:style-name", style_name) != XML_OK ||
        write_inline_list(&blk->as.paragraph.inlines, xw, diags) != CONVERT_OK ||
        xml_writer_end_elem(xw, "text:p") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    return CONVERT_OK;
  }

  if (blk->kind == DOC_MODEL_BLOCK_HEADING) {
    char level_buf[8];
    const char* heading_style;
    usize n;
    u8 level = blk->as.heading.level;
    if (level < 1 || level > 6) {
      level = 1;
    }
    n = rt_u64_to_dec((u64)level, level_buf, sizeof(level_buf));
    if (n >= sizeof(level_buf)) {
      return CONVERT_ERR_INVALID;
    }
    level_buf[n] = '\0';

    if (blk->style_id.data != 0 && blk->style_id.len > 0) {
      if (sv_to_cstr(blk->style_id, style_buf, sizeof(style_buf)) != CONVERT_OK) {
        return CONVERT_ERR_LIMIT;
      }
      heading_style = style_buf;
    } else {
      heading_style = heading_style_for_level(level);
    }

    if (xml_writer_start_elem(xw, "text:h") != XML_OK ||
        xml_writer_attr(xw, "text:style-name", heading_style) != XML_OK ||
        xml_writer_attr(xw, "text:outline-level", level_buf) != XML_OK ||
        write_inline_list(&blk->as.heading.inlines, xw, diags) != CONVERT_OK ||
        xml_writer_end_elem(xw, "text:h") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    return CONVERT_OK;
  }

  if (blk->kind == DOC_MODEL_BLOCK_LIST) {
    usize i = 0;
    RESOLVE_BLOCK_STYLE(blk->style_id, blk->as.list.ordered ? "LOrdered" : "L1");
    if (xml_writer_start_elem(xw, "text:list") != XML_OK ||
        xml_writer_attr(xw, "text:style-name", style_name) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }

    while (i < blk->as.list.items.count) {
      const doc_model_block* item = &blk->as.list.items.items[i];
      usize j;
      usize next_i = i + 1;
      if (item->kind != DOC_MODEL_BLOCK_LIST_ITEM) {
        diag_push_unsupported_warn(
          diags, CONVERT_STAGE_NORMALIZE_OUT, k_diag_list_child_unsupported);
        i++;
        continue;
      }

      if (is_nested_list_item(item)) {
        if (xml_writer_start_elem(xw, "text:list-item") != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
        for (j = 0; j < item->as.list_item.blocks.count; j++) {
          if (write_block(&item->as.list_item.blocks.items[j], xw, diags, depth + 1) != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
        }
        if (xml_writer_end_elem(xw, "text:list-item") != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
        i++;
        continue;
      }

      if (xml_writer_start_elem(xw, "text:list-item") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }

      for (j = 0; j < item->as.list_item.blocks.count; j++) {
        if (write_block(&item->as.list_item.blocks.items[j], xw, diags, depth + 1) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }

      if (next_i < blk->as.list.items.count &&
          is_nested_list_item(&blk->as.list.items.items[next_i])) {
        RESOLVE_BLOCK_STYLE(blk->style_id, blk->as.list.ordered ? "LOrdered" : "L1");
        if (xml_writer_start_elem(xw, "text:list") != XML_OK ||
            xml_writer_attr(xw, "text:style-name", style_name) != XML_OK) {
          return CONVERT_ERR_INVALID;
        }

        while (next_i < blk->as.list.items.count &&
               is_nested_list_item(&blk->as.list.items.items[next_i])) {
          const doc_model_block* nested_item = &blk->as.list.items.items[next_i];
          if (xml_writer_start_elem(xw, "text:list-item") != XML_OK) {
            return CONVERT_ERR_INVALID;
          }
          for (j = 0; j < nested_item->as.list_item.blocks.count; j++) {
            if (write_block(&nested_item->as.list_item.blocks.items[j], xw, diags, depth + 1) !=
                CONVERT_OK) {
              return CONVERT_ERR_INVALID;
            }
          }
          if (xml_writer_end_elem(xw, "text:list-item") != XML_OK) {
            return CONVERT_ERR_INVALID;
          }
          next_i++;
        }

        if (xml_writer_end_elem(xw, "text:list") != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
      }

      if (xml_writer_end_elem(xw, "text:list-item") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }

      i = next_i;
    }

    if (xml_writer_end_elem(xw, "text:list") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    return CONVERT_OK;
  }

  if (blk->kind == DOC_MODEL_BLOCK_CODE_BLOCK) {
    usize i;
    usize seg_start = 0;
    RESOLVE_BLOCK_STYLE(blk->style_id, "Preformatted_20_Text");

    if (xml_writer_start_elem(xw, "text:p") != XML_OK ||
        xml_writer_attr(xw, "text:style-name", style_name) != XML_OK) {
      return CONVERT_ERR_INVALID;
    }

    for (i = 0; i <= blk->as.code_block.text.len; i++) {
      if (i == blk->as.code_block.text.len || blk->as.code_block.text.data[i] == '\n') {
        if (i > seg_start &&
            xml_writer_text(xw,
                            blk->as.code_block.text.data + seg_start,
                            i - seg_start) != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
        if (i < blk->as.code_block.text.len) {
          if (xml_writer_start_elem(xw, "text:line-break") != XML_OK ||
              xml_writer_end_elem(xw, "text:line-break") != XML_OK) {
            return CONVERT_ERR_INVALID;
          }
        }
        seg_start = i + 1;
      }
    }

    if (xml_writer_end_elem(xw, "text:p") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    return CONVERT_OK;
  }

  if (blk->kind == DOC_MODEL_BLOCK_TABLE) {
    usize r;
    if (xml_writer_start_elem(xw, "table:table") != XML_OK ||
        xml_writer_attr(xw, "table:name", "Table1") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (blk->style_id.data != 0 && blk->style_id.len > 0) {
      RESOLVE_BLOCK_STYLE(blk->style_id, "");
      if (xml_writer_attr(xw, "table:style-name", style_name) != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
    }
    for (r = 0; r < blk->as.table.row_count; r++) {
      const doc_model_table_row* row = &blk->as.table.rows[r];
      usize c;
      if (xml_writer_start_elem(xw, "table:table-row") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
      for (c = 0; c < row->cell_count; c++) {
        const doc_model_table_cell* cell = &row->cells[c];
        usize b;
        if (xml_writer_start_elem(xw, "table:table-cell") != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
        if (cell->blocks.count == 0) {
          if (xml_writer_start_elem(xw, "text:p") != XML_OK ||
              xml_writer_end_elem(xw, "text:p") != XML_OK) {
            return CONVERT_ERR_INVALID;
          }
        }
        for (b = 0; b < cell->blocks.count; b++) {
          if (write_block(&cell->blocks.items[b], xw, diags, depth + 1) != CONVERT_OK) {
            return CONVERT_ERR_INVALID;
          }
        }
        if (xml_writer_end_elem(xw, "table:table-cell") != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
      }
      if (xml_writer_end_elem(xw, "table:table-row") != XML_OK) {
        return CONVERT_ERR_INVALID;
      }
    }
    if (xml_writer_end_elem(xw, "table:table") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    return CONVERT_OK;
  }

  if (blk->kind == DOC_MODEL_BLOCK_QUOTE) {
    usize i;
    RESOLVE_BLOCK_STYLE(blk->style_id, "Quotations");
    for (i = 0; i < blk->as.quote.blocks.count; i++) {
      const doc_model_block* qb = &blk->as.quote.blocks.items[i];
      if (qb->kind == DOC_MODEL_BLOCK_PARAGRAPH) {
        const char* qstyle = style_name;
        if (qb->style_id.data != 0 && qb->style_id.len > 0) {
          if (sv_to_cstr(qb->style_id, style_buf, sizeof(style_buf)) != CONVERT_OK) {
            return CONVERT_ERR_LIMIT;
          }
          qstyle = style_buf;
        }
        if (xml_writer_start_elem(xw, "text:p") != XML_OK ||
            xml_writer_attr(xw, "text:style-name", qstyle) != XML_OK ||
            write_inline_list(&qb->as.paragraph.inlines, xw, diags) != CONVERT_OK ||
            xml_writer_end_elem(xw, "text:p") != XML_OK) {
          return CONVERT_ERR_INVALID;
        }
      } else {
        if (write_block(qb, xw, diags, depth + 1) != CONVERT_OK) {
          return CONVERT_ERR_INVALID;
        }
      }
    }
    return CONVERT_OK;
  }

  diag_push_unsupported_warn(diags, CONVERT_STAGE_NORMALIZE_OUT, k_diag_block_unsupported);
  return CONVERT_OK;

#undef RESOLVE_BLOCK_STYLE
}

static int build_content_xml(const doc_model_document* doc,
                             u8* dst,
                             usize dst_cap,
                             usize* out_len,
                             convert_diagnostics* diags) {
  xml_writer xw;
  usize i;
  usize two_col_start = find_two_column_start_index(doc);
  int has_two_col_section = two_col_start < doc->blocks.count;

  xml_writer_init(&xw, dst, dst_cap);
  if (xml_writer_decl(&xw) != XML_OK ||
      xml_writer_start_elem(&xw, "office:document-content") != XML_OK ||
      xml_writer_attr(&xw,
                      "xmlns:office",
                      "urn:oasis:names:tc:opendocument:xmlns:office:1.0") != XML_OK ||
      xml_writer_attr(&xw,
                      "xmlns:text",
                      "urn:oasis:names:tc:opendocument:xmlns:text:1.0") != XML_OK ||
      xml_writer_attr(&xw,
                      "xmlns:style",
                      "urn:oasis:names:tc:opendocument:xmlns:style:1.0") != XML_OK ||
      xml_writer_attr(&xw,
                      "xmlns:fo",
                      "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0") != XML_OK ||
      xml_writer_attr(&xw,
                      "xmlns:xlink",
                      "http://www.w3.org/1999/xlink") != XML_OK ||
      xml_writer_attr(&xw,
              "xmlns:table",
              "urn:oasis:names:tc:opendocument:xmlns:table:1.0") != XML_OK ||
      xml_writer_attr(&xw,
              "xmlns:draw",
              "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0") != XML_OK ||
      xml_writer_attr(&xw, "office:version", "1.4") != XML_OK) {
    return CONVERT_ERR_INVALID;
  }

  if (has_two_col_section) {
    if (xml_writer_start_elem(&xw, "office:automatic-styles") != XML_OK ||
        xml_writer_start_elem(&xw, "style:style") != XML_OK ||
        xml_writer_attr(&xw, "style:name", "SectCols2") != XML_OK ||
        xml_writer_attr(&xw, "style:family", "section") != XML_OK ||
        xml_writer_start_elem(&xw, "style:section-properties") != XML_OK ||
        xml_writer_start_elem(&xw, "style:columns") != XML_OK ||
        xml_writer_attr(&xw, "fo:column-count", "2") != XML_OK ||
        xml_writer_attr(&xw, "fo:column-gap", "0.7cm") != XML_OK ||
        xml_writer_end_elem(&xw, "style:columns") != XML_OK ||
        xml_writer_end_elem(&xw, "style:section-properties") != XML_OK ||
        xml_writer_end_elem(&xw, "style:style") != XML_OK ||
        xml_writer_end_elem(&xw, "office:automatic-styles") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (xml_writer_start_elem(&xw, "office:body") != XML_OK ||
      xml_writer_start_elem(&xw, "office:text") != XML_OK) {
    return CONVERT_ERR_INVALID;
  }

  for (i = 0; i < two_col_start; i++) {
    if (write_block(&doc->blocks.items[i], &xw, diags, 0) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (has_two_col_section) {
    if (xml_writer_start_elem(&xw, "text:section") != XML_OK ||
        xml_writer_attr(&xw, "text:style-name", "SectCols2") != XML_OK ||
        xml_writer_attr(&xw, "text:name", "BodyTwoColumns") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
    for (i = two_col_start; i < doc->blocks.count; i++) {
      if (write_block(&doc->blocks.items[i], &xw, diags, 0) != CONVERT_OK) {
        return CONVERT_ERR_INVALID;
      }
    }
    if (xml_writer_end_elem(&xw, "text:section") != XML_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (xml_writer_end_elem(&xw, "office:text") != XML_OK ||
      xml_writer_end_elem(&xw, "office:body") != XML_OK ||
      xml_writer_end_elem(&xw, "office:document-content") != XML_OK ||
      xml_writer_finish(&xw, out_len) != XML_OK) {
    return CONVERT_ERR_INVALID;
  }

  return CONVERT_OK;
}

static int build_minimal_package(const u8* content_xml,
                                 usize content_len,
                                 u8* out,
                                 usize out_cap,
                                 usize* out_len) {
  static zip_writer zw;
  static u8 styles_xml[4096];
  usize styles_xml_len = 0;

  static const u8 styles_doc_deflated[] = {
      0xb5, 0x57, 0xdb, 0x8e, 0xda, 0x30, 0x10, 0xfd, 0x15, 0xe4, 0x3e, 0x67, 0x09, 0x97,
      0x5d, 0xb1, 0x11, 0x61, 0x9f, 0x5a, 0xb5, 0x52, 0xa5, 0xb6, 0xda, 0xed, 0xf3, 0xca,
      0x49, 0x26, 0x60, 0xd5, 0xf6, 0x44, 0xb6, 0xb3, 0x85, 0x7e, 0x7d, 0xed, 0x5c, 0xd8,
      0x90, 0x82, 0x30, 0x25, 0xf0, 0x80, 0xc8, 0xd8, 0x33, 0x73, 0xce, 0x19, 0xc7, 0x33,
      0x2c, 0x9f, 0xb6, 0x82, 0x8f, 0xde, 0x40, 0x69, 0x86, 0x32, 0x26, 0x93, 0xbb, 0x90,
      0x8c, 0x40, 0xa6, 0x98, 0x31, 0xb9, 0x8e, 0xc9, 0xcf, 0x97, 0x4f, 0xc1, 0x82, 0x3c,
      0xad, 0x96, 0x98, 0xe7, 0x2c, 0x85, 0x28, 0xc3, 0xb4, 0x14, 0x20, 0x4d, 0xa0, 0xcd,
      0x8e, 0x83, 0x1e, 0x59, 0x5f, 0xa9, 0xa3, 0x7a, 0x31, 0x26, 0xa5, 0x92, 0x11, 0x52,
      0xcd, 0x74, 0x24, 0xa9, 0x00, 0x1d, 0x99, 0x34, 0xc2, 0x02, 0x64, 0xeb, 0x14, 0x75,
      0x77, 0x47, 0x55, 0xa6, 0xda, 0x52, 0x05, 0xf3, 0x75, 0xaf, 0x36, 0x77, 0xbd, 0x0d,
      0x6c, 0x8d, 0xaf, 0xb3, 0xdb, 0xdb, 0xf5, 0xcd, 0xd1, 0xd7, 0x73, 0xab, 0x79, 0x90,
      0x63, 0x90, 0xa2, 0x28, 0xa8, 0x61, 0x49, 0x0b, 0xa1, 0x21, 0xd3, 0xd1, 0x6f, 0x4e,
      0xf6, 0x6a, 0xd5, 0x22, 0xad, 0x96, 0x35, 0xe4, 0xea, 0x7b, 0x54, 0xff, 0x76, 0x99,
      0x62, 0xf2, 0x6c, 0xa8, 0xcc, 0xa8, 0xca, 0x48, 0x63, 0xcd, 0xa9, 0x60, 0x7c, 0x17,
      0x93, 0x82, 0x2a, 0xba, 0x56, 0xb4, 0xd8, 0xb4, 0x0b, 0x29, 0xa7, 0x5a, 0xc7, 0xc4,
      0xa1, 0x27, 0xe3, 0xd3, 0xf1, 0x3e, 0x03, 0x75, 0x65, 0x3b, 0x1b, 0xce, 0x1a, 0xf6,
      0x35, 0x0c, 0x8e, 0x43, 0x69, 0x32, 0xa6, 0x1b, 0x5a, 0x18, 0x50, 0xa4, 0xcd, 0xe9,
      0x10, 0x04, 0x85, 0xb2, 0xfa, 0x28, 0xc3, 0x6c, 0xfd, 0x73, 0xb4, 0x12, 0xda, 0x50,
      0xbf, 0x81, 0xad, 0x37, 0xb6, 0x0a, 0x09, 0xf2, 0xcc, 0x21, 0x1c, 0x77, 0x20, 0x9e,
      0xc6, 0xfb, 0x62, 0xa3, 0xbd, 0x4e, 0xc3, 0xd7, 0x04, 0xb3, 0xdd, 0x80, 0xa0, 0x2b,
      0x99, 0xda, 0xac, 0xfb, 0x38, 0x3d, 0xd8, 0x82, 0xaa, 0x35, 0x93, 0x81, 0xc1, 0x22,
      0x26, 0x61, 0x2a, 0x48, 0xc7, 0x96, 0xa0, 0x31, 0x28, 0xac, 0xf9, 0x6e, 0xfe, 0xf8,
      0x68, 0x97, 0xbc, 0xf9, 0x34, 0xfa, 0x3b, 0x4a, 0x93, 0xff, 0xe1, 0xd3, 0xab, 0x5f,
      0x06, 0x39, 0x2d, 0xb9, 0x09, 0xb0, 0x34, 0x9c, 0x49, 0x08, 0x38, 0xbc, 0x01, 0xb7,
      0x47, 0xec, 0x6c, 0x35, 0x34, 0xfb, 0x63, 0xa3, 0x4d, 0xe7, 0x85, 0x21, 0x57, 0x56,
      0xa8, 0xc3, 0x68, 0x7a, 0x3b, 0x46, 0x53, 0x4f, 0x46, 0x93, 0xc5, 0xa0, 0x8c, 0x66,
      0xb7, 0x63, 0x34, 0xf3, 0x65, 0x34, 0x6c, 0x8d, 0xe6, 0xb7, 0x63, 0x34, 0xf7, 0x65,
      0x34, 0x1b, 0x94, 0xd1, 0xfd, 0xed, 0x18, 0xdd, 0xfb, 0x32, 0x9a, 0x0e, 0xca, 0xe8,
      0xe1, 0x76, 0x8c, 0x1e, 0x7c, 0x19, 0x4d, 0xae, 0x67, 0xf4, 0x5d, 0x41, 0x8e, 0x4a,
      0x50, 0x63, 0x20, 0x73, 0xb4, 0xdc, 0x5d, 0x7e, 0xd5, 0x1d, 0x7e, 0x0e, 0x7a, 0x1b,
      0x54, 0xa0, 0x44, 0x5d, 0xd0, 0x14, 0x48, 0x9f, 0x55, 0x58, 0x18, 0x7f, 0xfc, 0x3f,
      0x4a, 0x34, 0xb6, 0x91, 0xa3, 0xd4, 0x83, 0xa0, 0x3e, 0xd3, 0x66, 0x38, 0xe4, 0x56,
      0xe1, 0xc9, 0x61, 0x9f, 0x51, 0xb5, 0xee, 0x93, 0x4b, 0x5a, 0xcc, 0xb3, 0x51, 0x58,
      0x9f, 0xa3, 0x8f, 0xa2, 0xd8, 0xb8, 0xa9, 0xa5, 0x0f, 0xff, 0xa0, 0xfb, 0x0d, 0xdb,
      0xaf, 0xaf, 0x4a, 0xd9, 0x0c, 0x79, 0xcc, 0x50, 0xce, 0xd2, 0x0b, 0x08, 0x63, 0xa9,
      0x52, 0x38, 0x75, 0xc2, 0xbc, 0x32, 0xff, 0x7b, 0x72, 0xbc, 0xb3, 0x7f, 0x91, 0x76,
      0xfa, 0x91, 0x50, 0x4d, 0x29, 0xf6, 0x4d, 0xfb, 0x75, 0x69, 0xfe, 0x14, 0x39, 0xaa,
      0x98, 0x7c, 0x08, 0xed, 0x67, 0x11, 0xb6, 0xde, 0xd5, 0xc6, 0x52, 0x66, 0xa0, 0xaa,
      0xb7, 0xb7, 0x51, 0x46, 0x23, 0x67, 0x47, 0x8a, 0x51, 0x8d, 0xac, 0x9c, 0xe9, 0x46,
      0xc1, 0x03, 0x78, 0x5f, 0xdd, 0x2c, 0xf0, 0xbe, 0xa1, 0xba, 0x06, 0x9a, 0x73, 0x9a,
      0x94, 0x9c, 0x83, 0x19, 0xd5, 0x8b, 0xed, 0xe0, 0x50, 0x3f, 0xd6, 0x4b, 0x81, 0x9d,
      0xed, 0x2c, 0xb4, 0xc0, 0x65, 0xf4, 0x0e, 0x31, 0x3d, 0x19, 0x62, 0xdc, 0xc3, 0x79,
      0x06, 0xf8, 0x37, 0x65, 0xd9, 0x43, 0x76, 0x12, 0xbe, 0x2c, 0x45, 0x02, 0xaa, 0x0f,
      0xbf, 0x09, 0x51, 0x8a, 0xa0, 0xbe, 0x7b, 0x9c, 0xf1, 0x68, 0xee, 0x71, 0x6f, 0x02,
      0x1f, 0x1f, 0xff, 0xff, 0xb2, 0xfa, 0x0b,
  };
  static const usize styles_doc_deflated_len = 623;
  static const usize styles_doc_plain_len = 3326;

  if (deflate_inflate(styles_doc_deflated,
                      styles_doc_deflated_len,
                      styles_xml,
                      sizeof(styles_xml),
                      &styles_xml_len) != DEFLATE_OK ||
      styles_xml_len != styles_doc_plain_len) {
    return CONVERT_ERR_INVALID;
  }

  zip_writer_init(&zw, out, out_cap);

  if (zip_writer_add_entry(&zw,
                           "mimetype",
                           (const u8*)ODT_MIMETYPE,
                           rt_strlen(ODT_MIMETYPE),
                           ZIP_METHOD_STORE) != ZIP_OK ||
      zip_writer_add_entry(&zw, "content.xml", content_xml, content_len, ZIP_METHOD_STORE) != ZIP_OK ||
      zip_writer_add_entry(&zw,
                           "styles.xml",
                           styles_xml,
                           styles_xml_len,
                           ZIP_METHOD_DEFLATE) != ZIP_OK ||
      zip_writer_add_entry(&zw,
                           "meta.xml",
                           (const u8*)ODT_META_DOC_MINIMAL,
                           rt_strlen(ODT_META_DOC_MINIMAL),
                           ZIP_METHOD_DEFLATE) != ZIP_OK ||
      zip_writer_add_entry(&zw,
                           "META-INF/manifest.xml",
                           (const u8*)ODT_MANIFEST_DOC_MINIMAL,
                           rt_strlen(ODT_MANIFEST_DOC_MINIMAL),
                           ZIP_METHOD_DEFLATE) != ZIP_OK ||
      zip_writer_finish(&zw, out_len) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }

  return CONVERT_OK;
}

static int zip_entry_name_eq(const zip_entry_view* ze, const char* lit) {
  usize i = 0;
  while (lit[i] != '\0') {
    if (i >= (usize)ze->name_len || ze->name[i] != lit[i]) {
      return 0;
    }
    i++;
  }
  return i == (usize)ze->name_len;
}

static int cstr_ends_with(const char* s, const char* suffix) {
  usize slen = rt_strlen(s);
  usize xlen = rt_strlen(suffix);
  usize i;
  if (xlen > slen) {
    return 0;
  }
  for (i = 0; i < xlen; i++) {
    if (s[slen - xlen + i] != suffix[i]) {
      return 0;
    }
  }
  return 1;
}

static const char* manifest_media_type_for_path(const char* path) {
  if (cstr_ends_with(path, "/")) {
    return "";
  }
  if (cstr_eq(path, "content.xml") || cstr_eq(path, "styles.xml") || cstr_eq(path, "meta.xml") ||
      cstr_eq(path, "settings.xml") || cstr_eq(path, "META-INF/manifest.xml") ||
      cstr_ends_with(path, ".xml")) {
    return "text/xml";
  }
  if (cstr_ends_with(path, ".png")) {
    return "image/png";
  }
  if (cstr_ends_with(path, ".jpg") || cstr_ends_with(path, ".jpeg")) {
    return "image/jpeg";
  }
  if (cstr_ends_with(path, ".gif")) {
    return "image/gif";
  }
  if (cstr_ends_with(path, ".svg")) {
    return "image/svg+xml";
  }
  if (cstr_ends_with(path, ".bin")) {
    return "application/octet-stream";
  }
  return "";
}

static int manifest_add_entry(xml_writer* xw, const char* full_path, const char* media_type) {
  if (xml_writer_start_elem(xw, "manifest:file-entry") != XML_OK ||
      xml_writer_attr(xw, "manifest:full-path", full_path) != XML_OK ||
      xml_writer_attr(xw, "manifest:media-type", media_type) != XML_OK ||
      xml_writer_end_elem(xw, "manifest:file-entry") != XML_OK) {
    return CONVERT_ERR_INVALID;
  }
  return CONVERT_OK;
}

static int build_manifest_xml(const char names[][ZIP_WRITER_MAX_NAME + 1],
                              usize count,
                              u8* out,
                              usize out_cap,
                              usize* out_len) {
  xml_writer xw;
  usize i;

  xml_writer_init(&xw, out, out_cap);
  if (xml_writer_decl(&xw) != XML_OK || xml_writer_start_elem(&xw, "manifest:manifest") != XML_OK ||
      xml_writer_attr(&xw, "xmlns:manifest", "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0") !=
          XML_OK ||
      xml_writer_attr(&xw, "manifest:version", "1.4") != XML_OK) {
    return CONVERT_ERR_INVALID;
  }

  if (manifest_add_entry(&xw, "/", ODT_MIMETYPE) != CONVERT_OK ||
      manifest_add_entry(&xw, "content.xml", "text/xml") != CONVERT_OK) {
    return CONVERT_ERR_INVALID;
  }

  for (i = 0; i < count; i++) {
    const char* path = names[i];
    if (cstr_eq(path, "mimetype") || cstr_eq(path, "content.xml") ||
        cstr_eq(path, "META-INF/manifest.xml")) {
      continue;
    }
    if (manifest_add_entry(&xw, path, manifest_media_type_for_path(path)) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (manifest_add_entry(&xw, "META-INF/manifest.xml", "text/xml") != CONVERT_OK ||
      xml_writer_end_elem(&xw, "manifest:manifest") != XML_OK || xml_writer_finish(&xw, out_len) != XML_OK) {
    return CONVERT_ERR_INVALID;
  }

  return CONVERT_OK;
}

static int build_template_package(const fmt_odt_state* st,
                                  const u8* content_xml,
                                  usize content_len,
                                  u8* out,
                                  usize out_cap,
                                  usize* out_len) {
  static zip_writer zw;
  static u8 manifest_xml[ODT_CORE_MAX_MANIFEST_XML_BYTES];
  static char kept_names[ZIP_WRITER_MAX_ENTRIES][ZIP_WRITER_MAX_NAME + 1];
  zip_archive za;
  u16 entry_count = 0;
  u16 i;
  usize kept_count = 0;
  usize manifest_len = 0;

  if (zip_archive_open(&za, st->template_data, st->template_len) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }
  if (zip_archive_entry_count(&za, &entry_count) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }

  zip_writer_init(&zw, out, out_cap);

  /* ODT requires mimetype to be first and stored. */
  if (zip_writer_add_entry(&zw,
                           "mimetype",
                           (const u8*)ODT_MIMETYPE,
                           rt_strlen(ODT_MIMETYPE),
                           ZIP_METHOD_STORE) != ZIP_OK ||
      zip_writer_add_entry(&zw, "content.xml", content_xml, content_len, ZIP_METHOD_STORE) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }

  for (i = 0; i < entry_count; i++) {
    zip_entry_view ze;
    char name_buf[ZIP_WRITER_MAX_NAME + 1];
    usize j;
    usize k;

    if (zip_archive_get_entry(&za, i, &ze) != ZIP_OK) {
      return CONVERT_ERR_INVALID;
    }
    if (zip_entry_name_is_safe(&ze) != ZIP_OK) {
      return CONVERT_ERR_INVALID;
    }

    /* Inject generated content and canonical mimetype, keep the rest from template. */
    if (zip_entry_name_eq(&ze, "mimetype") || zip_entry_name_eq(&ze, "content.xml") ||
        zip_entry_name_eq(&ze, "META-INF/manifest.xml")) {
      continue;
    }
    if (ze.name_len > ZIP_WRITER_MAX_NAME) {
      return CONVERT_ERR_LIMIT;
    }

    for (j = 0; j < (usize)ze.name_len; j++) {
      name_buf[j] = ze.name[j];
    }
    name_buf[ze.name_len] = '\0';

    for (k = 0; k < kept_count; k++) {
      if (cstr_eq(kept_names[k], name_buf)) {
        return CONVERT_ERR_INVALID;
      }
    }

    if (kept_count >= ZIP_WRITER_MAX_ENTRIES) {
      return CONVERT_ERR_LIMIT;
    }
    for (j = 0; j < (usize)ze.name_len; j++) {
      kept_names[kept_count][j] = name_buf[j];
    }
    kept_names[kept_count][ze.name_len] = '\0';
    kept_count++;

    if (zip_writer_add_raw_entry(&zw,
                                 name_buf,
                                 ze.method,
                                 ze.crc32,
                                 ze.comp_size,
                                 ze.uncomp_size,
                                 ze.archive_data + ze.data_pos,
                                 (usize)ze.comp_size) != ZIP_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (build_manifest_xml(kept_names, kept_count, manifest_xml, sizeof(manifest_xml), &manifest_len) !=
          CONVERT_OK ||
      zip_writer_add_entry(
          &zw, "META-INF/manifest.xml", manifest_xml, manifest_len, ZIP_METHOD_DEFLATE) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }

  if (zip_writer_finish(&zw, out_len) != ZIP_OK) {
    return CONVERT_ERR_INVALID;
  }

  return CONVERT_OK;
}

static int odt_export(const convert_format_handler* handler,
                      const convert_doc_buffer* doc,
                      convert_policy_mode policy,
                      u8* output,
                      usize output_cap,
                      usize* output_len,
                      convert_diagnostics* diags) {
  fmt_odt_state* st = (fmt_odt_state*)handler->user;
  usize content_len = 0;

  (void)policy;

  if (st == 0 || doc == 0 || doc->doc == 0 || output == 0 || output_len == 0) {
    return CONVERT_ERR_INVALID;
  }

  if (st->use_template && (st->template_data == 0 || st->template_len == 0)) {
    return CONVERT_ERR_INVALID;
  }

  if (build_content_xml(doc->doc, (u8*)st->plain, sizeof(st->plain), &content_len, diags) != CONVERT_OK) {
    return CONVERT_ERR_INVALID;
  }

  if (st->use_template) {
    if (build_template_package(st,
                               (const u8*)st->plain,
                               content_len,
                               output,
                               output_cap,
                               output_len) != CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  } else {
    if (build_minimal_package((const u8*)st->plain, content_len, output, output_cap, output_len) !=
        CONVERT_OK) {
      return CONVERT_ERR_INVALID;
    }
  }

  if (odt_core_validate_package(output, *output_len) != ODT_OK) {
    return CONVERT_ERR_INVALID;
  }

  return CONVERT_OK;
}

void fmt_odt_state_init(fmt_odt_state* state) {
  if (state == 0) {
    return;
  }

  state->handler.name = "odt";
  state->handler.import_doc = odt_import;
  state->handler.export_doc = odt_export;
  state->handler.user = state;
  state->template_data = 0;
  state->template_len = 0;
  state->use_template = 0;
  state->block_count = 0;
  state->nested_block_count = 0;
  state->list_item_count = 0;
  state->inline_count = 0;
  state->aux_inline_count = 0;
  state->table_row_count = 0;
  state->table_cell_count = 0;
  state->text_len = 0;
}

void fmt_odt_set_template(fmt_odt_state* state, const u8* template_data, usize template_len) {
  if (state == 0) {
    return;
  }
  if (template_data == 0 || template_len == 0) {
    state->template_data = 0;
    state->template_len = 0;
    state->use_template = 0;
    return;
  }
  state->template_data = template_data;
  state->template_len = template_len;
  state->use_template = 1;
}

void fmt_odt_clear_template(fmt_odt_state* state) {
  if (state == 0) {
    return;
  }
  state->template_data = 0;
  state->template_len = 0;
  state->use_template = 0;
}

int fmt_odt_register(convert_registry* registry, fmt_odt_state* state) {
  if (registry == 0 || state == 0) {
    return CONVERT_ERR_INVALID;
  }
  fmt_odt_state_init(state);
  return convert_registry_register(registry, &state->handler);
}
