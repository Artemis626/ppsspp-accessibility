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

// The accessibility log: accessibility/logs/access.log beside the exe.
//
// Separate from PPSSPP's own log on purpose: it is always on, and contains
// only what matters to script authors and players (script output, errors,
// what was spoken), so it is short enough to read with a screen reader.
// The previous session's log is kept as access.old.log.
//
// Thread-safe. Text is UTF-8.

#include <string_view>

namespace Access {
namespace Log {

void Init();
void Shutdown();

void Info(std::string_view text);
void Warn(std::string_view text);
void Error(std::string_view text);
// Lines written by scripts (print, console.log).
void Script(std::string_view text);
// Everything sent to the screen reader.
void Spoken(std::string_view text);

}  // namespace Log
}  // namespace Access
