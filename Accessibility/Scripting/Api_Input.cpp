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

// "input" table (script hotkeys) and "joypad" table (PSP controller).
//
//   input.registerhotkey(action, defaultKey, fn [, description]) -> key name, or nil + error
//   input.unregisterhotkey(action) -> boolean
//   joypad.get() -> {Up=, Down=, Left=, Right=, Cross=, Circle=, Square=, Triangle=,
//                    L=, R=, Start=, Select=, AnalogX=, AnalogY=}
//   joypad.set(buttons [, frames])  -- e.g. joypad.set({Cross=true}, 2)

#include <map>
#include <string>
#include <vector>

#include "Accessibility/Input/Hotkeys.h"
#include "Accessibility/Scripting/LuaInternal.h"
#include "Accessibility/Scripting/ScriptHost.h"
#include "Common/CommonTypes.h"
#include "Core/HLE/sceCtrl.h"

namespace Access {
namespace Script {

namespace {

// ---- hotkeys --------------------------------------------------------------

std::map<std::string, int> g_hotkeyRefs;  // action -> Lua registry reference

int L_RegisterHotkey(lua_State *L) {
	const char *action = luaL_checkstring(L, 1);
	const char *defaultKey = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TFUNCTION);
	const char *description = luaL_optstring(L, 4, "");
	lua_pushvalue(L, 3);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);

	// No Lua errors below this line (see LuaInternal.h).
	std::string usedKey, error;
	bool ok = Hotkeys::RegisterScriptHotkey(RunningGameId(), action, defaultKey, description, &usedKey, &error);
	auto old = g_hotkeyRefs.find(action);
	if (old != g_hotkeyRefs.end()) {
		luaL_unref(L, LUA_REGISTRYINDEX, old->second);
		g_hotkeyRefs.erase(old);
	}
	if (!ok) {
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
		ReportError(std::string("input.registerhotkey '") + action + "'", error);
		lua_pushnil(L);
		lua_pushlstring(L, error.data(), error.size());
		return 2;
	}
	g_hotkeyRefs[action] = ref;
	lua_pushlstring(L, usedKey.data(), usedKey.size());  // "" if turned off in hotkeys.ini
	return 1;
}

int L_UnregisterHotkey(lua_State *L) {
	const char *action = luaL_checkstring(L, 1);
	auto it = g_hotkeyRefs.find(action);
	if (it != g_hotkeyRefs.end()) {
		luaL_unref(L, LUA_REGISTRYINDEX, it->second);
		g_hotkeyRefs.erase(it);
	}
	lua_pushboolean(L, Hotkeys::UnregisterScriptHotkey(action));
	return 1;
}

// ---- joypad ---------------------------------------------------------------

struct ButtonName {
	const char *name;
	u32 bit;
};

const ButtonName BUTTONS[] = {
	{ "Up", CTRL_UP }, { "Down", CTRL_DOWN }, { "Left", CTRL_LEFT }, { "Right", CTRL_RIGHT },
	{ "Cross", CTRL_CROSS }, { "Circle", CTRL_CIRCLE }, { "Square", CTRL_SQUARE }, { "Triangle", CTRL_TRIANGLE },
	{ "L", CTRL_LTRIGGER }, { "R", CTRL_RTRIGGER }, { "Start", CTRL_START }, { "Select", CTRL_SELECT },
};

// What joypad.set is currently holding.
struct Injection {
	u32 pressed = 0;     // Bits we are holding down.
	u32 released = 0;    // Bits we are holding up.
	int framesLeft = 0;
	bool analog = false;
	float x = 0.0f, y = 0.0f;
};
Injection g_injection;

int L_JoypadGet(lua_State *L) {
	u32 buttons = __CtrlPeekButtons();
	float x = 0.0f, y = 0.0f;
	__CtrlPeekAnalog(CTRL_STICK_LEFT, &x, &y);
	lua_createtable(L, 0, 14);
	for (const ButtonName &b : BUTTONS) {
		lua_pushboolean(L, (buttons & b.bit) != 0);
		lua_setfield(L, -2, b.name);
	}
	lua_pushnumber(L, x);
	lua_setfield(L, -2, "AnalogX");
	lua_pushnumber(L, y);
	lua_setfield(L, -2, "AnalogY");
	return 1;
}

// joypad.set(buttons [, frames]): true presses a button, false holds it up,
// a missing button is left alone. AnalogX/AnalogY (-1 to 1) move the left
// stick. Held for `frames` game frames (default 1), then let go.
int L_JoypadSet(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_Integer frames = luaL_optinteger(L, 2, 1);
	luaL_argcheck(L, frames >= 1 && frames <= 3600, 2, "frames must be between 1 and 3600");
	u32 press = 0, release = 0;
	for (const ButtonName &b : BUTTONS) {
		int type = lua_getfield(L, 1, b.name);
		if (type != LUA_TNIL) {
			if (lua_toboolean(L, -1)) {
				press |= b.bit;
			} else {
				release |= b.bit;
			}
		}
		lua_pop(L, 1);
	}
	bool analog = false;
	float x = g_injection.x, y = g_injection.y;
	if (lua_getfield(L, 1, "AnalogX") != LUA_TNIL) {
		x = (float)luaL_checknumber(L, -1);
		analog = true;
	}
	lua_pop(L, 1);
	if (lua_getfield(L, 1, "AnalogY") != LUA_TNIL) {
		y = (float)luaL_checknumber(L, -1);
		analog = true;
	}
	lua_pop(L, 1);

	g_injection.pressed = (g_injection.pressed & ~release) | press;
	g_injection.released = (g_injection.released & ~press) | release;
	if (analog) {
		g_injection.analog = true;
		g_injection.x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
		g_injection.y = y < -1.0f ? -1.0f : (y > 1.0f ? 1.0f : y);
	}
	g_injection.framesLeft = (int)frames;
	// Apply right away too, so the game sees it from this frame on.
	__CtrlUpdateButtons(g_injection.pressed, g_injection.released);
	if (g_injection.analog) {
		__CtrlSetAnalogXY(CTRL_STICK_LEFT, g_injection.x, g_injection.y);
	}
	return 0;
}

void ReleaseInjection() {
	if (g_injection.pressed) {
		__CtrlUpdateButtons(0, g_injection.pressed);
	}
	if (g_injection.analog) {
		__CtrlSetAnalogXY(CTRL_STICK_LEFT, 0.0f, 0.0f);
	}
	g_injection = Injection();
}

}  // namespace

