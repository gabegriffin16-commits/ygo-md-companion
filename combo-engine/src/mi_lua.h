// Lua's allocator (lauxlib's l_alloc) goes through mimalloc: building and tearing down a duel is mostly
// small Lua allocations, and the system allocator is a large share of that time.
#include <stdlib.h>
#include <mimalloc.h>
#define realloc mi_realloc
#define free mi_free
