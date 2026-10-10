// combo-engine: what makes a strong going-first end board, read from each card's text.
// Copyright (C) 2026 Gabe Griffin and contributors. SPDX-License-Identifier: AGPL-3.0-or-later
//
// A board is judged the way players judge one: how many times it can stop the opponent on their turn, and how
// well. Each card's text is split into its effects; an effect counts as an interruption when it can be used on
// the opponent's turn (Quick Effects, Traps and Quick-Play Spells, "when your opponent activates..." triggers)
// and it actually does something to them (negate, banish, destroy, bounce, send, take control...). Where it can be
// used from decides where it counts: on the field, set in the backrow, kept in hand (handtraps) or in the GY.
// Lasting locks ("your opponent cannot..."), protection and floating effects add resilience; bodies and spare
// cards in hand add a little. Cards that revive from the GY on the opponent's turn make the right monsters in the GY
// count too. The user's own end-board goals sit on top of all that.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct CardEval {
	uint32_t code = 0;   // the card this was read from (set when cards are loaded)
	float field = 0;     // interruption value while face-up on the field (monsters, continuous Spells/Traps)
	float set = 0;       // ...while set in the Spell & Trap Zone
	float hand = 0;      // ...while kept in hand (handtraps)
	float gy = 0;        // ...from the GY
	float lock = 0;      // a lasting restriction on the opponent while on the field
	float sturdy = 0;    // protection / floats: harder to break
	bool fromSide = false;   // its interruption switches it into the center zone: needs a side zone + a center monster
	bool inCenter = false;   // its effects only work while it's in the center Main Monster Zone
	// Revival on the opponent's turn: Special Summons a monster from the GY (June Pride, Strelitzia, Call of the Haunted).
	// Used from: 0 = can't, else one of the AT_ values. The tag is the quoted name it asks for ("" = any monster).
	enum { AT_FIELD = 1, AT_SET, AT_HAND, AT_GY };
	uint8_t reviveAt = 0;
	int reviveMaxLv = 99;
	std::string reviveTag;
	float onSummon = 0;      // what it does to them "if this card is Special Summoned" (counts when it lands on their turn)
	// Left in the end hand without being an interruption: an extender that summons itself from hand on their turn
	// (Elfnote Regina), or a starter for our next turn (it searches the Deck). Small next to the board itself.
	bool handExtender = false, starter = false;
	// Fusion on the opponent's turn from the Extra Deck (Favorite Contact). Used from: an AT_ value. fusionFrom: where
	// its materials can come from (1 hand, 2 field, 4 GY, 8 banished). fusionTag: what the Fusion must mention ("hero").
	uint8_t fusionAt = 0, fusionFrom = 0;
	std::string fusionTag;
	// A Fusion Monster's materials from its first line ("Elemental HERO Neos" + 1 "Wingman" Fusion Monster).
	// Empty when a part has no quoted name (e.g. "2 monsters with different Attributes"): can't be checked.
	// tag: a quoted name ("" = none); exact: the card itself rather than an archetype. Generic parts ("1 LIGHT
	// Spellcaster monster", "1 Fusion, Synchro, Xyz, or Link Monster") set attr / race / kinds / effect / minAtk.
	struct Mat { std::string tag; bool exact = false, fusion = false, effect = false, mentions = false; int n = 1; uint32_t attr = 0, kinds = 0; uint64_t race = 0; int minAtk = 0; };
	// Its interruption needs us to control a certain monster ("while you control a Fusion Monster that mentions "Fallen
	// of Albaz" as material": Tri-Brigade Mercourier). Checked against the board in score_of.
	std::vector<Mat> needs;
	std::vector<Mat> mats;
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
	if(has(e, "negate the activation") || has(e, "negate the summon") || has(e, "negate that summon") || has(e, "negate the normal or special summon") || has(e, "negate the special summon")) v = 3.5f;
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
	// How broad a negate is: "a card or effect" (Baronne) beats one card kind (Crystal Wing: monster effects); one that
	// only answers destruction (Stardust) is narrow.
	if(v >= 3.0f && has(e, "a card or effect is activated") && !has(e, "would destroy")) v += 0.5f;
	if(v >= 3.0f && has(e, "would destroy")) v -= 1.5f;
	if(v >= 3.0f && has(e, "spell/trap") && !has(e, "monster")) v -= 0.7f;   // combos run on monster effects
	if(v > 0 && has(e, "tribute this card")) v -= 0.5f;              // spends itself
	if(v > 0 && !has(e, "target")) v += 0.3f;                       // non-targeting gets around protection
	if(v > 0 && has(e, "and if you do, destroy")) v += 0.3f;
	return v;
}
// "from your hand, Deck, and GY" / "hand or GY" / "GY or banishment": a list of places that includes the GY.
inline bool gy_source(const std::string& s) {
	bool gy = false; size_t p = 0;
	while(p < s.size()) {
		size_t q = s.find_first_of(" ,.;:", p);
		std::string w = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
		if(w == "gy" || w == "graveyard") gy = true;
		else if(!w.empty() && w != "hand" && w != "deck" && w != "or" && w != "and" && w != "banishment") break;
		if(q == std::string::npos || s[q] == '.' || s[q] == ';' || s[q] == ':') break;
		p = q + 1;
	}
	return gy;
}
// Does this effect Special Summon another monster from the GY? Fills in what it asks for: the quoted name, if any
// ("1 Level 6 or lower "Elfnote" monster from your hand or GY"), and a Level cap.
inline bool revive_of(const std::string& e, std::string& tag, int& maxLv) {
	const std::string key = "special summon";
	for(size_t i = e.find(key); i != std::string::npos; i = e.find(key, i + key.size())) {
		size_t k = i + key.size();
		if(k < e.size() && e[k] != ' ') continue;                      // "special summoned", "special summons"
		std::string tail = e.substr(i), head = e.substr(0, i), seg;
		if(has(tail.substr(0, 30), "this card")) continue;               // revives itself: that's a float, not a revival
		size_t j = tail.find(" from your ");
		if(j != std::string::npos && gy_source(tail.substr(j + 11))) seg = tail.substr(0, j);
		else if(has(head, "in your gy") && (tail.compare(0, 17, "special summon it") == 0 || tail.compare(0, 19, "special summon that") == 0)) {
			size_t t = head.rfind("target"); seg = t == std::string::npos ? head : head.substr(t);   // "target 1 "X" monster in your GY; Special Summon it"
		} else continue;
		if(has(seg, "this card")) continue;
		tag.clear();
		size_t q = seg.find('"');
		if(q != std::string::npos) { size_t q2 = seg.find('"', q + 1); if(q2 != std::string::npos) tag = seg.substr(q + 1, q2 - q - 1); }
		maxLv = 99;
		size_t l = seg.find("level ");
		if(l != std::string::npos) { int n = std::atoi(seg.c_str() + l + 6); size_t d = l + 6 + (n >= 10 ? 2 : 1); if(n > 0 && d <= seg.size() && seg.compare(d, 9, " or lower") == 0) maxLv = n; }
		return true;
	}
	return false;
}
inline bool on_their_turn(const std::string& e, bool spellTrapQuick) {
	return spellTrapQuick || has(e, "(quick effect)") || has(e, "your opponent activates") || has(e, "opponent's turn") ||
		has(e, "either player's turn") || has(e, "your opponent would") || has(e, "opponent normal or special summons") ||
		has(e, "opponent special summons") || has(e, "your opponent normal summons") || has(e, "during the battle phase") ||
		has(e, "when an attack is declared") || has(e, "opponent's monster declares an attack");
}
// Fusion materials: split the first line on " + "; every part needs a quoted name.
inline std::vector<CardEval::Mat> materials(const std::string& text) {
	std::vector<CardEval::Mat> out;
	std::string line = lower(text.substr(0, text.find_first_of("\r\n")));
	size_t p = 0;
	while(p <= line.size()) {
		size_t q = line.find(" + ", p);
		std::string part = line.substr(p, q == std::string::npos ? std::string::npos : q - p);
		size_t a = part.find('"'), b = a == std::string::npos ? a : part.find('"', a + 1);
		CardEval::Mat m;
		std::string rest = part;
		if(b != std::string::npos) { m.tag = part.substr(a + 1, b - a - 1); m.exact = !has(part, "monster"); rest = part.substr(0, a) + part.substr(b + 1); }
		else if(!has(part, "monster")) return {};   // not a material we can read
		// What a generic part asks for. Longer Type names first, so "beast-warrior" isn't read as "beast".
		static const std::pair<const char*, uint64_t> races[] = {{"beast-warrior", 0x8000}, {"winged beast", 0x200}, {"sea serpent", 0x40000}, {"creator god", 0x400000},
			{"warrior", 0x1}, {"spellcaster", 0x2}, {"fairy", 0x4}, {"fiend", 0x8}, {"zombie", 0x10}, {"machine", 0x20}, {"aqua", 0x40}, {"pyro", 0x80}, {"rock", 0x100},
			{"plant", 0x400}, {"insect", 0x800}, {"thunder", 0x1000}, {"dragon", 0x2000}, {"beast", 0x4000}, {"dinosaur", 0x10000}, {"fish", 0x20000}, {"reptile", 0x80000},
			{"psychic", 0x100000}, {"divine-beast", 0x200000}, {"wyrm", 0x800000}, {"cyberse", 0x1000000}, {"illusion", 0x2000000}};
		for(const auto& rc : races) { size_t at = rest.find(rc.first); if(at != std::string::npos) { m.race |= rc.second; rest.erase(at, std::string(rc.first).size()); } }
		static const std::pair<const char*, uint32_t> attrs[] = {{"light", 0x10}, {"dark", 0x20}, {"earth", 0x1}, {"water", 0x2}, {"fire", 0x4}, {"wind", 0x8}};
		for(const auto& at : attrs) if(has(rest, at.first)) m.attr |= at.second;
		static const std::pair<const char*, uint32_t> kinds[] = {{"fusion", 0x40}, {"synchro", 0x2000}, {"xyz", 0x800000}, {"link", 0x4000000}};
		for(const auto& k : kinds) if(has(rest, k.first)) m.kinds |= k.second;
		m.fusion = m.kinds == 0x40;
		m.effect = has(rest, "effect monster");
		size_t atk = rest.find(" atk"); if(atk != std::string::npos && has(rest, "or more")) { size_t w = rest.rfind("with ", atk); if(w != std::string::npos) m.minAtk = std::atoi(rest.c_str() + w + 5); }
		if(!part.empty() && part[0] >= '1' && part[0] <= '9') m.n = part[0] - '0';
		m.mentions = has(part, "mention");
		out.push_back(m);
		if(q == std::string::npos) break;
		p = q + 3;
	}
	return out;
}
inline CardEval evaluate(const std::string& text, uint32_t type) {
	CardEval r;
	if(type & 0x40) r.mats = materials(text);   // Fusion Monster
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
		// Where the effect is used from is about *this card*: "a monster from your hand" is just a summon source.
		bool fromGy = has(e, "this card is in your gy") || has(e, "this card from your gy") || has(e, "this card in your gy") || has(e, "this card is in your graveyard");
		bool fromHand = has(e, "discard this card") || has(e, "this card from your hand") || has(e, "this card in your hand") || has(e, "this card is in your hand") || has(e, "reveal this card");
		bool theirTurn = on_their_turn(e, (trap || quickplay) && !fromGy);
		bool battleOnly = has(e, "attack is declared") || has(e, "declares an attack") || has(e, "during the battle phase");
		float h = theirTurn && !laterBetter ? hurt(e) * (battleOnly ? 0.5f : 1.0f) : 0;
		bool leaves = (has(e, "this card") || has(e, "this face-up card")) && (has(e, "leaves the field") || has(e, "is destroyed") || has(e, "sent from the field"));
		if(!theirTurn && leaves && mon) { float d = hurt(e); if(d > 0) onField.push_back(d * 0.5f); }   // punishes removal (e.g. Absolute Zero)
		// Playing on their turn: a set Trap / Quick-Play (or a Quick Effect) that summons something.
		if(h == 0 && !laterBetter && hurt(e) == 0 && theirTurn && !fromGy && (has(e, "special summon") || has(e, "fusion summon") || has(e, "synchro summon") || has(e, "xyz summon") || has(e, "link summon"))
			&& (trap || quickplay || has(e, "(quick effect)")) && !has(e, "special summon this card")) {   // summoning itself stops nothing
			// What a summon is worth is what it brings. A revival is valued by what's in the GY (see score_of), so its own
			// share is small; any other summon gets a generic share, below a real negate.
			std::string tg; int lv = 99;
			h = revive_of(e, tg, lv) ? 0.5f : 1.5f;
			// Summoning several at once ("up to 1 ... each from your hand, Deck, and GY") is worth more.
			// The GY part is scored by the revival pass (what's actually there to bring back), so only hand and Deck add here.
			if(has(e, "each from")) { int n = has(e, "hand") + has(e, "deck"); h += 1.0f * std::max(0, n - 1); }
			else if(has(e, "up to 2")) h += 1.0f; else if(has(e, "up to 3")) h += 2.0f;
		}
		if(mon && theirTurn && has(e, "special summon this card") && (fromHand || has(e, "(quick effect)"))) r.handExtender = true;
		// "While / if you control a ... monster" on an interruption: what it needs on our field.
		if(h > 0 && r.needs.empty()) for(const char* lead : {"while you control ", "if you control "}) {
			size_t at = e.find(lead); if(at == std::string::npos) continue;
			size_t st = at + std::strlen(lead), en = e.find_first_of(":;(", st);
			std::string phrase = e.substr(st, en == std::string::npos ? std::string::npos : en - st);
			if(has(phrase, "monster") && !has(phrase, "no ") && !has(phrase, "or more") && phrase.find(" + ") == std::string::npos) r.needs = materials(phrase);
			break;
		}
		if(has(e, "from your deck to your hand") || (has(e, "add") && has(e, "from your deck") && !has(e, "from your deck to the gy"))) r.starter = true;
		if(!theirTurn && (has(e, "this card is special summoned") || has(e, "this card is fusion summoned") || has(e, "this card is summoned"))) r.onSummon = std::max(r.onSummon, hurt(e));
		if(theirTurn && !r.fusionAt && has(e, "fusion monster") && has(e, "extra deck") && (has(e, "special summon") || has(e, "fusion summon"))) {
			r.fusionAt = fromGy ? CardEval::AT_GY : mon && fromHand ? CardEval::AT_HAND : (trap || quickplay) ? CardEval::AT_SET : CardEval::AT_FIELD;
			r.fusionFrom = (has(e, "hand") ? 1 : 0) | (has(e, "field") || has(e, "you control") ? 2 : 0) | (has(e, " gy") ? 4 : 0) | (has(e, "banish") ? 8 : 0);
			if(!r.fusionFrom) r.fusionFrom = 3;
			// "...that mentions a "HERO" monster as material": the first quoted name near "Fusion Monster".
			size_t q = e.find('"'), q2 = q == std::string::npos ? q : e.find('"', q + 1);
			if(q2 != std::string::npos && q < e.find("fusion monster") + 60) r.fusionTag = e.substr(q + 1, q2 - q - 1);
		}
		if(theirTurn && !r.reviveAt) {
			std::string tag; int lv = 99;
			if(revive_of(e, tag, lv)) {
				r.reviveAt = fromGy ? CardEval::AT_GY : mon && fromHand ? CardEval::AT_HAND : (trap || quickplay) ? CardEval::AT_SET : CardEval::AT_FIELD;
				r.reviveTag = tag; r.reviveMaxLv = lv;
			}
		}
		if(h > 0) {
			if(fromGy) inGy.push_back(h);
			else if(mon && fromHand) inHand.push_back(h);
			else if(trap || quickplay) setV.push_back(h);
			else onField.push_back(h);
		}
		// Lasting locks while face-up (no "you can": it's always on).
		if(!has(e, "you can") && !has(e, "in response") && !has(e, "return") && (has(e, "your opponent cannot") || has(e, "neither player can") || has(e, "your opponent can only")))
			r.lock = std::max(r.lock, has(e, "attack") && !has(e, "activate") && !has(e, "summon") ? 0.5f : 2.5f);   // an attack lock doesn't slow their combo
		if(has(e, "cannot be destroyed by card effects") || has(e, "unaffected by") || has(e, "cannot be targeted")) r.sturdy += 0.6f;
		if(leaves || has(e, "if this card in its owner's")) r.sturdy += 0.4f;
	}
	auto best = [](std::vector<float>& v) { std::sort(v.rbegin(), v.rend()); float s = 0; for(size_t i = 0; i < v.size() && i < 2; i++) s += i ? v[i] * 0.6f : v[i]; return s; };
	r.field = best(onField); r.set = best(setV); r.hand = best(inHand); r.gy = best(inGy);
	if(trap && continuous) r.field = std::max(r.field, r.set);   // a continuous Trap keeps working once flipped
	if(field || (spell && continuous)) r.field += r.lock;            // continuous / Field Spells: their lock is the point
	r.sturdy = std::min(r.sturdy, 1.2f);
	{
		std::string t = lower(text);
		r.fromSide = has(t, "switch the locations of this card") && has(t, "center main monster zone");
		r.inCenter = has(t, "while this card is in the center main monster zone") || has(t, "if this card is in the center main monster zone");
	}
	(void)field;
	return r;
}
} // namespace evalx
