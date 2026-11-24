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

#include "ScreenText.h"

namespace Discovery {

namespace {

// A run must be absent this many frames before it counts as gone. Games skip
// a frame of drawing now and then; without this, text would flicker.
const int GRACE_FRAMES = 3;
const size_t MAX_PENDING_CHANGES = 512;

float Distance(const TextRun &a, const TextRun &b) {
	if (!a.hasRect || !b.hasRect) {
		return 0;
	}
	return std::abs(a.rect.x0 - b.rect.x0) + std::abs(a.rect.y0 - b.rect.y0);
}

// Same place on screen: the rectangles share a good part of the smaller one.
bool SamePlace(const TextRun &a, const TextRun &b) {
	if (!a.hasRect || !b.hasRect) {
		return false;
	}
	float w = std::min(a.rect.x1, b.rect.x1) - std::max(a.rect.x0, b.rect.x0);
	float h = std::min(a.rect.y1, b.rect.y1) - std::max(a.rect.y0, b.rect.y0);
	if (w <= 0 || h <= 0) {
		return false;
	}
	float smaller = std::min(a.rect.Width() * a.rect.Height(), b.rect.Width() * b.rect.Height());
	return w * h >= smaller * 0.3f;
}

}  // namespace

void ScreenText::Update(int frame, std::vector<TextRun> runs) {
	// Candidates to match against: last frame's runs, then the ones in grace.
	std::vector<TextRun> old = std::move(current_);
	old.insert(old.end(), missing_.begin(), missing_.end());
	missing_.clear();
	std::vector<bool> oldUsed(old.size(), false);
	std::vector<bool> matched(runs.size(), false);

	// 1. Same text: the same run, wherever it moved to. With several equal
	//    texts on screen ("HP" twice), the nearest one is taken.
	for (size_t i = 0; i < runs.size(); i++) {
		int best = -1;
		float bestDistance = 0;
		for (size_t j = 0; j < old.size(); j++) {
			if (oldUsed[j] || old[j].source != runs[i].source || old[j].text != runs[i].text) {
				continue;
			}
			float d = Distance(runs[i], old[j]);
			if (best < 0 || d < bestDistance) {
				best = (int)j;
				bestDistance = d;
			}
		}
		if (best >= 0) {
			oldUsed[best] = true;
			matched[i] = true;
			runs[i].id = old[best].id;
			runs[i].firstFrame = old[best].firstFrame;
			if (runs[i].selected && !old[best].selected) {
				// The highlight moved onto it: worth telling, the text did not change.
				changes_.push_back(TextChange{ ChangeKind::Changed, runs[i], old[best].text });
			}
		}
	}
	// 2. Same place, different text: the run changed.
	for (size_t i = 0; i < runs.size(); i++) {
		if (matched[i]) {
			continue;
		}
		for (size_t j = 0; j < old.size(); j++) {
			if (oldUsed[j] || old[j].source != runs[i].source || !SamePlace(runs[i], old[j])) {
				continue;
			}
			oldUsed[j] = true;
			matched[i] = true;
			runs[i].id = old[j].id;
			runs[i].firstFrame = old[j].firstFrame;
			changes_.push_back(TextChange{ ChangeKind::Changed, runs[i], old[j].text });
			break;
		}
	}
	// 3. Anything else is new.
	for (size_t i = 0; i < runs.size(); i++) {
		runs[i].lastFrame = frame;
		if (!matched[i]) {
			runs[i].id = nextId_++;
			runs[i].firstFrame = frame;
			changes_.push_back(TextChange{ ChangeKind::Appeared, runs[i], std::string() });
		}
	}
	// 4. Old runs nobody claimed: wait a few frames, then report them gone.
	for (size_t j = 0; j < old.size(); j++) {
		if (oldUsed[j]) {
			continue;
		}
		if (frame - old[j].lastFrame < GRACE_FRAMES) {
			missing_.push_back(old[j]);
		} else {
			changes_.push_back(TextChange{ ChangeKind::Disappeared, old[j], std::string() });
		}
	}
	current_ = std::move(runs);
	if (changes_.size() > MAX_PENDING_CHANGES) {
		// Nobody is listening; keep the newest.
		changes_.erase(changes_.begin(), changes_.end() - MAX_PENDING_CHANGES);
	}
}

std::vector<TextChange> ScreenText::TakeChanges() {
	std::vector<TextChange> out;
	out.swap(changes_);
	return out;
}

void ScreenText::Clear() {
	current_.clear();
	missing_.clear();
	changes_.clear();
}

}  // namespace Discovery
