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
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Harness.h"
#include "TextUtil.h"

namespace Discovery {

namespace {

// A found run within this error rate of an expected text counts as "near".
const float NEAR_ERROR_RATE = 0.25f;

bool IsYes(const std::string &value) {
	std::string v = ToLower(value);
	return v == "yes" || v == "true" || v == "1" || v == "on";
}

bool StartsWith(const std::string &s, const char *prefix) {
	return s.compare(0, strlen(prefix), prefix) == 0;
}

const char *VerdictName(TextVerdict v) {
	switch (v) {
	case TextVerdict::Exact: return "exact";
	case TextVerdict::Contained: return "contained";
	case TextVerdict::Near: return "near";
	case TextVerdict::Missing: return "missing";
	}
	return "missing";
}

std::string Float(float f) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%.3f", f);
	return buf;
}

}  // namespace

bool ParseHarnessCase(std::string_view ini, HarnessCase *out, std::string *error) {
	*out = HarnessCase();
	bool sawCase = false;
	for (const IniSection &section : ParseIni(ini)) {
		std::string name = ToLower(section.name);
		if (name == "case") {
			sawCase = true;
			out->game = section.Get("game");
			out->state = section.Get("state");
			out->description = section.Get("description");
			if (section.Has("settle_frames")) {
				out->settleFrames = std::max(1, atoi(section.Get("settle_frames").c_str()));
			}
			out->complete = IsYes(section.Get("complete"));
		} else if (StartsWith(name, "text")) {
			ExpectedText t;
			t.section = section.name;
			t.text = NormalizeSpaces(section.Get("text"));
			t.contains = ToLower(section.Get("match", "exact")) == "contains";
			t.label = section.Get("label");
			if (t.text.empty()) {
				*error = "[" + section.name + "] has no text";
				return false;
			}
			out->texts.push_back(t);
		} else if (StartsWith(name, "cursor")) {
			out->cursors++;
		} else if (StartsWith(name, "value")) {
			out->values++;
		}
	}
	if (!sawCase) {
		*error = "no [case] section";
		return false;
	}
	return true;
}

HarnessResult ScoreHarnessCase(const HarnessCase &test, const std::vector<TextRun> &runs) {
	HarnessResult result;
	std::vector<std::string> found;
	std::string page;  // Everything found, in reading order, as one string.
	for (const TextRun &run : runs) {
		std::string text = NormalizeSpaces(run.text);
		found.push_back(text);
		result.found.push_back(FoundScore{ run, false });
		if (!text.empty()) {
			page += (page.empty() ? "" : " ") + text;
		}
	}

	for (const ExpectedText &expected : test.texts) {
		TextScore score;
		score.expected = expected;
		int expectedLen = std::max((int)DecodeUtf8(expected.text).size(), 1);

		int bestIndex = -1;
		for (size_t i = 0; i < found.size(); i++) {
			if (found[i].empty()) {
				continue;
			}
			bool exact = found[i] == expected.text;
			bool inside = found[i].find(expected.text) != std::string::npos;
			if (exact || (expected.contains && inside)) {
				score.verdict = TextVerdict::Exact;
				bestIndex = (int)i;
				score.errorRate = 0;
				break;
			}
			if (inside && score.verdict != TextVerdict::Contained) {
				score.verdict = TextVerdict::Contained;
				bestIndex = (int)i;
				score.errorRate = 0;
				continue;
			}
			if (score.verdict == TextVerdict::Contained) {
				continue;
			}
			float rate = (float)EditDistance(found[i], expected.text) / (float)expectedLen;
			if (rate < score.errorRate) {
				score.errorRate = rate;
				bestIndex = (int)i;
				score.verdict = rate <= NEAR_ERROR_RATE ? TextVerdict::Near : TextVerdict::Missing;
			}
		}
		// Found across several runs (a sentence the layer split into two cells)?
		if ((score.verdict == TextVerdict::Near || score.verdict == TextVerdict::Missing)
			&& page.find(expected.text) != std::string::npos) {
			score.verdict = expected.contains ? TextVerdict::Exact : TextVerdict::Contained;
			score.errorRate = 0;
			score.best = expected.text;
			score.source = "several runs";
			bestIndex = -1;
			// Every run that lies wholly inside the expected text is explained by it.
			for (size_t i = 0; i < found.size(); i++) {
				if (!found[i].empty() && expected.text.find(found[i]) != std::string::npos) {
					result.found[i].explained = true;
				}
			}
		}
		if (bestIndex >= 0) {
			score.best = found[bestIndex];
			score.source = TextSourceName(runs[bestIndex].source);
			score.confidence = runs[bestIndex].confidence;
			if (score.verdict != TextVerdict::Missing) {
				result.found[bestIndex].explained = true;
			}
		}
		switch (score.verdict) {
		case TextVerdict::Exact: result.exact++; break;
		case TextVerdict::Contained: result.contained++; break;
		case TextVerdict::Near: result.near++; break;
		case TextVerdict::Missing: result.missing++; break;
		}
		result.texts.push_back(score);
	}

	// A found run identical to an expected text is explained even when another
	// run already "used" that expectation (the same word twice on screen).
	int explained = 0;
	for (size_t i = 0; i < found.size(); i++) {
		if (!result.found[i].explained) {
			for (const ExpectedText &expected : test.texts) {
				if (found[i] == expected.text) {
					result.found[i].explained = true;
				}
			}
		}
		explained += result.found[i].explained ? 1 : 0;
	}
	if (!test.texts.empty()) {
		result.recall = (float)(result.exact + result.contained) / (float)test.texts.size();
	}
	if (!found.empty()) {
		result.precision = (float)explained / (float)found.size();
	}
	return result;
}

