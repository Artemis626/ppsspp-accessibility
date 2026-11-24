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

#include "ppsspp_config.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "Accessibility/Discovery/Ppsspp/DiscoveryHost.h"
#include "Accessibility/Input/Hotkeys.h"
#include "Accessibility/Log.h"
#include "Accessibility/Scripting/ScriptHost.h"
#include "Accessibility/Speech/Speech.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#endif

namespace Access {
namespace Hotkeys {

namespace {

struct Combo {
	InputKeyCode key = NKCODE_UNKNOWN;
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
	bool HasModifier() const { return ctrl || shift || alt; }
	bool operator<(const Combo &o) const {
		if (key != o.key) return key < o.key;
		if (ctrl != o.ctrl) return ctrl < o.ctrl;
		if (shift != o.shift) return shift < o.shift;
		return alt < o.alt;
	}
};

struct Binding {
	bool builtin;
	std::string action;
	std::string description;
	std::string keyName;  // Normalized, e.g. "Ctrl+Shift+R".
};

struct BuiltinAction {
	const char *action;
	const char *defaultKey;
	const char *description;
};

// Defaults use Ctrl+Shift, which neither PPSSPP nor NVDA uses for these keys.
const BuiltinAction BUILTINS[] = {
	{ "repeatLast", "Ctrl+Shift+R", "repeat the last message" },
	{ "copyLast", "Ctrl+Shift+C", "copy the last message, or the one you browsed to, to the clipboard" },
	{ "historyPrev", "Ctrl+Shift+PageUp", "older message in the history" },
	{ "historyNext", "Ctrl+Shift+PageDown", "newer message in the history" },
	{ "stopSpeech", "Ctrl+Shift+S", "stop speech" },
	{ "listHotkeys", "Ctrl+Shift+H", "list all hotkeys" },
	{ "reloadScript", "Ctrl+Shift+F12", "reload the script and this hotkey file" },
	{ "readScreen", "Ctrl+Shift+T", "read the text found on the game screen" },
	{ "survey", "Ctrl+Shift+D", "say how this game draws its text" },
	{ "toggleDialogReading", "Ctrl+Shift+A", "change what is read automatically: nothing, system dialogs, or dialogs and game text" },
	{ "inspectionReport", "Ctrl+Shift+L", "write a report of everything the discovery layer sees on this screen" },
	{ "record", "Ctrl+Shift+K", "record everything the discovery layer sees for ten seconds, or stop the recording" },
};

std::mutex g_mutex;
std::map<Combo, Binding> g_bindings;
std::set<InputKeyCode> g_heldConsumed;   // Keys whose key-down we swallowed.
std::vector<std::string> g_pressedScriptActions;
std::atomic<bool> g_inGame{ false };
// hotkeys.ini contents: lowercase section -> lowercase action -> key text.
std::map<std::string, std::map<std::string, std::string>> g_config;

// ---- key names ------------------------------------------------------------

struct KeyName {
	const char *name;
	InputKeyCode code;
};

// First name for each code is the one used when speaking/normalizing.
const KeyName NAMED_KEYS[] = {
	{ "F1", NKCODE_F1 }, { "F2", NKCODE_F2 }, { "F3", NKCODE_F3 }, { "F4", NKCODE_F4 },
	{ "F5", NKCODE_F5 }, { "F6", NKCODE_F6 }, { "F7", NKCODE_F7 }, { "F8", NKCODE_F8 },
	{ "F9", NKCODE_F9 }, { "F10", NKCODE_F10 }, { "F11", NKCODE_F11 }, { "F12", NKCODE_F12 },
	{ "Numpad0", NKCODE_NUMPAD_0 }, { "Numpad1", NKCODE_NUMPAD_1 }, { "Numpad2", NKCODE_NUMPAD_2 },
	{ "Numpad3", NKCODE_NUMPAD_3 }, { "Numpad4", NKCODE_NUMPAD_4 }, { "Numpad5", NKCODE_NUMPAD_5 },
	{ "Numpad6", NKCODE_NUMPAD_6 }, { "Numpad7", NKCODE_NUMPAD_7 }, { "Numpad8", NKCODE_NUMPAD_8 },
	{ "Numpad9", NKCODE_NUMPAD_9 },
	{ "NumpadPlus", NKCODE_NUMPAD_ADD }, { "NumpadAdd", NKCODE_NUMPAD_ADD },
	{ "NumpadMinus", NKCODE_NUMPAD_SUBTRACT }, { "NumpadSubtract", NKCODE_NUMPAD_SUBTRACT },
	{ "NumpadMultiply", NKCODE_NUMPAD_MULTIPLY }, { "NumpadStar", NKCODE_NUMPAD_MULTIPLY },
	{ "NumpadDivide", NKCODE_NUMPAD_DIVIDE }, { "NumpadSlash", NKCODE_NUMPAD_DIVIDE },
	{ "NumpadDot", NKCODE_NUMPAD_DOT }, { "NumpadDecimal", NKCODE_NUMPAD_DOT },
	{ "Up", NKCODE_DPAD_UP }, { "Down", NKCODE_DPAD_DOWN }, { "Left", NKCODE_DPAD_LEFT }, { "Right", NKCODE_DPAD_RIGHT },
	{ "Home", NKCODE_MOVE_HOME }, { "End", NKCODE_MOVE_END },
	{ "PageUp", NKCODE_PAGE_UP }, { "PageDown", NKCODE_PAGE_DOWN },
	{ "Insert", NKCODE_INSERT }, { "Delete", NKCODE_FORWARD_DEL }, { "Backspace", NKCODE_DEL },
	{ "Space", NKCODE_SPACE }, { "Enter", NKCODE_ENTER }, { "Return", NKCODE_ENTER },
	{ "Tab", NKCODE_TAB }, { "Escape", NKCODE_ESCAPE }, { "Esc", NKCODE_ESCAPE },
	{ "Pause", NKCODE_BREAK },
};

// Names people may try for punctuation keys. Refused with an explanation.
const char *const PUNCTUATION_NAMES[] = {
	"comma", "period", "dot", "minus", "plus", "equals", "equal", "semicolon", "colon",
	"slash", "backslash", "apostrophe", "quote", "grave", "tilde", "backquote",
	"leftbracket", "rightbracket", "bracketleft", "bracketright", "pipe",
};

std::string Lower(std::string s) {
	for (char &c : s) {
		c = (char)tolower((unsigned char)c);
	}
	return s;
}

std::string Trim(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) {
		return std::string();
	}
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

bool LookupKeyName(const std::string &lowerName, InputKeyCode *code, std::string *canonical) {
	if (lowerName.size() == 1 && isalnum((unsigned char)lowerName[0])) {
		char c = (char)toupper((unsigned char)lowerName[0]);
		*code = isdigit((unsigned char)c) ? (InputKeyCode)(NKCODE_0 + (c - '0')) : (InputKeyCode)(NKCODE_A + (c - 'A'));
		*canonical = std::string(1, c);
		return true;
	}
	for (const KeyName &k : NAMED_KEYS) {
		if (Lower(k.name) == lowerName) {
			*code = k.code;
			// Canonical = first name in the table with this code.
			for (const KeyName &first : NAMED_KEYS) {
				if (first.code == k.code) {
					*canonical = first.name;
					break;
				}
			}
			return true;
		}
	}
	return false;
}

#if PPSSPP_PLATFORM(WINDOWS)
// Virtual-key to PPSSPP key code, matching Windows/RawInput.cpp, so a
// scancode binding matches what PPSSPP receives for that key. Includes the
// punctuation keys: a scancode is a physical position, so it is the safe
// way to use them on any layout.
InputKeyCode VirtualKeyToKeyCode(UINT vk) {
	if (vk >= 'A' && vk <= 'Z') return (InputKeyCode)(NKCODE_A + (vk - 'A'));
	if (vk >= '0' && vk <= '9') return (InputKeyCode)(NKCODE_0 + (vk - '0'));
	if (vk >= VK_F1 && vk <= VK_F12) return (InputKeyCode)(NKCODE_F1 + (vk - VK_F1));
	if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (InputKeyCode)(NKCODE_NUMPAD_0 + (vk - VK_NUMPAD0));
	switch (vk) {
	case VK_DECIMAL: return NKCODE_NUMPAD_DOT;
	case VK_DIVIDE: return NKCODE_NUMPAD_DIVIDE;
	case VK_MULTIPLY: return NKCODE_NUMPAD_MULTIPLY;
	case VK_SUBTRACT: return NKCODE_NUMPAD_SUBTRACT;
	case VK_ADD: return NKCODE_NUMPAD_ADD;
	case VK_SEPARATOR: return NKCODE_NUMPAD_COMMA;
	case VK_OEM_PERIOD: return NKCODE_PERIOD;
	case VK_OEM_COMMA: return NKCODE_COMMA;
	case VK_OEM_MINUS: return NKCODE_MINUS;
	case VK_OEM_PLUS: return NKCODE_PLUS;
	case VK_OEM_1: return NKCODE_SEMICOLON;
	case VK_OEM_2: return NKCODE_SLASH;
	case VK_OEM_3: return NKCODE_GRAVE;
	case VK_OEM_4: return NKCODE_LEFT_BRACKET;
	case VK_OEM_5: return NKCODE_BACKSLASH;
	case VK_OEM_6: return NKCODE_RIGHT_BRACKET;
	case VK_OEM_7: return NKCODE_APOSTROPHE;
	case VK_OEM_8: return NKCODE_GRAVE;
	case VK_OEM_102: return NKCODE_EXT_PIPE;
	case VK_BACK: return NKCODE_DEL;
	case VK_SPACE: return NKCODE_SPACE;
	case VK_ESCAPE: return NKCODE_ESCAPE;
	case VK_UP: return NKCODE_DPAD_UP;
	case VK_DOWN: return NKCODE_DPAD_DOWN;
	case VK_LEFT: return NKCODE_DPAD_LEFT;
	case VK_RIGHT: return NKCODE_DPAD_RIGHT;
	case VK_INSERT: return NKCODE_INSERT;
	case VK_HOME: return NKCODE_MOVE_HOME;
	case VK_END: return NKCODE_MOVE_END;
	case VK_PRIOR: return NKCODE_PAGE_UP;
	case VK_NEXT: return NKCODE_PAGE_DOWN;
	case VK_DELETE: return NKCODE_FORWARD_DEL;
	case VK_TAB: return NKCODE_TAB;
	case VK_RETURN: return NKCODE_ENTER;
	case VK_PAUSE: return NKCODE_BREAK;
	case VK_CLEAR: return NKCODE_CLEAR;
	default: return NKCODE_UNKNOWN;
	}
}
#endif

bool ParseCombo(const std::string &text, Combo *combo, std::string *normalized, std::string *error) {
	std::vector<std::string> parts;
	size_t start = 0;
	while (true) {
		size_t plus = text.find('+', start);
		parts.push_back(Trim(text.substr(start, plus == std::string::npos ? std::string::npos : plus - start)));
		if (plus == std::string::npos) {
			break;
		}
		start = plus + 1;
	}
	*combo = Combo();
	std::string prefix;
	for (size_t i = 0; i + 1 < parts.size(); i++) {
		std::string m = Lower(parts[i]);
		if (m == "ctrl" || m == "control") {
			combo->ctrl = true;
		} else if (m == "shift") {
			combo->shift = true;
		} else if (m == "alt") {
			combo->alt = true;
		} else {
			*error = "unknown modifier '" + parts[i] + "' (use Ctrl, Shift or Alt)";
			return false;
		}
	}
	if (combo->ctrl) prefix += "Ctrl+";
	if (combo->shift) prefix += "Shift+";
	if (combo->alt) prefix += "Alt+";

	std::string keyText = parts.back();
	std::string lowerKey = Lower(keyText);
	if (keyText.empty()) {
		*error = "no key given in '" + text + "'";
		return false;
	}

	if (lowerKey.compare(0, 3, "sc:") == 0) {
#if PPSSPP_PLATFORM(WINDOWS)
		char *end = nullptr;
		unsigned long sc = strtoul(keyText.c_str() + 3, &end, 0);
		if (!end || *end != '\0' || sc == 0 || sc > 0xE0FF) {
			*error = "bad scancode '" + keyText + "' (example: sc:0x1E, or sc:0xE048 for an extended key)";
			return false;
		}
		UINT vk = MapVirtualKeyW((UINT)sc, MAPVK_VSC_TO_VK_EX);
		InputKeyCode code = VirtualKeyToKeyCode(vk);
		if (code == NKCODE_UNKNOWN) {
			*error = "scancode '" + keyText + "' doesn't match a key PPSSPP can see";
			return false;
		}
		combo->key = code;
		char buf[32];
		snprintf(buf, sizeof(buf), "sc:0x%lX", sc);
		*normalized = prefix + buf;
		return true;
#else
		*error = "scancodes are only supported on Windows";
		return false;
#endif
	}

	for (const char *p : PUNCTUATION_NAMES) {
		if (lowerKey == p) {
			*error = "punctuation keys can't be used by name, because they are in different places on different keyboard layouts. Use a scancode instead, for example sc:0x33";
			return false;
		}
	}
	if (lowerKey.size() == 1 && !isalnum((unsigned char)lowerKey[0])) {
		*error = "punctuation keys can't be used by name, because they are in different places on different keyboard layouts. Use a scancode instead, for example sc:0x33";
		return false;
	}

	InputKeyCode code;
	std::string canonical;
	if (!LookupKeyName(lowerKey, &code, &canonical)) {
		*error = "unknown key name '" + keyText + "'";
		return false;
	}
	combo->key = code;
	*normalized = prefix + canonical;
	return true;
}

// ---- hotkeys.ini ----------------------------------------------------------

Path ConfigPath() {
	return File::GetExeDirectory() / "accessibility" / "hotkeys.ini";
}

const char *DEFAULT_FILE_HEADER =
	"; PPSSPP accessibility hotkeys\n"
	";\n"
	"; Format:  action = keys     for example:  repeatLast = Ctrl+Shift+R\n"
	";\n"
	"; Key names: A-Z, 0-9, F1-F12, Numpad0-Numpad9, NumpadPlus, NumpadMinus,\n"
	";   NumpadMultiply, NumpadDivide, NumpadDot, Up, Down, Left, Right, Home, End,\n"
	";   PageUp, PageDown, Insert, Delete, Backspace, Space, Enter, Tab, Escape, Pause.\n"
	"; Numpad keys need Num Lock on.\n"
	"; A physical key by scancode works on any keyboard layout: sc:0x1E\n"
	";   (extended keys such as the arrows start with E0: sc:0xE048).\n"
	"; Modifiers: Ctrl+  Shift+  Alt+   Write  none  to turn an action off.\n"
	"; Punctuation keys can't be used by name: they move between keyboard layouts.\n"
	"; Keys without a modifier only work while a game runs with no menu open.\n"
	";\n"
	"; [global] holds the built-in actions. To change a key a script registers,\n"
	"; add a section named after the game ID, for example:\n"
	";   [ULUS10041]\n"
	";   readHP = Numpad5\n"
	"; Changes take effect after the reload hotkey or a restart.\n"
	"\n"
	"[global]\n";

void WriteDefaultFile(const Path &path) {
	File::CreateFullPath(path.NavigateUp());
	FILE *f = File::OpenCFile(path, "wb");
	if (!f) {
		Log::Warn("Could not create " + path.ToString());
		return;
	}
	fputs(DEFAULT_FILE_HEADER, f);
	for (const BuiltinAction &b : BUILTINS) {
		fprintf(f, "; %s\n%s = %s\n", b.description, b.action, b.defaultKey);
	}
	fclose(f);
	Log::Info("Created " + path.ToString() + " with the default hotkeys.");
}

void LoadConfigFile() {
	g_config.clear();
	Path path = ConfigPath();
	if (!File::Exists(path)) {
		WriteDefaultFile(path);
	}
	std::string text;
	if (!File::ReadBinaryFileToString(path, &text)) {
		return;
	}
	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
		text.erase(0, 3);
	}
	std::string section = "global";
	size_t pos = 0;
	while (pos < text.size()) {
		size_t eol = text.find('\n', pos);
		std::string line = Trim(text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos));
		pos = eol == std::string::npos ? text.size() : eol + 1;
		if (line.empty() || line[0] == ';' || line[0] == '#') {
			continue;
		}
		if (line[0] == '[') {
			size_t close = line.find(']');
			section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		g_config[section][Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
	}
}

std::string ConfiguredKey(const std::string &section, const std::string &action) {
	auto s = g_config.find(Lower(section));
	if (s == g_config.end()) {
		return std::string();
	}
	auto a = s->second.find(Lower(action));
	return a == s->second.end() ? std::string() : a->second;
}

// Call with g_mutex held.
void RebuildBuiltinsLocked() {
	for (auto it = g_bindings.begin(); it != g_bindings.end();) {
		it = it->second.builtin ? g_bindings.erase(it) : std::next(it);
	}
	for (const BuiltinAction &b : BUILTINS) {
		std::string keyText = ConfiguredKey("global", b.action);
		if (keyText.empty()) {
			keyText = b.defaultKey;
		}
		if (Lower(keyText) == "none") {
			continue;
		}
		Combo combo;
		std::string normalized, error;
		if (!ParseCombo(keyText, &combo, &normalized, &error)) {
			Log::Warn(std::string("hotkeys.ini: ") + b.action + " = " + keyText + ": " + error + ". Using " + b.defaultKey + ".");
			ParseCombo(b.defaultKey, &combo, &normalized, &error);
		}
		auto existing = g_bindings.find(combo);
		if (existing != g_bindings.end()) {
			Log::Warn(std::string("hotkeys.ini: ") + b.action + " and " + existing->second.action + " both use " + normalized + "; " + b.action + " is off.");
			continue;
		}
		g_bindings[combo] = Binding{ true, b.action, b.description, normalized };
	}
}

// ---- built-in actions (window thread) -------------------------------------

void ListHotkeys() {
	std::string builtins, scripts;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (const BuiltinAction &b : BUILTINS) {
			for (const auto &kv : g_bindings) {
				if (kv.second.builtin && kv.second.action == b.action) {
					builtins += kv.second.keyName + ", " + kv.second.description + ". ";
				}
			}
		}
		for (const auto &kv : g_bindings) {
			if (!kv.second.builtin) {
				scripts += kv.second.keyName + ", " + (kv.second.description.empty() ? kv.second.action : kv.second.description) + ". ";
			}
		}
	}
	std::string text = "Hotkeys. " + builtins;
	text += scripts.empty() ? "The script has no hotkeys." : "Script hotkeys: " + scripts;
	Speech::SpeakTransient(text, true);
}

void RunBuiltin(const std::string &action) {
	if (action == "repeatLast") {
		Speech::RepeatLast();
	} else if (action == "copyLast") {
		std::string text = Speech::CurrentHistoryEntry();
		if (text.empty()) {
			Speech::SpeakTransient("Nothing to copy", true);
		} else if (Speech::CopyToClipboard(text)) {
			Speech::SpeakTransient("Copied", true);
		} else {
			Speech::SpeakTransient("Copy failed", true);
		}
	} else if (action == "historyPrev") {
		Speech::BrowseHistory(+1);
	} else if (action == "historyNext") {
		Speech::BrowseHistory(-1);
	} else if (action == "stopSpeech") {
		Speech::Stop();
	} else if (action == "listHotkeys") {
		ListHotkeys();
	} else if (action == "reloadScript") {
		Script::RequestReload();
	} else if (action == "readScreen") {
		DiscoveryHost::RequestReadScreen();
	} else if (action == "survey") {
		DiscoveryHost::RequestSurvey();
	} else if (action == "inspectionReport") {
		DiscoveryHost::RequestReport();
	} else if (action == "record") {
		DiscoveryHost::RequestRecord();
	} else if (action == "toggleDialogReading") {
		DiscoveryHost::RequestToggleAutoRead();
	}
}

}  // namespace

void Init() {
	std::lock_guard<std::mutex> lock(g_mutex);
	LoadConfigFile();
	RebuildBuiltinsLocked();
}

void Shutdown() {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_bindings.clear();
	g_heldConsumed.clear();
	g_pressedScriptActions.clear();
}

void ReloadConfig() {
	std::lock_guard<std::mutex> lock(g_mutex);
	LoadConfigFile();
	RebuildBuiltinsLocked();
	Log::Info("Hotkeys reloaded from hotkeys.ini.");
}

bool OnKey(const KeyInput &key) {
	if (key.deviceId != DEVICE_ID_KEYBOARD || (key.flags & KeyInputFlags::CHAR)) {
		return false;
	}
	InputKeyCode code = key.keyCode;
	switch (code) {
	case NKCODE_CTRL_LEFT: case NKCODE_CTRL_RIGHT:
	case NKCODE_SHIFT_LEFT: case NKCODE_SHIFT_RIGHT:
	case NKCODE_ALT_LEFT: case NKCODE_ALT_RIGHT:
		return false;  // Modifiers always pass through.
	default:
		break;
	}

	if (key.flags & KeyInputFlags::UP) {
		std::lock_guard<std::mutex> lock(g_mutex);
		// Swallow the key-up of every key-down we swallowed.
		return g_heldConsumed.erase(code) > 0;
	}
	if (!(key.flags & KeyInputFlags::DOWN)) {
		return false;
	}

	Combo combo;
	combo.key = code;
#if PPSSPP_PLATFORM(WINDOWS)
	// Ask Windows for the modifier state instead of tracking it ourselves, so
	// a modifier released while the window was unfocused can't get stuck.
	combo.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	combo.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	combo.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
#endif

	std::string action;
	bool builtin = false;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_bindings.find(combo);
		if (it == g_bindings.end()) {
			return false;
		}
		if (!combo.HasModifier() && !g_inGame) {
			return false;  // Plain keys stay with menus and text fields.
		}
		if (!g_heldConsumed.insert(code).second) {
			return true;  // Key auto-repeat while held: swallow, don't fire again.
		}
		action = it->second.action;
		builtin = it->second.builtin;
		if (!builtin) {
			g_pressedScriptActions.push_back(action);
		}
	}
	if (builtin) {
		RunBuiltin(action);
	}
	return true;
}

