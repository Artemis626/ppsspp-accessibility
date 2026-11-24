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

#define _CRT_SECURE_NO_WARNINGS  // sscanf, for the standalone test build.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "GlyphLedger.h"

namespace Discovery {

namespace {

const size_t MAX_CELLS = 16384;
// Size assumed for a glyph whose size the game never asked about.
const int UNKNOWN_SIZE = 8;

int EffW(const GlyphCell &c) { return c.w ? c.w : UNKNOWN_SIZE; }
int EffH(const GlyphCell &c) { return c.h ? c.h : UNKNOWN_SIZE; }

u32 Span(const GlyphCell &c) {
	return (u32)EffH(c) * c.stride + (u32)EffW(c) * c.bpp / 8;
}

// Position of the pixel at `addr`, relative to the pixel at `origin`, in a
// buffer with the given row size. A buffer row wraps into the next, so a
// small negative column is reported as such instead of a huge positive one.
void RelativePixel(u32 origin, u32 addr, u32 stride, int bpp, int *col, int *row) {
	long long d = (long long)addr - (long long)origin;
	long long r = d / (long long)stride;
	long long rem = d - r * (long long)stride;
	if (rem < 0) {
		rem += stride;
		r--;
	}
	if (rem > (long long)stride / 2) {
		rem -= stride;
		r++;
	}
	*col = (int)(rem * 8 / bpp);
	*row = (int)r;
}

float OverlapArea(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1, float by1) {
	float w = std::min(ax1, bx1) - std::max(ax0, bx0);
	float h = std::min(ay1, by1) - std::max(ay0, by0);
	return (w > 0 && h > 0) ? w * h : 0.0f;
}

}  // namespace

void GlyphLedger::Record(const GlyphCell &cell) {
	if (cell.stride == 0 || cell.bpp == 0) {
		return;
	}
	u32 span = Span(cell);
	maxSpan_ = std::max(maxSpan_, span);

	// Forget cells this glyph was drawn over.
	u32 low = cell.addr > maxSpan_ ? cell.addr - maxSpan_ : 0;
	u32 high = cell.addr + span;
	for (auto it = cells_.lower_bound(low); it != cells_.end() && it->first < high;) {
		const GlyphCell &old = it->second;
		bool overlaps;
		if (old.stride == cell.stride && old.bpp == cell.bpp) {
			int col, row;
			RelativePixel(cell.addr, old.addr, cell.stride, cell.bpp, &col, &row);
			overlaps = OverlapArea(0, 0, (float)EffW(cell), (float)EffH(cell),
				(float)col, (float)row, (float)(col + EffW(old)), (float)(row + EffH(old))) > 0;
		} else {
			// Different layout over the same memory: compare byte ranges.
			overlaps = old.addr < high && old.addr + Span(old) > cell.addr;
		}
		it = overlaps ? cells_.erase(it) : std::next(it);
	}
	cells_[cell.addr] = cell;
	if (cells_.size() > MAX_CELLS) {
		Trim();
	}
}

void GlyphLedger::Find(u32 texAddr, u32 texStride, int bpp, float u0, float v0, float u1, float v1, std::vector<Hit> *hits) const {
	hits->clear();
	if (cells_.empty() || texStride == 0 || bpp == 0) {
		return;
	}
	if (u1 < u0) std::swap(u0, u1);
	if (v1 < v0) std::swap(v0, v1);
	if (v1 < 0 || u1 < 0) {
		return;
	}
	float queryArea = std::max((u1 - u0) * (v1 - v0), 1.0f);

	u32 firstRow = v0 > 0 ? (u32)v0 : 0;
	u32 low = texAddr + firstRow * texStride;
	low = low > maxSpan_ ? low - maxSpan_ : 0;
	low = std::max(low, texAddr);
	u32 high = texAddr + ((u32)v1 + 1) * texStride;

	// A glyph belongs to the rectangle if at least half of the glyph is inside.
	// If no glyph qualifies, the rectangle may be showing part of one glyph:
	// then take the glyph that covers at least half of the rectangle.
	Hit partial{ nullptr, 0, 0 };
	float partialScore = 0.5f;
	int newest = 0;
	for (auto it = cells_.lower_bound(low); it != cells_.end() && it->first < high; ++it) {
		const GlyphCell &c = it->second;
		if (c.stride != texStride || c.bpp != bpp) {
			continue;
		}
		u32 off = c.addr - texAddr;
		float cv = (float)(off / texStride);
		float cu = (float)((off % texStride) * 8 / bpp);
		if (c.w == 0 || c.h == 0) {
			// Size unknown: accept if the glyph's corner is inside the rectangle.
			if (cu >= u0 - 1 && cu < u1 && cv >= v0 - 1 && cv < v1) {
				hits->push_back(Hit{ &c, cu, cv });
				newest = std::max(newest, c.frame);
			}
			continue;
		}
		float area = OverlapArea(u0, v0, u1, v1, cu, cv, cu + c.w, cv + c.h);
		if (area >= (float)(c.w * c.h) * 0.5f) {
			hits->push_back(Hit{ &c, cu, cv });
			newest = std::max(newest, c.frame);
		} else if (area / queryArea > partialScore) {
			partialScore = area / queryArea;
			partial = Hit{ &c, cu, cv };
		}
	}
	if (hits->empty()) {
		if (partial.cell) {
			hits->push_back(partial);
		}
		return;
	}
	if (hits->size() > 1) {
		hits->erase(std::remove_if(hits->begin(), hits->end(), [newest](const Hit &h) {
			return h.cell->frame < newest - 2;
		}), hits->end());
	}
}

bool GlyphLedger::CoversRange(u32 addr, u32 size) const {
	auto it = cells_.lower_bound(addr);
	return it != cells_.end() && it->first - addr < size;
}

void GlyphLedger::Clear() {
	cells_.clear();
	maxSpan_ = 0;
}

void GlyphLedger::Trim() {
	// Drop the older half.
	std::vector<int> frames;
	frames.reserve(cells_.size());
	for (const auto &kv : cells_) {
		frames.push_back(kv.second.frame);
	}
	std::nth_element(frames.begin(), frames.begin() + frames.size() / 2, frames.end());
	int cutoff = frames[frames.size() / 2];
	for (auto it = cells_.begin(); it != cells_.end();) {
		it = it->second.frame < cutoff ? cells_.erase(it) : std::next(it);
	}
	if (cells_.size() > MAX_CELLS) {
		cells_.clear();  // Everything was drawn in the same frame: start over.
	}
}

std::string GlyphLedger::Serialize() const {
	std::string out = "glyph-ledger 1\n";
	char line[160];
	for (const auto &kv : cells_) {
		const GlyphCell &c = kv.second;
		// The advance is stored in 1/64 pixels, as a whole number, so the file
		// reads back the same whatever decimal separator the system uses.
		snprintf(line, sizeof(line), "%08x %u %u %u %u %x %d %d %08x\n",
			c.addr, c.stride, (unsigned)c.bpp, (unsigned)c.w, (unsigned)c.h, c.codepoint,
			(int)(c.advance * 64.0f + 0.5f), (int)c.left, c.pc);
		out += line;
	}
	return out;
}

bool GlyphLedger::Deserialize(std::string_view text) {
	Clear();
	size_t pos = text.find('\n');
	if (pos == std::string_view::npos || text.substr(0, pos) != "glyph-ledger 1") {
		return false;
	}
	pos++;
	while (pos < text.size()) {
		size_t eol = text.find('\n', pos);
		std::string line(text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos));
		pos = eol == std::string_view::npos ? text.size() : eol + 1;
		GlyphCell c;
		unsigned addr, stride, bpp, w, h, cp, pc;
		int advance, left;
		if (sscanf(line.c_str(), "%x %u %u %u %u %x %d %d %x", &addr, &stride, &bpp, &w, &h, &cp, &advance, &left, &pc) != 9) {
			continue;
		}
		c.addr = addr; c.stride = stride; c.bpp = (u8)bpp; c.w = (u16)w; c.h = (u16)h;
		c.codepoint = cp; c.advance = (float)advance / 64.0f; c.left = (s16)left; c.pc = pc;
		if (c.stride == 0 || c.bpp == 0) {
			continue;
		}
		cells_[c.addr] = c;
		maxSpan_ = std::max(maxSpan_, Span(c));
	}
	return true;
}

}  // namespace Discovery
