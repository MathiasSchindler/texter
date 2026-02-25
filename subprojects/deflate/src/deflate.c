#include "deflate/deflate.h"

#define DEFLATE_MAX_STORED_BLOCK 65535U
#define DEFLATE_MAX_BITS 15
#define DEFLATE_MAX_LITLEN 286
#define DEFLATE_MAX_DIST 30

typedef struct bit_reader {
  const u8* src;
  usize src_len;
  usize byte_pos;
  u32 bit_buf;
  u32 bit_count;
} bit_reader;

typedef struct huff_node {
  int left;
  int right;
  int symbol;
} huff_node;

typedef struct huff_tree {
  huff_node nodes[4096];
  usize node_count;
} huff_tree;

typedef struct bit_writer {
  u8* dst;
  usize cap;
  usize pos;
  u32 bit_buf;
  u32 bit_count;
} bit_writer;

static u32 bit_reverse_n(u32 v, u32 n) {
  u32 r = 0;
  u32 i;
  for (i = 0; i < n; i++) {
    r = (r << 1) | (v & 1U);
    v >>= 1;
  }
  return r;
}

static const u16 k_length_base[29] = {
    3,   4,   5,   6,   7,   8,   9,   10,  11,  13,
    15,  17,  19,  23,  27,  31,  35,  43,  51,  59,
    67,  83,  99,  115, 131, 163, 195, 227, 258};

static const u8 k_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0};

