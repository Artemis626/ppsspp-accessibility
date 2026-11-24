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

// The core's front door. An adapter creates one Engine, reports what its
// taps see through the On... functions, and reads the results back.
//
// All calls must come from one thread.

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "GlyphCipher.h"
#include "Selection.h"
#include "GlyphLedger.h"
#include "Machine.h"
#include "Recording.h"
#include "ScreenText.h"
#include "TextAssembler.h"
#include "Types.h"

namespace Discovery {

// Counts what the taps saw, to answer "how does this game draw its text?".
class Survey {
public:
	void Count(const std::string &name, u64 amount = 1) { counters_[name] += amount; }
	u64 Get(const std::string &name) const;
	const std::map<std::string, u64> &Counters() const { return counters_; }
	// Text other taps saw passing by, newest last (a few are kept as examples).
	void AddSample(std::string_view origin, std::string_view text);
	const std::vector<std::string> &Samples() const { return samples_; }
	void Clear();

	// One or two plain sentences for speech.
	std::string Verdict() const;
	// The verdict plus every counter, one per line, for the log.
	std::string Report() const;

private:
	std::map<std::string, u64> counters_;
	std::vector<std::string> samples_;
};

// Where a piece of text was found in memory.
struct TextLocation {
	u32 addr = 0;
	std::string encoding;  // "utf8" or "utf16le".
};

// A number on screen, read from a confirmed strip of digits.
struct ScreenNumber {
	u32 value = 0;
	int digits = 0;      // As drawn, leading zeros included.
	RectF rect;
	u32 tex = 0;
	// Where in memory this number may be kept: addresses of 16-bit values.
	// The list starts with every equal value in the records found
	// (GlyphBook::NumberRecords) and loses, each time the number on screen
	// changes, the addresses that did not change with it.
	std::vector<u32> candidates;
	int changes = 0;     // How many changes have narrowed the list.
};

// Could this rectangle be a letter? Only by size: letters are small. (Whether
// its texture is a picture at all is Engine::IsSurface's question.)
bool LetterSized(const SpriteDraw &d);

class Engine {
public:
	explicit Engine(Machine *machine) : machine_(machine) {}

	// A game started or stopped: forget everything.
	void Reset();
	// The machine's memory was replaced (a save state was loaded). Knowledge
	// gathered from past events no longer describes it.
	void OnMemoryReplaced();

	// ---- Tier 4: platform services ----
	// The platform's own UI drew a string (already real text).
	void OnSystemText(std::string_view utf8, const RectF &rect);
	// The platform's own UI drew a selection highlight.
	void OnSystemHighlight(const RectF &rect);
	// The platform font library drew a glyph into game memory.
	void OnGlyphRendered(const GlyphCell &cell);
	// Text seen in passing (a string conversion, for example). Kept as a sample.
	void OnAnchorText(std::string_view origin, std::string_view utf8);

	// ---- Tier 2: the draw stream ----
	// Cheap pre-check: could a draw from this texture memory be ledger text?
	bool WantsTexture(u32 texAddr, u32 byteSize) const { return ledger_.CoversRange(texAddr, byteSize); }
	void OnSpriteDraw(const SpriteDraw &draw);
	// The machine is drawing into this memory: a screen or an off-screen
	// surface. Addresses must be in the same form as texture addresses.
	void OnRenderTarget(u32 addr, u32 byteSize);
	// True if the draw's texture is something the game drew earlier (a screen
	// copy), not a stored picture: it is in video memory AND inside a surface
	// the game has been seen drawing into.
	bool IsSurface(const SpriteDraw &draw) const;
	// Keep every textured rectangle of the frame, not only those the ledger
	// knows. Needed for text a game draws with its own font.
	void SetCaptureAllSprites(bool on) { captureAll_ = on; }
	bool CapturesAllSprites() const { return captureAll_; }
	// The rectangles of the last complete frame, in draw order.
	const std::vector<SpriteDraw> &LastSprites() const { return lastSprites_; }

	// ---- Tier 0 ----
	// A frame was shown: assemble this frame's text and work out changes.
	void OnFrameEnd();
	int Frame() const { return frame_; }

