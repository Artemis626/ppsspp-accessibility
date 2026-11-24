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

#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Accessibility/Menu/MenuSpeech.h"
#include "Accessibility/Speech/Speech.h"
#include "Common/UI/Root.h"
#include "Common/UI/View.h"

namespace Access {
namespace MenuSpeech {

namespace {

UI::View *g_lastView = nullptr;
std::string g_lastText;

// Items like "Rendering resolution" show their current value ("2x") on the
// right. PPSSPP keeps that value behind a protected function, ValueText().
// A class derived from the widget is allowed to name protected members, so
// this helper reads the value without changing any upstream file. We never
// create a ValueReader; it only exists to get access.
struct ValueReader : public UI::AbstractChoiceWithValueDisplay {
	static std::string Read(const UI::AbstractChoiceWithValueDisplay *choice) {
		bool UI::AbstractChoiceWithValueDisplay::*masked = &ValueReader::passwordMasking_;
		if (choice->*masked) {
			return "";  // Never speak passwords.
		}
		std::string (UI::AbstractChoiceWithValueDisplay::*valueText)(bool *) const = &ValueReader::ValueText;
		bool shadow = false;
		return (choice->*valueText)(&shadow);
	}
};

// Same access trick for Choice, PPSSPP's basic button: we need its label and
// its icon, to name buttons that show only an icon.
struct ChoiceReader : public UI::Choice {
	static const std::string &Text(const UI::Choice *choice) {
		std::string UI::Choice::*text = &ChoiceReader::text_;
		return choice->*text;
	}
	static ImageID Image(const UI::Choice *choice) {
		ImageID UI::Choice::*image = &ChoiceReader::image_;
		return choice->*image;
	}
};

// ImageID keeps the icon's atlas name ("I_GEAR") as its only, private,
// member, a std::string_view. We copy that value out. The static_assert stops
// the build if a future PPSSPP release changes ImageID, instead of reading garbage.
static_assert(sizeof(ImageID) == sizeof(std::string_view), "ImageID layout changed; update IconName()");
std::string_view IconName(const ImageID &image) {
	std::string_view name;
	memcpy((void *)&name, (const void *)&image, sizeof(name));
	return name;
}

// Spoken names for the icons PPSSPP uses on buttons without text.
std::string IconWords(std::string_view icon) {
	static const std::unordered_map<std::string_view, const char *> words = {
		{ "I_NAVIGATE_BACK", "Back" },
		{ "I_GEAR", "Settings" },
		{ "I_GEAR_SMALL", "Settings" },
		{ "I_UP_DIRECTORY", "Up one folder" },
		{ "I_FOLDER_OPEN", "Load" },
		{ "I_INFO", "About" },
		{ "I_HOME", "Home folder" },
		{ "I_PIN", "Pin folder" },
		{ "I_UNPIN", "Unpin folder" },
		{ "I_SEARCH", "Search" },
		{ "I_GRID", "Grid view" },
		{ "I_LINES", "List view" },
		{ "I_THREE_DOTS", "More options" },
		{ "I_CROSS", "Close" },
		{ "I_CHECKMARK", "OK" },
		{ "I_PLUS", "Add" },
		{ "I_TRASHCAN", "Delete" },
		{ "I_FULLSCREEN", "Full screen" },
		{ "I_RESTORE", "Leave full screen" },
		{ "I_ROTATE_LEFT", "Rotate" },
		{ "I_LINK_OUT", "Open web page" },
		{ "I_LINK_OUT_QUESTION", "Help, opens web page" },
		{ "I_KEYBOARD", "Keyboard" },
		{ "I_FOLDER_UPLOAD", "Upload" },
		{ "I_WARNING", "Warning" },
		{ "I_HOMEBREW_STORE", "Homebrew store" },
		{ "I_ICON", "PPSSPP" },
		{ "I_ICON_GOLD", "PPSSPP Gold" },
	};
	auto it = words.find(icon);
	if (it != words.end()) {
		return it->second;
	}
	// Unknown icon: "I_FOLDER_OPEN" becomes "folder open". Not pretty, but
	// better than silence, and it tells us which icon to add above.
	std::string out;
	std::string_view rest = icon.substr(0, 2) == "I_" ? icon.substr(2) : icon;
	for (char c : rest) {
		out += c == '_' ? ' ' : (char)tolower((unsigned char)c);
	}
	return out;
}

// Turns line breaks into ". " so the screen reader pauses instead of
// reading the text as one run-on sentence.
std::string Flatten(const std::string &text) {
	std::string out;
	out.reserve(text.size());
	for (char c : text) {
		if (c == '\n') {
			if (!out.empty()) {
				out += ". ";
			}
		} else if (c != '\r') {
			out += c;
		}
	}
	return out;
}

std::string Describe(UI::View *view) {
	std::string text = view->DescribeText();

	// A button with only an icon describes itself as " choice" or " button"
	// (empty label). Use the icon's name as the label instead.
	ImageID iconOnly = ImageID::invalid();
	if (auto *choice = dynamic_cast<UI::Choice *>(view)) {
		if (ChoiceReader::Text(choice).empty()) {
			iconOnly = ChoiceReader::Image(choice);
		}
	} else if (auto *button = dynamic_cast<UI::Button *>(view)) {
		if (button->GetText().empty()) {
			iconOnly = button->GetImageID();
		}
	}
	if (iconOnly.isValid()) {
		std::string words = IconWords(IconName(iconOnly));
		size_t pos = text.find_first_not_of(' ');
		// Keep PPSSPP's own (translated) suffix, e.g. "choice" or "button".
		text = words + (pos == std::string::npos ? std::string() : " " + text.substr(pos));
	}

	// The file browser's "go to parent folder" entry is labelled "..".
	if (text == "..") {
		text = "Parent folder";
	}

	// The item's state goes right after its label (the first line); any extra
	// lines are help text and come last.
	std::string state;
	if (auto *header = dynamic_cast<UI::CollapsibleHeader *>(view)) {
		state = header->Toggled() ? "expanded" : "collapsed";
	} else if (auto *checkbox = dynamic_cast<UI::CheckBox *>(view)) {
		state = checkbox->Toggled() ? "checked" : "not checked";
	} else if (auto *valueChoice = dynamic_cast<UI::AbstractChoiceWithValueDisplay *>(view)) {
		state = Flatten(ValueReader::Read(valueChoice));
	}

	std::string label = text;
	std::string rest;
	size_t newline = text.find('\n');
	if (newline != std::string::npos) {
		label = text.substr(0, newline);
		rest = text.substr(newline + 1);
	}

	std::string result = label;
	if (!state.empty()) {
		result += ", " + state;
	}
	if (!view->IsEnabled()) {
		result += ", unavailable";
	}
	if (!rest.empty()) {
		result += ". " + rest;
	}
	return Flatten(result);
}

}  // namespace

void Update() {
	UI::View *view = UI::GetFocusedView();
	if (!view) {
		// Nothing focused (for example while a game is running). Forget the
		// last item so returning to the same menu item speaks it again.
		g_lastView = nullptr;
		g_lastText.clear();
		return;
	}

	std::string text = Describe(view);
	if (view == g_lastView && text == g_lastText) {
		return;
	}
	g_lastView = view;
	g_lastText = text;
	if (!text.empty()) {
		Speech::Speak(text, true);
	}
}

void Reset() {
	g_lastView = nullptr;
	g_lastText.clear();
}

}  // namespace MenuSpeech
}  // namespace Access
