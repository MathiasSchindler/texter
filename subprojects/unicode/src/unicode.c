#include "unicode/unicode.h"

static int is_cont(u8 b) {
  return (b & 0xC0U) == 0x80U;
}

int unicode_utf8_decode_one(const u8* src, usize len, u32* out_cp, usize* out_consumed) {
  u8 b0;
  if (len == 0) {
    return UNICODE_ERR_INVALID;
  }

  b0 = src[0];
  if (b0 <= 0x7FU) {
    *out_cp = (u32)b0;
    *out_consumed = 1;
    return UNICODE_OK;
  }

  if ((b0 & 0xE0U) == 0xC0U) {
    u8 b1;
    u32 cp;
    if (len < 2) {
      return UNICODE_ERR_INVALID;
    }
    b1 = src[1];
    if (!is_cont(b1)) {
      return UNICODE_ERR_INVALID;
    }
    cp = ((u32)(b0 & 0x1FU) << 6) | (u32)(b1 & 0x3FU);
    if (cp < 0x80U) {
      return UNICODE_ERR_INVALID;
    }
    *out_cp = cp;
    *out_consumed = 2;
    return UNICODE_OK;
  }

  if ((b0 & 0xF0U) == 0xE0U) {
    u8 b1;
    u8 b2;
    u32 cp;
    if (len < 3) {
      return UNICODE_ERR_INVALID;
    }
    b1 = src[1];
    b2 = src[2];
    if (!is_cont(b1) || !is_cont(b2)) {
      return UNICODE_ERR_INVALID;
    }
    cp = ((u32)(b0 & 0x0FU) << 12) | ((u32)(b1 & 0x3FU) << 6) | (u32)(b2 & 0x3FU);
    if (cp < 0x800U) {
      return UNICODE_ERR_INVALID;
    }
    if (cp >= 0xD800U && cp <= 0xDFFFU) {
      return UNICODE_ERR_INVALID;
    }
    *out_cp = cp;
    *out_consumed = 3;
    return UNICODE_OK;
  }

  if ((b0 & 0xF8U) == 0xF0U) {
    u8 b1;
    u8 b2;
    u8 b3;
    u32 cp;
    if (len < 4) {
      return UNICODE_ERR_INVALID;
    }
    b1 = src[1];
    b2 = src[2];
    b3 = src[3];
    if (!is_cont(b1) || !is_cont(b2) || !is_cont(b3)) {
      return UNICODE_ERR_INVALID;
    }
    cp = ((u32)(b0 & 0x07U) << 18) | ((u32)(b1 & 0x3FU) << 12) |
         ((u32)(b2 & 0x3FU) << 6) | (u32)(b3 & 0x3FU);
    if (cp < 0x10000U || cp > 0x10FFFFU) {
      return UNICODE_ERR_INVALID;
    }
    *out_cp = cp;
    *out_consumed = 4;
    return UNICODE_OK;
  }

  return UNICODE_ERR_INVALID;
}

int unicode_utf8_validate(const u8* src, usize len) {
  usize off = 0;
  while (off < len) {
    u32 cp;
    usize n;
    if (unicode_utf8_decode_one(src + off, len - off, &cp, &n) != UNICODE_OK) {
      return UNICODE_ERR_INVALID;
    }
    (void)cp;
    off += n;
  }
  return UNICODE_OK;
}

int unicode_utf8_encode_one(u32 cp, u8* dst, usize dst_cap, usize* out_len) {
  if (cp <= 0x7FU) {
    if (dst_cap < 1) {
      return UNICODE_ERR_DST_TOO_SMALL;
    }
    dst[0] = (u8)cp;
    *out_len = 1;
    return UNICODE_OK;
  }

  if (cp <= 0x7FFU) {
    if (dst_cap < 2) {
      return UNICODE_ERR_DST_TOO_SMALL;
    }
    dst[0] = (u8)(0xC0U | ((cp >> 6) & 0x1FU));
    dst[1] = (u8)(0x80U | (cp & 0x3FU));
    *out_len = 2;
    return UNICODE_OK;
  }

  if (cp >= 0xD800U && cp <= 0xDFFFU) {
    return UNICODE_ERR_INVALID;
  }

  if (cp <= 0xFFFFU) {
    if (dst_cap < 3) {
      return UNICODE_ERR_DST_TOO_SMALL;
    }
    dst[0] = (u8)(0xE0U | ((cp >> 12) & 0x0FU));
    dst[1] = (u8)(0x80U | ((cp >> 6) & 0x3FU));
    dst[2] = (u8)(0x80U | (cp & 0x3FU));
    *out_len = 3;
    return UNICODE_OK;
  }

  if (cp <= 0x10FFFFU) {
    if (dst_cap < 4) {
      return UNICODE_ERR_DST_TOO_SMALL;
    }
    dst[0] = (u8)(0xF0U | ((cp >> 18) & 0x07U));
    dst[1] = (u8)(0x80U | ((cp >> 12) & 0x3FU));
    dst[2] = (u8)(0x80U | ((cp >> 6) & 0x3FU));
    dst[3] = (u8)(0x80U | (cp & 0x3FU));
    *out_len = 4;
    return UNICODE_OK;
  }

  return UNICODE_ERR_INVALID;
}

int unicode_is_xml_whitespace(u32 cp) {
  return cp == 0x20U || cp == 0x09U || cp == 0x0AU || cp == 0x0DU;
}

int unicode_is_xml_name_start(u32 cp) {
  if ((cp >= (u32)'A' && cp <= (u32)'Z') || (cp >= (u32)'a' && cp <= (u32)'z')) {
    return 1;
  }
  if (cp == (u32)'_' || cp == (u32)':') {
    return 1;
  }
  /* Accept non-ASCII code points for practical XML name support in MVP. */
  if (cp >= 0x80U && cp <= 0x10FFFFU && !(cp >= 0xD800U && cp <= 0xDFFFU)) {
    return 1;
  }
  return 0;
}

int unicode_is_xml_name_char(u32 cp) {
  if (unicode_is_xml_name_start(cp)) {
    return 1;
  }
  if ((cp >= (u32)'0' && cp <= (u32)'9') || cp == (u32)'-' || cp == (u32)'.') {
    return 1;
  }
  return 0;
}