	// ---- results ----
	GlyphLedger &Ledger() { return ledger_; }
	GlyphBook &Book() { return book_; }
	// Everything the layer currently knows about the screen, as plain text
	// for a person to read: text found, lines it cannot read yet, the fonts
	// it has worked out, the textures being drawn from, and the survey.
	std::string InspectionReport() const;
	// ---- the per-game profile (Profile.cpp) ----
	// What has been learned about this game, as text to keep in a file.
	std::string ExportProfile() const;
	// Reads such a text back. Call it when a game has started.
	void ImportProfile(const std::string &text);
	// Changes whenever ExportProfile would give another text.
	u64 ProfileStamp() const { return profileStamp_ + (u64)book_.Version(); }

	// Which item is selected, if the layer has worked out how this game shows it.
	const Selection &Selected() const { return selection_.Current(); }
	// The numbers on screen in the last complete frame.
	const std::vector<ScreenNumber> &Numbers() const { return numbers_; }
	ScreenText &Screen() { return screen_; }
	Survey &GetSurvey() { return survey_; }

	// ---- recording (see Recording.h) ----
	// Starts writing everything the layer sees to the sink, for `frames`
	// frames (0 = until StopRecording). The sink must stay alive until the
	// recording has ended. False if a recording is already running.
	bool StartRecording(RecordingSink *sink, int frames, const std::string &note);
	// Writes the summary, the memory copy and the manifest, and ends the recording.
	void StopRecording();
	bool IsRecording() const { return rec_.sink != nullptr; }
	// Frames in the running recording, or in the last one once it has ended.
	int RecordedFrames() const { return rec_.sink ? rec_.frames : rec_.lastFrameCount; }
	// A draw the tap looked at (only counted while recording).
	void OnDrawNote(const DrawNote &note);

	// Searches the machine's memory for a string, as UTF-8 and as UTF-16.
	// At most maxHits results.
	std::vector<TextLocation> Locate(std::string_view utf8, size_t maxHits = 16) const;

private:
	Machine *machine_;
	GlyphLedger ledger_;
	ScreenText screen_;
	Survey survey_;
	int frame_ = 0;

	// Collected during the frame, used and cleared in OnFrameEnd.
	std::vector<PlacedGlyph> glyphs_;
	std::vector<TextRun> systemRuns_;
	std::vector<RectF> highlights_;
	std::vector<u32> streamChars_;   // Glyphs the font library drew this frame, in call order.
	u32 streamPc_ = 0;
	u32 drawOrder_ = 0;
	u64 glyphQuadsThisFrame_ = 0;
	std::vector<GlyphLedger::Hit> hitScratch_;
	bool captureAll_ = false;
	std::map<u32, u32> targets_;  // Surfaces drawn into: address -> size in bytes.

