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

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS  // sscanf, used with bounded formats only.
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Engine.h"
#include "Recording.h"
#include "TextUtil.h"

namespace Discovery {

namespace {

const char *const MANIFEST_FILE = "recording.txt";
const char *const SPRITES_FILE = "sprites.txt";
const char *const TIMELINE_FILE = "timeline.txt";
const char *const SUMMARY_FILE = "summary.txt";
const char *const MANIFEST_MAGIC = "discovery-recording 1";
// The text buffers are written out when they reach this size.
const size_t FLUSH_BYTES = 256 * 1024;

std::string DescribeRun(const TextRun &run) {
	char buf[200];
	std::string out = "[";
	out += TextSourceName(run.source);
	snprintf(buf, sizeof(buf), ", confidence %.2f", run.confidence);
	out += buf;
	if (run.hasRect) {
		snprintf(buf, sizeof(buf), ", x %.0f, y %.0f, %.0f by %.0f", run.rect.x0, run.rect.y0, run.rect.Width(), run.rect.Height());
		out += buf;
	}
	if (run.bufferAddr) {
		snprintf(buf, sizeof(buf), ", font texture 0x%08X", run.bufferAddr);
		out += buf;
	}
	if (run.codePc) {
		snprintf(buf, sizeof(buf), ", code 0x%08X", run.codePc);
		out += buf;
	}
	out += run.selected ? ", selected] " : "] ";
	return out + "\"" + run.text + "\"";
}

// Splits text into lines in place: calls fn(line) for each, without the line break.
template <typename Fn>
void ForEachLine(std::string_view text, Fn fn) {
	size_t pos = 0;
	while (pos < text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string_view::npos) {
			end = text.size();
		}
		size_t len = end - pos;
		if (len > 0 && text[pos + len - 1] == '\r') {
			len--;
		}
		if (!fn(text.substr(pos, len))) {
			return;
		}
		pos = end + 1;
	}
}

}  // namespace

// ---- the file format -------------------------------------------------------

std::string FormatSprite(const SpriteDraw &d) {
	char buf[320];
	// %.9g writes a float so that reading it back gives the same float.
	snprintf(buf, sizeof(buf), "%08X %u %u %u %u %u %d %08X %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %08X %08X %08X",
		d.texAddr, d.texStride, (unsigned)d.texBpp, (unsigned)d.texW, (unsigned)d.texH, (unsigned)d.texFormat,
		d.fromSurface ? 1 : 0, d.clutAddr, d.u0, d.v0, d.u1, d.v1,
		d.screen.x0, d.screen.y0, d.screen.x1, d.screen.y1, d.target, d.dataAddr, d.color);
	return buf;
}

bool ParseSprite(const char *line, SpriteDraw *out) {
	unsigned texAddr, stride, bpp, w, h, format, clut, target, data, color = 0;
	int surface;
	float u0, v0, u1, v1, x0, y0, x1, y1;
	// The colour came later: recordings made before 2026-11-15 have 18 fields.
	if (sscanf(line, "%x %u %u %u %u %u %d %x %f %f %f %f %f %f %f %f %x %x %x",
		&texAddr, &stride, &bpp, &w, &h, &format, &surface, &clut,
		&u0, &v0, &u1, &v1, &x0, &y0, &x1, &y1, &target, &data, &color) < 18) {
		return false;
	}
	*out = SpriteDraw();
	out->texAddr = texAddr;
	out->texStride = stride;
	out->texBpp = (u8)bpp;
	out->texW = (u16)w;
	out->texH = (u16)h;
	out->texFormat = (u8)format;
	out->fromSurface = surface != 0;
	out->clutAddr = clut;
	out->u0 = u0; out->v0 = v0; out->u1 = u1; out->v1 = v1;
	out->screen = RectF{ x0, y0, x1, y1 };
	out->target = target;
	out->dataAddr = data;
	out->color = color;
	return true;
}

bool RecordedMachine::Read(u32 addr, void *out, u32 size) const {
	const u8 *p = View(addr, size);
	if (!p) {
		return false;
	}
	memcpy(out, p, size);
	return true;
}

