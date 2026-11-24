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

// What the core asks of an emulator. An adapter for another emulator
// implements this class and calls the event functions in Engine.h.
//
// The interface is in tiers (see docs/Discovery-Phase0-Design.md, section 5).
// Phase D1 uses only the part of Tier 0 below; later phases add input
// injection, snapshots, memory and execute events, and the deep trace.

#include <string>
#include <vector>

#include "Types.h"

namespace Discovery {

struct MemoryRegion {
	u32 start = 0;
	u32 size = 0;
	std::string name;  // For reports, e.g. "user RAM".
};

class Machine {
public:
	virtual ~Machine() {}

	// Identity of the running game, e.g. a disc ID. Empty when nothing runs.
	virtual std::string GameId() const = 0;

	// The memory areas a game's own data can be in.
	virtual std::vector<MemoryRegion> Regions() const = 0;

	// Copies memory out. Returns false (and copies nothing) if any part of the
	// range is not valid memory.
	virtual bool Read(u32 addr, void *out, u32 size) const = 0;

	// Optional: a direct view of the same memory, valid until the machine
	// runs again, so a large search need not copy it. nullptr = not offered.
	virtual const u8 *View(u32 addr, u32 size) const { return nullptr; }
};

}  // namespace Discovery
