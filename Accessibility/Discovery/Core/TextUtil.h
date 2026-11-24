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

// Small text helpers the core needs. Written here because the core may not
// use the emulator's own helpers.

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Types.h"

namespace Discovery {

// ---- UTF-8 ----
void AppendUtf8(std::string &out, u32 codepoint);
std::vector<u32> DecodeUtf8(std::string_view text);

// Trims, and turns every run of whitespace (including line breaks) into one space.
std::string NormalizeSpaces(std::string_view text);
std::string Trim(std::string_view text);
std::string ToLower(std::string_view text);  // ASCII only.

// Edit distance between two strings, counted in Unicode characters.
int EditDistance(std::string_view a, std::string_view b);

// ---- JSON writing ----
std::string JsonString(std::string_view text);  // With the surrounding quotes.

// ---- INI reading ----
// Sections in file order; keys are lowercased; values are trimmed. A ';' or
// '#' starts a comment only at the start of a line or after whitespace.
struct IniSection {
	std::string name;
	std::map<std::string, std::string> values;
	std::string Get(const std::string &key, const std::string &fallback = std::string()) const;
	bool Has(const std::string &key) const { return values.count(key) != 0; }
};
std::vector<IniSection> ParseIni(std::string_view text);

}  // namespace Discovery
