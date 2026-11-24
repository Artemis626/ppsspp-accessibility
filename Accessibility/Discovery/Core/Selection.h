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

// Which item of a menu is selected (Phase D3, second part).
//
// A game shows its selection in one of a few ways, and each can be seen in
// the draw stream without looking at a pixel:
//
//   a mark      a small image (a bar, a hand, a frame) that jumps from one
//               text to another while the texts stay where they are;
//   a place     the selected text is set a little to the side of the others,
//               and when the selection moves, two texts change places;
//   a colour    the selected text is drawn in another colour (the colour the
//               game gives the draw, not a colour read from the screen), and
//               when the selection moves, two texts change colours.
//
// None of these can be told from a still screen: a title is also "the one in
// another colour". So nothing is claimed until the selection has been seen
// to move once. From then on it is followed.

#include <map>
#include <string>
#include <vector>

#include "Types.h"

namespace Discovery {

struct Selection {
	bool known = false;
	// What a person should hear: the selected text. If the mark stood by
	// another text of the same row (a value beside its label), the row's
	// first text comes first: "Message Speed: Normal".
	std::string text;
	RectF rect;
	const char *how = "";   // "mark", "place" or "colour".
	u32 markTex = 0;        // For "mark": the texture the mark is drawn from.
	int since = 0;          // Frame it came to this text.
};

class SelectionFinder {
public:
	void Clear();
	// Once a frame, with the frame's text and everything drawn in it. Sets
	// `selected` on the runs that are.
	void Update(int frame, std::vector<TextRun> &runs, const std::vector<SpriteDraw> &sprites);
	const Selection &Current() const { return current_; }
	// For the inspection report.
	std::string Describe() const;

private:
	struct Mark {
		u64 key = 0;          // The text it stands by now.
		int since = 0;
		float rel = 0;        // Its height on screen relative to that text's top.
		float atX = 0, atY = 0;  // Where it was when it came to rest (it may bob a little, not travel).
		float w = 0, h = 0;
		u64 restedKey = 0;    // The last text it rested by,
		float restedRel = 0;  // and how it stood there.
		float restedW = 0, restedH = 0;
		float restedX = 0, restedY = 0;  // Where on screen that was.
		int moves = 0;        // Times it has gone from resting by one text to resting by another.
		int lastSeen = 0;
	};
	struct Seen {
		float x = 0, stableX = 0;
		u32 color = 0, stableColor = 0;
		int xSince = 0, colorSince = 0;
		int lastSeen = 0;
		bool started = false;
	};
	struct Change {
		int frame = 0;
		u64 key = 0;
		bool colour = false;
		float dx = 0;
		u32 from = 0, to = 0;
		bool used = false;
	};
	std::map<u32, Mark> marks_;
	std::map<u64, Seen> seen_;
	std::vector<Change> changes_;
	// The selection found by a change of place or colour.
	u64 swapKey_ = 0;
	int swapFrame_ = -1;
	const char *swapHow_ = "";
	int swaps_ = 0;
	Selection current_;
};

}  // namespace Discovery
