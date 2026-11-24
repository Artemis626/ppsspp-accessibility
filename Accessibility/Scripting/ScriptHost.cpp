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

#include <atomic>
#include <string>
#include <thread>

#include "Accessibility/Audio/AudioCues.h"
#include "Accessibility/Input/Hotkeys.h"
#include "Accessibility/Log.h"
#include "Accessibility/Scripting/LuaInternal.h"
#include "Accessibility/Scripting/ScriptHost.h"
#include "Accessibility/Speech/Speech.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/TimeUtil.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HW/Display.h"
#include "Core/System.h"

namespace Access {
namespace Script {

namespace {

// A script function that runs for longer than this is treated as stuck
// (usually an endless loop) and stopped with an error, instead of freezing
// the emulator forever.
const double WATCHDOG_SECONDS = 3.0;

// How often (in Lua instructions) the watchdog checks the clock.
const int WATCHDOG_INSTRUCTION_STEP = 100000;

struct LoadedScript {
	lua_State *L = nullptr;
	Path path;
	std::string displayName;          // e.g. "ULUS10041.lua", used in speech and the log.
	Path dataDir;                     // scripts/data/<name>: the only folder the script may write to.
	lua_State *mainThread = nullptr;  // Coroutine running the script's main body.
	int mainThreadRef = LUA_NOREF;
	bool mainFinished = true;
	long long frames = 0;
	bool listeningForFlips = false;
};

Options g_options;
LoadedScript g_script;

bool g_gameRunning = false;
int g_lastFlipCount = 0;
std::string g_gameId;
std::thread::id g_emuThread;
std::atomic<bool> g_reloadRequested{ false };

double g_watchdogStart = 0.0;  // 0 = no script code running right now.

std::string g_lastSpokenError;
double g_lastSpokenErrorTime = -1000.0;

Path ScriptsDirectory() {
	return File::GetExeDirectory() / "scripts";
}

void WatchdogHook(lua_State *L, lua_Debug *) {
	if (g_watchdogStart > 0.0 && time_now_d() - g_watchdogStart > WATCHDOG_SECONDS) {
		g_watchdogStart = 0.0;
		luaL_error(L, "script took longer than %d seconds and was stopped. Check for an endless loop.", (int)WATCHDOG_SECONDS);
	}
}

// Starts the watchdog clock for one call into Lua. If a call starts inside
// another (an execute callback while... nothing else is running, in practice),
// the outer call's clock keeps running.
struct WatchdogScope {
	WatchdogScope() : outer_(g_watchdogStart) {
		if (outer_ == 0.0) {
			g_watchdogStart = time_now_d();
		}
	}
	~WatchdogScope() { g_watchdogStart = outer_; }
	double outer_;
};

int TracebackHandler(lua_State *L) {
	const char *msg = lua_tostring(L, 1);
	if (!msg) {
		msg = luaL_tolstring(L, 1, nullptr);
	}
	luaL_traceback(L, L, msg, 1);
	return 1;
}

void ResumeMainBody() {
	LoadedScript &s = g_script;
	if (!s.L || !s.mainThread || s.mainFinished) {
		return;
	}
	int nresults = 0;
	int status;
	{
		WatchdogScope watchdog;
		status = lua_resume(s.mainThread, s.L, 0, &nresults);
	}
	if (status == LUA_YIELD) {
		// The script called emu.frameadvance(); continue next frame.
		lua_pop(s.mainThread, nresults);
		return;
	}
	s.mainFinished = true;
	if (status == LUA_OK) {
		lua_settop(s.mainThread, 0);
		return;
	}
	const char *msg = lua_tostring(s.mainThread, -1);
	luaL_traceback(s.L, s.mainThread, msg ? msg : "(error object is not a string)", 0);
	std::string trace = lua_tostring(s.L, -1);
	lua_pop(s.L, 1);
	lua_settop(s.mainThread, 0);
	ReportError("main script body (it has stopped; callbacks it registered keep running)", trace);
}

void OnFlip(void *) {
	if (!g_script.L) {
		return;
	}
	g_script.frames++;
	ApplyJoypadInjection();
	// Callbacks first, then the main body: when emu.frameadvance() returns,
	// this frame's onframeend callbacks have already run (BizHawk order).
	RunFrameCallbacks(g_script.L);
	ResumeMainBody();
}

void Unload(const char *reason) {
	LoadedScript &s = g_script;
	if (!s.L) {
		return;
	}
	if (s.listeningForFlips) {
		__DisplayForgetFlip(&OnFlip, nullptr);
	}
	// Order matters: take out breakpoints and injected buttons, and stop
	// sounds, before the Lua state (which owns the callbacks) goes away.
	ClearCallbacks();
	ClearInputState();
	Audio::StopAll();
	Log::Info("Script " + s.displayName + " unloaded (" + reason + ").");
	lua_close(s.L);
	s = LoadedScript();
}

void SetPackagePath(lua_State *L, const Path &scriptDir) {
	// require("name") looks in the script's own folder first, then in the
	// shared scripts/lib folder.
	std::string dir = scriptDir.ToString();
	std::string lib = (ScriptsDirectory() / "lib").ToString();
	std::string path = dir + "/?.lua;" + dir + "/?/init.lua;" + lib + "/?.lua;" + lib + "/?/init.lua";
	lua_getglobal(L, "package");
	lua_pushstring(L, path.c_str());
	lua_setfield(L, -2, "path");
	lua_pop(L, 1);
}

// scripts/data/<name>, where <name> is the script's folder for
// "<name>/main.lua" and the file name without ".lua" otherwise. Kept apart
// from the scripts themselves, so a script can never rewrite code.
Path DataDirectoryFor(const Path &path) {
	std::string name = path.GetFilename();
	if (name == "main.lua" && path.CanNavigateUp()) {
		name = path.NavigateUp().GetFilename();
	} else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lua") == 0) {
		name = name.substr(0, name.size() - 4);
	}
	return ScriptsDirectory() / "data" / name;
}

std::string DisplayNameFor(const Path &path) {
	// "ULUS10041/main.lua" for folder scripts, "ULUS10041.lua" for single files.
	std::string file = path.GetFilename();
	if (file == "main.lua" && path.CanNavigateUp()) {
		return path.NavigateUp().GetFilename() + "/" + file;
	}
	return file;
}

bool Load(const Path &path) {
	std::string displayName = DisplayNameFor(path);

	std::string code;
	if (!File::ReadBinaryFileToString(path, &code)) {
		ReportError("loading " + displayName, "could not read the file " + path.ToString());
		return false;
	}
	// Lua's loadbuffer doesn't skip a UTF-8 byte order mark; Notepad adds one.
	if (code.size() >= 3 && code.compare(0, 3, "\xEF\xBB\xBF") == 0) {
		code.erase(0, 3);
	}

	lua_State *L = luaL_newstate();
	if (!L) {
		ReportError("loading " + displayName, "not enough memory to start Lua");
		return false;
	}
	luaL_openlibs(L);
	// Set before creating the main-body coroutine: new Lua threads copy the hook.
	lua_sethook(L, &WatchdogHook, LUA_MASKCOUNT, WATCHDOG_INSTRUCTION_STEP);
	SetPackagePath(L, path.NavigateUp());
	Path dataDir = DataDirectoryFor(path);
	File::CreateFullPath(dataDir);
	std::string sandboxError;
	if (!ApplySandbox(L, path.NavigateUp().ToString(), dataDir.ToString(), (ScriptsDirectory() / "lib").ToString(), &sandboxError)) {
		// Never run a script without its sandbox.
		lua_close(L);
		ReportError("loading " + displayName + " (the script was not started)", sandboxError);
		return false;
	}

	g_script = LoadedScript();
	g_script.L = L;
	g_script.path = path;
	g_script.displayName = displayName;
	g_script.dataDir = dataDir;
	ResetMemoryApiState();

	// Order matters: later tables add functions to earlier ones (emu, memory).
	RegisterInfoApi(L);
	RegisterMemoryApi(L);
	RegisterSpeechApi(L);
	RegisterEventApi(L);
	RegisterInputApi(L);
	RegisterAudioApi(L);
	RegisterDiscoveryApi(L);

	std::string chunkName = "@" + displayName;
	int status = luaL_loadbufferx(L, code.data(), code.size(), chunkName.c_str(), "t");
	if (status != LUA_OK) {
		// Syntax error: message is like "ULUS10041.lua:12: '=' expected near 'x'".
		std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown load error";
		ClearCallbacks();
		ClearInputState();
		lua_close(L);
		g_script = LoadedScript();
		ReportError("loading " + displayName + " (the script was not started)", msg);
		return false;
	}

	// Run the main body in a coroutine, so emu.frameadvance() can pause it
	// until the next frame, the BizHawk/DeSmuME "while true do ... end" style.
	lua_State *co = lua_newthread(L);
	g_script.mainThreadRef = luaL_ref(L, LUA_REGISTRYINDEX);  // Keeps the coroutine alive.
	g_script.mainThread = co;
	lua_xmove(L, co, 1);  // Move the loaded chunk onto the coroutine.
	g_script.mainFinished = false;

	Log::Info("Script " + displayName + " loaded from " + path.ToString());
	Speech::Speak("Script " + displayName + " loaded", false);

	__DisplayListenFlip(&OnFlip, nullptr);
	g_script.listeningForFlips = true;

	// Run the main body up to its first emu.frameadvance() (or to the end),
	// so setup code and callback registration happen right away.
	ResumeMainBody();
	return true;
}

Path ResolveOverride(const std::string &given) {
	Path asGiven(given);
	if (File::Exists(asGiven)) {
		return asGiven;
	}
	if (!asGiven.IsAbsolute()) {
		Path besideExe = File::GetExeDirectory() / given;
		if (File::Exists(besideExe)) {
			return besideExe;
		}
		Path inScripts = ScriptsDirectory() / given;
		if (File::Exists(inScripts)) {
			return inScripts;
		}
	}
	return Path();
}

Path FindScriptForGame(const std::string &gameId) {
	Path single = ScriptsDirectory() / (gameId + ".lua");
	if (File::Exists(single)) {
		return single;
	}
	Path folder = ScriptsDirectory() / gameId / "main.lua";
	if (File::Exists(folder)) {
		return folder;
	}
	return Path();
}

void LoadScriptForRunningGame() {
	if (g_options.noScript) {
		Log::Info("Scripts are turned off (--no-script).");
		return;
	}
	Path path;
	if (!g_options.scriptOverride.empty()) {
		path = ResolveOverride(g_options.scriptOverride);
		if (path.empty()) {
			ReportError("--script", "script file not found: " + g_options.scriptOverride);
			return;
		}
	} else {
		path = FindScriptForGame(g_gameId);
		if (path.empty()) {
			Log::Info("No script for " + g_gameId + ". Looked for scripts/" + g_gameId + ".lua and scripts/" + g_gameId + "/main.lua.");
			return;
		}
	}
	Load(path);
}

void OnGameStarted() {
	g_gameId = g_paramSFO.GetDiscID();
	std::string title = g_paramSFO.GetValueString("TITLE");
	Log::Info("Game started: " + g_gameId + ", " + title);
	LoadScriptForRunningGame();
}

void HandleReloadRequest() {
	if (!g_reloadRequested.exchange(false)) {
		return;
	}
	Hotkeys::ReloadConfig();
	if (!g_gameRunning) {
		Speech::Speak("No game running, hotkeys reloaded", true);
		return;
	}
	Unload("reload requested");
	Speech::Speak("Reloading script", true);
	LoadScriptForRunningGame();
	if (!g_script.L) {
		Speech::Speak("No script loaded", false);
	}
}

}  // namespace

void ReportError(std::string_view where, std::string_view message) {
	std::string full = "Script error in " + std::string(where) + ": " + std::string(message);
	Log::Error(full);

	// Speak only the first line (the stack trace is in the log).
	std::string firstLine(message.substr(0, message.find('\n')));
	double now = time_now_d();
	bool repeat = firstLine == g_lastSpokenError && now - g_lastSpokenErrorTime < 10.0;
	bool tooSoon = now - g_lastSpokenErrorTime < 2.0;
	if (repeat || tooSoon) {
		Log::Info("(error above not spoken, to avoid flooding speech)");
		return;
	}
	g_lastSpokenError = firstLine;
	g_lastSpokenErrorTime = now;
	Speech::Speak("Script error. " + firstLine, false);
}

bool ProtectedCall(lua_State *L, int nargs, int nresults, std::string_view where) {
	int base = lua_gettop(L) - nargs;  // Index of the function.
	lua_pushcfunction(L, &TracebackHandler);
	lua_insert(L, base);
	int status;
	{
		WatchdogScope watchdog;
		status = lua_pcall(L, nargs, nresults, base);
	}
	lua_remove(L, base);
	if (status != LUA_OK) {
		const char *msg = lua_tostring(L, -1);
		std::string text = msg ? msg : "(error object is not a string)";
		lua_pop(L, 1);
		ReportError(where, text);
		return false;
	}
	return true;
}

lua_State *ActiveState() {
	return g_script.L;
}

std::string ScriptDirectory() {
	return g_script.L ? g_script.path.NavigateUp().ToString() : ScriptsDirectory().ToString();
}

std::string ScriptDataDirectory() {
	return g_script.L ? g_script.dataDir.ToString() : std::string();
}

bool OnEmuThread() {
	return std::this_thread::get_id() == g_emuThread;
}

long long FrameCount() {
	return g_script.frames;
}

std::string RunningGameId() {
	return g_gameRunning ? g_gameId : std::string();
}

void RequestReload() {
	g_reloadRequested = true;
}

void Init(const Options &options) {
	g_options = options;
	g_emuThread = std::this_thread::get_id();
	File::CreateFullPath(ScriptsDirectory());
	if (options.noScript) {
		Log::Info("Started with --no-script: no scripts will be loaded.");
	} else if (!options.scriptOverride.empty()) {
		Log::Info("Started with --script=" + options.scriptOverride);
	}
}

void Shutdown() {
	Unload("emulator closing");
	g_gameRunning = false;
}

void Frame() {
	g_emuThread = std::this_thread::get_id();

	if (!PSP_IsInited()) {
		if (g_gameRunning) {
			Unload("game stopped");
			g_gameRunning = false;
			Log::Info("Game stopped.");
		}
		HandleReloadRequest();
		return;
	}
	// A reset can stop and restart the game between two of our checks; the
	// flip counter starting over tells us that happened.
	int flips = __DisplayGetFlipCount();
	if (g_gameRunning && flips < g_lastFlipCount) {
		Unload("game restarted");
		g_gameRunning = false;
	}
	g_lastFlipCount = flips;
	if (!g_gameRunning) {
		g_gameRunning = true;
		OnGameStarted();
	}
	HandleReloadRequest();

	if (g_script.L) {
		// Host-frame work: runs even while the game is paused.
		ProcessEventQueue(g_script.L);
		RunHotkeyActions(g_script.L);
	}
	Audio::Update();
}

}  // namespace Script
}  // namespace Access