std::string HarnessResultToJson(const HarnessCase &test, const HarnessResult &result, const std::string &mode, const std::string &extra) {
	std::string out = "{\n";
	out += "  \"game\": " + JsonString(test.game) + ",\n";
	out += "  \"state\": " + JsonString(test.state) + ",\n";
	out += "  \"description\": " + JsonString(test.description) + ",\n";
	out += "  \"mode\": " + JsonString(mode) + ",\n";
	out += std::string("  \"complete\": ") + (test.complete ? "true" : "false") + ",\n";
	out += "  \"expected_texts\": " + std::to_string(test.texts.size()) + ",\n";
	out += "  \"exact\": " + std::to_string(result.exact) + ",\n";
	out += "  \"contained\": " + std::to_string(result.contained) + ",\n";
	out += "  \"near\": " + std::to_string(result.near) + ",\n";
	out += "  \"missing\": " + std::to_string(result.missing) + ",\n";
	out += "  \"recall\": " + Float(result.recall) + ",\n";
	out += "  \"precision\": " + Float(result.precision) + ",\n";
	out += "  \"found_runs\": " + std::to_string(result.found.size()) + ",\n";
	out += "  \"cursors_declared\": " + std::to_string(test.cursors) + ",\n";
	out += "  \"values_declared\": " + std::to_string(test.values) + ",\n";
	if (!extra.empty()) {
		out += "  " + extra + ",\n";
	}
	out += "  \"texts\": [\n";
	for (size_t i = 0; i < result.texts.size(); i++) {
		const TextScore &t = result.texts[i];
		out += "    {\"section\": " + JsonString(t.expected.section) + ", \"expected\": " + JsonString(t.expected.text)
			+ ", \"verdict\": \"" + VerdictName(t.verdict) + "\", \"best\": " + JsonString(t.best)
			+ ", \"source\": " + JsonString(t.source) + ", \"error_rate\": " + Float(t.errorRate)
			+ ", \"confidence\": " + Float(t.confidence) + "}";
		out += i + 1 < result.texts.size() ? ",\n" : "\n";
	}
	out += "  ],\n  \"found\": [\n";
	for (size_t i = 0; i < result.found.size(); i++) {
		const TextRun &r = result.found[i].run;
		char place[160];
		snprintf(place, sizeof(place), ", \"x\": %.0f, \"y\": %.0f, \"w\": %.0f, \"h\": %.0f, \"code\": \"0x%08X\", \"buffer\": \"0x%08X\"",
			r.rect.x0, r.rect.y0, r.rect.Width(), r.rect.Height(), r.codePc, r.bufferAddr);
		out += "    {\"text\": " + JsonString(r.text) + ", \"source\": \"" + TextSourceName(r.source) + "\""
			+ ", \"confidence\": " + Float(r.confidence) + place
			+ ", \"explained\": " + (result.found[i].explained ? "true" : "false") + "}";
		out += i + 1 < result.found.size() ? ",\n" : "\n";
	}
	out += "  ]\n}\n";
	return out;
}

}  // namespace Discovery
