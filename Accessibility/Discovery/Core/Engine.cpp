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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Engine.h"
#include "TextUtil.h"

namespace Discovery {

namespace {

const size_t MAX_SAMPLES = 24;
const size_t MAX_SPRITES = 8000;
// The glyph cipher searches memory for lines it cannot read yet. A search
// takes a few milliseconds, so it runs only now and then, a few lines a time.
const int LEARN_EVERY_FRAMES = 20;
const int LEARN_LINES_PER_ROUND = 1;
// A line that was not found is tried again after this long, then after twice
// as long, and so on, up to about a minute.
const int RETRY_AFTER_FRAMES = 240;
const int RETRY_LONGEST_FRAMES = 3600;
const int TEXT_INDEX_FRAMES = 300;
// The search for text in a game's own letter code reads all of memory: one
// row at most in this many frames.
const int CODE_SEARCH_FRAMES = 60;
// Numbers: rows this short are looked at as possible numbers.
const size_t MAX_SHORT_ROW = 24;
const size_t MAX_SHORT_ROWS = 300;
// The number search reads all of memory once: not more often than this.
const int NUMBER_SEARCH_FRAMES = 120;
// A number must have stood still this long before memory is compared with
// it: games count a value up or down on screen after memory has changed.
const int NUMBER_STABLE_FRAMES = 10;
const size_t MAX_NUMBER_CANDIDATES = 64;
const u32 NUMBER_RECORD_MARGIN = 32;
// A line is reported as text when at least this share of its symbols is known.
const float READABLE_SHARE = 0.6f;
const size_t MAX_SYSTEM_RUNS = 400;
const size_t MAX_STREAM_CHARS = 2000;
// How long a call-stream text counts as "on screen" (it has no end event).
const int STREAM_HOLD_FRAMES = 300;

std::string Number(u64 n) {
	return std::to_string(n);
}

}  // namespace

// ---- Survey ----------------------------------------------------------------

u64 Survey::Get(const std::string &name) const {
	auto it = counters_.find(name);
	return it == counters_.end() ? 0 : it->second;
}

void Survey::AddSample(std::string_view origin, std::string_view text) {
	std::string line = std::string(origin) + ": " + NormalizeSpaces(text);
	if (line.size() > 160) {
		line.resize(160);
	}
	if (!samples_.empty() && samples_.back() == line) {
		return;
	}
	samples_.push_back(line);
	if (samples_.size() > MAX_SAMPLES) {
		samples_.erase(samples_.begin());
	}
}

void Survey::Clear() {
	counters_.clear();
	samples_.clear();
}

std::string Survey::Verdict() const {
	u64 frames = Get("frames");
	u64 rendered = Get("font.glyphs_rendered");
	u64 queried = Get("font.chars_queried");
	u64 matched = Get("draw.glyph_quads");
	u64 dialog = Get("dialog.texts");
	u64 sprites = Get("draw.textured_rect_prims");
	u64 sampled = Get("draw.sampled_frames");

	std::string out;
	if (frames == 0) {
		return "No frames seen yet.";
	}
	if (rendered > 0 && matched > 0) {
		out = "This game uses the system font library, and its glyphs were matched to draws on screen: "
			+ Number(rendered) + " glyphs rendered, " + Number(matched) + " glyph draws matched. Its text can be read directly.";
	} else if (rendered > 0) {
		out = "This game uses the system font library (" + Number(rendered) + " glyphs rendered), but no draw from those glyphs was seen. "
			"It probably copies glyphs elsewhere before drawing. Text is available in call order, without screen positions.";
	} else if (queried > 0) {
		out = "This game asks the system font library about characters (" + Number(queried) + " queries) but has not rendered any glyph yet.";
	} else {
		out = "This game has not used the system font library so far. Its own text needs the Phase D2 method.";
	}
	if (dialog > 0) {
		out += " System dialog text was seen.";
	}
	if (sampled > 0) {
		out += " On average it draws " + Number(sprites / sampled) + " textured rectangle batches per frame.";
	}
	return out;
}

std::string Survey::Report() const {
	std::string out = Verdict() + "\n";
	for (const auto &kv : counters_) {
		out += "  " + kv.first + " = " + Number(kv.second) + "\n";
	}
	for (const std::string &s : samples_) {
		out += "  sample: " + s + "\n";
	}
	return out;
}

// ---- Engine ----------------------------------------------------------------

void Engine::Reset() {
	StopRecording();  // Whatever was recorded is still worth having.
	ledger_.Clear();
	book_.Clear();
	tried_.clear();
	matchedTimes_.clear();
	pending_.clear();
	unread_.clear();
	sprites_.clear();
	lastSprites_.clear();
	targets_.clear();
	screen_.Clear();
	survey_.Clear();
	shortRows_.clear();
	numbers_.clear();
	tracks_.clear();
	selection_.Clear();
	book_.ForgetProfile();
	findings_.clear();
	lastSelectionFinding_.clear();
	recordsNoted_ = 0;
	numbersTriedFrame_ = -1000000;
	frame_ = 0;
	// The list of places text could be belongs to the game that ended.
	textIndexFrame_ = -1000000;
	codeSearchFrame_ = -1000000;
	glyphs_.clear();
	systemRuns_.clear();
	highlights_.clear();
	streamChars_.clear();
	streamRun_ = TextRun();
	streamRunUntil_ = 0;
	drawOrder_ = 0;
	glyphQuadsThisFrame_ = 0;
}

void Engine::OnMemoryReplaced() {
	ledger_.Clear();
	book_.Clear();
	tried_.clear();
	matchedTimes_.clear();
	textIndexFrame_ = -1000000;  // All of memory is new.
	codeSearchFrame_ = -1000000;
	shortRows_.clear();
	numbers_.clear();
	tracks_.clear();
	numbersTriedFrame_ = -1000000;
	pending_.clear();
	glyphs_.clear();
	streamChars_.clear();
	streamRun_ = TextRun();
	streamRunUntil_ = 0;
}

void Engine::OnSystemText(std::string_view utf8, const RectF &rect) {
	survey_.Count("dialog.texts");
	if (systemRuns_.size() >= MAX_SYSTEM_RUNS) {
		return;
	}
	std::string text = NormalizeSpaces(utf8);
	if (text.empty()) {
		return;
	}
	// A dialog may draw the same string twice in a frame (a fade, a shadow).
	for (const TextRun &r : systemRuns_) {
		if (r.text == text && std::abs(r.rect.x0 - rect.x0) < 2 && std::abs(r.rect.y0 - rect.y0) < 2) {
			return;
		}
	}
	TextRun run;
	run.text = text;
	run.rect = rect;
	run.hasRect = true;
	run.source = TextSource::SystemDialog;
	run.confidence = 1.0f;
	systemRuns_.push_back(run);
}

void Engine::OnSystemHighlight(const RectF &rect) {
	// Selection boxes are button-sized. Full-width bars and hairlines are decoration.
	if (rect.Height() >= 6 && rect.Height() <= 48 && rect.Width() >= 8 && rect.Width() <= 320 && highlights_.size() < 64) {
		highlights_.push_back(rect);
	}
}

void Engine::OnGlyphRendered(const GlyphCell &cellIn) {
	GlyphCell cell = cellIn;
	cell.frame = frame_;
	ledger_.Record(cell);
	survey_.Count("font.glyphs_rendered");
	if (streamChars_.size() < MAX_STREAM_CHARS) {
		streamChars_.push_back(cell.codepoint);
		if (!streamPc_) {
			streamPc_ = cell.pc;
		}
	}
}

void Engine::OnAnchorText(std::string_view origin, std::string_view utf8) {
	survey_.AddSample(origin, utf8);
}

void Engine::OnSpriteDraw(const SpriteDraw &draw) {
	if (captureAll_ && sprites_.size() < MAX_SPRITES) {
		sprites_.push_back(draw);
	}
	if (draw.texStride == 0) {
		return;
	}
	ledger_.Find(draw.texAddr, draw.texStride, draw.texBpp, draw.u0, draw.v0, draw.u1, draw.v1, &hitScratch_);
	if (hitScratch_.empty()) {
		return;
	}
	float u0 = std::min(draw.u0, draw.u1), u1 = std::max(draw.u0, draw.u1);
	float v0 = std::min(draw.v0, draw.v1), v1 = std::max(draw.v0, draw.v1);
	if (u1 - u0 < 0.5f || v1 - v0 < 0.5f || draw.screen.Empty()) {
		return;
	}
	// Texels to screen pixels: the rectangle may be drawn scaled.
	float sx = draw.screen.Width() / (u1 - u0);
	float sy = draw.screen.Height() / (v1 - v0);
	bool single = hitScratch_.size() == 1;
	for (const GlyphLedger::Hit &hit : hitScratch_) {
		const GlyphCell &c = *hit.cell;
		PlacedGlyph g;
		g.codepoint = c.codepoint;
		g.target = draw.target;
		g.order = drawOrder_++;
		g.pc = c.pc;
		g.bufferAddr = draw.texAddr;
		if (single || c.w == 0 || c.h == 0) {
			// One glyph per rectangle: the rectangle is the glyph's place.
			g.rect = draw.screen;
		} else {
			g.rect.x0 = draw.screen.x0 + (hit.u - u0) * sx;
			g.rect.y0 = draw.screen.y0 + (hit.v - v0) * sy;
			g.rect.x1 = g.rect.x0 + c.w * sx;
			g.rect.y1 = g.rect.y0 + c.h * sy;
		}
		if (c.w != 0 && c.advance > 0) {
			g.hasMetrics = true;
			// The pen is `left` pixels before the glyph's own left edge. For a
			// single-glyph rectangle the game may include padding, so measure
			// from where the glyph's texels start.
			float glyphLeft = draw.screen.x0 + (hit.u - u0) * sx;
			g.penX = glyphLeft - c.left * sx;
			g.advance = c.advance * sx;
		}
		glyphs_.push_back(g);
		glyphQuadsThisFrame_++;
	}
}

void Engine::OnRenderTarget(u32 addr, u32 byteSize) {
	if (byteSize == 0) {
		return;
	}
	auto it = targets_.find(addr);
	if (it != targets_.end()) {
		it->second = std::max(it->second, byteSize);
	} else if (targets_.size() < 64) {
		targets_[addr] = byteSize;
	}
}

bool Engine::IsSurface(const SpriteDraw &d) const {
	if (!d.fromSurface) {
		return false;
	}
	for (const auto &t : targets_) {
		if (d.texAddr >= t.first && d.texAddr - t.first < t.second) {
			return true;
		}
	}
	return false;
}

void Engine::OnFrameEnd() {
	frame_++;
	survey_.Count("frames");
	survey_.Count("draw.glyph_quads", glyphQuadsThisFrame_);

	std::vector<TextRun> runs;

	// System dialog text first: it is modal, so it is what the player must hear.
	std::sort(systemRuns_.begin(), systemRuns_.end(), [](const TextRun &a, const TextRun &b) {
		if (std::abs(a.rect.y0 - b.rect.y0) > 4) {
			return a.rect.y0 < b.rect.y0;
		}
		return a.rect.x0 < b.rect.x0;
	});
	for (TextRun &run : systemRuns_) {
		float cx = run.rect.CenterX(), cy = run.rect.CenterY();
		for (const RectF &h : highlights_) {
			if (cx >= h.x0 && cx <= h.x1 && cy >= h.y0 && cy <= h.y1) {
				run.selected = true;
			}
		}
		runs.push_back(run);
	}

	std::vector<TextRun> fontRuns = AssembleRuns(std::move(glyphs_));
	runs.insert(runs.end(), fontRuns.begin(), fontRuns.end());

	if (captureAll_) {
		// Timed, so the survey can say what the layer costs.
		auto t0 = std::chrono::steady_clock::now();
		ReadGameFontText(&runs);
		auto t1 = std::chrono::steady_clock::now();
		if (frame_ % LEARN_EVERY_FRAMES == 0) {
			LearnFromPendingLines();
		}
		if (frame_ % LEARN_EVERY_FRAMES == LEARN_EVERY_FRAMES / 2) {
			LearnNumbers();
		}
		TrackNumbers();
		auto t2 = std::chrono::steady_clock::now();
		survey_.Count("cost.reading_microseconds", (u64)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
		survey_.Count("cost.searching_microseconds", (u64)std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count());
	}

	// Fallback: glyphs were rendered this frame, but nothing on screen was
	// drawn from ledger memory. Offer the characters in call order.
	if (!streamChars_.empty() && glyphQuadsThisFrame_ == 0) {
		std::string text;
		for (u32 c : streamChars_) {
			AppendUtf8(text, c);
		}
		text = NormalizeSpaces(text);
		if (!text.empty()) {
			if (streamRunUntil_ > frame_ && streamRun_.firstFrame == frame_ - 1) {
				// The string is being rendered over several frames: keep adding.
				streamRun_.text += text;
			} else {
				streamRun_ = TextRun();
				streamRun_.text = text;
			}
			streamRun_.source = TextSource::FontCallStream;
			streamRun_.confidence = 0.5f;
			streamRun_.codePc = streamPc_;
			streamRun_.firstFrame = frame_;
			streamRunUntil_ = frame_ + STREAM_HOLD_FRAMES;
			survey_.Count("font.stream_runs");
		}
	}
	if (streamRunUntil_ > frame_ && glyphQuadsThisFrame_ == 0 && !streamRun_.text.empty()) {
		runs.push_back(streamRun_);
	}

	selection_.Update(frame_, runs, sprites_);
	// For the profile: how this game shows its selection, and its records.
	if (selection_.Current().known) {
		char how[80];
		if (selection_.Current().markTex != 0) {
			snprintf(how, sizeof(how), "selection = shown by a mark drawn from texture 0x%08X", selection_.Current().markTex);
		} else {
			snprintf(how, sizeof(how), "selection = shown by the %s of the text", selection_.Current().how);
		}
		if (lastSelectionFinding_ != how) {
			lastSelectionFinding_ = how;
			NoteFinding(how);
		}
	}
	for (; recordsNoted_ < book_.NumberRecords().size(); recordsNoted_++) {
		const NumberRecord &record = book_.NumberRecords()[recordsNoted_];
		char line[96];
		snprintf(line, sizeof(line), "record = 0x%08X, %u bytes: numbers shown together on screen", record.addr, record.size);
		NoteFinding(line);
	}
	screen_.Update(frame_, std::move(runs));
	if (rec_.sink) {
		RecordFrame();
	}

	lastSprites_.swap(sprites_);
	sprites_.clear();
	glyphs_.clear();
	systemRuns_.clear();
	highlights_.clear();
	streamChars_.clear();
	streamPc_ = 0;
	drawOrder_ = 0;
	glyphQuadsThisFrame_ = 0;
}

// ---- the game's own font ---------------------------------------------------

// Could this rectangle be a letter? Only by size: letters are small.
bool LetterSized(const SpriteDraw &d) {
	float w = d.screen.Width(), h = d.screen.Height();
	float uw = std::abs(d.u1 - d.u0), vh = std::abs(d.v1 - d.v0);
	// Width from 1.5: an "i" or "l" is two texels wide (Persona 2's menu font).
	return w >= 1.5f && w <= 48 && h >= 3 && h <= 48 && uw >= 1.5f && uw <= 64 && vh >= 3 && vh <= 64;
}

static u64 HashSymbols(const std::vector<int> &symbols) {
	u64 hash = 1469598103934665603ull;
	for (int sym : symbols) {
		hash = (hash ^ (u64)(u32)sym) * 1099511628211ull;
	}
	return hash;
}

void Engine::ReadGameFontText(std::vector<TextRun> *runs) {
	pending_.clear();
	unread_.clear();
	shortRows_.clear();
	numbers_.clear();
	// One list per texture: a line of text comes from one font texture, and
	// other small images drawn over it (a fade at the edge of a scrolling
	// line, an icon) must not break it up.
	// Textures of one size and format are taken together: a font may be
	// spread over several of them (capitals in one, small letters in another).
	std::map<u64, std::vector<PlacedGlyph>> byTexture;
	for (size_t i = 0; i < sprites_.size(); i++) {
		const SpriteDraw &d = sprites_[i];
		if (!LetterSized(d) || IsSurface(d)) {
			continue;
		}
		std::vector<PlacedGlyph> &glyphs = byTexture[((u64)d.texW << 32) | ((u64)d.texH << 16) | d.texFormat];
		PlacedGlyph g;
		g.codepoint = (u32)book_.Intern(d);  // A symbol number, not a character.
		g.rect = d.screen;
		g.target = d.target;
		g.order = (u32)i;
		g.bufferAddr = d.texAddr;
		glyphs.push_back(g);
	}

	std::vector<TextRun> found;
	for (auto &texture : byTexture) {
	for (const std::vector<PlacedGlyph> &line : AssembleLinesInDrawOrder(std::move(texture.second))) {
		size_t end = 0;
		while (end < line.size()) {
			// A row may pass from one texture to another only where both
			// letters stand exactly alike (same top, same height): pages of
			// one font do, two different fonts side by side do not.
			size_t start = end;
			for (end = start + 1; end < line.size(); end++) {
				const PlacedGlyph &a = line[end - 1], &b = line[end];
				if (a.bufferAddr == b.bufferAddr) {
					continue;
				}
				if (std::abs(a.rect.y0 - b.rect.y0) > 0.5f || std::abs(a.rect.Height() - b.rect.Height()) > 0.5f) {
					break;
				}
				book_.Join(a.bufferAddr, b.bufferAddr);
			}
			TextRun run;
			run.source = TextSource::GameFont;
			run.hasRect = true;
			run.target = line[start].target;
			run.bufferAddr = line[start].bufferAddr;
			run.rect = line[start].rect;
			run.color = line[start].order < sprites_.size() ? sprites_[line[start].order].color : 0;
			PendingLine pendingLine;
			std::vector<char> spaceBefore;
			int known = 0, deduced = 0;
			for (size_t k = start; k < end; k++) {
				const PlacedGlyph &g = line[k];
				int id = (int)g.codepoint;
				float pendingGap = -1000;
				spaceBefore.push_back(0);
				if (k > start) {
					const PlacedGlyph &prev = line[k - 1];
					int prevId = (int)prev.codepoint;
					float advance = g.rect.x0 - prev.rect.x0;
					float h = std::max(prev.rect.Height(), 1.0f);
					// Steps are remembered as a share of the letter's height, so
					// the same font drawn at another size compares correctly.
					float gapShare = (g.rect.x0 - prev.rect.x1) / h;
					pendingGap = gapShare;
					float usual = book_.Symbol(prevId).minAdvance * h;
					// A space the game did not draw: the step is clearly longer
					// than this letter's usual step, or leaves a clear gap.
					bool space = usual > 0 && advance > usual + std::max(2.0f, h * 0.2f)
						&& !book_.DrawsSpaces(prev.bufferAddr);
					if (book_.IsGridCell(prevId)) {
						// A font of fixed cells: a letter is narrower than its
						// cell, so neighbours always overlap cells. A step of a
						// whole cell or more means something was skipped.
						space = space || advance >= prev.rect.Width() - 0.5f;
					} else {
						space = space || g.rect.x0 - prev.rect.x1 >= std::max(2.0f, h * 0.25f);
					}
					// A matched line of this font has shown how wide its spaces
					// are: that is better than any general rule.
					float learnedGap = book_.IsGridCell(prevId) ? 0 : book_.SpaceGap(prev.bufferAddr);
					if (learnedGap != 0) {
						space = gapShare > learnedGap;
					}
					// Digits of one strip all step alike, and the strip's step
					// is known: a wider one starts a new number ("35 40").
					// Two sizes of digits side by side are two numbers.
					if (book_.IsDigit(id) && book_.IsDigit(prevId)) {
						float digitStep = book_.DigitAdvance(prevId);
						if (!book_.SameStrip(id, prevId)) {
							space = true;
						} else if (digitStep > 0) {
							space = advance > digitStep * h * 1.2f + 1.0f;
						}
					}
					book_.NoteAdvance(prevId, advance / h);
					if (space) {
						run.text += ' ';
						pendingLine.symbols.push_back(-1);
						spaceBefore.back() = 1;
					}
					run.rect.x0 = std::min(run.rect.x0, g.rect.x0);
					run.rect.y0 = std::min(run.rect.y0, g.rect.y0);
					run.rect.x1 = std::max(run.rect.x1, g.rect.x1);
					run.rect.y1 = std::max(run.rect.y1, g.rect.y1);
				}
				const GlyphSymbol &s = book_.Symbol(id);
				pendingLine.symbols.push_back(id);
				pendingLine.gaps.push_back(pendingGap);
				if (book_.Trusted(id)) {
					AppendUtf8(run.text, (u32)s.code);
					known++;
					deduced += s.origin == GlyphOrigin::Layout ? 1 : 0;
				} else {
					AppendUtf8(run.text, 0xFFFD);  // The "unknown character" mark.
				}
			}
			int count = (int)(end - start);
			// Numbers. A short row with unknown symbols may be one: kept for
			// the number search. And the digits already confirmed are read.
			if (known < count && (size_t)count <= MAX_SHORT_ROW && shortRows_.size() < MAX_SHORT_ROWS) {
				ShortRow shortRow;
				for (size_t k = start; k < end; k++) {
					shortRow.symbols.push_back((int)line[k].codepoint);
					shortRow.x.push_back(line[k].rect.x0);
					shortRow.height.push_back(line[k].rect.Height());
				}
				shortRows_.push_back(std::move(shortRow));
			}
			for (size_t k = start; k < end;) {
				if (!book_.IsDigit((int)line[k].codepoint)) {
					k++;
					continue;
				}
				ScreenNumber number;
				number.rect = line[k].rect;
				number.tex = line[k].bufferAddr;
				u64 value = 0;
				size_t first = k;
				for (; k < end && book_.IsDigit((int)line[k].codepoint) && (k == first || !spaceBefore[k - start]); k++) {
					value = value * 10 + (u64)(book_.Symbol((int)line[k].codepoint).code - '0');
					number.digits++;
					number.rect.x1 = std::max(number.rect.x1, line[k].rect.x1);
					number.rect.y1 = std::max(number.rect.y1, line[k].rect.y1);
				}
				if (number.digits <= 9 && numbers_.size() < 200) {
					number.value = (u32)value;
					numbers_.push_back(number);
				}
			}

			float share = (float)known / (float)count;
			run.text = NormalizeSpaces(run.text);
			if (share >= READABLE_SHARE && !run.text.empty()) {
				// Starting values, to be replaced by rates measured with the harness.
				run.confidence = (deduced > 0 ? 0.8f : 0.9f) * share;
				found.push_back(run);
			} else {
				UnreadLine u;
				u.hash = HashSymbols(pendingLine.symbols);
				// A recording keeps the whole row; the report on screen a short one.
				u.shape = Shape(pendingLine.symbols, rec_.sink ? 4000 : 150);
				u.rect = run.rect;
				u.tex = run.bufferAddr;
				u.symbols = count;
				u.known = known;
				if (count >= 3 && unread_.size() < 200) {
					unread_.push_back(u);
				}
			}
			if (known < count && count >= (book_.UsesOwnCode(run.bufferAddr) ? 4 : 5)) {
				pendingLine.hash = HashSymbols(pendingLine.symbols);
				pendingLine.tex = run.bufferAddr;
				pending_.push_back(std::move(pendingLine));
			}
		}
	}
	}
	std::sort(found.begin(), found.end(), [](const TextRun &a, const TextRun &b) {
		float h = std::max(std::min(a.rect.Height(), b.rect.Height()), 4.0f);
		if (std::abs(a.rect.CenterY() - b.rect.CenterY()) > h * 0.5f) {
			return a.rect.CenterY() < b.rect.CenterY();
		}
		return a.rect.x0 < b.rect.x0;
	});
	survey_.Count("cipher.lines_read", found.size());
	runs->insert(runs->end(), found.begin(), found.end());
}

void Engine::LearnFromPendingLines() {
	if (pending_.empty() || !machine_) {
		return;
	}
	// Longest lines first: they say the most.
	std::sort(pending_.begin(), pending_.end(), [](const PendingLine &a, const PendingLine &b) {
		return a.symbols.size() > b.symbols.size();
	});
	int attempts = 0;
	std::vector<MemoryRegion> regions;
	for (const PendingLine &line : pending_) {
		auto tried = tried_.find(line.hash);
		// A line that failed is searched for again when something new has
		// been learned, or after a few seconds: the game may only have put
		// the string in memory after we first looked.
		if (tried != tried_.end() && tried->second.version == book_.Version()) {
			int wait = RETRY_AFTER_FRAMES << std::min(tried->second.failures, 4);
			if (frame_ - tried->second.frame < std::min(wait, RETRY_LONGEST_FRAMES)) {
				continue;
			}
		}
		auto times = matchedTimes_.find(line.hash);
		if (times != matchedTimes_.end() && times->second >= 2) {
			continue;
		}
		// A font with its own letter code can only be searched for by the
		// costly search, which has its turn every so often. Until then the
		// row just waits: nothing was tried, so nothing has failed.
		if (book_.UsesOwnCode(line.tex) && frame_ - codeSearchFrame_ < CODE_SEARCH_FRAMES) {
			continue;
		}
		if (attempts++ >= LEARN_LINES_PER_ROUND) {
			break;
		}
		if (regions.empty()) {
			regions = machine_->Regions();
		}
		u32 at = 0;
		for (const MemoryRegion &region : regions) {
			const u8 *memory = machine_->View(region.start, region.size);
			if (!memory) {
				memoryCopy_.resize(region.size);
				if (!machine_->Read(region.start, memoryCopy_.data(), region.size)) {
					continue;
				}
				memory = memoryCopy_.data();
			}
			// The list of places text could be is rebuilt only every few
			// seconds: making it means reading all of memory once.
			if (textIndexBase_ != region.start || frame_ - textIndexFrame_ > TEXT_INDEX_FRAMES) {
				textIndex_.Build(memory, region.size);
				textIndexBase_ = region.start;
				textIndexFrame_ = frame_;
				survey_.Count("cipher.index_builds");
			}
			// A font whose own letter code is known is not ordinary text:
			// only the last search below can add to it.
			bool ownCode = book_.UsesOwnCode(line.tex);
			if (ownCode) {
				book_.SetLastOutcome("not_in_memory");
			} else {
				at = book_.LearnFromLine(line.symbols, memory, region.size, region.start, textIndex_);
			}
			if (!at && !ownCode) {
				// The gaps in the line are our guess at undrawn spaces. If
				// the guess was wrong (a game that draws its spaces, letters
				// set wide apart), the line without them may be found.
				std::vector<int> solid;
				for (int sym : line.symbols) {
					if (sym >= 0) {
						solid.push_back(sym);
					}
				}
				if (solid.size() != line.symbols.size()) {
					at = book_.LearnFromLine(solid, memory, region.size, region.start, textIndex_);
				}
			}
				if (!at && !ownCode) {
				// Our guess at the spaces may be wrong either way, and the
				// game may have its own code for a space.
				at = book_.LearnFromLooseLine(line.symbols, memory, region.size, region.start, textIndex_);
				if (at) {
					survey_.Count("cipher.lines_matched_loosely");
					// Memory showed where this row's spaces really are.
					const std::vector<signed char> &spaces = book_.LastSpaces();
					for (size_t k = 0; k < spaces.size() && k < line.gaps.size(); k++) {
						if (spaces[k] >= 0 && line.gaps[k] > -999) {
							book_.NoteSpacing(line.tex, line.gaps[k], spaces[k] == 1);
						}
					}
				}
			}
			if (!at && !ownCode) {
				// Still nothing: the row may not be in memory as one
				// string (control codes between the words, a name put in
				// by code, a row too long to search for). Its words may be.
				const char *whole = book_.LastOutcome();
				at = book_.LearnFromWords(line.symbols, memory, region.size, region.start, textIndex_);
				if (at) {
					survey_.Count("cipher.lines_matched_by_words");
				} else if (!strcmp(book_.LastOutcome(), "no_words")) {
					book_.SetLastOutcome(whole);  // The first answer says more.
				}
			}
			if (!at && frame_ - codeSearchFrame_ >= CODE_SEARCH_FRAMES) {
				// Last: the game may keep its text in a letter code of its own.
				const char *before = book_.LastOutcome();
				at = book_.LearnFromCodeLine(line.symbols, memory, region.size, region.start);
				if (book_.LastScanned()) {
					codeSearchFrame_ = frame_;
					survey_.Count("cipher.code_searches");
				}
				if (at) {
					survey_.Count("cipher.lines_matched_by_code");
					const std::vector<signed char> &spaces = book_.LastSpaces();
					for (size_t k = 0; k < spaces.size() && k < line.gaps.size(); k++) {
						if (spaces[k] >= 0 && line.gaps[k] > -999) {
							book_.NoteSpacing(line.tex, line.gaps[k], spaces[k] == 1);
						}
					}
				} else if (!strcmp(book_.LastOutcome(), "says_too_little")) {
					book_.SetLastOutcome(before);
				}
			}
			if (at) {
				break;
			}
		}
		survey_.Count("cipher.searches");
		if (rec_.sink) {
			RecordSearch(line.symbols, at);
		}
		if (!at) {
			survey_.Count(std::string("cipher.no_answer.") + book_.LastOutcome());
		}
		if (at) {
			if (matchedTimes_.size() > 4000) {
				matchedTimes_.clear();
			}
			matchedTimes_[line.hash]++;
			survey_.Count("cipher.lines_matched");
			book_.CheckRemembered(line.tex);
			std::string text;
			for (int sym : line.symbols) {
				AppendUtf8(text, sym < 0 ? ' ' : (u32)std::max(book_.Symbol(sym).code, 0x3F));
			}
			if (book_.Trusted(line.symbols[0] >= 0 ? line.symbols[0] : line.symbols.back())) {
				char found[64];
				snprintf(found, sizeof(found), "text = 0x%08X: ", at);
				NoteFinding(found + NormalizeSpaces(text));
			}
			char where[48];
			snprintf(where, sizeof(where), "line matched in memory at 0x%08X", at);
			survey_.AddSample(where, text);
		}
		int failures = 0;
		if (!at) {
			bool sameKnowledge = tried != tried_.end() && tried->second.version == book_.Version();
			failures = sameKnowledge ? tried->second.failures + 1 : 1;
		}
		if (tried_.size() > 4000) {
			tried_.clear();
		}
		tried_[line.hash] = Tried{ book_.Version(), frame_, failures, book_.LastOutcome() };
	}
}

// ---- numbers ----------------------------------------------------------------

void Engine::LearnNumbers() {
	if (shortRows_.empty() || !machine_) {
		return;
	}
	// The same rows with the same knowledge give the same answer.
	u64 hash = 1469598103934665603ull;
	for (const ShortRow &row : shortRows_) {
		hash = (hash ^ HashSymbols(row.symbols)) * 1099511628211ull;
	}
	if ((hash == numbersTriedHash_ && book_.Version() == numbersTriedVersion_)
		|| frame_ - numbersTriedFrame_ < NUMBER_SEARCH_FRAMES) {
		return;
	}
	numbersTriedHash_ = hash;
	numbersTriedFrame_ = frame_;
	bool learned = false;
	for (const MemoryRegion &region : machine_->Regions()) {
		const u8 *memory = machine_->View(region.start, region.size);
		if (!memory) {
			memoryCopy_.resize(region.size);
			if (!machine_->Read(region.start, memoryCopy_.data(), region.size)) {
				continue;
			}
			memory = memoryCopy_.data();
		}
		learned = book_.LearnDigits(shortRows_, memory, region.size, region.start) || learned;
	}
	numbersTriedVersion_ = book_.Version();
	survey_.Count("numbers.searches");
	if (learned) {
		survey_.Count("numbers.strips_learned");
	}
}

std::vector<u32> Engine::NumberCandidates(u32 value) const {
	std::vector<u32> out;
	if (!machine_ || value > 0xFFFF) {
		return out;
	}
	for (const NumberRecord &record : book_.NumberRecords()) {
		u32 start = (record.addr - NUMBER_RECORD_MARGIN) & ~1u;
		u8 bytes[256];
		u32 size = std::min((u32)sizeof(bytes), record.size + 2 * NUMBER_RECORD_MARGIN) & ~1u;
		if (!machine_->Read(start, bytes, size)) {
			continue;
		}
		for (u32 i = 0; i + 1 < size && out.size() < MAX_NUMBER_CANDIDATES; i += 2) {
			if (((u32)bytes[i] | ((u32)bytes[i + 1] << 8)) == value
				&& std::find(out.begin(), out.end(), start + i) == out.end()) {
				out.push_back(start + i);
			}
		}
	}
	return out;
}

void Engine::TrackNumbers() {
	if (numbers_.empty() || book_.NumberRecords().empty() || !machine_) {
		return;
	}
	if (tracks_.size() > 2000) {
		tracks_.clear();
	}
	for (ScreenNumber &n : numbers_) {
		// A number is known by where its right edge is: most are set flush right.
		u64 key = ((u64)n.tex << 32) | ((u64)((u32)std::lround(n.rect.x1) & 0xFFFF) << 16)
			| (u64)((u32)std::lround(n.rect.y0) & 0xFFFF);
		ValueTrack &t = tracks_[key];
		size_t records = book_.NumberRecords().size();
		if (!t.started || (t.changes == 0 && t.records != records && t.value == n.value)) {
			t.started = true;
			t.value = n.value;
			t.stableSince = frame_;
			t.candidates = NumberCandidates(n.value);
			t.records = records;
			t.checked = true;
		} else if (t.value != n.value) {
			t.value = n.value;
			t.stableSince = frame_;
			t.checked = false;
		} else if (!t.checked && frame_ - t.stableSince >= NUMBER_STABLE_FRAMES) {
			std::vector<u32> kept;
			for (u32 addr : t.candidates) {
				u8 bytes[2];
				if (machine_->Read(addr, bytes, 2) && ((u32)bytes[0] | ((u32)bytes[1] << 8)) == n.value) {
					kept.push_back(addr);
				}
			}
			t.candidates.swap(kept);
			t.changes++;
			t.checked = true;
		}
		n.candidates = t.candidates;
		n.changes = t.changes;
	}
}

std::string Engine::InspectionReport() const {
	std::string out;
	char line[400];
	snprintf(line, sizeof(line), "Frame %d. %d text runs on screen.\n\n", frame_, (int)screen_.Runs().size());
	out += line;

	out += "TEXT FOUND (reading order)\n";
	if (screen_.Runs().empty()) {
		out += "  none\n";
	}
	for (const TextRun &run : screen_.Runs()) {
		std::string place = "no position";
		if (run.hasRect) {
			snprintf(line, sizeof(line), "x %.0f, y %.0f, %.0f by %.0f", run.rect.x0, run.rect.y0, run.rect.Width(), run.rect.Height());
			place = line;
		}
		snprintf(line, sizeof(line), "  [%s, confidence %.2f, %s", TextSourceName(run.source), run.confidence, place.c_str());
		out += line;
		if (run.bufferAddr) {
			snprintf(line, sizeof(line), ", font texture 0x%08X", run.bufferAddr);
			out += line;
		}
		if (run.codePc) {
			snprintf(line, sizeof(line), ", code 0x%08X", run.codePc);
			out += line;
		}
		out += run.selected ? ", selected] " : "] ";
		out += run.text + "\n";
	}

	out += "\nLINES NOT READ YET (rows of letter-sized images whose characters are unknown)\n";
	if (unread_.empty()) {
		out += "  none\n";
	}
	for (const UnreadLine &u : unread_) {
		auto tried = tried_.find(u.hash);
		const char *state = "not searched for yet";
		if (u.symbols < 5) {
			state = "too short to search for";
		} else if (tried != tried_.end()) {
			state = tried->second.outcome;
		}
		snprintf(line, sizeof(line), "  x %.0f, y %.0f, %.0f by %.0f: %d symbols, %d known, texture 0x%08X; memory search: %s\n",
			u.rect.x0, u.rect.y0, u.rect.Width(), u.rect.Height(), u.symbols, u.known, u.tex, state);
		out += line;
		out += "      symbols:" + u.shape + "\n";
	}

	out += "\nSELECTED ITEM\n" + selection_.Describe();

	out += "\nNUMBERS ON SCREEN (with the places in memory each may be kept)\n";
	if (numbers_.empty()) {
		out += "  none\n";
	}
	for (const ScreenNumber &n : numbers_) {
		snprintf(line, sizeof(line), "  %u (x %.0f, y %.0f, %d digits):", n.value, n.rect.x0, n.rect.y0, n.digits);
		out += line;
		if (n.candidates.empty()) {
			out += n.changes > 0 ? " no place left after its changes" : " not in the records found";
		}
		for (size_t i = 0; i < n.candidates.size() && i < 8; i++) {
			snprintf(line, sizeof(line), " 0x%08X", n.candidates[i]);
			out += line;
		}
		if (n.candidates.size() > 8) {
			out += " and " + Number(n.candidates.size() - 8) + " more";
		}
		if (n.changes > 0) {
			out += "; narrowed by " + Number((u64)n.changes) + (n.changes == 1 ? " change" : " changes");
		}
		out += "\n";
	}
	if (!book_.NumberRecords().empty()) {
		out += "  Records (places where several of the numbers stand together, shown as 16-bit values):\n";
	}
	for (const NumberRecord &record : book_.NumberRecords()) {
		u32 start = (record.addr - 8) & ~1u;
		u8 bytes[64];
		u32 size = std::min((u32)sizeof(bytes), record.size + 24) & ~1u;
		snprintf(line, sizeof(line), "    0x%08X, %u bytes; from 0x%08X:", record.addr, record.size, start);
		out += line;
		if (machine_ && machine_->Read(start, bytes, size)) {
			for (u32 i = 0; i + 1 < size; i += 2) {
				out += " " + Number((u64)bytes[i] | ((u64)bytes[i + 1] << 8));
			}
		}
		out += "\n";
	}

	out += "\nFONTS WORKED OUT\n";
	out += book_.DescribeDigits();
	out += book_.DescribeTables();
	std::string fonts = book_.Describe();
	out += fonts.empty() ? "  none\n" : fonts;
	snprintf(line, sizeof(line), "  %d symbols seen in all, %d with a known character.\n", (int)book_.Size(), (int)book_.KnownCount());
	out += line;

	out += "\nTEXTURES DRAWN FROM IN THE LAST FRAME\n";
	struct Use { int count = 0; int letterSized = 0; u16 w = 0, h = 0; };
	std::map<u32, Use> textures;
	for (const SpriteDraw &d : lastSprites_) {
		Use &use = textures[d.texAddr];
		use.count++;
		use.letterSized += LetterSized(d) && !IsSurface(d) ? 1 : 0;
		use.w = d.texW;
		use.h = d.texH;
	}
	if (textures.empty()) {
		out += "  none (the draw tap may be off)\n";
	}
	for (const auto &kv : textures) {
		snprintf(line, sizeof(line), "  0x%08X, %d by %d: %d rectangles, %d letter-sized\n",
			kv.first, kv.second.w, kv.second.h, kv.second.count, kv.second.letterSized);
		out += line;
	}

	out += "\nSURVEY\n" + survey_.Report();
	return out;
}

std::vector<TextLocation> Engine::Locate(std::string_view utf8, size_t maxHits) const {
	std::vector<TextLocation> hits;
	if (utf8.size() < 3 || !machine_) {
		return hits;  // Too short to mean anything.
	}
	std::string narrow(utf8);
	std::string wide;
	for (u32 cp : DecodeUtf8(utf8)) {
		if (cp > 0xFFFF) {
			wide.clear();
			break;
		}
		wide += (char)(cp & 0xFF);
		wide += (char)(cp >> 8);
	}

	std::vector<u8> buffer;
	for (const MemoryRegion &region : machine_->Regions()) {
		buffer.resize(region.size);
		if (!machine_->Read(region.start, buffer.data(), region.size)) {
			continue;
		}
		const u8 *begin = buffer.data(), *end = begin + buffer.size();
		for (int pass = 0; pass < 2; pass++) {
			const std::string &needle = pass == 0 ? narrow : wide;
			if (needle.empty()) {
				continue;
			}
			const u8 *p = begin;
			while (hits.size() < maxHits) {
				p = std::search(p, end, needle.begin(), needle.end(), [](u8 a, char b) { return a == (u8)b; });
				if (p == end) {
					break;
				}
				TextLocation loc;
				loc.addr = region.start + (u32)(p - begin);
				loc.encoding = pass == 0 ? "utf8" : "utf16le";
				hits.push_back(loc);
				p++;
			}
		}
	}
	return hits;
}

}  // namespace Discovery