const u8 *RecordedMachine::View(u32 addr, u32 size) const {
	for (size_t i = 0; i < regions.size() && i < memory.size(); i++) {
		const MemoryRegion &r = regions[i];
		if (addr >= r.start && (u64)(addr - r.start) + size <= memory[i].size()) {
			return (const u8 *)memory[i].data() + (addr - r.start);
		}
	}
	return nullptr;
}

bool LoadRecording(RecordingSource *source, RecordedMachine *out, std::string *error) {
	*out = RecordedMachine();
	std::string manifest;
	if (!source->ReadAll(MANIFEST_FILE, &manifest)) {
		*error = std::string("could not read ") + MANIFEST_FILE;
		return false;
	}
	bool magic = false, bad = false;
	std::vector<std::string> memoryFiles;
	ForEachLine(manifest, [&](std::string_view lineView) {
		std::string line(lineView);
		if (!magic) {
			magic = line == MANIFEST_MAGIC;
			bad = !magic;
			return magic;
		}
		if (line.compare(0, 5, "game ") == 0) {
			out->game = line.substr(5);
		} else if (line.compare(0, 5, "note ") == 0) {
			out->note = line.substr(5);
		} else if (line.compare(0, 7, "region ") == 0) {
			unsigned start = 0, size = 0;
			char file[64] = {};
			int used = 0;
			if (sscanf(line.c_str(), "region %x %x %63s %n", &start, &size, file, &used) >= 3) {
				MemoryRegion r;
				r.start = start;
				r.size = size;
				r.name = used > 0 && (size_t)used <= line.size() ? line.substr(used) : std::string();
				out->regions.push_back(r);
				memoryFiles.push_back(file);
			}
		}
		return true;
	});
	if (bad || !magic) {
		*error = std::string(MANIFEST_FILE) + " does not start with \"" + MANIFEST_MAGIC + "\"";
		return false;
	}
	for (size_t i = 0; i < memoryFiles.size(); i++) {
		std::string bytes;
		if (!source->ReadAll(memoryFiles[i], &bytes) || bytes.size() != out->regions[i].size) {
			*error = "memory file " + memoryFiles[i] + " is missing or has the wrong size";
			return false;
		}
		out->memory.push_back(std::move(bytes));
	}

	std::string sprites;
	if (!source->ReadAll(SPRITES_FILE, &sprites)) {
		*error = std::string("could not read ") + SPRITES_FILE;
		return false;
	}
	int expect = 0;
	bool ok = true;
	std::vector<RenderTarget> targets;  // For the next frame.
	ForEachLine(sprites, [&](std::string_view lineView) {
		if (lineView.empty()) {
			return true;
		}
		std::string line(lineView);
		if (line.compare(0, 7, "target ") == 0) {
			unsigned addr = 0, bytes = 0;
			if (expect != 0 || sscanf(line.c_str(), "target %x %x", &addr, &bytes) != 2) {
				ok = false;
				return false;
			}
			targets.push_back(RenderTarget{ addr, bytes });
			return true;
		}
		if (line.compare(0, 6, "frame ") == 0) {
			if (expect != 0) {
				ok = false;
				return false;
			}
			RecordedFrame f;
			char word[16] = {};
			if (sscanf(line.c_str(), "frame %d %15s", &f.frame, word) != 2) {
				ok = false;
				return false;
			}
			if (!strcmp(word, "same")) {
				if (out->frames.empty()) {
					ok = false;
					return false;
				}
				f.sprites = out->frames.back().sprites;
			} else {
				expect = atoi(word);
				f.sprites.reserve((size_t)std::max(expect, 0));
			}
			f.targets.swap(targets);
			out->frames.push_back(std::move(f));
			return true;
		}
		SpriteDraw d;
		if (expect <= 0 || out->frames.empty() || !ParseSprite(line.c_str(), &d)) {
			ok = false;
			return false;
		}
		out->frames.back().sprites.push_back(d);
		expect--;
		return true;
	});
	if (!ok || expect != 0) {
		*error = std::string(SPRITES_FILE) + " is damaged (near frame " + std::to_string(out->frames.empty() ? 0 : out->frames.back().frame) + ")";
		return false;
	}
	return true;
}

