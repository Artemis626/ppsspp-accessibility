// Copyright (c) 2026- PPSSPP Accessibility Scripting contributors.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// The per-game profile: what the layer learned about a game, kept from one
// session to the next. It is plain text, so a person can read and edit it.
// The adapter decides where the file is; the core only makes and reads the text.
//
//   [letter-code]     A game's own letter code (GlyphPrivate.cpp). Used again
//                     as soon as one row of a font is found in memory and all
//                     its codes are letters under the table.
//   [font]            Which letter each image of a font is. Used again only
//                     after the font has been confirmed afresh in this
//                     session AND what is remembered agrees with it: a font
//                     that is a cache holds other letters every session.
//   [volatile-font]   Fonts where it did not agree. Never remembered again.
//   [findings]        Addresses found, for the person writing a mod: where
//                     texts are, records of numbers, how the selection is
//                     shown. Not used by the layer itself.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Engine.h"

namespace Discovery {

namespace {

const size_t MAX_SAVED_GLYPHS = 6000;
const size_t MAX_FINDINGS = 400;
// What is remembered of a font is believed once this many of its letters
// have been read afresh and every one agrees.
const int MIN_AGREEING = 5;

std::vector<std::string> Lines(const std::string &text) {
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) {
			end = text.size();
		}
		std::string line = text.substr(start, end - start);
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
			line.pop_back();
		}
		lines.push_back(line);
		start = end + 1;
	}
	return lines;
}

// "name = value" -> value, if the line's name is `name`.
bool Value(const std::string &line, const char *name, std::string *value) {
	size_t eq = line.find('=');
	if (eq == std::string::npos) {
		return false;
	}
	std::string left = line.substr(0, eq);
	while (!left.empty() && left.back() == ' ') {
		left.pop_back();
	}
	if (left != name) {
		return false;
	}
	size_t from = eq + 1;
	while (from < line.size() && line[from] == ' ') {
		from++;
	}
	*value = line.substr(from);
	return true;
}

}  // namespace

// ---- GlyphBook ----

void GlyphBook::ForgetProfile() {
	savedTables_.clear();
	remembered_.clear();
	rememberedOk_.clear();
	volatileFonts_.clear();
}

void GlyphBook::ImportProfile(const std::string &text) {
	std::string section, value;
	for (const std::string &line : Lines(text)) {
		if (line.empty() || line[0] == ';') {
			continue;
		}
		if (line[0] == '[') {
			section = line;
			continue;
		}
		if (section == "[letter-code]" && Value(line, "table", &value)) {
			int high = 0;
			unsigned small = 0, capital = 0, space = 0;
			int n = sscanf(value.c_str(), "%d %x %x %x", &high, &small, &capital, &space);
			if (n >= 2) {
				LetterTable t;
				t.found = true;
				t.highByteFirst = high != 0;
				t.small = (int)small;
				t.capital = n >= 3 && capital != 0xFFFFFFFF ? (int)capital : -1;
				t.space = n >= 4 && space != 0xFFFFFFFF ? (int)space : -1;
				savedTables_.push_back(t);
			}
		} else if (section == "[font]" && Value(line, "glyph", &value)) {
			unsigned tex, w, h, u0, v0, u1, v1, code;
			if (sscanf(value.c_str(), "%x %u %u %u %u %u %u %x", &tex, &w, &h, &u0, &v0, &u1, &v1, &code) == 8
				&& remembered_.size() < MAX_SAVED_GLYPHS) {
				GlyphKey key;
				key.tex = tex;
				key.texW = (u16)w;
				key.texH = (u16)h;
				key.u0 = (u16)u0;
				key.v0 = (u16)v0;
				key.u1 = (u16)u1;
				key.v1 = (u16)v1;
				remembered_[key] = (int)code;
			}
		} else if (section == "[volatile-font]" && Value(line, "font", &value)) {
			volatileFonts_.insert((u32)strtoul(value.c_str(), nullptr, 16));
		}
	}
	for (auto it = remembered_.begin(); it != remembered_.end();) {
		it = volatileFonts_.count(it->first.tex) ? remembered_.erase(it) : std::next(it);
	}
}

