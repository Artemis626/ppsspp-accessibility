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

// The PPSSPP adapter of the Discovery layer: owns the core engine, installs
// the taps while a game runs, speaks system dialogs, and runs harness cases.
//
// The platform-independent part is in ../Core and knows nothing about this
// folder. See docs/Discovery-Phase0-Design.md.

#include <string>

namespace Discovery {
class Engine;
}

namespace Access {
namespace DiscoveryHost {

struct Options {
	std::string harnessCase;    // --access-harness=<case.ini>: run one harness case, write the result, exit.
	std::string harnessOut;     // --access-harness-out=<file>: where the result goes.
	bool noSidecar = false;     // --access-no-sidecar: ignore the layer's own file beside a save state ("cold" run).
	std::string recordDir;      // --access-record=<folder>: when the state given with --state= has loaded, record into this folder, then exit.
	int recordFrames = 300;     // --access-record-frames=<n>: how many frames that recording lasts.
};

void Init(const Options &options);
void Shutdown();

// Once per host frame (emulation thread).
void Frame();

// The engine, or nullptr while no game runs. Emulation thread only.
::Discovery::Engine *GetEngine();

// Hotkey actions. Thread-safe: they only set a flag; the work is done on the
// emulation thread in Frame.
void RequestReadScreen();
void RequestSurvey();
void RequestToggleAutoRead();
void RequestReport();
// Starts a ten-second recording, or stops the one that is running.
void RequestRecord();

// Writes the inspection report to accessibility/logs/discovery-report.txt and
// returns its text. Emulation thread only.
std::string WriteReport();

// Starts a recording of everything the layer sees (see Core/Recording.h) into
// a new folder under accessibility/logs/recordings, for `frames` frames.
// Returns the folder, or an empty string if no game runs or a recording is
// already running. Emulation thread only.
std::string StartRecording(int frames, const std::string &note);
// Ends the running recording now. False if none was running.
bool StopRecording();

// ---- used by Taps.cpp ----
// Called after a save state was loaded or saved (see Core/SaveState.h).
void OnStateFile(const std::string &path, bool loaded);
// True while the GE tap should also count draws for the survey.
bool SurveySampling();

}  // namespace DiscoveryHost
}  // namespace Access
