#include "odt_core/odt_core.h"

#include "rt/rt.h"
#include "xml/xml.h"
#include "zip/zip.h"

const char ODT_META_DOC_MINIMAL[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
  " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
  " office:version=\"1.4\"><office:meta/></office:document-meta>";

const char ODT_MANIFEST_DOC_MINIMAL[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.4\">"
  "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
  "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
  "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
  "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>"
  "</manifest:manifest>";

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

static int cstr_eq_n(const char* a, const char* b, usize n) {
  usize i;
  for (i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return a[n] == '\0';
}

static int token_attr_value(const xml_token* tok, const char* qname, xml_sv* out_value) {
  usize i;
  for (i = 0; i < tok->attr_count; i++) {
    if (sv_eq_cstr(tok->attrs[i].qname, qname)) {
      *out_value = tok->attrs[i].value;
      return 1;
    }
  }
  return 0;
}

static int validate_manifest(const char* manifest, usize manifest_len) {
  xml_reader xr;
  xml_token tok;
  int saw_root_entry = 0;
  int saw_content_entry = 0;

  xml_reader_init(&xr, manifest, manifest_len);
  for (;;) {
    if (xml_reader_next(&xr, &tok) != XML_OK) {
      return ODT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      break;
    }
    if (tok.kind == XML_TOK_START_ELEM && sv_eq_cstr(tok.qname, "manifest:file-entry")) {
      xml_sv full_path;
      xml_sv media_type;
      int has_path = token_attr_value(&tok, "manifest:full-path", &full_path);
      int has_type = token_attr_value(&tok, "manifest:media-type", &media_type);
      if (has_path && has_type && sv_eq_cstr(full_path, "/") &&
          sv_eq_cstr(media_type, ODT_MIMETYPE)) {
        saw_root_entry = 1;
      }
      if (has_path && has_type && sv_eq_cstr(full_path, "content.xml") &&
          sv_eq_cstr(media_type, "text/xml")) {
        saw_content_entry = 1;
      }
    }
  }

  if (xml_reader_error(&xr) != XML_OK) {
    return ODT_ERR_INVALID;
  }
  if (!saw_root_entry || !saw_content_entry) {
    return ODT_ERR_INVALID;
  }
  return ODT_OK;
}

typedef struct odt_text_stream_parser {
  char* dst;
  usize dst_cap;
  usize out_pos;
  usize p_depth;
  int in_tag;
  int tag_overflow;
  int in_entity;
  char tag_buf[512];
  usize tag_len;
  char ent_buf[16];
  usize ent_len;
} odt_text_stream_parser;

static int parser_emit(odt_text_stream_parser* p, char c) {
  if (p->out_pos >= p->dst_cap) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  p->dst[p->out_pos++] = c;
  return ODT_OK;
}

static int parser_emit_spaces(odt_text_stream_parser* p, usize n) {
  usize i;
  for (i = 0; i < n; i++) {
    int rc = parser_emit(p, ' ');
    if (rc != ODT_OK) {
      return rc;
    }
  }
  return ODT_OK;
}

static int parser_qname_eq(const char* name, usize len, const char* lit) {
  usize i = 0;
  while (lit[i] != '\0') {
    if (i >= len || name[i] != lit[i]) {
      return 0;
    }
    i++;
  }
  return i == len;
}

static int parser_decode_entity(odt_text_stream_parser* p, char* out) {
  if (p->ent_len == 3 && p->ent_buf[0] == 'a' && p->ent_buf[1] == 'm' && p->ent_buf[2] == 'p') {
    *out = '&';
    return ODT_OK;
  }
  if (p->ent_len == 2 && p->ent_buf[0] == 'l' && p->ent_buf[1] == 't') {
    *out = '<';
    return ODT_OK;
  }
  if (p->ent_len == 2 && p->ent_buf[0] == 'g' && p->ent_buf[1] == 't') {
    *out = '>';
    return ODT_OK;
  }
  if (p->ent_len == 4 && p->ent_buf[0] == 'q' && p->ent_buf[1] == 'u' && p->ent_buf[2] == 'o' &&
      p->ent_buf[3] == 't') {
    *out = '"';
    return ODT_OK;
  }
  if (p->ent_len == 4 && p->ent_buf[0] == 'a' && p->ent_buf[1] == 'p' && p->ent_buf[2] == 'o' &&
      p->ent_buf[3] == 's') {
    *out = '\'';
    return ODT_OK;
  }
  return ODT_ERR_INVALID;
}

static int parser_handle_tag(odt_text_stream_parser* p) {
  usize i = 0;
  usize start;
  usize end;
  int is_end = 0;
  int self_close = 0;

  if (p->tag_overflow) {
    return ODT_OK;
  }

  while (i < p->tag_len && (p->tag_buf[i] == ' ' || p->tag_buf[i] == '\t' || p->tag_buf[i] == '\r' ||
                            p->tag_buf[i] == '\n')) {
    i++;
  }
  if (i >= p->tag_len) {
    return ODT_OK;
  }

  if (p->tag_buf[i] == '?' || p->tag_buf[i] == '!') {
    return ODT_OK;
  }

  if (p->tag_buf[i] == '/') {
    is_end = 1;
    i++;
  }

  start = i;
  while (i < p->tag_len && p->tag_buf[i] != ' ' && p->tag_buf[i] != '\t' && p->tag_buf[i] != '\r' &&
         p->tag_buf[i] != '\n' && p->tag_buf[i] != '/') {
    i++;
  }
  end = i;
  while (i < p->tag_len) {
    if (p->tag_buf[i] == '/') {
      self_close = 1;
    }
    i++;
  }

  if (is_end) {
    if (parser_qname_eq(p->tag_buf + start, end - start, "text:p")) {
      if (p->p_depth > 0) {
        p->p_depth--;
      }
      return parser_emit(p, '\n');
    }
    return ODT_OK;
  }

  if (parser_qname_eq(p->tag_buf + start, end - start, "text:p")) {
    p->p_depth++;
    if (self_close) {
      if (p->p_depth > 0) {
        p->p_depth--;
      }
      return parser_emit(p, '\n');
    }
    return ODT_OK;
  }

  if (p->p_depth > 0 && parser_qname_eq(p->tag_buf + start, end - start, "text:line-break")) {
    return parser_emit(p, '\n');
  }

  if (p->p_depth > 0 && parser_qname_eq(p->tag_buf + start, end - start, "text:s")) {
    usize c = 1;
    usize j = end;
    while (j + 8 < p->tag_len) {
      if (p->tag_buf[j] == 't' && p->tag_buf[j + 1] == 'e' && p->tag_buf[j + 2] == 'x' &&
          p->tag_buf[j + 3] == 't' && p->tag_buf[j + 4] == ':' && p->tag_buf[j + 5] == 'c' &&
          p->tag_buf[j + 6] == '=') {
        char quote;
        usize k;
        usize v = 0;
        j += 7;
        if (j >= p->tag_len) {
          break;
        }
        quote = p->tag_buf[j];
        if (quote != '"' && quote != '\'') {
          break;
        }
        j++;
        k = j;
        while (k < p->tag_len && p->tag_buf[k] != quote) {
          if (p->tag_buf[k] >= '0' && p->tag_buf[k] <= '9') {
            v = (v * 10U) + (usize)(p->tag_buf[k] - '0');
          } else {
            v = 0;
            break;
          }
          k++;
        }
        if (k < p->tag_len && v > 0) {
          c = v;
        }
        break;
      }
      j++;
    }
    return parser_emit_spaces(p, c);
  }

  return ODT_OK;
}

static int odt_text_stream_sink(void* user, const u8* data, usize len) {
  odt_text_stream_parser* p = (odt_text_stream_parser*)user;
  usize i;
  for (i = 0; i < len; i++) {
    char c = (char)data[i];

    if (p->in_entity) {
      if (c == ';') {
        char dc;
        int rc = parser_decode_entity(p, &dc);
        p->in_entity = 0;
        p->ent_len = 0;
        if (rc != ODT_OK) {
          return rc;
        }
        if (p->p_depth > 0) {
          rc = parser_emit(p, dc);
          if (rc != ODT_OK) {
            return rc;
          }
        }
      } else {
        if (p->ent_len + 1 >= sizeof(p->ent_buf)) {
          return ODT_ERR_INVALID;
        }
        p->ent_buf[p->ent_len++] = c;
      }
      continue;
    }

    if (p->in_tag) {
      if (c == '>') {
        int rc;
        p->in_tag = 0;
        rc = parser_handle_tag(p);
        p->tag_len = 0;
        p->tag_overflow = 0;
        if (rc != ODT_OK) {
          return rc;
        }
      } else {
        if (!p->tag_overflow) {
          if (p->tag_len + 1 >= sizeof(p->tag_buf)) {
            p->tag_overflow = 1;
          } else {
            p->tag_buf[p->tag_len++] = c;
          }
        }
      }
      continue;
    }

    if (c == '<') {
      p->in_tag = 1;
      p->tag_len = 0;
      p->tag_overflow = 0;
      continue;
    }
    if (c == '&') {
      p->in_entity = 1;
      p->ent_len = 0;
      continue;
    }

    if (p->p_depth > 0) {
      int rc = parser_emit(p, c);
      if (rc != ODT_OK) {
        return rc;
      }
    }
  }
  return ODT_OK;
}

int odt_core_validate_package(const u8* odt_data, usize odt_len) {
  zip_archive za;
  zip_entry_view first;
  zip_entry_view mt;
  zip_entry_view manifest;
  static u8 mimetype_buf[64];
  static u8 manifest_buf[ODT_CORE_MAX_MANIFEST_XML_BYTES];
  usize out_len;

  if (zip_archive_open(&za, odt_data, odt_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }

  if (zip_archive_get_entry(&za, 0, &first) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }
  if (!cstr_eq_n("mimetype", first.name, first.name_len) || first.method != ZIP_METHOD_STORE) {
    return ODT_ERR_INVALID;
  }

  if (zip_archive_find_entry(&za, "mimetype", &mt) != ZIP_OK) {
    return ODT_ERR_NOT_FOUND;
  }
  if (mt.uncomp_size > sizeof(mimetype_buf)) {
    return ODT_ERR_TOO_LARGE;
  }
  if (zip_entry_extract(&mt, mimetype_buf, sizeof(mimetype_buf), &out_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }
  if (out_len != rt_strlen(ODT_MIMETYPE) ||
      !cstr_eq_n(ODT_MIMETYPE, (const char*)mimetype_buf, out_len)) {
    return ODT_ERR_INVALID;
  }

  if (zip_archive_find_entry(&za, "content.xml", &mt) != ZIP_OK) {
    return ODT_ERR_NOT_FOUND;
  }

  if (zip_archive_find_entry(&za, "META-INF/manifest.xml", &manifest) != ZIP_OK) {
    return ODT_ERR_NOT_FOUND;
  }
  if ((usize)manifest.uncomp_size > sizeof(manifest_buf)) {
    return ODT_ERR_TOO_LARGE;
  }
  if (zip_entry_extract(&manifest, manifest_buf, sizeof(manifest_buf), &out_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }

  return validate_manifest((const char*)manifest_buf, out_len);
}

int odt_core_extract_plain_text(const u8* odt_data,
                                usize odt_len,
                                char* dst,
                                usize dst_cap,
                                usize* out_len) {
  zip_archive za;
  zip_entry_view content;
  usize xml_len = 0;
  odt_text_stream_parser parser;

  if (zip_archive_open(&za, odt_data, odt_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }
  if (zip_archive_find_entry(&za, "content.xml", &content) != ZIP_OK) {
    return ODT_ERR_NOT_FOUND;
  }
  if ((usize)content.uncomp_size > ODT_CORE_MAX_CONTENT_XML_BYTES) {
    return ODT_ERR_TOO_LARGE;
  }
  parser.dst = dst;
  parser.dst_cap = dst_cap;
  parser.out_pos = 0;
  parser.p_depth = 0;
  parser.in_tag = 0;
  parser.tag_overflow = 0;
  parser.in_entity = 0;
  parser.tag_len = 0;
  parser.ent_len = 0;

  if (zip_entry_extract_stream(&content, odt_text_stream_sink, &parser, &xml_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }
  if (parser.in_tag || parser.in_entity) {
    return ODT_ERR_INVALID;
  }

  *out_len = parser.out_pos;
  return ODT_OK;
}

static int build_content_xml(const char* plain_text,
                             usize plain_text_len,
                             u8* dst,
                             usize cap,
                             usize* out_len) {
  xml_writer xw;
  usize i;
  usize line_start = 0;
  int wrote_any_para = 0;

  xml_writer_init(&xw, dst, cap);
  if (xml_writer_decl(&xw) != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (xml_writer_start_elem(&xw, "office:document-content") != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (xml_writer_attr(&xw, "xmlns:office",
                      "urn:oasis:names:tc:opendocument:xmlns:office:1.0") != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (xml_writer_attr(&xw, "xmlns:text",
                      "urn:oasis:names:tc:opendocument:xmlns:text:1.0") != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (xml_writer_attr(&xw, "office:version", "1.4") != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (xml_writer_start_elem(&xw, "office:body") != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (xml_writer_start_elem(&xw, "office:text") != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }

  for (i = 0; i <= plain_text_len; i++) {
    if (i == plain_text_len || plain_text[i] == '\n') {
      if (xml_writer_start_elem(&xw, "text:p") != XML_OK) {
        return ODT_ERR_DST_TOO_SMALL;
      }
      if (i > line_start) {
        if (xml_writer_text(&xw, plain_text + line_start, i - line_start) != XML_OK) {
          return ODT_ERR_DST_TOO_SMALL;
        }
      }
      if (xml_writer_end_elem(&xw, "text:p") != XML_OK) {
        return ODT_ERR_DST_TOO_SMALL;
      }
      wrote_any_para = 1;
      line_start = i + 1;
    }
  }

  if (!wrote_any_para) {
    if (xml_writer_start_elem(&xw, "text:p") != XML_OK ||
        xml_writer_end_elem(&xw, "text:p") != XML_OK) {
      return ODT_ERR_DST_TOO_SMALL;
    }
  }

  if (xml_writer_end_elem(&xw, "office:text") != XML_OK ||
      xml_writer_end_elem(&xw, "office:body") != XML_OK ||
      xml_writer_end_elem(&xw, "office:document-content") != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (xml_writer_finish(&xw, out_len) != XML_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }

  return ODT_OK;
}

int odt_core_build_minimal(const char* plain_text,
                           usize plain_text_len,
                           u8* dst,
                           usize dst_cap,
                           usize* out_len) {
  static u8 content_xml[65536];
  static u8 styles_xml[2048];
  static u8 meta_xml[2048];
  static u8 manifest_xml[4096];
  usize content_len = 0;
  usize styles_len = 0;
  usize meta_len = 0;
  usize manifest_len = 0;
  static zip_writer zw;

  static const char styles_doc[] =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
      " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
      " office:version=\"1.4\"><office:styles/></office:document-styles>";


  if (build_content_xml(plain_text, plain_text_len, content_xml, sizeof(content_xml),
                        &content_len) != ODT_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }

  styles_len = rt_strlen(styles_doc);
  if (styles_len > sizeof(styles_xml)) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  {
    usize i;
    for (i = 0; i < styles_len; i++) {
      styles_xml[i] = (u8)styles_doc[i];
    }
  }

  meta_len = rt_strlen(ODT_META_DOC_MINIMAL);
  if (meta_len > sizeof(meta_xml)) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  {
    usize i;
    for (i = 0; i < meta_len; i++) {
      meta_xml[i] = (u8)ODT_META_DOC_MINIMAL[i];
    }
  }

  manifest_len = rt_strlen(ODT_MANIFEST_DOC_MINIMAL);
  if (manifest_len > sizeof(manifest_xml)) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  {
    usize i;
    for (i = 0; i < manifest_len; i++) {
      manifest_xml[i] = (u8)ODT_MANIFEST_DOC_MINIMAL[i];
    }
  }

  zip_writer_init(&zw, dst, dst_cap);

  if (zip_writer_add_entry(&zw, "mimetype", (const u8*)ODT_MIMETYPE,
                           rt_strlen(ODT_MIMETYPE), ZIP_METHOD_STORE) != ZIP_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (zip_writer_add_entry(&zw, "content.xml", content_xml, content_len,
                           ZIP_METHOD_DEFLATE) != ZIP_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (zip_writer_add_entry(&zw, "styles.xml", styles_xml, styles_len,
                           ZIP_METHOD_DEFLATE) != ZIP_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (zip_writer_add_entry(&zw, "meta.xml", meta_xml, meta_len,
                           ZIP_METHOD_DEFLATE) != ZIP_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  if (zip_writer_add_entry(&zw, "META-INF/manifest.xml", manifest_xml, manifest_len,
                           ZIP_METHOD_DEFLATE) != ZIP_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }

  if (zip_writer_finish(&zw, out_len) != ZIP_OK) {
    return ODT_ERR_DST_TOO_SMALL;
  }

  return ODT_OK;
}
