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

#include <map>
#include <memory>
#include <string>

#include "Accessibility/Audio/AudioCues.h"
#include "Accessibility/Log.h"
#include "Common/Data/Encoding/Utf8.h"

#include "Accessibility/Audio/MiniaudioConfig.h"

namespace Access {
namespace Audio {

namespace {

// Short fades stop clicks at the start and end of tones.
const ma_uint64 TONE_FADE_MS = 5;
// Tones at volume 1 are this loud, which is comfortable next to speech.
const double TONE_AMPLITUDE = 0.4;
const int MAX_SOUNDS = 64;

struct Sound {
	ma_sound sound{};
	bool isTone = false;
	ma_waveform waveform{};  // Only for tones; must outlive `sound`.
	bool loop = false;
};

bool g_engineReady = false;
bool g_engineFailed = false;
ma_engine g_engine;
std::map<int, std::unique_ptr<Sound>> g_sounds;
int g_nextHandle = 1;

bool EnsureEngine(std::string *error) {
	if (g_engineReady) {
		return true;
	}
	if (g_engineFailed) {
		*error = "audio output could not be opened (see the log)";
		return false;
	}
	ma_engine_config config = ma_engine_config_init();
	ma_result result = ma_engine_init(&config, &g_engine);
	if (result != MA_SUCCESS) {
		g_engineFailed = true;
		*error = std::string("audio output could not be opened: ") + ma_result_description(result);
		Log::Error(*error);
		return false;
	}
	g_engineReady = true;
	Log::Info("Audio cues: output opened.");
	return true;
}

float Clamp(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

void ApplyOptions(Sound &s, const SoundOptions &o) {
	// "pan" mode moves a mono sound between the speakers; the default
	// "balance" mode would only turn one side down.
	ma_sound_set_pan_mode(&s.sound, ma_pan_mode_pan);
	ma_sound_set_pan(&s.sound, Clamp(o.pan, -1.0f, 1.0f));
	ma_sound_set_volume(&s.sound, o.volume < 0.0f ? 0.0f : o.volume);
	ma_sound_set_pitch(&s.sound, o.pitch <= 0.0f ? 1.0f : o.pitch);
	ma_sound_set_looping(&s.sound, o.loop ? MA_TRUE : MA_FALSE);
	s.loop = o.loop;
}

bool MakeRoom(std::string *error) {
	if ((int)g_sounds.size() < MAX_SOUNDS) {
		return true;
	}
	Update();
	if ((int)g_sounds.size() < MAX_SOUNDS) {
		return true;
	}
	*error = "too many sounds playing at once (64)";
	return false;
}

int Register(std::unique_ptr<Sound> s) {
	int handle = g_nextHandle++;
	g_sounds[handle] = std::move(s);
	return handle;
}

void Destroy(Sound &s) {
	ma_sound_uninit(&s.sound);
	if (s.isTone) {
		ma_waveform_uninit(&s.waveform);
	}
}

Sound *Find(int handle) {
	auto it = g_sounds.find(handle);
	return it == g_sounds.end() ? nullptr : it->second.get();
}

}  // namespace

int PlayFile(const std::string &pathUtf8, const SoundOptions &options, std::string *error) {
	if (!EnsureEngine(error) || !MakeRoom(error)) {
		return 0;
	}
	auto s = std::make_unique<Sound>();
	// DECODE: decode the whole file into memory once. miniaudio keeps decoded
	// files by name, so playing the same cue again is instant.
	std::wstring widePath = ConvertUTF8ToWString(pathUtf8);
	ma_result result = ma_sound_init_from_file_w(&g_engine, widePath.c_str(),
		MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, &s->sound);
	if (result != MA_SUCCESS) {
		*error = "can't play " + pathUtf8 + ": " + ma_result_description(result);
		return 0;
	}
	ApplyOptions(*s, options);
	ma_sound_start(&s->sound);
	return Register(std::move(s));
}

int PlayTone(float frequencyHz, float durationMs, Wave wave, const SoundOptions &options, std::string *error) {
	if (!EnsureEngine(error) || !MakeRoom(error)) {
		return 0;
	}
	ma_waveform_type type = ma_waveform_type_sine;
	switch (wave) {
	case Wave::Sine: type = ma_waveform_type_sine; break;
	case Wave::Square: type = ma_waveform_type_square; break;
	case Wave::Triangle: type = ma_waveform_type_triangle; break;
	case Wave::Sawtooth: type = ma_waveform_type_sawtooth; break;
	}
	auto s = std::make_unique<Sound>();
	s->isTone = true;
	ma_waveform_config config = ma_waveform_config_init(ma_format_f32, 1, ma_engine_get_sample_rate(&g_engine),
		type, TONE_AMPLITUDE, frequencyHz);
	ma_result result = ma_waveform_init(&config, &s->waveform);
	if (result != MA_SUCCESS) {
		*error = std::string("can't create tone: ") + ma_result_description(result);
		return 0;
	}
	result = ma_sound_init_from_data_source(&g_engine, &s->waveform, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &s->sound);
	if (result != MA_SUCCESS) {
		ma_waveform_uninit(&s->waveform);
		*error = std::string("can't play tone: ") + ma_result_description(result);
		return 0;
	}
	SoundOptions o = options;
	o.loop = false;  // A waveform never ends by itself; the stop time ends it.
	ApplyOptions(*s, o);
	ma_uint64 length = (ma_uint64)(durationMs < 1.0f ? 1.0f : durationMs);
	ma_uint64 fade = length > 4 * TONE_FADE_MS ? TONE_FADE_MS : length / 4;
	ma_sound_set_fade_in_milliseconds(&s->sound, 0.0f, 1.0f, fade);
	ma_sound_set_stop_time_with_fade_in_milliseconds(&s->sound, ma_engine_get_time_in_milliseconds(&g_engine) + length, fade);
	ma_sound_start(&s->sound);
	return Register(std::move(s));
}

bool Stop(int handle) {
	auto it = g_sounds.find(handle);
	if (it == g_sounds.end()) {
		return false;
	}
	Destroy(*it->second);
	g_sounds.erase(it);
	return true;
}

void StopAll() {
	for (auto &kv : g_sounds) {
		Destroy(*kv.second);
	}
	g_sounds.clear();
}

bool IsPlaying(int handle) {
	Sound *s = Find(handle);
	return s && ma_sound_is_playing(&s->sound);
}

bool SetVolume(int handle, float volume) {
	Sound *s = Find(handle);
	if (s) ma_sound_set_volume(&s->sound, volume < 0.0f ? 0.0f : volume);
	return s != nullptr;
}

bool SetPan(int handle, float pan) {
	Sound *s = Find(handle);
	if (s) ma_sound_set_pan(&s->sound, Clamp(pan, -1.0f, 1.0f));
	return s != nullptr;
}

bool SetPitch(int handle, float pitch) {
	Sound *s = Find(handle);
	if (s) ma_sound_set_pitch(&s->sound, pitch <= 0.0f ? 1.0f : pitch);
	return s != nullptr;
}

void SetMasterVolume(float volume) {
	std::string error;
	if (EnsureEngine(&error)) {
		ma_engine_set_volume(&g_engine, volume < 0.0f ? 0.0f : volume);
	}
}

void Update() {
	for (auto it = g_sounds.begin(); it != g_sounds.end();) {
		Sound &s = *it->second;
		bool finished = !s.loop && !ma_sound_is_playing(&s.sound);
		if (finished) {
			Destroy(s);
			it = g_sounds.erase(it);
		} else {
			++it;
		}
	}
}

void Shutdown() {
	StopAll();
	if (g_engineReady) {
		ma_engine_uninit(&g_engine);
		g_engineReady = false;
	}
}

}  // namespace Audio
}  // namespace Access
