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

#include <cstdlib>
#include <cstring>
#include <string>

#include "Accessibility/AccessHost.h"
#include "Accessibility/Audio/AudioCues.h"
#include "Accessibility/Discovery/Ppsspp/DiscoveryHost.h"
#include "Accessibility/Input/Hotkeys.h"
#include "Accessibility/Log.h"
#include "Accessibility/Menu/MenuSpeech.h"
#include "Accessibility/Scripting/ScriptHost.h"
#include "Accessibility/Speech/Speech.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/UI/Screen.h"
#include "Core/System.h"

// Defined in UI/NativeApp.cpp, which has no header for it.
extern ScreenManager *g_screenManager;

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")  // CommandLineToArgvW
#endif

namespace Access {

namespace {

bool g_initialized = false;

// Our options are read straight from the Windows command line, so PPSSPP's
// own argument parser doesn't need to change. PPSSPP ignores options it
// doesn't know that start with "--", so the two parsers never clash.
struct AllOptions {
	Script::Options script;
	DiscoveryHost::Options discovery;
};

// If arg starts with prefix, puts the rest in *value.
bool TakeValue(const std::string &arg, const char *prefix, std::string *value) {
	size_t n = strlen(prefix);
	if (arg.compare(0, n, prefix) != 0) {
		return false;
	}
	*value = arg.substr(n);
	return true;
}

AllOptions ParseCommandLine() {
	AllOptions options;
#if PPSSPP_PLATFORM(WINDOWS)
	int argc = 0;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) {
		return options;
	}
	for (int i = 1; i < argc; i++) {
		std::string arg = ConvertWStringToUTF8(argv[i]);
		std::string value;
		if (TakeValue(arg, "--script=", &options.script.scriptOverride)) {
		} else if (arg == "--no-script") {
			options.script.noScript = true;
		} else if (TakeValue(arg, "--access-harness=", &options.discovery.harnessCase)) {
		} else if (TakeValue(arg, "--access-harness-out=", &options.discovery.harnessOut)) {
		} else if (arg == "--access-no-sidecar") {
			options.discovery.noSidecar = true;
		} else if (TakeValue(arg, "--access-record=", &options.discovery.recordDir)) {
		} else if (TakeValue(arg, "--access-record-frames=", &value)) {
			options.discovery.recordFrames = atoi(value.c_str());
		}
	}
	LocalFree(argv);
#endif
	return options;
}

}  // namespace

void Init() {
	if (g_initialized) {
		return;
	}
	g_initialized = true;
	Log::Init();
	Speech::Init();
	switch (Speech::ActiveBackend()) {
	case Speech::Backend::NVDA:
		Log::Info("Speech: NVDA Controller Client loaded.");
		break;
	case Speech::Backend::None:
		Log::Warn("Speech: no screen reader connection (nvdaControllerClient.dll missing or unusable). Speech goes only to this log.");
		break;
	}
	Hotkeys::Init();
	AllOptions options = ParseCommandLine();
	// Discovery before the script host: when a game starts, the taps are in
	// place before the game's script runs its first line.
	DiscoveryHost::Init(options.discovery);
	Script::Init(options.script);
	MenuSpeech::Reset();
	// Spoken at startup so you know immediately that speech works.
	Speech::Speak("PPSSPP accessibility ready", false);
}

void Frame() {
	if (!g_initialized) {
		return;
	}
	// "In game" = the game screen is on top, with no pause menu or dialog over it.
	// Hotkeys without a modifier only work then.
	Screen *top = g_screenManager ? g_screenManager->topScreen() : nullptr;
	Hotkeys::SetInGame(PSP_IsInited() && top && !strcmp(top->tag(), "Emu"));
	DiscoveryHost::Frame();
	Script::Frame();
	MenuSpeech::Update();
}

bool OnKey(const KeyInput &key) {
	if (!g_initialized) {
		return false;
	}
	return Hotkeys::OnKey(key);
}

void Shutdown() {
	if (!g_initialized) {
		return;
	}
	g_initialized = false;
	Script::Shutdown();
	DiscoveryHost::Shutdown();
	Audio::Shutdown();
	Hotkeys::Shutdown();
	Speech::Shutdown();
	Log::Info("Shut down.");
	Log::Shutdown();
}

}  // namespace Access