	// ---- the game's own font (glyph cipher) ----
	void ReadGameFontText(std::vector<TextRun> *runs);
	void LearnFromPendingLines();
	SelectionFinder selection_;
	void NoteFinding(const std::string &line);
	std::set<std::string> findings_;       // Lines of the profile's [findings] section.
	u64 profileStamp_ = 0;
	std::string lastSelectionFinding_;
	size_t recordsNoted_ = 0;
	void LearnNumbers();
	void TrackNumbers();
	std::vector<u32> NumberCandidates(u32 value) const;
	std::vector<ShortRow> shortRows_;      // This frame's short rows with unknown symbols.
	u64 numbersTriedHash_ = 0;             // The rows of the last number search,
	int numbersTriedVersion_ = -1;         // and what was known then.
	int numbersTriedFrame_ = -1000000;
	std::vector<ScreenNumber> numbers_;
	struct ValueTrack {
		bool started = false;
		u32 value = 0;
		std::vector<u32> candidates;
		size_t records = 0;       // How many records were known when the list was made.
		int changes = 0;
		int stableSince = 0;      // Frame the present value was first seen.
		bool checked = true;      // The list has been narrowed for the present value.
	};
	std::map<u64, ValueTrack> tracks_;     // By place on screen.
	struct PendingLine {
		std::vector<int> symbols;  // -1 = a gap that is probably a space.
		std::vector<float> gaps;   // Per symbol (gaps not counted): the distance from the letter before, as a share of its height.
		u32 tex = 0;
		u64 hash = 0;
	};
	struct UnreadLine {
		RectF rect;
		u32 tex = 0;
		int symbols = 0, known = 0;
		u64 hash = 0;
		std::string shape;  // The symbols, for the report: cell numbers, "s12" for a loose symbol, "_" for a gap.
	};
	struct Tried {
		int version = -1;            // Book version when the line was last searched for.
		int frame = 0;               // When that was.
		int failures = 0;            // How many searches in a row gave no answer.
		const char *outcome = "";    // Why that search gave no answer.
	};
	GlyphBook book_;
	std::vector<PendingLine> pending_;
	std::vector<UnreadLine> unread_;       // Last frame's lines that could not be read.
	std::map<u64, Tried> tried_;           // Line hash -> the last search for it.
	// How often a line was "found" in memory. A line found again and again
	// was each time thrown out by another line that disagreed: its reading
	// is a chance fit, and searching for it once more only costs time.
	std::map<u64, int> matchedTimes_;
	std::vector<u8> memoryCopy_;
	TextIndex textIndex_;
	int textIndexFrame_ = -1000000;
	int codeSearchFrame_ = -1000000;       // When memory was last read through for a row in a game's own letter code.
	u32 textIndexBase_ = 0;
	std::vector<SpriteDraw> sprites_;      // This frame, so far.
	std::vector<SpriteDraw> lastSprites_;  // The last complete frame.

	// ---- recording ----
	struct RecordingState {
		RecordingSink *sink = nullptr;
		int limit = 0, frames = 0, firstFrame = 0, lastFrameCount = 0;
		std::string note;
		std::string spriteText, timeline;  // Not yet written out.
		std::string events;                // This frame's timeline lines from the search.
		std::string lastBlock;             // The last frame's sprite lines, to spot a frame that is the same.
		std::set<u32> targetsWritten;
		std::map<u32, std::string> previousRuns;
		struct SeenText { TextRun run; int first = 0, last = 0; };
		struct SeenUnread { UnreadLine line; int first = 0, last = 0; std::string outcome; };
		std::map<std::string, SeenText> texts;
		std::map<u64, SeenUnread> unread;
		struct TextureKey {
			u32 addr; u16 w, h; u8 format;
			bool operator<(const TextureKey &o) const {
				if (addr != o.addr) return addr < o.addr;
				if (w != o.w) return w < o.w;
				if (h != o.h) return h < o.h;
				return format < o.format;
			}
		};
		struct TextureUse {
			u64 rects = 0, letterSized = 0;
			int frames = 0, lastFrame = -1;
			u32 stride = 0, clut = 0;
			u8 bpp = 0;
			bool fromSurface = false, surface = false;
			RectF box;
			float minW = 0, maxW = 0, minH = 0, maxH = 0;
		};
		std::map<TextureKey, TextureUse> textures;
		struct DrawKey {
			u8 prim; bool textured, clear; u32 texAddr; u16 texW, texH; u8 texFormat;
			bool operator<(const DrawKey &o) const {
				if (prim != o.prim) return prim < o.prim;
				if (textured != o.textured) return textured < o.textured;
				if (clear != o.clear) return clear < o.clear;
				if (texAddr != o.texAddr) return texAddr < o.texAddr;
				if (texW != o.texW) return texW < o.texW;
				if (texH != o.texH) return texH < o.texH;
				return texFormat < o.texFormat;
			}
		};
		struct DrawTally { u64 prims = 0, vertices = 0, rectangles = 0; std::string name; };
		std::map<DrawKey, DrawTally> draws;
	};
	RecordingState rec_;
	void RecordFrame();
	void RecordSearch(const std::vector<int> &symbols, u32 at);
	std::string FontTables() const;
	// A row's symbols as text: cell numbers, "s12" for a loose symbol, "_" for a gap.
	std::string Shape(const std::vector<int> &symbols, size_t maxLength) const;

	// The latest call-stream text, shown for a while (it has no "gone" event).
	TextRun streamRun_;
	int streamRunUntil_ = 0;
};

}  // namespace Discovery
