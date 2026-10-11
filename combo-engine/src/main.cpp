// combo-engine: finds turn-one combo lines for a Yu-Gi-Oh! deck by playing them out in the real rules engine
// (edo9300/ygopro-core, the engine behind EDOPro) with the ProjectIgnis card scripts and card database.
//
// Copyright (C) 2026 Gabe Griffin and contributors.
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
// General Public License as published by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version. See LICENSE. Source: https://github.com/gabegriffin16-commits/ygo-md-companion
//
// Protocol: one JSON object per line on stdin, one JSON object per line on stdout.
//   {"id":1,"cmd":"init","cdb":"cards.cdb","scripts":"CardScripts.zip"}       -> {"id":1,"ready":true,"cards":N}
//   {"id":2,"cmd":"search","deck":[...],"extra":[...],"hand":[...],
//     "maxActions":8,"timeMs":20000,"threads":2,"targets":[...],"top":12}    -> {"id":2,"progress":{...}} ... {"id":2,"done":true,...}
//   {"cmd":"stop"}                                                              -> ends the running search early (results still sent)
//   {"cmd":"quit"}

#include "ocgapi.h"
#include "ocgapi_constants.h"
extern "C++" {
#include "lua.h"
#include "lauxlib.h"
}
#include "sqlite3.h"
#include "miniz.h"
#include "json.hpp"
#include "evaluate.h"
#ifdef MDC_MIMALLOC
#include <mimalloc-new-delete.h>   // the rules engine's own (C++) allocations go through mimalloc too
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------------ output
static std::mutex g_out;
static void emit(const json& j) {
	std::lock_guard<std::mutex> lk(g_out);
	std::cout << j.dump() << "\n";
	std::cout.flush();
}

// ------------------------------------------------------------------ card database (BabelCDB cards.cdb)
struct CardRow {
	uint32_t alias = 0, type = 0, level = 0, attribute = 0;
	uint64_t race = 0;
	int32_t atk = 0, def = 0;
	std::vector<uint16_t> setcodes;
	std::string name, lname;   // lname: lowercase, for matching what a reviver asks for
	bool centerAware = false;  // its text cares about the center Main Monster Zone
	bool endPhase = false;     // it does something "during the End Phase"
	bool backAtEnd = false;    // it banishes itself "until the End Phase": still ours once the turn ends
	bool needsChain = false;   // it acts "in response to" an activation (Zalen) or "when you activate" one (Gulamel): used on top of our own
	std::vector<std::string> strs;
	CardEval ev;   // how much it adds to an end board, read from its text (src/evaluate.h)
	std::string desc;
};
static std::unordered_map<uint32_t, CardRow> g_cards;

static bool load_cdb(const std::string& path, std::string& err) {
	sqlite3* db = nullptr;
	if(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) { err = "can't open card database"; if(db) sqlite3_close(db); return false; }
	sqlite3_stmt* st = nullptr;
	const char* q = "select d.id,d.alias,d.setcode,d.type,d.atk,d.def,d.level,d.race,d.attribute,t.name,"
		"t.str1,t.str2,t.str3,t.str4,t.str5,t.str6,t.str7,t.str8,t.str9,t.str10,t.str11,t.str12,t.str13,t.str14,t.str15,t.str16,t.desc "
		"from datas d left join texts t on t.id=d.id";
	if(sqlite3_prepare_v2(db, q, -1, &st, nullptr) != SQLITE_OK) { err = sqlite3_errmsg(db); sqlite3_close(db); return false; }
	while(sqlite3_step(st) == SQLITE_ROW) {
		uint32_t id = (uint32_t)sqlite3_column_int64(st, 0);
		CardRow r;
		r.alias = (uint32_t)sqlite3_column_int64(st, 1);
		uint64_t sc = (uint64_t)sqlite3_column_int64(st, 2);
		for(int i = 0; i < 4; i++) { uint16_t s = (sc >> (16 * i)) & 0xffff; if(s) r.setcodes.push_back(s); }
		r.setcodes.push_back(0);
		r.type = (uint32_t)sqlite3_column_int64(st, 3);
		r.atk = (int32_t)sqlite3_column_int64(st, 4);
		r.def = (int32_t)sqlite3_column_int64(st, 5);
		r.level = (uint32_t)sqlite3_column_int64(st, 6);
		r.race = (uint64_t)sqlite3_column_int64(st, 7);
		r.attribute = (uint32_t)sqlite3_column_int64(st, 8);
		const unsigned char* nm = sqlite3_column_text(st, 9);
		if(nm) { r.name = (const char*)nm; r.lname = evalx::lower(r.name); }
		for(int i = 0; i < 16; i++) { const unsigned char* s = sqlite3_column_text(st, 10 + i); r.strs.push_back(s ? (const char*)s : ""); }
		{ const unsigned char* dt = sqlite3_column_text(st, 26); r.desc = dt ? (const char*)dt : ""; r.ev = evalx::evaluate(r.desc, r.type); { std::string ld = evalx::lower(r.desc); r.centerAware = ld.find("center main monster zone") != std::string::npos; r.endPhase = ld.find("end phase") != std::string::npos;
			r.backAtEnd = ld.find("banish this card (until the end phase)") != std::string::npos || ld.find("banish this card until the end phase") != std::string::npos;
			r.needsChain = ld.find("in response to") != std::string::npos || ld.find("when you activate") != std::string::npos; } }
		r.ev.code = id;
		g_cards[id] = std::move(r);
	}
	sqlite3_finalize(st);
	sqlite3_close(db);
	return true;
}
static std::string card_name(uint32_t code) {
	auto it = g_cards.find(code);
	return it == g_cards.end() || it->second.name.empty() ? std::to_string(code) : it->second.name;
}
// Effect description id (code<<20 | index) -> the card's own text for that effect, as EDOPro shows it.
static std::string desc_text(uint64_t d) {
	uint32_t code = (uint32_t)(d >> 20), idx = (uint32_t)(d & 0xfffff);
	auto it = g_cards.find(code);
	if(it != g_cards.end() && idx < 16 && !it->second.strs[idx].empty()) return it->second.strs[idx];
	return "";
}

static void card_reader(void*, uint32_t code, OCG_CardData* out) {
	static uint16_t zero = 0;
	std::memset(out, 0, sizeof(*out));
	out->code = code;
	out->setcodes = &zero;
	auto it = g_cards.find(code);
	if(it == g_cards.end()) return;
	const CardRow& r = it->second;
	out->alias = r.alias;
	out->setcodes = const_cast<uint16_t*>(r.setcodes.data());
	out->type = r.type;
	out->level = r.level & 0xff;
	out->lscale = (r.level >> 24) & 0xff;
	out->rscale = (r.level >> 16) & 0xff;
	out->attribute = r.attribute;
	out->race = r.race;
	out->attack = r.atk;
	if(r.type & TYPE_LINK) { out->defense = 0; out->link_marker = (uint32_t)r.def; }
	else { out->defense = r.def; out->link_marker = 0; }
}
static void card_reader_done(void*, OCG_CardData*) {}

// ------------------------------------------------------------------ scripts (CardScripts zip or folder) + bytecode cache
static mz_zip_archive g_zip;
static std::vector<char> g_zip_data;
static bool g_zip_open = false;
static std::string g_script_dir;
static std::unordered_map<std::string, uint32_t> g_zip_index;   // file name -> zip entry
static std::unordered_map<std::string, std::string> g_dir_index; // file name -> path
static std::unordered_map<std::string, std::string> g_bytecode;  // file name -> compiled chunk
static std::mutex g_script_mx;

static int score_folder(const std::string& path) {
	// Prefer official scripts over pre-release / unofficial when a name exists in several folders.
	if(path.find("/official/") != std::string::npos) return 0;
	if(path.find("/pre-release/") != std::string::npos) return 2;
	if(path.find("/unofficial/") != std::string::npos) return 3;
	if(path.find("/pre-errata/") != std::string::npos || path.find("/goat/") != std::string::npos || path.find("/rush/") != std::string::npos || path.find("/skill/") != std::string::npos) return 9;
	return 1;
}
static std::string base_name(const std::string& p) { size_t s = p.find_last_of("/\\"); return s == std::string::npos ? p : p.substr(s + 1); }

static bool open_scripts(const std::string& path, std::string& err) {
	std::map<std::string, int> best;
	if(path.size() > 4 && path.substr(path.size() - 4) == ".zip") {
		std::memset(&g_zip, 0, sizeof(g_zip));
		// Read the zip into memory through a UTF-8 aware path (Windows user folders can have non-ASCII names).
		std::ifstream zf(std::filesystem::u8path(path), std::ios::binary);
		if(!zf) { err = "can't open scripts zip"; return false; }
		g_zip_data.assign(std::istreambuf_iterator<char>(zf), std::istreambuf_iterator<char>());
		if(!mz_zip_reader_init_mem(&g_zip, g_zip_data.data(), g_zip_data.size(), 0)) { err = "scripts zip is damaged"; return false; }
		g_zip_open = true;
		uint32_t n = mz_zip_reader_get_num_files(&g_zip);
		for(uint32_t i = 0; i < n; i++) {
			char fn[512];
			mz_zip_reader_get_filename(&g_zip, i, fn, sizeof(fn));
			std::string f = fn;
			if(f.size() < 5 || f.substr(f.size() - 4) != ".lua") continue;
			std::string b = base_name(f);
			int sc = score_folder(f);
			auto it = best.find(b);
			if(it == best.end() || sc < it->second) { best[b] = sc; g_zip_index[b] = i; }
		}
		return !g_zip_index.empty() || (err = "no scripts in zip", false);
	}
	g_script_dir = path;  // a plain folder: scripts are looked up on demand
	return true;
}
static bool read_script_source(const std::string& name, std::string& out) {
	if(g_zip_open) {
		auto it = g_zip_index.find(name);
		if(it == g_zip_index.end()) return false;
		size_t sz = 0;
		void* p = mz_zip_reader_extract_to_heap(&g_zip, it->second, &sz, 0);
		if(!p) return false;
		out.assign((const char*)p, sz);
		mz_free(p);
		return true;
	}
	const char* subs[] = {"official/", "", "pre-release/", "unofficial/"};
	for(const char* s : subs) {
		std::ifstream f(std::filesystem::u8path(g_script_dir + "/" + s + name), std::ios::binary);
		if(f) { std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; }
	}
	return false;
}
static int dump_writer(lua_State*, const void* p, size_t sz, void* ud) { ((std::string*)ud)->append((const char*)p, sz); return 0; }
// Compile each script once and keep its bytecode: creating a duel loads a few dozen scripts, and parsing
// Lua source every time is most of the cost of a replay.
static const std::string* get_script(const std::string& name) {
	std::lock_guard<std::mutex> lk(g_script_mx);
	auto it = g_bytecode.find(name);
	if(it != g_bytecode.end()) return it->second.empty() ? nullptr : &it->second;
	std::string src, bc;
	if(read_script_source(name, src)) {
		lua_State* L = luaL_newstate();
		if(luaL_loadbuffer(L, src.data(), src.size(), ("@" + name).c_str()) == LUA_OK) lua_dump(L, dump_writer, &bc, 1);
		else bc = src;   // let the engine report the error
		lua_close(L);
	}
	auto& slot = g_bytecode[name];
	slot = std::move(bc);
	return slot.empty() ? nullptr : &slot;
}
static int script_reader(void*, OCG_Duel duel, const char* name) {
	const std::string* b = get_script(base_name(name));
	if(!b) return 0;
	return OCG_LoadScript(duel, b->data(), (uint32_t)b->size(), name);
}
static void log_handler(void*, const char*, int) {}

// ------------------------------------------------------------------ message reading
struct Rd {
	const uint8_t* b; size_t n, o = 0;
	Rd(const uint8_t* b_, size_t n_, size_t o_ = 0) : b(b_), n(n_), o(o_) {}
	bool ok(size_t k) const { return o + k <= n; }
	uint8_t u8() { uint8_t v = ok(1) ? b[o] : 0; o += 1; return v; }
	uint16_t u16() { uint16_t v = 0; if(ok(2)) std::memcpy(&v, b + o, 2); o += 2; return v; }
	uint32_t u32() { uint32_t v = 0; if(ok(4)) std::memcpy(&v, b + o, 4); o += 4; return v; }
	int32_t i32() { int32_t v = 0; if(ok(4)) std::memcpy(&v, b + o, 4); o += 4; return v; }
	uint64_t u64() { uint64_t v = 0; if(ok(8)) std::memcpy(&v, b + o, 8); o += 8; return v; }
};
struct Loc { uint8_t con = 0, loc = 0; uint32_t seq = 0, pos = 0; };
static Loc rloc(Rd& r) { Loc l; l.con = r.u8(); l.loc = r.u8(); l.seq = r.u32(); l.pos = r.u32(); return l; }

struct IdleItem { uint32_t code; uint64_t desc = 0; };
// Which copy of a card: code + controller + place (+ zone, on the field). Tells a card we had when their turn started
// from another copy that came out during it.
static uint64_t where_key(uint32_t code, uint8_t con, uint32_t loc, uint32_t seq) {
	bool zone = loc == LOCATION_MZONE || loc == LOCATION_SZONE;
	return ((uint64_t)code << 32) | ((uint64_t)(con & 1) << 31) | ((uint64_t)(loc & 0x7fff) << 16) | (zone ? (seq & 0xffff) : 0xffff);
}
struct Prompt {
	int type = 0; uint8_t player = 0;
	std::vector<IdleItem> summon, spsummon, activate, sset;   // sset: Spells/Traps that can be Set
	bool to_ep = false;
	uint32_t code = 0; uint64_t desc = 0;
	std::vector<uint64_t> options;
	uint8_t cancelable = 0, finishable = 0; uint32_t mn = 0, mx = 0;
	std::vector<uint32_t> cards;         // SELECT_CARD / TRIBUTE / UNSELECT (select list) / SUM cards
	std::vector<uint64_t> ckeys;         // UNSELECT: where each card is (for picking in a fixed order, see pick_order)
	std::vector<uint8_t> ccon;           // SELECT_CARD / UNSELECT: who controls each card
	std::vector<uint32_t> cardParam;     // SUM: per-card value
	std::vector<uint32_t> mustParam; uint32_t acc = 0;
	std::vector<std::pair<uint32_t, uint64_t>> chains;
	std::vector<uint64_t> chainAt; uint64_t codeAt = 0;   // CHAIN options / EFFECTYN card: which copy (see where_key)
	bool trig = false;                   // CHAIN: these are triggers to pick from
	bool to_bp = false, to_m2 = false;   // IDLECMD: can go to battle; BATTLECMD: can go to Main Phase 2 (to_ep: to the End Phase)
	std::vector<std::pair<uint32_t, bool>> attackers;   // BATTLECMD: monsters that can attack (code, can attack directly)
	uint8_t forced = 0;
	uint8_t count = 0; uint32_t flag = 0; uint8_t positions = 0;
	uint64_t available = 0;
	uint64_t hint = 0;                   // the "Select a card to discard / add / banish..." message shown with it
};
static bool is_prompt(int t) {
	switch(t) {
	case MSG_SELECT_BATTLECMD: case MSG_SELECT_IDLECMD: case MSG_SELECT_EFFECTYN: case MSG_SELECT_YESNO: case MSG_SELECT_OPTION:
	case MSG_SELECT_CARD: case MSG_SELECT_CHAIN: case MSG_SELECT_PLACE: case MSG_SELECT_POSITION: case MSG_SELECT_TRIBUTE:
	case MSG_SORT_CHAIN: case MSG_SELECT_COUNTER: case MSG_SELECT_SUM: case MSG_SELECT_DISFIELD: case MSG_SORT_CARD:
	case MSG_SELECT_UNSELECT_CARD: case MSG_ANNOUNCE_RACE: case MSG_ANNOUNCE_ATTRIB: case MSG_ANNOUNCE_CARD: case MSG_ANNOUNCE_NUMBER:
	case 132: return true;
	}
	return false;
}
static Prompt parse_prompt(int t, const uint8_t* body, size_t n) {
	Rd r(body, n);
	Prompt m; m.type = t; m.player = r.u8();
	switch(t) {
	case MSG_SELECT_IDLECMD: {
		auto lst = [&](std::vector<IdleItem>* into, bool seq8) {
			uint32_t k = r.u32();
			for(uint32_t i = 0; i < k; i++) { IdleItem it; it.code = r.u32(); r.u8(); r.u8(); if(seq8) r.u8(); else r.u32(); if(into) into->push_back(it); }
		};
		lst(&m.summon, false); lst(&m.spsummon, false); lst(nullptr, true); lst(nullptr, false); lst(&m.sset, false);
		uint32_t k = r.u32();
		for(uint32_t i = 0; i < k; i++) { IdleItem it; it.code = r.u32(); r.u8(); r.u8(); r.u32(); it.desc = r.u64(); r.u8(); m.activate.push_back(it); }
		m.to_bp = r.u8() != 0; m.to_ep = r.u8() != 0; r.u8();
		break;
	}
	case MSG_SELECT_BATTLECMD: {
		uint32_t k = r.u32();
		for(uint32_t i = 0; i < k; i++) { r.u32(); r.u8(); r.u8(); r.u32(); r.u64(); r.u8(); }
		k = r.u32();
		for(uint32_t i = 0; i < k; i++) { uint32_t c = r.u32(); r.u8(); r.u8(); r.u8(); bool direct = r.u8() != 0; m.attackers.push_back({c, direct}); }
		m.to_m2 = r.u8() != 0; m.to_ep = r.u8() != 0;
		break;
	}
	case MSG_SELECT_EFFECTYN: { m.code = r.u32(); Loc l = rloc(r); m.codeAt = where_key(m.code, l.con, l.loc, l.seq); m.desc = r.u64(); break; }
	case MSG_SELECT_YESNO: m.desc = r.u64(); break;
	case MSG_SELECT_OPTION: { uint8_t k = r.u8(); for(int i = 0; i < k; i++) m.options.push_back(r.u64()); break; }
	case MSG_SELECT_CARD: case MSG_SELECT_TRIBUTE: {
		m.cancelable = r.u8(); m.mn = r.u32(); m.mx = r.u32();
		uint32_t k = r.u32();
		for(uint32_t i = 0; i < k; i++) {
			if(t == MSG_SELECT_CARD) { m.cards.push_back(r.u32()); m.ccon.push_back(rloc(r).con); }
			else { m.cards.push_back(r.u32()); r.u8(); r.u8(); r.u32(); r.u8(); }
		}
		break;
	}
	case MSG_SELECT_UNSELECT_CARD: {
		m.finishable = r.u8(); m.cancelable = r.u8(); m.mn = r.u32(); m.mx = r.u32();
		auto key = [](uint32_t c, const Loc& l) { return ((uint64_t)l.con << 56) | ((uint64_t)l.loc << 48) | ((uint64_t)(l.seq & 0xffff) << 32) | c; };
		uint32_t k = r.u32(); for(uint32_t i = 0; i < k; i++) { uint32_t c = r.u32(); Loc l = rloc(r); m.cards.push_back(c); m.ckeys.push_back(key(c, l)); m.ccon.push_back(l.con); }
		k = r.u32(); for(uint32_t i = 0; i < k; i++) { r.u32(); rloc(r); }
		break;
	}
	case MSG_SELECT_CHAIN: {
		m.trig = r.u8() == 0x7f; m.forced = r.u8(); r.u32(); r.u32();   // 0x7f: choosing among triggers (not a Quick Effect window)
		uint32_t k = r.u32();
		for(uint32_t i = 0; i < k; i++) { uint32_t c = r.u32(); Loc l = rloc(r); uint64_t d = r.u64(); r.u8(); m.chains.push_back({c, d}); m.chainAt.push_back(where_key(c, l.con, l.loc, l.seq)); }
		break;
	}
	case MSG_SELECT_PLACE: case MSG_SELECT_DISFIELD: m.count = r.u8(); m.flag = r.u32(); break;
	case MSG_SELECT_POSITION: m.code = r.u32(); m.positions = r.u8(); break;
	case MSG_SELECT_SUM: {
		r.u8(); m.acc = r.u32(); m.mn = r.u32(); m.mx = r.u32();
		uint32_t k = r.u32(); for(uint32_t i = 0; i < k; i++) { r.u32(); rloc(r); m.mustParam.push_back(r.u32()); }
		k = r.u32(); for(uint32_t i = 0; i < k; i++) { m.cards.push_back(r.u32()); rloc(r); m.cardParam.push_back(r.u32()); }
		break;
	}
	case MSG_SORT_CARD: case MSG_SORT_CHAIN: { uint32_t k = r.u32(); for(uint32_t i = 0; i < k; i++) { m.cards.push_back(r.u32()); r.u8(); r.u32(); r.u32(); } break; }
	case MSG_ANNOUNCE_NUMBER: case MSG_ANNOUNCE_CARD: { uint8_t k = r.u8(); for(int i = 0; i < k; i++) m.options.push_back(r.u64()); break; }
	case MSG_ANNOUNCE_RACE: m.count = r.u8(); m.available = r.u64(); break;
	case MSG_ANNOUNCE_ATTRIB: m.count = r.u8(); m.available = r.u32(); break;
	}
	return m;
}