// ---- writing: the Engine's recording functions -----------------------------

std::string Engine::Shape(const std::vector<int> &symbols, size_t maxLength) const {
	std::string shape;
	for (int sym : symbols) {
		if (shape.size() > maxLength) {
			shape += " ...";
			break;
		}
		int cellNumber = sym < 0 ? -1 : book_.CellNumber(sym);
		shape += sym < 0 ? " _" : (cellNumber >= 0 ? " " + std::to_string(cellNumber) : " s" + std::to_string(sym));
	}
	return shape;
}

bool Engine::StartRecording(RecordingSink *sink, int frames, const std::string &note) {
	if (!sink || rec_.sink) {
		return false;
	}
	rec_ = RecordingState();
	rec_.sink = sink;
	rec_.limit = frames;
	rec_.firstFrame = frame_ + 1;
	rec_.note = NormalizeSpaces(note);
	rec_.timeline = "Timeline of a discovery recording. Game: " + (machine_ ? machine_->GameId() : std::string()) + "\n"
		"Marks:  +  a text appeared    ~  a text changed in place    -  a text went away\n"
		"        ?  a row of letter-sized images that could not be read (first time seen)\n"
		"        search  the layer looked in memory for an unread row\n"
		"Symbols of a row: a number is the image's cell in its font texture, sNN a symbol that is not a grid cell, _ a gap.\n\n";
	return true;
}

void Engine::OnDrawNote(const DrawNote &note) {
	if (!rec_.sink) {
		return;
	}
	RecordingState::DrawKey key{ note.prim, note.textured, note.clear, note.textured ? note.texAddr : 0,
		note.textured ? note.texW : (u16)0, note.textured ? note.texH : (u16)0, note.textured ? note.texFormat : (u8)0 };
	RecordingState::DrawTally &t = rec_.draws[key];
	t.prims++;
	t.vertices += note.vertices;
	t.rectangles += (u64)std::max(note.rectangles, 0);
	t.name = note.primName;
}

void Engine::RecordSearch(const std::vector<int> &symbols, u32 at) {
	char buf[160];
	u32 tex = 0;
	for (int sym : symbols) {
		if (sym >= 0) {
			tex = book_.Symbol(sym).key.tex;
			break;
		}
	}
	snprintf(buf, sizeof(buf), "  search: %d symbols of texture 0x%08X:", (int)symbols.size(), tex);
	rec_.events += buf + Shape(symbols, 4000) + "\n";
	if (at) {
		std::string text;
		for (int sym : symbols) {
			AppendUtf8(text, sym < 0 ? ' ' : (u32)std::max(book_.Symbol(sym).code, 0x3F));
		}
		snprintf(buf, sizeof(buf), "      found in memory at 0x%08X: ", at);
		rec_.events += buf + ("\"" + text + "\"\n");
	} else {
		rec_.events += std::string("      no answer: ") + book_.LastOutcome() + "\n";
	}
}

