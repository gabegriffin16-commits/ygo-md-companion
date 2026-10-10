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
	std::string name;
	std::vector<std::string> strs;
};
static std::unordered_map<uint32_t, CardRow> g_cards;

static bool load_cdb(const std::string& path, std::string& err) {
	sqlite3* db = nullptr;
	if(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) { err = "can't open card database"; if(db) sqlite3_close(db); return false; }
	sqlite3_stmt* st = nullptr;
	const char* q = "select d.id,d.alias,d.setcode,d.type,d.atk,d.def,d.level,d.race,d.attribute,t.name,"
		"t.str1,t.str2,t.str3,t.str4,t.str5,t.str6,t.str7,t.str8,t.str9,t.str10,t.str11,t.str12,t.str13,t.str14,t.str15,t.str16 "
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
		if(nm) r.name = (const char*)nm;
		for(int i = 0; i < 16; i++) { const unsigned char* s = sqlite3_column_text(st, 10 + i); r.strs.push_back(s ? (const char*)s : ""); }
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
		if(luaL_loadbuffer(L, src.data(), src.size(), ("@" + name).c_str()) == LUA_OK) lua_dump(L, dump_writer, &bc, 0);
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
struct Prompt {
	int type = 0; uint8_t player = 0;
	std::vector<IdleItem> summon, spsummon, activate;
	bool to_ep = false;
	uint32_t code = 0; uint64_t desc = 0;
	std::vector<uint64_t> options;
	uint8_t cancelable = 0, finishable = 0; uint32_t mn = 0, mx = 0;
	std::vector<uint32_t> cards;         // SELECT_CARD / TRIBUTE / UNSELECT (select list) / SUM cards
	std::vector<uint32_t> cardParam;     // SUM: per-card value
	std::vector<uint32_t> mustParam; uint32_t acc = 0;
	std::vector<std::pair<uint32_t, uint64_t>> chains;
	uint8_t forced = 0;
	uint8_t count = 0; uint32_t flag = 0; uint8_t positions = 0;
	uint64_t available = 0;
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
		lst(&m.summon, false); lst(&m.spsummon, false); lst(nullptr, true); lst(nullptr, false); lst(nullptr, false);
		uint32_t k = r.u32();
		for(uint32_t i = 0; i < k; i++) { IdleItem it; it.code = r.u32(); r.u8(); r.u8(); r.u32(); it.desc = r.u64(); r.u8(); m.activate.push_back(it); }
		r.u8(); m.to_ep = r.u8() != 0; r.u8();
		break;
	}
	case MSG_SELECT_EFFECTYN: m.code = r.u32(); rloc(r); m.desc = r.u64(); break;
	case MSG_SELECT_YESNO: m.desc = r.u64(); break;
	case MSG_SELECT_OPTION: { uint8_t k = r.u8(); for(int i = 0; i < k; i++) m.options.push_back(r.u64()); break; }
	case MSG_SELECT_CARD: case MSG_SELECT_TRIBUTE: {
		m.cancelable = r.u8(); m.mn = r.u32(); m.mx = r.u32();
		uint32_t k = r.u32();
		for(uint32_t i = 0; i < k; i++) {
			if(t == MSG_SELECT_CARD) { m.cards.push_back(r.u32()); rloc(r); }
			else { m.cards.push_back(r.u32()); r.u8(); r.u8(); r.u32(); r.u8(); }
		}
		break;
	}
	case MSG_SELECT_UNSELECT_CARD: {
		m.finishable = r.u8(); m.cancelable = r.u8(); m.mn = r.u32(); m.mx = r.u32();
		uint32_t k = r.u32(); for(uint32_t i = 0; i < k; i++) { m.cards.push_back(r.u32()); rloc(r); }
		k = r.u32(); for(uint32_t i = 0; i < k; i++) { r.u32(); rloc(r); }
		break;
	}
	case MSG_SELECT_CHAIN: {
		r.u8(); m.forced = r.u8(); r.u32(); r.u32();
		uint32_t k = r.u32();
		for(uint32_t i = 0; i < k; i++) { uint32_t c = r.u32(); rloc(r); uint64_t d = r.u64(); r.u8(); m.chains.push_back({c, d}); }
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
struct Setup { std::vector<uint32_t> deck, extra, hand; };
static const uint32_t DUMMY = 46986414;   // opponent's deck: Dark Magician x5, they never act on our turn

struct Duel {
	OCG_Duel h = nullptr;
	explicit Duel(const Setup& s) {
		OCG_DuelOptions o{};
		o.seed[0] = 1; o.seed[1] = 2; o.seed[2] = 3; o.seed[3] = 4;
		o.flags = DUEL_MODE_MR5 | DUEL_SIMPLE_AI | DUEL_PSEUDO_SHUFFLE;
		o.team1 = {8000, 0, 1}; o.team2 = {8000, 0, 1};
		o.cardReader = card_reader; o.scriptReader = script_reader; o.logHandler = log_handler; o.cardReaderDone = card_reader_done;
		if(OCG_CreateDuel(&h, &o) != OCG_DUEL_CREATION_SUCCESS) { h = nullptr; return; }
		for(const char* f : {"constant.lua", "utility.lua"}) { const std::string* b = get_script(f); if(!b || !OCG_LoadScript(h, b->data(), (uint32_t)b->size(), f)) { OCG_DestroyDuel(h); h = nullptr; return; } }
		auto add = [&](uint8_t team, uint32_t code, uint32_t loc) { OCG_NewCardInfo i{team, 0, code, team, loc, 0, POS_FACEDOWN_DEFENSE}; OCG_DuelNewCard(h, &i); };
		for(uint32_t c : s.deck) add(0, c, LOCATION_DECK);
		for(uint32_t c : s.extra) add(0, c, LOCATION_EXTRA);
		for(uint32_t c : s.hand) add(0, c, LOCATION_HAND);
		for(int i = 0; i < 5; i++) add(1, DUMMY, LOCATION_DECK);
		OCG_StartDuel(h);
	}
	~Duel() { if(h) OCG_DestroyDuel(h); }
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
					if(is_prompt(t)) { out = parse_prompt(t, body + 1, ln - 1); got = true; }
				}
				o += 4 + ln;
			}
			if(got) return true;
			if(st == OCG_DUEL_STATUS_END) return false;
		}
		return false;
	}
	void respond(const Bytes& b) { OCG_DuelSetResponse(h, b.data(), (uint32_t)b.size()); }
	std::vector<uint32_t> cards(uint32_t loc, bool faceupOnly = false) {
		OCG_QueryInfo q{QUERY_CODE | QUERY_POSITION, 0, loc, 0, 0};
		uint32_t n = 0; const uint8_t* p = (const uint8_t*)OCG_DuelQueryLocation(h, &n, &q);
		std::vector<uint32_t> out;
		Rd r(p, n, 4);
		while(r.o + 2 <= n) {
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
			if(code && (!faceupOnly || (pos & POS_FACEUP))) out.push_back(code);
		}
		return out;
	}
};

