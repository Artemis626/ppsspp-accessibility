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

// The measuring harness: reads a case file that says what a screen should
// contain, compares it with what the layer found, and writes the result.
//
// A case file is an INI file (see Accessibility/harness/README.md):
//
//   [case]
//   game = ULES01500
//   state = shop-buy-list.ppst
//   description = Shop, buy list, cursor on the second item
//   settle_frames = 60
//   complete = yes          ; every visible text is listed below
//
//   [text.1]
//   text = Decoy Scroll
//   match = exact           ; or: contains
//
// [cursor.N] and [value.N] sections are read and counted, and are scored
// from Phase D3 on.

#include <string>
#include <string_view>
#include <vector>

#include "Types.h"

namespace Discovery {

struct ExpectedText {
	std::string section;   // "text.1"
	std::string text;
	bool contains = false; // match = contains: enough if a found run includes it.
	std::string label;
};

struct HarnessCase {
	std::string game;
	std::string state;
	std::string description;
	int settleFrames = 60;
	bool complete = false;
	std::vector<ExpectedText> texts;
	int cursors = 0;  // Declared, not yet scored.
	int values = 0;
};

bool ParseHarnessCase(std::string_view ini, HarnessCase *out, std::string *error);

enum class TextVerdict {
	Exact,      // A found run says exactly this.
	Contained,  // The words were found, but split or joined differently.
	Near,       // A found run is close (a few wrong characters).
	Missing,
};

struct TextScore {
	ExpectedText expected;
	TextVerdict verdict = TextVerdict::Missing;
	std::string best;        // The found text that came closest.
	std::string source;      // Which technique found it.
	float errorRate = 1.0f;  // Wrong characters / expected characters, against `best`.
	float confidence = 0;
};

struct FoundScore {
	TextRun run;
	bool explained = false;  // It corresponds to something expected.
};

struct HarnessResult {
	std::vector<TextScore> texts;
	std::vector<FoundScore> found;
	int exact = 0, contained = 0, near = 0, missing = 0;
	// Share of expected texts found (exact or contained; "contains" items count as exact).
	float recall = 0;
	// Share of found runs that were expected. Only meaningful when the case is complete.
	float precision = 0;
};

HarnessResult ScoreHarnessCase(const HarnessCase &test, const std::vector<TextRun> &runs);

// extra is inserted as further top-level fields: it must be empty or a list
// of `"name": value` pairs without the surrounding braces.
std::string HarnessResultToJson(const HarnessCase &test, const HarnessResult &result, const std::string &mode, const std::string &extra);

}  // namespace Discovery