void ApplyJoypadInjection() {
	if (g_injection.framesLeft <= 0) {
		return;
	}
	if (--g_injection.framesLeft == 0) {
		ReleaseInjection();
		return;
	}
	// Re-apply each frame: real controller input may have changed these bits.
	__CtrlUpdateButtons(g_injection.pressed, g_injection.released);
	if (g_injection.analog) {
		__CtrlSetAnalogXY(CTRL_STICK_LEFT, g_injection.x, g_injection.y);
	}
}

void RunHotkeyActions(lua_State *L) {
	for (const std::string &action : Hotkeys::TakePressedScriptActions()) {
		auto it = g_hotkeyRefs.find(action);
		if (it == g_hotkeyRefs.end()) {
			continue;
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
		// A failing hotkey stays registered: it only runs when you press it,
		// so it can't flood speech, and you can fix and reload the script.
		ProtectedCall(L, 0, 0, "hotkey '" + action + "'");
	}
}

void ClearInputState() {
	Hotkeys::ClearScriptHotkeys();
	g_hotkeyRefs.clear();  // References die with the Lua state.
	if (g_injection.framesLeft > 0) {
		ReleaseInjection();
	}
	g_injection = Injection();
}

void RegisterInputApi(lua_State *L) {
	static const luaL_Reg inputFuncs[] = {
		{ "registerhotkey", &L_RegisterHotkey },
		{ "unregisterhotkey", &L_UnregisterHotkey },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, inputFuncs);
	lua_setglobal(L, "input");

	static const luaL_Reg joypadFuncs[] = {
		{ "get", &L_JoypadGet },
		{ "set", &L_JoypadSet },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, joypadFuncs);
	lua_setglobal(L, "joypad");
}

}  // namespace Script
}  // namespace Access
