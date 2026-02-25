#include "zip/zip.h"

#include "crc32/crc32.h"
#include "deflate/deflate.h"
#include "rt/rt.h"

static u16 rd_le16(const u8* p) {
  return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 rd_le32(const u8* p) {
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr_le16(u8* p, u16 v) {
  p[0] = (u8)(v & 0xFFU);
  p[1] = (u8)((v >> 8) & 0xFFU);
}

static void wr_le32(u8* p, u32 v) {
  p[0] = (u8)(v & 0xFFU);
  p[1] = (u8)((v >> 8) & 0xFFU);
  p[2] = (u8)((v >> 16) & 0xFFU);
  p[3] = (u8)((v >> 24) & 0xFFU);
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

static int name_is_safe_raw(const char* name, usize name_len) {
  usize i = 0;
  if (name_len == 0) {
    return 1;
  }
  if (name[0] == '/' || name[0] == '\\') {
    return 0;
  }
  while (i < name_len) {
    usize seg_start = i;
    while (i < name_len && name[i] != '/' && name[i] != '\\') {
      i++;
    }
    if (i - seg_start == 2 && name[seg_start] == '.' && name[seg_start + 1] == '.') {
      return 0;
    }
    if (i < name_len) {
      i++;
    }
  }
  return 1;
}

int zip_archive_open(zip_archive* za, const u8* data, usize len) {
  usize scan_start;
  usize pos;
  usize eocd_pos = (usize)-1;
  u16 total_entries;
  u32 cd_size;
  u32 cd_offset;

  if (len < 22) {
    return ZIP_ERR_INVALID;
  }

  scan_start = (len > 70000U) ? (len - 70000U) : 0U;
  pos = len - 22;
  for (;;) {
    if (rd_le32(data + pos) == 0x06054B50U) {
      eocd_pos = pos;
      break;
    }
    if (pos == scan_start) {
      break;
    }
    pos--;
  }

  if (eocd_pos == (usize)-1) {
    return ZIP_ERR_INVALID;
  }

  total_entries = rd_le16(data + eocd_pos + 10);
  cd_size = rd_le32(data + eocd_pos + 12);
  cd_offset = rd_le32(data + eocd_pos + 16);

  if ((usize)cd_offset + (usize)cd_size > len) {
    return ZIP_ERR_INVALID;
  }

  za->data = data;
  za->len = len;
  za->cd_pos = (usize)cd_offset;
  za->cd_size = (usize)cd_size;
  za->total_entries = total_entries;
  return ZIP_OK;
}

int zip_archive_entry_count(const zip_archive* za, u16* out_count) {
  *out_count = za->total_entries;
  return ZIP_OK;
}

int zip_archive_get_entry(const zip_archive* za, u16 index, zip_entry_view* out) {
  usize pos = za->cd_pos;
  usize end = za->cd_pos + za->cd_size;
  u16 i;

  if (index >= za->total_entries) {
    return ZIP_ERR_NOT_FOUND;
  }

  for (i = 0; i <= index; i++) {
    u16 name_len;
    u16 extra_len;
    u16 comment_len;

    if (pos + 46 > end || rd_le32(za->data + pos) != 0x02014B50U) {
      return ZIP_ERR_INVALID;
    }

    name_len = rd_le16(za->data + pos + 28);
    extra_len = rd_le16(za->data + pos + 30);
    comment_len = rd_le16(za->data + pos + 32);

    if (i == index) {
      u16 method = rd_le16(za->data + pos + 10);
      u16 gp_flags = rd_le16(za->data + pos + 8);
      u32 crc = rd_le32(za->data + pos + 16);
      u32 comp_size = rd_le32(za->data + pos + 20);
      u32 uncomp_size = rd_le32(za->data + pos + 24);
      u32 local_off = rd_le32(za->data + pos + 42);
      usize lpos = (usize)local_off;
      usize data_pos;
      u16 lname_len;
      u16 lextra_len;

      if (lpos + 30 > za->len || rd_le32(za->data + lpos) != 0x04034B50U) {
        return ZIP_ERR_INVALID;
      }

      lname_len = rd_le16(za->data + lpos + 26);
      lextra_len = rd_le16(za->data + lpos + 28);
      data_pos = lpos + 30 + (usize)lname_len + (usize)lextra_len;

      if (data_pos + (usize)comp_size > za->len) {
        return ZIP_ERR_INVALID;
      }

      out->archive_data = za->data;
      out->archive_len = za->len;
      out->name = (const char*)(za->data + pos + 46);
      out->name_len = name_len;
      out->method = method;
      out->gp_flags = gp_flags;
      out->crc32 = crc;
      out->comp_size = comp_size;
      out->uncomp_size = uncomp_size;
      out->local_header_pos = lpos;
      out->data_pos = data_pos;
      return ZIP_OK;
    }

    pos += 46 + (usize)name_len + (usize)extra_len + (usize)comment_len;
  }

  return ZIP_ERR_NOT_FOUND;
}

int zip_archive_find_entry(const zip_archive* za, const char* name, zip_entry_view* out) {
  u16 n;
  u16 i;
  zip_archive_entry_count(za, &n);
  for (i = 0; i < n; i++) {
    zip_entry_view ze;
    int rc = zip_archive_get_entry(za, i, &ze);
    if (rc != ZIP_OK) {
      return rc;
    }
    if (cstr_eq_n(name, ze.name, ze.name_len)) {
      *out = ze;
      return ZIP_OK;
    }
  }
  return ZIP_ERR_NOT_FOUND;
}

int zip_entry_name_is_safe(const zip_entry_view* ze) {
  return name_is_safe_raw(ze->name, ze->name_len) ? ZIP_OK : ZIP_ERR_UNSAFE_NAME;
}

int zip_entry_extract(const zip_entry_view* ze, u8* dst, usize dst_cap, usize* out_len) {
  const u8* src = ze->archive_data + ze->data_pos;

  if (ze->method == ZIP_METHOD_STORE) {
    if ((usize)ze->uncomp_size > dst_cap || ze->comp_size != ze->uncomp_size) {
      return ZIP_ERR_DST_TOO_SMALL;
    }
    {
      usize i;
      for (i = 0; i < (usize)ze->uncomp_size; i++) {
        dst[i] = src[i];
      }
    }
    *out_len = (usize)ze->uncomp_size;
    return ZIP_OK;
  }

  if (ze->method == ZIP_METHOD_DEFLATE) {
    int rc = deflate_inflate(src, (usize)ze->comp_size, dst, dst_cap, out_len);
    if (rc != DEFLATE_OK) {
      return ZIP_ERR_INVALID;
    }
    if (*out_len != (usize)ze->uncomp_size) {
      return ZIP_ERR_INVALID;
    }
    return ZIP_OK;
  }

  return ZIP_ERR_UNSUPPORTED_METHOD;
}

typedef struct zip_sink_bridge {
  zip_extract_sink sink;
  void* user;
} zip_sink_bridge;

static int zip_sink_deflate_bridge(void* user, const u8* data, usize len) {
  zip_sink_bridge* b = (zip_sink_bridge*)user;
  return b->sink(b->user, data, len);
}

int zip_entry_extract_stream(const zip_entry_view* ze,
                             zip_extract_sink sink,
                             void* sink_user,
                             usize* out_len) {
  const u8* src = ze->archive_data + ze->data_pos;
  if (sink == (void*)0 || out_len == (void*)0) {
    return ZIP_ERR_INVALID;
  }

  if (ze->method == ZIP_METHOD_STORE) {
    usize pos = 0;
    while (pos < (usize)ze->uncomp_size) {
      usize chunk = (usize)ze->uncomp_size - pos;
      if (chunk > 4096U) {
        chunk = 4096U;
      }
      if (sink(sink_user, src + pos, chunk) != ZIP_OK) {
        return ZIP_ERR_DST_TOO_SMALL;
      }
      pos += chunk;
    }
    *out_len = (usize)ze->uncomp_size;
    return ZIP_OK;
  }

  if (ze->method == ZIP_METHOD_DEFLATE) {
    int rc;
    zip_sink_bridge bridge;
    bridge.sink = sink;
    bridge.user = sink_user;
    rc = deflate_inflate_stream(src,
                                (usize)ze->comp_size,
                                zip_sink_deflate_bridge,
                                &bridge,
                                out_len);
    if (rc != DEFLATE_OK) {
      return ZIP_ERR_INVALID;
    }
    if (*out_len != (usize)ze->uncomp_size) {
      return ZIP_ERR_INVALID;
    }
    return ZIP_OK;
  }

  return ZIP_ERR_UNSUPPORTED_METHOD;
}

void zip_writer_init(zip_writer* zw, u8* dst, usize cap) {
  zw->dst = dst;
  zw->cap = cap;
  zw->pos = 0;
  zw->entry_count = 0;
}

static int zip_writer_write_local_header(zip_writer* zw,
                                         u32 local_off,
                                         const char* name,
                                         u16 name_len,
                                         u16 method,
                                         u32 crc,
                                         u32 comp_size,
                                         u32 uncomp_size) {
  usize p = (usize)local_off;
  if (p + 30 + (usize)name_len > zw->cap) {
    return ZIP_ERR_DST_TOO_SMALL;
  }

  wr_le32(zw->dst + p + 0, 0x04034B50U);
  wr_le16(zw->dst + p + 4, 20);
  wr_le16(zw->dst + p + 6, 0);
  wr_le16(zw->dst + p + 8, method);
  wr_le16(zw->dst + p + 10, 0);
  wr_le16(zw->dst + p + 12, 0);
  wr_le32(zw->dst + p + 14, crc);
  wr_le32(zw->dst + p + 18, comp_size);
  wr_le32(zw->dst + p + 22, uncomp_size);
  wr_le16(zw->dst + p + 26, name_len);
  wr_le16(zw->dst + p + 28, 0);

  {
    usize i;
    for (i = 0; i < (usize)name_len; i++) {
      zw->dst[p + 30 + i] = (u8)name[i];
    }
  }

  return ZIP_OK;
}

static void zip_writer_fill_meta(zip_writer_entry_meta* m,
                                 const char* name,
                                 usize name_len,
                                 u16 method,
                                 u32 crc,
                                 u32 comp_size,
                                 u32 uncomp_size,
                                 u32 local_off) {
  usize i;
  for (i = 0; i < name_len; i++) {
    m->name[i] = name[i];
  }
  m->name[name_len] = '\0';
  m->name_len = (u16)name_len;
  m->method = method;
  m->crc32 = crc;
  m->comp_size = comp_size;
  m->uncomp_size = uncomp_size;
  m->local_header_offset = local_off;
}

int zip_writer_add_entry(zip_writer* zw,
                         const char* name,
                         const u8* data,
                         usize len,
                         u16 method) {
  u32 crc;
  u32 local_off;
  usize name_len = rt_strlen(name);
  zip_writer_entry_meta* m;

  if (zw->entry_count >= ZIP_WRITER_MAX_ENTRIES) {
    return ZIP_ERR_LIMIT;
  }
  if (name_len == 0 || name_len > ZIP_WRITER_MAX_NAME) {
    return ZIP_ERR_INVALID;
  }
  if (!name_is_safe_raw(name, name_len)) {
    return ZIP_ERR_UNSAFE_NAME;
  }

  crc = crc32_compute(data, len);
  local_off = (u32)zw->pos;

  if (method == ZIP_METHOD_STORE) {
    u32 comp_size = (u32)len;
    int rc = zip_writer_write_local_header(zw, local_off, name, (u16)name_len, method, crc,
                                           comp_size, (u32)len);
    if (rc != ZIP_OK) {
      return rc;
    }

    zw->pos += 30 + name_len;
    if (zw->pos + len > zw->cap) {
      return ZIP_ERR_DST_TOO_SMALL;
    }

    {
      usize i;
      for (i = 0; i < len; i++) {
        zw->dst[zw->pos + i] = data[i];
      }
    }
    zw->pos += len;

    m = &zw->entries[zw->entry_count++];
    zip_writer_fill_meta(m, name, name_len, method, crc, comp_size, (u32)len, local_off);
    return ZIP_OK;
  }

  if (method == ZIP_METHOD_DEFLATE) {
    usize comp_bound = len + ((len / 65535U) + 1U) * 5U;
    usize comp_out = 0;
    int rc;

    if (zw->pos + 30 + name_len + comp_bound > zw->cap) {
      return ZIP_ERR_DST_TOO_SMALL;
    }

    rc = zip_writer_write_local_header(zw, local_off, name, (u16)name_len, method, crc, 0, 0);
    if (rc != ZIP_OK) {
      return rc;
    }

    zw->pos += 30 + name_len;
    rc = deflate_compress(data, len, DEFLATE_LEVEL_BEST, zw->dst + zw->pos,
                          zw->cap - zw->pos, &comp_out);
    if (rc != DEFLATE_OK) {
      return ZIP_ERR_INVALID;
    }

    /* Patch sizes and crc into local header now that compressed size is known. */
    wr_le32(zw->dst + local_off + 14, crc);
    wr_le32(zw->dst + local_off + 18, (u32)comp_out);
    wr_le32(zw->dst + local_off + 22, (u32)len);

    zw->pos += comp_out;

    m = &zw->entries[zw->entry_count++];
    zip_writer_fill_meta(m, name, name_len, method, crc, (u32)comp_out, (u32)len, local_off);
    return ZIP_OK;
  }

  return ZIP_ERR_UNSUPPORTED_METHOD;
}

int zip_writer_add_raw_entry(zip_writer* zw,
                             const char* name,
                             u16 method,
                             u32 crc32,
                             u32 comp_size,
                             u32 uncomp_size,
                             const u8* comp_data,
                             usize comp_data_len) {
  u32 local_off;
  usize name_len = rt_strlen(name);
  zip_writer_entry_meta* m;
  int rc;

  if (zw->entry_count >= ZIP_WRITER_MAX_ENTRIES) {
    return ZIP_ERR_LIMIT;
  }
  if (name_len == 0 || name_len > ZIP_WRITER_MAX_NAME) {
    return ZIP_ERR_INVALID;
  }
  if (!name_is_safe_raw(name, name_len)) {
    return ZIP_ERR_UNSAFE_NAME;
  }
  if (method != ZIP_METHOD_STORE && method != ZIP_METHOD_DEFLATE) {
    return ZIP_ERR_UNSUPPORTED_METHOD;
  }
  if ((usize)comp_size != comp_data_len) {
    return ZIP_ERR_INVALID;
  }
  if (method == ZIP_METHOD_STORE && comp_size != uncomp_size) {
    return ZIP_ERR_INVALID;
  }

  local_off = (u32)zw->pos;
  if (zw->pos + 30 + name_len + comp_data_len > zw->cap) {
    return ZIP_ERR_DST_TOO_SMALL;
  }

  rc = zip_writer_write_local_header(
      zw, local_off, name, (u16)name_len, method, crc32, comp_size, uncomp_size);
  if (rc != ZIP_OK) {
    return rc;
  }

  zw->pos += 30 + name_len;
  {
    usize i;
    for (i = 0; i < comp_data_len; i++) {
      zw->dst[zw->pos + i] = comp_data[i];
    }
  }
  zw->pos += comp_data_len;

  m = &zw->entries[zw->entry_count++];
  zip_writer_fill_meta(m, name, name_len, method, crc32, comp_size, uncomp_size, local_off);
  return ZIP_OK;
}

int zip_writer_finish(zip_writer* zw, usize* out_len) {
  usize cd_start = zw->pos;
  usize i;

  for (i = 0; i < zw->entry_count; i++) {
    zip_writer_entry_meta* m = &zw->entries[i];
    if (zw->pos + 46 + m->name_len > zw->cap) {
      return ZIP_ERR_DST_TOO_SMALL;
    }

    wr_le32(zw->dst + zw->pos + 0, 0x02014B50U);
    wr_le16(zw->dst + zw->pos + 4, 20);
    wr_le16(zw->dst + zw->pos + 6, 20);
    wr_le16(zw->dst + zw->pos + 8, 0);
    wr_le16(zw->dst + zw->pos + 10, m->method);
    wr_le16(zw->dst + zw->pos + 12, 0);
    wr_le16(zw->dst + zw->pos + 14, 0);
    wr_le32(zw->dst + zw->pos + 16, m->crc32);
    wr_le32(zw->dst + zw->pos + 20, m->comp_size);
    wr_le32(zw->dst + zw->pos + 24, m->uncomp_size);
    wr_le16(zw->dst + zw->pos + 28, m->name_len);
    wr_le16(zw->dst + zw->pos + 30, 0);
    wr_le16(zw->dst + zw->pos + 32, 0);
    wr_le16(zw->dst + zw->pos + 34, 0);
    wr_le16(zw->dst + zw->pos + 36, 0);
    wr_le32(zw->dst + zw->pos + 38, 0);
    wr_le32(zw->dst + zw->pos + 42, m->local_header_offset);

    {
      usize j;
      for (j = 0; j < m->name_len; j++) {
        zw->dst[zw->pos + 46 + j] = (u8)m->name[j];
      }
    }

    zw->pos += 46 + m->name_len;
  }

  if (zw->pos + 22 > zw->cap) {
    return ZIP_ERR_DST_TOO_SMALL;
  }

  wr_le32(zw->dst + zw->pos + 0, 0x06054B50U);
  wr_le16(zw->dst + zw->pos + 4, 0);
  wr_le16(zw->dst + zw->pos + 6, 0);
  wr_le16(zw->dst + zw->pos + 8, zw->entry_count);
  wr_le16(zw->dst + zw->pos + 10, zw->entry_count);
  wr_le32(zw->dst + zw->pos + 12, (u32)(zw->pos - cd_start));
  wr_le32(zw->dst + zw->pos + 16, (u32)cd_start);
  wr_le16(zw->dst + zw->pos + 20, 0);

  zw->pos += 22;
  *out_len = zw->pos;
  return ZIP_OK;
}
