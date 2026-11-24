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

// The single entry point PPSSPP calls into. Every hook in upstream code
// (marked "// [ACCESS]") calls one of these functions and nothing else, so
// moving to a new PPSSPP release only means re-placing these calls.

struct KeyInput;

namespace Access {

// App start (end of NativeInit). Safe to call more than once.
void Init();

// Once per host frame (NativeFrame, after the UI update). Runs on the
// emulation thread, also while a game is paused or a menu is open.
void Frame();

// App exit (start of NativeShutdown).
void Shutdown();

// Every key event, first thing in NativeKey (window thread). Returns true if
// the key is an accessibility hotkey, in which case PPSSPP must ignore it.
bool OnKey(const KeyInput &key);

}  // namespace Access
