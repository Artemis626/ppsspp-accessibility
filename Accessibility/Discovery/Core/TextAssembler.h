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

// Turns single characters with screen positions into lines of text.
//
// A game draws text one glyph at a time, in any order, often twice (a shadow
// and then the letter). The assembler removes the duplicates, groups glyphs
// that sit on the same line, orders them left to right, and works out where
// the spaces are (games usually do not draw a space; they just leave a gap).

#include <vector>

#include "Types.h"

namespace Discovery {

struct PlacedGlyph {
	u32 codepoint = 0;
	RectF rect;           // On screen.
	u32 target = 0;
	u32 order = 0;        // Draw order within the frame.
	// From the font's own metrics, scaled to screen pixels. hasMetrics false
	// means only the rectangle is known.
	bool hasMetrics = false;
	float penX = 0;       // Where the pen was when this glyph was drawn.
	float advance = 0;    // How far the pen moves after it.
	u32 pc = 0;
	u32 bufferAddr = 0;
};

// The grouping step alone: duplicates removed, glyphs grouped into lines, each
// line ordered left to right. `codepoint` is only compared for equality here,
// so a caller may put any symbol number in it.
std::vector<std::vector<PlacedGlyph>> AssembleLines(std::vector<PlacedGlyph> glyphs);

// The same, but trusting the order the game drew in: a line is a stretch of
// consecutive draws that stay on one row and move right. This keeps apart
// texts that share a row on screen, such as a popup drawn over a menu, which
// grouping by position alone would shuffle together.
std::vector<std::vector<PlacedGlyph>> AssembleLinesInDrawOrder(std::vector<PlacedGlyph> glyphs);

// The runs come back in reading order: top to bottom, then left to right.
// Run ids are left at 0; ScreenText assigns them.
std::vector<TextRun> AssembleRuns(std::vector<PlacedGlyph> glyphs);

}  // namespace Discovery
