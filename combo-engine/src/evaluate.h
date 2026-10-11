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
	float second[4] = {0, 0, 0, 0};   // what a 2nd interruption adds per place (field, set, hand, GY): dropped when both share one use per turn
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
	uint64_t fusionRace = 0; int fusionMinLv = 0; bool fusionEither = false;   // what the Fusion must be: "a Dragon or Spellcaster Fusion Monster", "Level 8 or higher"
	std::string fusionTag;
	// A Fusion Monster's materials from its first line ("Elemental HERO Neos" + 1 "Wingman" Fusion Monster).
	// Empty when a part has no quoted name (e.g. "2 monsters with different Attributes"): can't be checked.
	// tag: a quoted name ("" = none); exact: the card itself rather than an archetype. Generic parts ("1 LIGHT
	// Spellcaster monster", "1 Fusion, Synchro, Xyz, or Link Monster") set attr / race / kinds / effect / minAtk.
	struct Mat { std::string tag; bool exact = false, fusion = false, effect = false, mentions = false; int n = 1; uint32_t attr = 0, kinds = 0, notKinds = 0; uint64_t race = 0; int minAtk = 0; };   // notKinds: non-Synchro etc.
	// What its interruption needs, as alternatives (any one will do), checked against the board in score_of: a monster we
	// control ("while you control a Fusion Monster that mentions "Fallen of Albaz"": Tri-Brigade Mercourier), or a cost
	// paid from the GY ("banish 1 Tuner from your GY": Kewl Tune RS; "1 face-up Fusion Monster you control, or 2 Fusion
	// Monsters in your GY, that mention "Fallen of Albaz"": Branded Retribution).
	struct Need { int where = 1; int n = 1; Mat m; };   // where: 1 = we control it, 2 = in our GY
	std::vector<Need> needs;
	std::vector<Mat> mats;
	// A summon on their turn from the hand, Deck or Extra Deck: worth what it can bring (judged in score_of against what's
	// actually there), not a flat share. Accel Synchro Stardust Dragon: "Stardust Dragon" from the Extra Deck; Kewl Tune
	// Remix: then Synchro Summon a Tuner Synchro Monster; Incredible Ecclesia: a "Swordsoul" monster or "Fallen of Albaz"
	// from the hand or Deck. bringAt: an AT_ value; bringFrom: 1 hand, 2 Deck, 16 Extra Deck; bringKinds: what an
	// Extra Deck summon must be (Synchro 0x2000, Tuner 0x1000); bringBase: the generic share it replaces.
	struct Tag { std::string name; bool exact = false; };
	uint8_t bringAt = 0, bringFrom = 0; uint32_t bringKinds = 0; float bringBase = 0; int bringLvUp = 0;
	bool bringSelf = false;    // it is one of the materials (Kewl Tune Clip: Special Summon this card, then Synchro Summon)
	bool bringGyTuner = false; // a Tuner comes back from the GY first and can be a material (Remix, B2B)
	// "If this card is sent to the GY as Synchro Material: ..." worth (Kewl Tune Mix: destroy a monster), and a card that
	// applies such an effect from the GY on their turn (Kewl Tune Loudness War); copyTag: what it can banish to copy.
	float matHurt = 0; bool copyMat = false; std::string copyTag;   // "increase its Level by 3" first (Elfnote Power Patron)
	std::vector<Tag> bringTags;
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
			// Older cards say "Graveyard": read it as the "GY" every rule here looks for (Superheavy Samurai Flutist).
			for(size_t at; (at = e.find("graveyard")) != std::string::npos;) e.replace(at, 9, "gy");
			// "Special Summon it. Its effects are negated." is one effect: the 2nd sentence is about the 1st one's monster.
			const bool cont = !out.empty() && !inBullet && (e.rfind("its effects are negated", 0) == 0 || e.rfind("their effects are negated", 0) == 0 || e.rfind("negate its effects", 0) == 0 ||
				e.rfind("negate the effects of that monster", 0) == 0 || e.rfind("negate their effects", 0) == 0);
			if(cont) { out.back().text += " " + e; header = out.back().text; }
			else if(inBullet) out.push_back({header + " " + e, group}); else { out.push_back({e, ++group}); header = e; }
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
// "Opponent's ..." that's about their cards (field, hand, monster...), not "opponent's turn / Main Phase / GY".
inline bool theirs(const std::string& e) {
	for(size_t i = e.find("opponent's"); i != std::string::npos; i = e.find("opponent's", i + 1)) {
		std::string w = e.substr(i + 11, 12);
		if(w.compare(0, 4, "turn") && w.compare(0, 4, "main") && w.compare(0, 6, "battle") && w.compare(0, 2, "gy") && w.compare(0, 5, "end p") && w.compare(0, 7, "standby") && w.compare(0, 2, "lp") && w.compare(0, 4, "life") && w.compare(0, 4, "deck")) return true;   // their LP / Deck aren't their cards on the field
	}
	return false;
}
// A real negate: "negate" the verb, or "negated" that isn't a drawback of what the effect does to its own pick: a monster
// it Special Summons ("Special Summon it, but its effects are negated": Crystron Quan, Wicked Rebirth) or takes control
// of (Fabled Levilazebul). Only the action part counts (after the colon): "If this card is Special Summoned: ... its
// effects are negated" (Holding Arms) and "gains 400 ATK, but its effects are negated" (Forbidden Chalice) are negates.
inline bool negates(const std::string& e) {
	const size_t colon = e.find(':'); const size_t act = colon == std::string::npos ? 0 : colon + 1;
	for(size_t i = e.find("negate"); i != std::string::npos; i = e.find("negate", i + 1)) {
		const std::string pre = e.substr(i >= 10 ? i - 10 : 0, i >= 10 ? 10 : i);
		if(pre.find("cannot be ") != std::string::npos || pre.find("can't be ") != std::string::npos) continue;   // "this card's activation cannot be negated"
		const std::string before = i > act ? e.substr(act, i - act) : std::string();
		size_t own = std::string::npos; for(const char* w : {"special summon", "take control", "gain control"}) { size_t at = before.rfind(w); if(at != std::string::npos && (own == std::string::npos || at > own)) own = at; }
		const bool ownPick = own != std::string::npos && before.find("opponent controls", own) == std::string::npos && before.find("opponent's", own) == std::string::npos;
		if(i + 6 >= e.size() || e[i + 6] != 'd') {   // the verb: "negate its effects" right after summoning it (Purrely Sharely!?, Fossil Excavation) is a drawback too
			const std::string after = e.substr(i + 6, 30);
			const bool pronoun = after.rfind(" its effects", 0) == 0 || after.rfind(" their effects", 0) == 0 || after.rfind(" the effects of that monster", 0) == 0;
			if(!(pronoun && ownPick)) return true;
			continue;
		}
		if(!ownPick) return true;
	}
	return false;
}
inline float hurt(const std::string& e) {
	float v = 0;
	if(has(e, "3 or more attacks have been declared")) return 0;   // only after our own attacks (Sangenpai): never on their turn
	// What it does comes after the condition: "If a Fusion Monster you control is destroyed by an opponent's card effect,
	// while this card is in your GY: ..." (Dracotail Pan) destroys nothing. Narrowing words (whose effects a negate
	// answers) can still be in the condition, so the adjustments below read the whole text.
	const std::string full = e; const size_t colon = full.find(':');
	std::string x = colon == std::string::npos ? full : full.substr(colon + 1);
	// Protection isn't removal: "it cannot be destroyed by your opponent's card effects" (Salamangreat Almiraj).
	for(size_t at; (at = x.find("cannot be destroyed")) != std::string::npos;) x.replace(at, 19, "is kept safe");
	{ const std::string& e = x;
	if(has(e, "effect becomes \"") || has(e, "effect becomes:")) v = 3.0f;   // "the activated effect becomes \"...\"" replaces it: as good as a negate (Phantom of Yubel, Samsara D Lotus)
	else if((has(e, "negate the attack") || has(e, "negate the next attack") || has(e, "negate an attack")) && !has(e, "activation")) v = 0.5f;   // battle only (Necro Gardna): no play stopped
	else if(has(e, "negate the activation") || has(e, "negate the summon") || has(e, "negate that summon") || has(e, "negate the normal or special summon") || has(e, "negate the special summon")) v = 3.5f;
	else if(negates(e)) v = 3.0f;
	// "Opponent's" means their cards, not "during your opponent's turn" (Rindbrumm reviving itself from the GY on their
	// turn isn't removal). Banishing cards from their GY stops no play (Dracotail Sting).
	// "banish" the verb, not "banished cards" (a place: Lubellion shuffles materials from "face-up banished cards").
	else if((has(e, "banish ") || has(e, "banish,")) && !has(e, "your opponent controls") && !has(e, "on the field") && (has(e, "opponent's gy") || has(e, "any gy") || has(e, "either gy") || has(e, "opponent's graveyard"))) v = 0.3f;
	else if((has(e, "banish ") || has(e, "banish,")) && has(e, "random card from your opponent's hand")) v = 2.0f;   // Tinia: a random card, back at the End Phase
	else if((has(e, "banish ") || has(e, "banish,")) && has(e, "extra deck") && !has(e, "on the field") && !has(e, "your opponent controls")) v = 1.0f;   // from their Extra Deck (Crackle): no play stopped
	else if((has(e, "banish ") || has(e, "banish,")) && (has(e, "your opponent controls") || has(e, "on the field") || theirs(e))) v = 2.5f;
	else if(has(e, "destroy") && (has(e, "your opponent controls") || has(e, "on the field") || theirs(e) || has(e, "that card") || has(e, "that monster"))) v = 2.0f;
	// Their cards, not ours: "shuffling Fusion Materials ... from your monsters on the field" (Lubellion) removes nothing.
	else if((has(e, "return") && has(e, "to the hand")) || has(e, "shuffle") || has(e, "to the extra deck")) v = theirs(e) || has(e, "your opponent controls") || (has(e, "on the field") && !has(e, "your monsters on the field") && !has(e, "fusion material")) ? 2.0f : 0;
	else if(has(e, "take control") || has(e, "gain control")) v = 2.5f;
	else if(has(e, "tribute") && (has(e, "on the field") || has(e, "your opponent controls"))) v = 2.5f;
	else if(has(e, "draw") && has(e, "opponent") && (has(e, "special summon") || has(e, "activates"))) v = 2.0f;   // Maxx "C" and friends
	else if(has(e, "send") && has(e, "to the gy") && (has(e, "your opponent controls") || theirs(e)) && !has(e, "their deck") && !has(e, "top card")) v = 2.0f;
	else if(has(e, "send all") && has(e, "from the field to the gy")) v = 2.5f;
	else if((has(e, "link summon") || has(e, "synchro summon") || has(e, "xyz summon") || has(e, "fusion summon")) && (has(e, "only that opponent's monster") || has(e, "monster your opponent controls as material") || has(e, "1 link-2 or lower monster your opponent controls"))) v = 2.5f;   // their monster as material: it's gone (Unchained Soul of Rage)   // a field wipe (Divine Arsenal AA-ZEUS)
	else if((has(e, "top of the deck") || has(e, "bottom of the deck")) && (has(e, "your opponent controls") || has(e, "on the field")) && !has(e, "from your hand") && !has(e, "place this card") && !has(e, "return this card")) v = 2.0f;   // spun to the Deck (Time Thief Redoer)   // milling their Deck (Light of Destruction) stops nothing
	else if(has(e, "change") && has(e, "face-down")) v = 1.5f;
	else if(has(e, "cannot be added")) v = 2.0f;   // Droll & Lock Bird: no more searches this turn
	else if(has(e, "cannot activate") && has(e, "opponent")) v = 1.5f;
	else if(has(e, "loses") && has(e, "atk") && has(e, "opponent")) v = 0.5f;
	}
	// How broad a negate is: "a card or effect" (Baronne) beats one card kind (Crystal Wing: monster effects); one that
	// only answers destruction (Stardust) is narrow.
	if(v >= 3.0f && has(full, "a card or effect is activated") && !has(full, "would destroy")) v += 0.5f;
	if(v >= 3.0f && has(full, "would destroy")) v -= 1.5f;
	if(v >= 3.0f && has(full, "spell/trap") && !has(full, "monster")) v -= 0.7f;   // combos run on monster effects
	if(v >= 3.0f && has(full, "face-up spell") && !has(full, "monster") && !has(full, "trap")) v -= 1.5f;   // only a face-up Spell (Dracotail Flame): narrow
	if(v >= 3.0f && (has(full, "fusion, synchro, xyz, or link monster") || has(full, "extra deck monster's effect"))) v -= 0.7f;   // only Extra Deck monsters' effects (Rindbrumm)
	if(v > 0 && has(full, "tribute this card")) v -= 0.5f;              // spends itself
	if(v > 0 && !has(full, "target")) v += 0.3f;                       // non-targeting gets around protection
	if(v > 0 && has(full, "and if you do, destroy")) v += 0.3f;
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
		else if((has(head, "in your gy") || has(head, "in any gy") || has(head, "in either gy")) && (tail.compare(0, 17, "special summon it") == 0 || tail.compare(0, 19, "special summon that") == 0 || tail.compare(0, 19, "special summon both") == 0 || tail.compare(0, 19, "special summon them") == 0)) {
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
	return spellTrapQuick || has(e, "(quick effect)") || has(e, "this is a quick effect") || has(e, "your opponent activates") || has(e, "opponent's turn") ||
		has(e, "either player's turn") || has(e, "your opponent would") || has(e, "opponent normal or special summons") ||
		has(e, "opponent special summons") || has(e, "summoned to your opponent's field") || has(e, "by your normal trap effect") || has(e, "your opponent normal summons") || has(e, "during the battle phase") ||
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
		for(const auto& k : kinds) { if(has(rest, ("non-" + std::string(k.first)).c_str())) m.notKinds |= k.second; else if(has(rest, k.first)) m.kinds |= k.second; }
		if(has(rest, "non-tuner")) m.notKinds |= 0x1000; else if(has(rest, "tuner")) m.kinds |= 0x1000;
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
		bool fromGy = has(e, "this card is in your gy") || has(e, "this card from your gy") || has(e, "this card in your gy") || has(e, "this card from your hand or gy") || has(e, "this card from your hand, deck, or gy");
		bool fromHand = has(e, "discard this card") || has(e, "this card from your hand") || has(e, "this card in your hand") || has(e, "this card is in your hand") || has(e, "reveal this card");
		bool theirTurn = on_their_turn(e, (trap || quickplay) && !fromGy);
		bool battleOnly = has(e, "attack is declared") || has(e, "declares an attack") || has(e, "during the battle phase");
		float h = theirTurn && !laterBetter ? hurt(e) * (battleOnly ? 0.5f : 1.0f) : 0;
		bool leaves = (has(e, "this card") || has(e, "this face-up card")) && (has(e, "leaves the field") || has(e, "is destroyed") || has(e, "sent from the field"));
		// Punishes removal (Absolute Zero). From the GY when it's a GY effect (Aluber: "while this card is in your GY").
		if(!theirTurn && leaves && mon) { float d = hurt(e); if(d > 0) (has(e, "while this card is in your gy") ? inGy : onField).push_back(d * 0.5f); }
		// Playing on their turn: a set Trap / Quick-Play (or a Quick Effect) that summons something.
		if(h == 0 && !laterBetter && hurt(e) == 0 && theirTurn && !fromGy && (has(e, "special summon") || has(e, "fusion summon") || has(e, "synchro summon") || has(e, "xyz summon") || has(e, "link summon"))
			&& (trap || quickplay || has(e, "(quick effect)")) && !has(e, "special summon this card")) {   // summoning itself stops nothing
			// What a summon is worth is what it brings. A revival is valued by what's in the GY (see score_of), so its own
			// share is small; any other summon gets a generic share, below a real negate.
			std::string tg; int lv = 99;
			bool rev = revive_of(e, tg, lv);
			h = rev ? 0.5f : 1.5f;
			if(has(e, "each field")) h = 0.3f;   // "Special Summon both, 1 to each field" (Albion the Sanctifire): hands them a monster too; what it revives counts (revival)
			// Summoning several at once ("up to 1 ... each from your hand, Deck, and GY") is worth more.
			// The GY part is scored by the revival pass (what's actually there to bring back), so only hand and Deck add here.
			if(has(e, "each from")) { int n = has(e, "hand") + has(e, "deck"); h += 1.0f * std::max(0, n - 1); }
			else if(has(e, "up to 2")) h += 1.0f; else if(has(e, "up to 3")) h += 2.0f;
			// One monster from the hand, Deck or Extra Deck (not a Fusion: those are judged as Fusions, below).
			else if((!rev || has(e, "synchro summon 1 ")) && !r.bringAt && !has(e, "fusion summon") && !has(e, "fusion monster")) {
				size_t ss = e.find("special summon"); std::string tail = ss == std::string::npos ? std::string() : e.substr(ss);
				uint8_t from = 0; uint32_t kinds = 0;
				if(has(tail, "from your extra deck")) from |= 16;
				else if(tail.find(" from your ") != std::string::npos) { std::string src = tail.substr(tail.find(" from your ") + 11, 24);
					if(has(src, "hand")) from |= 1; if(has(src, "deck") && !has(src, "extra deck")) from |= 2; }
				// "...then you can Synchro Summon 1 Tuner Synchro Monster" (not "material for a Fusion, Synchro, Xyz, or Link Summon").
				for(const auto& k : std::initializer_list<std::pair<const char*, uint32_t>>{{"synchro summon 1 ", 0x2000}, {"xyz summon 1 ", 0x800000}, {"link summon 1 ", 0x4000000},
						{"synchro summon using", 0x2000}, {"xyz summon using", 0x800000}, {"link summon using", 0x4000000}}) {   // "...Synchro Summon using monsters you control" (Yummy★Snatchy)
					size_t at = e.find(k.first); if(at == std::string::npos || from) continue;
					from |= 16; kinds = k.second; tail = e.substr(at);
					if(has(tail.substr(0, 40), "tuner synchro") || has(tail.substr(0, 40), "tuner xyz")) kinds |= 0x1000;
				}
				if(from) {
					for(size_t q = tail.find('"'); q != std::string::npos; ) {
						size_t q2 = tail.find('"', q + 1); if(q2 == std::string::npos) break;
						std::string nx = tail.substr(q2 + 1, 22); CardEval::Tag t; t.name = tail.substr(q + 1, q2 - q - 1);
						t.exact = !(has(nx, " monster") || has(nx, " card") || has(nx, " spell") || has(nx, " trap"));
						r.bringTags.push_back(t); q = tail.find('"', q2 + 1);
					}
					r.bringFrom = from; r.bringKinds = kinds; r.bringBase = h;
					r.bringGyTuner = has(e, "tuner in your gy") || has(e, "tuners in your gy");
					if(has(e, "summon using monsters you control")) r.bringSelf = true;   // it's one of the materials
					size_t up = e.find("level by "); if(up != std::string::npos && has(e.substr(std::max<size_t>(up, 12) - 12, 12), "increase")) r.bringLvUp = std::atoi(e.c_str() + up + 9);
					r.bringAt = fromGy ? CardEval::AT_GY : mon && fromHand ? CardEval::AT_HAND : (trap || quickplay) ? CardEval::AT_SET : CardEval::AT_FIELD;
					// What it needs first: "choose / target N ... in your GY" (Remix: 2 non-Synchro Tuners).
					for(const char* lead : {"choose ", "target "}) {
						size_t at = e.find(lead), gy = at == std::string::npos ? at : e.find(" in your gy", at);
						if(gy == std::string::npos || gy - at > 50) continue;
						std::string ph = e.substr(at + std::strlen(lead), gy - at - std::strlen(lead)); size_t sp = ph.find(' ');
						if(sp == std::string::npos) continue;
						std::string what = ph.substr(sp + 1); if(!has(what, "monster")) what += " monster";
						auto ms = materials(what);
						if(!ms.empty()) { CardEval::Need nd; nd.where = 2; nd.n = std::max(1, std::atoi(ph.c_str())); nd.m = ms[0]; r.needs.push_back(nd); }
						break;
					}
				}
			}
		}
		const bool bringOnly = r.bringAt && r.bringBase == h && h > 0;   // its value comes from what it brings (score_of)
		bool typedLock = false;
		if(mon && theirTurn && has(e, "special summon this card") && (fromHand || has(e, "(quick effect)"))) {
			r.handExtender = true;
			// ...then Synchro Summons with itself as a material, on their turn (Kewl Tune Clip): worth the Synchro it can make.
			size_t at = e.find("synchro summon 1 ");
			if(at != std::string::npos && !r.bringAt) { r.bringAt = fromHand ? CardEval::AT_HAND : CardEval::AT_FIELD; r.bringFrom = 16; r.bringKinds = 0x2000 | (has(e.substr(at, 40), "tuner synchro") ? 0x1000 : 0); r.bringBase = 0.8f; r.bringSelf = true; }
		}
		if(has(e, "sent to the gy as synchro material") || has(e, "sent to the gy as material")) r.matHurt = std::max(r.matHurt, hurt(e));
		if(theirTurn && has(e, "apply") && has(e, "sent to the gy as synchro material") && has(e, "banish")) {
			r.copyMat = true; size_t q = e.find('"'), q2 = q == std::string::npos ? q : e.find('"', q + 1); if(q2 != std::string::npos) r.copyTag = e.substr(q + 1, q2 - q - 1);
		}
		// What an interruption needs (alternatives): "while / if you control a ... monster"; a GY cost before the ";".
		if(h > 0 && r.needs.empty()) {
			for(const char* lead : {"while you control ", "if you control "}) {
				size_t at = e.find(lead); if(at == std::string::npos) continue;
				size_t st = at + std::strlen(lead), en = e.find_first_of(":;(", st);
				std::string phrase = e.substr(st, en == std::string::npos ? std::string::npos : en - st);
				// ...or a named card: "if you control \"Mementoal Tecuhtlica - Combined Creation\"" (Mementotlan Mace)
				if((has(phrase, "monster") || (!phrase.empty() && phrase[0] == '"')) && !has(phrase, "no ") && !has(phrase, "or more") && phrase.find(" + ") == std::string::npos)
					for(const auto& m : materials(phrase)) { CardEval::Need nd; nd.where = 1; nd.m = m; r.needs.push_back(nd); }
				break;
			}
			std::string cost = e.substr(0, std::min(e.find(';'), e.size()));
			auto num = [](const std::string& w) { return w == "a" || w == "an" ? 1 : std::max(1, std::atoi(w.c_str())); };
			size_t o = cost.find(" you control, or ");
			if(r.needs.empty() && o != std::string::npos && cost.find(" in your gy", o) != std::string::npos) {
				// "N face-up X you control, or M X in your GY[, that mention "Y"]"
				size_t a0 = cost.rfind(", ", o); a0 = a0 == std::string::npos ? 0 : a0 + 2;
				std::string left = cost.substr(a0, o - a0), right = cost.substr(o + 17), tagPart;
				size_t g = right.find(" in your gy"); tagPart = right.substr(g + 11); right = right.substr(0, g);
				size_t sp1 = left.find(' '), sp2 = right.find(' ');
				if(sp1 != std::string::npos && sp2 != std::string::npos) {
					std::string mention = tagPart.find("mention") != std::string::npos ? tagPart : "";
					auto ml = materials(left.substr(sp1 + 1) + " " + mention), mr = materials(right.substr(sp2 + 1) + " " + mention);
					if(!ml.empty() && !mr.empty()) { CardEval::Need a; a.where = 1; a.n = num(left.substr(0, sp1)); a.m = ml[0]; CardEval::Need b; b.where = 2; b.n = num(right.substr(0, sp2)); b.m = mr[0]; r.needs = {a, b}; }
				}
			} else if(r.needs.empty()) {
				size_t bz = cost.find("banish "), fg = cost.find(" from your gy");
				if(bz != std::string::npos && fg != std::string::npos && fg > bz) {   // "banish 1 Tuner from your GY"
					std::string ph = cost.substr(bz + 7, fg - bz - 7); size_t sp = ph.find(' ');
					if(sp != std::string::npos) { auto ms = materials(ph.substr(sp + 1).find("monster") == std::string::npos ? ph.substr(sp + 1) + " monster" : ph.substr(sp + 1));
						if(!ms.empty() && (ms[0].kinds || ms[0].race || ms[0].attr || !ms[0].tag.empty())) { CardEval::Need nd; nd.where = 2; nd.n = num(ph.substr(0, sp)); nd.m = ms[0]; r.needs.push_back(nd); } }
				}
			}
		}
		if(has(e, "from your deck to your hand") || (has(e, "add") && has(e, "from your deck") && !has(e, "from your deck to the gy"))) r.starter = true;
		if(!theirTurn && (has(e, "this card is special summoned") || has(e, "this card is fusion summoned") || has(e, "this card is synchro summoned") || has(e, "this card is xyz summoned") || has(e, "this card is link summoned") || has(e, "this card is summoned") || has(e, "this card is normal or special summoned"))) r.onSummon = std::max(r.onSummon, hurt(e));   // (Cooky☆Yummy, revived on their turn by a Yummy Synchro)
		// Summoned on their turn, it Fusion Summons using their monster as material (Fallen of Albaz): that's removal.
		if(!theirTurn && mon && has(e, "this card is normal or special summoned") && has(e, "fusion summon") && has(e, "either field")) r.onSummon = std::max(r.onSummon, 2.0f);
		// A Fusion it makes: "Fusion Summon 1 ...", not "except Fusion Monsters" (Branded Opening) or "return 1 Fusion Monster
		// to the Extra Deck" as a cost (Branded Retribution, which is a negate).
		if(theirTurn && !r.fusionAt && has(e, "fusion monster") && has(e, "extra deck") && (has(e, "fusion summon") || has(e, "fusion monster from your extra deck"))) {
			r.fusionEither = has(e, "either field");   // Super Polymerization: their monster as material (it's gone)
			r.fusionAt = fromGy ? CardEval::AT_GY : mon && fromHand ? CardEval::AT_HAND : (trap || quickplay) ? CardEval::AT_SET : CardEval::AT_FIELD;
			r.fusionFrom = (has(e, "hand") ? 1 : 0) | (has(e, "field") || has(e, "you control") ? 2 : 0) | (has(e, " gy") ? 4 : 0) | (has(e, "banish") ? 8 : 0);
			if(!r.fusionFrom) r.fusionFrom = 3;
			// "...that mentions a "HERO" monster as material": the first quoted name near "Fusion Monster".
			size_t q = e.find('"'), q2 = q == std::string::npos ? q : e.find('"', q + 1);
			if(q2 != std::string::npos && q < e.find("fusion monster") + 60) r.fusionTag = e.substr(q + 1, q2 - q - 1);
			// "Fusion Summon 1 Dragon or Spellcaster Fusion Monster" (Faimena), "1 Level 8 or higher Fusion Monster" (Cartesia)
			size_t fm = e.find("fusion monster"), st = e.rfind("summon", fm == std::string::npos ? 0 : fm);
			if(fm != std::string::npos && st != std::string::npos && fm - st < 60) {
				std::string ph = e.substr(st + 6, fm - st - 6);
				auto ms = materials(ph + " monster"); if(!ms.empty()) r.fusionRace = ms[0].race;
				size_t lv = ph.find("level "); if(lv != std::string::npos && has(ph.substr(lv), "or higher")) r.fusionMinLv = std::atoi(ph.c_str() + lv + 6);
			}
		}
		if(theirTurn && !r.reviveAt) {
			std::string tag; int lv = 99;
			if(revive_of(e, tag, lv)) {
				r.reviveAt = fromGy ? CardEval::AT_GY : mon && fromHand ? CardEval::AT_HAND : (trap || quickplay) ? CardEval::AT_SET : CardEval::AT_FIELD;
				r.reviveTag = tag; r.reviveMaxLv = lv;
			}
		}
		if(h > 0 && !bringOnly) {
			if(fromGy) { inGy.push_back(h); if(mon && fromHand && (has(e, "from your hand or gy") || has(e, "from your hand, deck, or gy"))) inHand.push_back(h); }   // Rikka Princess: either place
			else if(mon && fromHand) {
				inHand.push_back(h);
				// "send this card from your hand or face-up field": it works from the field too (Tri-Brigade Mercourier)
				if(has(e, "this card from your hand or face-up field") || has(e, "this card from your hand or field") || has(e, "this card from your hand or monster zone") || has(e, "this card in your hand or on the field")) onField.push_back(h);
			}
			else if(trap || quickplay) setV.push_back(h);
			else onField.push_back(h);
		}
		// Lasting locks while face-up (no "you can": it's always on).
		// ("Your opponent cannot target this card" is protection, below, not a lock.) A lock on certain Types only (Secreterion:
		// "Special Summoned Dragon or Spellcaster monsters") counts for less than one on everything.
		if(!has(e, "you can") && !has(e, "in response") && !has(e, "return") && !has(e, "cannot target") && (has(e, "your opponent cannot") || has(e, "neither player can") || has(e, "your opponent can only"))) {
			if(has(e, "or more atk")) typedLock = true;   // only strong monsters (TY-PHON: 3000 or more ATK)
			bool typed = typedLock; for(const char* t : {"dragon", "spellcaster", "warrior", "fiend", "zombie", "machine", "fairy", "beast", "plant", "insect", "aqua", "pyro", "rock", "thunder", "reptile", "psychic", "wyrm", "cyberse", "dinosaur", "fish", "illusion"}) if(has(e, t)) typed = true;
			r.lock = std::max(r.lock, (has(e, "attack") && !has(e, "activate") && !has(e, "summon")) || has(e, "during the battle phase") || has(e, "end of the damage step") || has(e, "monster battles") ? 0.5f : typed ? 1.5f : 2.5f);   // ...nor one that only holds in the Battle Phase (Sangenpai Transcendent Dragion)   // an attack lock doesn't slow their combo
		}
		if(has(e, "cannot be destroyed by card effects") || has(e, "unaffected by") || has(e, "cannot be targeted") || has(e, "cannot target")) r.sturdy += 0.6f;
		if(leaves || has(e, "if this card in its owner's")) r.sturdy += 0.4f;
	}
	auto best = [](std::vector<float>& v) { std::sort(v.rbegin(), v.rend()); float s = 0; for(size_t i = 0; i < v.size() && i < 2; i++) s += i ? v[i] * 0.6f : v[i]; return s; };
	r.field = best(onField); r.set = best(setV); r.hand = best(inHand); r.gy = best(inGy);
	{ int q = 0; for(auto* v : {&onField, &setV, &inHand, &inGy}) { float one = 0; for(float x : *v) one = std::max(one, x); r.second[q] = (q == 0 ? r.field : q == 1 ? r.set : q == 2 ? r.hand : r.gy) - one; q++; } }
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
