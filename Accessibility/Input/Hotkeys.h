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

#pragma once

// Keyboard hotkeys: built-in ones (repeat last message, copy, history...)
// and ones that scripts register.
//
// Keys are named (A-Z, 0-9, F1-F12, Numpad0..., arrows, PageUp...) or given
// as a physical scancode ("sc:0x1E"). Punctuation keys can't be used by name,
// because they sit in different places on different keyboard layouts.
// Every binding can be changed in accessibility/hotkeys.ini.
//
// Threads: OnKey runs on PPSSPP's window thread, as keys arrive. Built-in
// actions run right there (they only use the thread-safe speech module), so
// they work even while the game is paused. Script actions are queued and
// run on the emulation thread, where Lua lives.

#include <string>
#include <vector>

struct KeyInput;

namespace Access {
namespace Hotkeys {

// Loads hotkeys.ini, writing a commented default file the first time.
void Init();
void Shutdown();
// Re-reads hotkeys.ini (the reload hotkey does this).
void ReloadConfig();

// Window thread. Returns true if the key belongs to a hotkey, so PPSSPP
// must not also handle it.
bool OnKey(const KeyInput &key);

// Emulation thread, every host frame: true while a game is running with no
// menu open. Keys without a modifier only work then, so they can't
// interfere with typing or menu navigation.
void SetInGame(bool inGame);

// ---- script hotkeys (emulation thread) ----

// Binds action to a key. The key comes from hotkeys.ini [<gameId>] if the
// user set one there, otherwise defaultKey. Returns false with *error set if
// the key is invalid or already taken. *usedKey gets the key actually bound
// ("" if the user turned the action off with "none").
bool RegisterScriptHotkey(const std::string &gameId, const std::string &action, const std::string &defaultKey,
	const std::string &description, std::string *usedKey, std::string *error);
bool UnregisterScriptHotkey(const std::string &action);
void ClearScriptHotkeys();

// Script actions pressed since the last call, oldest first.
std::vector<std::string> TakePressedScriptActions();

// "Ctrl+Shift+F5" style name of a parsed key string, or "" if invalid.
std::string NormalizeKeyName(const std::string &keyText, std::string *error);

}  // namespace Hotkeys
}  // namespace Access
