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

// Speaks PPSSPP's own menus.
//
// How it works: once per UI frame we look at which menu item has keyboard or
// gamepad focus (UI::GetFocusedView) and build a description of it from the
// item's own DescribeText() plus its current state (checked, expanded, or
// selected value). When the focused item OR its description changes, we
// speak it with interrupt. That one rule covers moving through a menu,
// toggling a checkbox, moving a slider, and a value changing after you pick it
// in a popup, without editing any PPSSPP UI code.

namespace Access {
namespace MenuSpeech {

// Call once per UI frame, on the thread that runs the UI (the emulation thread).
void Update();

// Forget the last spoken item, so the next Update speaks the focused item again.
void Reset();

}  // namespace MenuSpeech
}  // namespace Access