std::string GlyphBook::ExportProfile() const {
	std::string out;
	char line[200];

	// Letter codes: the ones worked out now, and the ones carried along.
	std::vector<LetterTable> tables = savedTables_;
	for (const auto &kv : tables_) {
		if (kv.second.found) {
			tables.push_back(kv.second);
		}
	}
	std::set<std::string> tableLines;
	for (const LetterTable &t : tables) {
		snprintf(line, sizeof(line), "table = %d %04X %04X %04X\n", t.highByteFirst ? 1 : 0, (unsigned)t.small,
			t.capital >= 0 ? (unsigned)t.capital : 0xFFFFFFFFu, t.space >= 0 ? (unsigned)t.space : 0xFFFFFFFFu);
		tableLines.insert(line);
	}
	if (!tableLines.empty()) {
		out += "[letter-code]\n; two bytes a letter: high byte first (1) or not (0), the code of \"a\", of \"A\", and the space as stored\n";
		for (const std::string &l : tableLines) {
			out += l;
		}
		out += "\n";
	}

	// Fonts: what is remembered, overlaid with what this session read itself.
	std::map<GlyphKey, int> glyphs = remembered_;
	for (const GlyphSymbol &s : symbols_) {
		if (s.origin == GlyphOrigin::Matched && s.code >= 0x20 && TextureTrusted(s.key.tex)
			&& !UsesOwnCode(s.key.tex) && !volatileFonts_.count(s.key.tex)) {
			glyphs[s.key] = s.code;
		}
	}
	if (!glyphs.empty()) {
		out += "[font]\n; texture address, its width and height, the image's rectangle in it, the character code\n";
		size_t n = 0;
		for (const auto &kv : glyphs) {
			if (n++ >= MAX_SAVED_GLYPHS) {
				break;
			}
			const GlyphKey &k = kv.first;
			snprintf(line, sizeof(line), "glyph = %08X %u %u %u %u %u %u %X\n", k.tex, (unsigned)k.texW, (unsigned)k.texH,
				(unsigned)k.u0, (unsigned)k.v0, (unsigned)k.u1, (unsigned)k.v1, (unsigned)kv.second);
			out += line;
		}
		out += "\n";
	}
	if (!volatileFonts_.empty()) {
		out += "[volatile-font]\n; fonts that hold other letters each session (a cache): nothing is remembered of them\n";
		for (u32 tex : volatileFonts_) {
			snprintf(line, sizeof(line), "font = %08X\n", tex);
			out += line;
		}
		out += "\n";
	}
	return out;
}

void GlyphBook::CheckRemembered(u32 tex) {
	if (remembered_.empty() || !TextureTrusted(tex) || UsesOwnCode(tex)) {
		return;
	}
	u32 family = Family(tex);
	int agree = 0, disagree = 0;
	std::set<u32> textures;
	for (const GlyphSymbol &s : symbols_) {
		if (Family(s.key.tex) != family) {
			continue;
		}
		textures.insert(s.key.tex);
		auto r = remembered_.find(s.key);
		if (r == remembered_.end() || s.code < 0 || rememberedOk_.count(s.key.tex)) {
			continue;
		}
		(r->second == s.code ? agree : disagree)++;
	}
	if (disagree > 0) {
		// This font holds other letters than last time: a cache. Forget it for good.
		for (auto it = remembered_.begin(); it != remembered_.end();) {
			it = textures.count(it->first.tex) ? remembered_.erase(it) : std::next(it);
		}
		volatileFonts_.insert(textures.begin(), textures.end());
		version_++;
		return;
	}
	if (agree < MIN_AGREEING) {
		return;
	}
	rememberedOk_.insert(textures.begin(), textures.end());
	for (size_t id = 0; id < symbols_.size(); id++) {
		const GlyphSymbol &s = symbols_[id];
		auto r = remembered_.find(s.key);
		if (s.code < 0 && r != remembered_.end() && textures.count(s.key.tex)) {
			SetCode((int)id, r->second, GlyphOrigin::Matched);
		}
	}
}

bool GlyphBook::AdoptSavedTable(u32 family) {
	int raws = 0;
	for (const GlyphSymbol &s : symbols_) {
		raws += s.raw >= 0 && Family(s.key.tex) == family ? 1 : 0;
	}
	if (raws < 8) {
		return false;
	}
	for (const LetterTable &saved : savedTables_) {
		bool all = true;
		for (const GlyphSymbol &s : symbols_) {
			if (s.raw >= 0 && Family(s.key.tex) == family) {
				all = all && TableChar(saved, s.raw) >= 0;
			}
		}
		if (all) {
			int space = tables_[family].space;
			tables_[family] = saved;
			if (space >= 0) {
				tables_[family].space = space;
			}
			OwnCodeConfirmed(family);
			return true;
		}
	}
	return false;
}

// ---- Engine ----

void Engine::ImportProfile(const std::string &text) {
	book_.ImportProfile(text);
	std::string section;
	for (const std::string &line : Lines(text)) {
		if (!line.empty() && line[0] == '[') {
			section = line;
		} else if (section == "[findings]" && !line.empty() && line[0] != ';' && findings_.size() < MAX_FINDINGS) {
			findings_.insert(line);
		}
	}
	profileStamp_++;
}

void Engine::NoteFinding(const std::string &line) {
	if (findings_.size() < MAX_FINDINGS && findings_.insert(line).second) {
		profileStamp_++;
	}
}

std::string Engine::ExportProfile() const {
	std::string out = "; Discovery profile: what the layer has learned about this game.\n"
		"; It is written by the emulator and read again when the game starts.\n"
		"; You may edit or delete it; deleting it only means everything is learned again.\n\n";
	out += book_.ExportProfile();
	if (!findings_.empty()) {
		out += "[findings]\n; for a person writing a mod: places in memory the layer found. Not used by the layer itself.\n";
		for (const std::string &line : findings_) {
			out += line + "\n";
		}
	}
	return out;
}

}  // namespace Discovery
