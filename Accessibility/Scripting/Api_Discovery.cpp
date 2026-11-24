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

// The Discovery layer's first Lua functions (Phase D1). The full API, with
// the game map and text events, is Phase D4.
//
//   text.onscreen() -> array of runs, in reading order. Each run is a table:
//       text        the text (UTF-8)
//       source      "system_dialog", "system_font" or "font_call_stream"
//       confidence  0..1
//       id          stays the same while the run stays on screen
//       selected    true if a system dialog highlights it
//       x, y, w, h  place on the PSP screen (480 x 272); absent if unknown
//       code        game code that asked for the text (0 if unknown)
//       buffer      memory the glyph images were drawn from (0 if none)
//   text.locate(string [, max]) -> array of {addr=, encoding=}: where the
//       string is stored in game memory, as "utf8" or "utf16le"
//   discover.survey() -> {verdict=string, counters={name=count}, samples={...}}:
//       what the taps have seen in this game so far
//   discover.record([seconds [, note]]) -> folder, or nil and a reason:
//       records everything the layer sees for that long (default 5, at most
//       120 seconds) into a new folder under accessibility/logs/recordings
//   discover.stoprecording() -> true if a recording was running

#include <string>
#include <vector>

#include "Accessibility/Discovery/Core/Engine.h"
#include "Accessibility/Discovery/Ppsspp/DiscoveryHost.h"
#include "Accessibility/Scripting/LuaInternal.h"

