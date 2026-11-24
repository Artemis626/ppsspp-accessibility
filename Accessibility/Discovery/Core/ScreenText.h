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

// The text currently on screen, and what changed since the last frame.
//
// A game redraws everything every frame, so "the same line is still there"
// has to be worked out: each frame's runs are matched against the previous
// frame's. A run keeps its id while it stays on screen, even if it moves
// (a sliding menu) or its text changes in place (a counter).

#include <string>
#include <vector>

#include "Types.h"

namespace Discovery {

enum class ChangeKind {
	Appeared,
	Changed,      // Same place, different text. oldText has what it said before.
	Disappeared,
};

struct TextChange {
	ChangeKind kind;
	TextRun run;
	std::string oldText;
};

class ScreenText {
public:
	// This frame's runs, in reading order. Ids are assigned here.
	void Update(int frame, std::vector<TextRun> runs);

	// Runs on screen now, in reading order.
	const std::vector<TextRun> &Runs() const { return current_; }

	// Changes since the last call, oldest first.
	std::vector<TextChange> TakeChanges();

	void Clear();

private:
	std::vector<TextRun> current_;   // Seen in the latest frame.
	std::vector<TextRun> missing_;   // Not seen for a few frames, not yet declared gone.
	std::vector<TextChange> changes_;
	u32 nextId_ = 1;
};

}  // namespace Discovery
