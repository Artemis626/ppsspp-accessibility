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

// "gameinfo", "emu" and "console" tables, plus print().

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "Accessibility/Log.h"
#include "Accessibility/Scripting/LuaInternal.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HW/Display.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"

namespace Access {
namespace Script {

namespace {

void PushStdString(lua_State *L, const std::string &s) {
	lua_pushlstring(L, s.data(), s.size());
}

// gameinfo.getid() -> "ULUS10041"
int L_GetId(lua_State *L) {
	PushStdString(L, g_paramSFO.GetDiscID());
	return 1;
}

// gameinfo.gettitle() -> the game's title from PARAM.SFO
int L_GetTitle(lua_State *L) {
	PushStdString(L, g_paramSFO.GetValueString("TITLE"));
	return 1;
}

// gameinfo.getversion() -> disc version, e.g. "1.01"
int L_GetVersion(lua_State *L) {
	PushStdString(L, g_paramSFO.GetValueString("DISC_VERSION"));
	return 1;
}

// emu.framecount() -> frames the game has presented since the script loaded
int L_FrameCount(lua_State *L) {
	lua_pushinteger(L, FrameCount());
	return 1;
}

// emu.vblankcount() -> vertical blanks since the game started; always 60 per
// second, whatever the game's frame rate, so it works as a steady clock.
int L_VblankCount(lua_State *L) {
	lua_pushinteger(L, __DisplayGetNumVblanks());
	return 1;
}

// emu.getscriptdir() -> the running script's folder, ending in "/".
// Lua's io.open uses the emulator's working folder for relative names, so
// scripts open their own data files with io.open(emu.getscriptdir() .. "data.txt").
int L_GetScriptDir(lua_State *L) {
	std::string dir = ScriptDirectory();
	if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
		dir += '/';
	}
	lua_pushlstring(L, dir.data(), dir.size());
	return 1;
}

// emu.frameadvance() - pause the script's main body until the next frame.
int L_FrameAdvance(lua_State *L) {
	if (!lua_isyieldable(L)) {
		return luaL_error(L, "emu.frameadvance() can only be used in the main body of a script, not inside a callback");
	}
	return lua_yield(L, 0);
}

// ---- CPU registers ----
//
// Names: zero at v0 v1 a0-a3 t0-t9 s0-s7 k0 k1 gp sp fp (also s8) ra,
// r0-r31, pc, hi, lo, and f0-f31 (floating point). Inside an execute
// callback these are the values at the moment that instruction is about to
// run: for a function's first instruction, a0-a3 are its first four arguments.

const char *const GPR_NAMES[32] = {
	"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
	"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
	"t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
};

enum class RegType { Gpr, Fpr, Pc, Hi, Lo, Unknown };

RegType ParseRegister(const char *name, int *index) {
	for (int i = 0; i < 32; i++) {
		if (!strcmp(name, GPR_NAMES[i])) {
			*index = i;
			return RegType::Gpr;
		}
	}
	if (!strcmp(name, "s8")) {
		*index = 30;
		return RegType::Gpr;
	}
	if (!strcmp(name, "pc")) return RegType::Pc;
	if (!strcmp(name, "hi")) return RegType::Hi;
	if (!strcmp(name, "lo")) return RegType::Lo;
	if ((name[0] == 'r' || name[0] == 'f') && name[1] >= '0' && name[1] <= '9') {
		char *end = nullptr;
		long n = strtol(name + 1, &end, 10);
		if (*end == '\0' && n >= 0 && n < 32) {
			*index = (int)n;
			return name[0] == 'r' ? RegType::Gpr : RegType::Fpr;
		}
	}
	return RegType::Unknown;
}

// emu.getregister(name) -> number (unsigned 32-bit for integer registers)
int L_GetRegister(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	int index = 0;
	switch (ParseRegister(name, &index)) {
	case RegType::Gpr: lua_pushinteger(L, currentMIPS->r[index]); break;
	case RegType::Fpr: lua_pushnumber(L, currentMIPS->f[index]); break;
	case RegType::Pc: lua_pushinteger(L, currentMIPS->pc); break;
	case RegType::Hi: lua_pushinteger(L, currentMIPS->hi); break;
	case RegType::Lo: lua_pushinteger(L, currentMIPS->lo); break;
	default: return luaL_argerror(L, 1, "unknown register name (examples: a0, v0, sp, ra, pc, f12)");
	}
	return 1;
}

// emu.setregister(name, value). pc can't be set: from inside emulation the
// CPU would not jump there.
int L_SetRegister(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	int index = 0;
	RegType type = ParseRegister(name, &index);
	switch (type) {
	case RegType::Gpr:
		if (index != 0) {  // r0 is always zero on MIPS.
			currentMIPS->r[index] = (u32)luaL_checkinteger(L, 2);
		}
		break;
	case RegType::Fpr: currentMIPS->f[index] = (float)luaL_checknumber(L, 2); break;
	case RegType::Hi: currentMIPS->hi = (u32)luaL_checkinteger(L, 2); break;
	case RegType::Lo: currentMIPS->lo = (u32)luaL_checkinteger(L, 2); break;
	case RegType::Pc: return luaL_argerror(L, 1, "pc can't be changed by scripts");
	default: return luaL_argerror(L, 1, "unknown register name (examples: a0, v0, sp, ra, pc, f12)");
	}
	return 0;
}

// print(...) and console.log(...): values separated by tabs, like Lua's print,
// written to accessibility/logs/access.log. Not spoken: use speak() for that.
int L_Print(lua_State *L) {
	int n = lua_gettop(L);
	luaL_Buffer buffer;
	luaL_buffinit(L, &buffer);
	for (int i = 1; i <= n; i++) {
		if (i > 1) {
			luaL_addchar(&buffer, '\t');
		}
		luaL_tolstring(L, i, nullptr);
		luaL_addvalue(&buffer);
	}
	luaL_pushresult(&buffer);
	size_t len = 0;
	const char *text = lua_tolstring(L, -1, &len);
	Log::Script(std::string_view(text, len));
	return 0;
}

}  // namespace

void RegisterInfoApi(lua_State *L) {
	static const luaL_Reg gameinfoFuncs[] = {
		{ "getid", &L_GetId },
		{ "gettitle", &L_GetTitle },
		{ "getromname", &L_GetTitle },  // BizHawk name
		{ "getversion", &L_GetVersion },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, gameinfoFuncs);
	lua_setglobal(L, "gameinfo");

	static const luaL_Reg emuFuncs[] = {
		{ "framecount", &L_FrameCount },
		{ "vblankcount", &L_VblankCount },
		{ "frameadvance", &L_FrameAdvance },
		{ "getscriptdir", &L_GetScriptDir },
		{ "getregister", &L_GetRegister },
		{ "setregister", &L_SetRegister },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, emuFuncs);
	lua_setglobal(L, "emu");

	static const luaL_Reg consoleFuncs[] = {
		{ "log", &L_Print },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, consoleFuncs);
	lua_setglobal(L, "console");

	lua_register(L, "print", &L_Print);
}

}  // namespace Script
}  // namespace Access
