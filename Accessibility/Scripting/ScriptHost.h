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

// Loads and runs the Lua script for the running game.
//
// Lifecycle, without any hook in PPSSPP's game code: Frame() runs every host
// frame and watches PSP_IsInited(). When a game finishes booting we look for
// its script and load it; when the game stops (or is reset) we unload it.
// All Lua runs on the emulation thread.

#include <string>

namespace Access {
namespace Script {

struct Options {
	std::string scriptOverride;  // --script=<path>: use this file instead of the game-ID lookup.
	bool noScript = false;       // --no-script: never load a script.
};

void Init(const Options &options);
void Shutdown();

// Once per host frame, from Access::Frame (emulation thread).
void Frame();

// Thread-safe: ask for the script to be reloaded on the next Frame (the
// reload hotkey). Also re-reads hotkeys.ini.
void RequestReload();

// The running game's ID, or "" when no game is running. Emulation thread only.
std::string RunningGameId();

}  // namespace Script
}  // namespace Access