void SetInGame(bool inGame) {
	g_inGame = inGame;
}

bool RegisterScriptHotkey(const std::string &gameId, const std::string &action, const std::string &defaultKey,
	const std::string &description, std::string *usedKey, std::string *error) {
	std::lock_guard<std::mutex> lock(g_mutex);
	std::string keyText = ConfiguredKey(gameId, action);
	bool fromConfig = !keyText.empty();
	if (!fromConfig) {
		keyText = defaultKey;
	}
	usedKey->clear();
	// Re-registering an action replaces its old key.
	for (auto it = g_bindings.begin(); it != g_bindings.end();) {
		it = (!it->second.builtin && it->second.action == action) ? g_bindings.erase(it) : std::next(it);
	}
	if (Lower(keyText) == "none" || keyText.empty()) {
		return true;  // Turned off by the user (or no default given).
	}
	Combo combo;
	std::string normalized;
	if (!ParseCombo(keyText, &combo, &normalized, error)) {
		if (fromConfig) {
			*error = "hotkeys.ini [" + gameId + "] " + action + ": " + *error;
		}
		return false;
	}
	auto existing = g_bindings.find(combo);
	if (existing != g_bindings.end()) {
		*error = normalized + " is already used by " + existing->second.action;
		return false;
	}
	g_bindings[combo] = Binding{ false, action, description, normalized };
	*usedKey = normalized;
	return true;
}

bool UnregisterScriptHotkey(const std::string &action) {
	std::lock_guard<std::mutex> lock(g_mutex);
	bool removed = false;
	for (auto it = g_bindings.begin(); it != g_bindings.end();) {
		if (!it->second.builtin && it->second.action == action) {
			it = g_bindings.erase(it);
			removed = true;
		} else {
			++it;
		}
	}
	return removed;
}

void ClearScriptHotkeys() {
	std::lock_guard<std::mutex> lock(g_mutex);
	for (auto it = g_bindings.begin(); it != g_bindings.end();) {
		it = it->second.builtin ? std::next(it) : g_bindings.erase(it);
	}
	g_pressedScriptActions.clear();
}

std::vector<std::string> TakePressedScriptActions() {
	std::lock_guard<std::mutex> lock(g_mutex);
	std::vector<std::string> out;
	out.swap(g_pressedScriptActions);
	return out;
}

std::string NormalizeKeyName(const std::string &keyText, std::string *error) {
	Combo combo;
	std::string normalized;
	if (!ParseCombo(keyText, &combo, &normalized, error)) {
		return std::string();
	}
	return normalized;
}

}  // namespace Hotkeys
}  // namespace Access
