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
#include <atomic>
#include <chrono>
#include <memory>
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

#if defined(_MSC_VER)
#include <intrin.h>
static inline int __builtin_ctzll_compat(unsigned long long x) { unsigned long i; _BitScanForward64(&i, x); return (int)i; }
#else
static inline int __builtin_ctzll_compat(unsigned long long x) { return __builtin_ctzll(x); }
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
	// Write-watch: the OS records which pages get written, so a restore copies back only those (see mdc_snap_*).
	g_base = (char*)VirtualAlloc(nullptr, SLOT * MAXA, MEM_RESERVE | (getenv("MDC_NOWW") ? 0 : MEM_WRITE_WATCH), PAGE_NOACCESS);
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
std::atomic<int64_t> g_miLive{0}, g_escaped{0};
void* route_alloc(size_t n) {
	if(tl_cur >= 0) { void* p = arena_alloc(tl_cur, n ? n : 1); if(p) return p; g_escaped++; }
	g_miLive++;
	return mi_malloc(n ? n : 1);
}
void route_free(void* p) {
	if(!p) return;
	int a = owner(p);
	if(a >= 0) arena_free(a, p); else { g_miLive--; mi_free(p); }
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

// ---- saves that restore only what changed ----
// A save is a full copy of the region's used bytes. Each region keeps a stack of its live saves (a search saves at a
// branch point, explores, and the deeper saves are gone before it restores again). Pages written since the last check
// are credited to every save on the stack; a restore copies back only the pages written since that save. Without
// write-watch (Linux), every page counts as written.
namespace {
constexpr size_t PAGE = 4096;
// Pages are shared between saves: a save taken while an older one is live copies only the pages written since that
// one and points at the older save's copy of every other page.
// Page copies come from a pool (slabs of pages, one shared free list) with a reference count in each page: millions
// of 4 KB new/delete calls spread over short-lived search threads made the general allocator keep gigabytes it never
// handed back (14 GB after 20 searches).
struct PageBlk { std::atomic<int> ref; char data[PAGE]; };
using Page = PageBlk*;
thread_local std::vector<void*> tl_addrs; thread_local std::vector<PageBlk*> tl_got, tl_back;   // per-thread scratch (released in mdc_thread_done)
std::mutex g_poolMx; std::vector<PageBlk*> g_poolFree; std::atomic<int64_t> g_poolTotal{0}, g_liveSnaps{0};
void pool_take(std::vector<PageBlk*>& out, size_t n) {
	std::lock_guard<std::mutex> lk(g_poolMx);
	while(g_poolFree.size() < n) { const size_t SLAB = 256; PageBlk* slab = (PageBlk*)mi_malloc(sizeof(PageBlk) * SLAB); for(size_t i = 0; i < SLAB; i++) g_poolFree.push_back(slab + i); g_poolTotal += SLAB; }
	for(size_t i = 0; i < n; i++) { out.push_back(g_poolFree.back()); g_poolFree.pop_back(); }
}
void pool_give(const std::vector<PageBlk*>& v) { if(v.empty()) return; std::lock_guard<std::mutex> lk(g_poolMx); g_poolFree.insert(g_poolFree.end(), v.begin(), v.end()); }
struct Snap { int a; size_t n; std::vector<Page> pages; std::vector<uint64_t> dirty; };
inline bool is_dirty(const Snap* sp, size_t pg) { return pg >= sp->dirty.size() * 64 || (sp->dirty[pg >> 6] >> (pg & 63)) & 1; }
std::vector<Snap*> g_stack[MAXA];
std::atomic<uint64_t> st_push{0}, st_pushFull{0}, st_pagesCopied{0}, st_pagesShared{0}, st_restore{0}, st_restorePages{0}, st_collectNs{0};
inline void mark(Snap* sp, size_t pg) { if(pg < sp->dirty.size() * 64) sp->dirty[pg >> 6] |= uint64_t(1) << (pg & 63); }
void collect(int a) {
	auto t0 = std::chrono::steady_clock::now();
	struct Done { std::chrono::steady_clock::time_point t0; ~Done() { st_collectNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count(); } } done{t0};
#ifdef _WIN32
	auto& addrs = tl_addrs;
	size_t pages = g_committed[a] / PAGE; if(addrs.size() < pages) addrs.resize(pages);
	ULONG_PTR count = pages; DWORD gran = 0;
	char* base = g_base + SLOT * a;
	// Only the newest save is marked; it hands its marks down to the one below when it goes (mdc_snap_pop): a page
	// written since a newer save was also written since every older one.
	static const bool noWW = getenv("MDC_NOWW") != nullptr;
	if(noWW) { if(!g_stack[a].empty()) std::fill(g_stack[a].back()->dirty.begin(), g_stack[a].back()->dirty.end(), ~uint64_t(0)); return; }
	if(g_stack[a].empty()) { ResetWriteWatch(base, g_committed[a]); return; }
	Snap* top = g_stack[a].back();
	if(GetWriteWatch(WRITE_WATCH_FLAG_RESET, base, g_committed[a], addrs.data(), &count, &gran) != 0) { std::fill(top->dirty.begin(), top->dirty.end(), ~uint64_t(0)); return; }
	for(ULONG_PTR i = 0; i < count; i++) mark(top, size_t((char*)addrs[i] - base) / PAGE);
#else
	if(!g_stack[a].empty()) std::fill(g_stack[a].back()->dirty.begin(), g_stack[a].back()->dirty.end(), ~uint64_t(0));
#endif
}
void forget_writes(int a) {
#ifdef _WIN32
	static const bool noWW = getenv("MDC_NOWW") != nullptr;
	if(!noWW) ResetWriteWatch(g_base + SLOT * a, g_committed[a]);
#else
	(void)a;
#endif
}
}   // namespace
void* mdc_snap_push(int a) {
	collect(a);
	const char* b = (const char*)head(a); const size_t n = head(a)->used, np = (n + PAGE - 1) / PAGE;
	Snap* parent = g_stack[a].empty() ? nullptr : g_stack[a].back();
	st_push++; if(!parent) st_pushFull++;
	Snap* sp = new Snap{a, n, std::vector<Page>(np, nullptr), std::vector<uint64_t>((np + 64) / 64, 0)};
	size_t fresh = 0;
	for(size_t pg = 0; pg < np; pg++) {
		if(parent && pg < parent->pages.size() && !is_dirty(parent, pg)) { sp->pages[pg] = parent->pages[pg]; sp->pages[pg]->ref.fetch_add(1, std::memory_order_relaxed); st_pagesShared++; }
		else fresh++;
	}
	auto& got = tl_got; got.clear(); pool_take(got, fresh);
	size_t k = 0;
	for(size_t pg = 0; pg < np; pg++) {
		if(sp->pages[pg]) continue;
		st_pagesCopied++;
		PageBlk* q = got[k++]; q->ref.store(1, std::memory_order_relaxed);
		std::memcpy(q->data, b + pg * PAGE, std::min(PAGE, n - pg * PAGE)); sp->pages[pg] = q;
	}
	g_stack[a].push_back(sp); g_liveSnaps++;
	return sp;
}
void mdc_snap_restore(void* h) {
	Snap* sp = (Snap*)h; int a = sp->a;
	st_restore++;
	collect(a);
	char* base = (char*)head(a); const size_t n = sp->n;
	for(size_t w = 0; w < sp->dirty.size(); w++) for(uint64_t bits = sp->dirty[w]; bits; bits &= bits - 1) {
		size_t pg = w * 64 + (size_t)__builtin_ctzll_compat(bits), off = pg * PAGE;
		if(off >= n) continue;
		st_restorePages++;
		std::memcpy(base + off, sp->pages[pg]->data, std::min(PAGE, n - off));
	}
	std::fill(sp->dirty.begin(), sp->dirty.end(), 0);
	forget_writes(a);   // the copies above put back this save's bytes: nothing written since it now
}
void mdc_snap_pop(void* h) {
	Snap* sp = (Snap*)h; auto& st = g_stack[sp->a];
	if(st.size() > 1 && st.back() == sp) collect(sp->a);   // marks up to now, before handing them down (the bottom save has no one to hand them to)
	for(size_t i = st.size(); i-- > 0;) if(st[i] == sp) {
		if(i > 0) { Snap* below = st[i - 1];   // what was written since this save was written since the one below too
			for(size_t w = 0; w < sp->dirty.size(); w++) if(sp->dirty[w]) for(uint64_t bits = sp->dirty[w]; bits; bits &= bits - 1) mark(below, w * 64 + (size_t)__builtin_ctzll_compat(bits));
			if(sp->dirty.size() * 64 > below->dirty.size() * 64) {}   // pages past the older save's size don't matter to it
		}
		st.erase(st.begin() + i); break;
	}
	auto& back = tl_back; back.clear();
	for(PageBlk* q : sp->pages) if(q && q->ref.fetch_sub(1, std::memory_order_acq_rel) == 1) back.push_back(q);
	pool_give(back);
	delete sp; g_liveSnaps--;
}

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

// MDC_PROF: what the saves cost (counts since the last call).
void mdc_snap_stats(char* out, size_t cap) {
	size_t freeN; { std::lock_guard<std::mutex> lk(g_poolMx); freeN = g_poolFree.size(); }
	int n0 = snprintf(out, cap, "outside allocations live %lld (made while a duel was current: %lld); live saves %lld, page pool %lld pages (%lld free); ", (long long)g_miLive.load(), (long long)g_escaped.load(), (long long)g_liveSnaps.load(), (long long)g_poolTotal.load(), (long long)freeN);
	if(n0 < 0 || (size_t)n0 >= cap) return; out += n0; cap -= n0;
	snprintf(out, cap, "pushes %llu (%llu with nothing to share from), pages copied %llu / shared %llu; restores %llu (%llu pages); write-watch checks %.1f thread-s",
		(unsigned long long)st_push.exchange(0), (unsigned long long)st_pushFull.exchange(0), (unsigned long long)st_pagesCopied.exchange(0), (unsigned long long)st_pagesShared.exchange(0),
		(unsigned long long)st_restore.exchange(0), (unsigned long long)st_restorePages.exchange(0), st_collectNs.exchange(0) / 1e9);
}

// After a search: hand memory cached by the search's (now finished) threads back to the OS.
void mdc_mem_collect() { mi_collect(true); }
void mdc_arena_stats(int& inUse, size_t& committed) { std::lock_guard<std::mutex> lk(g_mx); inUse = 0; committed = 0; for(int a = 0; a < MAXA; a++) { if(g_inuse[a]) inUse++; committed += g_committed[a]; } }

// End of a search thread: the statically linked allocator doesn't always see Windows threads exit, and threads it never
// hears about keep their memory (1,200 of 3,100 search threads after 10 searches; private bytes +250 MB a search).
void mdc_thread_done() {
	std::vector<void*>().swap(tl_addrs); std::vector<PageBlk*>().swap(tl_got); std::vector<PageBlk*>().swap(tl_back);
	mi_thread_done();
}
