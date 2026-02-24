#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

#include "rt/types.h"

int platform_write_stdout(const char* text);
int platform_probe_read_file(const char* path, usize* out_bytes_read);

#endif