namespace Access {
namespace Script {

namespace {

void PushString(lua_State *L, const std::string &s) {
	lua_pushlstring(L, s.data(), s.size());
}

int L_OnScreen(lua_State *L) {
	::Discovery::Engine *engine = DiscoveryHost::GetEngine();
	lua_newtable(L);
	if (!engine) {
		return 1;
	}
	int n = 0;
	for (const ::Discovery::TextRun &run : engine->Screen().Runs()) {
		lua_newtable(L);
		PushString(L, run.text);
		lua_setfield(L, -2, "text");
		lua_pushstring(L, ::Discovery::TextSourceName(run.source));
		lua_setfield(L, -2, "source");
		lua_pushnumber(L, run.confidence);
		lua_setfield(L, -2, "confidence");
		lua_pushinteger(L, run.id);
		lua_setfield(L, -2, "id");
		lua_pushboolean(L, run.selected);
		lua_setfield(L, -2, "selected");
		if (run.hasRect) {
			lua_pushnumber(L, run.rect.x0);
			lua_setfield(L, -2, "x");
			lua_pushnumber(L, run.rect.y0);
			lua_setfield(L, -2, "y");
			lua_pushnumber(L, run.rect.Width());
			lua_setfield(L, -2, "w");
			lua_pushnumber(L, run.rect.Height());
			lua_setfield(L, -2, "h");
		}
		lua_pushinteger(L, run.codePc);
		lua_setfield(L, -2, "code");
		lua_pushinteger(L, run.bufferAddr);
		lua_setfield(L, -2, "buffer");
		lua_rawseti(L, -2, ++n);
	}
	return 1;
}

int L_Locate(lua_State *L) {
	// Lua errors first, C++ objects after (see LuaInternal.h).
	size_t len = 0;
	const char *text = luaL_checklstring(L, 1, &len);
	lua_Integer maxHits = luaL_optinteger(L, 2, 16);
	luaL_argcheck(L, maxHits >= 1 && maxHits <= 1000, 2, "max must be between 1 and 1000");

	::Discovery::Engine *engine = DiscoveryHost::GetEngine();
	lua_newtable(L);
	if (!engine) {
		return 1;
	}
	std::vector<::Discovery::TextLocation> hits = engine->Locate(std::string_view(text, len), (size_t)maxHits);
	for (size_t i = 0; i < hits.size(); i++) {
		lua_newtable(L);
		lua_pushinteger(L, hits[i].addr);
		lua_setfield(L, -2, "addr");
		PushString(L, hits[i].encoding);
		lua_setfield(L, -2, "encoding");
		lua_rawseti(L, -2, (lua_Integer)i + 1);
	}
	return 1;
}

int L_Survey(lua_State *L) {
	::Discovery::Engine *engine = DiscoveryHost::GetEngine();
	lua_newtable(L);
	if (!engine) {
		lua_pushstring(L, "No game running.");
		lua_setfield(L, -2, "verdict");
		return 1;
	}
	const ::Discovery::Survey &survey = engine->GetSurvey();
	PushString(L, survey.Verdict());
	lua_setfield(L, -2, "verdict");
	lua_newtable(L);
	for (const auto &kv : survey.Counters()) {
		lua_pushinteger(L, (lua_Integer)kv.second);
		lua_setfield(L, -2, kv.first.c_str());
	}
	lua_setfield(L, -2, "counters");
	lua_newtable(L);
	int n = 0;
	for (const std::string &sample : survey.Samples()) {
		PushString(L, sample);
		lua_rawseti(L, -2, ++n);
	}
	lua_setfield(L, -2, "samples");
	return 1;
}

// discover.sprites() -> every textured rectangle of the last frame, in draw
// order: {tex=, clut=, tw=, th=, fmt=, u0=, v0=, u1=, v1=, x=, y=, w=, h=, target=, data=}
int L_Sprites(lua_State *L) {
	::Discovery::Engine *engine = DiscoveryHost::GetEngine();
	lua_newtable(L);
	if (!engine) {
		return 1;
	}
	int n = 0;
	for (const ::Discovery::SpriteDraw &d : engine->LastSprites()) {
		lua_createtable(L, 0, 15);
		lua_pushinteger(L, d.texAddr); lua_setfield(L, -2, "tex");
		lua_pushinteger(L, d.clutAddr); lua_setfield(L, -2, "clut");
		lua_pushinteger(L, d.texW); lua_setfield(L, -2, "tw");
		lua_pushinteger(L, d.texH); lua_setfield(L, -2, "th");
		lua_pushinteger(L, d.texFormat); lua_setfield(L, -2, "fmt");
		lua_pushnumber(L, d.u0); lua_setfield(L, -2, "u0");
		lua_pushnumber(L, d.v0); lua_setfield(L, -2, "v0");
		lua_pushnumber(L, d.u1); lua_setfield(L, -2, "u1");
		lua_pushnumber(L, d.v1); lua_setfield(L, -2, "v1");
		lua_pushnumber(L, d.screen.x0); lua_setfield(L, -2, "x");
		lua_pushnumber(L, d.screen.y0); lua_setfield(L, -2, "y");
		lua_pushnumber(L, d.screen.Width()); lua_setfield(L, -2, "w");
		lua_pushnumber(L, d.screen.Height()); lua_setfield(L, -2, "h");
		lua_pushinteger(L, d.target); lua_setfield(L, -2, "target");
		lua_pushinteger(L, d.dataAddr); lua_setfield(L, -2, "data");
		lua_rawseti(L, -2, ++n);
	}
	return 1;
}

// discover.report() -> the inspection report as a string; it is also written
// to accessibility/logs/discovery-report.txt
int L_Report(lua_State *L) {
	if (!DiscoveryHost::GetEngine()) {
		lua_pushstring(L, "No game running.");
		return 1;
	}
	PushString(L, DiscoveryHost::WriteReport());
	return 1;
}

int L_Record(lua_State *L) {
	// Lua errors first, C++ objects after (see LuaInternal.h).
	lua_Number seconds = luaL_optnumber(L, 1, 5.0);
	size_t len = 0;
	const char *note = luaL_optlstring(L, 2, "started by the script", &len);
	luaL_argcheck(L, seconds > 0 && seconds <= 120, 1, "seconds must be more than 0 and at most 120");

	int frames = (int)(seconds * 60.0 + 0.5);
	std::string folder = DiscoveryHost::StartRecording(frames < 1 ? 1 : frames, std::string(note, len));
	if (folder.empty()) {
		lua_pushnil(L);
		lua_pushstring(L, DiscoveryHost::GetEngine() ? "a recording is already running" : "no game running");
		return 2;
	}
	PushString(L, folder);
	return 1;
}

int L_StopRecording(lua_State *L) {
	lua_pushboolean(L, DiscoveryHost::StopRecording());
	return 1;
}

}  // namespace

void RegisterDiscoveryApi(lua_State *L) {
	static const luaL_Reg textFuncs[] = {
		{ "onscreen", &L_OnScreen },
		{ "locate", &L_Locate },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, textFuncs);
	lua_setglobal(L, "text");

	static const luaL_Reg discoverFuncs[] = {
		{ "survey", &L_Survey },
		{ "sprites", &L_Sprites },
		{ "report", &L_Report },
		{ "record", &L_Record },
		{ "stoprecording", &L_StopRecording },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, discoverFuncs);
	lua_setglobal(L, "discover");
}

}  // namespace Script
}  // namespace Access
