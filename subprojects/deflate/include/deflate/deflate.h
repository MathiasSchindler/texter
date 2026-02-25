#ifndef DEFLATE_DEFLATE_H
#define DEFLATE_DEFLATE_H

#include "rt/types.h"

#define DEFLATE_OK 0
#define DEFLATE_ERR_DST_TOO_SMALL (-1)
#define DEFLATE_ERR_INVALID_STREAM (-2)
#define DEFLATE_ERR_UNSUPPORTED (-3)

typedef enum deflate_level {
  /* Legacy/compat mode. Prefer DEFLATE_LEVEL_BEST for production paths. */
  DEFLATE_LEVEL_STORE_ONLY = 0,
  DEFLATE_LEVEL_BEST = 1
} deflate_level;

/*
 * Compresses src into raw DEFLATE stream.
 * BEST is the recommended policy and is used by package-writing paths.
 * STORE_ONLY remains for compatibility/testing scenarios.
 */
int deflate_compress(const u8* src,
                     usize src_len,
                     deflate_level level,
                     u8* dst,
                     usize dst_cap,
                     usize* dst_len);

/*
 * Decompresses raw DEFLATE stream.
 * Supports STORED, FIXED Huffman, and DYNAMIC Huffman blocks.
 */
int deflate_inflate(const u8* src,
                    usize src_len,
                    u8* dst,
                    usize dst_cap,
                    usize* dst_len);

#endif
