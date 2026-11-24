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

#pragma once

// The glyph cipher: reading text a game draws with its own font, without
// looking at a single pixel.
//
// A game with its own font draws each letter as a small rectangle taken from
// a font texture. We can see WHICH rectangle (the "glyph": a texture address
// plus a rectangle in it) and where it lands on screen, but not which letter
// it is. So a line of text is, to us, a row of unknown symbols: 7 2 9 9 4.
//
// Somewhere in the game's memory is the string that line was drawn from, and
// it has the same pattern of repeats: third and fourth code equal, the rest
// different. The book searches memory for that pattern. A match tells us
// which code each symbol stands for. If the codes are ordinary character
// codes, the symbols are now letters, and every later line that uses them
// can be read directly.
//
// Font textures are nearly always laid out in code order, so once a few
// symbols are known the book can work out the rule ("cell number + 32 = code")
// and name the rest of the font without having seen them in a matched line.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Types.h"

namespace Discovery {

struct GlyphKey {
	u32 tex = 0;
	// The texture's size is part of the identity: games reuse texture memory,
	// and a texture of another size at the same address is another texture.
	u16 texW = 0, texH = 0;
	u16 u0 = 0, v0 = 0, u1 = 0, v1 = 0;
	bool operator<(const GlyphKey &o) const {
		if (tex != o.tex) return tex < o.tex;
		if (texW != o.texW) return texW < o.texW;
		if (texH != o.texH) return texH < o.texH;
		if (v0 != o.v0) return v0 < o.v0;
		if (u0 != o.u0) return u0 < o.u0;
		if (u1 != o.u1) return u1 < o.u1;
		return v1 < o.v1;
	}
};

// Where in a block of memory text could be: the stretches of bytes that are
// all printable characters. Searching only these is many times faster than
// searching everything, and the list stays useful for a few seconds.
struct TextIndex {
	struct Span {
		u32 start = 0;   // Offset in the block.
		u32 length = 0;  // In bytes.
	};
	std::vector<Span> narrow;  // One byte per character.
	std::vector<Span> wide;    // Two bytes per character, the second zero.
	void Build(const u8 *memory, size_t size);
};

enum class GlyphOrigin : u8 {
	Unknown,
	Matched,   // Its code was read from memory, in a line that matched.
	Layout,    // Worked out from the font texture's layout.
	Table,     // A letter in the game's own letter code, worked out from the code's place in the alphabet.
	Digits,    // A digit: its place in a strip of digits, confirmed by numbers found in memory.
};

// A short row of small images, for the number search: the symbol numbers left
// to right, and where each one's left edge is on screen.
struct ShortRow {
	std::vector<int> symbols;
	std::vector<float> x;
	std::vector<float> height;  // Of each image on screen.
};

// A place in memory where several of the numbers on screen stand together.
struct NumberRecord {
	u32 addr = 0;   // Address of the first of the values found there.
	u32 size = 0;   // Bytes from there to the end of the last one.
};

struct GlyphSymbol {
	GlyphKey key;
	u16 texW = 0;
	int code = -1;                  // Character code, or -1 while unknown.
	int raw = -1;                   // The game's own 16-bit code for it (as stored, low byte first), or -1. See LearnFromCodeLine.
	GlyphOrigin origin = GlyphOrigin::Unknown;
	float minAdvance = 0;           // Smallest step to the next glyph seen so far, as a share of the glyph's height; 0 = none seen.
};

class GlyphBook {
public:
	// The symbol number for a drawn rectangle (the same rectangle of the same
	// texture always gets the same number).
	int Intern(const SpriteDraw &draw);
	const GlyphSymbol &Symbol(int id) const { return symbols_[id]; }
	size_t Size() const { return symbols_.size(); }
	// True if the symbol is exactly one cell of a font texture laid out as a
	// grid of equal cells (then its rectangle is the cell, not the letter).
	bool IsGridCell(int id) const;
	// True once a symbol of this texture is known to be the space character:
	// the game draws its spaces, so no space needs to be guessed from gaps.
	bool DrawsSpaces(u32 tex) const { return spaceTextures_.count(Family(tex)) != 0; }
	// A font may be spread over several textures (capitals in one, small
	// letters in another). Textures seen side by side in one row of text are
	// joined into a family: what is learned about trust holds for all of them.
	void Join(u32 texA, u32 texB);
	u32 Family(u32 tex) const;
	// Its cell number in that grid, or -1.
	int CellNumber(int id) const;
	size_t KnownCount() const { return known_; }
	void Clear();

	// May this symbol's code be shown to a person? A code read from memory is
	// only trusted once its font is confirmed: either the match was of the
	// strict "codes follow the texture's layout" kind, or a second, different
	// line of the same font has matched and agreed with the first.
	bool Trusted(int id) const;

