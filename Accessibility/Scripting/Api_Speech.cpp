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

// Speech for scripts: the global speak() / stopSpeech() functions and the
// "speech" table.

#include <string>
#include <string_view>
#include <vector>

#include "Accessibility/Scripting/LuaInternal.h"
#include "Accessibility/Speech/Speech.h"

namespace Access {
namespace Script {

namespace {

// speak(text [, interrupt]) - text can be any value; numbers and booleans are
// converted the way tostring() does it.
int L_Speak(lua_State *L) {
	luaL_checkany(L, 1);
	// Read interrupt first: luaL_tolstring pushes its result onto the stack,
	// which would otherwise sit where argument 2 is read.
	bool interrupt = lua_toboolean(L, 2) != 0;
	size_t len = 0;
	const char *text = luaL_tolstring(L, 1, &len);
	Speech::Speak(std::string_view(text, len), interrupt);
	return 0;
}

int L_Stop(lua_State *L) {
	Speech::Stop();
	return 0;
}

int L_Braille(lua_State *L) {
	luaL_checkany(L, 1);
	size_t len = 0;
	const char *text = luaL_tolstring(L, 1, &len);
	Speech::Braille(std::string_view(text, len));
	return 0;
}

// speech.history([count]) -> table of strings, newest first (default 10)
int L_History(lua_State *L) {
	lua_Integer count = luaL_optinteger(L, 1, 10);
	luaL_argcheck(L, count >= 0, 1, "count can't be negative");
	std::vector<std::string> items = Speech::GetHistory((size_t)count);
	lua_createtable(L, (int)items.size(), 0);
	for (size_t i = 0; i < items.size(); i++) {
		lua_pushlstring(L, items[i].data(), items[i].size());
		lua_rawseti(L, -2, (lua_Integer)i + 1);
	}
	return 1;
}

// speech.last() -> the newest history entry, or "" if nothing was spoken yet
int L_Last(lua_State *L) {
	std::string last = Speech::LastSpoken();
	lua_pushlstring(L, last.data(), last.size());
	return 1;
}

int L_RepeatLast(lua_State *L) {
	Speech::RepeatLast();
	return 0;
}

// speech.copylast() -> true if the newest history entry was put on the clipboard
int L_CopyLast(lua_State *L) {
	lua_pushboolean(L, Speech::CopyLastToClipboard());
	return 1;
}

// speech.copy(text) -> true on success
int L_Copy(lua_State *L) {
	size_t len = 0;
	const char *text = luaL_checklstring(L, 1, &len);
	lua_pushboolean(L, Speech::CopyToClipboard(std::string_view(text, len)));
	return 1;
}

}  // namespace

void RegisterSpeechApi(lua_State *L) {
	lua_register(L, "speak", &L_Speak);
	lua_register(L, "stopSpeech", &L_Stop);

	static const luaL_Reg funcs[] = {
		{ "say", &L_Speak },
		{ "stop", &L_Stop },
		{ "braille", &L_Braille },
		{ "history", &L_History },
		{ "last", &L_Last },
		{ "repeatlast", &L_RepeatLast },
		{ "copylast", &L_CopyLast },
		{ "copy", &L_Copy },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, funcs);
	lua_setglobal(L, "speech");
}

}  // namespace Script
}  // namespace Access
