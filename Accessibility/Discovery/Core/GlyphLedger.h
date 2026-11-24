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

// The glyph ledger: a record of which character the platform's font library
// drew into which place in memory.
//
// When a game asks the system font library to draw character X into one of
// its buffers, the adapter tells the ledger "the rectangle at this address
// now holds X". When the game later draws a textured rectangle that samples
// that place, the ledger answers "that is X". No pixel is ever read: the
// link is the memory address alone.

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Types.h"

namespace Discovery {

struct GlyphCell {
	u32 addr = 0;        // Address of the glyph's top-left pixel.
	u32 stride = 0;      // Bytes per row of the buffer it was drawn into.
	u8 bpp = 0;          // Bits per pixel of that buffer.
	u16 w = 0, h = 0;    // Size in pixels; 0 = the game never asked, so unknown.
	u32 codepoint = 0;   // Unicode.
	float advance = 0;   // How far the pen moves after this glyph, in pixels; 0 = unknown.
	s16 left = 0;        // Pixels from the pen position to the glyph's left edge.
	u32 pc = 0;          // Game code that asked for the glyph.
	int frame = 0;       // When it was drawn.
};

class GlyphLedger {
public:
	// Adds a cell, and forgets older cells the new glyph was drawn over.
	void Record(const GlyphCell &cell);

	// Which characters does a texture rectangle show? The texture starts at
	// texAddr with texStride bytes per row; the rectangle is in texels.
	// Usually the answer is one glyph (a font atlas cell). It is several when
	// the game drew a whole string into a strip and shows the strip at once;
	// then only the glyphs drawn together with the newest one are returned,
	// so that the tail of an older, longer string is not reported.
	struct Hit {
		const GlyphCell *cell;
		float u, v;  // The glyph's top-left corner, in texels of this texture.
	};
	void Find(u32 texAddr, u32 texStride, int bpp, float u0, float v0, float u1, float v1, std::vector<Hit> *hits) const;

	// True if any known glyph lies in [addr, addr + size). A cheap test the
	// adapter uses to skip draws that cannot be system-font text.
	bool CoversRange(u32 addr, u32 size) const;

	void Clear();
	size_t Size() const { return cells_.size(); }

	// For the save-state sidecar file.
	std::string Serialize() const;
	bool Deserialize(std::string_view text);

private:
	void Trim();

	std::map<u32, GlyphCell> cells_;
	u32 maxSpan_ = 0;  // Largest h * stride of any cell: how far back a lookup must search.
};

}  // namespace Discovery