	// Remembers the smallest step seen from this symbol to the next one, which
	// later tells a normal step from a step with a space in it.
	void NoteAdvance(int id, float advance);

	// Tries to find a line in memory. `symbols` are the line's symbol numbers
	// left to right, all from one texture. memory/size/base is a copy of the
	// machine's memory. On success the symbols' codes are recorded and the
	// address of the matching string is returned; 0 means no single answer
	// (not found, or found with more than one possible reading).
	u32 LearnFromLine(const std::vector<int> &symbols, const u8 *memory, size_t size, u32 base, const TextIndex &index);

	// For a row that LearnFromLine could not find as a whole: looks for its
	// words instead. Games often keep a line with control codes between the
	// words, so the row is not in memory as one string, but its words are.
	//
	// Only for a font laid out as a grid, and only under the strict rule:
	// every code is the cell number plus one constant. A stretch of
	// WORD_WINDOW symbols is looked for; an answer needs two different
	// stretches of the row found with the same constant, and no other
	// constant doing as well. On success the layout rule is learned (which
	// names the whole font) and the address of the first stretch is returned.
	u32 LearnFromWords(const std::vector<int> &symbols, const u8 *memory, size_t size, u32 base, const TextIndex &index);

	// For a row LearnFromLine could not find: the same search, but not
	// trusting our guess at where the spaces are. The row's symbols are
	// looked for in order, and memory may have a space between any two of
	// them, or one other code the game uses as its space (always the same
	// one within the line). Gaps in `symbols` are ignored. If the row's first
	// letters are not found (they may be from a part of the font not seen
	// before), the rest of the row is tried without them.
	u32 LearnFromLooseLine(const std::vector<int> &symbols, const u8 *memory, size_t size, u32 base, const TextIndex &index);

	// After a LearnFromLooseLine success: for each symbol of the row (gaps
	// not counted), whether memory had a space before it: 1 yes, 0 no, -1 not known.
	const std::vector<signed char> &LastSpaces() const { return lastSpaces_; }
	// What a matched line showed about this font's spacing: the distance
	// between two letters (as a share of their height) with or without a
	// space between them. Once both kinds were seen and do not overlap,
	// SpaceGap gives the distance above which a space is meant; else 0.
	void NoteSpacing(u32 tex, float gap, bool space);
	float SpaceGap(u32 tex) const;

	// Text in a game's own letter code (GlyphPrivate.cpp). For a row no
	// other search found: looks for it among 16-bit values of any kind, by
	// its pattern of repeats alone. A find gives each symbol the game's code
	// for it. Once two different rows of a font have been found, the letters
	// are worked out: the codes of an alphabet follow each other in order,
	// and the counts of the codes in the game's strings around the rows must
	// fit English letter counts in exactly one placing. Returns the address
	// of the string, or 0.
	u32 LearnFromCodeLine(const std::vector<int> &symbols, const u8 *memory, size_t size, u32 base);
	// True once this font's own letter code has been worked out. The other
	// searches then have nothing to say about it, and must leave it alone.
	bool UsesOwnCode(u32 tex) const {
		auto it = tables_.find(Family(tex));
		return it != tables_.end() && it->second.found;
	}
	// True if the last LearnFromCodeLine call read through memory (the costly part).
	bool LastScanned() const { return scanned_; }
	// For the inspection report: the letter codes worked out.
	std::string DescribeTables() const;

	// The per-game profile (Profile.cpp): what is carried from one session
	// to the next. ExportProfile gives the book's part of the profile's
	// text; ImportProfile reads it back (other sections are passed over).
	// What is imported is not believed outright: see Profile.cpp.
	std::string ExportProfile() const;
	void ImportProfile(const std::string &text);
	void ForgetProfile();
	// To be called when a row of this font has just been found in memory:
	// what is remembered of the font is then checked against what was read.
	void CheckRemembered(u32 tex);

	// Numbers (GlyphDigits.cpp). Looks at short rows of unknown symbols for
	// strips of digits: images of one size, side by side in their texture.
	// A symbol's place in the strip says which digit it is, once we know
	// which digit the strip starts with. Each possible start is tried: it
	// turns the rows into numbers, and memory is searched for a place where
	// several of those numbers stand together as 16-bit values. Exactly one
	// start must find such a place. On success the digits are named, the
	// places are kept (NumberRecords), and true is returned.
	bool LearnDigits(const std::vector<ShortRow> &rows, const u8 *memory, size_t size, u32 base);
	// True if the symbol is a confirmed digit.
	bool IsDigit(int id) const { return symbols_[id].origin == GlyphOrigin::Digits; }
	// For a confirmed digit: the step from one digit of a number to the next,
	// as a share of the digit's height. 0 if not known.
	float DigitAdvance(int id) const;
	// True if two symbols are images of one size from one row of one texture
	// (two digits of the same strip are).
	bool SameStrip(int a, int b) const {
		const GlyphKey &x = symbols_[a].key, &y = symbols_[b].key;
		return x.tex == y.tex && x.v0 == y.v0 && x.v1 == y.v1 && x.u1 - x.u0 == y.u1 - y.u0;
	}
	const std::vector<NumberRecord> &NumberRecords() const { return numberRecords_; }
	// One line per digit strip, for the inspection report.
	std::string DescribeDigits() const;

