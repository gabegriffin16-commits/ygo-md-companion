// Lua's allocator (lauxlib's l_alloc) goes through src/arena.cpp: a duel's region while the duel is current (so the
// duel can be saved and restored), mimalloc otherwise (building and tearing down a duel is mostly small Lua allocations).
#include <stdlib.h>
#include <stddef.h>
void* mdc_lua_realloc(void* p, size_t n);
void mdc_lua_free(void* p);
#define realloc mdc_lua_realloc
#define free mdc_lua_free
