#include "phase_api.h"
#include "platform/platform.h"

int main(void) {
  if (phase0_run() != 0) {
    platform_write_stdout("test: phase0 failed\n");
    return 1;
  }
  if (phase1_run() != 0) {
    platform_write_stdout("test: phase1 failed\n");
    return 1;
  }
  if (phase2_run() != 0) {
    platform_write_stdout("test: phase2 failed\n");
    return 1;
  }
  if (phase3_run() != 0) {
    platform_write_stdout("test: phase3 failed\n");
    return 1;
  }
  if (phase4_run() != 0) {
    platform_write_stdout("test: phase4 failed\n");
    return 1;
  }
  if (phase5_run() != 0) {
    platform_write_stdout("test: phase5 failed\n");
    return 1;
  }
  if (phase6_run() != 0) {
    platform_write_stdout("test: phase6 failed\n");
    return 1;
  }
  if (phase7_run() != 0) {
    platform_write_stdout("test: phase7 failed\n");
    return 1;
  }
  if (phase8_run() != 0) {
    platform_write_stdout("test: phase8 failed\n");
    return 1;
  }
  if (phase9_run() != 0) {
    platform_write_stdout("test: phase9 failed\n");
    return 1;
  }
  if (phase10_run() != 0) {
    platform_write_stdout("test: phase10 failed\n");
    return 1;
  }

  platform_write_stdout("test: all phases passed\n");
  return 0;
}
