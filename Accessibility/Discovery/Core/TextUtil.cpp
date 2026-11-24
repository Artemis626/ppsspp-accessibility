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

#include "TextUtil.h"

namespace Discovery {

const char *TextSourceName(TextSource source) {
	switch (source) {
	case TextSource::SystemDialog: return "system_dialog";
	case TextSource::SystemFont: return "system_font";
	case TextSource::FontCallStream: return "font_call_stream";
	case TextSource::GameFont: return "game_font";
	}
	return "unknown";
}

void AppendUtf8(std::string &out, u32 c) {
	if (c < 0x80) {
		out += (char)c;
	} else if (c < 0x800) {
		out += (char)(0xC0 | (c >> 6));
		out += (char)(0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		out += (char)(0xE0 | (c >> 12));
		out += (char)(0x80 | ((c >> 6) & 0x3F));
		out += (char)(0x80 | (c & 0x3F));
	} else {
		out += (char)(0xF0 | ((c >> 18) & 0x07));
		out += (char)(0x80 | ((c >> 12) & 0x3F));
		out += (char)(0x80 | ((c >> 6) & 0x3F));
		out += (char)(0x80 | (c & 0x3F));
	}
}

std::vector<u32> DecodeUtf8(std::string_view text) {
	std::vector<u32> out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		unsigned char c = (unsigned char)text[i];
		u32 cp = c;
		int extra = 0;
		if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
		else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
		else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
		i++;
		for (int k = 0; k < extra && i < text.size(); k++, i++) {
			unsigned char cc = (unsigned char)text[i];
			if ((cc & 0xC0) != 0x80) {
				break;  // Broken sequence: keep what we have.
			}
			cp = (cp << 6) | (cc & 0x3F);
		}
		out.push_back(cp);
	}
	return out;
}

static bool IsSpace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string Trim(std::string_view text) {
	size_t a = 0, b = text.size();
	while (a < b && IsSpace(text[a])) a++;
	while (b > a && IsSpace(text[b - 1])) b--;
	return std::string(text.substr(a, b - a));
}

std::string NormalizeSpaces(std::string_view text) {
	std::string out;
	bool pendingSpace = false;
	for (char c : text) {
		if (IsSpace(c)) {
			pendingSpace = !out.empty();
		} else {
			if (pendingSpace) {
				out += ' ';
				pendingSpace = false;
			}
			out += c;
		}
	}
	return out;
}

std::string ToLower(std::string_view text) {
	std::string out(text);
	for (char &c : out) {
		if (c >= 'A' && c <= 'Z') {
			c = (char)(c - 'A' + 'a');
		}
	}
	return out;
}

int EditDistance(std::string_view a, std::string_view b) {
	std::vector<u32> x = DecodeUtf8(a), y = DecodeUtf8(b);
	std::vector<int> prev(y.size() + 1), cur(y.size() + 1);
	for (size_t j = 0; j <= y.size(); j++) {
		prev[j] = (int)j;
	}
	for (size_t i = 1; i <= x.size(); i++) {
		cur[0] = (int)i;
		for (size_t j = 1; j <= y.size(); j++) {
			int subst = prev[j - 1] + (x[i - 1] == y[j - 1] ? 0 : 1);
			cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, subst });
		}
		prev.swap(cur);
	}
	return prev[y.size()];
}

std::string JsonString(std::string_view text) {
	std::string out = "\"";
	for (char ch : text) {
		unsigned char c = (unsigned char)ch;
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += ch;
			}
		}
	}
	out += '"';
	return out;
}

std::string IniSection::Get(const std::string &key, const std::string &fallback) const {
	auto it = values.find(key);
	return it == values.end() ? fallback : it->second;
}

std::vector<IniSection> ParseIni(std::string_view text) {
	std::vector<IniSection> sections;
	if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") {
		text.remove_prefix(3);
	}
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t eol = text.find('\n', pos);
		std::string_view raw = text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
		pos = eol == std::string_view::npos ? text.size() + 1 : eol + 1;

		// Cut a trailing comment: ';' or '#' that follows whitespace.
		size_t cut = std::string_view::npos;
		for (size_t i = 0; i < raw.size(); i++) {
			if ((raw[i] == ';' || raw[i] == '#') && (i == 0 || IsSpace(raw[i - 1]))) {
				cut = i;
				break;
			}
		}
		std::string line = Trim(raw.substr(0, cut));
		if (line.empty()) {
			continue;
		}
		if (line[0] == '[') {
			size_t close = line.find(']');
			IniSection section;
			section.name = Trim(std::string_view(line).substr(1, close == std::string::npos ? std::string::npos : close - 1));
			sections.push_back(section);
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		if (sections.empty()) {
			sections.push_back(IniSection());
		}
		sections.back().values[ToLower(Trim(std::string_view(line).substr(0, eq)))] = Trim(std::string_view(line).substr(eq + 1));
	}
	return sections;
}

}  // namespace Discovery
