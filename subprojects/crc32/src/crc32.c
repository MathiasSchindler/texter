#include "crc32/crc32.h"

static u32 crc32_table[256];
static int crc32_table_ready;

static void crc32_make_table(void) {
  u32 i;
  for (i = 0; i < 256; i++) {
    u32 c = i;
    int k;
    for (k = 0; k < 8; k++) {
      c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
    }
    crc32_table[i] = c;
  }
  crc32_table_ready = 1;
}

u32 crc32_init(void) {
  if (!crc32_table_ready) {
    crc32_make_table();
  }
  return 0xFFFFFFFFU;
}

u32 crc32_update(u32 crc, const u8* data, usize len) {
  usize i;
  if (!crc32_table_ready) {
    crc32_make_table();
  }
  for (i = 0; i < len; i++) {
    u32 idx = (crc ^ data[i]) & 0xFFU;
    crc = crc32_table[idx] ^ (crc >> 8);
  }
  return crc;
}

u32 crc32_final(u32 crc) {
  return crc ^ 0xFFFFFFFFU;
}

u32 crc32_compute(const u8* data, usize len) {
  u32 crc = crc32_init();
  crc = crc32_update(crc, data, len);
  return crc32_final(crc);
}
