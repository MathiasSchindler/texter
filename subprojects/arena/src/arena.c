#include "arena/arena.h"

static usize arena_align_up(usize value, usize align) {
  usize mask;
  if (align <= 1) {
    return value;
  }
  mask = align - 1;
  return (value + mask) & ~mask;
}

void arena_init(arena_state* a, void* backing, usize cap) {
  a->base = (u8*)backing;
  a->cap = cap;
  a->off = 0;
}

void arena_reset(arena_state* a) {
  a->off = 0;
}

void* arena_alloc(arena_state* a, usize size, usize align) {
  usize aligned_off;
  usize end_off;

  if (align == 0) {
    align = 1;
  }

  aligned_off = arena_align_up(a->off, align);

  if (aligned_off > a->cap) {
    return (void*)0;
  }

  end_off = aligned_off + size;
  if (end_off < aligned_off || end_off > a->cap) {
    return (void*)0;
  }

  a->off = end_off;
  return (void*)(a->base + aligned_off);
}
