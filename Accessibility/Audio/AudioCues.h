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

// Audio cues for scripts: sound files (WAV, FLAC, MP3) and generated tones,
// each with volume, stereo pan and pitch.
//
// Uses miniaudio on its own output stream, separate from the game's sound:
// cues still play while the game is paused or fast-forwarding, are never
// slowed down by emulation speed, and PPSSPP's audio code is untouched. The
// audio device is opened the first time a sound is played, so scripts that
// never play sounds cost nothing.
//
// Call these from the emulation thread (where Lua runs).

#include <string>

namespace Access {
namespace Audio {

enum class Wave {
	Sine,
	Square,
	Triangle,
	Sawtooth,
};

struct SoundOptions {
	float volume = 1.0f;  // 0 = silent, 1 = normal, above 1 = louder.
	float pan = 0.0f;     // -1 = full left, 0 = centre, 1 = full right.
	float pitch = 1.0f;   // 1 = normal, 2 = one octave up, 0.5 = one octave down.
	bool loop = false;
};

void Shutdown();

// Returns a handle (> 0), or 0 with *error set.
int PlayFile(const std::string &pathUtf8, const SoundOptions &options, std::string *error);
int PlayTone(float frequencyHz, float durationMs, Wave wave, const SoundOptions &options, std::string *error);

bool Stop(int handle);
void StopAll();
bool IsPlaying(int handle);
bool SetVolume(int handle, float volume);
bool SetPan(int handle, float pan);
bool SetPitch(int handle, float pitch);
void SetMasterVolume(float volume);

// Frees sounds that have finished. Once per host frame.
void Update();

}  // namespace Audio
}  // namespace Access
