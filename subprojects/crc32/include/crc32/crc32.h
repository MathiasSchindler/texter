#ifndef CRC32_CRC32_H
#define CRC32_CRC32_H

#include "rt/types.h"

u32 crc32_init(void);
u32 crc32_update(u32 crc, const u8* data, usize len);
u32 crc32_final(u32 crc);
u32 crc32_compute(const u8* data, usize len);

#endif