// ------------------------------------------------------------------ search
struct Step { std::string kind; uint32_t card = 0; std::string effect; std::vector<uint32_t> picks; };
struct Opt { std::string label; Bytes resp; bool main = false; Step step; std::vector<uint32_t> picks; bool end = false; };

static std::vector<Opt> dedupe(std::vector<Opt> v) {
	std::set<std::string> seen; std::vector<Opt> out;
	for(auto& o : v) if(seen.insert(o.label).second) out.push_back(std::move(o));
	return out;
}
static std::vector<Opt> choices(const Prompt& m) {
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
		for(size_t i = 0; i < m.cards.size(); i++) { Opt o; o.label = "pick " + std::to_string(m.cards[i]); o.resp = r_unselect((int)i); o.picks = {m.cards[i]}; out.push_back(o); }
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

struct Board { std::vector<uint32_t> mzone, szone, hand, grave, banished; };
struct Found { std::vector<Step> steps; Board board; double score = 0; };

struct Search {
	Setup setup;
	int maxActions = 8; double timeLimit = 20; int threads = 2; size_t top = 12;
	std::set<uint32_t> targets;
	std::atomic<bool> stop{false};
	Clock::time_point t0;
	std::atomic<uint64_t> replays{0}, prompts{0};
	std::mutex mx;
	std::unordered_set<std::string> visited;
	std::map<std::string, Found> boards;
	struct Task { std::vector<Bytes> path; std::vector<Step> steps; int actions = 0; };
	std::deque<Task> queue;
	std::condition_variable cv;
	int busy = 0;

	bool out_of_time() const { return stop.load() || std::chrono::duration<double>(Clock::now() - t0).count() > timeLimit; }
	static bool extra_type(uint32_t code) { auto it = g_cards.find(code); return it != g_cards.end() && (it->second.type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ | TYPE_LINK)); }
	double score_of(const Board& b) const {
		double s = 0;
		for(uint32_t c : b.mzone) s += extra_type(c) ? 3 : 2;
		s += 1.0 * b.szone.size();
		for(uint32_t c : b.mzone) if(targets.count(c)) s += 10;
		for(uint32_t c : b.szone) if(targets.count(c)) s += 10;
		s += 0.25 * b.hand.size();
		return s;
	}
	Board read_board(Duel& d) {
		Board b; b.mzone = d.cards(LOCATION_MZONE); b.szone = d.cards(LOCATION_SZONE); b.hand = d.cards(LOCATION_HAND);
		b.grave = d.cards(LOCATION_GRAVE); b.banished = d.cards(LOCATION_REMOVED); return b;
	}
	static std::string key_of(std::vector<uint32_t> v) { std::sort(v.begin(), v.end()); std::string s; for(uint32_t c : v) s += std::to_string(c) + ","; return s; }
	void record(Duel& d, const std::vector<Step>& steps) {
		Board b = read_board(d);
		std::string k = key_of(b.mzone) + "|" + key_of(b.szone) + "|" + key_of(b.hand);
		std::lock_guard<std::mutex> lk(mx);
		auto it = boards.find(k);
		if(it == boards.end() || steps.size() < it->second.steps.size()) { Found f; f.steps = steps; f.board = b; f.score = score_of(b); boards[k] = f; }
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
			if(!o.picks.empty()) for(uint32_t c : o.picks) steps.back().picks.push_back(c);
			else if(o.step.kind == "Option" && steps.back().effect.empty()) steps.back().effect = o.step.effect;
		}
	}
	// Explore from a live duel `d` sitting at prompt m. Hands extra branches to idle workers.
	void dfs(std::unique_ptr<Duel> d, Prompt m, std::vector<Bytes> path, std::vector<Step> steps, int actions) {
		for(;;) {
			if(out_of_time()) return;
			prompts++;
			std::vector<Opt> opts = (m.type == MSG_SELECT_CHAIN && m.chains.empty()) ? std::vector<Opt>{} : choices(m);
			if(m.type == MSG_SELECT_CHAIN && m.chains.empty()) { Opt o; o.label = "pass"; o.resp = p32(-1); opts.push_back(o); }
			if(opts.empty()) return;
			if(opts.size() == 1) {
				const Opt& o = opts[0];
				if(o.end) { record(*d, steps); return; }
				d->respond(o.resp); path.push_back(o.resp); add_step(steps, o);
				if(o.main && m.type == MSG_SELECT_IDLECMD) actions++;
				bool retry; if(!d->run(m, retry)) return;
				continue;
			}
			if(m.type == MSG_SELECT_IDLECMD) {
				Board b = read_board(*d);
				std::string k = key_of(b.mzone) + "|" + key_of(b.szone) + "|" + key_of(b.hand) + "|" + key_of(b.grave) + "|" + key_of(b.banished) + "#";
				for(auto& o : opts) k += o.label + ";";
				{ std::lock_guard<std::mutex> lk(mx); if(!visited.insert(k).second) return; }
				record(*d, steps);
				if(actions >= maxActions) return;
			}
			// first branch continues on this duel; the rest are replayed (or handed to idle threads)
			bool first = true;
			for(const Opt& o : opts) {
				if(o.end) continue;
				std::vector<Bytes> p2 = path; p2.push_back(o.resp);
				std::vector<Step> s2 = steps; add_step(s2, o);
				int a2 = actions + ((o.main && m.type == MSG_SELECT_IDLECMD) ? 1 : 0);
				if(first) { first = false; continue; }   // handled last, below
				{
					std::lock_guard<std::mutex> lk(mx);
					if((int)queue.size() < threads * 2) { queue.push_back({p2, s2, a2}); cv.notify_one(); continue; }
				}
				if(out_of_time()) return;
				Prompt mm; bool ok; auto dd = replay(p2, mm, ok);
				if(ok) dfs(std::move(dd), mm, p2, s2, a2);
			}
			// the first non-end option, on the live duel
			for(const Opt& o : opts) {
				if(o.end) continue;
				d->respond(o.resp); path.push_back(o.resp); add_step(steps, o);
				if(o.main && m.type == MSG_SELECT_IDLECMD) actions++;
				bool retry; if(!d->run(m, retry)) return;
				break;
			}
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
			if(ok) dfs(std::move(d), m, t.path, t.steps, t.actions);
			{ std::lock_guard<std::mutex> lk(mx); busy--; }
			cv.notify_all();
		}
	}
	json run(int id) {
		t0 = Clock::now();
		queue.push_back({{}, {}, 0});
		std::vector<std::thread> ts;
		for(int i = 0; i < std::max(1, threads); i++) ts.emplace_back([this] { worker(); });
		std::atomic<bool> finished{false};
		std::thread prog([&] {
			while(!finished.load()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				if(finished.load()) break;
				size_t nb; { std::lock_guard<std::mutex> lk(mx); nb = boards.size(); }
				emit({{"id", id}, {"progress", {{"replays", replays.load()}, {"states", visited.size()}, {"boards", nb}, {"seconds", std::chrono::duration<double>(Clock::now() - t0).count()}}}});
			}
		});
		for(auto& t : ts) t.join();
		finished = true; prog.join();
		double secs = std::chrono::duration<double>(Clock::now() - t0).count();
		std::vector<Found> all; for(auto& kv : boards) all.push_back(kv.second);
		std::sort(all.begin(), all.end(), [](const Found& a, const Found& b) { return a.score != b.score ? a.score > b.score : a.steps.size() < b.steps.size(); });
		json res = json::array();
		std::set<std::string> fieldSeen;
		for(const Found& f : all) {
			std::string fk = key_of(f.board.mzone) + "|" + key_of(f.board.szone);
			if(!fieldSeen.insert(fk).second) continue;   // same field, different hand: keep the best one only
			json steps = json::array();
			for(const Step& s : f.steps) steps.push_back({{"do", s.kind}, {"card", s.card}, {"effect", s.effect}, {"picks", s.picks}});
			res.push_back({{"score", f.score}, {"field", f.board.mzone}, {"backrow", f.board.szone}, {"hand", f.board.hand}, {"gy", f.board.grave}, {"banished", f.board.banished}, {"steps", steps}});
			if(res.size() >= top) break;
		}
		bool complete = !stop.load() && secs <= timeLimit;
		return {{"id", id}, {"done", true}, {"complete", complete}, {"boards", res}, {"stats", {{"replays", replays.load()}, {"states", visited.size()}, {"endBoards", boards.size()}, {"seconds", secs}}}};
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
			g_cards.clear();
			if(!load_cdb(req.value("cdb", ""), err) || !open_scripts(req.value("scripts", ""), err)) { emit({{"id", id}, {"error", err}}); continue; }
			ready = true;
			emit({{"id", id}, {"ready", true}, {"cards", g_cards.size()}, {"scripts", g_zip_index.size()}});
			continue;
		}
		if(cmd == "search") {
			if(!ready) { emit({{"id", id}, {"error", "not initialized"}}); continue; }
			if(g_search_thread.joinable()) g_search_thread.join();
			auto s = std::make_unique<Search>();
			s->setup.hand = ids(req, "hand");
			s->setup.extra = ids(req, "extra");
			std::vector<uint32_t> deck = ids(req, "deck");
			for(uint32_t c : s->setup.hand) { auto it = std::find(deck.begin(), deck.end(), c); if(it != deck.end()) deck.erase(it); }
			s->setup.deck = deck;
			s->maxActions = req.value("maxActions", 8);
			s->timeLimit = req.value("timeMs", 20000) / 1000.0;
			s->threads = std::max(1, std::min(16, req.value("threads", 2)));
			s->top = (size_t)req.value("top", 12);
			for(uint32_t t : ids(req, "targets")) s->targets.insert(t);
			std::vector<uint32_t> missing;
			for(uint32_t c : s->setup.hand) if(!g_cards.count(c) || !g_zip_index.count("c" + std::to_string(c) + ".lua")) missing.push_back(c);
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