// ------------------------------------------------------------------ responses
using Bytes = std::string;
static Bytes p32(int32_t v) { return Bytes((const char*)&v, 4); }
static Bytes pu32(uint32_t v) { return Bytes((const char*)&v, 4); }
static Bytes r_idle(int t, int s) { return p32((s << 16) | t); }
static Bytes r_cards(const std::vector<uint32_t>& idx) { Bytes b = p32(0) + pu32((uint32_t)idx.size()); for(uint32_t i : idx) b += pu32(i); return b; }
static Bytes r_unselect(int i) { return i >= 0 ? p32(1) + p32(i) : p32(-1); }
static Bytes r_place(const Prompt& m) {
	uint32_t flag = m.flag; Bytes out;
	for(int c = 0; c < m.count; c++) {
		bool done = false;
		for(int side = 0; side < 2 && !done; side++) {
			uint32_t off = side ? 16 : 0; uint8_t pl = side ? (uint8_t)(1 - m.player) : m.player;
			for(int seq = 0; seq < 7 && !done; seq++) if(!(flag & (1u << (seq + off)))) { out += (char)pl; out += (char)LOCATION_MZONE; out += (char)seq; flag |= 1u << (seq + off); done = true; }
			for(int seq = 0; seq < 8 && !done; seq++) if(!(flag & (1u << (seq + 8 + off)))) { out += (char)pl; out += (char)LOCATION_SZONE; out += (char)seq; flag |= 1u << (seq + 8 + off); done = true; }
		}
	}
	return out;
}
static Bytes r_position(const Prompt& m) {
	for(int p : {POS_FACEUP_ATTACK, POS_FACEUP_DEFENSE, POS_FACEDOWN_DEFENSE, POS_FACEDOWN_ATTACK}) if(m.positions & p) return p32(p);
	return p32(POS_FACEUP_ATTACK);
}

// ------------------------------------------------------------------ duel
struct Setup { std::vector<uint32_t> deck, extra, hand, oppHand, oppDeck, oppExtra;   // oppDeck/oppExtra: the opponent's-turn simulation
	std::vector<uint32_t> preField, preBack, preGy, preBanished; std::vector<int> preZones; };   // a board set straight onto the field (simboard)
bool mdc_reset_duel(OCG_Duel h, const OCG_DuelOptions& o);
void mdc_mark_duel(OCG_Duel h); void mdc_forget_duel(OCG_Duel h);
static std::mutex g_pool_mx; static std::vector<OCG_Duel> g_pool; static bool g_reuse = true;
static std::unordered_map<OCG_Duel, int> g_uses;   // how many times each pooled duel has been reused
static void pool_flush() { std::lock_guard<std::mutex> lk(g_pool_mx); for(OCG_Duel d : g_pool) { mdc_forget_duel(d); OCG_DestroyDuel(d); } g_pool.clear(); g_uses.clear(); }
static const uint32_t DUMMY = 46986414;   // opponent's deck: Dark Magician x5, they never act on our turn
// Draws are random in a real duel. Cards drawn during a line come off a stack of these vanilla Spiral Serpents
// on top of the Deck, so a line never counts on drawing a specific card (and boards don't show them).
static const uint32_t BLANK = 32626733;
static const int BLANKS = 12;
// Whether zone choices are worth branching on: only for decks whose cards care about zones (set per search).
static std::atomic<bool> g_zones{false};

struct Duel {
	OCG_Duel h = nullptr;
	uint64_t lastHint[2] = {0, 0};
	int turnPlayer = 0, turns = 0, phase = 0;   // whose turn it is (how many turns have started) and the phase
	std::vector<uint64_t>* left = nullptr;      // if set: cards that left a Monster / Spell & Trap Zone (where_key of the zone left)
	int lp[2] = {8000, 8000};
	std::vector<std::pair<uint32_t, uint8_t>> chainLinks;          // the chain being built: (card, controller) per link
	std::vector<std::pair<uint32_t, uint8_t>> negated;             // activations negated or whose effect was negated
	// A reused duel slowly keeps a little memory from each game, so after this many reuses it's rebuilt.
	static constexpr int MAX_REUSE = 150;
	explicit Duel(const Setup& s) {
		// Reuse a finished duel when there is one: wiping its field keeps every card script already loaded,
		// and loading scripts is most of the cost of starting a duel.
		bool worn = false;
		if(g_reuse) { std::lock_guard<std::mutex> lk(g_pool_mx); if(!g_pool.empty()) { h = g_pool.back(); g_pool.pop_back(); worn = ++g_uses[h] > MAX_REUSE; } }
		if(h && worn) { forget(h); OCG_DestroyDuel(h); h = nullptr; }
		if(h && !reset()) { forget(h); OCG_DestroyDuel(h); h = nullptr; }
		if(!h) {
			OCG_DuelOptions o = options();
			if(OCG_CreateDuel(&h, &o) != OCG_DUEL_CREATION_SUCCESS) { h = nullptr; return; }
			for(const char* f : {"constant.lua", "utility.lua"}) { const std::string* b = get_script(f); if(!b || !OCG_LoadScript(h, b->data(), (uint32_t)b->size(), f)) { OCG_DestroyDuel(h); h = nullptr; return; } }
			mdc_mark_duel(h);
		}
		auto add = [&](uint8_t team, uint32_t code, uint32_t loc) { OCG_NewCardInfo i{team, 0, code, team, loc, 0, POS_FACEDOWN_DEFENSE}; OCG_DuelNewCard(h, &i); };
		for(uint32_t c : s.deck) add(0, c, LOCATION_DECK);
		for(int i = 0; i < BLANKS; i++) add(0, BLANK, LOCATION_DECK);
		for(uint32_t c : s.extra) add(0, c, LOCATION_EXTRA);
		for(uint32_t c : s.hand) add(0, c, LOCATION_HAND);
		// A preset board (scoring a guide's end board): monsters face-up (center first, unless zones are given), Traps /
		// Quick-Play / Normal Spells Set, Continuous and Field Spells face-up.
		{ static const int order[5] = {2, 1, 3, 0, 4};
		  for(size_t i = 0; i < s.preField.size() && i < 5; i++) { int z = i < s.preZones.size() && s.preZones[i] >= 0 && s.preZones[i] < 5 ? s.preZones[i] : order[i];
			OCG_NewCardInfo ci{0, 0, s.preField[i], 0, LOCATION_MZONE, (uint32_t)z, POS_FACEUP_ATTACK}; OCG_DuelNewCard(h, &ci); }
		  uint32_t seq = 0;
		  for(uint32_t c : s.preBack) { auto it = g_cards.find(c); uint32_t t = it == g_cards.end() ? 0 : it->second.type;
			bool fieldSpell = (t & TYPE_SPELL) && (t & TYPE_FIELD), faceUp = (t & TYPE_SPELL) && (t & (TYPE_CONTINUOUS | TYPE_FIELD | TYPE_EQUIP));
			if(!fieldSpell && seq >= 5) continue;
			OCG_NewCardInfo ci{0, 0, c, 0, LOCATION_SZONE, fieldSpell ? 5u : seq++, faceUp ? (uint32_t)POS_FACEUP : (uint32_t)POS_FACEDOWN}; OCG_DuelNewCard(h, &ci); }
		  for(uint32_t c : s.preGy) add(0, c, LOCATION_GRAVE);
		  for(uint32_t c : s.preBanished) { OCG_NewCardInfo ci{0, 0, c, 0, LOCATION_REMOVED, 0, POS_FACEUP}; OCG_DuelNewCard(h, &ci); } }
		if(s.oppDeck.empty()) for(int i = 0; i < 5; i++) add(1, DUMMY, LOCATION_DECK);
		for(uint32_t c : s.oppDeck) add(1, c, LOCATION_DECK);
		for(uint32_t c : s.oppExtra) add(1, c, LOCATION_EXTRA);
		for(uint32_t c : s.oppHand) add(1, c, LOCATION_HAND);   // handtraps for "what if they hit this" searches
		OCG_StartDuel(h);
	}
	// Wipe the field back to an empty Duel (the engine's own puzzle-reload path), keep the loaded scripts, and
	// clear the "already set up this Duel" flags scripts keep on their card tables so their global effects
	// register again.
	static OCG_DuelOptions options() {
		OCG_DuelOptions o{};
		o.seed[0] = 1; o.seed[1] = 2; o.seed[2] = 3; o.seed[3] = 4;
		o.flags = DUEL_MODE_MR5 | DUEL_PSEUDO_SHUFFLE;   // the opponent's choices are made by Search::opp_choice
		o.team1 = {8000, 0, 1}; o.team2 = {8000, 0, 1};
		o.cardReader = card_reader; o.scriptReader = script_reader; o.logHandler = log_handler; o.cardReaderDone = card_reader_done;
		return o;
	}
	bool reset() { return mdc_reset_duel(h, options()); }   // src/reset.cpp
	~Duel() {
		if(!h) return;
		if(g_reuse) { std::lock_guard<std::mutex> lk(g_pool_mx); if(g_pool.size() < 256) { g_pool.push_back(h); return; } }
		forget(h); OCG_DestroyDuel(h);
	}
	static void forget(OCG_Duel d) { mdc_forget_duel(d); std::lock_guard<std::mutex> lk(g_pool_mx); g_uses.erase(d); }
	// Runs until player 0 must decide something. returns false at duel end / error.
	bool run(Prompt& out, bool& retry) {
		retry = false;
		for(int guard = 0; guard < 10000; guard++) {
			int st = OCG_DuelProcess(h);
			uint32_t n = 0; const uint8_t* p = (const uint8_t*)OCG_DuelGetMessage(h, &n);
			bool got = false;
			for(size_t o = 0; o + 4 <= n;) {
				uint32_t ln = 0; std::memcpy(&ln, p + o, 4);
				const uint8_t* body = p + o + 4;
				if(ln >= 1) {
					int t = body[0];
					if(t == MSG_RETRY) { retry = true; return false; }
					if(t == MSG_WIN) return false;
					if(t == MSG_HINT && ln >= 11 && body[1] == 3 && body[2] < 2) std::memcpy(&lastHint[body[2]], body + 3, 8);   // HINT_SELECTMSG
					if(t == MSG_NEW_TURN && ln >= 2) { turnPlayer = body[1]; turns++; phase = 0; }
					if(t == MSG_NEW_PHASE && ln >= 3) { uint16_t ph; std::memcpy(&ph, body + 1, 2); phase = ph; }
					if(t == MSG_CHAINING && ln >= 33) { uint32_t code, n; std::memcpy(&code, body + 1, 4); std::memcpy(&n, body + 29, 4);
						if(n >= 1 && n < 64) { if(chainLinks.size() < n) chainLinks.resize(n); chainLinks[n - 1] = {code, body[15]}; } }
					if((t == MSG_CHAIN_NEGATED || t == MSG_CHAIN_DISABLED) && ln >= 2 && body[1] >= 1 && body[1] <= chainLinks.size()) negated.push_back(chainLinks[body[1] - 1]);
					if((t == MSG_DAMAGE || t == MSG_RECOVER || t == MSG_LPUPDATE || t == MSG_PAY_LPCOST) && ln >= 6 && body[1] < 2) {
						uint32_t v; std::memcpy(&v, body + 2, 4);
						lp[body[1]] = t == MSG_LPUPDATE ? (int)v : lp[body[1]] + (t == MSG_RECOVER ? (int)v : -(int)v); }
					if(t == MSG_MOVE && left && ln >= 15) { uint32_t code, seq; std::memcpy(&code, body + 1, 4); std::memcpy(&seq, body + 7, 4);
						if(body[6] & (LOCATION_MZONE | LOCATION_SZONE)) left->push_back(where_key(code, body[5], body[6], seq)); }
					if(is_prompt(t)) { out = parse_prompt(t, body + 1, ln - 1); if(out.player < 2) { out.hint = lastHint[out.player]; lastHint[out.player] = 0; } got = true; }
				}
				o += 4 + ln;
			}
			if(got) return true;
			if(st == OCG_DUEL_STATUS_END) return false;
		}
		return false;
	}
	void respond(const Bytes& b) { OCG_DuelSetResponse(h, b.data(), (uint32_t)b.size()); }
	// A player's cards in one place: code, position and ATK (for the simulation's checks on the opponent).
	struct Seen { uint32_t code = 0, pos = 0, seq = 0; int32_t atk = 0; };
	std::vector<Seen> look(uint8_t player, uint32_t loc) {
		OCG_QueryInfo q{QUERY_CODE | QUERY_POSITION | QUERY_ATTACK, player, loc, 0, 0};
		uint32_t n = 0; const uint8_t* p = (const uint8_t*)OCG_DuelQueryLocation(h, &n, &q);
		std::vector<Seen> out; Rd r(p, n, 4); int slot = -1;
		while(r.o + 2 <= n) {
			slot++;
			uint16_t sz = r.u16();
			if(sz == 0) continue;
			Seen c; c.seq = (uint32_t)slot;
			for(;;) {
				uint32_t flag = r.u32();
				if(flag == QUERY_END) break;
				size_t start = r.o;
				uint32_t v = r.u32();
				if(flag == QUERY_CODE) c.code = v; else if(flag == QUERY_POSITION) c.pos = v; else if(flag == QUERY_ATTACK) c.atk = (int32_t)v;
				r.o = start + (sz - 4);
				sz = r.u16();
			}
			if(c.code) out.push_back(c);
		}
		return out;
	}
	std::vector<uint32_t> cards(uint32_t loc, bool faceupOnly = false, std::vector<int>* slots = nullptr) {
		OCG_QueryInfo q{QUERY_CODE | QUERY_POSITION, 0, loc, 0, 0};
		uint32_t n = 0; const uint8_t* p = (const uint8_t*)OCG_DuelQueryLocation(h, &n, &q);
		std::vector<uint32_t> out;
		Rd r(p, n, 4);
		int slot = -1;
		while(r.o + 2 <= n) {
			slot++;
			uint16_t sz = r.u16();
			if(sz == 0) continue;
			uint32_t code = 0, pos = 0;
			for(;;) {
				uint32_t flag = r.u32();
				if(flag == QUERY_END) break;
				size_t start = r.o;
				uint32_t v = r.u32();
				if(flag == QUERY_CODE) code = v; else if(flag == QUERY_POSITION) pos = v;
				r.o = start + (sz - 4);
				sz = r.u16();
			}
			if(code && (!faceupOnly || (pos & POS_FACEUP))) { out.push_back(code); if(slots) slots->push_back(slot); }
		}
		return out;
	}
};

