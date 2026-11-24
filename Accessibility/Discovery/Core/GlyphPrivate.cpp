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

// Text kept in a game's own letter code. See GlyphBook::LearnFromCodeLine in
// GlyphCipher.h.
//
// The ordinary search looks for a row's pattern of repeats among bytes that
// are letters. A game with its own letter code (Persona 2's menus: "e" is
// 01 05, "M" is 00 EC) has no such bytes. So this search assumes nothing
// about the values: only that the same symbol is always the same 16-bit
// value, and different symbols are different values.
//
// That finds the string, and gives each symbol the game's code for it. It
// does not say which letter the code is. For that, two things games do:
//
//   1. The codes of an alphabet follow each other in alphabet order.
//   2. Around the string found are the game's other strings, hundreds of
//      them. In English text "e" is the commonest letter, then "t", "a"...
//      Only one placing of the alphabet on the codes gives that picture.
//
// Nothing is shown to a person until that placing has been found: a wrong
// match gives codes whose counts look nothing like a language.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "GlyphCipher.h"

namespace Discovery {

namespace {

const size_t MIN_CODE_LINE = 10;
const size_t MIN_SHORT_CODE_LINE = 4;
const size_t MAX_CODE_LINE = 64;
// Repeated symbols, plus symbols whose code is already known: what makes a
// row say enough to be looked for.
const size_t MIN_CODE_EVIDENCE = 4;
// The game's other strings are counted this far either side of a string found.
const u32 HARVEST_BYTES = 8192;
const size_t MAX_CODE_ADDRS = 8;
// How well the counts must fit, and how far ahead of the next best placing.
const int MIN_SMALL_LETTERS = 150;
const float MIN_SMALL_FIT = 0.90f;
const int MIN_CAPITALS = 60;
const float MIN_CAPITAL_FIT = 0.75f;
const float MIN_LEAD = 0.08f;

// How often each letter occurs in English text, per thousand.
const float ENGLISH[26] = {
	82, 15, 28, 43, 127, 22, 20, 61, 70, 1.5f, 7.7f, 40, 24, 67, 75, 19, 1, 60, 63, 91, 28, 9.8f, 24, 1.5f, 20, 0.7f,
};
// How often each letter begins an English word in a dictionary, per
// thousand: what capitals in menus and names follow.
const float ENGLISH_INITIAL[26] = {
	65, 50, 94, 56, 40, 40, 30, 37, 38, 9, 10, 32, 51, 20, 25, 77, 5, 50, 110, 50, 30, 15, 27, 1, 4, 3,
};

u32 Unit(const u8 *memory, size_t index) {
	return (u32)memory[index * 2] | ((u32)memory[index * 2 + 1] << 8);
}

u32 Swapped(u32 v) {
	return ((v & 0xFF) << 8) | (v >> 8);
}

float Fit(const std::map<u32, int> &counts, u32 first, const float *language, int *total) {
	float dot = 0, a = 0, b = 0;
	*total = 0;
	for (int i = 0; i < 26; i++) {
		auto it = counts.find(first + i);
		float c = it == counts.end() ? 0.0f : (float)it->second;
		*total += (int)c;
		dot += c * language[i];
		a += c * c;
		b += language[i] * language[i];
	}
	return a > 0 ? dot / std::sqrt(a * b) : 0;
}

}  // namespace

int GlyphBook::TableChar(const LetterTable &table, int raw) const {
	if (!table.found || raw < 0) {
		return -1;
	}
	int v = table.highByteFirst ? (int)Swapped((u32)raw) : raw;
	if (v >= table.small && v < table.small + 26) {
		return 'a' + (v - table.small);
	}
	if (table.capital >= 0 && v >= table.capital && v < table.capital + 26) {
		return 'A' + (v - table.capital);
	}
	return -1;
}

void GlyphBook::ApplyTable(u32 family) {
	auto table = tables_.find(family);
	if (table == tables_.end() || !table->second.found) {
		return;
	}
	for (size_t id = 0; id < symbols_.size(); id++) {
		const GlyphSymbol &s = symbols_[id];
		// A code some other search put here was never confirmed (or this
		// search would not have been needed): the table overrides it.
		if (s.raw < 0 || s.origin == GlyphOrigin::Table || Family(s.key.tex) != family) {
			continue;
		}
		int c = TableChar(table->second, s.raw);
		if (c >= 0) {
			SetCode((int)id, c, GlyphOrigin::Table);
		}
	}
}

void GlyphBook::InferLetters(u32 family, const u8 *memory, size_t size, u32 base) {
	LetterTable &table = tables_[family];
	if (table.found) {
		return;
	}
	// Count every 16-bit value around the strings found.
	std::map<u32, int> counts[2];  // As stored; and with the two bytes changed over.
	std::vector<std::pair<size_t, size_t>> done;
	for (u32 addr : codeAddrs_[family]) {
		size_t at = (addr - base) / 2;
		size_t from = at > HARVEST_BYTES / 2 ? at - HARVEST_BYTES / 2 : 0;
		size_t to = std::min(size / 2, at + HARVEST_BYTES / 2);
		for (size_t i = from; i < to; i++) {
			bool counted = false;
			for (const auto &range : done) {
				counted = counted || (i >= range.first && i < range.second);
			}
			if (counted) {
				continue;
			}
			u32 v = Unit(memory, i);
			if (v != 0 && v != 0xFFFF) {
				counts[0][v]++;
				counts[1][Swapped(v)]++;
			}
		}
		done.push_back(std::make_pair(from, to));
	}
	std::vector<int> raws;
	for (const GlyphSymbol &s : symbols_) {
		if (s.raw >= 0 && Family(s.key.tex) == family) {
			raws.push_back(s.raw);
		}
	}

	LetterTable best;
	for (int order = 0; order < 2; order++) {
		// An alphabet must hold some of the codes we know: try every placing that does.
		std::set<u32> firsts;
		for (int raw : raws) {
			u32 v = order ? Swapped((u32)raw) : (u32)raw;
			for (u32 j = 0; j < 26 && j <= v; j++) {
				firsts.insert(v - j);
			}
		}
		float top = 0, next = 0;
		u32 topFirst = 0;
		for (u32 first : firsts) {
			int total;
			float fit = Fit(counts[order], first, ENGLISH, &total);
			if (total < MIN_SMALL_LETTERS) {
				continue;
			}
			if (fit > top) {
				next = top;
				top = fit;
				topFirst = first;
			} else if (fit > next) {
				next = fit;
			}
		}
		if (top < MIN_SMALL_FIT || top - next < MIN_LEAD || top <= best.smallFit) {
			continue;
		}
		best = LetterTable();
		best.found = true;
		best.highByteFirst = order == 1;
		best.small = (int)topFirst;
		best.smallFit = top;
		// The capitals: another run of 26, clear of the small letters.
		float capTop = 0, capNext = 0;
		u32 capFirst = 0;
		for (u32 first : firsts) {
			if (first + 25 >= topFirst && first <= topFirst + 25) {
				continue;
			}
			int total;
			float fit = Fit(counts[order], first, ENGLISH_INITIAL, &total);
			if (total < MIN_CAPITALS) {
				continue;
			}
			if (fit > capTop) {
				capNext = capTop;
				capTop = fit;
				capFirst = first;
			} else if (fit > capNext) {
				capNext = fit;
			}
		}
		if (capTop >= MIN_CAPITAL_FIT && capTop - capNext >= MIN_LEAD) {
			best.capital = (int)capFirst;
			best.capitalFit = capTop;
		}
		best.capitalNext = capNext;
		best.capitalTop = capTop;
	}
	if (best.found) {
		best.space = table.space;
		table = best;
		OwnCodeConfirmed(family);
	}
}

void GlyphBook::OwnCodeConfirmed(u32 family) {
	{
		version_++;
		// Whatever the other searches made of this font was chance (seen in
		// Persona 2: "Adjrat BGw vtlruM" shown for "Adjust BGM volume").
		// Their letters, their trust and their idea of its spacing all go.
		for (size_t id = 0; id < symbols_.size(); id++) {
			const GlyphSymbol &s = symbols_[id];
			if (Family(s.key.tex) != family) {
				continue;
			}
			if (s.origin == GlyphOrigin::Matched) {
				SetCode((int)id, -1, GlyphOrigin::Unknown);
			}
			matchedLines_.erase(s.key.tex);
			orderedTextures_.erase(s.key.tex);
		}
		matchedLines_.erase(family);
		orderedTextures_.erase(family);
		spaceTextures_.erase(family);
		spacing_.erase(family);
	}
}

u32 GlyphBook::LearnFromCodeLine(const std::vector<int> &symbolsIn, const u8 *memory, size_t size, u32 base) {
	scanned_ = false;
	outcome_ = "says_too_little";
	std::vector<int> syms;
	std::vector<char> gapBefore;
	bool gap = false;
	for (int s : symbolsIn) {
		if (s < 0) {
			gap = true;
			continue;
		}
		gapBefore.push_back(gap && !syms.empty() ? 1 : 0);
		syms.push_back(s);
		gap = false;
	}
	size_t n = syms.size();
	if (n < MIN_SHORT_CODE_LINE || n > MAX_CODE_LINE || size < 4096) {
		return 0;
	}
	u32 family = Family(symbols_[syms[0]].key.tex);
	// A short row ("Load", "Title") is only looked for once the font's
	// letter code is known, to learn the one or two letters still missing.
	bool shortRow = n < MIN_CODE_LINE;
	if (shortRow && !UsesOwnCode(family)) {
		return 0;
	}
	u64 hash = 1469598103934665603ull;
	for (int s : syms) {
		if (Family(symbols_[s].key.tex) != family) {
			return 0;
		}
		hash = (hash ^ (u64)(u32)s) * 1099511628211ull;
	}
	// Each different symbol gets a small number of its own.
	std::vector<int> local(n);
	std::vector<int> distinct;
	for (size_t i = 0; i < n; i++) {
		auto it = std::find(distinct.begin(), distinct.end(), syms[i]);
		local[i] = (int)(it - distinct.begin());
		if (it == distinct.end()) {
			distinct.push_back(syms[i]);
		}
	}
	size_t d = distinct.size();

	bool tableFound = tables_.count(family) && tables_[family].found;
	bool anyKnown = false;
	for (int s : distinct) {
		anyKnown = anyKnown || symbols_[s].raw >= 0;
	}
	// Before the letters are worked out, the codes we hold come from one
	// string only, and may be a chance find. If a row cannot be found with
	// them, it is tried once more without them.
	for (int pass = 0; pass < (tableFound ? 1 : 2); pass++) {
		bool useKnown = pass == 0;
		size_t known = 0;
		for (int s : distinct) {
			known += useKnown && symbols_[s].raw >= 0 ? 1 : 0;
		}
		if (known == d) {
			outcome_ = "code_known_letter_not";
			return 0;
		}
		if (pass == 1 && !anyKnown) {
			break;  // Nothing was held back in the first pass.
		}
		if ((n - d) + known < MIN_CODE_EVIDENCE && !shortRow) {
			outcome_ = "says_too_little";
			continue;
		}
		if (shortRow && (known < 3 || d - known > 2)) {
			outcome_ = "says_too_little";
			continue;
		}

		std::vector<size_t> gaps;
		for (size_t i = 0; i < n; i++) {
			if (gapBefore[i]) {
				gaps.push_back(i);
			}
		}
		// Our gaps are guesses at spaces. First: every gap is a space in
		// memory. Then: none is. With two or three gaps, every mixture.
		std::vector<u32> masks;
		u32 all = gaps.empty() ? 0 : (1u << gaps.size()) - 1;
		masks.push_back(all);
		if (all != 0) {
			masks.push_back(0);
		}
		if (gaps.size() == 2 || gaps.size() == 3) {
			for (u32 m = 1; m < all; m++) {
				masks.push_back(m);
			}
		}
		if (gaps.size() > 3) {
			masks.resize(std::min(masks.size(), (size_t)2));
		}

		for (u32 mask : masks) {
			std::vector<size_t> off(n);
			std::vector<size_t> seps;
			size_t extra = 0;
			for (size_t i = 0; i < n; i++) {
				for (size_t g = 0; g < gaps.size(); g++) {
					if (gaps[g] == i && (mask & (1u << g))) {
						seps.push_back(i + extra);
						extra++;
					}
				}
				off[i] = i + extra;
			}
			size_t length = off[n - 1] + 1;
			size_t units = size / 2;
			if (units <= length) {
				continue;
			}
			// A quick first test for each place: a symbol whose code is
			// known, or else the two nearest equal symbols.
			size_t fa = 0, fb = 0;
			int wanted = -1;
			size_t nearest = n;
			for (size_t i = 0; i < n && wanted < 0; i++) {
				if (useKnown && symbols_[syms[i]].raw >= 0) {
					wanted = symbols_[syms[i]].raw;
					fa = off[i];
				}
			}
			if (wanted < 0) {
				for (size_t i = 0; i < n; i++) {
					for (size_t j = i + 1; j < n && j - i < nearest; j++) {
						if (local[i] == local[j]) {
							nearest = j - i;
							fa = off[i];
							fb = off[j];
						}
					}
				}
				if (nearest == n) {
					continue;
				}
			}
			scanned_ = true;
			std::vector<u32> found, cur(d);
			u32 foundSep = 0;
			size_t foundAt = 0;
			bool have = false, several = false;
			for (size_t p = 0; p + length <= units && !several; p++) {
				u32 a = Unit(memory, p + fa);
				if (wanted >= 0 ? a != (u32)wanted : (a == 0 || a != Unit(memory, p + fb))) {
					continue;
				}
				bool ok = true;
				std::fill(cur.begin(), cur.end(), 0);
				for (size_t i = 0; i < n && ok; i++) {
					u32 v = Unit(memory, p + off[i]);
					int l = local[i];
					if (v == 0 || v == 0xFFFF) {
						ok = false;
					} else if (cur[l] != 0) {
						ok = cur[l] == v;
					} else {
						int raw = useKnown ? symbols_[syms[i]].raw : -1;
						if (raw >= 0 && (u32)raw != v) {
							ok = false;
						}
						for (size_t k = 0; k < d && ok; k++) {
							ok = cur[k] != v;  // Two symbols never share a code.
						}
						// Nor may a symbol of this font outside the row have it.
						if (ok && useKnown && raw < 0) {
							auto owner = rawOwner_.find(std::make_pair(family, (int)v));
							ok = owner == rawOwner_.end();
						}
						cur[l] = v;
					}
				}
				u32 sep = 0;
				for (size_t k = 0; k < seps.size() && ok; k++) {
					u32 v = Unit(memory, p + seps[k]);
					ok = v != 0 && v != 0xFFFF && (k == 0 || v == sep);
					sep = v;
					for (size_t j = 0; j < d && ok; j++) {
						ok = cur[j] != v;
					}
				}
				if (!ok) {
					continue;
				}
				if (!have) {
					found = cur;
					foundSep = sep;
					foundAt = p;
					have = true;
				} else if (found != cur) {
					several = true;
				}
			}
			if (several) {
				outcome_ = "several_readings";
				return 0;
			}
			if (!have) {
				outcome_ = "not_in_memory";
				continue;
			}
			// In a short row a new code must be a letter of the known alphabets.
			bool letters = true;
			for (size_t k = 0; k < d && shortRow; k++) {
				letters = letters && (symbols_[distinct[k]].raw >= 0 || TableChar(tables_[family], (int)found[k]) >= 0);
			}
			if (!letters) {
				outcome_ = "not_in_memory";
				continue;
			}
			// Found. If the codes held before were left out, they were wrong.
			if (!useKnown) {
				for (GlyphSymbol &s : symbols_) {
					if (Family(s.key.tex) == family) {
						s.raw = -1;
					}
				}
				for (auto it = rawOwner_.begin(); it != rawOwner_.end();) {
					it = it->first.first == family ? rawOwner_.erase(it) : std::next(it);
				}
				codeLines_[family].clear();
				codeAddrs_[family].clear();
			}
			for (size_t k = 0; k < d; k++) {
				symbols_[distinct[k]].raw = (int)found[k];
				rawOwner_[std::make_pair(family, (int)found[k])] = distinct[k];
			}
			version_++;
			codeLines_[family].insert(hash);
			std::vector<u32> &addrs = codeAddrs_[family];
			u32 addr = base + (u32)(foundAt * 2);
			if (addrs.size() < MAX_CODE_ADDRS && std::find(addrs.begin(), addrs.end(), addr) == addrs.end()) {
				addrs.push_back(addr);
			}
			if (foundSep != 0) {
				tables_[family].space = (int)foundSep;
			}
			// Where this row's spaces really are, for the caller (as after
			// LearnFromLooseLine): a gap of ours with no space in memory was
			// no space, and between two symbols with no gap there was none.
			lastSpaces_.assign(n, 0);
			lastSpaces_[0] = -1;
			for (size_t g = 0; g < gaps.size(); g++) {
				lastSpaces_[gaps[g]] = (mask & (1u << g)) ? 1 : 0;
			}
			// A letter code remembered from an earlier session may fit at once.
			if (!tables_[family].found) {
				AdoptSavedTable(family);
			}
			// Two different rows that agree: now the letters may be worked out.
			if (codeLines_[family].size() >= 2) {
				InferLetters(family, memory, size, base);
			}
			ApplyTable(family);
			outcome_ = "";
			return addr;
		}
	}
	return 0;
}

std::string GlyphBook::DescribeTables() const {
	std::string out;
	char line[320];
	for (const auto &kv : codeAddrs_) {
		if (kv.second.empty()) {
			continue;
		}
		auto table = tables_.find(kv.first);
		size_t lines = codeLines_.count(kv.first) ? codeLines_.find(kv.first)->second.size() : 0;
		snprintf(line, sizeof(line), "  font texture 0x%08X: text kept in the game's own two-byte letter code; %d rows found in memory, the first at 0x%08X\n",
			kv.first, (int)lines, kv.second[0]);
		out += line;
		if (table == tables_.end() || !table->second.found) {
			out += "      which letter each code is has not been worked out yet (it needs two rows, and the game's strings around them must read like English)\n";
			continue;
		}
		const LetterTable &t = table->second;
		snprintf(line, sizeof(line), "      a code is two bytes, %s; small letters a to z are 0x%04X to 0x%04X (fit %.2f)\n",
			t.highByteFirst ? "high byte first" : "low byte first", t.small, t.small + 25, t.smallFit);
		out += line;
		if (t.capital >= 0) {
			snprintf(line, sizeof(line), "      capitals A to Z are 0x%04X to 0x%04X (fit %.2f)\n", t.capital, t.capital + 25, t.capitalFit);
		} else {
			snprintf(line, sizeof(line), "      capitals: not settled (best fit %.2f, next %.2f)\n", t.capitalTop, t.capitalNext);
		}
		out += line;
		if (t.space >= 0) {
			u32 v = t.highByteFirst ? Swapped((u32)t.space) : (u32)t.space;
			snprintf(line, sizeof(line), "      the space is 0x%04X\n", v);
			out += line;
		}
	}
	return out;
}

}  // namespace Discovery
