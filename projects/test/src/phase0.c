#include "platform/platform.h"
#include "rt/rt.h"

int phase0_run(void) {
  char num_buf[32];
  usize bytes_read = 0;
  usize n;

  if (platform_write_stdout("phase0: runtime online\n") != 0) {
    return 1;
  }

  if (platform_probe_read_file("plan.md", &bytes_read) != 0) {
    platform_write_stdout("phase0: failed to open/read plan.md\n");
    return 2;
  }

  platform_write_stdout("phase0: read bytes from plan.md = ");
  n = rt_u64_to_dec((u64)bytes_read, num_buf, sizeof(num_buf));
  rt_write_all(1, num_buf, n);
  platform_write_stdout("\nphase0: success\n");
  return 0;
}