void Engine::RecordFrame() {
	RecordingState &r = rec_;
	r.frames++;
	char buf[400];

	// ---- sprites.txt ----
	std::string block;
	block.reserve(sprites_.size() * 120);
	for (const SpriteDraw &d : sprites_) {
		block += FormatSprite(d);
		block += '\n';
		RecordingState::TextureUse &use = r.textures[RecordingState::TextureKey{ d.texAddr, d.texW, d.texH, d.texFormat }];
		if (use.rects == 0) {
			use.box = d.screen;
			use.minW = use.maxW = d.screen.Width();
			use.minH = use.maxH = d.screen.Height();
		}
		use.rects++;
		use.letterSized += LetterSized(d) && !IsSurface(d) ? 1 : 0;
		if (use.lastFrame != frame_) {
			use.lastFrame = frame_;
			use.frames++;
		}
		use.stride = d.texStride;
		use.bpp = d.texBpp;
		use.clut = d.clutAddr;
		use.fromSurface = d.fromSurface;
		use.surface = IsSurface(d);
		use.box.x0 = std::min(use.box.x0, d.screen.x0);
		use.box.y0 = std::min(use.box.y0, d.screen.y0);
		use.box.x1 = std::max(use.box.x1, d.screen.x1);
		use.box.y1 = std::max(use.box.y1, d.screen.y1);
		use.minW = std::min(use.minW, d.screen.Width());
		use.maxW = std::max(use.maxW, d.screen.Width());
		use.minH = std::min(use.minH, d.screen.Height());
		use.maxH = std::max(use.maxH, d.screen.Height());
	}
	for (const auto &t : targets_) {
		if (r.targetsWritten.insert(t.first).second) {
			snprintf(buf, sizeof(buf), "target %08X %08X\n", t.first, t.second);
			r.spriteText += buf;
		}
	}
	if (r.frames > 1 && block == r.lastBlock) {
		snprintf(buf, sizeof(buf), "frame %d same\n", frame_);
		r.spriteText += buf;
	} else {
		snprintf(buf, sizeof(buf), "frame %d %d\n", frame_, (int)sprites_.size());
		r.spriteText += buf;
		r.spriteText += block;
		r.lastBlock.swap(block);
	}

	// ---- timeline.txt ----
	std::string events;
	events.swap(r.events);
	std::map<u32, std::string> now;
	for (const TextRun &run : screen_.Runs()) {
		now[run.id] = run.text;
		auto before = r.previousRuns.find(run.id);
		if (before == r.previousRuns.end()) {
			snprintf(buf, sizeof(buf), "  + run %u ", run.id);
			events += buf + DescribeRun(run) + "\n";
		} else if (before->second != run.text) {
			snprintf(buf, sizeof(buf), "  ~ run %u ", run.id);
			events += buf + DescribeRun(run) + "   (was \"" + before->second + "\")\n";
		}
		auto seen = r.texts.find(run.text);
		if (seen == r.texts.end()) {
			r.texts[run.text] = RecordingState::SeenText{ run, frame_, frame_ };
		} else {
			seen->second.last = frame_;
		}
	}
	for (const auto &kv : r.previousRuns) {
		if (!now.count(kv.first)) {
			snprintf(buf, sizeof(buf), "  - run %u \"", kv.first);
			events += buf + kv.second + "\"\n";
		}
	}
	r.previousRuns.swap(now);

	for (const UnreadLine &u : unread_) {
		auto tried = tried_.find(u.hash);
		const char *state = u.symbols < 5 ? "too short to search for" : (tried != tried_.end() ? tried->second.outcome : "not searched for yet");
		auto seen = r.unread.find(u.hash);
		if (seen == r.unread.end()) {
			snprintf(buf, sizeof(buf), "  ? x %.0f, y %.0f, %.0f by %.0f: %d symbols, %d known, texture 0x%08X:",
				u.rect.x0, u.rect.y0, u.rect.Width(), u.rect.Height(), u.symbols, u.known, u.tex);
			events += buf + u.shape + "\n";
			r.unread[u.hash] = RecordingState::SeenUnread{ u, frame_, frame_, state };
		} else {
			seen->second.last = frame_;
			seen->second.outcome = state;
		}
	}
	if (!events.empty()) {
		snprintf(buf, sizeof(buf), "frame %d\n", frame_);
		r.timeline += buf + events;
	}

	if (r.spriteText.size() >= FLUSH_BYTES) {
		r.sink->Append(SPRITES_FILE, r.spriteText.data(), r.spriteText.size());
		r.spriteText.clear();
	}
	if (r.timeline.size() >= FLUSH_BYTES) {
		r.sink->Append(TIMELINE_FILE, r.timeline.data(), r.timeline.size());
		r.timeline.clear();
	}
	if (r.limit > 0 && r.frames >= r.limit) {
		StopRecording();
	}
}

