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
#include <cstdio>
#include <set>

#include "Selection.h"

namespace Discovery {

namespace {

// A mark is a few images at most, and not large.
const int MARK_MAX_IMAGES = 6;
const float MARK_MAX_WIDTH = 320;
const float MARK_MAX_HEIGHT = 40;
// It must rest this long by a text for the stay to count (a figure walking
// across a text does not rest).
const int REST_FRAMES = 8;
// A text's place or colour must hold this long to count as its new one (so
// a slide or a fade is one change, not many).
const int HOLD_FRAMES = 3;
// The two texts of a selection moving change within this many frames of each other.
const int SWAP_FRAMES = 20;
const float MIN_SHIFT = 2;
const float MAX_SHIFT = 40;
const float MIN_CONFIDENCE = 0.6f;

u64 KeyOf(const TextRun &run) {
	u64 hash = 1469598103934665603ull;
	for (unsigned char c : run.text) {
		hash = (hash ^ c) * 1099511628211ull;
	}
	return (hash ^ (u64)(u32)(int)std::lround(run.rect.y0)) * 1099511628211ull;
}

bool Usable(const TextRun &run) {
	return run.hasRect && run.source == TextSource::GameFont && run.confidence >= MIN_CONFIDENCE && !run.text.empty();
}

}  // namespace

void SelectionFinder::Clear() {
	marks_.clear();
	seen_.clear();
	changes_.clear();
	swapKey_ = 0;
	swapFrame_ = -1;
	swaps_ = 0;
	current_ = Selection();
}

void SelectionFinder::Update(int frame, std::vector<TextRun> &runs, const std::vector<SpriteDraw> &sprites) {
	Selection found;
	int foundRun = -1;

	// ---- a mark ----
	struct Box {
		RectF r;
		int n = 0;
	};
	std::map<u32, Box> boxes;
	for (const SpriteDraw &d : sprites) {
		Box &b = boxes[d.texAddr];
		if (b.n++ == 0) {
			b.r = d.screen;
		} else {
			b.r.x0 = std::min(b.r.x0, d.screen.x0);
			b.r.y0 = std::min(b.r.y0, d.screen.y0);
			b.r.x1 = std::max(b.r.x1, d.screen.x1);
			b.r.y1 = std::max(b.r.y1, d.screen.y1);
		}
	}
	std::set<u32> fonts;
	for (const TextRun &run : runs) {
		fonts.insert(run.bufferAddr);
	}
	for (const auto &kv : boxes) {
		const Box &b = kv.second;
		if (b.n > MARK_MAX_IMAGES || b.r.Width() > MARK_MAX_WIDTH || b.r.Height() > MARK_MAX_HEIGHT
			|| b.r.Width() < 3 || b.r.Height() < 2 || fonts.count(kv.first)) {
			continue;
		}
		// The text it stands by: on its height, and the nearest sideways.
		float cy = b.r.CenterY();
		int by = -1;
		float nearest = 1e9f;
		for (size_t i = 0; i < runs.size(); i++) {
			const TextRun &run = runs[i];
			if (!Usable(run) || cy < run.rect.y0 - 2 || cy > run.rect.y1 + 10) {
				continue;
			}
			float gap = std::max(0.0f, std::max(run.rect.x0 - b.r.x1, b.r.x0 - run.rect.x1));
			if (gap < nearest) {
				nearest = gap;
				by = (int)i;
			}
		}
		if (by < 0) {
			continue;
		}
		Mark &m = marks_[kv.first];
		u64 key = KeyOf(runs[by]);
		float rel = cy - runs[by].rect.y0;
		m.lastSeen = frame;
		// Resting means staying put: an image passing over a text (a figure
		// walking by) stands by it for a while too, but never stops.
		bool travelled = std::abs(b.r.CenterX() - m.atX) > 3 || std::abs(cy - m.atY) > 3;
		if (m.key != key || travelled) {
			m.key = key;
			m.atX = b.r.CenterX();
			m.atY = cy;
			m.since = frame;
			m.rel = rel;
			m.w = b.r.Width();
			m.h = b.r.Height();
		} else if (frame - m.since == REST_FRAMES) {
			// It has come to rest. If it rested by another text before, and
			// stands by this one just as it stood by that one, it has moved
			// as a mark moves.
			// (It must itself have moved: a text that changes beside a
			// still image, like a help line beside an icon, is no move.)
			bool moved = std::abs(b.r.CenterX() - m.restedX) >= 4 || std::abs(cy - m.restedY) >= 4;
			if (m.restedKey != 0 && m.restedKey != key && moved && std::abs(m.restedRel - m.rel) <= 3
				&& std::abs(m.restedW - m.w) <= 2 && std::abs(m.restedH - m.h) <= 2) {
				m.moves++;
			}
			m.restedX = b.r.CenterX();
			m.restedY = cy;
			m.restedKey = key;
			m.restedRel = m.rel;
			m.restedW = m.w;
			m.restedH = m.h;
		}
		if (m.moves > 0 && (!found.known || m.since > found.since)) {
			found.known = true;
			found.how = "mark";
			found.markTex = kv.first;
			found.since = m.since;
			foundRun = by;
		}
	}
	if (marks_.size() > 2000) {
		marks_.clear();
	}

	// ---- a place or a colour ----
	for (const TextRun &run : runs) {
		if (!Usable(run)) {
			continue;
		}
		u64 key = KeyOf(run);
		Seen &s = seen_[key];
		bool continued = s.started && frame - s.lastSeen <= 2;
		if (!continued) {
			s = Seen();
			s.started = true;
			s.x = s.stableX = run.rect.x0;
			s.color = s.stableColor = run.color;
			s.xSince = s.colorSince = frame;
		}
		s.lastSeen = frame;
		if (std::abs(run.rect.x0 - s.x) > 0.5f) {
			s.x = run.rect.x0;
			s.xSince = frame;
		} else if (frame - s.xSince == HOLD_FRAMES && std::abs(s.x - s.stableX) >= MIN_SHIFT) {
			if (std::abs(s.x - s.stableX) <= MAX_SHIFT) {
				Change c;
				c.frame = frame;
				c.key = key;
				c.dx = s.x - s.stableX;
				changes_.push_back(c);
			}
			s.stableX = s.x;
		}
		if (run.color != s.color) {
			s.color = run.color;
			s.colorSince = frame;
		} else if (frame - s.colorSince == HOLD_FRAMES && s.color != s.stableColor) {
			Change c;
			c.frame = frame;
			c.key = key;
			c.colour = true;
			c.from = s.stableColor;
			c.to = s.color;
			changes_.push_back(c);
			s.stableColor = s.color;
		}
	}
	changes_.erase(std::remove_if(changes_.begin(), changes_.end(), [&](const Change &c) {
		return frame - c.frame > SWAP_FRAMES;
	}), changes_.end());
	if (seen_.size() > 4000) {
		seen_.clear();
	}
	// Two texts that changed places, or colours, with each other.
	for (size_t i = 0; i < changes_.size(); i++) {
		for (size_t j = i + 1; j < changes_.size(); j++) {
			Change &a = changes_[i], &b = changes_[j];
			if (a.used || b.used || a.key == b.key || a.colour != b.colour) {
				continue;
			}
			bool swap = a.colour ? (a.from == b.to && a.to == b.from) : std::abs(a.dx + b.dx) <= 1.5f;
			if (!swap) {
				continue;
			}
			a.used = b.used = true;
			// Which of the two is now the selected one? The one that is
			// now unlike the other texts of its size.
			const TextRun *ra = nullptr, *rb = nullptr;
			for (const TextRun &run : runs) {
				if (Usable(run)) {
					u64 key = KeyOf(run);
					ra = key == a.key ? &run : ra;
					rb = key == b.key ? &run : rb;
				}
			}
			if (!ra || !rb) {
				continue;
			}
			int likeA = 0, likeB = 0;
			for (const TextRun &run : runs) {
				if (!Usable(run) || &run == ra || &run == rb || std::abs(run.rect.Height() - ra->rect.Height()) > 1.5f) {
					continue;
				}
				if (a.colour) {
					likeA += run.color == ra->color ? 1 : 0;
					likeB += run.color == rb->color ? 1 : 0;
				} else {
					likeA += std::abs(run.rect.x0 - ra->rect.x0) <= 1 ? 1 : 0;
					likeB += std::abs(run.rect.x0 - rb->rect.x0) <= 1 ? 1 : 0;
				}
			}
			if (likeA == likeB) {
				continue;  // Two items only: no telling which way round it is.
			}
			swapKey_ = likeA < likeB ? a.key : b.key;
			swapFrame_ = frame;
			swapHow_ = a.colour ? "colour" : "place";
			swaps_++;
		}
	}
	if (swapKey_ != 0 && (!found.known || swapFrame_ > found.since)) {
		for (size_t i = 0; i < runs.size(); i++) {
			if (Usable(runs[i]) && KeyOf(runs[i]) == swapKey_) {
				found.known = true;
				found.how = swapHow_;
				found.markTex = 0;
				found.since = swapFrame_;
				foundRun = (int)i;
			}
		}
	}

	if (found.known && foundRun >= 0) {
		TextRun &run = runs[foundRun];
		run.selected = true;
		found.rect = run.rect;
		found.text = run.text;
		if (found.markTex != 0) {
			// A mark may stand by a value: then the row's first text names it.
			int label = foundRun;
			for (size_t i = 0; i < runs.size(); i++) {
				if (Usable(runs[i]) && std::abs(runs[i].rect.y0 - run.rect.y0) <= 4 && runs[i].rect.x0 < runs[label].rect.x0) {
					label = (int)i;
				}
			}
			if (label != foundRun) {
				found.text = runs[label].text + ": " + run.text;
			}
		}
		current_ = found;
	} else {
		current_ = Selection();
	}
}

std::string SelectionFinder::Describe() const {
	char line[400];
	if (!current_.known) {
		return "  not known (it shows once the selection has been moved: the layer learns how this game marks it by seeing it move)\n";
	}
	if (current_.markTex != 0) {
		snprintf(line, sizeof(line), "  \"%s\" (x %.0f, y %.0f), shown by a mark drawn from texture 0x%08X\n",
			current_.text.c_str(), current_.rect.x0, current_.rect.y0, current_.markTex);
	} else {
		snprintf(line, sizeof(line), "  \"%s\" (x %.0f, y %.0f), shown by its %s\n",
			current_.text.c_str(), current_.rect.x0, current_.rect.y0, current_.how);
	}
	return line;
}

}  // namespace Discovery
