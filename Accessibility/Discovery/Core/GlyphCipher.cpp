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

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "GlyphCipher.h"

namespace Discovery {

namespace {

// A line shorter than this, or with fewer different symbols, says too little
// to be searched for.
const size_t MIN_LINE = 5;
const size_t MIN_DISTINCT = 3;
const size_t MAX_LINE = 96;
// How many matched symbols must agree before a font's layout rule is believed.
const int MIN_FOR_LAYOUT = 5;
const size_t MAX_SYMBOLS = 20000;
// The layout rule names symbols only up to here. Every font agrees on plain
// ASCII; beyond it, games put their own extra characters in their own order,
// so those are left unknown until a line in memory shows what they are.
const int LAYOUT_MAX_CODE = 0x7E;
// The word search looks for stretches of this many symbols. Six is the
// shortest that chance does not produce: five exact differences in a row.
const size_t WORD_WINDOW = 6;
const size_t MAX_WORD_WINDOWS = 160;

// Character codes a line of text may contain under the "ordinary single-byte
// text" reading: printable ASCII. Accented letters are left out on purpose
// for now: allowing every byte above 0xA0 makes most of a game's binary data
// look like possible text, which made each search many times slower. A line
// with an accented letter is therefore not found yet; its font is still
// learned from the other lines.
bool Allowed(u8 c) {
	return c >= 0x20 && c <= 0x7E;
}

bool Plain(u8 c) {
	return c == ' ' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

}  // namespace

int GlyphBook::Intern(const SpriteDraw &draw) {
	GlyphKey key;
	key.tex = draw.texAddr;
	key.texW = draw.texW;
	key.texH = draw.texH;
	float u0 = std::min(draw.u0, draw.u1), u1 = std::max(draw.u0, draw.u1);
	float v0 = std::min(draw.v0, draw.v1), v1 = std::max(draw.v0, draw.v1);
	key.u0 = (u16)(u0 + 0.5f);
	key.v0 = (u16)(v0 + 0.5f);
	key.u1 = (u16)(u1 + 0.5f);
	key.v1 = (u16)(v1 + 0.5f);
	auto it = ids_.find(key);
	if (it != ids_.end()) {
		return it->second;
	}
	if (symbols_.size() >= MAX_SYMBOLS) {
		Clear();  // A game cycling through textures: start again rather than grow for ever.
	}
	GlyphSymbol s;
	s.key = key;
	s.texW = draw.texW;
	int id = (int)symbols_.size();
	symbols_.push_back(s);
	ids_[key] = id;

	// A font whose layout is already understood names its new symbols at once.
	FontKey font;
	int index;
	if (CellIndex(symbols_[id], &font, &index)) {
		auto rule = layouts_.find(font);
		if (rule != layouts_.end() && rule->second.found) {
			int code = index + rule->second.offset;
			if (code >= 0x20 && code <= LAYOUT_MAX_CODE) {
				SetCode(id, code, GlyphOrigin::Layout);
			}
		}
	}
	// So does a font whose remembered letters were confirmed this session.
	if (rememberedOk_.count(key.tex) && symbols_[id].code < 0) {
		auto r = remembered_.find(key);
		if (r != remembered_.end()) {
			SetCode(id, r->second, GlyphOrigin::Matched);
		}
	}
	// So does a strip of digits.
	for (const DigitStrip &strip : strips_) {
		int digit = StripDigit(strip, symbols_[id]);
		if (digit >= 0 && symbols_[id].code < 0) {
			SetCode(id, '0' + digit, GlyphOrigin::Digits);
		}
	}
	return id;
}

bool GlyphBook::IsGridCell(int id) const {
	FontKey font;
	int index;
	return CellIndex(symbols_[id], &font, &index);
}

u32 GlyphBook::Family(u32 tex) const {
	for (int hops = 0; hops < 16; hops++) {
		auto it = family_.find(tex);
		if (it == family_.end()) {
			break;
		}
		tex = it->second;
	}
	return tex;
}

void GlyphBook::Join(u32 texA, u32 texB) {
	u32 a = Family(texA), b = Family(texB);
	if (a == b) {
		return;
	}
	family_[b] = a;
	auto lines = matchedLines_.find(b);
	if (lines != matchedLines_.end()) {
		matchedLines_[a].insert(lines->second.begin(), lines->second.end());
		matchedLines_.erase(b);
	}
	if (orderedTextures_.erase(b)) {
		orderedTextures_.insert(a);
	}
	if (spaceTextures_.erase(b)) {
		spaceTextures_.insert(a);
	}
	version_++;
}

void GlyphBook::NoteSpacing(u32 tex, float gap, bool space) {
	Spacing &s = spacing_[Family(tex)];
	if (space) {
		s.narrowestWith = std::min(s.narrowestWith, gap);
	} else {
		s.widestWithout = std::max(s.widestWithout, gap);
	}
}

float GlyphBook::SpaceGap(u32 tex) const {
	auto it = spacing_.find(Family(tex));
	if (it == spacing_.end() || it->second.narrowestWith > 999 || it->second.widestWithout < -999
		|| it->second.narrowestWith <= it->second.widestWithout) {
		return 0;
	}
	return (it->second.narrowestWith + it->second.widestWithout) * 0.5f;
}

bool GlyphBook::TextureTrusted(u32 tex) const {
	tex = Family(tex);
	if (orderedTextures_.count(tex)) {
		return true;
	}
	auto it = matchedLines_.find(tex);
	return it != matchedLines_.end() && it->second.size() >= 2;
}

bool GlyphBook::Trusted(int id) const {
	const GlyphSymbol &s = symbols_[id];
	return s.code >= 0 && (s.origin == GlyphOrigin::Layout || s.origin == GlyphOrigin::Digits || s.origin == GlyphOrigin::Table || TextureTrusted(s.key.tex));
}

int GlyphBook::CellNumber(int id) const {
	FontKey font;
	int index;
	return CellIndex(symbols_[id], &font, &index) ? index : -1;
}

void GlyphBook::Clear() {
	matchedLines_.clear();
	spacing_.clear();
	lastSpaces_.clear();
	family_.clear();
	spaceTextures_.clear();
	orderedTextures_.clear();
	strips_.clear();
	numberRecords_.clear();
	rememberedOk_.clear();  // The fonts must be confirmed afresh. What is remembered stays.
	tables_.clear();
	codeLines_.clear();
	codeAddrs_.clear();
	rawOwner_.clear();
	symbols_.clear();
	ids_.clear();
	layouts_.clear();
	known_ = 0;
	version_++;
}

void GlyphBook::NoteAdvance(int id, float advance) {
	if (id < 0 || id >= (int)symbols_.size() || advance <= 0.02f) {
		return;
	}
	GlyphSymbol &s = symbols_[id];
	if (s.minAdvance == 0 || advance < s.minAdvance) {
		s.minAdvance = advance;
	}
}

void GlyphBook::SetCode(int id, int code, GlyphOrigin origin) {
	GlyphSymbol &s = symbols_[id];
	if (s.code < 0 && code >= 0) known_++;
	if (s.code >= 0 && code < 0) known_--;
	if (s.code != code || s.origin != origin) {
		version_++;
	}
	s.code = code;
	if (code == 0x20) {
		spaceTextures_.insert(Family(s.key.tex));
	}
	s.origin = code < 0 ? GlyphOrigin::Unknown : origin;
}

// A font texture is usually a grid of equal cells. This gives a symbol's cell
// number, counting along the rows, if its rectangle is exactly one cell.
bool GlyphBook::CellIndex(const GlyphSymbol &s, FontKey *font, int *index) const {
	int w = s.key.u1 - s.key.u0, h = s.key.v1 - s.key.v0;
	// A cell under 4 wide is a narrow letter of a packed font (an "i" two
	// texels wide divides any texture evenly), not a cell of a grid.
	if (w < 4 || h <= 0 || s.texW == 0 || s.texW % w != 0 || s.key.u0 % w != 0 || s.key.v0 % h != 0) {
		return false;
	}
	font->tex = s.key.tex;
	font->cellW = (u16)w;
	font->cellH = (u16)h;
	*index = (s.key.v0 / h) * (s.texW / w) + s.key.u0 / w;
	return true;
}

void GlyphBook::UpdateLayout(const FontKey &font) {
	// Do all symbols whose codes were read from memory sit at "code - offset"?
	int count = 0, offset = 0;
	bool consistent = true;
	for (const GlyphSymbol &s : symbols_) {
		FontKey f;
		int index;
		if (s.origin != GlyphOrigin::Matched || !CellIndex(s, &f, &index) || f < font || font < f) {
			continue;
		}
		int o = s.code - index;
		if (count == 0) {
			offset = o;
		} else if (o != offset) {
			consistent = false;
		}
		count++;
	}
	LayoutRule &rule = layouts_[font];
	bool found = consistent && count >= MIN_FOR_LAYOUT;
	if (found == rule.found && (!found || offset == rule.offset)) {
		return;
	}
	rule.found = found;
	rule.offset = offset;
	// Apply the rule to (or withdraw it from) the symbols it named.
	for (size_t i = 0; i < symbols_.size(); i++) {
		const GlyphSymbol &s = symbols_[i];
		FontKey f;
		int index;
		if (s.origin == GlyphOrigin::Matched || !CellIndex(s, &f, &index) || f < font || font < f) {
			continue;
		}
		int code = found ? index + offset : -1;
		if (code < 0x20 || code > LAYOUT_MAX_CODE) {
			code = -1;
		}
		SetCode((int)i, code, GlyphOrigin::Layout);
	}
}

void TextIndex::Build(const u8 *memory, size_t size) {
	narrow.clear();
	wide.clear();
	size_t runStart = 0;
	bool inRun = false;
	for (size_t i = 0; i <= size; i++) {
		bool ok = i < size && Allowed(memory[i]);
		if (ok && !inRun) {
			runStart = i;
			inRun = true;
		} else if (!ok && inRun) {
			inRun = false;
			if (i - runStart >= MIN_LINE) {
				narrow.push_back(Span{ (u32)runStart, (u32)(i - runStart) });
			}
		}
	}
	for (size_t parity = 0; parity < 2; parity++) {
		inRun = false;
		for (size_t i = parity; ; i += 2) {
			bool ok = i + 1 < size && memory[i + 1] == 0 && Allowed(memory[i]);
			if (ok && !inRun) {
				runStart = i;
				inRun = true;
			} else if (!ok && inRun) {
				inRun = false;
				if (i - runStart >= MIN_LINE * 2) {
					wide.push_back(Span{ (u32)runStart, (u32)(i - runStart) });
				}
			}
			if (i + 1 >= size) {
				break;
			}
		}
	}
}

u32 GlyphBook::LearnFromLine(const std::vector<int> &symbols, const u8 *memory, size_t size, u32 base, const TextIndex &index) {
	const size_t n = symbols.size();
	outcome_ = "too_short";
	if (n < MIN_LINE || n > MAX_LINE) {
		return 0;
	}
	// first[i]: where this position's symbol first appeared in the line.
	// A gap (-1) counts as one more symbol: every gap must be the same code.
	int first[MAX_LINE];
	int known[MAX_LINE];
	int cell[MAX_LINE];  // The symbol's cell number in its font texture, or -1.
	size_t withCell = 0;
	bool allHaveCell = true;
	size_t distinct = 0;
	u32 tex = 0;
	for (size_t i = 0; i < n; i++) {
		first[i] = (int)i;
		for (size_t j = 0; j < i; j++) {
			if (symbols[j] == symbols[i]) {
				first[i] = (int)j;
				break;
			}
		}
		distinct += first[i] == (int)i ? 1 : 0;
		known[i] = -1;
		cell[i] = -1;
		if (symbols[i] >= 0) {
			const GlyphSymbol &s = symbols_[symbols[i]];
			tex = Family(s.key.tex);
			FontKey f;
			if (CellIndex(s, &f, &cell[i])) {
				withCell += first[i] == (int)i ? 1 : 0;
			} else {
				cell[i] = -1;
				allHaveCell = false;
			}
			// Only codes read from memory, in a confirmed font, are binding.
			// A code worked out from the layout is a deduction, and a first,
			// unconfirmed match may be a coincidence: a match may correct both.
			if (s.origin == GlyphOrigin::Matched && TextureTrusted(s.key.tex)) {
				known[i] = s.code;
			}
		}
	}
	if (distinct < MIN_DISTINCT) {
		return 0;
	}
	outcome_ = "not_in_memory";
	// The plain pattern search is only believed for lines that say a lot:
	// long, with several repeated letters. Gaps do not count as repeats: a
	// line whose only repeated "symbol" is the space fits far too much text.
	size_t realCount = 0, realRepeats = 0;
	u64 lineHash = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++) {
		lineHash = (lineHash ^ (u64)(u32)symbols[i]) * 1099511628211ull;
		if (symbols[i] >= 0) {
			realCount++;
			realRepeats += first[i] != (int)i ? 1 : 0;
		}
	}
	const bool wantPlain = realCount >= 8 && realRepeats >= 3;
	// Codes already taken by other symbols of this font cannot be given to a new one.
	bool taken[256] = {};
	for (const GlyphSymbol &s : symbols_) {
		if (s.origin == GlyphOrigin::Matched && Family(s.key.tex) == tex && TextureTrusted(tex) && s.code >= 0 && s.code < 256) {
			taken[s.code] = true;
		}
	}
	for (size_t i = 0; i < n; i++) {
		if (known[i] >= 0 && known[i] < 256) {
			taken[known[i]] = false;  // Its own code is of course allowed.
		}
	}

