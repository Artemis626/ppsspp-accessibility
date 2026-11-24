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

// Shared between the script host and the Lua API files. Not for use outside
// Accessibility/Scripting.
//
// The API is written against Lua's plain C API (not sol2) so that argument
// errors give modders Lua's standard messages, for example
//   bad argument #1 to 'read_u8' (number expected, got nil)
//
// IMPORTANT for anyone editing the API files: Lua reports errors with
// longjmp. Call every luaL_check* / luaL_error BEFORE creating any C++ object
// that has a destructor (std::string, std::vector...), or that object's
// destructor may never run.

#include <string>
#include <string_view>

#include "ext/lua/lua.hpp"

namespace Access {
namespace Script {

// ---- ScriptHost.cpp ----

// Log the full message (with stack trace) and speak a short version.
// Rate-limited so a broken per-frame function can't flood speech.
void ReportError(std::string_view where, std::string_view message);

// lua_pcall with a stack trace and the runaway-script watchdog.
// Stack before: function, args. On failure the error is reported and false is returned.
bool ProtectedCall(lua_State *L, int nargs, int nresults, std::string_view where);

// The running script's Lua state, or nullptr when no script is loaded.
lua_State *ActiveState();
// Folder of the running script (UTF-8, forward slashes), used to resolve
// relative file names such as sound files.
std::string ScriptDirectory();
// True on the thread that runs the emulator and all Lua code.
bool OnEmuThread();
// Frames presented since the script was loaded.
long long FrameCount();

// ---- Registration: each adds global tables/functions to L ----
void RegisterInfoApi(lua_State *L);    // gameinfo, emu (counters, frameadvance, registers), console, print
void RegisterMemoryApi(lua_State *L);  // memory (reads and writes)
void RegisterSpeechApi(lua_State *L);  // speak, stopSpeech, speech
void RegisterEventApi(lua_State *L);   // event, emu.registerafter
void RegisterInputApi(lua_State *L);   // input (hotkeys), joypad
void RegisterAudioApi(lua_State *L);   // audio
void RegisterDiscoveryApi(lua_State *L);  // text, discover

// Takes away what could harm the computer (programs, file deletion, native
// code, bytecode, debug), allows writing only in the script's data folder and
// reading only there and in the script's folder. Run after luaL_openlibs and
// the package path. See Sandbox.cpp.
bool ApplySandbox(lua_State *L, const std::string &scriptDir, const std::string &dataDir, const std::string &libDir, std::string *error);

// The running script's data folder, scripts/data/<name> (see Sandbox.cpp).
std::string ScriptDataDirectory();

// ---- Per-frame and per-host-frame work ----
void RunFrameCallbacks(lua_State *L);   // Api_Event.cpp: event.onframeend, on each game frame
void ProcessEventQueue(lua_State *L);   // Api_Event.cpp: memory events from other threads; sync breakpoints
void RunHotkeyActions(lua_State *L);    // Api_Input.cpp: script hotkeys pressed since last time
void ApplyJoypadInjection();            // Api_Input.cpp: joypad.set, on each game frame

// ---- Cleanup when a script is unloaded (before lua_close) ----
void ResetMemoryApiState();
void ClearCallbacks();
void ClearInputState();

}  // namespace Script
}  // namespace Access
