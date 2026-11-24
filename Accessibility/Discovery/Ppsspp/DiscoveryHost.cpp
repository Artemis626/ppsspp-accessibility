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

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "Accessibility/Discovery/Core/Engine.h"
#include "Accessibility/Discovery/Core/Harness.h"
#include "Accessibility/Discovery/Core/TextUtil.h"
#include "Accessibility/Discovery/Ppsspp/DiscoveryHost.h"
#include "Accessibility/Discovery/Ppsspp/Taps.h"
#include "Accessibility/Log.h"
#include "Accessibility/Speech/Speech.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/System/Request.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HW/Display.h"
#include "Core/MemMap.h"
#include "Core/System.h"

namespace Access {
namespace DiscoveryHost {

namespace {

// How many frames the GE tap counts draws for the survey: at game start, and
// again each time the survey is asked for.
const int SURVEY_SAMPLE_FRAMES = 600;
// A harness run gives up if the state has not loaded after this many host frames.
const int HARNESS_LOAD_TIMEOUT_FRAMES = 3600;

// The PSP seen through the core's Machine interface.
class PspMachine : public ::Discovery::Machine {
public:
	std::string GameId() const override {
		return g_paramSFO.GetDiscID();
	}
	std::vector<::Discovery::MemoryRegion> Regions() const override {
		std::vector<::Discovery::MemoryRegion> regions;
		if (Memory::base) {
			u32 start = PSP_GetUserMemoryBase();
			regions.push_back(::Discovery::MemoryRegion{ start, PSP_GetUserMemoryEnd() - start, "user RAM" });
		}
		return regions;
	}
	bool Read(u32 addr, void *out, u32 size) const override {
		if (!Memory::IsValidRange(addr, size)) {
			return false;
		}
		memcpy(out, Memory::GetPointerUnchecked(addr), size);
		return true;
	}
	const u8 *View(u32 addr, u32 size) const override {
		return Memory::IsValidRange(addr, size) ? Memory::GetPointerUnchecked(addr) : nullptr;
	}
};

struct HarnessRun {
	bool active = false;
	::Discovery::HarnessCase test;
	bool stateLoaded = false;   // Or no state is needed.
	bool sidecarLoaded = false;
	int framesSinceLoad = 0;
	int hostFrames = 0;
	bool done = false;
};

Options g_options;
PspMachine g_machine;
::Discovery::Engine g_engine(&g_machine);

bool g_gameRunning = false;
int g_lastFlipCount = 0;
bool g_listening = false;
int g_surveyFramesLeft = 0;
// What is spoken without being asked: 0 = nothing, 1 = system dialogs,
// 2 = system dialogs and the game's own text.
int g_autoRead = 2;

// For speaking game text: a run is spoken once its text has stopped changing,
// so a line typed out letter by letter is read once, complete, and a counter
// that never stops is not read at all.
struct SpokenRun {
	std::string text;     // What the run said last frame.
	std::string spoken;   // What was last spoken for it.
	int since = 0;        // Frame its text last changed.
	int seen = 0;         // Frame it was last on screen.
};
std::map<unsigned, SpokenRun> g_spokenRuns;
const int STABLE_FRAMES = 20;
const size_t MAX_RUNS_PER_BURST = 12;
HarnessRun g_harness;

std::atomic<bool> g_wantReadScreen{ false };
std::atomic<bool> g_wantSurvey{ false };
std::atomic<bool> g_wantToggleAutoRead{ false };
std::atomic<bool> g_wantReport{ false };
std::atomic<bool> g_wantRecord{ false };

// ---- recording -------------------------------------------------------------

// Writes a recording's files into one folder.
class FolderSink : public ::Discovery::RecordingSink {
public:
	void SetFolder(const Path &folder) { folder_ = folder; failed_ = false; }
	const Path &Folder() const { return folder_; }
	bool Failed() const { return failed_; }
	bool Append(const std::string &file, const void *data, size_t size) override {
		FILE *f = File::OpenCFile(folder_ / file, "ab");
		if (!f) {
			failed_ = true;
			return false;
		}
		bool ok = size == 0 || fwrite(data, 1, size, f) == size;
		fclose(f);
		failed_ = failed_ || !ok;
		return ok;
	}

private:
	Path folder_;
	bool failed_ = false;
};

FolderSink g_recordSink;
bool g_wasRecording = false;
bool g_recordSpoken = false;       // The running recording was started by a person: say when it ends.
bool g_recordRunStarted = false;   // --access-record: the recording has begun.
int g_recordRunHostFrames = 0;
// A recording started with the hotkey lasts this long unless stopped earlier.
const int HOTKEY_RECORD_FRAMES = 600;

bool BeginRecording(const Path &folder, int frames, const std::string &note) {
	if (g_engine.IsRecording()) {
		return false;
	}
	File::CreateFullPath(folder);
	g_recordSink.SetFolder(folder);
	if (!g_engine.StartRecording(&g_recordSink, frames, note)) {
		return false;
	}
	g_wasRecording = true;
	Log::Info("Discovery: recording " + std::to_string(frames) + " frames into " + folder.ToString());
	return true;
}

void OnRecordingEnded() {
	g_wasRecording = false;
	std::string where = g_recordSink.Folder().ToString();
	int frames = g_engine.RecordedFrames();
	if (g_recordSink.Failed()) {
		Log::Error("Discovery: the recording in " + where + " is incomplete: a file could not be written.");
	} else {
		Log::Info("Discovery: recording written, " + std::to_string(frames) + " frames, in " + where);
	}
	if (g_recordSpoken) {
		g_recordSpoken = false;
		Speech::Speak(g_recordSink.Failed() ? "Recording failed: a file could not be written."
			: "Recording written, " + std::to_string(frames) + " frames.", true);
	}
	if (!g_options.recordDir.empty() && g_recordRunStarted) {
		Log::Info("RECORD DONE");
		System_ExitApp();
	}
}

std::string SidecarPath(const std::string &statePath) {
	return statePath + ".access";
}

// ---- speech ----------------------------------------------------------------

// Speaks what changed in system dialogs. Those are modal and their text is
// certain, so they are spoken without being asked. Game text is not spoken
// automatically: what to say and when is the mod's decision (it gets the same
// information from text.onscreen in Lua, or the read-screen hotkey).
void SpeakDialogChanges(const std::vector<::Discovery::TextChange> &changes) {
	std::vector<std::string> pieces;
	int singleCharacters = 0;
	for (const ::Discovery::TextChange &c : changes) {
		if (c.run.source != ::Discovery::TextSource::SystemDialog || c.kind == ::Discovery::ChangeKind::Disappeared) {
			continue;
		}
		if (::Discovery::DecodeUtf8(c.run.text).size() == 1) {
			singleCharacters++;
		}
		pieces.push_back(c.run.selected ? c.run.text + ", selected" : c.run.text);
	}
	if (pieces.empty()) {
		return;
	}
	// The on-screen keyboard draws every key as its own text. Reading out the
	// whole grid helps nobody; say what it is instead.
	if (singleCharacters > 12) {
		Speech::Speak("On-screen keyboard", false);
		return;
	}
	std::string text;
	for (const std::string &p : pieces) {
		text += (text.empty() ? "" : ". ") + p;
	}
	Speech::Speak(text, false);
}

void SpeakStableGameText() {
	int frame = g_engine.Frame();
	std::string text;
	size_t pieces = 0;
	for (const ::Discovery::TextRun &run : g_engine.Screen().Runs()) {
		if (run.source == ::Discovery::TextSource::SystemDialog || run.confidence < 0.6f) {
			continue;
		}
		SpokenRun &s = g_spokenRuns[run.id];
		s.seen = frame;
		if (s.text != run.text) {
			s.text = run.text;
			s.since = frame;
		} else if (frame - s.since >= STABLE_FRAMES && s.spoken != run.text) {
			s.spoken = run.text;
			if (pieces++ < MAX_RUNS_PER_BURST) {
				text += (text.empty() ? "" : ". ") + run.text;
			}
		}
	}
	for (auto it = g_spokenRuns.begin(); it != g_spokenRuns.end();) {
		it = frame - it->second.seen > 120 ? g_spokenRuns.erase(it) : std::next(it);
	}
	if (!text.empty()) {
		Speech::Speak(text, false);
	}
}

// ---- the per-game profile ---------------------------------------------------

// What the layer learned about a game is kept in accessibility/profiles/<game>.ini
// beside the exe, and read again when the game starts.
std::string g_profileGame;       // The game whose profile is loaded ("" = none yet).
u64 g_profileSaved = 0;          // The engine's profile stamp when the file was last written.
int g_profileFrames = 0;
const int PROFILE_SAVE_FRAMES = 600;

Path ProfilePath(const std::string &game) {
	return File::GetExeDirectory() / "accessibility" / "profiles" / (game + ".ini");
}

// Measuring runs (the harness, a recording pass) must give the same result
// every time: they neither read nor write profiles.
bool ProfilesInUse() {
	return g_options.harnessCase.empty() && g_options.recordDir.empty();
}

void SaveProfile() {
	if (g_profileGame.empty() || g_engine.ProfileStamp() == g_profileSaved) {
		return;
	}
	Path out = ProfilePath(g_profileGame);
	File::CreateFullPath(out.NavigateUp());
	if (!File::WriteStringToFile(true, g_engine.ExportProfile(), out)) {
		Log::Warn("Could not write " + out.ToString());
	}
	g_profileSaved = g_engine.ProfileStamp();
}

void ProfileTick() {
	if (!ProfilesInUse()) {
		return;
	}
	if (g_profileGame.empty()) {
		std::string game = g_machine.GameId();
		if (game.empty()) {
			return;
		}
		g_profileGame = game;
		std::string text;
		if (File::ReadTextFileToString(ProfilePath(game), &text)) {
			g_engine.ImportProfile(text);
			Log::Info("Discovery: profile read from " + ProfilePath(game).ToString());
		}
		g_profileSaved = g_engine.ProfileStamp();
		g_profileFrames = 0;
	} else if (++g_profileFrames >= PROFILE_SAVE_FRAMES) {
		g_profileFrames = 0;
		SaveProfile();
	}
}

// Speaks the selected item when the selection moves. This is the one piece
// of game text worth saying unasked and at once: it answers the key press.
std::string g_spokenSelection;
void SpeakSelection() {
	const ::Discovery::Selection &selection = g_engine.Selected();
	if (!selection.known) {
		g_spokenSelection.clear();
		return;
	}
	if (selection.text != g_spokenSelection) {
		g_spokenSelection = selection.text;
		Speech::Speak(selection.text, true);
	}
}

void ReadScreen() {
	const std::vector<::Discovery::TextRun> &runs = g_engine.Screen().Runs();
	if (runs.empty()) {
		Speech::SpeakTransient("No text found on screen", true);
		return;
	}
	std::string text;
	for (const ::Discovery::TextRun &run : runs) {
		text += (text.empty() ? "" : ". ") + run.text;
		if (run.selected) {
			text += ", selected";
		}
	}
	Speech::Speak(text, true);
}

void SpeakSurvey() {
	Log::Info("Discovery survey for " + g_machine.GameId() + ":\n" + g_engine.GetSurvey().Report());
	Speech::Speak(g_engine.GetSurvey().Verdict(), true);
	// Count draws again for a while, so the next survey has fresh numbers.
	g_surveyFramesLeft = SURVEY_SAMPLE_FRAMES;
}

// ---- harness ---------------------------------------------------------------

Path HarnessOutPath() {
	if (!g_options.harnessOut.empty()) {
		return Path(g_options.harnessOut);
	}
	return File::GetExeDirectory() / "accessibility" / "harness-result.json";
}

void WriteHarnessFile(const std::string &json) {
	Path out = HarnessOutPath();
	File::CreateFullPath(out.NavigateUp());
	if (!File::WriteStringToFile(false, json, out)) {
		Log::Error("Harness: could not write " + out.ToString());
	}
}

void FinishHarness(const std::string &error) {
	g_harness.done = true;
	if (!error.empty()) {
		Log::Error("Harness: " + error);
		WriteHarnessFile("{\n  \"error\": " + ::Discovery::JsonString(error) + "\n}\n");
	} else {
		const std::vector<::Discovery::TextRun> &runs = g_engine.Screen().Runs();
		::Discovery::HarnessResult result = ::Discovery::ScoreHarnessCase(g_harness.test, runs);

		// Extra facts for the report: what the survey saw, and where in
		// memory each found text is stored.
		std::string extra = "\"game_running\": " + ::Discovery::JsonString(g_machine.GameId());
		extra += ",\n  \"ledger_cells\": " + std::to_string(g_engine.Ledger().Size());
		extra += ",\n  \"survey_verdict\": " + ::Discovery::JsonString(g_engine.GetSurvey().Verdict());
		extra += ",\n  \"survey\": {";
		bool first = true;
		for (const auto &kv : g_engine.GetSurvey().Counters()) {
			extra += (first ? "" : ", ") + ::Discovery::JsonString(kv.first) + ": " + std::to_string(kv.second);
			first = false;
		}
		extra += "},\n  \"locations\": [";
		first = true;
		for (const ::Discovery::TextRun &run : runs) {
			std::vector<::Discovery::TextLocation> hits = g_engine.Locate(run.text, 4);
			if (hits.empty()) {
				continue;
			}
			extra += std::string(first ? "" : ", ") + "{\"text\": " + ::Discovery::JsonString(run.text) + ", \"at\": [";
			for (size_t i = 0; i < hits.size(); i++) {
				char buf[64];
				snprintf(buf, sizeof(buf), "%s\"0x%08X %s\"", i ? ", " : "", hits[i].addr, hits[i].encoding.c_str());
				extra += buf;
			}
			extra += "]}";
			first = false;
		}
		extra += "]";

		std::string mode = g_harness.sidecarLoaded ? "warm" : "cold";
		WriteHarnessFile(::Discovery::HarnessResultToJson(g_harness.test, result, mode, extra));
		char summary[160];
		snprintf(summary, sizeof(summary), "Harness: %d expected texts: %d exact, %d contained, %d near, %d missing. %d runs found.",
			(int)g_harness.test.texts.size(), result.exact, result.contained, result.near, result.missing, (int)result.found.size());
		Log::Info(summary);
	}
	Log::Info("HARNESS DONE");
	System_ExitApp();
}

void StartHarness() {
	g_harness = HarnessRun();
	if (g_options.harnessCase.empty()) {
		return;
	}
	g_harness.active = true;
	std::string text, error;
	if (!File::ReadTextFileToString(Path(g_options.harnessCase), &text)) {
		FinishHarness("could not read the case file " + g_options.harnessCase);
		return;
	}
	if (!::Discovery::ParseHarnessCase(text, &g_harness.test, &error)) {
		FinishHarness("case file " + g_options.harnessCase + ": " + error);
		return;
	}
	// A case without a state measures the game as it is after booting.
	g_harness.stateLoaded = g_harness.test.state.empty();
	Log::Info("Harness: running case " + g_options.harnessCase);
}

// ---- game lifecycle --------------------------------------------------------

void UpdateGeTap() {
	// The GE tap is useful when the ledger has glyphs to look for, while the
	// survey counts draws, or during a harness run. Otherwise leave PPSSPP's
	// fast display-list loop alone.
	bool want = g_engine.CapturesAllSprites() || g_engine.Ledger().Size() > 0 || g_surveyFramesLeft > 0 || (g_harness.active && !g_harness.done);
	if (want != GeTapEnabled()) {
		SetGeTapEnabled(want);
	}
}

void LogSurveyAndReset() {
	if (!g_engine.GetSurvey().Counters().empty()) {
		Log::Info("Discovery survey at game end:\n" + g_engine.GetSurvey().Report());
	}
	if (g_engine.IsRecording()) {
		// The game is ending: keep what was recorded. (Its memory is gone by
		// now, so the recording has no memory copy and cannot be replayed.)
		g_engine.StopRecording();
		OnRecordingEnded();
	}
	if (ProfilesInUse()) {
		SaveProfile();
	}
	g_profileGame.clear();
	SetGeTapEnabled(false);
	ResetTapState();
	g_engine.Reset();
	g_surveyFramesLeft = 0;
}

void OnFlip(void *) {
	if (GeTapEnabled() && g_surveyFramesLeft > 0) {
		g_engine.GetSurvey().Count("draw.sampled_frames");
	}
	if (g_surveyFramesLeft > 0) {
		g_surveyFramesLeft--;
	}
	g_engine.OnFrameEnd();
	if (g_wasRecording && !g_engine.IsRecording()) {
		OnRecordingEnded();
	}

	std::vector<::Discovery::TextChange> changes = g_engine.Screen().TakeChanges();
	if (g_autoRead >= 1 && !changes.empty()) {
		SpeakDialogChanges(changes);
	}
	if (g_autoRead >= 2) {
		SpeakSelection();
		SpeakStableGameText();
	}
	ProfileTick();
	UpdateGeTap();

	if (g_harness.active && !g_harness.done && g_harness.stateLoaded) {
		if (++g_harness.framesSinceLoad >= g_harness.test.settleFrames) {
			FinishHarness(std::string());
		}
	}
}

// The taps are installed once, at start-up, and stay: a short program can
// run and finish between two of our Frame calls, and its calls should still
// be counted. The engine is emptied when a game ends, so it is clean for the
// next one.
void OnGameStarted() {
	g_surveyFramesLeft = SURVEY_SAMPLE_FRAMES;
	UpdateGeTap();
	__DisplayListenFlip(&OnFlip, nullptr);
	g_listening = true;
	if (!g_harness.active) {
		StartHarness();
	}
}

void OnGameStopped() {
	if (g_listening) {
		__DisplayForgetFlip(&OnFlip, nullptr);
		g_listening = false;
	}
	LogSurveyAndReset();
}

}  // namespace

void Init(const Options &options) {
	g_options = options;
	if (!options.harnessCase.empty()) {
		Log::Info("Started with --access-harness=" + options.harnessCase);
		g_autoRead = 0;  // A measuring run should not talk.
	}
	if (!options.recordDir.empty()) {
		Log::Info("Started with --access-record=" + options.recordDir);
		g_autoRead = 0;
	}
	InstallTaps(&g_engine);
	g_engine.SetCaptureAllSprites(true);
}

void Shutdown() {
	if (g_gameRunning) {
		OnGameStopped();
		g_gameRunning = false;
	}
	RemoveTaps();
}

void Frame() {
	if (!PSP_IsInited()) {
		if (g_gameRunning) {
			OnGameStopped();
			g_gameRunning = false;
		} else if (!g_engine.GetSurvey().Counters().empty()) {
			LogSurveyAndReset();  // A program that came and went between two frames.
		}
		g_wantReadScreen = false;
		g_wantReport = false;
		g_wantRecord = false;
		if (g_wantSurvey.exchange(false)) {
			Speech::SpeakTransient("No game running", true);
		}
		return;
	}
	// A reset restarts the flip counter (the same test the script host uses).
	int flips = __DisplayGetFlipCount();
	if (g_gameRunning && flips < g_lastFlipCount) {
		OnGameStopped();
		g_gameRunning = false;
	}
	g_lastFlipCount = flips;
	if (!g_gameRunning) {
		g_gameRunning = true;
		OnGameStarted();
	}

	if (g_wantReadScreen.exchange(false)) {
		ReadScreen();
	}
	if (g_wantSurvey.exchange(false)) {
		SpeakSurvey();
	}
	if (g_wantReport.exchange(false)) {
		WriteReport();
		char summary[200];
		snprintf(summary, sizeof(summary), "Report written. %d text runs found, %d symbols known out of %d.",
			(int)g_engine.Screen().Runs().size(), (int)g_engine.Book().KnownCount(), (int)g_engine.Book().Size());
		Speech::Speak(summary, true);
	}
	if (g_wantRecord.exchange(false)) {
		if (g_engine.IsRecording()) {
			g_recordSpoken = true;
			StopRecording();
		} else if (!StartRecording(HOTKEY_RECORD_FRAMES, "started with the hotkey").empty()) {
			g_recordSpoken = true;
			Speech::Speak("Recording for ten seconds. Press again to stop.", true);
		} else {
			Speech::Speak("Could not start a recording.", true);
		}
	}
	if (!g_options.recordDir.empty() && !g_recordRunStarted && ++g_recordRunHostFrames > HARNESS_LOAD_TIMEOUT_FRAMES) {
		Log::Error("Record run: no save state was loaded in time (was --state= given, and is it this game's state?)");
		Log::Info("RECORD DONE");
		g_recordRunStarted = true;
		System_ExitApp();
	}
	if (g_wantToggleAutoRead.exchange(false)) {
		g_autoRead = (g_autoRead + 1) % 3;
		g_spokenRuns.clear();
		const char *modes[] = { "Automatic reading off", "Reading system dialogs only", "Reading system dialogs and game text" };
		Speech::SpeakTransient(modes[g_autoRead], true);
	}

	if (g_harness.active && !g_harness.done && !g_harness.stateLoaded) {
		if (++g_harness.hostFrames > HARNESS_LOAD_TIMEOUT_FRAMES) {
			FinishHarness("the save state '" + g_harness.test.state + "' was not loaded in time (was --state= given, and is it this game's state?)");
		}
	}
}

::Discovery::Engine *GetEngine() {
	return g_gameRunning ? &g_engine : nullptr;
}

void RequestReadScreen() { g_wantReadScreen = true; }
void RequestSurvey() { g_wantSurvey = true; }
void RequestToggleAutoRead() { g_wantToggleAutoRead = true; }
void RequestReport() { g_wantReport = true; }
void RequestRecord() { g_wantRecord = true; }

std::string StartRecording(int frames, const std::string &note) {
	if (!g_gameRunning || g_engine.IsRecording()) {
		return std::string();
	}
	char stamp[32] = "";
	time_t now = time(nullptr);
	struct tm local;
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
	std::string game = g_machine.GameId();
	Path folder = File::GetExeDirectory() / "accessibility" / "logs" / "recordings" / ((game.empty() ? "unknown" : game) + "-" + stamp);
	return BeginRecording(folder, frames, note) ? folder.ToString() : std::string();
}

bool StopRecording() {
	if (!g_engine.IsRecording()) {
		return false;
	}
	g_engine.StopRecording();
	OnRecordingEnded();
	return true;
}

std::string WriteReport() {
	std::string report = "Discovery inspection report for " + g_machine.GameId() + "\n\n" + g_engine.InspectionReport();
	Path out = File::GetExeDirectory() / "accessibility" / "logs" / "discovery-report.txt";
	File::CreateFullPath(out.NavigateUp());
	if (!File::WriteStringToFile(false, report, out)) {
		Log::Warn("Could not write " + out.ToString());
	}
	return report;
}

bool SurveySampling() {
	return g_surveyFramesLeft > 0;
}

void OnStateFile(const std::string &path, bool loaded) {
	if (!loaded) {
		// A state was saved: put what the layer knows beside it, so loading it
		// later does not start from nothing. No knowledge, no file.
		Path sidecar(SidecarPath(path));
		if (g_engine.Ledger().Size() > 0) {
			if (!File::WriteStringToFile(false, g_engine.Ledger().Serialize(), sidecar)) {
				Log::Warn("Could not write " + sidecar.ToString());
			}
		} else if (File::Exists(sidecar)) {
			File::Delete(sidecar);
		}
		return;
	}
	// A state was loaded (or the game was rewound): memory is not what the
	// ledger describes any more.
	g_engine.OnMemoryReplaced();
	bool sidecarLoaded = false;
	if (!path.empty() && !g_options.noSidecar) {
		std::string text;
		Path sidecar(SidecarPath(path));
		if (File::Exists(sidecar) && File::ReadBinaryFileToString(sidecar, &text)) {
			sidecarLoaded = g_engine.Ledger().Deserialize(text);
			Log::Info(sidecarLoaded ? "Loaded " + sidecar.ToString() + " (" + std::to_string(g_engine.Ledger().Size()) + " glyphs)."
				: "Ignored " + sidecar.ToString() + ": not a file this version understands.");
		}
	}
	UpdateGeTap();
	if (loaded && !g_options.recordDir.empty() && !g_recordRunStarted) {
		// A record run: the state is in, record what it shows from its first frame.
		g_recordRunStarted = BeginRecording(Path(g_options.recordDir), std::max(g_options.recordFrames, 1), "save state " + path);
		if (!g_recordRunStarted) {
			Log::Error("Record run: could not start the recording.");
		}
	}
	if (g_harness.active && !g_harness.done) {
		g_harness.stateLoaded = true;
		g_harness.sidecarLoaded = sidecarLoaded;
		g_harness.framesSinceLoad = 0;
	}
}

}  // namespace DiscoveryHost
}  // namespace Access