	// Why the last LearnFromLine or LearnFromWords call gave no answer, in a few words ("" after a success).
	const char *LastOutcome() const { return outcome_; }
	void SetLastOutcome(const char *outcome) { outcome_ = outcome; }

	// Changes every time something is learned (so callers can tell whether
	// retrying a line could give a new result).
	int Version() const { return version_; }

	// How many learned symbols disagreed with a later match and were dropped.
	int Conflicts() const { return conflicts_; }

	// One line per font texture: how many symbols are known, and the layout
	// rule if one was found. For the inspection report.
	std::string Describe() const;

private:
	struct FontKey {
		u32 tex;
		u16 cellW, cellH;
		bool operator<(const FontKey &o) const {
			if (tex != o.tex) return tex < o.tex;
			if (cellW != o.cellW) return cellW < o.cellW;
			return cellH < o.cellH;
		}
	};
	// A font texture laid out in code order: code = cell number + offset.
	struct LayoutRule {
		bool found = false;
		int offset = 0;
	};

	// A game's own letter code, for one font.
	struct LetterTable {
		bool found = false;
		bool highByteFirst = false;
		int small = -1;      // The code of "a".
		int capital = -1;    // The code of "A", or -1 if not settled.
		int space = -1;      // As stored.
		float smallFit = 0, capitalFit = 0, capitalTop = 0, capitalNext = 0;
	};
	int TableChar(const LetterTable &table, int raw) const;
	void ApplyTable(u32 family);
	void InferLetters(u32 family, const u8 *memory, size_t size, u32 base);
	bool AdoptSavedTable(u32 family);
	// A font's own code has been worked out: what the other searches made
	// of the font goes.
	void OwnCodeConfirmed(u32 family);
	std::vector<LetterTable> savedTables_;              // From the profile.
	std::map<GlyphKey, int> remembered_;                // From the profile: image -> character.
	std::set<u32> rememberedOk_;                        // Textures whose remembered letters were confirmed this session.
	std::set<u32> volatileFonts_;                       // Textures whose letters change from session to session.
	std::map<u32, LetterTable> tables_;                 // By family.
	std::map<u32, std::set<u64>> codeLines_;            // The different rows found by LearnFromCodeLine.
	std::map<u32, std::vector<u32>> codeAddrs_;         // Where they were found.
	std::map<std::pair<u32, int>, int> rawOwner_;       // (family, code) -> symbol.
	bool scanned_ = false;

	// A strip of digits in a texture: images of one size on one row.
	struct DigitStrip {
		u32 tex = 0;
		u16 texW = 0, texH = 0, w = 0, h = 0, u0 = 0, v0 = 0;
		float step = 0;   // From one image to the next.
		float advance = 0;  // On screen, from one digit of a number to the next, as a share of the height.
		int first = 0;    // The digit of the image at u0.
	};
	// The digit a symbol is in that strip, or -1.
	int StripDigit(const DigitStrip &strip, const GlyphSymbol &s) const;
	std::vector<DigitStrip> strips_;
	std::vector<NumberRecord> numberRecords_;

	bool CellIndex(const GlyphSymbol &s, FontKey *font, int *index) const;
	void UpdateLayout(const FontKey &font);
	void SetCode(int id, int code, GlyphOrigin origin);

	std::vector<GlyphSymbol> symbols_;
	std::map<GlyphKey, int> ids_;
	std::map<FontKey, LayoutRule> layouts_;
	bool TextureTrusted(u32 tex) const;
	std::map<u32, std::set<u64>> matchedLines_;  // Per texture: the different lines matched so far.
	std::set<u32> spaceTextures_;
	struct Spacing {
		float widestWithout = -1000, narrowestWith = 1000;
	};
	std::map<u32, Spacing> spacing_;             // By family.
	std::vector<signed char> lastSpaces_;
	std::map<u32, u32> family_;                  // Texture -> the texture that stands for its family.
	std::set<u32> orderedTextures_;              // Textures confirmed by a layout-ordered match.
	size_t known_ = 0;
	int version_ = 0;
	int conflicts_ = 0;
	const char *outcome_ = "";
};

}  // namespace Discovery