// ------------------------------------------------------------------ search
struct Step { std::string kind; uint32_t card = 0; std::string effect; std::vector<uint32_t> picks;
	std::vector<std::pair<uint32_t, std::vector<uint32_t>>> groups; };   // picks grouped by what they were for (hint id)
struct Opt { std::string label; Bytes resp; bool main = false; Step step; std::vector<uint32_t> picks; bool end = false; uint32_t hint = 0; uint64_t pkey = 0; std::shared_ptr<const std::vector<uint64_t>> avail; int ptype = 0; };   // ptype: the prompt it answers (0: any)   // avail: every card pickable alongside it

static std::vector<Opt> dedupe(std::vector<Opt> v) {
	std::set<std::string> seen; std::vector<Opt> out;
	for(auto& o : v) if(seen.insert(o.label).second) out.push_back(std::move(o));
	return out;
}
static std::vector<Opt> choices0(const Prompt& m);
static std::vector<Opt> choices(const Prompt& m) {
	std::vector<Opt> v = choices0(m);
	for(auto& o : v) { if(!o.picks.empty()) o.hint = (uint32_t)m.hint; o.ptype = m.type; }
	return v;
}
static std::vector<Opt> choices0(const Prompt& m) {
	std::vector<Opt> out;
	auto pickset = [&](const std::vector<uint32_t>& idx, const std::vector<uint32_t>& codes) {
		std::vector<uint32_t> cs; for(uint32_t i : idx) cs.push_back(codes[i]);
		std::vector<uint32_t> sorted = cs; std::sort(sorted.begin(), sorted.end());
		std::string lbl = "pick"; for(uint32_t c : sorted) lbl += " " + std::to_string(c);
		Opt o; o.label = lbl; o.resp = r_cards(idx); o.picks = cs; return o;
	};
	switch(m.type) {
	case MSG_SELECT_IDLECMD:
		for(size_t i = 0; i < m.summon.size(); i++) { Opt o; o.label = "ns " + std::to_string(m.summon[i].code); o.resp = r_idle(0, (int)i); o.main = true; o.step = {"Normal Summon", m.summon[i].code, "", {}}; out.push_back(o); }
		for(size_t i = 0; i < m.spsummon.size(); i++) { Opt o; o.label = "ss " + std::to_string(m.spsummon[i].code); o.resp = r_idle(1, (int)i); o.main = true; o.step = {"Special Summon", m.spsummon[i].code, "", {}}; out.push_back(o); }
		for(size_t i = 0; i < m.activate.size(); i++) { Opt o; o.label = "act " + std::to_string(m.activate[i].code) + " " + std::to_string(m.activate[i].desc); o.resp = r_idle(5, (int)i); o.main = true; o.step = {"Activate", m.activate[i].code, desc_text(m.activate[i].desc), {}}; out.push_back(o); }
		if(m.to_ep) { Opt o; o.label = "END"; o.resp = r_idle(7, 0); o.main = true; o.end = true; out.push_back(o); }
		return dedupe(out);
	case MSG_SELECT_CHAIN: {
		if(!m.forced) { Opt o; o.label = "no chain"; o.resp = p32(-1); out.push_back(o); }
		for(size_t i = 0; i < m.chains.size(); i++) { Opt o; o.label = "chain " + std::to_string(m.chains[i].first) + " " + std::to_string(m.chains[i].second); o.resp = p32((int)i); o.main = true; o.step = {"Activate", m.chains[i].first, desc_text(m.chains[i].second), {}}; out.push_back(o); }
		return dedupe(out);
	}
	case MSG_SELECT_EFFECTYN: case MSG_SELECT_YESNO: {
		Opt y; y.label = "yes"; y.resp = p32(1); Opt n; n.label = "no"; n.resp = p32(0);
		if(m.type == MSG_SELECT_EFFECTYN) { y.main = true; y.step = {"Use", m.code, desc_text(m.desc), {}}; }
		out.push_back(y); out.push_back(n); return out;
	}
	case MSG_SELECT_OPTION: case MSG_ANNOUNCE_NUMBER:
		for(size_t i = 0; i < m.options.size(); i++) { Opt o; o.label = "opt " + std::to_string(i); o.resp = p32((int)i); std::string d = m.type == MSG_SELECT_OPTION ? desc_text(m.options[i]) : std::to_string(m.options[i]); if(!d.empty()) o.step = {"Option", 0, d, {}}; out.push_back(o); }
		return out;
	case MSG_SELECT_CARD: case MSG_SELECT_TRIBUTE: {
		size_t n = m.cards.size(); uint32_t lo = std::max<uint32_t>(m.mn, 1), hi = std::min<uint32_t>(m.mx, (uint32_t)n);
		std::vector<uint32_t> idx;
		std::function<void(size_t, uint32_t)> rec = [&](size_t start, uint32_t k) {
			if(out.size() > 80) return;
			if(idx.size() == k) { out.push_back(pickset(idx, m.cards)); return; }
			for(size_t i = start; i < n; i++) { idx.push_back((uint32_t)i); rec(i + 1, k); idx.pop_back(); }
		};
		for(uint32_t k = lo; k <= hi; k++) rec(0, k);
		return dedupe(out);
	}
	case MSG_SELECT_UNSELECT_CARD:
		{ auto av = std::make_shared<const std::vector<uint64_t>>(m.ckeys);
		  for(size_t i = 0; i < m.cards.size(); i++) { Opt o; o.label = "pick " + std::to_string(m.cards[i]); o.resp = r_unselect((int)i); o.picks = {m.cards[i]}; if(i < m.ckeys.size()) { o.pkey = m.ckeys[i]; o.avail = av; } out.push_back(o); } }
		if(m.finishable || (m.cancelable && out.empty())) { Opt o; o.label = "finish"; o.resp = r_unselect(-1); out.push_back(o); }
		return dedupe(out);
	case MSG_SELECT_SUM: {
		uint32_t must = 0; for(uint32_t v : m.mustParam) must += v & 0xffff;
		size_t n = m.cards.size(); std::vector<uint32_t> idx;
		std::function<void(size_t, uint32_t)> rec = [&](size_t start, uint32_t total) {
			if(out.size() > 40) return;
			if(!idx.empty() && must + total == m.acc) out.push_back(pickset(idx, m.cards));
			for(size_t i = start; i < n; i++) { uint32_t v = m.cardParam[i] & 0xffff; if(must + total + v > m.acc) continue; idx.push_back((uint32_t)i); rec(i + 1, total + v); idx.pop_back(); }
		};
		rec(0, 0);
		if(out.empty()) { std::vector<uint32_t> all; for(size_t i = 0; i < n; i++) all.push_back((uint32_t)i); out.push_back(pickset(all, m.cards)); }
		return dedupe(out);
	}
	case MSG_SELECT_PLACE: case MSG_SELECT_DISFIELD: { Opt o; o.label = "place"; o.resp = r_place(m); out.push_back(o); return out; }
	case MSG_SELECT_POSITION: { Opt o; o.label = "pos"; o.resp = r_position(m); out.push_back(o); return out; }
	case MSG_SORT_CARD: case MSG_SORT_CHAIN: { Opt o; o.label = "sort"; o.resp = Bytes(1, (char)0xff); out.push_back(o); return out; }
	case MSG_ANNOUNCE_RACE: { Opt o; o.label = "race"; uint64_t a = m.available & (~m.available + 1); o.resp = Bytes((const char*)&a, 8); out.push_back(o); return out; }
	case MSG_ANNOUNCE_ATTRIB: { Opt o; o.label = "attr"; uint32_t a = (uint32_t)(m.available & (~m.available + 1)); o.resp = pu32(a); out.push_back(o); return out; }
	case MSG_ANNOUNCE_CARD: { if(!m.options.empty()) { Opt o; o.label = "announce"; o.resp = pu32((uint32_t)m.options[0]); out.push_back(o); } return out; }
	default: return out;   // battle commands, counters, RPS: not part of a turn-one combo
	}
}

struct Board { std::vector<uint32_t> mzone, szone, hand, grave, banished, extra; std::vector<int> mslot; };   // mslot: zone of each mzone card (2 = center)
struct Found { std::vector<Step> steps; std::vector<std::string> labels; Board board; double score = 0; std::vector<Bytes> mine; std::vector<int> mineType; };   // mine: our own responses (replays the line even when the opponent gets extra prompts)

struct Search {
	Setup setup;
	int maxActions = 8; double timeLimit = 20; int threads = 2; size_t top = 12; bool wantLabels = false; std::string mode;
	std::set<uint32_t> targets;
	// "What if they hit this?": follow `prefix` (our choices, by label) until our step `hitStep` is activated,
	// let the opponent chain `hitCard` there, then search freely from whatever is left.
	std::vector<std::string> prefix; uint32_t hitCard = 0; int hitStep = -1;
	std::vector<std::vector<std::string>> seeds;   // lines found before for this hand (choice labels): replayed first, then built on
	bool interrupting() const { return hitCard != 0; }
	std::atomic<bool> stop{false};
	Clock::time_point t0;
	std::atomic<uint64_t> replays{0}, prompts{0};
	std::mutex mx;
	std::unordered_set<std::string> visited;
	std::map<std::string, Found> boards;
	// ending: the turn was ended and the End Phase is being played out (see ep_worth).
	struct St { std::vector<Bytes> path; std::vector<Step> steps; std::vector<std::string> labels; int actions = 0; bool hit = false, aim = false, ending = false; uint64_t lastPick = 0; std::shared_ptr<const std::vector<uint64_t>> pickedFrom; std::vector<Bytes> mine; std::vector<int> mineType; };
	// Is the End Phase worth playing out? Only when a card we have mentions it (Ecclesia / Cartesia adding themselves
	// back, Branded searches): otherwise the board at "end turn" is already final and searches stay as fast as before.
	static bool ep_worth(const Board& b) {
		for(const auto* v : {&b.mzone, &b.szone, &b.hand, &b.grave, &b.banished}) for(uint32_t c : *v) { auto it = g_cards.find(c); if(it != g_cards.end() && it->second.endPhase) return true; }
		return false;
	}
	// In the End Phase only End Phase effects are offered, so Quick Effects aren't spent in the opponent's Draw Phase.
	static void ep_filter(const Prompt& m, std::vector<Opt>& opts) {
		if(m.type != MSG_SELECT_CHAIN || m.forced) return;
		std::vector<Opt> keep;
		for(auto& o : opts) { auto it = g_cards.find(o.step.card); if(!o.main || (it != g_cards.end() && it->second.endPhase)) keep.push_back(o); }
		opts.swap(keep);
	}
	using Task = St;
	std::deque<Task> queue;
	std::condition_variable cv;
	int busy = 0;