	// Two readings are tried: one byte per character, then two (UTF-16 with a
	// zero high byte). The first reading that gives exactly one answer wins.
	for (int unit = 1; unit <= 2; unit++) {
		if (size < n * unit) {
			continue;
		}
		u8 found[MAX_LINE];
		u8 cur[MAX_LINE];
		size_t foundAt = 0;
		bool have = false, ambiguous = false;
		// A second, stricter kind of answer: a reading in which every symbol's
		// code is its cell number plus one constant. Font textures are laid
		// out that way, and chance does not produce it, so such a reading is
		// accepted even when the looser search finds other candidates.
		// It needs a line whose cells jump about, as the letters of a word do.
		// A row of neighbouring cells (an on-screen keyboard: A B C D...) fits
		// any table of rising numbers in memory, so it proves nothing.
		std::set<int> steps;
		int previousCell = -1;
		for (size_t k = 0; k < n; k++) {
			if (cell[k] >= 0) {
				if (previousCell >= 0) {
					steps.insert(cell[k] - previousCell);
				}
				previousCell = cell[k];
			}
		}
		const bool wantOrdered = allHaveCell && withCell >= 4 && steps.size() >= 3;
		u8 ordered[MAX_LINE];
		size_t orderedAt = 0;
		bool haveOrdered = false, orderedAmbiguous = false;
		size_t orderedPlain = 0;
		u32 owner[256] = {};
		u32 stamp = 0;
		if (!wantPlain && !wantOrdered) {
			outcome_ = "says_too_little";
			return 0;
		}
		bool usedOrdered = false;
		const std::vector<TextIndex::Span> &spans = unit == 1 ? index.narrow : index.wide;
		for (const TextIndex::Span &span : spans) {
		if (span.length < n * unit || (size_t)span.start + span.length > size) {
			continue;
		}
		if (ambiguous && !wantOrdered) {
			break;
		}
		size_t p = span.start;
		const size_t limit = (size_t)span.start + span.length - n * unit;
		while (p <= limit && (!ambiguous || wantOrdered)) {
			stamp++;
			size_t i = 0;
			bool disallowed = false;
			for (; i < n; i++) {
				u8 c = memory[p + i * unit];
				if (!Allowed(c) || (unit == 2 && memory[p + i * unit + 1] != 0)) {
					disallowed = true;
					break;
				}
				if (first[i] != (int)i) {
					if (c != cur[first[i]]) break;
				} else {
					if (owner[c] == stamp) break;                    // Two symbols, one code.
					if (known[i] >= 0 ? c != known[i] : taken[c]) break;
					owner[c] = stamp;
				}
				cur[i] = c;
			}
			if (i < n) {
				// A byte that cannot be text rules out every start up to it.
				p += disallowed ? (i + 1) * unit : unit;
				continue;
			}
			// The pattern fits. Real text is mostly letters, digits and spaces.
			size_t plain = 0;
			for (size_t k = 0; k < n; k++) {
				plain += Plain(cur[k]) ? 1 : 0;
			}
			if (wantPlain && plain * 10 >= n * 7) {
				if (!have) {
					memcpy(found, cur, n);
					foundAt = p;
					have = true;
				} else if (memcmp(found, cur, n) != 0) {
					ambiguous = true;  // Two different readings: no answer.
				}
			}
			if (wantOrdered) {
				bool inOrder = true;
				int offset = 0;
				bool haveOffset = false;
				for (size_t k = 0; k < n && inOrder; k++) {
					if (cell[k] < 0) {
						continue;
					}
					int o = (int)cur[k] - cell[k];
					if (!haveOffset) {
						offset = o;
						haveOffset = true;
					} else if (o != offset) {
						inOrder = false;
					}
				}
				if (inOrder && plain * 10 >= n * 6) {
					// Memory can hold the same line twice in layout order: as
					// character codes, and as the game's own cell numbers
					// (the codes minus a constant). The copy that reads as
					// letters and digits is the text; the other is discarded.
					if (!haveOrdered || plain > orderedPlain) {
						memcpy(ordered, cur, n);
						orderedAt = p;
						orderedPlain = plain;
						haveOrdered = true;
						orderedAmbiguous = false;
					} else if (plain == orderedPlain && memcmp(ordered, cur, n) != 0) {
						orderedAmbiguous = true;
					}
				}
			}
			p += unit;
		}
		}
		if (haveOrdered && !orderedAmbiguous) {
			memcpy(found, ordered, n);
			foundAt = orderedAt;
			usedOrdered = true;
		} else if (ambiguous || orderedAmbiguous) {
			outcome_ = orderedAmbiguous ? "two_layout_readings" : "several_readings";
			return 0;
		} else if (!have) {
			continue;
		}
		// An earlier, unconfirmed match of this font that disagrees with this
		// one was a coincidence: forget it and start again from this line.
		if (!TextureTrusted(tex)) {
			bool disagrees = false;
			for (size_t i = 0; i < n; i++) {
				if (symbols[i] >= 0 && symbols_[symbols[i]].origin == GlyphOrigin::Matched && symbols_[symbols[i]].code != found[i]) {
					disagrees = true;
				}
			}
			if (disagrees) {
				for (size_t k = 0; k < symbols_.size(); k++) {
					if (Family(symbols_[k].key.tex) == tex && symbols_[k].origin == GlyphOrigin::Matched) {
						SetCode((int)k, -1, GlyphOrigin::Unknown);
					}
				}
				matchedLines_[tex].clear();
				conflicts_++;
			}
		}
		matchedLines_[tex].insert(lineHash);
		if (usedOrdered) {
			orderedTextures_.insert(tex);
		}
		version_++;
		FontKey font{};
		bool anyFont = false;
		for (size_t i = 0; i < n; i++) {
			if (symbols[i] < 0 || first[i] != (int)i) {
				continue;
			}
			SetCode(symbols[i], found[i], GlyphOrigin::Matched);
			FontKey f;
			int index;
			if (CellIndex(symbols_[symbols[i]], &f, &index)) {
				font = f;
				anyFont = true;
			}
		}
		if (anyFont) {
			UpdateLayout(font);
		}
		outcome_ = "";
		return base + (u32)foundAt;
	}
	outcome_ = "not_in_memory";
	return 0;
}

u32 GlyphBook::LearnFromWords(const std::vector<int> &symbols, const u8 *memory, size_t size, u32 base, const TextIndex &index) {
	outcome_ = "no_words";
	const size_t n = symbols.size();
	if (n < WORD_WINDOW) {
		return 0;
	}
	// Every symbol must be a cell of one grid font.
	std::vector<int> cell(n, -1);
	FontKey font{};
	bool haveFont = false;
	u32 tex = 0;
	for (size_t i = 0; i < n; i++) {
		if (symbols[i] < 0) {
			continue;  // A gap: stretches do not cross it.
		}
		FontKey f;
		if (!CellIndex(symbols_[symbols[i]], &f, &cell[i])) {
			return 0;
		}
		if (haveFont && (f < font || font < f)) {
			return 0;
		}
		font = f;
		haveFont = true;
		tex = Family(f.tex);
	}
	if (!haveFont) {
		return 0;
	}

	// The stretches worth looking for: cells that jump about as the letters
	// of a word do (see the same test in LearnFromLine), each stretch once.
	struct Window {
		size_t start;
		int c[WORD_WINDOW];
	};
	std::vector<Window> windows;
	std::set<std::vector<int>> seenStretches;
	for (size_t s = 0; s + WORD_WINDOW <= n && windows.size() < MAX_WORD_WINDOWS; s++) {
		Window w;
		w.start = s;
		bool whole = true;
		std::set<int> distinct, steps;
		for (size_t k = 0; k < WORD_WINDOW; k++) {
			w.c[k] = cell[s + k];
			if (symbols[s + k] < 0) {
				whole = false;
				break;
			}
			distinct.insert(w.c[k]);
			if (k > 0) {
				steps.insert(w.c[k] - w.c[k - 1]);
			}
		}
		if (!whole || distinct.size() < 4 || steps.size() < 3) {
			continue;
		}
		if (!seenStretches.insert(std::vector<int>(w.c, w.c + WORD_WINDOW)).second) {
			continue;
		}
		windows.push_back(w);
	}
	if (windows.size() < 2) {
		outcome_ = "says_too_little";
		return 0;
	}
	// Which stretches could start at a place in memory is decided by the
	// first two differences there, so one pass over memory serves them all.
	std::map<int, std::vector<int>> byStart;
	for (size_t i = 0; i < windows.size(); i++) {
		const Window &w = windows[i];
		byStart[(w.c[1] - w.c[0]) * 1024 + (w.c[2] - w.c[0])].push_back((int)i);
	}

	struct Reading {
		std::set<int> found;   // Which stretches were found with this constant.
		size_t plain = 0;      // Letters, digits and spaces among them.
		size_t firstAt = 0;
	};
	outcome_ = "not_in_memory";
	for (int unit = 1; unit <= 2; unit++) {
		std::map<int, Reading> readings;  // By constant.
		const std::vector<TextIndex::Span> &spans = unit == 1 ? index.narrow : index.wide;
		for (const TextIndex::Span &span : spans) {
			if (span.length < WORD_WINDOW * unit || (size_t)span.start + span.length > size) {
				continue;
			}
			const size_t limit = (size_t)span.start + span.length - WORD_WINDOW * unit;
			for (size_t p = span.start; p <= limit; p += unit) {
				int b0 = memory[p];
				auto candidates = byStart.find((memory[p + unit] - b0) * 1024 + (memory[p + 2 * unit] - b0));
				if (candidates == byStart.end()) {
					continue;
				}
				for (int wi : candidates->second) {
					const Window &w = windows[wi];
					int constant = b0 - w.c[0];
					size_t plain = 0;
					bool fits = true;
					for (size_t k = 0; k < WORD_WINDOW && fits; k++) {
						u8 c = memory[p + k * unit];
						fits = Allowed(c) && (int)c - w.c[k] == constant && (unit == 1 || memory[p + k * unit + 1] == 0);
						plain += Plain(c) ? 1 : 0;
					}
					if (!fits) {
						continue;
					}
					Reading &r = readings[constant];
					if (r.found.empty()) {
						r.firstAt = p;
					}
					if (r.found.insert(wi).second) {
						r.plain += plain;
					}
				}
			}
		}
		// The winner: found the most stretches; among equals, reads most like text.
		const Reading *best = nullptr;
		int bestConstant = 0;
		bool tie = false;
		for (const auto &kv : readings) {
			const Reading &r = kv.second;
			if (!best || r.found.size() > best->found.size() || (r.found.size() == best->found.size() && r.plain > best->plain)) {
				best = &r;
				bestConstant = kv.first;
				tie = false;
			} else if (r.found.size() == best->found.size() && r.plain == best->plain) {
				tie = true;
			}
		}
		if (!best || best->found.size() < 2) {
			continue;  // One stretch alone is not believed.
		}
		if (tie) {
			outcome_ = "two_layout_readings";
			return 0;
		}
		if (best->plain * 10 < best->found.size() * WORD_WINDOW * 6) {
			outcome_ = "words_not_text";
			return 0;
		}
		// It must agree with what this font is already known to say.
		if (TextureTrusted(tex)) {
			for (size_t i = 0; i < n; i++) {
				if (symbols[i] >= 0 && symbols_[symbols[i]].origin == GlyphOrigin::Matched && symbols_[symbols[i]].code != cell[i] + bestConstant) {
					outcome_ = "words_disagree";
					return 0;
				}
			}
		} else {
			// An earlier, unconfirmed match of this font that disagrees was a coincidence.
			bool disagrees = false;
			for (const GlyphSymbol &s : symbols_) {
				FontKey f;
				int idx;
				if (Family(s.key.tex) == tex && s.origin == GlyphOrigin::Matched && CellIndex(s, &f, &idx) && s.code != idx + bestConstant) {
					disagrees = true;
				}
			}
			if (disagrees) {
				for (size_t k = 0; k < symbols_.size(); k++) {
					if (Family(symbols_[k].key.tex) == tex && symbols_[k].origin == GlyphOrigin::Matched) {
						SetCode((int)k, -1, GlyphOrigin::Unknown);
					}
				}
				matchedLines_[tex].clear();
				conflicts_++;
			}
		}
		u64 hash = 1469598103934665603ull;
		for (int sym : symbols) {
			hash = (hash ^ (u64)(u32)sym) * 1099511628211ull;
		}
		matchedLines_[tex].insert(hash);
		orderedTextures_.insert(tex);
		version_++;
		for (int wi : best->found) {
			const Window &w = windows[wi];
			for (size_t k = 0; k < WORD_WINDOW; k++) {
				SetCode(symbols[w.start + k], w.c[k] + bestConstant, GlyphOrigin::Matched);
			}
		}
		UpdateLayout(font);
		outcome_ = "";
		return base + (u32)best->firstAt;
	}
	return 0;
}

u32 GlyphBook::LearnFromLooseLine(const std::vector<int> &symbolsIn, const u8 *memory, size_t size, u32 base, const TextIndex &index) {
	outcome_ = "not_in_memory";
	std::vector<int> all;
	for (int sym : symbolsIn) {
		if (sym >= 0) {
			all.push_back(sym);
		}
	}
	lastSpaces_.assign(all.size(), -1);
	// Stricter than the plain pattern search in LearnFromLine, because this
	// search forgives more: only long rows with several repeated letters, and
	// (below) only readings that look like written words.
	const size_t MIN_LOOSE = 12;
	const size_t MAX_DROPPED = 6;
	for (size_t dropped = 0; dropped <= MAX_DROPPED && all.size() >= dropped + MIN_LOOSE; dropped++) {
		const size_t n = std::min(all.size() - dropped, MAX_LINE);
		const int *symbols = all.data() + dropped;
		int first[MAX_LINE];
		int known[MAX_LINE];
		size_t repeats = 0;
		u32 tex = 0;
		u64 lineHash = 1469598103934665603ull;
		for (size_t i = 0; i < n; i++) {
			first[i] = (int)i;
			for (size_t j = 0; j < i; j++) {
				if (symbols[j] == symbols[i]) {
					first[i] = (int)j;
					break;
				}
			}
			repeats += first[i] != (int)i ? 1 : 0;
			const GlyphSymbol &s = symbols_[symbols[i]];
			tex = Family(s.key.tex);
			known[i] = s.origin == GlyphOrigin::Matched && TextureTrusted(s.key.tex) ? s.code : -1;
			lineHash = (lineHash ^ (u64)(u32)symbols[i]) * 1099511628211ull;
		}
		if (repeats < 4) {
			outcome_ = "says_too_little";
			continue;
		}
		bool taken[256] = {};
		for (const GlyphSymbol &s : symbols_) {
			if (s.origin == GlyphOrigin::Matched && Family(s.key.tex) == tex && TextureTrusted(tex) && s.code >= 0 && s.code < 256) {
				taken[s.code] = true;
			}
		}
		for (size_t i = 0; i < n; i++) {
			if (known[i] >= 0 && known[i] < 256) {
				taken[known[i]] = false;
			}
		}

		for (int unit = 1; unit <= 2; unit++) {
			u8 found[MAX_LINE];
			u8 cur[MAX_LINE];
			signed char skipped[MAX_LINE];
			signed char foundSkipped[MAX_LINE];
			size_t foundAt = 0;
			bool have = false, ambiguous = false;
			u32 owner[256] = {};
			u32 stamp = 0;
			const std::vector<TextIndex::Span> &spans = unit == 1 ? index.narrow : index.wide;
			for (const TextIndex::Span &span : spans) {
				if (ambiguous) {
					break;
				}
				const size_t spanEnd = std::min((size_t)span.start + span.length, size);
				for (size_t p = span.start; p + unit <= spanEnd && !ambiguous; p += unit) {
					stamp++;
					size_t q = p;
					bool haveSeparator = false;
					u32 separator = 0;
					size_t i = 0;
					for (; i < n; i++) {
						if (q + 2 * unit > size) {
							break;
						}
						u32 v = unit == 1 ? memory[q] : (u32)memory[q] | ((u32)memory[q + 1] << 8);
						skipped[i] = 0;
						if (i > 0) {
							// One space may stand here, or the game's own space
							// code: something that is not a character, never
							// zero (the end of a string), the same all along.
							bool otherCode = v != 0 && (v > 0xFF || !Allowed((u8)v));
							if (v == 0x20 || (otherCode && (!haveSeparator || v == separator))) {
								if (otherCode) {
									separator = v;
									haveSeparator = true;
								}
								q += unit;
								skipped[i] = 1;
								v = unit == 1 ? memory[q] : (u32)memory[q] | ((u32)memory[q + 1] << 8);
							}
						}
						if (v > 0xFF || v == 0x20 || !Allowed((u8)v)) {
							break;
						}
						u8 c = (u8)v;
						if (first[i] != (int)i) {
							if (c != cur[first[i]]) break;
						} else {
							if (owner[c] == stamp) break;
							if (known[i] >= 0 ? c != known[i] : taken[c]) break;
							owner[c] = stamp;
						}
						cur[i] = c;
						q += unit;
					}
					if (i < n) {
						continue;
					}
					size_t plain = 0;
					for (size_t k = 0; k < n; k++) {
						plain += Plain(cur[k]) ? 1 : 0;
					}
					if (plain * 10 < n * 7) {
						continue;
					}
					// Written words are mostly small letters. A reading in
					// capitals and signs is far more often a chance fit to
					// data. (A game that writes all in capitals is then only
					// read through the stricter searches.)
					size_t small = 0;
					for (size_t k = 0; k < n; k++) {
						small += cur[k] >= 'a' && cur[k] <= 'z' ? 1 : 0;
					}
					if (small * 2 < n) {
						continue;
					}
					if (!have) {
						memcpy(found, cur, n);
						memcpy(foundSkipped, skipped, n);
						foundAt = p;
						have = true;
					} else if (memcmp(found, cur, n) != 0) {
						ambiguous = true;
					}
				}
			}
			if (ambiguous) {
				outcome_ = "several_readings";
				return 0;
			}
			if (!have) {
				continue;
			}
			for (size_t i = 1; i < n; i++) {
				lastSpaces_[dropped + i] = foundSkipped[i];
			}
			if (!TextureTrusted(tex)) {
				bool disagrees = false;
				for (size_t i = 0; i < n; i++) {
					if (symbols_[symbols[i]].origin == GlyphOrigin::Matched && symbols_[symbols[i]].code != found[i]) {
						disagrees = true;
					}
				}
				if (disagrees) {
					for (size_t k = 0; k < symbols_.size(); k++) {
						if (Family(symbols_[k].key.tex) == tex && symbols_[k].origin == GlyphOrigin::Matched) {
							SetCode((int)k, -1, GlyphOrigin::Unknown);
						}
					}
					matchedLines_[tex].clear();
					conflicts_++;
				}
			}
			matchedLines_[tex].insert(lineHash);
			version_++;
			for (size_t i = 0; i < n; i++) {
				if (first[i] != (int)i) {
					continue;
				}
				SetCode(symbols[i], found[i], GlyphOrigin::Matched);
				FontKey f;
				int cellIndex;
				if (CellIndex(symbols_[symbols[i]], &f, &cellIndex)) {
					UpdateLayout(f);
				}
			}
			outcome_ = "";
			return base + (u32)foundAt;
		}
		outcome_ = "not_in_memory";
	}
	return 0;
}

std::string GlyphBook::Describe() const {
	struct Tally { int total = 0, matched = 0, layout = 0; };
	std::map<FontKey, Tally> fonts;
	std::map<u32, Tally> loose;
	for (const GlyphSymbol &s : symbols_) {
		FontKey f;
		int index;
		Tally &t = CellIndex(s, &f, &index) ? fonts[f] : loose[s.key.tex];
		t.total++;
		t.matched += s.origin == GlyphOrigin::Matched ? 1 : 0;
		t.layout += s.origin == GlyphOrigin::Layout ? 1 : 0;
	}
	std::string out;
	char line[200];
	for (const auto &kv : fonts) {
		if (kv.second.matched + kv.second.layout == 0) {
			continue;
		}
		auto rule = layouts_.find(kv.first);
		char ruleText[64] = "no layout rule yet";
		if (rule != layouts_.end() && rule->second.found) {
			snprintf(ruleText, sizeof(ruleText), "layout rule: code = cell number + %d", rule->second.offset);
		}
		snprintf(line, sizeof(line), "  font texture 0x%08X, cells %dx%d: %d symbols seen, %d read from memory, %d from the layout; %s\n",
			kv.first.tex, kv.first.cellW, kv.first.cellH, kv.second.total, kv.second.matched, kv.second.layout, ruleText);
		out += line;
	}
	return out;
}

}  // namespace Discovery
