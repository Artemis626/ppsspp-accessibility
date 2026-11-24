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

// Numbers (Phase D3, first part). See GlyphBook::LearnDigits in GlyphCipher.h.
//
// A number on screen is two or three symbols: far too short for the pattern
// search that reads text. But digits have two properties text does not:
//
//   1. Games keep them in a strip: ten images of one size, side by side in
//      the texture, in the order 0 to 9. So the position of a symbol in its
//      strip says which digit it is, up to one unknown: which digit the
//      strip starts with (we may not have seen the 0 yet).
//   2. The numbers shown together (hit points, their maximum, ...) are kept
//      together in memory, as plain binary values a few bytes apart.
//
// So: guess the strip's first digit, work out the numbers that guess puts on
// screen, and look in memory for a place where several of them stand
// together. Only one guess finds such a place. That confirms the digits and,
// at the same stroke, finds the record the numbers come from.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>

#include "GlyphCipher.h"

namespace Discovery {

namespace {

// Numbers are looked for within this many bytes either side of one of them.
const int WINDOW_BYTES = 32;
// A value found in more places than this says nothing (10, 16, 100...).
const size_t MAX_HITS = 8000;
// What counts as "several numbers together": this many numbers of three or
// more digits, with this many digits between them. Three numbers of three
// digits within a few bytes of each other do not happen by chance.
const int MIN_INSTANCES = 3;
const int MIN_DIGITS = 8;
// A strip takes part in the answer if this many of its digits were found.
const int MIN_STRIP_DIGITS = 3;
const size_t MAX_JOINT = 400;
const size_t MAX_RECORDS = 16;

struct Group {
	u32 tex = 0;
	u16 texW = 0, texH = 0, w = 0, h = 0, v0 = 0, u0 = 0;
	float step = 0;
	int maxCol = 0;
	float advance = 0;                        // From one digit to the next, as a share of the height.
	std::map<int, int> colOf;                 // Symbol number -> place in the strip.
	std::vector<std::vector<int>> stretches;  // Each number on screen, as places in the strip.
	int Hypotheses() const { return 10 - maxCol; }
};

struct Instance {
	u32 value;
	int digits;
	int group;
};

int DigitCount(u32 v) {
	int n = 1;
	while (v >= 10) {
		v /= 10;
		n++;
	}
	return n;
}

// The numbers a strip puts on screen if its first place is the digit `first`.
void Values(const Group &g, int group, int first, std::vector<Instance> *out) {
	for (const std::vector<int> &stretch : g.stretches) {
		if (stretch.size() > 9) {
			continue;
		}
		u32 v = 0;
		bool allAlike = true;
		for (int col : stretch) {
			v = v * 10 + (u32)(col + first);
			allAlike = allAlike && col == stretch[0];
		}
		// Only numbers that say something are evidence. Seen in Tactics Ogre:
		// a wrong first digit turned the padding "000" into 111 and 444, and
		// those, with a few two-digit numbers, were all found close together
		// in a table of small values. So: three digits or more, not all alike.
		if (v >= 100 && v <= 0xFFFF && !allAlike) {
			out->push_back(Instance{ v, DigitCount(v), group });
		}
	}
}

size_t CountInRange(const std::vector<u32> &sorted, u32 lo, u32 hi) {
	return std::upper_bound(sorted.begin(), sorted.end(), hi) - std::lower_bound(sorted.begin(), sorted.end(), lo);
}

}  // namespace

int GlyphBook::StripDigit(const DigitStrip &strip, const GlyphSymbol &s) const {
	const GlyphKey &k = s.key;
	if (k.tex != strip.tex || k.texW != strip.texW || k.texH != strip.texH || k.v0 != strip.v0
		|| k.u1 - k.u0 != strip.w || k.v1 - k.v0 != strip.h) {
		return -1;
	}
	float d = (float)k.u0 - (float)strip.u0;
	int col = (int)std::lround(d / strip.step);
	if (std::abs(d - col * strip.step) > strip.step * 0.15f + 1.0f) {
		return -1;
	}
	int digit = col + strip.first;
	return digit >= 0 && digit <= 9 ? digit : -1;
}

bool GlyphBook::LearnDigits(const std::vector<ShortRow> &rows, const u8 *memory, size_t size, u32 base) {
	// 1. Possible strips: unknown symbols of one size on one row of a texture.
	typedef std::tuple<u32, u16, u16, u16, u16, u16> StripKey;
	std::map<StripKey, std::set<int>> bySize;
	for (const ShortRow &row : rows) {
		for (int id : row.symbols) {
			if (id < 0 || symbols_[id].code >= 0) {
				continue;
			}
			const GlyphKey &k = symbols_[id].key;
			bySize[StripKey(k.tex, k.texW, k.texH, (u16)(k.u1 - k.u0), (u16)(k.v1 - k.v0), k.v0)].insert(id);
		}
	}
	std::vector<Group> groups;
	for (const auto &kv : bySize) {
		if (kv.second.size() < 3 || kv.second.size() > 10) {
			continue;
		}
		Group g;
		std::tie(g.tex, g.texW, g.texH, g.w, g.h, g.v0) = kv.first;
		std::vector<int> us;
		for (int id : kv.second) {
			us.push_back(symbols_[id].key.u0);
		}
		std::sort(us.begin(), us.end());
		int closest = 1 << 20;
		for (size_t i = 1; i < us.size(); i++) {
			closest = std::min(closest, us[i] - us[i - 1]);
		}
		// Neighbours in a strip touch or nearly touch. If the closest two we
		// have seen are further apart, we have not seen two neighbours yet,
		// and the step between places is not known.
		if (closest < g.w || closest > g.w + std::max(3, g.w / 3)) {
			continue;
		}
		g.u0 = (u16)us.front();
		int lastCol = (int)std::lround((float)(us.back() - us.front()) / (float)closest);
		if (lastCol < 1 || lastCol > 9) {
			continue;
		}
		g.step = (float)(us.back() - us.front()) / (float)lastCol;
		bool fits = true;
		std::set<int> used;
		for (int id : kv.second) {
			float d = (float)(symbols_[id].key.u0 - g.u0);
			int col = (int)std::lround(d / g.step);
			if (std::abs(d - col * g.step) > g.step * 0.15f + 1.0f || !used.insert(col).second) {
				fits = false;
				break;
			}
			g.colOf[id] = col;
			g.maxCol = std::max(g.maxCol, col);
		}
		if (fits) {
			groups.push_back(g);
		}
	}
	if (groups.empty()) {
		return false;
	}

	// 2. The numbers on screen: neighbours of one strip at the strip's usual
	// step. A wider step starts a new number ("35 40").
	for (Group &g : groups) {
		float usual = 1e9f;
		for (const ShortRow &row : rows) {
			for (size_t i = 1; i < row.symbols.size(); i++) {
				if (g.colOf.count(row.symbols[i - 1]) && g.colOf.count(row.symbols[i]) && row.x[i] > row.x[i - 1]) {
					if (row.x[i] - row.x[i - 1] < usual) {
						usual = row.x[i] - row.x[i - 1];
						g.advance = usual / std::max(row.height[i - 1], 1.0f);
					}
				}
			}
		}
		for (const ShortRow &row : rows) {
			std::vector<int> stretch;
			for (size_t i = 0; i < row.symbols.size(); i++) {
				auto col = g.colOf.find(row.symbols[i]);
				bool joins = col != g.colOf.end() && !stretch.empty() && row.x[i] > row.x[i - 1]
					&& row.x[i] - row.x[i - 1] <= usual * 1.2f + 1.0f;
				if (!joins && !stretch.empty()) {
					g.stretches.push_back(stretch);
					stretch.clear();
				}
				if (col != g.colOf.end()) {
					stretch.push_back(col->second);
				}
			}
			if (!stretch.empty()) {
				g.stretches.push_back(stretch);
			}
		}
	}

	// 3. Every value any guess could put on screen, found in memory in one pass.
	std::vector<int> slot(0x10000, -1);
	std::vector<std::vector<u32>> hits;
	for (size_t gi = 0; gi < groups.size(); gi++) {
		for (int first = 0; first < groups[gi].Hypotheses(); first++) {
			std::vector<Instance> values;
			Values(groups[gi], (int)gi, first, &values);
			for (const Instance &v : values) {
				if (slot[v.value] < 0) {
					slot[v.value] = (int)hits.size();
					hits.emplace_back();
				}
			}
		}
	}
	if (hits.empty()) {
		return false;
	}
	for (size_t i = 0; i + 1 < size; i += 2) {
		int s = slot[(u32)memory[i] | ((u32)memory[i + 1] << 8)];
		if (s >= 0 && hits[s].size() <= MAX_HITS) {
			hits[s].push_back((u32)i);
		}
	}

	// 4. Per texture: the strips' guesses taken together.
	std::map<u32, std::vector<int>> byTexture;
	for (size_t gi = 0; gi < groups.size(); gi++) {
		byTexture[Family(groups[gi].tex)].push_back((int)gi);
	}
	bool learned = false;
	for (auto &texture : byTexture) {
		std::vector<int> &members = texture.second;
		std::sort(members.begin(), members.end(), [&](int a, int b) {
			return groups[a].stretches.size() > groups[b].stretches.size();
		});
		auto combinations = [&]() {
			size_t n = 1;
			for (int gi : members) {
				n *= (size_t)groups[gi].Hypotheses();
			}
			return n;
		};
		while (members.size() > 3 || (members.size() > 1 && combinations() > MAX_JOINT)) {
			members.pop_back();
		}

		struct Scored {
			std::vector<int> first;       // The guess, per member.
			int score = 0;
			std::vector<u32> windows;     // Where the best score was reached (the anchor's offset).
		};
		std::vector<Scored> all;
		std::vector<int> guess(members.size(), 0);
		for (;;) {
			Scored sc;
			sc.first = guess;
			// The different values of this guess, each with the numbers showing it.
			std::map<u32, std::vector<Instance>> wanted;
			for (size_t m = 0; m < members.size(); m++) {
				std::vector<Instance> values;
				Values(groups[members[m]], (int)m, guess[m], &values);
				for (const Instance &v : values) {
					const std::vector<u32> &h = hits[slot[v.value]];
					if (!h.empty() && h.size() <= MAX_HITS) {
						wanted[v.value].push_back(v);
					}
				}
			}
			size_t total = 0;
			for (const auto &w : wanted) {
				total += w.second.size();
			}
			if (total >= (size_t)MIN_INSTANCES) {
				// Start from the rarest values: few places to look at.
				std::vector<u32> anchors;
				for (const auto &w : wanted) {
					anchors.push_back(w.first);
				}
				std::sort(anchors.begin(), anchors.end(), [&](u32 a, u32 b) {
					return hits[slot[a]].size() < hits[slot[b]].size();
				});
				anchors.resize(std::min(anchors.size(), (size_t)3));
				for (u32 anchor : anchors) {
					for (u32 p : hits[slot[anchor]]) {
						u32 lo = p > (u32)WINDOW_BYTES ? p - WINDOW_BYTES : 0, hi = p + WINDOW_BYTES;
						int score = 0;
						for (const auto &w : wanted) {
							score += (int)std::min(w.second.size(), CountInRange(hits[slot[w.first]], lo, hi));
						}
						if (score > sc.score) {
							sc.score = score;
							sc.windows.clear();
						}
						if (score == sc.score && sc.windows.size() < 64) {
							sc.windows.push_back(p);
						}
					}
				}
			}
			all.push_back(sc);
			size_t m = 0;
			for (; m < members.size(); m++) {
				if (++guess[m] < groups[members[m]].Hypotheses()) {
					break;
				}
				guess[m] = 0;
			}
			if (m == members.size()) {
				break;
			}
		}

		const Scored *best = nullptr;
		for (const Scored &sc : all) {
			if (!best || sc.score > best->score) {
				best = &sc;
			}
		}
		if (!best || best->score < MIN_INSTANCES) {
			continue;
		}
		// Which strips does the best place speak for, and how many digits in all?
		std::vector<int> stripDigits(members.size(), 0);
		int digits = 0;
		u32 at = best->windows.front();
		u32 lo = at > (u32)WINDOW_BYTES ? at - WINDOW_BYTES : 0, hi = at + WINDOW_BYTES;
		for (size_t m = 0; m < members.size(); m++) {
			std::vector<Instance> values;
			Values(groups[members[m]], (int)m, best->first[m], &values);
			std::map<u32, int> counted;
			for (const Instance &v : values) {
				const std::vector<u32> &h = hits[slot[v.value]];
				if (h.empty() || h.size() > MAX_HITS) {
					continue;
				}
				if (counted[v.value]++ < (int)CountInRange(h, lo, hi)) {
					stripDigits[m] += v.digits;
					digits += v.digits;
				}
			}
		}
		if (digits < MIN_DIGITS) {
			continue;
		}
		// No other guess about these strips may do as well.
		bool only = true;
		for (const Scored &sc : all) {
			if (&sc == best || sc.score < best->score) {
				continue;
			}
			for (size_t m = 0; m < members.size(); m++) {
				if (stripDigits[m] >= MIN_STRIP_DIGITS && sc.first[m] != best->first[m]) {
					only = false;
				}
			}
		}
		if (!only) {
			continue;
		}

		for (size_t m = 0; m < members.size(); m++) {
			if (stripDigits[m] < MIN_STRIP_DIGITS) {
				continue;
			}
			const Group &g = groups[members[m]];
			DigitStrip strip;
			strip.tex = g.tex;
			strip.texW = g.texW;
			strip.texH = g.texH;
			strip.w = g.w;
			strip.h = g.h;
			strip.u0 = g.u0;
			strip.v0 = g.v0;
			strip.step = g.step;
			strip.first = best->first[m];
			strip.advance = g.advance;
			strips_.push_back(strip);
			for (const auto &kv : g.colOf) {
				SetCode(kv.first, '0' + kv.second + strip.first, GlyphOrigin::Digits);
			}
			learned = true;
		}
		// The places themselves: where these numbers live.
		std::vector<u32> wantedValues;
		for (size_t m = 0; m < members.size(); m++) {
			std::vector<Instance> values;
			Values(groups[members[m]], (int)m, best->first[m], &values);
			for (const Instance &v : values) {
				wantedValues.push_back(v.value);
			}
		}
		for (u32 p : best->windows) {
			u32 wlo = p > (u32)WINDOW_BYTES ? p - WINDOW_BYTES : 0, whi = p + WINDOW_BYTES;
			u32 first = 0xFFFFFFFF, last = 0;
			for (u32 v : wantedValues) {
				const std::vector<u32> &h = hits[slot[v]];
				for (auto it = std::lower_bound(h.begin(), h.end(), wlo); it != h.end() && *it <= whi; ++it) {
					first = std::min(first, *it);
					last = std::max(last, *it);
				}
			}
			bool have = false;
			for (const NumberRecord &r : numberRecords_) {
				have = have || r.addr == base + first;
			}
			if (!have && first != 0xFFFFFFFF && numberRecords_.size() < MAX_RECORDS) {
				numberRecords_.push_back(NumberRecord{ base + first, last - first + 2 });
			}
		}
	}
	// The tightest places first: a record is compact, a chance find is not.
	std::stable_sort(numberRecords_.begin(), numberRecords_.end(), [](const NumberRecord &a, const NumberRecord &b) {
		return a.size < b.size;
	});
	return learned;
}

float GlyphBook::DigitAdvance(int id) const {
	if (!IsDigit(id)) {
		return 0;
	}
	for (const DigitStrip &strip : strips_) {
		if (StripDigit(strip, symbols_[id]) >= 0) {
			return strip.advance;
		}
	}
	return 0;
}

std::string GlyphBook::DescribeDigits() const {
	std::string out;
	char line[240];
	for (const DigitStrip &s : strips_) {
		snprintf(line, sizeof(line), "  digit strip in texture 0x%08X: images %d by %d at row %d, starting at column %d with the digit %d, one every %.1f\n",
			s.tex, s.w, s.h, s.v0, s.u0, s.first, s.step);
		out += line;
	}
	return out;
}

}  // namespace Discovery
