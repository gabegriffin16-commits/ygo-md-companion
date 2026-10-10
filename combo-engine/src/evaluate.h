// combo-engine: what makes a strong going-first end board, read from each card's text.
// Copyright (C) 2026 Gabe Griffin and contributors. SPDX-License-Identifier: AGPL-3.0-or-later
//
// A board is judged the way players judge one: how many times it can stop the opponent on their turn, and how
// well. Each card's text is split into its effects; an effect counts as an interruption when it can be used on
// the opponent's turn (Quick Effects, Traps and Quick-Play Spells, "when your opponent activates..." triggers)
// and it actually does something to them (negate, banish, destroy, bounce, send, take control...). Where it can be
// used from decides where it counts: on the field, set in the backrow, kept in hand (handtraps) or in the GY.
// Lasting locks ("your opponent cannot..."), protection and floating effects add resilience; bodies and spare
// cards in hand add a little. The user's own end-board goals sit on top of all that.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

struct CardEval {
	float field = 0;     // interruption value while face-up on the field (monsters, continuous Spells/Traps)
	float set = 0;       // ...while set in the Spell & Trap Zone
	float hand = 0;      // ...while kept in hand (handtraps)
	float gy = 0;        // ...from the GY
	float lock = 0;      // a lasting restriction on the opponent while on the field
	float sturdy = 0;    // protection / floats: harder to break
};

