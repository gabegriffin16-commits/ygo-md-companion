// combo-engine: reuse a finished duel instead of building a new one.
// Copyright (C) 2026 Gabe Griffin and contributors. SPDX-License-Identifier: AGPL-3.0-or-later
//
// Building a duel is mostly loading Lua card scripts. A duel that's done can be wiped back to an empty field
// and set up again with every script still loaded. This does what the engine's own field reload
// (duel::clear) does, plus what that leaves behind for a reuse loop: the old cards' Lua references,
// unfinished coroutines, queued messages and the random number generator (reseeded so a replay of the
// same moves always plays out the same way).
// Standard headers first: the engine's headers define a yield() macro that breaks <thread> on MSVC.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "duel.h"
#include "card.h"
#include "effect.h"
#include "field.h"
#include "group.h"
#include "interpreter.h"
#include "ocgapi.h"
extern "C++" {
#include "lua.h"
#include "lauxlib.h"
}

// duel keeps its message queue and RNG private. The standard "explicit instantiation may name private members"
// rule gives this file a pointer to those two members without changing the engine.
template<typename Tag, typename Tag::type M> struct Grab { friend typename Tag::type get(Tag) { return M; } };
struct DuelRandom { using type = RNG::Xoshiro256StarStar duel::*; friend type get(DuelRandom); };
struct DuelMessages { using type = std::deque<duel::duel_message> duel::*; friend type get(DuelMessages); };
template struct Grab<DuelRandom, &duel::random>;
template struct Grab<DuelMessages, &duel::messages>;

static std::mutex g_base_mx;
static std::unordered_map<duel*, std::unordered_set<lua_Integer>> g_base;
// Called once per duel right after the rules scripts load: the registry entries that belong to the duel itself.
void mdc_mark_duel(OCG_Duel h) {
	auto* d = static_cast<duel*>(h);
	lua_State* L = d->lua->lua_state;
	std::unordered_set<lua_Integer> keys;
	lua_pushnil(L);
	while(lua_next(L, LUA_REGISTRYINDEX)) { if(lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) keys.insert(lua_tointeger(L, -2)); lua_pop(L, 1); }
	std::lock_guard<std::mutex> lk(g_base_mx);
	g_base[d] = std::move(keys);
}
void mdc_forget_duel(OCG_Duel h) { std::lock_guard<std::mutex> lk(g_base_mx); g_base.erase(static_cast<duel*>(h)); }

bool mdc_reset_duel(OCG_Duel h, const OCG_DuelOptions& o) {
	auto* d = static_cast<duel*>(h);
	if(!d || !d->lua || !d->game_field) return false;
	interpreter* lua = d->lua;
	lua_State* L = lua->lua_state;
	for(auto& c : lua->coroutines) luaL_unref(L, LUA_REGISTRYINDEX, c.second.second);
	lua->coroutines.clear();
	for(card* pc : d->cards) {
		if(pc->ref_handle) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, pc->ref_handle);
			auto** lobj = static_cast<lua_obj**>(lua_touserdata(L, -1));
			if(lobj) *lobj = &lua->deleted;
			lua_pop(L, 1);
			luaL_unref(L, LUA_REGISTRYINDEX, pc->ref_handle);
			pc->ref_handle = 0;
		}
		delete pc;
	}
	d->cards.clear();
	d->assumes.clear();
	for(effect* pe : d->effects) { lua->unregister_effect(pe); delete pe; }
	d->effects.clear();
	d->uncopy.clear();
	delete d->game_field;
	for(group* pg : d->groups) { pg->container.clear(); pg->is_iterator_dirty = true; }
	lua->params.clear();
	lua->current_state = L;
	lua->call_depth = 0;
	lua->no_action = 0;
	lua_settop(L, 0);
	// Everything else the old game left in the Lua registry (custom activity counters, unique-on-field checks...):
	// release every reference that wasn't there when the duel was first built.
	{
		std::vector<lua_Integer> stale;
		{
			std::lock_guard<std::mutex> lk(g_base_mx);
			auto it = g_base.find(d);
			if(it != g_base.end()) {
				lua_pushnil(L);
				while(lua_next(L, LUA_REGISTRYINDEX)) {
					if(lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2) && lua_type(L, -1) != LUA_TNUMBER) {
						lua_Integer k = lua_tointeger(L, -2);
						if(!it->second.count(k)) stale.push_back(k);
					}
					lua_pop(L, 1);
				}
			}
		}
		for(lua_Integer k : stale) luaL_unref(L, LUA_REGISTRYINDEX, (int)k);
	}
	// Garbage from the old game: a quick incremental step usually, and a full collection once the heap has
	// grown a quarter past its size after the last one, so a duel reused thousands of times stays fast.
	{
		static std::mutex mx; static std::unordered_map<duel*, int> base;
		int kb = lua_gc(L, LUA_GCCOUNT, 0);
		int& b = [&]() -> int& { std::lock_guard<std::mutex> lk(mx); return base[d]; }();   // (a new duel at a reused address starts over)
		if(!b || kb > b + b / 4) { lua_gc(L, LUA_GCCOLLECT, 0); b = lua_gc(L, LUA_GCCOUNT, 0); }
		else lua_gc(L, LUA_GCSTEP, 0);
	}
	(d->*get(DuelMessages())).clear();
	d->buff.clear();
	d->*get(DuelRandom()) = RNG::Xoshiro256StarStar({ o.seed[0], o.seed[1], o.seed[2], o.seed[3] });
	d->game_field = new field(d, o);
	d->game_field->temp_card = d->new_card(0);
	// Scripts mark "global effects already registered" on their card tables; clear that so they register again.
	lua_pushglobaltable(L);
	lua_pushnil(L);
	while(lua_next(L, -2) != 0) {
		if(lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TTABLE) {
			const char* k = lua_tostring(L, -2);
			if(k[0] == 'c' && k[1] >= '0' && k[1] <= '9') {
				for(const char* f : {"global_check", "check"}) {   // only the true/false flags: Ash Blossom's s.check is a function
					lua_getfield(L, -1, f);
					bool flag = lua_type(L, -1) == LUA_TBOOLEAN;
					lua_pop(L, 1);
					if(flag) { lua_pushnil(L); lua_setfield(L, -2, f); }
				}
			}
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return true;
}
