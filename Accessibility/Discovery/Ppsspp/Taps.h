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

// The taps: where the adapter listens to the emulated PSP.
//
//   System-call tap   every PSP system-library call a game makes. We read
//                     the font library (which character went into which
//                     buffer), the character-conversion library, and the
//                     opening of system dialogs.
//   Dialog tap        the text and selection boxes PPSSPP's own system
//                     dialogs draw.
//   GE tap            every draw command, to see which draws sample memory
//                     that holds font-library glyphs.
//   State tap         save states loaded and saved.
//
// Each tap is a function pointer in PPSSPP that is null unless we set it
// (search the PPSSPP source for "[ACCESS]").

namespace Discovery {
class Engine;
}

namespace Access {
namespace DiscoveryHost {

// Starts listening. The engine must outlive the taps.
void InstallTaps(::Discovery::Engine *engine);
void RemoveTaps();
// Forgets what the taps remembered about one game (glyph sizes).
void ResetTapState();

// The GE tap makes PPSSPP interpret display lists in its slower, per-command
// loop, so it is only on while it can be useful.
void SetGeTapEnabled(bool enabled);
bool GeTapEnabled();

}  // namespace DiscoveryHost
}  // namespace Access
