#ifndef UNICODE_UNICODE_H
#define UNICODE_UNICODE_H

#include "rt/types.h"

#define UNICODE_OK 0
#define UNICODE_ERR_INVALID (-1)
#define UNICODE_ERR_DST_TOO_SMALL (-2)

int unicode_utf8_validate(const u8* src, usize len);
int unicode_utf8_decode_one(const u8* src, usize len, u32* out_cp, usize* out_consumed);
int unicode_utf8_encode_one(u32 cp, u8* dst, usize dst_cap, usize* out_len);

int unicode_is_xml_whitespace(u32 cp);
int unicode_is_xml_name_start(u32 cp);
int unicode_is_xml_name_char(u32 cp);

#endif
