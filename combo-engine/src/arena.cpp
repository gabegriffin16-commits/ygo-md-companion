// combo-engine: every duel's memory in its own region, so a duel can be saved and restored with one memory copy.
// Copyright (C) 2026 Gabe Griffin and contributors. SPDX-License-Identifier: AGPL-3.0-or-later
//
// The rules core has no way to copy a game in progress, so the search replays a line from the start for every branch
// (most of its time). Here each duel gets its own slice of address space with an allocator whose bookkeeping lives
// inside the slice: while a duel is "current" on a thread (ArenaScope), the core's C++ objects and its Lua state are
// allocated there. Saving the duel = copying the slice's used bytes out; restoring = copying them back to the same
// addresses, where every pointer is still valid. Memory outside a duel (our own code) goes to mimalloc as before.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>
#include <mimalloc.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {
constexpr size_t SLOT = size_t(1) << 30;     // address space per duel (only what's used gets committed)
constexpr int MAXA = 256;                    // duels that can have a region at once
constexpr size_t STEP = size_t(4) << 20;     // commit granularity
constexpr int SMALL = 64;                    // classes 0..63: 16..1024 bytes in 16-byte steps
constexpr int NCLS = SMALL + 24;             // then powers of two from 2 KB up
struct Header { size_t used; void* freel[NCLS]; };   // lives at the start of the region (saved with it)
struct Block { uint32_t cls; uint32_t pad; uint64_t size; };   // 16 bytes in front of every allocation
char* g_base = nullptr;
size_t g_committed[MAXA];
bool g_inuse[MAXA];
std::mutex g_mx;
thread_local int tl_cur = -1;

bool reserve() {
	if(g_base) return true;
#ifdef _WIN32
	g_base = (char*)VirtualAlloc(nullptr, SLOT * MAXA, MEM_RESERVE, PAGE_NOACCESS);
#else
	void* p = mmap(nullptr, SLOT * MAXA, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	g_base = p == MAP_FAILED ? nullptr : (char*)p;
#endif
	return g_base != nullptr;
}
bool commit(int a, size_t upto) {
	if(upto <= g_committed[a]) return true;
	size_t want = (upto + STEP - 1) / STEP * STEP;
	if(want > SLOT) { if(getenv("MDC_ARENALOG")) fprintf(stderr, "arena %d: full (%zu MB)\n", a, want >> 20); return false; }
	char* at = g_base + SLOT * a + g_committed[a];
#ifdef _WIN32
	if(!VirtualAlloc(at, want - g_committed[a], MEM_COMMIT, PAGE_READWRITE)) { if(getenv("MDC_ARENALOG")) fprintf(stderr, "arena %d: commit failed at %zu MB\n", a, want >> 20); return false; }
#else
	if(mprotect(at, want - g_committed[a], PROT_READ | PROT_WRITE) != 0) return false;
#endif
	g_committed[a] = want;
	return true;
}
inline Header* head(int a) { return (Header*)(g_base + SLOT * a); }
inline int owner(const void* p) {
	if(!g_base || (const char*)p < g_base || (const char*)p >= g_base + SLOT * MAXA) return -1;
	return int(((const char*)p - g_base) / SLOT);
}
inline int cls_of(size_t total, size_t& cap) {
	if(total <= 1024) { int c = int((total + 15) / 16) - 1; cap = size_t(c + 1) * 16; return c; }
	int c = SMALL; size_t s = 2048; while(s < total) { s <<= 1; c++; }
	cap = s; return c;
}
void* arena_alloc(int a, size_t n) {
	size_t cap; int c = cls_of(n + sizeof(Block), cap);
	if(c >= NCLS) return nullptr;
	Header* h = head(a);
	Block* b;
	if(h->freel[c]) { b = (Block*)h->freel[c]; h->freel[c] = *(void**)(b + 1);
		static const bool check = getenv("MDC_ARENACHECK") != nullptr;
		if(check) { const unsigned char* q = (const unsigned char*)(b + 1); size_t n2 = b->size - sizeof(Block); if(n2 > 256) n2 = 256;
			for(size_t i = sizeof(void*); i < n2; i++) if(q[i] != 0xDD) { fprintf(stderr, "arena %d: block of class %d (%llu bytes) at +%zu written after free (byte %zu = %02x)", a, c, (unsigned long long)b->size, (size_t)((char*)b - (char*)h), i, q[i]); fputc(10, stderr); abort(); } } }
	else {
		if(!commit(a, h->used + cap)) return nullptr;
		b = (Block*)((char*)h + h->used); h->used += cap;
	}
	b->cls = (uint32_t)c; b->size = cap;
	return b + 1;
}
void arena_free(int a, void* p) {
	Block* b = (Block*)p - 1; Header* h = head(a);
	static const bool check = getenv("MDC_ARENACHECK") != nullptr;
	if(check) { size_t n2 = b->size - sizeof(Block); if(n2 > 256) n2 = 256; std::memset(p, 0xDD, n2); }
	*(void**)p = h->freel[b->cls]; h->freel[b->cls] = b;
}
size_t arena_cap(void* p) { return ((Block*)p - 1)->size - sizeof(Block); }
void* route_alloc(size_t n) {
	if(tl_cur >= 0) { void* p = arena_alloc(tl_cur, n ? n : 1); if(p) return p; }
	return mi_malloc(n ? n : 1);
}
void route_free(void* p) {
	if(!p) return;
	int a = owner(p);
	if(a >= 0) arena_free(a, p); else mi_free(p);
}
}   // namespace

