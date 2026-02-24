#include "xml/xml.h"

#include "rt/rt.h"

static int sv_eq_cstr(xml_sv sv, const char* s) {
  usize i = 0;
  while (s[i] != '\0') {
    if (i >= sv.len || sv.data[i] != s[i]) {
      return 0;
    }
    i++;
  }
  return i == sv.len;
}

static int sv_eq_sv(xml_sv a, xml_sv b) {
  usize i;
  if (a.len != b.len) {
    return 0;
  }
  for (i = 0; i < a.len; i++) {
    if (a.data[i] != b.data[i]) {
      return 0;
    }
  }
  return 1;
}

static int is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_name_start(char c) {
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
    return 1;
  }
  return c == '_' || c == ':';
}

static int is_name_char(char c) {
  if (is_name_start(c)) {
    return 1;
  }
  return (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static void skip_ws(xml_reader* xr) {
  while (xr->pos < xr->len && is_space(xr->src[xr->pos])) {
    xr->pos++;
  }
}

static int peek(xml_reader* xr, char c) {
  return xr->pos < xr->len && xr->src[xr->pos] == c;
}

static int consume(xml_reader* xr, char c) {
  if (!peek(xr, c)) {
    xr->err = XML_ERR_INVALID;
    return XML_ERR_INVALID;
  }
  xr->pos++;
  return XML_OK;
}

static int read_name(xml_reader* xr, xml_sv* out) {
  usize start = xr->pos;
  if (xr->pos >= xr->len || !is_name_start(xr->src[xr->pos])) {
    xr->err = XML_ERR_INVALID;
    return XML_ERR_INVALID;
  }
  xr->pos++;
  while (xr->pos < xr->len && is_name_char(xr->src[xr->pos])) {
    xr->pos++;
  }
  out->data = xr->src + start;
  out->len = xr->pos - start;
  return XML_OK;
}

static void split_qname(xml_sv qname, xml_sv* prefix, xml_sv* local) {
  usize i;
  for (i = 0; i < qname.len; i++) {
    if (qname.data[i] == ':') {
      prefix->data = qname.data;
      prefix->len = i;
      local->data = qname.data + i + 1;
      local->len = qname.len - i - 1;
      return;
    }
  }
  prefix->data = qname.data;
  prefix->len = 0;
  local->data = qname.data;
  local->len = qname.len;
}

static int decode_entity(xml_reader* xr, usize* i, char* out_ch) {
  usize p = *i;
  if (p >= xr->len || xr->src[p] != '&') {
    return XML_ERR_INVALID;
  }
  p++;
  if (p < xr->len && xr->src[p] == '#') {
    u32 v = 0;
    int hex = 0;
    p++;
    if (p < xr->len && (xr->src[p] == 'x' || xr->src[p] == 'X')) {
      hex = 1;
      p++;
    }
    while (p < xr->len && xr->src[p] != ';') {
      char c = xr->src[p];
      if (hex) {
        if (c >= '0' && c <= '9') {
          v = (v * 16U) + (u32)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
          v = (v * 16U) + (u32)(10 + c - 'a');
        } else if (c >= 'A' && c <= 'F') {
          v = (v * 16U) + (u32)(10 + c - 'A');
        } else {
          return XML_ERR_INVALID;
        }
      } else {
        if (c >= '0' && c <= '9') {
          v = (v * 10U) + (u32)(c - '0');
        } else {
          return XML_ERR_INVALID;
        }
      }
      p++;
    }
    if (p >= xr->len || xr->src[p] != ';' || v > 127U) {
      return XML_ERR_INVALID;
    }
    *out_ch = (char)v;
    *i = p + 1;
    return XML_OK;
  }

  if (p + 3 < xr->len && xr->src[p] == 'a' && xr->src[p + 1] == 'm' &&
      xr->src[p + 2] == 'p' && xr->src[p + 3] == ';') {
    *out_ch = '&';
    *i = p + 4;
    return XML_OK;
  }
  if (p + 2 < xr->len && xr->src[p] == 'l' && xr->src[p + 1] == 't' &&
      xr->src[p + 2] == ';') {
    *out_ch = '<';
    *i = p + 3;
    return XML_OK;
  }
  if (p + 2 < xr->len && xr->src[p] == 'g' && xr->src[p + 1] == 't' &&
      xr->src[p + 2] == ';') {
    *out_ch = '>';
    *i = p + 3;
    return XML_OK;
  }
  if (p + 4 < xr->len && xr->src[p] == 'q' && xr->src[p + 1] == 'u' &&
      xr->src[p + 2] == 'o' && xr->src[p + 3] == 't' && xr->src[p + 4] == ';') {
    *out_ch = '"';
    *i = p + 5;
    return XML_OK;
  }
  if (p + 4 < xr->len && xr->src[p] == 'a' && xr->src[p + 1] == 'p' &&
      xr->src[p + 2] == 'o' && xr->src[p + 3] == 's' && xr->src[p + 4] == ';') {
    *out_ch = '\'';
    *i = p + 5;
    return XML_OK;
  }
  return XML_ERR_INVALID;
}

static int decode_slice(xml_reader* xr, usize start, usize end, xml_sv* out) {
  usize i = start;
  usize begin = xr->decode_len;
  while (i < end) {
    char c = xr->src[i];
    if (c == '&') {
      int rc = decode_entity(xr, &i, &c);
      if (rc != XML_OK) {
        xr->err = XML_ERR_INVALID;
        return XML_ERR_INVALID;
      }
    } else {
      i++;
    }
    if (xr->decode_len >= XML_MAX_TOKEN_TEXT) {
      xr->err = XML_ERR_LIMIT;
      return XML_ERR_LIMIT;
    }
    xr->decode_buf[xr->decode_len++] = c;
  }
  out->data = xr->decode_buf + begin;
  out->len = xr->decode_len - begin;
  return XML_OK;
}

static int ns_store_copy(xml_reader* xr, xml_sv src, xml_sv* out) {
  usize i;
  if (xr->ns_uri_storage_len + src.len > XML_MAX_TOKEN_TEXT) {
    xr->err = XML_ERR_LIMIT;
    return XML_ERR_LIMIT;
  }
  out->data = xr->ns_uri_storage + xr->ns_uri_storage_len;
  out->len = src.len;
  for (i = 0; i < src.len; i++) {
    xr->ns_uri_storage[xr->ns_uri_storage_len + i] = src.data[i];
  }
  xr->ns_uri_storage_len += src.len;
  return XML_OK;
}

static xml_sv lookup_ns(xml_reader* xr, xml_sv prefix) {
  usize i = xr->ns_count;
  while (i > 0) {
    i--;
    if (sv_eq_sv(prefix, xr->ns_bindings[i].prefix)) {
      return xr->ns_bindings[i].uri;
    }
  }
  {
    xml_sv empty;
    empty.data = "";
    empty.len = 0;
    return empty;
  }
}

static void pop_ns_for_depth(xml_reader* xr, usize depth) {
  while (xr->ns_count > 0 && xr->ns_bindings[xr->ns_count - 1].depth == depth) {
    xr->ns_count--;
  }
}

void xml_reader_init(xml_reader* xr, const char* src, usize len) {
  xr->src = src;
  xr->len = len;
  xr->pos = 0;
  xr->err = XML_OK;
  xr->decode_len = 0;
  xr->ns_uri_storage_len = 0;
  xr->attr_count = 0;
  xr->ns_count = 0;
  xr->depth = 0;
  xr->pending_end = 0;
}

int xml_reader_error(const xml_reader* xr) {
  return xr->err;
}

static int parse_text(xml_reader* xr, xml_token* out) {
  usize start = xr->pos;
  xr->decode_len = 0;
  while (xr->pos < xr->len && xr->src[xr->pos] != '<') {
    xr->pos++;
  }
  if (decode_slice(xr, start, xr->pos, &out->text) != XML_OK) {
    return XML_ERR_INVALID;
  }
  out->kind = XML_TOK_TEXT;
  out->depth = xr->depth;
  out->attrs = (const xml_attr*)0;
  out->attr_count = 0;
  out->qname.data = "";
  out->qname.len = 0;
  out->prefix = out->qname;
  out->local = out->qname;
  out->ns_uri = out->qname;
  return XML_OK;
}

static int parse_start_or_empty(xml_reader* xr, xml_token* out) {
  xml_sv qname;
  xml_sv prefix;
  xml_sv local;
  int self_close = 0;
  usize new_depth;

  if (read_name(xr, &qname) != XML_OK) {
    return XML_ERR_INVALID;
  }
  split_qname(qname, &prefix, &local);

  xr->decode_len = 0;
  xr->attr_count = 0;
  for (;;) {
    skip_ws(xr);
    if (peek(xr, '/')) {
      xr->pos++;
      if (consume(xr, '>') != XML_OK) {
        return XML_ERR_INVALID;
      }
      self_close = 1;
      break;
    }
    if (peek(xr, '>')) {
      xr->pos++;
      break;
    }

    if (xr->attr_count >= XML_MAX_ATTRS) {
      xr->err = XML_ERR_LIMIT;
      return XML_ERR_LIMIT;
    }

    {
      xml_attr* a = &xr->attrs[xr->attr_count];
      usize vstart;
      usize vend;
      char quote;

      if (read_name(xr, &a->qname) != XML_OK) {
        return XML_ERR_INVALID;
      }
      split_qname(a->qname, &a->prefix, &a->local);

      skip_ws(xr);
      if (consume(xr, '=') != XML_OK) {
        return XML_ERR_INVALID;
      }
      skip_ws(xr);

      if (xr->pos >= xr->len || (xr->src[xr->pos] != '"' && xr->src[xr->pos] != '\'')) {
        xr->err = XML_ERR_INVALID;
        return XML_ERR_INVALID;
      }
      quote = xr->src[xr->pos++];
      vstart = xr->pos;
      while (xr->pos < xr->len && xr->src[xr->pos] != quote) {
        xr->pos++;
      }
      if (xr->pos >= xr->len) {
        xr->err = XML_ERR_INVALID;
        return XML_ERR_INVALID;
      }
      vend = xr->pos;
      xr->pos++;

      if (decode_slice(xr, vstart, vend, &a->value) != XML_OK) {
        return XML_ERR_INVALID;
      }

      a->ns_uri.data = "";
      a->ns_uri.len = 0;
      xr->attr_count++;
    }
  }

  new_depth = xr->depth + 1;
  if (new_depth > XML_MAX_DEPTH) {
    xr->err = XML_ERR_LIMIT;
    return XML_ERR_LIMIT;
  }

  /* Apply xmlns declarations at new depth. */
  {
    usize i;
    for (i = 0; i < xr->attr_count; i++) {
      xml_attr* a = &xr->attrs[i];
      int is_default_xmlns = sv_eq_cstr(a->qname, "xmlns");
      int is_prefixed_xmlns = (sv_eq_cstr(a->prefix, "xmlns") && a->local.len > 0);
      if (is_default_xmlns || is_prefixed_xmlns) {
        if (xr->ns_count >= XML_MAX_NS_BINDINGS) {
          xr->err = XML_ERR_LIMIT;
          return XML_ERR_LIMIT;
        }
        if (is_default_xmlns) {
          xr->ns_bindings[xr->ns_count].prefix.data = "";
          xr->ns_bindings[xr->ns_count].prefix.len = 0;
        } else {
          xr->ns_bindings[xr->ns_count].prefix = a->local;
        }
        if (ns_store_copy(xr, a->value, &xr->ns_bindings[xr->ns_count].uri) != XML_OK) {
          return XML_ERR_LIMIT;
        }
        xr->ns_bindings[xr->ns_count].depth = new_depth;
        xr->ns_count++;
      }
    }
  }

  out->kind = XML_TOK_START_ELEM;
  out->depth = new_depth;
  out->qname = qname;
  out->prefix = prefix;
  out->local = local;
  out->ns_uri = lookup_ns(xr, prefix);
  out->attrs = xr->attrs;
  out->attr_count = xr->attr_count;
  out->text.data = "";
  out->text.len = 0;

  {
    usize i;
    for (i = 0; i < xr->attr_count; i++) {
      xml_attr* a = &xr->attrs[i];
      if (sv_eq_cstr(a->qname, "xmlns") || sv_eq_cstr(a->prefix, "xmlns")) {
        a->ns_uri.data = "";
        a->ns_uri.len = 0;
      } else if (a->prefix.len == 0) {
        a->ns_uri.data = "";
        a->ns_uri.len = 0;
      } else {
        a->ns_uri = lookup_ns(xr, a->prefix);
      }
    }
  }

  xr->elem_qname_stack[new_depth - 1] = qname;
  xr->elem_prefix_stack[new_depth - 1] = prefix;
  xr->elem_local_stack[new_depth - 1] = local;
  xr->elem_ns_stack[new_depth - 1] = out->ns_uri;
  xr->depth = new_depth;

  if (self_close) {
    xr->pending_end = 1;
  }

  return XML_OK;
}

static int parse_end(xml_reader* xr, xml_token* out) {
  xml_sv qname;
  xml_sv prefix;
  xml_sv local;

  if (read_name(xr, &qname) != XML_OK) {
    return XML_ERR_INVALID;
  }
  split_qname(qname, &prefix, &local);
  skip_ws(xr);
  if (consume(xr, '>') != XML_OK) {
    return XML_ERR_INVALID;
  }

  if (xr->depth == 0 || !sv_eq_sv(qname, xr->elem_qname_stack[xr->depth - 1])) {
    xr->err = XML_ERR_INVALID;
    return XML_ERR_INVALID;
  }

  out->kind = XML_TOK_END_ELEM;
  out->depth = xr->depth;
  out->qname = xr->elem_qname_stack[xr->depth - 1];
  out->prefix = xr->elem_prefix_stack[xr->depth - 1];
  out->local = xr->elem_local_stack[xr->depth - 1];
  out->ns_uri = xr->elem_ns_stack[xr->depth - 1];
  out->attrs = (const xml_attr*)0;
  out->attr_count = 0;
  out->text.data = "";
  out->text.len = 0;

  pop_ns_for_depth(xr, xr->depth);
  xr->depth--;
  return XML_OK;
}

static int skip_declaration_like(xml_reader* xr, const char* end_pat, usize end_len) {
  while (xr->pos + end_len <= xr->len) {
    usize i;
    int match = 1;
    for (i = 0; i < end_len; i++) {
      if (xr->src[xr->pos + i] != end_pat[i]) {
        match = 0;
        break;
      }
    }
    if (match) {
      xr->pos += end_len;
      return XML_OK;
    }
    xr->pos++;
  }
  xr->err = XML_ERR_INVALID;
  return XML_ERR_INVALID;
}

int xml_reader_next(xml_reader* xr, xml_token* out) {
  if (xr->err != XML_OK) {
    return xr->err;
  }

  if (xr->pending_end) {
    out->kind = XML_TOK_END_ELEM;
    out->depth = xr->depth;
    out->qname = xr->elem_qname_stack[xr->depth - 1];
    out->prefix = xr->elem_prefix_stack[xr->depth - 1];
    out->local = xr->elem_local_stack[xr->depth - 1];
    out->ns_uri = xr->elem_ns_stack[xr->depth - 1];
    out->attrs = (const xml_attr*)0;
    out->attr_count = 0;
    out->text.data = "";
    out->text.len = 0;

    pop_ns_for_depth(xr, xr->depth);
    xr->depth--;
    xr->pending_end = 0;
    return XML_OK;
  }

  if (xr->pos >= xr->len) {
    out->kind = XML_TOK_EOF;
    out->depth = xr->depth;
    out->attrs = (const xml_attr*)0;
    out->attr_count = 0;
    out->text.data = "";
    out->text.len = 0;
    out->qname = out->text;
    out->prefix = out->text;
    out->local = out->text;
    out->ns_uri = out->text;
    return XML_OK;
  }

  if (!peek(xr, '<')) {
    return parse_text(xr, out);
  }

  xr->pos++;
  if (peek(xr, '/')) {
    xr->pos++;
    return parse_end(xr, out);
  }
  if (peek(xr, '?')) {
    xr->pos++;
    if (skip_declaration_like(xr, "?>", 2) != XML_OK) {
      return XML_ERR_INVALID;
    }
    return xml_reader_next(xr, out);
  }
  if (peek(xr, '!')) {
    xr->pos++;
    if (xr->pos + 2 <= xr->len && xr->src[xr->pos] == '-' && xr->src[xr->pos + 1] == '-') {
      xr->pos += 2;
      if (skip_declaration_like(xr, "-->", 3) != XML_OK) {
        return XML_ERR_INVALID;
      }
      return xml_reader_next(xr, out);
    }
    if (skip_declaration_like(xr, ">", 1) != XML_OK) {
      return XML_ERR_INVALID;
    }
    return xml_reader_next(xr, out);
  }

  return parse_start_or_empty(xr, out);
}

static int writer_put(xml_writer* xw, const char* s, usize n) {
  usize i;
  if (xw->pos + n > xw->cap) {
    return XML_ERR_DST_TOO_SMALL;
  }
  for (i = 0; i < n; i++) {
    xw->dst[xw->pos + i] = (u8)s[i];
  }
  xw->pos += n;
  return XML_OK;
}

static int writer_put_c(xml_writer* xw, char c) {
  if (xw->pos + 1 > xw->cap) {
    return XML_ERR_DST_TOO_SMALL;
  }
  xw->dst[xw->pos++] = (u8)c;
  return XML_OK;
}

static int writer_escape(xml_writer* xw, const char* s, usize n, int in_attr) {
  usize i;
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (c == '&') {
      if (writer_put(xw, "&amp;", 5) != XML_OK) {
        return XML_ERR_DST_TOO_SMALL;
      }
    } else if (c == '<') {
      if (writer_put(xw, "&lt;", 4) != XML_OK) {
        return XML_ERR_DST_TOO_SMALL;
      }
    } else if (c == '>') {
      if (writer_put(xw, "&gt;", 4) != XML_OK) {
        return XML_ERR_DST_TOO_SMALL;
      }
    } else if (in_attr && c == '"') {
      if (writer_put(xw, "&quot;", 6) != XML_OK) {
        return XML_ERR_DST_TOO_SMALL;
      }
    } else if (in_attr && c == '\'') {
      if (writer_put(xw, "&apos;", 6) != XML_OK) {
        return XML_ERR_DST_TOO_SMALL;
      }
    } else {
      if (writer_put_c(xw, c) != XML_OK) {
        return XML_ERR_DST_TOO_SMALL;
      }
    }
  }
  return XML_OK;
}

void xml_writer_init(xml_writer* xw, u8* dst, usize cap) {
  xw->dst = dst;
  xw->cap = cap;
  xw->pos = 0;
  xw->tag_open = 0;
}

int xml_writer_decl(xml_writer* xw) {
  return writer_put(xw, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>", 38);
}

int xml_writer_start_elem(xml_writer* xw, const char* qname) {
  int rc;
  if (xw->tag_open) {
    rc = writer_put_c(xw, '>');
    if (rc != XML_OK) {
      return rc;
    }
  }
  rc = writer_put_c(xw, '<');
  if (rc != XML_OK) {
    return rc;
  }
  rc = writer_put(xw, qname, rt_strlen(qname));
  if (rc != XML_OK) {
    return rc;
  }
  xw->tag_open = 1;
  return XML_OK;
}

int xml_writer_attr(xml_writer* xw, const char* qname, const char* value) {
  int rc;
  if (!xw->tag_open) {
    return XML_ERR_INVALID;
  }
  rc = writer_put_c(xw, ' ');
  if (rc != XML_OK) {
    return rc;
  }
  rc = writer_put(xw, qname, rt_strlen(qname));
  if (rc != XML_OK) {
    return rc;
  }
  rc = writer_put(xw, "=\"", 2);
  if (rc != XML_OK) {
    return rc;
  }
  rc = writer_escape(xw, value, rt_strlen(value), 1);
  if (rc != XML_OK) {
    return rc;
  }
  return writer_put_c(xw, '"');
}

int xml_writer_text(xml_writer* xw, const char* text, usize len) {
  int rc;
  if (xw->tag_open) {
    rc = writer_put_c(xw, '>');
    if (rc != XML_OK) {
      return rc;
    }
    xw->tag_open = 0;
  }
  return writer_escape(xw, text, len, 0);
}

int xml_writer_end_elem(xml_writer* xw, const char* qname) {
  int rc;
  if (xw->tag_open) {
    rc = writer_put(xw, "/>", 2);
    if (rc != XML_OK) {
      return rc;
    }
    xw->tag_open = 0;
    return XML_OK;
  }
  rc = writer_put(xw, "</", 2);
  if (rc != XML_OK) {
    return rc;
  }
  rc = writer_put(xw, qname, rt_strlen(qname));
  if (rc != XML_OK) {
    return rc;
  }
  return writer_put_c(xw, '>');
}

int xml_writer_finish(xml_writer* xw, usize* out_len) {
  if (xw->tag_open) {
    if (writer_put_c(xw, '>') != XML_OK) {
      return XML_ERR_DST_TOO_SMALL;
    }
    xw->tag_open = 0;
  }
  *out_len = xw->pos;
  return XML_OK;
}