	// Stable stop: once the best board hasn't improved for stableFrac of the time spent (and at least stableMin seconds
	// have passed), more searching is unlikely to find better, so stop early. 0 = off: run the full time.
	double stableFrac = 0, stableMin = 5;
	std::atomic<double> bestAt{0}; double bestScore = -1e18;   // when the best board so far was found (seconds)
	std::vector<Bytes> bestPath;   // the line to the best board so far (the beam keeps the states along it)
	mutable std::atomic<bool> stoppedStable{false};
	double elapsed() const { return std::chrono::duration<double>(Clock::now() - t0).count(); }
	bool out_of_time() const {
		if(stop.load() || stoppedStable.load()) return true;
		double el = elapsed();
		if(el > timeLimit) return true;
		if(stableFrac > 0 && el >= stableMin && el - bestAt.load() >= stableFrac * el) { stoppedStable = true; return true; }
		return false;
	}
	// Positions are keyed by their cards (sorted, so GY order doesn't matter); for zone decks also by whether each
	// center-aware monster is in the center or a side zone. Other monsters' zones don't split positions (that would
	// multiply equal states and crowd out real lines).
	static std::string zkey(const Board& b) {
		if(!g_zones) return "";
		std::vector<std::string> z;
		for(size_t i = 0; i < b.mzone.size(); i++) { auto it = g_cards.find(b.mzone[i]); if(it == g_cards.end() || !it->second.centerAware) continue;
			z.push_back(std::to_string(b.mzone[i]) + (i < b.mslot.size() && b.mslot[i] == 2 ? "C" : "S")); }
		std::sort(z.begin(), z.end()); std::string k = "Z"; for(auto& x : z) k += x + ","; return k;
	}
	static bool extra_type(uint32_t code) { auto it = g_cards.find(code); return it != g_cards.end() && (it->second.type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ | TYPE_LINK)); }
	// How strong this end board is (see src/evaluate.h): every way it can stop the opponent, best first with a
	// gentle fall-off, plus locks, sturdiness, bodies and spare cards, plus the deck's own goal cards.
	double score_of(const Board& b0, json* why = nullptr) const {
		// A monster that banished itself "until the End Phase" comes back when the turn ends: count it on the field.
		Board moved; const Board* bp = &b0;
		for(uint32_t c : b0.banished) { auto it = g_cards.find(c); if(it == g_cards.end() || !it->second.backAtEnd) continue;
			if(bp == &b0) { moved = b0; bp = &moved; }
			auto at = std::find(moved.banished.begin(), moved.banished.end(), c); moved.banished.erase(at); moved.mzone.push_back(c); moved.mslot.resize(moved.mzone.size() - 1, -1); moved.mslot.push_back(-1); }
		const Board& b = *bp;
		// stops: (value, what) — "what" is only filled in when a breakdown is asked for.
		struct Stop { double first; std::string second; uint32_t code; };   // value, what (breakdown only), the card that acts on their turn
		std::vector<Stop> stops; double s = 0;
		auto nm = [&](uint32_t c) { return why ? card_name(c) : std::string(); };
		auto add = [&](double v, const std::string& what) { s += v; if(why && v != 0) (*why)["flat"].push_back({{"what", what}, {"value", v}}); };
		std::vector<const CardEval*> revivers;   // cards that can bring a monster back from the GY on their turn
		std::vector<const CardEval*> fusers;     // cards that Fusion from the Extra Deck on their turn (judged below)
		const bool knowExtra = !b.extra.empty();
		auto ev = [](uint32_t c) -> const CardEval* { auto it = g_cards.find(c); return it == g_cards.end() ? nullptr : &it->second.ev; };
		// An interruption that needs a certain monster on our field (Mercourier: a Fusion that mentions "Fallen of Albaz")
		// counts 30% without one: the condition might still be met on their turn (the simulation checks for real).
		auto need = [&](const CardEval* e) -> double {
			if(!e || e->needs.empty()) return 1.0;
			auto fits = [](const CardEval::Mat& m, const CardRow& pc) {
				if(m.kinds && !(pc.type & m.kinds)) return false;
				if(m.attr && !(pc.attribute & m.attr)) return false;
				if(m.race && !(pc.race & m.race)) return false;
				if(!m.tag.empty()) {
					bool ok = false;
					if(m.mentions) { for(const auto& pm : pc.ev.mats) if(pm.tag.find(m.tag) != std::string::npos) ok = true; if(!ok && evalx::lower(pc.desc).find("\"" + m.tag + "\"") != std::string::npos) ok = true; }
					else ok = pc.lname.find(m.tag) != std::string::npos;
					if(!ok) return false;
				}
				return true;
			};
			for(const auto& nd : e->needs) {   // alternatives: any one met will do
				int have = 0;
				for(uint32_t c : nd.where == 2 ? b.grave : b.mzone) { auto it = g_cards.find(c); if(it != g_cards.end() && fits(nd.m, it->second)) have++; }
				if(have >= nd.n) return 1.0;
			}
			return 0.3;
		};
		bool centerTaken = std::find(b.mslot.begin(), b.mslot.end(), 2) != b.mslot.end();
		for(size_t i = 0; i < b.mzone.size(); i++) {
			uint32_t c = b.mzone[i]; int slot = i < b.mslot.size() ? b.mslot[i] : -1;
			const CardEval* e = ev(c);
			add(0.5 + (extra_type(c) ? 0.3 : 0), why ? "body: " + nm(c) : "");
			if(!e) continue;
			// Zone-dependent cards: "switch with the center monster" only works from a side zone with the center filled;
			// "while in the center Main Monster Zone" only works in the center.
			bool works = !(e->fromSide && (slot == 2 || !centerTaken)) && !(e->inCenter && slot != 2);
			// A monster that Fusion Summons on their turn (Blazing Cartesia) is worth the Fusion it can make, judged below
			// with Favorite Contact; its own generic "summons something" share is dropped.
			bool fuses = works && knowExtra && e->fusionAt == CardEval::AT_FIELD;
			if(fuses) fusers.push_back(e);
			if(e->field > 0 && works && !(fuses && e->field <= 1.5f)) stops.push_back({e->field * need(e), nm(c), c});
			if(works && e->reviveAt == CardEval::AT_FIELD) revivers.push_back(e);
			add(works ? e->lock : 0, why ? "lock: " + nm(c) : ""); add(e->sturdy, why ? "sturdy: " + nm(c) : "");
		}
		for(uint32_t c : b.szone) {
			const CardEval* e = ev(c); if(!e) { add(0.3, "backrow card"); continue; }
			if(knowExtra && (e->fusionAt == CardEval::AT_SET || e->fusionAt == CardEval::AT_FIELD)) { fusers.push_back(e); add(e->lock, why ? "lock: " + nm(c) : ""); continue; }
			double v = std::max(e->set, e->field);
			if(v > 0) stops.push_back({v * need(e), nm(c) + " (set)", c}); else add(0.3, why ? "backrow: " + nm(c) : "");
			if(e->reviveAt == CardEval::AT_SET || e->reviveAt == CardEval::AT_FIELD) revivers.push_back(e);
			add(e->lock, why ? "lock: " + nm(c) : "");
		}
		// Traps and Quick-Plays still in hand get Set at the end of the turn, while there's room.
		size_t room = b.szone.size() >= 5 ? 0 : 5 - b.szone.size();
		for(uint32_t c : b.hand) {
			const CardEval* e = ev(c);
			auto it = g_cards.find(c);
			bool settable = it != g_cards.end() && ((it->second.type & TYPE_TRAP) || ((it->second.type & TYPE_SPELL) && (it->second.type & TYPE_QUICKPLAY)));
			if(settable && room && e && e->set >= (e->hand > 0 ? e->hand : 0) && e->set > 0) { if(knowExtra && e->fusionAt == CardEval::AT_SET) fusers.push_back(e); else stops.push_back({e->set * need(e), nm(c) + " (set from hand)", c}); room--; if(e->reviveAt == CardEval::AT_SET) revivers.push_back(e); }
			else if(e && e->hand > 0) { stops.push_back({e->hand * need(e), nm(c) + " (hand)", c}); if(e->reviveAt == CardEval::AT_HAND) revivers.push_back(e); }
			// Anything else is for later: an extender for their turn, a starter for our next turn, or just a card.
			else if(e && e->handExtender) add(0.8, why ? "hand (extender on their turn): " + nm(c) : "");
			else if(e && e->starter) add(0.6, why ? "hand (starter next turn): " + nm(c) : "");
			else add(0.3, why ? "hand: " + nm(c) : "");   // (or one drawn during the line)
		}
		for(uint32_t c : b.grave) { const CardEval* e = ev(c); if(e && e->gy > 0) stops.push_back({e->gy * 0.8, nm(c) + " (GY)", c}); if(e && e->reviveAt == CardEval::AT_GY) revivers.push_back(e); }
		// Revival: each reviver brings back the best monster in the GY that fits what it asks for, and that monster's
		// interruption counts too (an Elfnote that June Pride or Rhapsodia returns on their turn). Any-monster
		// revivers count for less: their text often has conditions this doesn't read (Type, Attribute).
		// Chains: a revived monster that revives on the field goes on with its own revival (Rhapsodia brings back
		// Strelitzia, which brings back Tinia), so picks look one step ahead.
		if(!revivers.empty()) {
			std::vector<char> used(b.grave.size(), 0);
			auto row = [&](size_t i) -> const CardRow* { auto it = g_cards.find(b.grave[i]); return it == g_cards.end() || !(it->second.type & TYPE_MONSTER) ? nullptr : &it->second; };
			auto fits = [&](const CardEval* r, size_t i) {
				const CardRow* c = row(i);
				if(used[i] || !c || &c->ev == r) return false;
				if(!r->reviveTag.empty() && c->lname.find(r->reviveTag) == std::string::npos) return false;
				return !(r->reviveMaxLv < 99 && ((c->type & (TYPE_XYZ | TYPE_LINK)) || (int)(c->level & 0xff) > r->reviveMaxLv));
			};
			auto val = [&](size_t i) {
				const CardRow* c = row(i);
				double v = std::max(c->ev.field, c->ev.onSummon);   // what it does when it lands counts too
				return c->ev.inCenter ? v * 0.5 : v;                // comes back wherever there's room
			};
			for(size_t ri = 0; ri < revivers.size() && ri < 8; ri++) { const CardEval* r = revivers[ri];
				double best = -1; size_t pick = 0;
				for(size_t i = 0; i < b.grave.size(); i++) {
					if(!fits(r, i)) continue;
					double v = val(i);
					const CardEval* ce = &row(i)->ev;
					if(ce->reviveAt == CardEval::AT_FIELD) {   // what it would bring back in turn
						double next = 0; used[i] = 1;
						for(size_t j = 0; j < b.grave.size(); j++) if(fits(ce, j)) next = std::max(next, val(j));
						used[i] = 0; v += next * (ce->reviveTag.empty() ? 0.5 : 0.7) / (r->reviveTag.empty() ? 0.5 : 0.7);   // same scale as its own stop
					}
					if(v > best) { best = v; pick = i; }
				}
				if(best < 0) continue;
				used[pick] = 1;
				double own = val(pick);
				if(own > 0) stops.push_back({own * (r->reviveTag.empty() ? 0.5 : 0.7), why ? "revive " + nm(b.grave[pick]) : "", r->code}); else add(0.3, why ? "revive body: " + nm(b.grave[pick]) : "");
				if(row(pick)->ev.reviveAt == CardEval::AT_FIELD) revivers.push_back(&row(pick)->ev);   // and it revives in turn
			}
		}
		// Does a card fit one Fusion material? Names also match through "always treated as" (its alias).
		auto mat_ok = [](const CardEval::Mat& m, const CardRow& pc) {
			if(!m.tag.empty()) {
				auto al = pc.alias ? g_cards.find(pc.alias) : g_cards.end();
				const std::string* an = al != g_cards.end() ? &al->second.lname : nullptr;
				bool ok = m.exact ? (pc.lname == m.tag || (an && *an == m.tag)) : (pc.lname.find(m.tag) != std::string::npos || (an && an->find(m.tag) != std::string::npos));
				if(!ok) return false;
			}
			if(m.attr && !(pc.attribute & m.attr)) return false;
			if(m.race && !(pc.race & m.race)) return false;
			if(m.kinds && !(pc.type & m.kinds)) return false;
			if(m.effect && !(pc.type & TYPE_EFFECT)) return false;
			return pc.atk >= m.minAtk;
		};
		// Fusion on their turn (Favorite Contact): worth something only if the Extra Deck holds a Fusion it can make from
		// the monsters in reach (Neos + a Wingman for Shining Neos Wingman). Then it's worth that monster, by its Quick
		// Effect or what it does when summoned, and it counts for the goals. With nothing to make, it's a dead card.
		for(const CardEval* fz : fusers) {
			std::vector<uint32_t> pool;
			auto addp = [&](const std::vector<uint32_t>& v) { for(uint32_t c : v) { auto it = g_cards.find(c); if(it != g_cards.end() && (it->second.type & TYPE_MONSTER)) pool.push_back(c); } };
			if(fz->fusionFrom & 1) addp(b.hand);
			if(fz->fusionFrom & 2) addp(b.mzone);
			if(fz->fusionFrom & 4) addp(b.grave);
			if(fz->fusionFrom & 8) addp(b.banished);
			double best = -1; bool goal = false; uint32_t made = 0; std::set<uint32_t> tried;
			for(uint32_t fc : b.extra) {
				auto fi = g_cards.find(fc);
				if(!tried.insert(fc).second || fi == g_cards.end() || !(fi->second.type & TYPE_FUSION) || fi->second.ev.mats.empty()) continue;
				const auto& mats = fi->second.ev.mats;
				if(!fz->fusionTag.empty()) { bool ok = false; for(const auto& m : mats) if(m.tag.find(fz->fusionTag) != std::string::npos) ok = true; if(!ok) continue; }
				// Exact names first, then Fusion-only parts, then loose ones, so a loose part can't take what an exact one needs.
				std::vector<const CardEval::Mat*> order; for(const auto& m : mats) order.push_back(&m);
				auto rank = [](const CardEval::Mat* x) { return x->exact ? 0 : (!x->tag.empty() || x->attr || x->race || x->kinds || x->effect || x->minAtk) ? 1 : 2; };
				std::stable_sort(order.begin(), order.end(), [&](const CardEval::Mat* x, const CardEval::Mat* y) { return rank(x) < rank(y); });
				std::vector<char> used(pool.size(), 0); bool ok = true;
				for(const CardEval::Mat* m : order) for(int k = 0; k < m->n && ok; k++) {
					bool got = false;
					for(size_t i = 0; i < pool.size() && !got; i++) {
						if(used[i]) continue;
						const CardRow& pc = g_cards.find(pool[i])->second;
						bool match = mat_ok(*m, pc);
						if(match) { used[i] = 1; got = true; }
					}
					ok = got;
				}
				if(!ok) continue;
				double v = std::max(1.0, (double)std::max(fi->second.ev.field, fi->second.ev.onSummon));
				bool g = targets.count(fc) > 0;
				if(v + (g ? 8 : 0) > best + (goal ? 8 : 0)) { best = v; goal = g; made = fc; }
			}
			if(best < 0) { add(0.3, "dead Fusion card (nothing to make)"); continue; }
			stops.push_back({best, why ? "fusion into " + nm(made) : "", fz->code});
			if(goal) add(8, why ? "goal via fusion: " + nm(made) : "");
		}
		std::sort(stops.begin(), stops.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
		double f = 1.0;
		for(const auto& st : stops) { s += st.first * f; if(why) (*why)["stops"].push_back({{"what", st.second}, {"value", st.first}, {"counts", st.first * f}, {"code", st.code}}); f = std::max(0.5, f - 0.1); }
		for(uint32_t c : b.mzone) if(targets.count(c)) add(8, why ? "goal: " + nm(c) : "");
		for(uint32_t c : b.szone) if(targets.count(c)) add(8, why ? "goal: " + nm(c) : "");
		if(why) (*why)["total"] = s;
		return s;
	}
	Board read_board(Duel& d) {
		Board b; b.mzone = d.cards(LOCATION_MZONE, false, &b.mslot); b.szone = d.cards(LOCATION_SZONE); b.hand = d.cards(LOCATION_HAND);
		b.grave = d.cards(LOCATION_GRAVE); b.banished = d.cards(LOCATION_REMOVED); b.extra = d.cards(LOCATION_EXTRA); return b;
	}
	static std::string key_of(std::vector<uint32_t> v) { std::sort(v.begin(), v.end()); std::string s; for(uint32_t c : v) s += std::to_string(c) + ","; return s; }
	void record(Duel& d, const St& st) {
		if(interrupting() && !st.hit) return;
		const std::vector<Step>& steps = st.steps;
		Board b = read_board(d);
		std::string k = key_of(b.mzone) + "|" + key_of(b.szone) + "|" + key_of(b.hand);
		double sc = score_of(b);   // can differ for the same visible cards: what's in the GY to revive
		std::lock_guard<std::mutex> lk(mx);
		auto it = boards.find(k);
		if(it == boards.end() || sc > it->second.score + 1e-9 || (sc > it->second.score - 1e-9 && steps.size() < it->second.steps.size())) { Found f; f.steps = steps; f.labels = st.labels; f.board = b; f.score = sc; f.mine = st.mine; f.mineType = st.mineType; boards[k] = f; }
		if(sc > bestScore + 1e-9) { bestScore = sc; bestAt = elapsed(); bestPath = st.path; }
	}
	std::unique_ptr<Duel> replay(const std::vector<Bytes>& path, Prompt& m, bool& ok) {
		auto d = std::make_unique<Duel>(setup);
		replays++;
		ok = false;
		if(!d->h) return d;
		bool retry;
		if(!d->run(m, retry)) return d;
		for(const Bytes& b : path) { d->respond(b); if(!d->run(m, retry)) return d; }
		ok = true;
		return d;
	}
	static void add_step(std::vector<Step>& steps, const Opt& o) {
		if(!o.step.kind.empty() && o.step.kind != "Option") { steps.push_back(o.step); return; }
		if(!steps.empty()) {
			if(!o.picks.empty()) {
				Step& st = steps.back();
				for(uint32_t c : o.picks) st.picks.push_back(c);
				if(st.groups.empty() || st.groups.back().first != o.hint) st.groups.push_back({o.hint, {}});
				for(uint32_t c : o.picks) st.groups.back().second.push_back(c);
			}
			else if(o.step.kind == "Option" && steps.back().effect.empty()) steps.back().effect = o.step.effect;
		}
	}
	// Where a monster goes, for decks that care about zones (center Main Monster Zone, columns):
	// - a card that wants the center (a trigger "Summoned to the center Main Monster Zone", or effects that only
	//   work "while ... in the center") goes there when it's free;
	// - other zone-aware cards (e.g. ones that later switch into the center) go to a side zone, keeping it free;
	// - when the summon is the action itself (Normal Summon, Synchro / Link... from the Extra Deck) of a zone-aware
	//   card, both the center and a side zone are tried.
	// Anything else takes the first free zone, as before.
	static uint32_t placing(const St& st, bool& isAction) {
		isAction = false;
		if(st.steps.empty()) return 0;
		const Step& s = st.steps.back();
		for(auto it = s.groups.rbegin(); it != s.groups.rend(); ++it) if(it->first == 509 && !it->second.empty()) return it->second.back();
		if(s.kind == "Normal Summon" || s.kind == "Special Summon") { isAction = true; return s.card; }
		return 0;
	}
	static void zone_opts(const Prompt& m, const St& st, std::vector<Opt>& opts) {
		if(!g_zones || m.type != MSG_SELECT_PLACE || m.count != 1 || m.player != 0) return;
		auto freeM = [&](int seq) { return !(m.flag & (1u << seq)); };
		auto pick = [&](int seq, const char* lbl) { Opt o; o.label = lbl; o.resp = Bytes(); o.resp += (char)m.player; o.resp += (char)LOCATION_MZONE; o.resp += (char)seq; return o; };
		int side = -1; for(int seq : {0, 1, 3, 4}) if(freeM(seq)) { side = seq; break; }
		if(side < 0 && !freeM(2)) return;   // no Main Monster Zone choice to make
		bool isAction; uint32_t c = placing(st, isAction);
		auto it = g_cards.find(c);
		if(it == g_cards.end()) return;
		std::string d = evalx::lower(it->second.desc);
		bool aware = d.find("center main monster zone") != std::string::npos;
		if(!aware) return;
		bool wantsCenter = d.find("summoned to the center main monster zone") != std::string::npos || d.find("summoned to your center main monster zone") != std::string::npos ||
			d.find("while this card is in the center main monster zone") != std::string::npos || d.find("if this card is in the center main monster zone") != std::string::npos ||
			d.find("while in the center main monster zone") != std::string::npos;
		std::vector<Opt> out;
		if(isAction) { if(freeM(2)) out.push_back(pick(2, "zone center")); if(side >= 0) out.push_back(pick(side, "zone side")); }
		else if(wantsCenter && freeM(2)) out.push_back(pick(2, "zone center"));
		else if(side >= 0) out.push_back(pick(side, "zone side"));
		else out.push_back(pick(2, "zone center"));
		if(!out.empty()) opts.swap(out);
	}
	// The opponent's side of every prompt: pass, except chaining hitCard at the planned moment.
	Opt opp_choice(const Prompt& m, const St& st, bool& hitNow) const {
		hitNow = false;
		uint32_t want = (hitStep >= 0 && hitStep < (int)st.steps.size()) ? st.steps[hitStep].card : 0;
		Opt o; o.label = "opp";
		switch(m.type) {
		case MSG_SELECT_CHAIN:
			o.resp = p32(m.forced && !m.chains.empty() ? 0 : -1);
			if(interrupting() && !st.hit && (int)st.steps.size() == hitStep + 1)
				for(size_t i = 0; i < m.chains.size(); i++) if(m.chains[i].first == hitCard) { o.resp = p32((int)i); hitNow = true; break; }
			return o;
		case MSG_SELECT_CARD: case MSG_SELECT_TRIBUTE: {
			std::vector<uint32_t> idx;
			for(size_t i = 0; i < m.cards.size() && want; i++) if(m.cards[i] == want) { idx.push_back((uint32_t)i); break; }
			for(size_t i = 0; idx.size() < std::max<uint32_t>(m.mn, 1) && i < m.cards.size(); i++) if(std::find(idx.begin(), idx.end(), (uint32_t)i) == idx.end()) idx.push_back((uint32_t)i);
			o.resp = r_cards(idx); return o;
		}
		case MSG_SELECT_UNSELECT_CARD: {
			int pick = -1;
			for(size_t i = 0; i < m.cards.size(); i++) if(m.cards[i] == want) { pick = (int)i; break; }
			if(pick < 0 && !m.finishable && !m.cards.empty()) pick = 0;
			o.resp = r_unselect(pick); return o;
		}
		case MSG_SELECT_EFFECTYN: case MSG_SELECT_YESNO: o.resp = p32(1); return o;
		default: {
			std::vector<Opt> c = choices(m);
			o.resp = c.empty() ? p32(0) : c[0].resp; return o;
		}
		}
	}
	static void take(St& st, const Opt& o) { st.path.push_back(o.resp); st.mine.push_back(o.resp); st.mineType.push_back(o.ptype); st.labels.push_back(o.label); add_step(st.steps, o); if(o.end) st.ending = true; st.lastPick = o.pkey; st.pickedFrom = o.avail; }
	// Cards picked one at a time: picking A then B ends where B then A does, so after one of our picks only cards that
	// come later (by where they are) are offered, and each set is tried once instead of once per order. Only our own
	// picks count (not a material the game selected for us), and only cards that were pickable then are skipped: some
	// prompts change what's pickable after each pick (a Tuner first, then the rest), and those sets have only one order.
	static void pick_order(const Prompt& m, const St& st, std::vector<Opt>& opts) {
		static const bool off = getenv("MDC_NOPICKORDER") != nullptr;
		if(off || m.type != MSG_SELECT_UNSELECT_CARD || !st.lastPick) return;
		auto before = [&](uint64_t k) { return st.pickedFrom && std::find(st.pickedFrom->begin(), st.pickedFrom->end(), k) != st.pickedFrom->end(); };
		std::vector<Opt> keep; for(auto& o : opts) if(!o.pkey || o.pkey > st.lastPick || !before(o.pkey)) keep.push_back(std::move(o));
		opts.swap(keep);
	}
	// Explore from a live duel `d` sitting at prompt m. Hands extra branches to idle workers.
	void dfs(std::unique_ptr<Duel> d, Prompt m, St st) {
		for(;;) {
			if(out_of_time()) return;
			prompts++;
			bool retry;
			if(st.ending && m.type == MSG_SELECT_IDLECMD) { record(*d, st); return; }   // the opponent's turn has started
			if(m.player == 1 && m.type != MSG_SELECT_IDLECMD) {
				// A targeting handtrap (Imperm, Veiler) only counts when it can target the card from the planned step.
				if(st.aim && (m.type == MSG_SELECT_CARD || m.type == MSG_SELECT_UNSELECT_CARD || m.type == MSG_SELECT_TRIBUTE)) {
					uint32_t want = st.steps[hitStep].card;
					if(std::find(m.cards.begin(), m.cards.end(), want) == m.cards.end()) return;
					st.aim = false;
				}
				bool hitNow; Opt o = opp_choice(m, st, hitNow);
				d->respond(o.resp); st.path.push_back(o.resp);
				if(hitNow) { st.hit = true; st.aim = true; st.steps.push_back({"Opp", hitCard, "", {}}); }
				if(!d->run(m, retry)) return;
				continue;
			}
			st.aim = false;   // back to us: any target was already chosen
			std::vector<Opt> opts = (m.type == MSG_SELECT_CHAIN && m.chains.empty()) ? std::vector<Opt>{} : choices(m);
			if(m.type == MSG_SELECT_CHAIN && m.chains.empty()) { Opt o; o.label = "pass"; o.resp = p32(-1); opts.push_back(o); }
			zone_opts(m, st, opts); pick_order(m, st, opts);
			if(st.ending) ep_filter(m, opts);
			if(interrupting() && !st.hit) {
				// Still replaying the planned line: only its next choice is allowed.
				size_t pi = st.labels.size();
				if(pi >= prefix.size()) return;          // the line finished without them getting the chance
				std::vector<Opt> keep; for(auto& o : opts) if(o.label == prefix[pi]) keep.push_back(o);
				opts.swap(keep);
			}
			if(opts.empty()) return;
			bool counts = !interrupting() || st.hit;
			if(opts.size() == 1) {
				const Opt& o = opts[0];
				if(o.end) { record(*d, st); if(!ep_worth(read_board(*d))) return; }
				d->respond(o.resp); take(st, o);
				if(o.main && m.type == MSG_SELECT_IDLECMD && counts) st.actions++;
				if(!d->run(m, retry)) return;
				continue;
			}
			bool epOK = false;
			if(m.type == MSG_SELECT_IDLECMD) {
				Board b = read_board(*d);
				epOK = ep_worth(b);
				std::string k = std::string(st.hit ? "H" : "") + key_of(b.mzone) + "|" + key_of(b.szone) + "|" + key_of(b.hand) + "|" + key_of(b.grave) + "|" + key_of(b.banished) + zkey(b) + "#";
				for(auto& o : opts) k += o.label + ";";
				{ std::lock_guard<std::mutex> lk(mx); if(!visited.insert(k).second) return; }
				record(*d, st);
				if(st.actions >= maxActions) {   // out of actions: only ending the turn (and its End Phase) is left
					if(!epOK) return;
					std::vector<Opt> e; for(auto& o : opts) if(o.end) e.push_back(o); opts.swap(e);
				}
			}
			// first branch continues on this duel; the rest are replayed (or handed to idle threads)
			bool first = true;
			for(const Opt& o : opts) {
				if(o.end && !epOK) continue;
				if(first) { first = false; continue; }   // handled last, below
				St s2 = st; take(s2, o);
				if(o.main && m.type == MSG_SELECT_IDLECMD && counts) s2.actions++;
				{
					std::lock_guard<std::mutex> lk(mx);
					if((int)queue.size() < threads * 2) { queue.push_back(std::move(s2)); cv.notify_one(); continue; }
				}
				if(out_of_time()) return;
				Prompt mm; bool ok; auto dd = replay(s2.path, mm, ok);
				if(ok) dfs(std::move(dd), mm, std::move(s2));
			}
			// the first non-end option, on the live duel
			bool moved = false;
			for(const Opt& o : opts) {
				if(o.end && !epOK) continue;
				d->respond(o.resp); take(st, o);
				if(o.main && m.type == MSG_SELECT_IDLECMD && counts) st.actions++;
				moved = true;
				break;
			}
			if(!moved || !d->run(m, retry)) return;
		}
	}
	// ---------------- beam search (normal searches) ----------------
	// Level by level over the moments we're back in the Main Phase with a free choice: expand every kept state
	// by each action (and every choice inside it), keep the most promising few, repeat. The beam widens on
	// each pass until time runs out, so early choices (what to search, what to discard) all get a fair look,
	// which plain depth-first search with a time limit doesn't give them.
	struct Node { St st; double h = 0; size_t parent = 0; uint64_t tie = 0; };   // tie: fixed pseudo-random order for equal h
	std::mutex vmx; std::unordered_set<std::string> bvisited;
	// How good a mid-combo state looks: the board so far, plus how much is still left to do from here
	// (effects ready to use, cards in hand, the Normal Summon).
	double promise(const Board& b, bool nsLeft, size_t moves) const {
		return score_of(b) + W_MOVES * moves + W_HAND * b.hand.size() + (nsLeft ? W_NS : 0) + W_GY * b.grave.size();
	}
	double W_MOVES = 0.35, W_HAND = 0.25, W_NS = 0.5, W_GY = 0.1; bool diverse = true; size_t width0 = 8;
	// Play from the live duel at prompt m until the next free Main Phase choice; each one reached becomes a child.
	void sub(std::unique_ptr<Duel> d, Prompt m, St st, bool atStart, std::vector<Node>& out, std::mutex& omx, size_t par) {
		for(;;) {
			if(out_of_time()) return;
			prompts++;
			bool retry;
			if(st.ending && m.type == MSG_SELECT_IDLECMD) { record(*d, st); return; }   // the opponent's turn has started
			if(m.player == 1 && m.type != MSG_SELECT_IDLECMD) {
				bool hn; Opt o = opp_choice(m, st, hn);
				d->respond(o.resp); st.path.push_back(o.resp);
				if(!d->run(m, retry)) return;
				continue;
			}
			// A new decision point ends this expansion: back in the Main Phase with a free choice, or a chain window
			// where we could respond (decks full of Quick Effects play whole combos inside chain windows).
			bool window = !st.ending && m.type == MSG_SELECT_CHAIN && !m.chains.empty() && !m.forced;   // End Phase choices stay inside this expansion
			if((m.type == MSG_SELECT_IDLECMD || window) && !atStart) {
				Board b = read_board(*d);
				std::vector<Opt> opts = choices(m);
				std::string k = key_of(b.mzone) + "|" + key_of(b.szone) + "|" + key_of(b.hand) + "|" + key_of(b.grave) + "|" + key_of(b.banished) + zkey(b) + "#";
				for(auto& o : opts) k += o.label + ";";
				if(window) k = "W" + k;
				{ std::lock_guard<std::mutex> lk(vmx); if(!bvisited.insert(k).second) return; }
				if(!window) record(*d, st);
				size_t moves = 0; for(auto& o : opts) if(o.main && !o.end) moves++;
				Node n; n.st = std::move(st); n.h = promise(b, !m.summon.empty(), moves); n.parent = par;
				{ uint64_t t = 1469598103934665603ull; for(auto& l : n.st.labels) { for(char ch : l) t = (t ^ (unsigned char)ch) * 1099511628211ull; t = (t ^ 0xff) * 1099511628211ull; } n.tie = t; }   // FNV-1a of the line
				std::lock_guard<std::mutex> lk(omx); out.push_back(std::move(n));
				return;
			}
			std::vector<Opt> opts = (m.type == MSG_SELECT_CHAIN && m.chains.empty()) ? std::vector<Opt>{} : choices(m);
			if(m.type == MSG_SELECT_CHAIN && m.chains.empty()) { Opt o; o.label = "pass"; o.resp = p32(-1); opts.push_back(o); }
			zone_opts(m, st, opts); pick_order(m, st, opts);
			if(st.ending) ep_filter(m, opts);
			bool epOK = false;
			if(m.type == MSG_SELECT_IDLECMD) {        // the state we're expanding: ending the turn here is a result too
				record(*d, st);
				epOK = ep_worth(read_board(*d));       // ...and playing out its End Phase may be a better one
				if(st.actions >= maxActions && !epOK) return;
			}
			std::vector<const Opt*> live; for(auto& o : opts) if(!o.end || epOK) live.push_back(&o);
			// Card picks inside one action (which materials, which cards to send) multiply: several 15-way picks in one
			// action can cost a whole search. Like the beam itself, they widen with each pass: half the beam width
			// (4, 12, 36...), in a fixed scattered order, so early passes stay cheap and later ones see every pick.
			bool pickPrompt = m.type == MSG_SELECT_CARD || m.type == MSG_SELECT_UNSELECT_CARD || m.type == MSG_SELECT_SUM || m.type == MSG_SELECT_TRIBUTE;
			static const bool noCap = getenv("MDC_NOPICKCAP") != nullptr;
			static const size_t capMin = getenv("MDC_PICKMIN") ? (size_t)atoi(getenv("MDC_PICKMIN")) : 4;
			size_t pickCap = noCap ? (size_t)-1 : std::max<size_t>(capMin, beam_width / 2);
			if(pickPrompt && live.size() > pickCap) {
				auto h = [](const std::string& s) { uint64_t t = 1469598103934665603ull; for(char ch : s) t = (t ^ (unsigned char)ch) * 1099511628211ull; return t; };
				std::vector<const Opt*> keep, rest;
				for(auto* o : live) (o->label == "finish" ? keep : rest).push_back(o);
				std::stable_sort(rest.begin(), rest.end(), [&](const Opt* a, const Opt* b) { return h(a->label) < h(b->label); });
				for(auto* o : rest) { if(keep.size() >= pickCap) break; keep.push_back(o); }
				live.swap(keep); capped = true;
			}
			if(m.type == MSG_SELECT_IDLECMD && st.actions >= maxActions) { live.clear(); for(auto& o : opts) if(o.end) live.push_back(&o); }
			if(live.empty()) return;
			for(size_t i = 1; i < live.size(); i++) {
				if(out_of_time()) return;
				St s2 = st; take(s2, *live[i]);
				if(live[i]->main && m.type == MSG_SELECT_IDLECMD) s2.actions++;
				Prompt mm; bool ok; auto dd = replay(s2.path, mm, ok);
				if(ok) sub(std::move(dd), mm, std::move(s2), false, out, omx, par);
			}
			const Opt& o = *live[0];
			d->respond(o.resp); take(st, o);
			if(o.main && m.type == MSG_SELECT_IDLECMD) st.actions++;
			atStart = false;
			if(!d->run(m, retry)) return;
		}
	}
	std::vector<Node> expand_level(const std::vector<Node>& level) {
		std::vector<Node> out; std::mutex omx; std::atomic<size_t> next{0};
		auto work = [&] {
			for(;;) {
				size_t i = next++; if(i >= level.size() || out_of_time()) return;
				Prompt m; bool ok; auto d = replay(level[i].st.path, m, ok);
				if(ok) sub(std::move(d), m, level[i].st, true, out, omx, i);
			}
		};
		std::vector<std::thread> ts; int n = std::max(1, std::min<int>(beamThreads, (int)level.size()));
		for(int i = 0; i < n; i++) ts.emplace_back(work);
		for(auto& t : ts) t.join();
		return out;
	}
	std::atomic<bool> capped{false};   // this pass skipped some card picks (so it didn't check every line)
	int beamThreads = 1; bool beam_complete = false; size_t beam_width = 0; int beam_depth = 0;
	void beam() {
		for(size_t width = width0; !out_of_time(); width *= 3) {
			{ std::lock_guard<std::mutex> lk(vmx); bvisited.clear(); }
			capped = false;
			std::vector<Node> level(1);
			bool trimmed = false;
			beam_width = width;
			for(int depth = 0; depth <= maxActions * 4 && !level.empty() && !out_of_time(); depth++) {
				beam_depth = depth;
				auto tl = Clock::now(); uint64_t r0 = replays.load();
				std::vector<Node> next = expand_level(level);
				if(getenv("MDC_BEAMLOG2") && next.size() > 200) { for(size_t q = 0; q < 12; q++) { std::string t; auto& lb = next[q * next.size() / 12].st.labels; for(size_t k = 0; k < lb.size(); k++) t += lb[k] + " | "; fprintf(stderr, "  %s\n", t.c_str()); } }
				if(getenv("MDC_BEAMLOG")) fprintf(stderr, "width %zu depth %d: %zu nodes -> %zu children, %llu replays, %.1fs\n", width, depth, level.size(), next.size(), (unsigned long long)(replays.load() - r0), std::chrono::duration<double>(Clock::now() - tl).count());
				// Ties are broken by a hash of the line, not by which thread finished first: the same search keeps the same
				// states every time (reproducible for tuning) while ties still land in a scattered order (no bias by name).
				std::sort(next.begin(), next.end(), [](const Node& a, const Node& b) { return a.h != b.h ? a.h > b.h : a.tie < b.tie; });
				// MDC_TRACE=<file with a line's labels (JSON array)>: where that line's state ranks at each level (debugging
				// why the beam drops a known line).
				static const std::vector<std::string> trace = [] { std::vector<std::string> t; if(const char* f = getenv("MDC_TRACE")) { std::ifstream in(f); if(in) t = json::parse(in).get<std::vector<std::string>>(); } return t; }();
				if(!trace.empty()) {
					long rank = -1; size_t best = 0;
					for(size_t q = 0; q < next.size(); q++) { const auto& lb = next[q].st.labels; if(lb.size() <= trace.size() && lb.size() >= best && std::equal(lb.begin(), lb.end(), trace.begin())) { rank = (long)q; best = lb.size(); } }
					if(rank >= 0) fprintf(stderr, "trace w%zu d%d: rank %ld of %zu (h %.2f, top %.2f), %zu/%zu labels in, board %.2f\n", width, depth, rank, next.size(), next[rank].h, next.empty() ? 0 : next[0].h, best, trace.size(), next[rank].h);
					else fprintf(stderr, "trace w%zu d%d: line not among %zu children\n", width, depth, next.size());
					if(!next.empty() && depth <= 4) { std::string t; for(auto& l : next[0].st.labels) if(l.rfind("ns ", 0) == 0 || l.rfind("ss ", 0) == 0 || l.rfind("act ", 0) == 0 || l.rfind("chain ", 0) == 0) t += l + " | "; fprintf(stderr, "   top: %s\n", t.c_str()); }
				}
				if(next.size() > width) {
					trimmed = true;
					// States on the line to the best board found so far (by either search) are always kept: a wider pass
					// can't lose a line an earlier pass or a depth-first worker found, and it explores around it.
					std::vector<Node> elite;
					static const bool noElite = getenv("MDC_NOELITE") != nullptr;
					if(!noElite) { std::vector<Bytes> bp; { std::lock_guard<std::mutex> lk(mx); bp = bestPath; }
					  for(auto& n : next) if(!n.st.path.empty() && n.st.path.size() <= bp.size() && std::equal(n.st.path.begin(), n.st.path.end(), bp.begin())) elite.push_back(n); }
					if(diverse) {
						// Round-robin over parents (best child of each first) so one strong branch can't crowd out the rest.
						std::map<size_t, std::vector<Node*>> byParent; for(auto& n : next) byParent[n.parent].push_back(&n);
						std::vector<Node> keep;
						for(size_t r = 0; keep.size() < width; r++) {
							bool any = false;
							std::vector<Node*> round; for(auto& kv : byParent) if(r < kv.second.size()) { round.push_back(kv.second[r]); any = true; }
							if(!any) break;
							std::sort(round.begin(), round.end(), [](Node* a, Node* b) { return a->h != b->h ? a->h > b->h : a->tie < b->tie; });
							for(Node* n : round) { if(keep.size() >= width) break; keep.push_back(std::move(*n)); }
						}
						next.swap(keep);
					} else next.resize(width);
					for(auto& e : elite) { bool have = false; for(auto& n : next) if(n.st.path == e.st.path) { have = true; break; } if(!have) next.push_back(std::move(e)); }
				}
				level.swap(next);
			}
			if(!trimmed && !capped && !out_of_time()) { beam_complete = true; return; }   // nothing was cut: every line was checked
			if(width > 100000) return;
		}
	}
	void worker() {
		for(;;) {
			Task t;
			{
				std::unique_lock<std::mutex> lk(mx);
				cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return !queue.empty() || (busy == 0 && queue.empty()) || out_of_time(); });
				if(out_of_time()) { cv.notify_all(); return; }
				if(queue.empty()) { if(busy == 0) { cv.notify_all(); return; } continue; }
				t = std::move(queue.front()); queue.pop_front(); busy++;
			}
			Prompt m; bool ok; auto d = replay(t.path, m, ok);
			if(ok) dfs(std::move(d), m, std::move(t));
			{ std::lock_guard<std::mutex> lk(mx); busy--; }
			cv.notify_all();
		}
	}
	// Replay a line by its choice labels (as returned with "labels":true), recording each board along the way; stops where
	// a label no longer matches (the deck changed). Every free Main Phase point on it is added to `starts`.
	void seed_line(const std::vector<std::string>& labels, std::vector<St>& starts) {
		Prompt m; bool ok; auto d = replay({}, m, ok); if(!ok) return;
		St st; bool retry;
		for(size_t li = 0; li < labels.size();) {
			if(st.ending && m.type == MSG_SELECT_IDLECMD) break;   // the opponent's turn has started
			if(m.player == 1 && m.type != MSG_SELECT_IDLECMD) {
				bool hn; Opt o = opp_choice(m, st, hn); d->respond(o.resp); st.path.push_back(o.resp);
				if(!d->run(m, retry)) return;
				continue;
			}
			std::vector<Opt> opts = (m.type == MSG_SELECT_CHAIN && m.chains.empty()) ? std::vector<Opt>{} : choices(m);
			if(m.type == MSG_SELECT_CHAIN && m.chains.empty()) { Opt o; o.label = "pass"; o.resp = p32(-1); opts.push_back(o); }
			zone_opts(m, st, opts);
			const Opt* pick = nullptr; for(auto& o : opts) if(o.label == labels[li]) { pick = &o; break; }
			if(!pick) break;
			if(m.type == MSG_SELECT_IDLECMD) { record(*d, st); if(starts.size() < 64) starts.push_back(st); }
			Opt o = *pick;
			d->respond(o.resp); take(st, o);
			if(o.main && m.type == MSG_SELECT_IDLECMD) st.actions++;
			li++;
			if(!d->run(m, retry)) return;
		}
		if(m.type == MSG_SELECT_IDLECMD) record(*d, st);
	}

	// ---------------- the opponent's turn, played out (re-ranks the best boards) ----------------
	// Text scoring guesses what a board stops; this plays the opponent's turn for real against each finished board and
	// checks. The opponent is a fixed "probe" hand where each card stands for one kind of play:
	//   H  Evil HERO Adusted Gold: a monster effect from the hand that adds from the Deck (Dark Fusion)
	//   S  Reinforcement of the Army: a Spell that adds from the Deck (Celtic Guardian)
	//   N  Goblindbergh: a Normal Summon...   T  ...whose trigger Special Summons from the hand (Photon Chargeman)
	//   F  Photon Chargeman: a monster effect on the field (doubles its ATK)
	//   X  Gagaga Cowboy: an Extra Deck summon (Goblindbergh + Chargeman)
	// and a second wave that doesn't depend on the first (so a board's 3rd and 4th interruption still get a target after
	// the Goblindbergh line is stopped):
	//   S2 Pot of Greed, S3 Upstart Goblin: more Spells     M2 Photon Thrasher: a monster that comes when their field is empty
	//   C  Poison of the Old Man chained to their own Pot of Greed (a 2-link chain: what "in response to" cards like Zalen need)
	//   A  an attack with their first monster that can (battle effects: attack negates, Sunrise)
	//   XE Gagaga Cowboy's effect, right after it's summoned: an Extra Deck monster's effect (what Rindbrumm-style negates
	//      answer); stopped if the engine reports it negated / disabled, or Cowboy is gone before it resolves
	// None of them has a Quick Effect, so they can't act on our turn. Our side: every choice on their turn (chain or
	// pass, targets, options) is searched (bounded local search, see simulate), keeping what stops the most; one of our
	// own interruptions per opponent play (triggers and cards made during their turn aside). A play counts as stopped only if it was tried and its result didn't happen, checked on the field
	// (the added card isn't in hand, the monster isn't there, the ATK didn't double). Plays that never came because an
	// earlier one was stopped give no credit (otherwise negating the Normal Summon would score three plays).
	static constexpr uint32_t P_ADUSTED = 13650422, P_DFUSION = 94820406, P_REINFORCE = 32807846, P_CELTIC = 91152256,
		P_GOBLIN = 25259669, P_CHARGE = 2618045, P_COWBOY = 12014404, P_POT = 55144522, P_UPSTART = 70368879, P_THRASHER = 65367484, P_POISON = 8842266,
		P_DRNM = 54693926, P_DUSTER = 18144506, P_STORM = 14532163;
	static constexpr int PLAYS = 13, THREATS = 15;
	// The order they make their Main Phase plays (8, the attack, comes in battle). Scenario 1 ("breaker first") opens with
	// real board breakers, each only with a legal, worthwhile target: Dark Ruler No More (10) if we have a face-up monster,
	// Harpie's Feather Duster (11) if we have backrow, Lightning Storm (12) on whichever of our sides it hits harder.
	static constexpr int ORDER0[] = {0, 1, 2, 3, 4, 9, 5, 6, 7};
	static constexpr int ORDER1[] = {10, 11, 12, 0, 1, 2, 3, 4, 9, 5, 6, 7};   // plays they make (Goblindbergh's is two threats: the summon and its trigger)
	static constexpr const char* THREAT[THREATS] = {"monster effect in hand", "Spell", "Normal Summon", "summon trigger", "monster effect on field", "Extra Deck summon", "2nd Spell", "monster summoned from hand", "3rd Spell", "Quick-Play chained to their own Spell", "attack", "Extra Deck monster effect", "Dark Ruler No More", "Harpie's Feather Duster", "Lightning Storm"};
	struct SimDec { int step; bool act, quick; int pass; };   // one of our decisions: during which play, an activation (a Quick Effect, not a trigger), which option passes
	struct SimRun { bool ok = false; bool att[THREATS] = {}, stop[THREATS] = {}; int stopsAt[PLAYS] = {}; std::vector<std::pair<double, uint32_t>> credits; std::vector<int> creditPlay; int used = 0; std::vector<SimDec> decs; };
	static double card_value(uint32_t c) {   // what one of our cards' interruption is worth (its text value; 1 if the text missed it)
		auto it = g_cards.find(c); if(it == g_cards.end()) return 1.0;
		const CardEval& e = it->second.ev;
		double v = std::max({(double)e.field, (double)e.set, (double)e.hand, e.gy * 0.8, (double)e.onSummon});
		return v > 0 ? v : 1.0;
	}
	// An interruption that was never offered during their turn: was that because the probe never does what it waits for
	// (it stays at half), or because its own condition wasn't met (Tri-Brigade Mercourier needs a Fusion that mentions
	// "Fallen of Albaz"; Branded Retribution needs one to return)? The probe hand covers hand / Spell / field / Extra
	// Deck effects, Normal / Special / Extra Deck summons, searches, a 2-link chain and an attack, so a card waiting for
	// one of those that never got offered is dead (0). Only these aren't covered:
	static bool uncovered(uint32_t c) {
		auto it = g_cards.find(c); if(it == g_cards.end()) return true;
		const bool st = it->second.type & (TYPE_TRAP | TYPE_QUICKPLAY);
		// Only the card's interruptions count (Mercourier's "if this card is banished: search" isn't what stops them).
		for(const auto& fx : evalx::effects(evalx::lower(it->second.desc))) {
			const std::string& e = fx.text;
			if(evalx::hurt(e) <= 0 || !evalx::on_their_turn(e, st)) continue;
			for(const char* w : {"would destroy", "would be destroyed", "is destroyed", "targeted", "targets a", "targets 1", "5 or more", "summoned 5", "from the gy", "from their gy", "is banished", "are banished", "draw phase", "standby phase", "damage step", "battle damage"})
				if(e.find(w) != std::string::npos) return true;
		}
		return false;
	}
	static double falloff(std::vector<double> v) { std::sort(v.rbegin(), v.rend()); double s = 0, f = 1.0; for(double x : v) { s += x * f; f = std::max(0.5, f - 0.1); } return s; }
	Setup sim_setup(int scenario) const {
		Setup s = setup;
		s.oppHand = {P_ADUSTED, P_REINFORCE, P_GOBLIN, P_CHARGE, P_POT, P_THRASHER, P_UPSTART, P_POISON};
		if(scenario == 1) for(uint32_t c : {P_DRNM, P_DUSTER, P_STORM}) s.oppHand.push_back(c);
		s.oppDeck = {P_DFUSION, P_DFUSION, P_CELTIC, P_CELTIC}; for(int i = 0; i < 8; i++) s.oppDeck.push_back(DUMMY);
		s.oppExtra = {P_COWBOY};
		return s;
	}
	// One play-through. `forced` picks our options at each decision (index; past its end: the first option), and the
	// options seen are written to `counts` / `chosen` so the caller can walk every combination.
	bool sim_once(int scenario, const Found& f, const Setup& s, const std::vector<int>& forced, std::vector<int>& chosen, std::vector<int>& counts, std::set<uint32_t>& offered, SimRun& R) const {
		Duel d(s); if(!d.h) return false;
		std::vector<uint64_t> left; d.left = &left;
		Prompt m; bool retry;
		if(!d.run(m, retry)) return false;
		size_t mi = 0, bi = 0;
		int step = -1, actedAt = -2, orderAt = -1;          // the opponent's last play (index in the order of plays below), and the play we last used a card on
		bool tried[PLAYS] = {}, done[PLAYS] = {}, gobTrig = false, reached = false, snapped = false, poisoned = false, battled = false, attacked = false;
		uint32_t attacker = 0;
		std::vector<uint32_t> acts[PLAYS];    // our activations while each play was going on
		std::set<uint64_t> ours;              // our cards when their turn started, by copy (anything else came out during it)
		std::vector<char> actsOurs[PLAYS];
		std::set<uint32_t> usedNow;           // our cards already used this turn
		// What a card of ours is still worth keeping (for picking what to give up): spent this turn, little; a card that
		// can still act on their turn on its own (a Faimena in hand, a Cartesia that fuses), more than its text value.
		auto keep_value = [&](uint32_t c) -> double {
			if(usedNow.count(c)) return 0.2;
			auto it = g_cards.find(c); double v = card_value(c);
			if(it != g_cards.end() && (it->second.ev.fusionAt || it->second.ev.handExtender || it->second.ev.reviveAt)) v += 1.5;
			return v;
		};    // whether each activation was by a card we had (decided when it activated)
		struct Before { int dfusion = 0, celtic = 0, charge = 0, hand = 0, oppLp = 0, ourLp = 0, ourMons = 0; } before;   // what each play's success check compares against
		auto count = [&](uint32_t loc, uint32_t code) { int n = 0; for(auto& c : d.look(1, loc)) if(c.code == code && (loc != LOCATION_MZONE || (c.pos & POS_FACEUP))) n++; return n; };
		auto judge = [&](int k) {
			if(k < 0 || !tried[k] || done[k]) return; done[k] = true;
			int stops = 0;
			auto mark = [&](int t, bool stopped) { R.att[t] = true; R.stop[t] = stopped; stops += stopped; R.stopsAt[k] += stopped; };
			int hand = (int)d.look(1, LOCATION_HAND).size();
			switch(k) {
			case 0: mark(0, count(LOCATION_HAND, P_DFUSION) <= before.dfusion); break;
			case 1: mark(1, count(LOCATION_HAND, P_CELTIC) <= before.celtic); break;
			case 2: mark(2, count(LOCATION_MZONE, P_GOBLIN) == 0); if(gobTrig) mark(3, count(LOCATION_MZONE, P_CHARGE) <= before.charge); break;
			case 3: { bool doubled = false; for(auto& c : d.look(1, LOCATION_MZONE)) if(c.code == P_CHARGE && (c.pos & POS_FACEUP) && c.atk >= 2000) doubled = true; mark(4, !doubled); break; }
			case 4: mark(5, count(LOCATION_MZONE, P_COWBOY) == 0); break;
			case 5: mark(6, hand < before.hand + 1 - (poisoned ? 1 : 0)); if(poisoned) mark(9, d.lp[1] < before.oppLp + 1200); break;   // Pot: -1 (itself) +2 (and -1 for Poison chained to it); Poison: +1200 LP
			case 6: mark(7, count(LOCATION_MZONE, P_THRASHER) == 0); break;
			case 7: mark(8, hand < before.hand); break;       // Upstart: -1 +1
			case 8: {   // the attack: stopped only if we used a card on it and it did nothing (or the attacker is gone)
				bool gone = count(LOCATION_MZONE, attacker) == 0;
				bool nothing = d.lp[0] >= before.ourLp && (int)d.look(0, LOCATION_MZONE).size() >= before.ourMons;
				mark(10, !acts[8].empty() && (gone || nothing)); break; }
			case 10: case 11: case 12: { uint32_t bc = k == 10 ? P_DRNM : k == 11 ? P_DUSTER : P_STORM; bool neg = false;
				for(auto& x : d.negated) if(x.first == bc && x.second == 1) neg = true;
				mark(k + 2, neg); break; }   // a breaker is stopped only if it was negated (it has no other result to check)
			case 9: { bool neg = false; for(auto& x : d.negated) if(x.first == P_COWBOY && x.second == 1) neg = true;
				mark(11, neg || count(LOCATION_MZONE, P_COWBOY) == 0); break; }
			}
			// Credit: one per interruption, at its card's value, best first. A card that only came out during their turn
			// (the Shining Neos Wingman a Favorite Contact made) is part of the activation that brought it when both act
			// on the same play; on a later play it counts on its own (the RS a Remix made, negating later). One
			// interruption stopping two plays (destroying Goblindbergh before its trigger resolves) adds 0.3, not a 2nd card.
			std::vector<std::pair<double, uint32_t>> vals; int base = 0, made = 0;
			for(size_t i = 0; i < acts[k].size(); i++) { uint32_t c = acts[k][i]; vals.push_back({card_value(c), c}); (actsOurs[k][i] ? base : made)++; }
			std::sort(vals.rbegin(), vals.rend());
			int inter = base + (base == 0 ? made : 0);
			for(int i = 0; i < stops; i++) { R.credits.push_back(i < inter ? vals[i] : std::make_pair(0.3, 0u)); R.creditPlay.push_back(k); }
		};
		auto opp_default = [&](const Prompt& p) -> Bytes {
			switch(p.type) {
			case MSG_SELECT_CHAIN: {
				if(d.turnPlayer == 1) for(size_t i = 0; i < p.chains.size(); i++) if(p.chains[i].first == P_GOBLIN) { gobTrig = true; return p32((int)i); }
				if(d.turnPlayer == 1 && step == 5 && !poisoned) for(size_t i = 0; i < p.chains.size(); i++) if(p.chains[i].first == P_POISON) { poisoned = true; return p32((int)i); }
				return p32(p.forced && !p.chains.empty() ? 0 : -1);
			}
			case MSG_SELECT_EFFECTYN: if(d.turnPlayer == 1 && p.code == P_GOBLIN) gobTrig = true; return p32(d.turnPlayer == 1 ? 1 : 0);
			case MSG_SELECT_YESNO: return p32(d.turnPlayer == 1 ? 1 : 0);
			case MSG_SELECT_OPTION: {   // Lightning Storm: the side of ours it hurts more; Poison of the Old Man: gain the LP
				if(step == 12) {
					int atk = 0; for(auto& c : d.look(0, LOCATION_MZONE)) if(c.pos & POS_ATTACK) atk++;
					int st = (int)d.look(0, LOCATION_SZONE).size();
					for(size_t i = 0; i < p.options.size(); i++) { std::string t = evalx::lower(desc_text(p.options[i])); bool mons = t.find("monster") != std::string::npos;
						if(mons == (atk >= st)) return p32((int)i); }
					return p32(0);
				}
				for(size_t i = 0; i < p.options.size(); i++) { std::string t = evalx::lower(desc_text(p.options[i])); if(t.find("gain") != std::string::npos) return p32((int)i); }
				return p32(0);
			}
			case MSG_SELECT_BATTLECMD: {   // one attack with their first monster that can, then on to the End Phase
				judge(step);
				if(!attacked && !p.attackers.empty()) {
					attacked = true; step = 8; tried[8] = true; attacker = p.attackers[0].first;
					before.ourLp = d.lp[0]; before.ourMons = (int)d.look(0, LOCATION_MZONE).size();
					return p32((0 << 16) | 1);
				}
				return p32(p.to_ep ? 3 : 2);
			}
			case MSG_SELECT_CARD: case MSG_SELECT_TRIBUTE: {   // their picks: Chargeman for Goblindbergh, the two for the Xyz
				std::vector<uint32_t> idx;
				for(uint32_t want : {P_CHARGE, P_GOBLIN, P_CELTIC, P_DFUSION}) for(size_t i = 0; i < p.cards.size() && idx.size() < std::max<uint32_t>(p.mn, 1); i++)
					if(p.cards[i] == want && std::find(idx.begin(), idx.end(), (uint32_t)i) == idx.end()) idx.push_back((uint32_t)i);
				for(size_t i = 0; i < p.cards.size() && idx.size() < std::max<uint32_t>(p.mn, 1); i++) if(std::find(idx.begin(), idx.end(), (uint32_t)i) == idx.end()) idx.push_back((uint32_t)i);
				return r_cards(idx);
			}
			case MSG_SELECT_UNSELECT_CARD: {
				for(uint32_t want : {P_CHARGE, P_GOBLIN}) for(size_t i = 0; i < p.cards.size(); i++) if(p.cards[i] == want) return r_unselect((int)i);
				return r_unselect(p.finishable || p.cards.empty() ? -1 : 0);
			}
			default: { std::vector<Opt> c = choices(p); return c.empty() ? p32(0) : c[0].resp; }
			}
		};
		// Before our turn ends, Traps and Quick-Plays in hand get Set (the text score counts them as Set too).
		auto set_one = [&](const Prompt& p) -> int {
			for(size_t i = 0; i < p.sset.size(); i++) { auto it = g_cards.find(p.sset[i].code); if(it != g_cards.end() && ((it->second.type & TYPE_TRAP) || ((it->second.type & TYPE_SPELL) && (it->second.type & TYPE_QUICKPLAY)))) return (int)i; }
			return -1;
		};
		const Bytes endTurn = r_idle(7, 0);
		auto our_default = [&](const Prompt& p) -> Bytes {
			if(p.type == MSG_SELECT_IDLECMD) { int i = set_one(p); return i >= 0 ? r_idle(4, i) : endTurn; }   // the line is over: Set, then end the turn
			if(p.type == MSG_SELECT_CHAIN) return p32(p.forced && !p.chains.empty() ? 0 : -1);
			if(p.type == MSG_SELECT_EFFECTYN || p.type == MSG_SELECT_YESNO) return p32(1);
			std::vector<Opt> c = choices(p); return c.empty() ? p32(0) : c[0].resp;
		};
		for(int guard = 0; guard < 20000; guard++) {
			Bytes resp;
			bool theirTurn = d.turnPlayer == 1 && d.turns >= 2;
			if(theirTurn && !snapped) { snapped = true; for(uint32_t loc : {LOCATION_MZONE, LOCATION_SZONE, LOCATION_HAND, LOCATION_GRAVE, LOCATION_REMOVED}) for(auto& c : d.look(0, loc)) ours.insert(where_key(c.code, 0, loc, c.seq)); }
			// A card of ours that left its zone is gone: a copy that later lands in the same zone came out during their turn.
			if(snapped) for(uint64_t k : left) ours.erase(k);
			left.clear();
			if(theirTurn && d.phase >= PHASE_MAIN2) break;   // their Main Phase and battle are over
			bool live = theirTurn && (d.phase == PHASE_MAIN1 || (d.phase >= PHASE_BATTLE_START && d.phase < PHASE_MAIN2));   // their turn, where we decide
			if(m.player == 0 && mi < f.mine.size() && !(live && reached)) {   // our line
				// A prompt the line never saw (the opponent holding cards adds "look at their hand" options): answer it by
				// default and keep the line's next response for its own prompt.
				if(mi < f.mineType.size() && f.mineType[mi] && f.mineType[mi] != m.type) resp = our_default(m);
				else { int si = (!theirTurn && m.type == MSG_SELECT_IDLECMD && f.mine[mi] == endTurn) ? set_one(m) : -1;
					if(si >= 0) resp = r_idle(4, si);
					else {
						// By label where it's there (a card + effect, the cards picked): the options can come in another order
						// now (more End Phase effects once Traps were Set); otherwise the recorded bytes.
						resp = f.mine[mi]; bool found = false;
						if(mi < f.labels.size()) for(auto& o : choices(m)) if(o.label == f.labels[mi]) { resp = o.resp; found = true; break; }
						// A chain window that doesn't offer the line's card + effect is an extra one (a trigger from a card we Set
						// just now): pass it, keep the line's response for its own window.
						if(!found && m.type == MSG_SELECT_CHAIN && mi < f.labels.size() && f.labels[mi].rfind("chain ", 0) == 0) resp = our_default(m);
						else mi++;
					} }
			}
			else if(m.player == 1 && theirTurn && m.type == MSG_SELECT_IDLECMD) {
				reached = true;
				judge(step);
				// their next play: the first one still to come that they can make now
				int next = -1; Bytes r;
				const int* order = scenario == 1 ? ORDER1 : ORDER0; const int norder = scenario == 1 ? (int)(sizeof(ORDER1) / sizeof(int)) : (int)(sizeof(ORDER0) / sizeof(int));
				int ourFaceUp = 0, ourAtk = 0; for(auto& c : d.look(0, LOCATION_MZONE)) { if(c.pos & POS_FACEUP) ourFaceUp++; if(c.pos & POS_ATTACK) ourAtk++; }
				int ourBack = (int)d.look(0, LOCATION_SZONE).size();
				for(int oi = orderAt + 1; oi < norder && next < 0; oi++) {
					int k = order[oi];
					auto find = [&](const std::vector<IdleItem>& v, uint32_t code) { for(size_t i = 0; i < v.size(); i++) if(v[i].code == code) return (int)i; return -1; };
					int i = -1;
					if(k == 0 && (i = find(m.activate, P_ADUSTED)) >= 0) r = r_idle(5, i);
					else if(k == 1 && (i = find(m.activate, P_REINFORCE)) >= 0) r = r_idle(5, i);
					else if(k == 2 && (i = find(m.summon, P_GOBLIN)) >= 0) r = r_idle(0, i);
					else if(k == 3 && (i = find(m.activate, P_CHARGE)) >= 0) r = r_idle(5, i);
					else if(k == 4 && (i = find(m.spsummon, P_COWBOY)) >= 0) r = r_idle(1, i);
					else if(k == 5 && (i = find(m.activate, P_POT)) >= 0) r = r_idle(5, i);
					else if(k == 6 && (i = find(m.spsummon, P_THRASHER)) >= 0) r = r_idle(1, i);
					else if(k == 7 && (i = find(m.activate, P_UPSTART)) >= 0) r = r_idle(5, i);
					else if(k == 9 && (i = find(m.activate, P_COWBOY)) >= 0) r = r_idle(5, i);
					else if(k == 10 && ourFaceUp > 0 && (i = find(m.activate, P_DRNM)) >= 0) r = r_idle(5, i);
					else if(k == 11 && ourBack > 0 && (i = find(m.activate, P_DUSTER)) >= 0) r = r_idle(5, i);
					else if(k == 12 && (ourAtk > 0 || ourBack > 0) && (i = find(m.activate, P_STORM)) >= 0) r = r_idle(5, i);
					if(i >= 0) { next = k; orderAt = oi; }
				}
				if(next < 0) {   // their Main Phase plays are done: on to battle (once), else stop
					if(!battled && m.to_bp) { battled = true; resp = r_idle(6, 0); goto respond; }
					break;
				}
				step = next; tried[step] = true;
				before.dfusion = count(LOCATION_HAND, P_DFUSION); before.celtic = count(LOCATION_HAND, P_CELTIC); before.charge = count(LOCATION_MZONE, P_CHARGE);
				before.hand = (int)d.look(1, LOCATION_HAND).size(); before.oppLp = d.lp[1];
				resp = r;
			}
			else if(m.player == 1) resp = opp_default(m);
			else if(live) {
				// Our decision on their turn.
				std::vector<Opt> opts; std::vector<uint32_t> actCode; std::vector<uint64_t> actAt;   // actCode: the card each option activates (0 = none); actAt: which copy
				if(m.type == MSG_SELECT_CHAIN && !m.forced && !m.chains.empty()) {
					for(auto& c : m.chains) offered.insert(c.first);
					// One Quick Effect of ours per opponent play: stacking two negates on one play is never what a player wants,
					// and it keeps the search small. Triggers (the Wingman a Favorite Contact made destroying on summon, Kewl
					// Tune Mix destroying when used as material) are part of what's already going on: always offered. So are
					// cards that only work "in response to" an activation (Zalen negating the first link once we respond).
					auto chainer = [&](uint32_t c) { auto it = g_cards.find(c); return it != g_cards.end() && it->second.needsChain; };
					for(size_t i = 0; i < m.chains.size(); i++) if(actedAt != step || m.trig || chainer(m.chains[i].first)) { Opt o; o.resp = p32((int)i); opts.push_back(o); actCode.push_back(m.chains[i].first); actAt.push_back(i < m.chainAt.size() ? m.chainAt[i] : 0); }
					Opt pass; pass.resp = p32(-1); opts.push_back(pass); actCode.push_back(0); actAt.push_back(0);
				} else if(m.type == MSG_SELECT_EFFECTYN) {
					offered.insert(m.code);
					Opt y; y.resp = p32(1); Opt n; n.resp = p32(0);
					opts = {y, n}; actCode = {m.code, 0}; actAt = {m.codeAt, 0};
				} else if(m.type == MSG_SELECT_CARD || m.type == MSG_SELECT_UNSELECT_CARD || m.type == MSG_SELECT_OPTION || m.type == MSG_SELECT_SUM || m.type == MSG_SELECT_TRIBUTE) {
					// Targets / materials / options for our effects; the search tries the top three. Their cards first (what's
					// being stopped). Our own cards by what the pick is for (the game's hint): what we summon / add / Set, the
					// most valuable (the Fusion with the best effect); what we give up (materials, costs, Tributes, discards,
					// sending / banishing / destroying our own), the least valuable, so a Quick Fusion on their turn doesn't
					// eat the board's best monsters.
					opts = choices(m);
					const uint64_t hint = m.hint;
					const bool giveUp = hint == 500 || hint == 501 || hint == 502 || hint == 503 || hint == 504 || hint == 507 || (hint >= 511 && hint <= 513) || hint == 519;
					auto rank = [&](const Opt& o) { double v = 0; for(uint32_t c : o.picks) { size_t at = std::find(m.cards.begin(), m.cards.end(), c) - m.cards.begin();
						bool theirs = at < m.ccon.size() && m.ccon[at] == 1;
						if(theirs) v += 10; else v += giveUp ? -0.1 * keep_value(c) : 0.1 * card_value(c); }
						return o.label == "finish" ? (giveUp ? 100.0 : -100.0) : v; };   // giving up cards: stop as soon as allowed
					std::stable_sort(opts.begin(), opts.end(), [&](const Opt& a, const Opt& b) { return rank(a) > rank(b); });
					if(getenv("MDC_SIMLOG") && opts.size() > 3) { std::string all; for(auto& o : opts) all += " [" + o.label + "]"; fprintf(stderr, "  (all %zu picks:%s)\n", opts.size(), all.substr(0, 400).c_str()); }
					if(opts.size() > 3) opts.resize(3);
					actCode.assign(opts.size(), 0); actAt.assign(opts.size(), 0);
				}
				if(opts.size() > 1) {
					int pick = bi < forced.size() ? std::min<int>(forced[bi], (int)opts.size() - 1) : 0;
					bi++; counts.push_back((int)opts.size()); chosen.push_back(pick);
					int passAt = m.type == MSG_SELECT_CHAIN ? (int)opts.size() - 1 : m.type == MSG_SELECT_EFFECTYN ? 1 : -1;
					bool quick = m.type == MSG_SELECT_CHAIN && !m.trig;
					R.decs.push_back({step, actCode[pick] != 0, quick, passAt});
					if(actCode[pick]) { if(step >= 0) { acts[step].push_back(actCode[pick]); actsOurs[step].push_back(ours.count(actAt[pick]) > 0); } usedNow.insert(actCode[pick]); if(quick) actedAt = step; R.used++; }
					static const bool slog = getenv("MDC_SIMLOG") != nullptr;
					if(slog) { std::string ol; if(!actCode[pick]) for(auto& o : opts) ol += " [" + o.label + "]";
						fprintf(stderr, "  dec#%zu play %d type %d: %d of %zu%s%s\n", bi - 1, step, m.type, pick, opts.size(), actCode[pick] ? (" activates " + card_name(actCode[pick])).c_str() : "", ol.substr(0, 160).c_str()); }
					resp = opts[pick].resp;
				} else resp = opts.empty() ? our_default(m) : opts[0].resp;
			}
			else {
				if(theirTurn && m.type == MSG_SELECT_CHAIN) for(auto& c : m.chains) offered.insert(c.first);
				resp = our_default(m);
			}
		respond:
			d.respond(resp);
			if(!d.run(m, retry)) {
				if(retry || !reached) { if(getenv("MDC_SIMLOG")) fprintf(stderr, "sim fail: %s at mine %zu/%zu, turn %d (player %d) phase %d, prompt type %d for player %d\n", retry ? "invalid response" : "duel ended", mi, f.mine.size(), d.turns, d.turnPlayer, d.phase, m.type, m.player);
					if(getenv("MDC_SIMLOG")) { for(size_t q = mi >= 4 ? mi - 4 : 0; q < mi && q < f.labels.size(); q++) fprintf(stderr, "   line: %s\n", f.labels[q].c_str());
						for(uint64_t o : m.options) fprintf(stderr, "   option now: %s\n", desc_text(o).c_str()); }
					return false; }
				break;
			}
		}
		if(!reached) { if(getenv("MDC_SIMLOG")) fprintf(stderr, "sim fail: never reached their Main Phase (mine %zu/%zu, turn %d phase %d, last prompt type %d player %d)\n", mi, f.mine.size(), d.turns, d.phase, m.type, m.player); return false; }
		judge(step);
		R.ok = true;
		return true;
	}
	// Re-score a finished board by playing their turn: the text score's stops are replaced by what actually stopped
	// something (credited at the stopping card's value); cards that never got a chance to act (their trigger isn't one
	// of the probe plays: Nibiru, battle effects) keep half their text value; cards that had the chance and stopped
	// nothing count 0.
	// One scenario's best play-through for our side (see the local search below); < -1e8: the line didn't replay.
	double sim_scenario(int scenario, const Found& f, std::set<uint32_t>& offered, int& runs, SimRun& best) const {
		const Setup s = sim_setup(scenario);
		static const int budget = getenv("MDC_SIMRUNS") ? atoi(getenv("MDC_SIMRUNS")) : 150;
		const int runs0 = runs;
		// Our choices on their turn, by local search: start from "use everything as soon as it can be used", then try
		// changing one decision at a time (pass instead, another card, another target), replaying the rest greedily, and
		// keep a change when it stops more (or the same with fewer cards used). Sweeps repeat until nothing improves.
		// This drops interruptions wasted on plays they can't stop, and keeps setup plays (a Remix that makes the RS
		// that negates later): dropping those lowers the total, so that change is rejected.
		auto play = [&](const std::vector<int>& forced, std::vector<int>& chosen, std::vector<int>& counts, SimRun& R) -> double {
			chosen.clear(); counts.clear(); runs++;
			if(!sim_once(scenario, f, s, forced, chosen, counts, offered, R)) return -1e9;   // the line didn't replay
			std::vector<double> v; for(auto& c : R.credits) v.push_back(c.first);
			return falloff(v) - 0.01 * R.used;   // equal stops: fewer cards used is better
		};
		std::vector<int> bestChosen, bestCounts;
		double bestV = play({}, bestChosen, bestCounts, best);
		if(bestV < -1e8) return bestV;
		// Waste removal first: Quick Effects used on a play beyond the number of plays stopped there (stacked on one play,
		// or used where they stopped nothing, or before they did anything) all become passes at once, the rest replayed
		// greedily, while that doesn't lower the total. Changing one at a time can't do this: passing one wasted card
		// just wastes it on the next play, same total. Triggers aren't touched (a Wingman's destroy is how its Favorite
		// Contact stops something).
		for(int it = 0; it < 8 && runs - runs0 < budget; it++) {
			std::vector<int> forced; size_t last = 0; bool any = false; int seen[PLAYS] = {};
			for(size_t i = 0; i < bestChosen.size() && i < best.decs.size(); i++) {
				const SimDec& dc = best.decs[i];
				bool wasted = dc.act && dc.quick && dc.pass >= 0 && (dc.step < 0 || seen[dc.step]++ >= best.stopsAt[dc.step]);
				forced.push_back(wasted ? dc.pass : bestChosen[i]);
				if(wasted) { any = true; last = i; }
			}
			if(!any) break;
			forced.resize(last + 1);
			std::vector<int> ch, cn; SimRun R;
			double v = play(forced, ch, cn, R);
			if(getenv("MDC_SIMLOG")) fprintf(stderr, "waste pass %d: %.2f (best %.2f)\n", it, v, bestV);
			if(v < bestV - 1e-9) break;
			bestV = v; best = R; bestChosen = ch; bestCounts = cn;
		}
		for(bool improved = true; improved && runs - runs0 < budget;) {
			improved = false;
			for(size_t i = 0; i < bestChosen.size() && runs - runs0 < budget && !improved; i++)
				for(int alt = 0; alt < bestCounts[i] && runs - runs0 < budget; alt++) {
					if(alt == bestChosen[i]) continue;
					std::vector<int> forced(bestChosen.begin(), bestChosen.begin() + i); forced.push_back(alt);
					std::vector<int> ch, cn; SimRun R;
					double v = play(forced, ch, cn, R);
					if(v > bestV + 1e-9) { bestV = v; best = R; bestChosen = ch; bestCounts = cn; improved = true; break; }
				}
		}
		return bestV;
	}
	// Both scenarios (their normal plays; breaker first), averaged.
	json simulate(const Found& f, double& out) const {
		std::set<uint32_t> offered; int runs = 0;
		SimRun best, brk;
		if(sim_scenario(0, f, offered, runs, best) < -1e8) { out = f.score; return {{"ok", false}, {"runs", runs}}; }
		bool brkOk = sim_scenario(1, f, offered, runs, brk) > -1e8;
		json why = json::object(); double text = score_of(f.board, &why);
		double textStops = 0; std::vector<double> unt; json untested = json::array();
		if(why.contains("stops")) for(auto& st : why["stops"]) {
			textStops += st["counts"].get<double>();
			uint32_t c = st.value("code", 0u);
			if(c && !offered.count(c)) { double v = uncovered(c) ? st["value"].get<double>() * 0.5 : 0; unt.push_back(v); untested.push_back({{"card", c}, {"value", v}}); }
		}
		static const char* PLAY[PLAYS] = {"monster effect in hand", "Spell", "Normal Summon + trigger", "monster effect on field", "Extra Deck summon", "2nd Spell", "monster summoned from hand", "3rd Spell", "attack", "Extra Deck monster effect", "Dark Ruler No More", "Harpie's Feather Duster", "Lightning Storm"};
		auto stops = [&](const SimRun& r, json& credits) { std::vector<double> v = unt;
			for(size_t i = 0; i < r.credits.size(); i++) { const auto& c = r.credits[i]; v.push_back(c.first);
				credits.push_back({{"card", c.second}, {"value", c.first}, {"play", i < r.creditPlay.size() ? PLAY[r.creditPlay[i]] : ""}}); }
			return falloff(v); };
		auto plays = [&](const SimRun& r) { json th = json::array(); for(int t = 0; t < THREATS; t++) if(r.att[t] || t < 12) th.push_back({{"play", THREAT[t]}, {"tried", r.att[t]}, {"stopped", r.stop[t]}}); return th; };
		json credits = json::array(), bcredits = json::array();
		double s0 = stops(best, credits), s1 = brkOk ? stops(brk, bcredits) : s0;
		out = text - textStops + 0.5 * (s0 + s1);
		return {{"ok", true}, {"text", text}, {"plays", plays(best)}, {"credits", credits}, {"untested", untested}, {"runs", runs},
			{"normal", s0}, {"breaker", {{"ok", brkOk}, {"stops", s1}, {"plays", brkOk ? plays(brk) : json::array()}, {"credits", bcredits}}}};
	}
	bool simOn = false;
	double beamShare = 0.5;

	json run(int id) {
		t0 = Clock::now();
		std::vector<std::thread> ts;
		bool useBeam = !interrupting() && mode != "dfs";
		// Normal searches run both: a widening beam (fair to every early choice) and depth-first workers
		// (quick to find long lines). They share what they find.
		// beamShare: the beam's share of the threads (default half). Which split is best depends on the deck: beam-only
		// finds HERO's best boards twice as fast (Stratos + Faris 10.59 at 19 s vs 38 s), depth-first workers find some
		// long Fallen of the White Dragon lines; in-between splits weren't better than either. The app alternates.
		int dfsThreads = !useBeam ? std::max(1, threads) : mode == "beam" ? 0 : std::max(1, (int)std::lround(threads * (1.0 - beamShare)));
		beamThreads = std::max(1, threads - dfsThreads);
		if(useBeam) ts.emplace_back([this] { beam(); });
		// Seeds (the best lines earlier searches found for this hand) are replayed first: their boards are recorded (so this
		// search can't end worse), the best one becomes the beam's kept line, and the depth-first workers also start from
		// points along them.
		std::vector<St> seedStarts;
		if(!interrupting()) for(const auto& sd : seeds) seed_line(sd, seedStarts);
		if(dfsThreads) { queue.push_back(St{}); for(auto& st : seedStarts) queue.push_back(st); for(int i = 0; i < dfsThreads; i++) ts.emplace_back([this] { worker(); }); }
		std::atomic<bool> finished{false};
		std::mutex pmx; std::condition_variable pcv;
		std::thread prog([&] {
			while(!finished.load()) {
				{ std::unique_lock<std::mutex> lk(pmx); pcv.wait_for(lk, std::chrono::milliseconds(500), [&] { return finished.load(); }); }
				if(finished.load()) break;
				size_t nb; { std::lock_guard<std::mutex> lk(mx); nb = boards.size(); }
				emit({{"id", id}, {"progress", {{"replays", replays.load()}, {"states", visited.size()}, {"boards", nb}, {"seconds", std::chrono::duration<double>(Clock::now() - t0).count()}}}});
			}
		});
		for(auto& t : ts) t.join();
		{ std::lock_guard<std::mutex> lk(pmx); finished = true; } pcv.notify_all(); prog.join();
		double secs = std::chrono::duration<double>(Clock::now() - t0).count();
		std::vector<Found> all; for(auto& kv : boards) all.push_back(kv.second);
		std::sort(all.begin(), all.end(), [](const Found& a, const Found& b) { return a.score != b.score ? a.score > b.score : a.steps.size() < b.steps.size(); });
		json res = json::array();
		std::set<std::string> fieldSeen;
		std::vector<Found> picked;
		for(const Found& f : all) {
			std::string fk = key_of(f.board.mzone) + "|" + key_of(f.board.szone);
			if(!fieldSeen.insert(fk).second) continue;   // same field, different hand: keep the best one only
			picked.push_back(f);
			if(picked.size() >= top) break;
		}
		// Play the opponent's turn against each picked board (in parallel) and rank them by what really stops them.
		std::vector<json> sims(picked.size());
		double simSecs = 0;
		if(simOn && !interrupting() && !picked.empty()) {
			auto ts0 = Clock::now();
			std::atomic<size_t> nx{0}; std::vector<std::thread> st;
			int simThreads = getenv("MDC_SIMLOG") ? 1 : std::max(1, std::min<int>(threads, (int)picked.size()));   // one at a time when logging
			for(int i = 0; i < simThreads; i++) st.emplace_back([&] {
				for(size_t k; (k = nx++) < picked.size();) { if(getenv("MDC_SIMLOG")) fprintf(stderr, "=== board %zu\n", k);
					double v = picked[k].score; json j = simulate(picked[k], v); j["textScore"] = picked[k].score; picked[k].score = v; sims[k] = j; }
			});
			for(auto& t : st) t.join();
			// A board whose line didn't replay keeps its text score, which runs higher than simulated ones: scale it by the
			// typical simulated/text ratio of this search's other boards so it isn't ranked up just for failing.
			std::vector<double> ratios; for(size_t k = 0; k < picked.size(); k++) if(sims[k].value("ok", false) && sims[k]["textScore"].get<double>() > 0) ratios.push_back(picked[k].score / sims[k]["textScore"].get<double>());
			if(!ratios.empty()) { std::sort(ratios.begin(), ratios.end()); double r = ratios[ratios.size() / 2];
				for(size_t k = 0; k < picked.size(); k++) if(!sims[k].value("ok", false)) { picked[k].score *= r; sims[k]["scaled"] = r; } }
			std::vector<size_t> order(picked.size()); for(size_t i = 0; i < order.size(); i++) order[i] = i;
			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return picked[a].score > picked[b].score; });
			std::vector<Found> p2; std::vector<json> s2; for(size_t i : order) { p2.push_back(picked[i]); s2.push_back(sims[i]); }
			picked.swap(p2); sims.swap(s2);
			simSecs = std::chrono::duration<double>(Clock::now() - ts0).count();
		}
		for(size_t pi = 0; pi < picked.size(); pi++) {
			const Found& f = picked[pi];
			// Drawn cards are unknown: leave the stand-ins out of the board, and show them as 0 ("a drawn card") in steps.
			auto known = [](std::vector<uint32_t> v) { v.erase(std::remove(v.begin(), v.end(), BLANK), v.end()); return v; };
			auto mask = [](std::vector<uint32_t> v) { for(auto& c : v) if(c == BLANK) c = 0; return v; };
			json steps = json::array();
			for(const Step& s : f.steps) { json g = json::array(); for(auto& gr : s.groups) g.push_back({gr.first, mask(gr.second)});
				steps.push_back({{"do", s.kind}, {"card", s.card == BLANK ? 0 : s.card}, {"effect", s.effect}, {"picks", mask(s.picks)}, {"groups", g}}); }
			json slots = json::array(); for(int z : f.board.mslot) slots.push_back(z);
			json bj = {{"score", f.score}, {"field", f.board.mzone}, {"zones", slots}, {"backrow", f.board.szone}, {"hand", known(f.board.hand)}, {"gy", known(f.board.grave)}, {"banished", known(f.board.banished)}, {"steps", steps}};
			if(wantLabels) bj["labels"] = f.labels;
			if(!sims[pi].is_null()) bj["sim"] = sims[pi];
			res.push_back(bj);
		}
		bool complete = useBeam ? beam_complete : (!stop.load() && !stoppedStable.load() && secs <= timeLimit);
		// "stable": every line was checked, or the best board stopped improving well before the end (the last 40% of the
		// time, or the stable-stop share when that's on).
		bool stable = complete || stoppedStable.load() || (secs - bestAt.load() >= std::max(stableFrac, 0.4) * secs);
		return {{"id", id}, {"done", true}, {"complete", complete}, {"stable", stable}, {"boards", res}, {"stats", {{"replays", replays.load()}, {"states", visited.size()}, {"endBoards", boards.size()}, {"seconds", secs}, {"bestAt", bestAt.load()}, {"stoppedStable", stoppedStable.load()}, {"width", beam_width}, {"depth", beam_depth}, {"simSeconds", simSecs}}}};
	}
};

