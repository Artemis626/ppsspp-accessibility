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
#include <cmath>
#include <map>

#include "TextAssembler.h"
#include "TextUtil.h"

namespace Discovery {

namespace {

// More glyphs than this in one frame is not text we can sensibly assemble
// (and the duplicate check below is quadratic).
const size_t MAX_GLYPHS = 3000;

struct Line {
	std::vector<int> members;  // Indexes into the glyph list, left to right.
	u32 target = 0;
	float right = 0;           // Right edge of the last glyph.
	// Vertical band of the line, taken from its tall glyphs only, so a comma
	// or an apostrophe does not stretch it.
	float top = 0, bottom = 0;
	float tallest = 0;
};

float VerticalOverlap(float a0, float a1, float b0, float b1) {
	return std::min(a1, b1) - std::max(a0, b0);
}

// Does a glyph belong on a line? Small glyphs (a full stop, an apostrophe)
// sit at the bottom or top of the line, so the comparison widens the taller
// of the two by 30% before asking whether the smaller one is mostly inside.
bool SameLine(const Line &line, const RectF &g) {
	float lineH = line.bottom - line.top;
	float glyphH = g.Height();
	if (glyphH <= lineH) {
		float grow = lineH * 0.3f;
		return VerticalOverlap(line.top - grow, line.bottom + grow, g.y0, g.y1) >= glyphH * 0.5f;
	}
	float grow = glyphH * 0.3f;
	return VerticalOverlap(g.y0 - grow, g.y1 + grow, line.top, line.bottom) >= lineH * 0.5f;
}

void RecomputeBand(Line &line, const std::vector<PlacedGlyph> &glyphs) {
	line.tallest = 0;
	for (int i : line.members) {
		line.tallest = std::max(line.tallest, glyphs[i].rect.Height());
	}
	bool first = true;
	for (int i : line.members) {
		const RectF &r = glyphs[i].rect;
		if (r.Height() < line.tallest * 0.6f) {
			continue;
		}
		line.top = first ? r.y0 : std::min(line.top, r.y0);
		line.bottom = first ? r.y1 : std::max(line.bottom, r.y1);
		first = false;
	}
}

// Removes a glyph drawn twice in (almost) the same place: shadows and outlines.
void RemoveDuplicates(std::vector<PlacedGlyph> &glyphs) {
	std::sort(glyphs.begin(), glyphs.end(), [](const PlacedGlyph &a, const PlacedGlyph &b) { return a.order < b.order; });
	std::vector<PlacedGlyph> kept;
	kept.reserve(glyphs.size());
	for (const PlacedGlyph &g : glyphs) {
		bool replaced = false;
		for (PlacedGlyph &k : kept) {
			if (k.codepoint != g.codepoint || k.target != g.target) {
				continue;
			}
			// A shadow or outline is the same glyph moved by a pixel or two.
			// Compared by offset, not by overlap, for two reasons: a full stop
			// is so small that its shadow overlaps less than half of it; and a
			// game that draws fixed-size cells closer together than they are
			// wide makes a real double letter ("ll") overlap a great deal.
			// A double letter never moves down, and moves right by more than a pixel.
			float dx = std::abs(k.rect.x0 - g.rect.x0), dy = std::abs(k.rect.y0 - g.rect.y0);
			bool nudged = dx <= 2.0f && dy <= 2.0f && (dy > 0.0f || dx <= 1.0f)
				&& std::abs(k.rect.Width() - g.rect.Width()) <= 1.0f && std::abs(k.rect.Height() - g.rect.Height()) <= 1.0f;
			if (nudged) {
				k = g;  // The later draw is the one on top.
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			kept.push_back(g);
		}
	}
	glyphs.swap(kept);
}

}  // namespace

std::vector<std::vector<PlacedGlyph>> AssembleLines(std::vector<PlacedGlyph> glyphs) {
	std::vector<std::vector<PlacedGlyph>> result;
	// Leave out glyphs with no size on screen.
	glyphs.erase(std::remove_if(glyphs.begin(), glyphs.end(), [](const PlacedGlyph &g) { return g.rect.Empty(); }), glyphs.end());
	if (glyphs.empty() || glyphs.size() > MAX_GLYPHS) {
		return result;
	}
	RemoveDuplicates(glyphs);
	std::sort(glyphs.begin(), glyphs.end(), [](const PlacedGlyph &a, const PlacedGlyph &b) {
		return a.rect.x0 != b.rect.x0 ? a.rect.x0 < b.rect.x0 : a.order < b.order;
	});

	// Going left to right, give each glyph to the line it continues, or start
	// a new line. A gap wider than 2.5 line heights ends a line, which is what
	// separates the columns of a table into their own runs.
	std::vector<Line> lines;
	for (int i = 0; i < (int)glyphs.size(); i++) {
		const RectF &r = glyphs[i].rect;
		int bestLine = -1;
		float bestDistance = 0;
		for (int l = 0; l < (int)lines.size(); l++) {
			const Line &line = lines[l];
			if (line.target != glyphs[i].target) {
				continue;
			}
			float lineH = std::max(line.bottom - line.top, 8.0f);
			float gap = r.x0 - line.right;
			// The lower limit allows fixed-size cells drawn overlapping (a
			// narrow letter in a wide cell, followed closely by the next).
			if (gap < -1.1f * lineH - 1.0f || gap > 2.5f * lineH) {
				continue;
			}
			if (!SameLine(line, r)) {
				continue;
			}
			float distance = std::abs(r.CenterY() - (line.top + line.bottom) * 0.5f);
			if (bestLine < 0 || distance < bestDistance) {
				bestLine = l;
				bestDistance = distance;
			}
		}
		if (bestLine < 0) {
			Line line;
			line.target = glyphs[i].target;
			lines.push_back(line);
			bestLine = (int)lines.size() - 1;
		}
		Line &line = lines[bestLine];
		line.members.push_back(i);
		line.right = std::max(line.members.size() == 1 ? r.x1 : line.right, r.x1);
		if (line.members.size() == 1 || r.Height() > line.tallest) {
			RecomputeBand(line, glyphs);
		} else if (r.Height() >= line.tallest * 0.6f) {
			line.top = std::min(line.top, r.y0);
			line.bottom = std::max(line.bottom, r.y1);
		}
	}

	for (const Line &line : lines) {
		std::vector<PlacedGlyph> members;
		members.reserve(line.members.size());
		for (int i : line.members) {
			members.push_back(glyphs[i]);
		}
		result.push_back(std::move(members));
	}
	return result;
}

std::vector<std::vector<PlacedGlyph>> AssembleLinesInDrawOrder(std::vector<PlacedGlyph> glyphs) {
	std::vector<std::vector<PlacedGlyph>> result;
	glyphs.erase(std::remove_if(glyphs.begin(), glyphs.end(), [](const PlacedGlyph &g) { return g.rect.Empty(); }), glyphs.end());
	if (glyphs.empty() || glyphs.size() > MAX_GLYPHS) {
		return result;
	}
	RemoveDuplicates(glyphs);
	std::sort(glyphs.begin(), glyphs.end(), [](const PlacedGlyph &a, const PlacedGlyph &b) { return a.order < b.order; });
	for (const PlacedGlyph &g : glyphs) {
		bool continues = false;
		if (!result.empty()) {
			const PlacedGlyph &prev = result.back().back();
			float h = std::max(std::max(prev.rect.Height(), g.rect.Height()), 1.0f);
			continues = g.target == prev.target
				&& std::abs(g.rect.CenterY() - prev.rect.CenterY()) <= h * 0.5f
				&& g.rect.x0 > prev.rect.x0
				&& g.rect.x0 - prev.rect.x1 <= 2.5f * h;
		}
		if (!continues) {
			result.push_back(std::vector<PlacedGlyph>());
		}
		result.back().push_back(g);
	}
	return result;
}

std::vector<TextRun> AssembleRuns(std::vector<PlacedGlyph> glyphs) {
	std::vector<TextRun> runs;
	for (const std::vector<PlacedGlyph> &line : AssembleLines(std::move(glyphs))) {
		TextRun run;
		run.source = TextSource::SystemFont;
		run.target = line[0].target;
		run.hasRect = true;
		float lineH = 1.0f;
		for (const PlacedGlyph &g : line) {
			lineH = std::max(lineH, g.rect.Height());
		}
		std::map<u32, int> pcVotes;
		bool allMeasured = true;
		for (size_t k = 0; k < line.size(); k++) {
			const PlacedGlyph &g = line[k];
			if (k == 0) {
				run.rect = g.rect;
				run.bufferAddr = g.bufferAddr;
			} else {
				const PlacedGlyph &prev = line[k - 1];
				bool space;
				if (prev.hasMetrics && g.hasMetrics && prev.advance > 0) {
					// The font says where the next glyph's pen should be. If
					// the pen is clearly further on, the game skipped a space.
					space = g.penX - (prev.penX + prev.advance) > lineH * 0.15f;
				} else {
					space = g.rect.x0 - prev.rect.x1 > lineH * 0.3f;
				}
				if (space) {
					run.text += ' ';
				}
				run.rect.x0 = std::min(run.rect.x0, g.rect.x0);
				run.rect.y0 = std::min(run.rect.y0, g.rect.y0);
				run.rect.x1 = std::max(run.rect.x1, g.rect.x1);
				run.rect.y1 = std::max(run.rect.y1, g.rect.y1);
			}
			AppendUtf8(run.text, g.codepoint);
			allMeasured = allMeasured && g.hasMetrics;
			if (g.pc) {
				pcVotes[g.pc]++;
			}
		}
		int bestVotes = 0;
		for (const auto &kv : pcVotes) {
			if (kv.second > bestVotes) {
				bestVotes = kv.second;
				run.codePc = kv.first;
			}
		}
		// Starting values, to be replaced by rates measured with the harness.
		run.confidence = allMeasured ? 0.95f : 0.8f;
		runs.push_back(run);
	}

	// Reading order. Runs whose tops are within half a line count as the same row.
	std::sort(runs.begin(), runs.end(), [](const TextRun &a, const TextRun &b) {
		if (a.target != b.target) {
			return a.target < b.target;
		}
		float h = std::max(std::min(a.rect.Height(), b.rect.Height()), 4.0f);
		if (std::abs(a.rect.CenterY() - b.rect.CenterY()) > h * 0.5f) {
			return a.rect.CenterY() < b.rect.CenterY();
		}
		return a.rect.x0 < b.rect.x0;
	});
	return runs;
}

}  // namespace Discovery
