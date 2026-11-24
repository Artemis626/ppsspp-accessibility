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

// The "audio" table.
//
//   audio.play(file [, {volume=, pan=, pitch=, loop=}]) -> handle
//   audio.tone(frequencyHz, durationMs [, {volume=, pan=, pitch=, wave=}]) -> handle
//        wave: "sine" (default), "square", "triangle", "sawtooth"
//   audio.stop(handle), audio.stopall(), audio.isplaying(handle)
//   audio.setvolume(handle, v), audio.setpan(handle, p), audio.setpitch(handle, p)
//   audio.setmastervolume(v)
//
// Relative file names are looked up in the script's own folder, so a script
// folder can carry its own sounds/ subfolder. A missing or unreadable file is
// reported like a script error (spoken and logged) and returns nil, but does
// not stop the calling function.

#include <cstring>
#include <string>

#include "Accessibility/Audio/AudioCues.h"
#include "Accessibility/Scripting/LuaInternal.h"
#include "Common/File/Path.h"

namespace Access {
namespace Script {

namespace {

float OptField(lua_State *L, int table, const char *name, float def) {
	if (lua_getfield(L, table, name) == LUA_TNIL) {
		lua_pop(L, 1);
		return def;
	}
	int isNumber = 0;
	lua_Number v = lua_tonumberx(L, -1, &isNumber);
	lua_pop(L, 1);
	if (!isNumber) {
		luaL_error(L, "option '%s' must be a number", name);
	}
	return (float)v;
}

// Reads the options table at `index` (may be absent). Raises Lua errors, so
// call it before creating C++ objects.
Audio::SoundOptions ReadOptions(lua_State *L, int index, Audio::Wave *wave) {
	Audio::SoundOptions o;
	if (lua_isnoneornil(L, index)) {
		return o;
	}
	luaL_checktype(L, index, LUA_TTABLE);
	o.volume = OptField(L, index, "volume", 1.0f);
	o.pan = OptField(L, index, "pan", 0.0f);
	o.pitch = OptField(L, index, "pitch", 1.0f);
	lua_getfield(L, index, "loop");
	o.loop = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	if (wave && lua_getfield(L, index, "wave") != LUA_TNIL) {
		const char *w = lua_tostring(L, -1);
		if (!w) {
			luaL_error(L, "option 'wave' must be a string");
		} else if (!strcmp(w, "sine")) {
			*wave = Audio::Wave::Sine;
		} else if (!strcmp(w, "square")) {
			*wave = Audio::Wave::Square;
		} else if (!strcmp(w, "triangle")) {
			*wave = Audio::Wave::Triangle;
		} else if (!strcmp(w, "sawtooth") || !strcmp(w, "saw")) {
			*wave = Audio::Wave::Sawtooth;
		} else {
			luaL_error(L, "unknown wave '%s' (use sine, square, triangle or sawtooth)", w);
		}
	}
	if (wave) {
		lua_pop(L, 1);
	}
	return o;
}

int PushHandleOrReport(lua_State *L, int handle, const char *function, const std::string &error) {
	if (handle > 0) {
		lua_pushinteger(L, handle);
		return 1;
	}
	ReportError(function, error);
	lua_pushnil(L);
	lua_pushlstring(L, error.data(), error.size());
	return 2;
}

int L_Play(lua_State *L) {
	const char *file = luaL_checkstring(L, 1);
	Audio::SoundOptions options = ReadOptions(L, 2, nullptr);

	// No Lua errors below this line (see LuaInternal.h).
	Path path(file);
	if (!path.IsAbsolute()) {
		path = Path(ScriptDirectory()) / file;
	}
	std::string error;
	int handle = Audio::PlayFile(path.ToString(), options, &error);
	return PushHandleOrReport(L, handle, "audio.play", error);
}

int L_Tone(lua_State *L) {
	lua_Number frequency = luaL_checknumber(L, 1);
	lua_Number duration = luaL_checknumber(L, 2);
	luaL_argcheck(L, frequency > 0 && frequency <= 20000, 1, "frequency must be between 1 and 20000 Hz");
	luaL_argcheck(L, duration > 0 && duration <= 60000, 2, "duration must be between 1 and 60000 milliseconds");
	Audio::Wave wave = Audio::Wave::Sine;
	Audio::SoundOptions options = ReadOptions(L, 3, &wave);

	std::string error;
	int handle = Audio::PlayTone((float)frequency, (float)duration, wave, options, &error);
	return PushHandleOrReport(L, handle, "audio.tone", error);
}

int L_Stop(lua_State *L) {
	lua_pushboolean(L, Audio::Stop((int)luaL_checkinteger(L, 1)));
	return 1;
}

int L_StopAll(lua_State *L) {
	Audio::StopAll();
	return 0;
}

int L_IsPlaying(lua_State *L) {
	lua_pushboolean(L, Audio::IsPlaying((int)luaL_checkinteger(L, 1)));
	return 1;
}

int L_SetVolume(lua_State *L) {
	int h = (int)luaL_checkinteger(L, 1);
	lua_pushboolean(L, Audio::SetVolume(h, (float)luaL_checknumber(L, 2)));
	return 1;
}

int L_SetPan(lua_State *L) {
	int h = (int)luaL_checkinteger(L, 1);
	lua_pushboolean(L, Audio::SetPan(h, (float)luaL_checknumber(L, 2)));
	return 1;
}

int L_SetPitch(lua_State *L) {
	int h = (int)luaL_checkinteger(L, 1);
	lua_pushboolean(L, Audio::SetPitch(h, (float)luaL_checknumber(L, 2)));
	return 1;
}

int L_SetMasterVolume(lua_State *L) {
	Audio::SetMasterVolume((float)luaL_checknumber(L, 1));
	return 0;
}

}  // namespace

void RegisterAudioApi(lua_State *L) {
	static const luaL_Reg funcs[] = {
		{ "play", &L_Play },
		{ "tone", &L_Tone },
		{ "stop", &L_Stop },
		{ "stopall", &L_StopAll },
		{ "isplaying", &L_IsPlaying },
		{ "setvolume", &L_SetVolume },
		{ "setpan", &L_SetPan },
		{ "setpitch", &L_SetPitch },
		{ "setmastervolume", &L_SetMasterVolume },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, funcs);
	lua_setglobal(L, "audio");
}

}  // namespace Script
}  // namespace Access