std::string Engine::FontTables() const {
	std::vector<int> order(book_.Size());
	for (size_t i = 0; i < order.size(); i++) {
		order[i] = (int)i;
	}
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		return book_.Symbol(a).key < book_.Symbol(b).key;
	});
	std::string out;
	char buf[300];
	u32 tex = 0;
	u16 texW = 0, texH = 0;
	bool first = true;
	for (int id : order) {
		const GlyphSymbol &s = book_.Symbol(id);
		if (first || s.key.tex != tex || s.key.texW != texW || s.key.texH != texH) {
			tex = s.key.tex;
			texW = s.key.texW;
			texH = s.key.texH;
			first = false;
			snprintf(buf, sizeof(buf), "  texture 0x%08X, %d by %d%s\n", tex, texW, texH,
				book_.DrawsSpaces(tex) ? " (draws its spaces)" : "");
			out += buf;
		}
		int cell = book_.CellNumber(id);
		char cellText[32] = "not a grid cell";
		if (cell >= 0) {
			snprintf(cellText, sizeof(cellText), "cell %d", cell);
		}
		std::string code = "character unknown";
		if (s.code >= 0) {
			std::string ch;
			AppendUtf8(ch, (u32)s.code);
			snprintf(buf, sizeof(buf), "code 0x%02X \"%s\", %s, %s", s.code, ch.c_str(),
				s.origin == GlyphOrigin::Matched ? "read from memory" : "from the layout",
				book_.Trusted(id) ? "trusted" : "not yet trusted");
			code = buf;
		}
		snprintf(buf, sizeof(buf), "    s%d: u %d to %d, v %d to %d (%d by %d), %s, %s, smallest step %.2f\n",
			id, s.key.u0, s.key.u1, s.key.v0, s.key.v1, s.key.u1 - s.key.u0, s.key.v1 - s.key.v0,
			cellText, code.c_str(), s.minAdvance);
		out += buf;
	}
	return out.empty() ? "  none\n" : out;
}

