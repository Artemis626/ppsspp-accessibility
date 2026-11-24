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

// Recordings: everything the layer saw over a stretch of frames, written to
// a folder, so a screen can be studied (and the engine run again on it)
// without starting the emulator.
//
// A recording folder holds:
//
//   recording.txt   The manifest: game, frame range, memory areas. Machine-read.
//   sprites.txt     Every textured rectangle of every frame, in draw order,
//                   and the surfaces the game drew into ("target" lines).
//                   Machine-read; this is what a replay feeds to an Engine.
//   memory-N.bin    A copy of each memory area, taken when the recording ended.
//   timeline.txt    For a person: frame by frame, the text that appeared,
//                   changed and went, lines that could not be read, and every
//                   memory search with its result.
//   summary.txt     For a person: the inspection report at the end, every
//                   text and every unread line seen, the font tables, the
//                   textures drawn from, and the draws the tap saw.
//
// Engine::StartRecording (Engine.h) writes one. LoadRecording and
// RecordedMachine read one back.
//
// A recording contains game memory. Like a save state it is game data and
// never belongs in a source repository.

#include <string>
#include <string_view>
#include <vector>

#include "Machine.h"
#include "Types.h"

namespace Discovery {

// Where a recording's files go. The adapter supplies it (so the core needs no
// file functions of its own and works with any emulator's file layer).
class RecordingSink {
public:
	virtual ~RecordingSink() {}
	// Adds bytes to the end of the named file in the recording's folder,
	// creating the file if needed.
	virtual bool Append(const std::string &file, const void *data, size_t size) = 0;
};

// Reads a recording's files back. `file` is a name from the list above.
class RecordingSource {
public:
	virtual ~RecordingSource() {}
	virtual bool ReadAll(const std::string &file, std::string *out) = 0;
};

// One draw the tap looked at, whether or not it became rectangles. Counted
// while recording, to answer "is this game's text drawn in a way the tap
// does not understand?".
struct DrawNote {
	u8 prim = 0;           // The machine's own primitive number.
	const char *primName = "";  // Its name, for the summary.
	bool textured = false;
	bool clear = false;    // A screen-clearing draw.
	u32 texAddr = 0;
	u16 texW = 0, texH = 0;
	u8 texFormat = 0;
	u32 vertices = 0;
	int rectangles = 0;    // How many rectangles the tap made of it; 0 = not understood or not wanted.
};

struct RenderTarget {
	u32 addr = 0;
	u32 bytes = 0;
};

struct RecordedFrame {
	int frame = 0;
	std::vector<RenderTarget> targets;  // Surfaces first seen drawn into in this frame.
	std::vector<SpriteDraw> sprites;
};

// A recording read back into memory. It is also a Machine, so an Engine can
// be run on it.
class RecordedMachine : public Machine {
public:
	std::string GameId() const override { return game; }
	std::vector<MemoryRegion> Regions() const override { return regions; }
	bool Read(u32 addr, void *out, u32 size) const override;
	const u8 *View(u32 addr, u32 size) const override;

	std::string game;
	std::string note;
	std::vector<MemoryRegion> regions;
	std::vector<std::string> memory;   // One per region.
	std::vector<RecordedFrame> frames;
};

// False (with a reason in *error) if the folder is not a recording this
// version understands.
bool LoadRecording(RecordingSource *source, RecordedMachine *out, std::string *error);

// One sprites.txt line, without the line break; and back.
std::string FormatSprite(const SpriteDraw &d);
bool ParseSprite(const char *line, SpriteDraw *out);

}  // namespace Discovery
