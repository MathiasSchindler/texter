#ifndef ZIP_ZIP_H
#define ZIP_ZIP_H

#include "rt/types.h"

#define ZIP_OK 0
#define ZIP_ERR_INVALID (-1)
#define ZIP_ERR_NOT_FOUND (-2)
#define ZIP_ERR_DST_TOO_SMALL (-3)
#define ZIP_ERR_UNSUPPORTED_METHOD (-4)
#define ZIP_ERR_UNSAFE_NAME (-5)
#define ZIP_ERR_LIMIT (-6)

#define ZIP_METHOD_STORE 0
#define ZIP_METHOD_DEFLATE 8

#define ZIP_WRITER_MAX_ENTRIES 2048
#define ZIP_WRITER_MAX_NAME 255

typedef struct zip_archive {
  const u8* data;
  usize len;
  usize cd_pos;
  usize cd_size;
  u16 total_entries;
} zip_archive;

typedef struct zip_entry_view {
  const u8* archive_data;
  usize archive_len;
  const char* name;
  u16 name_len;
  u16 method;
  u16 gp_flags;
  u32 crc32;
  u32 comp_size;
  u32 uncomp_size;
  usize local_header_pos;
  usize data_pos;
} zip_entry_view;

typedef struct zip_writer_entry_meta {
  char name[ZIP_WRITER_MAX_NAME + 1];
  u16 name_len;
  u16 method;
  u32 crc32;
  u32 comp_size;
  u32 uncomp_size;
  u32 local_header_offset;
} zip_writer_entry_meta;

typedef struct zip_writer {
  u8* dst;
  usize cap;
  usize pos;
  zip_writer_entry_meta entries[ZIP_WRITER_MAX_ENTRIES];
  u16 entry_count;
} zip_writer;

int zip_archive_open(zip_archive* za, const u8* data, usize len);
int zip_archive_entry_count(const zip_archive* za, u16* out_count);
int zip_archive_get_entry(const zip_archive* za, u16 index, zip_entry_view* out);
int zip_archive_find_entry(const zip_archive* za, const char* name, zip_entry_view* out);

int zip_entry_name_is_safe(const zip_entry_view* ze);
int zip_entry_extract(const zip_entry_view* ze, u8* dst, usize dst_cap, usize* out_len);

typedef int (*zip_extract_sink)(void* user, const u8* data, usize len);
int zip_entry_extract_stream(const zip_entry_view* ze,
                             zip_extract_sink sink,
                             void* sink_user,
                             usize* out_len);

void zip_writer_init(zip_writer* zw, u8* dst, usize cap);
int zip_writer_add_entry(zip_writer* zw,
                         const char* name,
                         const u8* data,
                         usize len,
                         u16 method);
int zip_writer_add_raw_entry(zip_writer* zw,
                             const char* name,
                             u16 method,
                             u32 crc32,
                             u32 comp_size,
                             u32 uncomp_size,
                             const u8* comp_data,
                             usize comp_data_len);
int zip_writer_finish(zip_writer* zw, usize* out_len);

#endif
