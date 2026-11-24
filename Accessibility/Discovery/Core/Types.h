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

// Discovery core: shared types.
//
// RULE FOR EVERY FILE IN THIS FOLDER: no emulator headers. The core knows
// nothing about PSP or PPSSPP; it is compiled with an include path that
// contains only this folder, so an emulator #include does not compile. The
// emulator-specific side lives in ../Ppsspp and talks to the core through
// Machine.h and Engine.h.
//
// The core never looks at pixels. An image is identified only by where its
// data lives in the emulated machine's memory.

#include <cstdint>
#include <string>
#include <vector>

namespace Discovery {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
typedef int32_t s32;

struct RectF {
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	float Width() const { return x1 - x0; }
	float Height() const { return y1 - y0; }
	float CenterX() const { return (x0 + x1) * 0.5f; }
	float CenterY() const { return (y0 + y1) * 0.5f; }
	bool Empty() const { return x1 <= x0 || y1 <= y0; }
};

// Where a piece of text came from. Each has its own typical reliability,
// which the harness measures.
enum class TextSource {
	SystemDialog,    // Drawn by the platform's own dialogs: already real text.
	SystemFont,      // Platform font library glyphs, matched to draws on screen.
	FontCallStream,  // Platform font library calls in call order; no screen position.
	GameFont,        // The game's own font, read with the glyph cipher.
};

const char *TextSourceName(TextSource source);

// One line (or column cell) of text on screen.
struct TextRun {
	u32 id = 0;             // Stays the same while this run stays on screen.
	std::string text;       // UTF-8.
	RectF rect;             // Screen coordinates of the machine (PSP: 480 x 272).
	bool hasRect = false;   // False for sources that have no position.
	TextSource source = TextSource::SystemDialog;
	float confidence = 0;   // 0..1.
	u32 target = 0;         // Which render target it was drawn to (0 = unknown or the screen).
	u32 codePc = 0;         // Game code that asked for the text (0 = unknown).
	u32 bufferAddr = 0;     // Memory the glyph images were drawn from (0 = not applicable).
	bool selected = false;  // It is the selected item: a highlight behind it (system dialogs), or see Selection.h.
	u32 color = 0;          // The colour the game gave the draw of its first letter (0 = not known). Not read from the screen.
	int firstFrame = 0;
	int lastFrame = 0;
};

// Tier 2 draw-stream record: one textured rectangle. Texture coordinates are
// in texels; screen coordinates in the machine's pixels.
struct SpriteDraw {
	u32 texAddr = 0;       // Address of the texture's first texel.
	u32 texStride = 0;     // Bytes per texture row; 0 if the texture is not stored row by row.
	u8 texBpp = 0;         // Bits per texel.
	u16 texW = 0, texH = 0;
	u8 texFormat = 0;      // The machine's own format number (only compared, never interpreted).
	// The texture is in the part of memory where the machine's drawing
	// surfaces are (video memory). Only a hint: games also keep ordinary
	// pictures there, fonts included. Engine::IsSurface gives the verdict.
	bool fromSurface = false;
	u32 clutAddr = 0;      // Palette address, if the texture uses one. Same glyph, other colour.
	float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
	RectF screen;
	u32 target = 0;
	u32 dataAddr = 0;      // Where the vertex data is (for later provenance work).
	// The colour the game gave this draw (its vertex colour: red, green,
	// blue, alpha from the low byte up). It is part of the draw command, not
	// something read from the picture. 0 = not known (older recordings).
	u32 color = 0;
};

}  // namespace Discovery