// ------------------------------------------------------------------ main loop
static std::unique_ptr<Search> g_search;
static std::thread g_search_thread;
static std::mutex g_search_mx;

static std::vector<uint32_t> ids(const json& j, const char* k) { std::vector<uint32_t> v; if(j.contains(k) && j[k].is_array()) for(auto& x : j[k]) v.push_back(x.get<uint32_t>()); return v; }

int main(int argc, char** argv) {
	std::ios::sync_with_stdio(false);
	bool ready = false;
	if(argc > 1 && std::string(argv[1]) == "--version") { int a, b; OCG_GetVersion(&a, &b); std::cout << "combo-engine 1.0 (ocgcore " << a << "." << b << ")\n"; return 0; }
	std::string line;
	while(std::getline(std::cin, line)) {
		if(line.empty()) continue;
		json req; try { req = json::parse(line); } catch(...) { emit({{"error", "bad json"}}); continue; }
		int id = req.value("id", 0);
		std::string cmd = req.value("cmd", "");
		if(cmd == "quit") break;
		if(cmd == "stop") { std::lock_guard<std::mutex> lk(g_search_mx); if(g_search) g_search->stop = true; continue; }
		if(cmd == "init") {
			std::string err;
			pool_flush();   // pooled duels hold scripts from the old data
			g_cards.clear();
			if(!load_cdb(req.value("cdb", ""), err) || !open_scripts(req.value("scripts", ""), err)) { emit({{"id", id}, {"error", err}}); continue; }
			ready = true;
			emit({{"id", id}, {"ready", true}, {"cards", g_cards.size()}, {"scripts", g_zip_index.size()}});
			continue;
		}
		if(cmd == "score") {   // score any board and say why: {"field":[..],"zones":[..],"backrow","hand","gy","banished","extra","targets"}
			Search sc; for(uint32_t t : ids(req, "targets")) sc.targets.insert(t);
			Board b; b.mzone = ids(req, "field"); b.szone = ids(req, "backrow"); b.hand = ids(req, "hand");
			b.grave = ids(req, "gy"); b.banished = ids(req, "banished"); b.extra = ids(req, "extra");
			if(req.contains("zones") && req["zones"].is_array()) for(auto& z : req["zones"]) b.mslot.push_back(z.get<int>());
			json why = json::object(); sc.score_of(b, &why);
			emit({{"id", id}, {"score", why}}); continue;
		}
		if(cmd == "simboard") {   // play the opponent's turn against a given board: {deck, extra, field, zones, backrow, hand, gy, banished, targets}
			if(!ready) { emit({{"id", id}, {"error", "not initialized"}}); continue; }
			Search sc; for(uint32_t t : ids(req, "targets")) sc.targets.insert(t);
			Board b; b.mzone = ids(req, "field"); b.szone = ids(req, "backrow"); b.hand = ids(req, "hand"); b.grave = ids(req, "gy"); b.banished = ids(req, "banished");
			std::vector<uint32_t> deck = ids(req, "deck"), extra = ids(req, "extra");
			auto take = [](std::vector<uint32_t>& from, uint32_t c) { auto it = std::find(from.begin(), from.end(), c); if(it != from.end()) { from.erase(it); return true; } return false; };
			for(const auto* v : {&b.mzone, &b.szone, &b.hand, &b.grave, &b.banished}) for(uint32_t c : *v) if(!take(deck, c)) take(extra, c);   // the board's cards come out of the Deck / Extra Deck
			b.extra = extra;
			if(req.contains("zones") && req["zones"].is_array()) for(auto& z : req["zones"]) { b.mslot.push_back(z.get<int>()); sc.setup.preZones.push_back(z.get<int>()); }
			sc.setup.deck = deck; sc.setup.extra = extra; sc.setup.hand = b.hand;
			sc.setup.preField = b.mzone; sc.setup.preBack = b.szone; sc.setup.preGy = b.grave; sc.setup.preBanished = b.banished;
			Found f; f.board = b; f.score = sc.score_of(b);
			double v = f.score; json j = sc.simulate(f, v); j["textScore"] = f.score;
			emit({{"id", id}, {"score", v}, {"sim", j}}); continue;
		}
		if(cmd == "eval") {   // what the board evaluator reads from each card (for checking src/evaluate.h)
			json out = json::object();
			for(uint32_t c : ids(req, "cards")) { auto it = g_cards.find(c); if(it == g_cards.end()) continue; const CardEval& e = it->second.ev;
				out[std::to_string(c)] = {{"name", it->second.name}, {"field", e.field}, {"set", e.set}, {"hand", e.hand}, {"gy", e.gy}, {"lock", e.lock}, {"sturdy", e.sturdy}};
				if(e.reviveAt) out[std::to_string(c)]["revive"] = {{"from", e.reviveAt}, {"tag", e.reviveTag}, {"maxLevel", e.reviveMaxLv}};
				if(e.onSummon > 0) out[std::to_string(c)]["onSummon"] = e.onSummon;
				if(e.fusionAt) out[std::to_string(c)]["fusion"] = {{"from", e.fusionAt}, {"materialsFrom", e.fusionFrom}, {"tag", e.fusionTag}};
				if(!e.mats.empty()) { json ms = json::array(); for(const auto& m : e.mats) ms.push_back({{"tag", m.tag}, {"exact", m.exact}, {"fusion", m.fusion}, {"n", m.n}}); out[std::to_string(c)]["materials"] = ms; }
				if(!e.needs.empty()) { json ms = json::array(); for(const auto& nd : e.needs) ms.push_back({{"where", nd.where == 2 ? "gy" : "field"}, {"n", nd.n}, {"tag", nd.m.tag}, {"mentions", nd.m.mentions}, {"kinds", nd.m.kinds}, {"attr", nd.m.attr}, {"race", nd.m.race}}); out[std::to_string(c)]["needs"] = ms; } }
			emit({{"id", id}, {"eval", out}}); continue;
		}
		if(cmd == "search") {
			if(!ready) { emit({{"id", id}, {"error", "not initialized"}}); continue; }
			{ std::lock_guard<std::mutex> lk(g_search_mx); if(g_search) g_search->stop = true; }  // a new search replaces the old one
			if(g_search_thread.joinable()) g_search_thread.join();
			auto s = std::make_unique<Search>();
			s->setup.hand = ids(req, "hand");
			s->setup.extra = ids(req, "extra");
			std::vector<uint32_t> deck = ids(req, "deck");
			for(uint32_t c : s->setup.hand) { auto it = std::find(deck.begin(), deck.end(), c); if(it != deck.end()) deck.erase(it); }
			s->setup.deck = deck;
			s->maxActions = req.value("maxActions", 8);
			s->timeLimit = req.value("timeMs", 20000) / 1000.0;
			s->stableFrac = req.value("stable", 0.0); s->stableMin = req.value("stableMinMs", 5000) / 1000.0;
			s->threads = std::max(1, std::min(16, req.value("threads", 2)));
			s->top = (size_t)req.value("top", 12);
			for(uint32_t t : ids(req, "targets")) s->targets.insert(t);
			s->setup.oppHand = ids(req, "oppHand");
			s->wantLabels = req.value("labels", false);
			s->simOn = req.value("sim", false);
			s->beamShare = std::min(1.0, std::max(0.0, req.value("beamShare", 0.5)));
			if(req.contains("seeds") && req["seeds"].is_array()) for(auto& sd : req["seeds"]) if(sd.is_array()) { std::vector<std::string> v; for(auto& l : sd) if(l.is_string()) v.push_back(l.get<std::string>()); if(!v.empty()) s->seeds.push_back(v); }
			s->mode = req.value("mode", std::string());
			{
				// Branch on zones only if some card in the deck talks about zones or columns.
				bool z = false;
				for(const auto* v : {&s->setup.deck, &s->setup.extra, &s->setup.hand}) for(uint32_t c : *v) {
					auto it = g_cards.find(c); if(it == g_cards.end()) continue;
					const std::string& d = it->second.desc;
					if(d.find("center Main Monster Zone") != std::string::npos || d.find(" column") != std::string::npos || d.find("adjacent") != std::string::npos) { z = true; break; }
				}
				g_zones = req.value("zones", z);
			}
			if(req.value("reuse", true) != g_reuse) { pool_flush(); g_reuse = req.value("reuse", true); }
			s->diverse = req.value("diverse", true);
			s->width0 = (size_t)std::max(2, req.value("width", 8));
			if(req.contains("w") && req["w"].is_array() && req["w"].size() == 4) { s->W_MOVES = req["w"][0]; s->W_HAND = req["w"][1]; s->W_NS = req["w"][2]; s->W_GY = req["w"][3]; }
			s->hitCard = req.value("hitCard", 0u);
			s->hitStep = req.value("hitStep", -1);
			if(req.contains("prefix") && req["prefix"].is_array()) for(auto& x : req["prefix"]) s->prefix.push_back(x.get<std::string>());
			std::vector<uint32_t> missing;
			for(uint32_t c : s->setup.hand) {
				auto ci = g_cards.find(c);
				bool vanilla = ci != g_cards.end() && (ci->second.type & 0x10) && !(ci->second.type & 0x20);  // Normal monsters need no script
				if(ci == g_cards.end() || (!vanilla && g_zip_open && !g_zip_index.count("c" + std::to_string(c) + ".lua"))) missing.push_back(c);
			}
			{ std::lock_guard<std::mutex> lk(g_search_mx); g_search = std::move(s); }
			Search* sp = g_search.get();
			if(!missing.empty()) emit({{"id", id}, {"warning", "not scripted yet"}, {"cards", missing}});
			g_search_thread = std::thread([sp, id] { json r = sp->run(id); emit(r); });
			continue;
		}
		emit({{"id", id}, {"error", "unknown command"}});
	}
	{ std::lock_guard<std::mutex> lk(g_search_mx); if(g_search) g_search->stop = true; }
	if(g_search_thread.joinable()) g_search_thread.join();
	return 0;
}
