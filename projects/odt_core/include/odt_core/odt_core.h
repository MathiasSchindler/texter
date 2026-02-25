#ifndef ODT_CORE_ODT_CORE_H
#define ODT_CORE_ODT_CORE_H

#include "rt/types.h"

#define ODT_OK 0
#define ODT_ERR_INVALID (-1)
#define ODT_ERR_NOT_FOUND (-2)
#define ODT_ERR_DST_TOO_SMALL (-3)
#define ODT_ERR_TOO_LARGE (-4)

#define ODT_CORE_MAX_CONTENT_XML_BYTES (16U * 1024U * 1024U)
#define ODT_CORE_MAX_MANIFEST_XML_BYTES (4U * 1024U * 1024U)

#define ODT_MIMETYPE "application/vnd.oasis.opendocument.text"

extern const char ODT_META_DOC_MINIMAL[];
extern const char ODT_MANIFEST_DOC_MINIMAL[];

typedef struct odt_text_view {
  const char* data;
  usize len;
} odt_text_view;

int odt_core_validate_package(const u8* odt_data, usize odt_len);
int odt_core_extract_plain_text(const u8* odt_data,
                                usize odt_len,
                                char* dst,
                                usize dst_cap,
                                usize* out_len);
int odt_core_build_minimal(const char* plain_text,
                           usize plain_text_len,
                           u8* dst,
                           usize dst_cap,
                           usize* out_len);

#endif