// ---- the engine's side ----
int mdc_arena_new() {
	std::lock_guard<std::mutex> lk(g_mx);
	if(!reserve()) return -1;
	for(int a = 0; a < MAXA; a++) if(!g_inuse[a]) {
		if(!commit(a, sizeof(Header) + 4096)) return -1;
		Header* h = head(a); std::memset(h, 0, sizeof(Header)); h->used = (sizeof(Header) + 15) / 16 * 16;
		g_inuse[a] = true; return a;
	}
	if(getenv("MDC_ARENALOG")) fprintf(stderr, "arenas: all %d in use\n", MAXA);
	return -1;
}
void mdc_arena_release(int a) { if(a < 0) return; std::lock_guard<std::mutex> lk(g_mx); g_inuse[a] = false; }
int mdc_arena_swap(int a) { int prev = tl_cur; tl_cur = a; return prev; }   // make region a current on this thread (-1: none)
size_t mdc_arena_used(int a) { return a < 0 ? 0 : head(a)->used; }
void mdc_arena_save(int a, std::vector<char>& out) { const char* b = (const char*)head(a); out.assign(b, b + head(a)->used); }
void mdc_arena_load(int a, const std::vector<char>& in) { std::memcpy(head(a), in.data(), in.size()); }

// ---- Lua (lauxlib's allocator, via mi_lua.h) ----
void* mdc_lua_realloc(void* p, size_t n) {
	if(!p) return route_alloc(n);
	int a = owner(p);
	if(a < 0) return mi_realloc(p, n);
	if(n <= arena_cap(p)) return p;
	void* q = arena_alloc(a, n); if(!q) return nullptr;
	std::memcpy(q, p, arena_cap(p)); arena_free(a, p);
	return q;
}
void mdc_lua_free(void* p) { route_free(p); }

// ---- C++ new / delete (replaces mimalloc-new-delete.h) ----
void* operator new(size_t n) { void* p = route_alloc(n); if(!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n) { void* p = route_alloc(n); if(!p) throw std::bad_alloc(); return p; }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return route_alloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return route_alloc(n); }
void operator delete(void* p) noexcept { route_free(p); }
void operator delete[](void* p) noexcept { route_free(p); }
void operator delete(void* p, size_t) noexcept { route_free(p); }
void operator delete[](void* p, size_t) noexcept { route_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { route_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { route_free(p); }
// Over-aligned objects (none in the core today) stay outside the regions.
void* operator new(size_t n, std::align_val_t al) { void* p = mi_malloc_aligned(n, (size_t)al); if(!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n, std::align_val_t al) { void* p = mi_malloc_aligned(n, (size_t)al); if(!p) throw std::bad_alloc(); return p; }
void operator delete(void* p, std::align_val_t) noexcept { mi_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { mi_free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { mi_free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { mi_free(p); }
