#ifndef ARENA_ARENA_H
#define ARENA_ARENA_H

#include "rt/types.h"

typedef struct arena_state {
  u8* base;
  usize cap;
  usize off;
} arena_state;

void arena_init(arena_state* a, void* backing, usize cap);
void arena_reset(arena_state* a);
void* arena_alloc(arena_state* a, usize size, usize align);

#endif
