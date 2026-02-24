#ifndef XML_XML_H
#define XML_XML_H

#include "rt/types.h"

#define XML_OK 0
#define XML_ERR_INVALID (-1)
#define XML_ERR_LIMIT (-2)
#define XML_ERR_DST_TOO_SMALL (-3)

#define XML_MAX_ATTRS 64
#define XML_MAX_DEPTH 128
#define XML_MAX_NS_BINDINGS 512
#define XML_MAX_TOKEN_TEXT 65536

typedef struct xml_sv {
  const char* data;
  usize len;
} xml_sv;

typedef enum xml_token_kind {
  XML_TOK_EOF = 0,
  XML_TOK_START_ELEM = 1,
  XML_TOK_END_ELEM = 2,
  XML_TOK_TEXT = 3
} xml_token_kind;

typedef struct xml_attr {
  xml_sv qname;
  xml_sv prefix;
  xml_sv local;
  xml_sv ns_uri;
  xml_sv value;
} xml_attr;

typedef struct xml_token {
  xml_token_kind kind;
  usize depth;
  xml_sv qname;
  xml_sv prefix;
  xml_sv local;
  xml_sv ns_uri;
  const xml_attr* attrs;
  usize attr_count;
  xml_sv text;
} xml_token;

typedef struct xml_reader xml_reader;
struct xml_reader {
  const char* src;
  usize len;
  usize pos;
  int err;

  char decode_buf[XML_MAX_TOKEN_TEXT];
  usize decode_len;
  char ns_uri_storage[XML_MAX_TOKEN_TEXT];
  usize ns_uri_storage_len;

  xml_attr attrs[XML_MAX_ATTRS];
  usize attr_count;

  struct {
    xml_sv prefix;
    xml_sv uri;
    usize depth;
  } ns_bindings[XML_MAX_NS_BINDINGS];
  usize ns_count;

  xml_sv elem_qname_stack[XML_MAX_DEPTH];
  xml_sv elem_prefix_stack[XML_MAX_DEPTH];
  xml_sv elem_local_stack[XML_MAX_DEPTH];
  xml_sv elem_ns_stack[XML_MAX_DEPTH];
  usize depth;

  int pending_end;
};

void xml_reader_init(xml_reader* xr, const char* src, usize len);
int xml_reader_next(xml_reader* xr, xml_token* out);
int xml_reader_error(const xml_reader* xr);

typedef struct xml_writer {
  u8* dst;
  usize cap;
  usize pos;
  int tag_open;
} xml_writer;

void xml_writer_init(xml_writer* xw, u8* dst, usize cap);
int xml_writer_decl(xml_writer* xw);
int xml_writer_start_elem(xml_writer* xw, const char* qname);
int xml_writer_attr(xml_writer* xw, const char* qname, const char* value);
int xml_writer_text(xml_writer* xw, const char* text, usize len);
int xml_writer_end_elem(xml_writer* xw, const char* qname);
int xml_writer_finish(xml_writer* xw, usize* out_len);

#endif
