#ifndef RT_SYSCALL_H
#define RT_SYSCALL_H

#include "rt/types.h"

#define RT_NR_READ 0
#define RT_NR_WRITE 1
#define RT_NR_CLOSE 3
#define RT_NR_OPENAT 257
#define RT_NR_EXIT 60

static inline long rt_syscall0(long n) {
  long ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n)
                   : "rcx", "r11", "memory");
  return ret;
}

static inline long rt_syscall1(long n, long a1) {
  long ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n), "D"(a1)
                   : "rcx", "r11", "memory");
  return ret;
}

static inline long rt_syscall2(long n, long a1, long a2) {
  long ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n), "D"(a1), "S"(a2)
                   : "rcx", "r11", "memory");
  return ret;
}

static inline long rt_syscall3(long n, long a1, long a2, long a3) {
  long ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                   : "rcx", "r11", "memory");
  return ret;
}

static inline long rt_syscall4(long n, long a1, long a2, long a3, long a4) {
  long ret;
  register long r10 __asm__("r10") = a4;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                   : "rcx", "r11", "memory");
  return ret;
}

#endif