static const u16 k_dist_base[30] = {
    1,    2,    3,    4,    5,    7,    9,    13,   17,   25,
    33,   49,   65,   97,   129,  193,  257,  385,  513,  769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

static const u8 k_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
    4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int br_read_bits(bit_reader* br, u32 nbits, u32* out) {
  while (br->bit_count < nbits) {
    if (br->byte_pos >= br->src_len) {
      return DEFLATE_ERR_INVALID_STREAM;
    }
    br->bit_buf |= (u32)br->src[br->byte_pos++] << br->bit_count;
    br->bit_count += 8;
  }

  *out = br->bit_buf & ((1U << nbits) - 1U);
  br->bit_buf >>= nbits;
  br->bit_count -= nbits;
  return DEFLATE_OK;
}

static int br_read_bit(bit_reader* br, u32* out) {
  return br_read_bits(br, 1, out);
}

static void br_align_to_byte(bit_reader* br) {
  u32 drop = br->bit_count & 7U;
  br->bit_buf >>= drop;
  br->bit_count -= drop;
}

static void huff_tree_init(huff_tree* t) {
  t->node_count = 1;
  t->nodes[0].left = -1;
  t->nodes[0].right = -1;
  t->nodes[0].symbol = -1;
}

static int huff_tree_new_node(huff_tree* t) {
  usize idx = t->node_count;
  if (idx >= sizeof(t->nodes) / sizeof(t->nodes[0])) {
    return -1;
  }
  t->nodes[idx].left = -1;
  t->nodes[idx].right = -1;
  t->nodes[idx].symbol = -1;
  t->node_count++;
  return (int)idx;
}

static int huff_tree_insert(huff_tree* t, u32 code, u32 nbits, int symbol) {
  int node = 0;
  u32 i;
  for (i = 0; i < nbits; i++) {
    u32 b = (code >> i) & 1U;
    int* edge = (b == 0) ? &t->nodes[node].left : &t->nodes[node].right;
    if (*edge < 0) {
      int next = huff_tree_new_node(t);
      if (next < 0) {
        return DEFLATE_ERR_INVALID_STREAM;
      }
      *edge = next;
    }
    node = *edge;
  }

  if (t->nodes[node].symbol >= 0) {
    return DEFLATE_ERR_INVALID_STREAM;
  }
  t->nodes[node].symbol = symbol;
  return DEFLATE_OK;
}

static int huff_build(const u8* lens, usize nsyms, huff_tree* out) {
  u16 bl_count[DEFLATE_MAX_BITS + 1];
  u16 next_code[DEFLATE_MAX_BITS + 1];
  u32 code = 0;
  usize sym;
  u32 bits;
  int any = 0;

  for (bits = 0; bits <= DEFLATE_MAX_BITS; bits++) {
    bl_count[bits] = 0;
    next_code[bits] = 0;
  }

  for (sym = 0; sym < nsyms; sym++) {
    u8 l = lens[sym];
    if (l > DEFLATE_MAX_BITS) {
      return DEFLATE_ERR_INVALID_STREAM;
    }
    if (l != 0) {
      bl_count[l]++;
      any = 1;
    }
  }

  if (!any) {
    return DEFLATE_ERR_INVALID_STREAM;
  }

  for (bits = 1; bits <= DEFLATE_MAX_BITS; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = (u16)code;
  }

  huff_tree_init(out);
  for (sym = 0; sym < nsyms; sym++) {
    u8 len = lens[sym];
    if (len != 0) {
      u16 c = next_code[len]++;
      u32 rev = bit_reverse_n(c, len);
      int rc = huff_tree_insert(out, rev, len, (int)sym);
      if (rc != DEFLATE_OK) {
        return rc;
      }
    }
  }

  return DEFLATE_OK;
}

static int huff_decode_symbol(bit_reader* br, const huff_tree* t, int* out_symbol) {
  int node = 0;
  for (;;) {
    u32 b;
    int rc;
    if (t->nodes[node].symbol >= 0) {
      *out_symbol = t->nodes[node].symbol;
      return DEFLATE_OK;
    }
    rc = br_read_bit(br, &b);
    if (rc != DEFLATE_OK) {
      return rc;
    }
    node = (b == 0) ? t->nodes[node].left : t->nodes[node].right;
    if (node < 0) {
      return DEFLATE_ERR_INVALID_STREAM;
    }
  }
}

static int build_fixed_trees(huff_tree* litlen, huff_tree* dist) {
  u8 litlen_lens[288];
  u8 dist_lens[32];
  usize i;

  for (i = 0; i < 288; i++) {
    litlen_lens[i] = 0;
  }

  for (i = 0; i <= 143; i++) {
    litlen_lens[i] = 8;
  }
  for (i = 144; i <= 255; i++) {
    litlen_lens[i] = 9;
  }
  for (i = 256; i <= 279; i++) {
    litlen_lens[i] = 7;
  }
  for (i = 280; i <= 285; i++) {
    litlen_lens[i] = 8;
  }

  for (i = 0; i < 32; i++) {
    dist_lens[i] = 5;
  }

  if (huff_build(litlen_lens, 288, litlen) != DEFLATE_OK) {
    return DEFLATE_ERR_INVALID_STREAM;
  }
  if (huff_build(dist_lens, 32, dist) != DEFLATE_OK) {
    return DEFLATE_ERR_INVALID_STREAM;
  }
  return DEFLATE_OK;
}

static int decode_compressed_data(bit_reader* br,
                                  const huff_tree* litlen,
                                  const huff_tree* dist,
                                  u8* dst,
                                  usize dst_cap,
                                  usize* out_pos) {
  for (;;) {
    int sym;
    int rc = huff_decode_symbol(br, litlen, &sym);
    if (rc != DEFLATE_OK) {
      return rc;
    }

    if (sym < 256) {
      if (*out_pos >= dst_cap) {
        return DEFLATE_ERR_DST_TOO_SMALL;
      }
      dst[(*out_pos)++] = (u8)sym;
      continue;
    }

    if (sym == 256) {
      return DEFLATE_OK;
    }

    if (sym < 257 || sym > 285) {
      return DEFLATE_ERR_INVALID_STREAM;
    }

    {
      u32 extra_len_bits = k_length_extra[sym - 257];
      u32 extra_len = 0;
      u32 length = k_length_base[sym - 257];
      int dist_sym;
      u32 dist_extra;
      u32 distance;
      usize i;

      if (extra_len_bits) {
        rc = br_read_bits(br, extra_len_bits, &extra_len);
        if (rc != DEFLATE_OK) {
          return rc;
        }
        length += extra_len;
      }

      rc = huff_decode_symbol(br, dist, &dist_sym);
      if (rc != DEFLATE_OK) {
        return rc;
      }

      if (dist_sym < 0 || dist_sym >= 30) {
        return DEFLATE_ERR_INVALID_STREAM;
      }

      dist_extra = 0;
      if (k_dist_extra[dist_sym]) {
        rc = br_read_bits(br, k_dist_extra[dist_sym], &dist_extra);
        if (rc != DEFLATE_OK) {
          return rc;
        }
      }
      distance = k_dist_base[dist_sym] + dist_extra;

      if (distance == 0 || distance > *out_pos) {
        return DEFLATE_ERR_INVALID_STREAM;
      }

      if (*out_pos + length > dst_cap) {
        return DEFLATE_ERR_DST_TOO_SMALL;
      }

      for (i = 0; i < (usize)length; i++) {
        dst[*out_pos] = dst[*out_pos - distance];
        (*out_pos)++;
      }
    }
  }
}

static int read_dynamic_trees(bit_reader* br, huff_tree* litlen_tree, huff_tree* dist_tree) {
  static const u8 k_cl_order[19] = {16, 17, 18, 0, 8, 7, 9,  6, 10, 5,
                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
  u32 hlit_bits, hdist_bits, hclen_bits;
  u32 hlit, hdist, hclen;
  u8 cl_lens[19];
  huff_tree cl_tree;
  u8 ll_lens[DEFLATE_MAX_LITLEN + DEFLATE_MAX_DIST];
  usize total;
  usize idx;
  int rc;
  usize i;

  rc = br_read_bits(br, 5, &hlit_bits);
  if (rc != DEFLATE_OK) {
    return rc;
  }
  rc = br_read_bits(br, 5, &hdist_bits);
  if (rc != DEFLATE_OK) {
    return rc;
  }
  rc = br_read_bits(br, 4, &hclen_bits);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  hlit = hlit_bits + 257;
  hdist = hdist_bits + 1;
  hclen = hclen_bits + 4;

  if (hlit > DEFLATE_MAX_LITLEN || hdist > DEFLATE_MAX_DIST) {
    return DEFLATE_ERR_INVALID_STREAM;
  }

  for (i = 0; i < 19; i++) {
    cl_lens[i] = 0;
  }

  for (i = 0; i < hclen; i++) {
    u32 v;
    rc = br_read_bits(br, 3, &v);
    if (rc != DEFLATE_OK) {
      return rc;
    }
    cl_lens[k_cl_order[i]] = (u8)v;
  }

  rc = huff_build(cl_lens, 19, &cl_tree);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  total = (usize)(hlit + hdist);
  idx = 0;
  while (idx < total) {
    int sym;
    rc = huff_decode_symbol(br, &cl_tree, &sym);
    if (rc != DEFLATE_OK) {
      return rc;
    }

    if (sym >= 0 && sym <= 15) {
      ll_lens[idx++] = (u8)sym;
    } else if (sym == 16) {
      u32 rep_bits;
      u32 rep;
      u8 prev;
      if (idx == 0) {
        return DEFLATE_ERR_INVALID_STREAM;
      }
      rc = br_read_bits(br, 2, &rep_bits);
      if (rc != DEFLATE_OK) {
        return rc;
      }
      rep = rep_bits + 3;
      prev = ll_lens[idx - 1];
      while (rep-- && idx < total) {
        ll_lens[idx++] = prev;
      }
    } else if (sym == 17) {
      u32 rep_bits;
      u32 rep;
      rc = br_read_bits(br, 3, &rep_bits);
      if (rc != DEFLATE_OK) {
        return rc;
      }
      rep = rep_bits + 3;
      while (rep-- && idx < total) {
        ll_lens[idx++] = 0;
      }
    } else if (sym == 18) {
      u32 rep_bits;
      u32 rep;
      rc = br_read_bits(br, 7, &rep_bits);
      if (rc != DEFLATE_OK) {
        return rc;
      }
      rep = rep_bits + 11;
      while (rep-- && idx < total) {
        ll_lens[idx++] = 0;
      }
    } else {
      return DEFLATE_ERR_INVALID_STREAM;
    }
  }

  rc = huff_build(ll_lens, hlit, litlen_tree);
  if (rc != DEFLATE_OK) {
    return rc;
  }
  rc = huff_build(ll_lens + hlit, hdist, dist_tree);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  return DEFLATE_OK;
}

static int write_stored_block(const u8* src,
                              usize block_len,
                              int is_final,
                              u8* dst,
                              usize dst_cap,
                              usize* dst_pos) {
  u16 len16 = (u16)block_len;
  u16 nlen16 = (u16)(~len16);

  if (*dst_pos + 5 + block_len > dst_cap) {
    return DEFLATE_ERR_DST_TOO_SMALL;
  }

  dst[(*dst_pos)++] = (u8)(is_final ? 0x01 : 0x00);
  dst[(*dst_pos)++] = (u8)(len16 & 0xFFU);
  dst[(*dst_pos)++] = (u8)((len16 >> 8) & 0xFFU);
  dst[(*dst_pos)++] = (u8)(nlen16 & 0xFFU);
  dst[(*dst_pos)++] = (u8)((nlen16 >> 8) & 0xFFU);

  {
    usize i;
    for (i = 0; i < block_len; i++) {
      dst[(*dst_pos)++] = src[i];
    }
  }

  return DEFLATE_OK;
}

static void bw_init(bit_writer* bw, u8* dst, usize cap) {
  bw->dst = dst;
  bw->cap = cap;
  bw->pos = 0;
  bw->bit_buf = 0;
  bw->bit_count = 0;
}

static int bw_put_bits(bit_writer* bw, u32 bits, u32 nbits) {
  if (nbits == 0U) {
    return DEFLATE_OK;
  }
  bw->bit_buf |= (bits & ((1U << nbits) - 1U)) << bw->bit_count;
  bw->bit_count += nbits;
  while (bw->bit_count >= 8U) {
    if (bw->pos >= bw->cap) {
      return DEFLATE_ERR_DST_TOO_SMALL;
    }
    bw->dst[bw->pos++] = (u8)(bw->bit_buf & 0xFFU);
    bw->bit_buf >>= 8;
    bw->bit_count -= 8;
  }
  return DEFLATE_OK;
}

static int bw_finish(bit_writer* bw) {
  if (bw->bit_count != 0U) {
    if (bw->pos >= bw->cap) {
      return DEFLATE_ERR_DST_TOO_SMALL;
    }
    bw->dst[bw->pos++] = (u8)(bw->bit_buf & 0xFFU);
    bw->bit_buf = 0;
    bw->bit_count = 0;
  }
  return DEFLATE_OK;
}

static void fixed_litlen_code(u32 sym, u32* out_code, u32* out_nbits) {
  u32 code;
  u32 nbits;
  if (sym <= 143U) {
    code = 0x30U + sym;
    nbits = 8;
  } else if (sym <= 255U) {
    code = 0x190U + (sym - 144U);
    nbits = 9;
  } else if (sym <= 279U) {
    code = sym - 256U;
    nbits = 7;
  } else {
    code = 0xC0U + (sym - 280U);
    nbits = 8;
  }
  *out_code = bit_reverse_n(code, nbits);
  *out_nbits = nbits;
}

static int __attribute__((unused)) fixed_length_symbol(u32 length,
                                                       u32* out_sym,
                                                       u32* out_extra_bits,
                                                       u32* out_extra) {
  u32 i;
  for (i = 0; i < 29U; i++) {
    u32 base = k_length_base[i];
    u32 extra_bits = k_length_extra[i];
    u32 max = base + ((1U << extra_bits) - 1U);
    if (length >= base && length <= max) {
      *out_sym = 257U + i;
      *out_extra_bits = extra_bits;
      *out_extra = length - base;
      return DEFLATE_OK;
    }
  }
  return DEFLATE_ERR_INVALID_STREAM;
}

static int __attribute__((unused)) fixed_dist_symbol(u32 dist,
                                                     u32* out_sym,
                                                     u32* out_extra_bits,
                                                     u32* out_extra) {
  u32 i;
  for (i = 0; i < 30U; i++) {
    u32 base = k_dist_base[i];
    u32 extra_bits = k_dist_extra[i];
    u32 max = base + ((1U << extra_bits) - 1U);
    if (dist >= base && dist <= max) {
      *out_sym = i;
      *out_extra_bits = extra_bits;
      *out_extra = dist - base;
      return DEFLATE_OK;
    }
  }
  return DEFLATE_ERR_INVALID_STREAM;
}

static int emit_fixed_literal(bit_writer* bw, u8 lit) {
  u32 code;
  u32 nbits;
  fixed_litlen_code((u32)lit, &code, &nbits);
  return bw_put_bits(bw, code, nbits);
}

static int __attribute__((unused)) emit_fixed_match(bit_writer* bw, u32 length, u32 dist) {
  u32 len_sym;
  u32 len_extra_bits;
  u32 len_extra;
  u32 dist_sym;
  u32 dist_extra_bits;
  u32 dist_extra;
  u32 code;
  u32 nbits;
  int rc;

  rc = fixed_length_symbol(length, &len_sym, &len_extra_bits, &len_extra);
  if (rc != DEFLATE_OK) {
    return rc;
  }
  rc = fixed_dist_symbol(dist, &dist_sym, &dist_extra_bits, &dist_extra);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  fixed_litlen_code(len_sym, &code, &nbits);
  rc = bw_put_bits(bw, code, nbits);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  rc = bw_put_bits(bw, len_extra, len_extra_bits);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  code = bit_reverse_n(dist_sym, 5U);
  rc = bw_put_bits(bw, code, 5U);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  return bw_put_bits(bw, dist_extra, dist_extra_bits);
}

static int write_fixed_lz77_block(const u8* src,
                                  usize src_len,
                                  int is_final,
                                  usize out_limit,
                                  u8* dst,
                                  usize dst_cap,
                                  usize* dst_pos) {
  bit_writer bw;
  usize i;
  int rc;

  bw_init(&bw, dst + *dst_pos, dst_cap - *dst_pos);

  rc = bw_put_bits(&bw, is_final ? 3U : 2U, 3U);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  i = 0;
  while (i < src_len) {
    /*
     * Keep BEST mode deterministic and portable by emitting literal-only fixed
     * blocks for now. This still typically beats STORED blocks for text and
     * avoids fragile match-path edge cases.
     */
    rc = emit_fixed_literal(&bw, src[i]);
    if (rc != DEFLATE_OK) {
      return rc;
    }
    i++;

    if (*dst_pos + bw.pos + ((bw.bit_count + 7U) / 8U) >= out_limit) {
      return DEFLATE_ERR_UNSUPPORTED;
    }
  }

  {
    u32 eob_code;
    u32 eob_nbits;
    fixed_litlen_code(256U, &eob_code, &eob_nbits);
    rc = bw_put_bits(&bw, eob_code, eob_nbits);
    if (rc != DEFLATE_OK) {
      return rc;
    }
  }

  rc = bw_finish(&bw);
  if (rc != DEFLATE_OK) {
    return rc;
  }

  if (*dst_pos + bw.pos >= out_limit) {
    return DEFLATE_ERR_UNSUPPORTED;
  }

  *dst_pos += bw.pos;
  return DEFLATE_OK;
}

static usize estimate_stored_size(usize src_len) {
  usize blocks = (src_len == 0) ? 1 : ((src_len - 1U) / DEFLATE_MAX_STORED_BLOCK) + 1U;
  return src_len + (blocks * 5U);
}

int deflate_compress(const u8* src,
                     usize src_len,
                     deflate_level level,
                     u8* dst,
                     usize dst_cap,
                     usize* dst_len) {
  usize src_pos = 0;
  usize out_pos = 0;

  if (dst_len == (void*)0) {
    return DEFLATE_ERR_INVALID_STREAM;
  }

  if (level != DEFLATE_LEVEL_STORE_ONLY && level != DEFLATE_LEVEL_BEST) {
    return DEFLATE_ERR_UNSUPPORTED;
  }

  if (level == DEFLATE_LEVEL_BEST) {
    usize stored_sz = estimate_stored_size(src_len);
    int rc_fixed = write_fixed_lz77_block(src, src_len, 1, stored_sz, dst, dst_cap, &out_pos);
    if (rc_fixed == DEFLATE_OK) {
      *dst_len = out_pos;
      return DEFLATE_OK;
    }
    if (rc_fixed != DEFLATE_ERR_UNSUPPORTED) {
      return rc_fixed;
    }
  }

  if (src_len == 0) {
    int rc_empty = write_stored_block(src, 0, 1, dst, dst_cap, &out_pos);
    if (rc_empty != DEFLATE_OK) {
      return rc_empty;
    }
    *dst_len = out_pos;
    return DEFLATE_OK;
  }

  while (src_pos < src_len) {
    usize remain = src_len - src_pos;
    usize chunk = remain > DEFLATE_MAX_STORED_BLOCK ? DEFLATE_MAX_STORED_BLOCK : remain;
    int is_final = (src_pos + chunk == src_len);
    int rc = write_stored_block(src + src_pos, chunk, is_final, dst, dst_cap, &out_pos);
    if (rc != DEFLATE_OK) {
      return rc;
    }
    src_pos += chunk;
  }

  *dst_len = out_pos;
  return DEFLATE_OK;
}

int deflate_inflate(const u8* src,
                    usize src_len,
                    u8* dst,
                    usize dst_cap,
                    usize* dst_len) {
  bit_reader br;
  usize out_pos = 0;
  int done = 0;

  if (dst_len == (void*)0) {
    return DEFLATE_ERR_INVALID_STREAM;
  }

  br.src = src;
  br.src_len = src_len;
  br.byte_pos = 0;
  br.bit_buf = 0;
  br.bit_count = 0;

  while (!done) {
    u32 bfinal;
    u32 btype;
    int rc;

    rc = br_read_bits(&br, 1, &bfinal);
    if (rc != DEFLATE_OK) {
      return rc;
    }

    rc = br_read_bits(&br, 2, &btype);
    if (rc != DEFLATE_OK) {
      return rc;
    }

    if (btype == 0U) {
      br_align_to_byte(&br);

      if (br.byte_pos + 4 > br.src_len) {
        return DEFLATE_ERR_INVALID_STREAM;
      }

      {
        u16 len16 = (u16)br.src[br.byte_pos] | ((u16)br.src[br.byte_pos + 1] << 8);
        u16 nlen16 = (u16)br.src[br.byte_pos + 2] | ((u16)br.src[br.byte_pos + 3] << 8);
        usize i;

        br.byte_pos += 4;

        if ((u16)(len16 ^ nlen16) != 0xFFFFU) {
          return DEFLATE_ERR_INVALID_STREAM;
        }

        if (br.byte_pos + (usize)len16 > br.src_len) {
          return DEFLATE_ERR_INVALID_STREAM;
        }

        if (out_pos + (usize)len16 > dst_cap) {
          return DEFLATE_ERR_DST_TOO_SMALL;
        }

        for (i = 0; i < (usize)len16; i++) {
          dst[out_pos++] = br.src[br.byte_pos++];
        }
      }
    } else if (btype == 1U || btype == 2U) {
      huff_tree litlen_tree;
      huff_tree dist_tree;

      if (btype == 1U) {
        if (build_fixed_trees(&litlen_tree, &dist_tree) != DEFLATE_OK) {
          return DEFLATE_ERR_INVALID_STREAM;
        }
      } else {
        int trc = read_dynamic_trees(&br, &litlen_tree, &dist_tree);
        if (trc != DEFLATE_OK) {
          return trc;
        }
      }

      {
        int drc = decode_compressed_data(&br, &litlen_tree, &dist_tree, dst, dst_cap,
                                         &out_pos);
        if (drc != DEFLATE_OK) {
          return drc;
        }
      }
    } else {
      return DEFLATE_ERR_INVALID_STREAM;
    }

    done = (bfinal != 0U);
  }

  *dst_len = out_pos;
  return DEFLATE_OK;
}
