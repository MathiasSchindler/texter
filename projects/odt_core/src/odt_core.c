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

static int append_bytes(char* dst, usize cap, usize* pos, const char* src, usize n) {
  usize i;
  if (*pos + n > cap) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  for (i = 0; i < n; i++) {
    dst[*pos + i] = src[i];
  }
  *pos += n;
  return ODT_OK;
}

static int append_c(char* dst, usize cap, usize* pos, char c) {
  if (*pos + 1 > cap) {
    return ODT_ERR_DST_TOO_SMALL;
  }
  dst[*pos] = c;
  *pos += 1;
  return ODT_OK;
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

int odt_core_validate_package(const u8* odt_data, usize odt_len) {
  zip_archive za;
  zip_entry_view first;
  zip_entry_view mt;
  zip_entry_view manifest;
  static u8 temp[131072];
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
  if (zip_entry_extract(&mt, temp, sizeof(temp), &out_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }
  if (out_len != rt_strlen(ODT_MIMETYPE) || !cstr_eq_n(ODT_MIMETYPE, (const char*)temp, out_len)) {
    return ODT_ERR_INVALID;
  }

  if (zip_archive_find_entry(&za, "content.xml", &mt) != ZIP_OK) {
    return ODT_ERR_NOT_FOUND;
  }

  if (zip_archive_find_entry(&za, "META-INF/manifest.xml", &manifest) != ZIP_OK) {
    return ODT_ERR_NOT_FOUND;
  }
  if (zip_entry_extract(&manifest, temp, sizeof(temp), &out_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }

  return validate_manifest((const char*)temp, out_len);
}

int odt_core_extract_plain_text(const u8* odt_data,
                                usize odt_len,
                                char* dst,
                                usize dst_cap,
                                usize* out_len) {
  zip_archive za;
  zip_entry_view content;
  static u8 xml_data[262144];
  usize xml_len;
  xml_reader xr;
  xml_token tok;
  usize out_pos = 0;
  usize p_depth = 0;

  if (zip_archive_open(&za, odt_data, odt_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }
  if (zip_archive_find_entry(&za, "content.xml", &content) != ZIP_OK) {
    return ODT_ERR_NOT_FOUND;
  }
  if (zip_entry_extract(&content, xml_data, sizeof(xml_data), &xml_len) != ZIP_OK) {
    return ODT_ERR_INVALID;
  }

  xml_reader_init(&xr, (const char*)xml_data, xml_len);
  for (;;) {
    if (xml_reader_next(&xr, &tok) != XML_OK) {
      return ODT_ERR_INVALID;
    }
    if (tok.kind == XML_TOK_EOF) {
      break;
    }

    if (tok.kind == XML_TOK_START_ELEM && sv_eq_cstr(tok.qname, "text:p")) {
      p_depth++;
    } else if (tok.kind == XML_TOK_END_ELEM && sv_eq_cstr(tok.qname, "text:p")) {
      if (append_c(dst, dst_cap, &out_pos, '\n') != ODT_OK) {
        return ODT_ERR_DST_TOO_SMALL;
      }
      if (p_depth > 0) {
        p_depth--;
      }
    } else if (tok.kind == XML_TOK_START_ELEM && p_depth > 0 && sv_eq_cstr(tok.qname, "text:s")) {
      if (append_c(dst, dst_cap, &out_pos, ' ') != ODT_OK) {
        return ODT_ERR_DST_TOO_SMALL;
      }
    } else if (tok.kind == XML_TOK_START_ELEM && p_depth > 0 &&
               sv_eq_cstr(tok.qname, "text:line-break")) {
      if (append_c(dst, dst_cap, &out_pos, '\n') != ODT_OK) {
        return ODT_ERR_DST_TOO_SMALL;
      }
    } else if (tok.kind == XML_TOK_TEXT && p_depth > 0 && tok.text.len > 0) {
      if (append_bytes(dst, dst_cap, &out_pos, tok.text.data, tok.text.len) != ODT_OK) {
        return ODT_ERR_DST_TOO_SMALL;
      }
    }
  }

  if (xml_reader_error(&xr) != XML_OK) {
    return ODT_ERR_INVALID;
  }

  *out_len = out_pos;
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
  zip_writer zw;

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