namespace evalx {
inline std::string lower(std::string s) { for(char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline bool has(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }
// Split card text into effects: sentences, bullets and lines. A bullet ("●") keeps the sentence that introduces it
// in front, so "If you control no cards (Quick Effect): discard this card; apply these effects... ● Each time..."
// still reads as a handtrap.
struct Fx { std::string text; int group; };   // bullets share their introducing sentence's group
inline std::vector<Fx> effects(const std::string& text) {
	std::vector<Fx> out; std::string cur, header; bool inBullet = false; int group = 0;
	auto flush = [&] {
		if(cur.find_first_not_of(" \r\n\t") != std::string::npos) {
			std::string e = lower(cur);
			if(inBullet) out.push_back({header + " " + e, group}); else { out.push_back({e, ++group}); header = e; }
		}
		cur.clear();
	};
	for(size_t i = 0; i < text.size(); i++) {
		char c = text[i];
		if(c == '\n' || c == '\r') { flush(); if(i + 1 < text.size() && text[i + 1] != '\n' && text[i + 1] != '\r' && !((unsigned char)text[i + 1] == 0xE2)) inBullet = false; continue; }
		if((unsigned char)c == 0xE2 && i + 2 < text.size() && (unsigned char)text[i + 1] == 0x97 && (unsigned char)text[i + 2] == 0x8F) { flush(); inBullet = true; i += 2; continue; }   // ●
		cur += c;
		if(c == '.' && i + 1 < text.size() && text[i + 1] == ' ' && !(i >= 2 && text[i - 1] == 'p' && text[i - 2] == ' ')) flush();
	}
	flush();
	return out;
}
// How much one effect hurts the opponent, if it can be used on their turn.
inline float hurt(const std::string& e) {
	float v = 0;
	if(has(e, "negate the activation") || (has(e, "negate") && has(e, "summon"))) v = 3.5f;
	else if(has(e, "negate")) v = 3.0f;
	else if(has(e, "banish") && (has(e, "your opponent controls") || has(e, "on the field") || has(e, "opponent's"))) v = 2.5f;
	else if(has(e, "destroy") && (has(e, "your opponent controls") || has(e, "on the field") || has(e, "opponent's") || has(e, "that card") || has(e, "that monster"))) v = 2.0f;
	else if((has(e, "return") && has(e, "to the hand")) || has(e, "shuffle") || has(e, "to the extra deck")) v = has(e, "opponent") || has(e, "on the field") ? 2.0f : 0;
	else if(has(e, "take control") || has(e, "gain control")) v = 2.5f;
	else if(has(e, "tribute") && (has(e, "on the field") || has(e, "your opponent controls"))) v = 2.5f;
	else if(has(e, "draw") && has(e, "opponent") && (has(e, "special summon") || has(e, "activates"))) v = 2.0f;   // Maxx "C" and friends
	else if(has(e, "send") && has(e, "to the gy") && (has(e, "your opponent controls") || has(e, "opponent's"))) v = 2.0f;
	else if(has(e, "change") && has(e, "face-down")) v = 1.5f;
	else if(has(e, "cannot activate") && has(e, "opponent")) v = 1.5f;
	else if(has(e, "loses") && has(e, "atk") && has(e, "opponent")) v = 0.5f;
	if(v > 0 && !has(e, "target")) v += 0.3f;                       // non-targeting gets around protection
	if(v > 0 && has(e, "and if you do, destroy")) v += 0.3f;
	return v;
}
inline bool on_their_turn(const std::string& e, bool spellTrapQuick) {
	return spellTrapQuick || has(e, "(quick effect)") || has(e, "your opponent activates") || has(e, "opponent's turn") ||
		has(e, "either player's turn") || has(e, "your opponent would") || has(e, "opponent normal or special summons") ||
		has(e, "opponent special summons") || has(e, "your opponent normal summons") || has(e, "during the battle phase") ||
		has(e, "when an attack is declared") || has(e, "opponent's monster declares an attack");
}
inline CardEval evaluate(const std::string& text, uint32_t type) {
	CardEval r;
	const bool mon = type & 0x1, spell = type & 0x2, trap = type & 0x4;
	const bool quickplay = spell && (type & 0x10000), continuous = (spell || trap) && (type & 0x20000), field = spell && (type & 0x80000);
	std::vector<float> onField, setV, inHand, inGy;
	std::vector<Fx> fxs = effects(text);
	for(size_t fi = 0; fi < fxs.size(); fi++) {
		const std::string& e = fxs[fi].text;
		// A bulleted list is one effect with options: only its best option counts.
		bool laterBetter = false;
		for(size_t fj = 0; fj < fxs.size(); fj++) if(fj != fi && fxs[fj].group == fxs[fi].group && hurt(fxs[fj].text) > hurt(e)) laterBetter = true;
		for(size_t fj = 0; fj < fi; fj++) if(fxs[fj].group == fxs[fi].group && hurt(fxs[fj].text) == hurt(e)) laterBetter = true;
		bool fromGy = has(e, "in your gy") || has(e, "from your gy") || has(e, "banish this card from your gy") || has(e, "if this card is in your graveyard");
		bool fromHand = has(e, "discard this card") || has(e, "from your hand") || has(e, "send this card from your hand") || has(e, "reveal this card");
		bool theirTurn = on_their_turn(e, (trap || quickplay) && !fromGy);
		bool battleOnly = has(e, "attack is declared") || has(e, "declares an attack") || has(e, "during the battle phase");
		float h = theirTurn && !laterBetter ? hurt(e) * (battleOnly ? 0.5f : 1.0f) : 0;
		bool leaves = (has(e, "this card") || has(e, "this face-up card")) && (has(e, "leaves the field") || has(e, "is destroyed") || has(e, "sent from the field"));
		if(!theirTurn && leaves && mon) { float d = hurt(e); if(d > 0) onField.push_back(d * 0.5f); }   // punishes removal (e.g. Absolute Zero)
		// Playing on their turn: a set Trap / Quick-Play (or a Quick Effect) that summons something.
		if(h == 0 && theirTurn && !fromGy && (has(e, "special summon") || has(e, "fusion summon") || has(e, "synchro summon") || has(e, "xyz summon") || has(e, "link summon"))
			&& (trap || quickplay || has(e, "(quick effect)")) && !has(e, "special summon this card from your hand")) h = 2.0f;
		if(h > 0) {
			if(fromGy) inGy.push_back(h);
			else if(mon && fromHand) inHand.push_back(h);
			else if(trap || quickplay) setV.push_back(h);
			else onField.push_back(h);
		}
		// Lasting locks while face-up (no "you can": it's always on).
		if(!has(e, "you can") && !has(e, "in response") && !has(e, "return") && (has(e, "your opponent cannot") || has(e, "neither player can") || has(e, "your opponent can only"))) r.lock = std::max(r.lock, 2.5f);
		if(has(e, "cannot be destroyed by card effects") || has(e, "unaffected by") || has(e, "cannot be targeted")) r.sturdy += 0.6f;
		if(leaves || has(e, "if this card in its owner's")) r.sturdy += 0.4f;
	}
	auto best = [](std::vector<float>& v) { std::sort(v.rbegin(), v.rend()); float s = 0; for(size_t i = 0; i < v.size() && i < 2; i++) s += i ? v[i] * 0.6f : v[i]; return s; };
	r.field = best(onField); r.set = best(setV); r.hand = best(inHand); r.gy = best(inGy);
	if(trap && continuous) r.field = std::max(r.field, r.set);   // a continuous Trap keeps working once flipped
	if(field || (spell && continuous)) r.field += r.lock;            // continuous / Field Spells: their lock is the point
	r.sturdy = std::min(r.sturdy, 1.2f);
	(void)field;
	return r;
}
} // namespace evalx