void Engine::StopRecording() {
	RecordingState &r = rec_;
	if (!r.sink) {
		return;
	}
	RecordingSink *sink = r.sink;
	char buf[400];

	if (!r.spriteText.empty()) {
		sink->Append(SPRITES_FILE, r.spriteText.data(), r.spriteText.size());
	}
	if (!r.events.empty()) {
		r.timeline += "after the last frame\n" + r.events;
	}
	snprintf(buf, sizeof(buf), "end of recording: %d frames, %d to %d\n", r.frames, r.firstFrame, r.firstFrame + r.frames - 1);
	r.timeline += buf;
	sink->Append(TIMELINE_FILE, r.timeline.data(), r.timeline.size());

	// ---- summary.txt ----
	std::string game = machine_ ? machine_->GameId() : std::string();
	std::string out = "Discovery recording of " + game + "\n";
	if (!r.note.empty()) {
		out += "Note: " + r.note + "\n";
	}
	snprintf(buf, sizeof(buf), "%d frames recorded, %d to %d.\n\n", r.frames, r.firstFrame, r.firstFrame + r.frames - 1);
	out += buf;

	out += "==== THE SCREEN AT THE END ====\n\n" + InspectionReport();

	out += "\n==== EVERY TEXT SEEN DURING THE RECORDING (in order of first appearance) ====\n";
	std::vector<const RecordingState::SeenText *> texts;
	for (const auto &kv : r.texts) {
		texts.push_back(&kv.second);
	}
	std::sort(texts.begin(), texts.end(), [](const RecordingState::SeenText *a, const RecordingState::SeenText *b) {
		return a->first != b->first ? a->first < b->first : a->run.rect.y0 < b->run.rect.y0;
	});
	if (texts.empty()) {
		out += "  none\n";
	}
	for (const RecordingState::SeenText *t : texts) {
		snprintf(buf, sizeof(buf), "  frames %d to %d: ", t->first, t->last);
		out += buf + DescribeRun(t->run) + "\n";
	}

	out += "\n==== EVERY UNREAD ROW SEEN DURING THE RECORDING ====\n";
	std::vector<const RecordingState::SeenUnread *> rows;
	for (const auto &kv : r.unread) {
		rows.push_back(&kv.second);
	}
	std::sort(rows.begin(), rows.end(), [](const RecordingState::SeenUnread *a, const RecordingState::SeenUnread *b) {
		return a->first != b->first ? a->first < b->first : a->line.rect.y0 < b->line.rect.y0;
	});
	if (rows.empty()) {
		out += "  none\n";
	}
	for (const RecordingState::SeenUnread *u : rows) {
		snprintf(buf, sizeof(buf), "  frames %d to %d: x %.0f, y %.0f, %.0f by %.0f: %d symbols, %d known, texture 0x%08X; memory search: %s\n",
			u->first, u->last, u->line.rect.x0, u->line.rect.y0, u->line.rect.Width(), u->line.rect.Height(),
			u->line.symbols, u->line.known, u->line.tex, u->outcome.c_str());
		out += buf;
		out += "      symbols:" + u->line.shape + "\n";
	}

	out += "\n==== FONT TABLES (every letter-sized image seen, by texture) ====\n" + FontTables();

	out += "\n==== TEXTURES DRAWN FROM DURING THE RECORDING ====\n";
	if (r.textures.empty()) {
		out += "  none\n";
	}
	for (const auto &kv : r.textures) {
		const RecordingState::TextureUse &use = kv.second;
		snprintf(buf, sizeof(buf), "  0x%08X, %d by %d, format %d, %d bits, row bytes %u%s, palette 0x%08X: %llu rectangles (%llu letter-sized) in %d frames; "
			"on screen x %.0f to %.0f, y %.0f to %.0f; rectangle sizes %.0f to %.0f wide, %.0f to %.0f high\n",
			kv.first.addr, kv.first.w, kv.first.h, kv.first.format, use.bpp, use.stride,
			use.surface ? ", a drawing surface" : (use.fromSurface ? ", a picture kept in video memory" : ""), use.clut,
			(unsigned long long)use.rects, (unsigned long long)use.letterSized, use.frames,
			use.box.x0, use.box.x1, use.box.y0, use.box.y1, use.minW, use.maxW, use.minH, use.maxH);
		out += buf;
	}

	out += "\n==== DRAWS THE TAP SAW (all kinds, whole recording) ====\n";
	if (r.draws.empty()) {
		out += "  none reported by the adapter\n";
	}
	for (const auto &kv : r.draws) {
		const RecordingState::DrawKey &k = kv.first;
		const RecordingState::DrawTally &t = kv.second;
		std::string what = t.name.empty() ? "primitive " + std::to_string(k.prim) : t.name;
		if (k.clear) {
			what += ", clearing";
		}
		if (k.textured) {
			snprintf(buf, sizeof(buf), ", texture 0x%08X %d by %d format %d", k.texAddr, k.texW, k.texH, k.texFormat);
			what += buf;
		} else {
			what += ", no texture";
		}
		snprintf(buf, sizeof(buf), ": %llu draws, %llu vertices, %llu rectangles taken\n",
			(unsigned long long)t.prims, (unsigned long long)t.vertices, (unsigned long long)t.rectangles);
		out += "  " + what + buf;
	}
	sink->Append(SUMMARY_FILE, out.data(), out.size());

	// ---- memory and the manifest ----
	std::string manifest = std::string(MANIFEST_MAGIC) + "\n";
	manifest += "game " + game + "\n";
	if (!r.note.empty()) {
		manifest += "note " + r.note + "\n";
	}
	manifest += "first_frame " + std::to_string(r.firstFrame) + "\n";
	manifest += "frames " + std::to_string(r.frames) + "\n";
	if (machine_) {
		int n = 0;
		std::vector<u8> copy;
		for (const MemoryRegion &region : machine_->Regions()) {
			const u8 *memory = machine_->View(region.start, region.size);
			if (!memory) {
				copy.resize(region.size);
				if (!machine_->Read(region.start, copy.data(), region.size)) {
					continue;
				}
				memory = copy.data();
			}
			std::string file = "memory-" + std::to_string(n++) + ".bin";
			if (sink->Append(file, memory, region.size)) {
				snprintf(buf, sizeof(buf), "region %08X %08X %s %s\n", region.start, region.size, file.c_str(), region.name.c_str());
				manifest += buf;
			}
		}
	}
	sink->Append(MANIFEST_FILE, manifest.data(), manifest.size());

	int frames = r.frames;
	rec_ = RecordingState();
	rec_.lastFrameCount = frames;
}

}  // namespace Discovery
