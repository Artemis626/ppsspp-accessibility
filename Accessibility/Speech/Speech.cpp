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

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include "Accessibility/Log.h"
#include "Accessibility/Speech/Speech.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#endif

namespace Access {
namespace Speech {

namespace {

enum class Op {
	Speak,
	Cancel,
	Braille,
};

struct Request {
	Op op;
	std::wstring text;
};

const size_t MAX_HISTORY = 200;

std::mutex g_queueMutex;
std::condition_variable g_queueCond;
std::deque<Request> g_queue;
bool g_quit = false;
bool g_running = false;
std::thread g_worker;

std::mutex g_historyMutex;
std::deque<std::string> g_history;  // front = newest

Backend g_backend = Backend::None;

#if PPSSPP_PLATFORM(WINDOWS)
// The NVDA Controller Client functions return an RPC error_status_t
// (unsigned long); 0 means success. We load the DLL at runtime instead of
// linking it, so a missing DLL never stops the emulator from starting.
typedef unsigned long(__stdcall *NvdaVoidFn)();
typedef unsigned long(__stdcall *NvdaTextFn)(const wchar_t *);

HMODULE g_nvdaDll = nullptr;
NvdaVoidFn g_nvdaTestIfRunning = nullptr;
NvdaVoidFn g_nvdaCancelSpeech = nullptr;
NvdaTextFn g_nvdaSpeakText = nullptr;
NvdaTextFn g_nvdaBrailleMessage = nullptr;

bool LoadNvda() {
	// Load only from the exe's own folder (full path), never from the
	// general DLL search path, so a stray DLL elsewhere can't be picked up.
	wchar_t exePath[MAX_PATH]{};
	DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) {
		return false;
	}
	std::wstring dllPath(exePath, len);
	size_t slash = dllPath.find_last_of(L"\\/");
	dllPath.resize(slash == std::wstring::npos ? 0 : slash + 1);
	dllPath += L"nvdaControllerClient.dll";

	g_nvdaDll = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!g_nvdaDll) {
		WARN_LOG(::Log::System, "[Access] nvdaControllerClient.dll not found beside the exe (error %lu). Speech is off.", GetLastError());
		return false;
	}
	g_nvdaTestIfRunning = (NvdaVoidFn)GetProcAddress(g_nvdaDll, "nvdaController_testIfRunning");
	g_nvdaCancelSpeech = (NvdaVoidFn)GetProcAddress(g_nvdaDll, "nvdaController_cancelSpeech");
	g_nvdaSpeakText = (NvdaTextFn)GetProcAddress(g_nvdaDll, "nvdaController_speakText");
	g_nvdaBrailleMessage = (NvdaTextFn)GetProcAddress(g_nvdaDll, "nvdaController_brailleMessage");
	if (!g_nvdaTestIfRunning || !g_nvdaCancelSpeech || !g_nvdaSpeakText) {
		ERROR_LOG(::Log::System, "[Access] nvdaControllerClient.dll is missing expected functions. Speech is off.");
		FreeLibrary(g_nvdaDll);
		g_nvdaDll = nullptr;
		return false;
	}
	unsigned long running = g_nvdaTestIfRunning();
	if (running == 0) {
		INFO_LOG(::Log::System, "[Access] NVDA Controller Client loaded, NVDA is running.");
	} else {
		// Not fatal: NVDA may be started later, and each call reconnects.
		WARN_LOG(::Log::System, "[Access] NVDA Controller Client loaded, but NVDA is not running yet (code %lu).", running);
	}
	return true;
}

void UnloadNvda() {
	if (g_nvdaDll) {
		FreeLibrary(g_nvdaDll);
	}
	g_nvdaDll = nullptr;
	g_nvdaTestIfRunning = nullptr;
	g_nvdaCancelSpeech = nullptr;
	g_nvdaSpeakText = nullptr;
	g_nvdaBrailleMessage = nullptr;
}
#endif

void Deliver(const Request &req) {
#if PPSSPP_PLATFORM(WINDOWS)
	if (g_backend != Backend::NVDA) {
		return;
	}
	// Remember whether the last call failed, so we log once per change
	// instead of once per message (menus can produce many messages a second).
	static bool lastFailed = false;
	unsigned long rc = 0;
	switch (req.op) {
	case Op::Cancel:
		rc = g_nvdaCancelSpeech();
		break;
	case Op::Speak:
		rc = g_nvdaSpeakText(req.text.c_str());
		break;
	case Op::Braille:
		rc = g_nvdaBrailleMessage ? g_nvdaBrailleMessage(req.text.c_str()) : 0;
		break;
	}
	bool failed = rc != 0;
	if (failed != lastFailed) {
		if (failed) {
			WARN_LOG(::Log::System, "[Access] NVDA call failed (code %lu). Is NVDA running?", rc);
		} else {
			INFO_LOG(::Log::System, "[Access] NVDA calls are working again.");
		}
		lastFailed = failed;
	}
#endif
}

void WorkerLoop() {
	SetCurrentThreadName("AccessSpeech");
	while (true) {
		Request req;
		{
			std::unique_lock<std::mutex> lock(g_queueMutex);
			g_queueCond.wait(lock, [] { return g_quit || !g_queue.empty(); });
			if (g_quit) {
				return;
			}
			req = std::move(g_queue.front());
			g_queue.pop_front();
		}
		Deliver(req);
	}
}

void Enqueue(Request &&req, bool clearFirst) {
	{
		std::lock_guard<std::mutex> lock(g_queueMutex);
		if (!g_running) {
			return;
		}
		if (clearFirst) {
			g_queue.clear();
			g_queue.push_back(Request{ Op::Cancel, L"" });
		}
		g_queue.push_back(std::move(req));
	}
	g_queueCond.notify_one();
}

// History browsing position: 0 = newest. Reset whenever something new is spoken.
int g_historyCursor = 0;

void AddToHistory(std::string_view text) {
	std::lock_guard<std::mutex> lock(g_historyMutex);
	g_historyCursor = 0;
	if (!g_history.empty() && g_history.front() == text) {
		return;
	}
	g_history.emplace_front(text);
	if (g_history.size() > MAX_HISTORY) {
		g_history.pop_back();
	}
}

bool IsBlank(std::string_view text) {
	for (char c : text) {
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
			return false;
		}
	}
	return true;
}

}  // namespace

void Init() {
	std::lock_guard<std::mutex> lock(g_queueMutex);
	if (g_running) {
		return;
	}
#if PPSSPP_PLATFORM(WINDOWS)
	g_backend = LoadNvda() ? Backend::NVDA : Backend::None;
#else
	g_backend = Backend::None;
#endif
	g_quit = false;
	g_running = true;
	g_worker = std::thread(&WorkerLoop);
}

void Shutdown() {
	{
		std::lock_guard<std::mutex> lock(g_queueMutex);
		if (!g_running) {
			return;
		}
		g_quit = true;
		g_running = false;
		g_queue.clear();
	}
	g_queueCond.notify_one();
	if (g_worker.joinable()) {
		g_worker.join();
	}
#if PPSSPP_PLATFORM(WINDOWS)
	UnloadNvda();
#endif
	g_backend = Backend::None;
}

void Speak(std::string_view text, bool interrupt) {
	if (IsBlank(text)) {
		if (interrupt) {
			Stop();
		}
		return;
	}
	// Logged so speech can be checked in the log file without listening,
	// and so testers can see exactly what was sent to the screen reader.
	// Goes to access.log and PPSSPP's log as "SAY ...".
	Access::Log::Spoken(std::string(interrupt ? "(interrupt) " : "") + std::string(text));
	AddToHistory(text);
	Enqueue(Request{ Op::Speak, ConvertUTF8ToWString(text) }, interrupt);
}

void Stop() {
	std::lock_guard<std::mutex> lock(g_queueMutex);
	if (!g_running) {
		return;
	}
	g_queue.clear();
	g_queue.push_back(Request{ Op::Cancel, L"" });
	g_queueCond.notify_one();
}

void Braille(std::string_view text) {
	Enqueue(Request{ Op::Braille, ConvertUTF8ToWString(text) }, false);
}

Backend ActiveBackend() {
	return g_backend;
}

std::vector<std::string> GetHistory(size_t maxCount) {
	std::lock_guard<std::mutex> lock(g_historyMutex);
	size_t n = std::min(maxCount, g_history.size());
	return std::vector<std::string>(g_history.begin(), g_history.begin() + n);
}

std::string LastSpoken() {
	std::lock_guard<std::mutex> lock(g_historyMutex);
	return g_history.empty() ? std::string() : g_history.front();
}

void SpeakTransient(std::string_view text, bool interrupt) {
	if (IsBlank(text)) {
		return;
	}
	Access::Log::Spoken(std::string("(not in history) ") + std::string(text));
	Enqueue(Request{ Op::Speak, ConvertUTF8ToWString(text) }, interrupt);
}

void RepeatLast() {
	std::string last;
	{
		std::lock_guard<std::mutex> lock(g_historyMutex);
		g_historyCursor = 0;
		if (!g_history.empty()) {
			last = g_history.front();
		}
	}
	if (last.empty()) {
		SpeakTransient("Nothing spoken yet", true);
		return;
	}
	// Not added to history again (it is already the newest entry).
	Access::Log::Spoken("(repeat) " + last);
	Enqueue(Request{ Op::Speak, ConvertUTF8ToWString(last) }, true);
}

void BrowseHistory(int direction) {
	std::string text;
	bool atEdge = false;
	{
		std::lock_guard<std::mutex> lock(g_historyMutex);
		if (g_history.empty()) {
			atEdge = true;
		} else {
			int next = g_historyCursor + direction;
			if (next < 0 || next >= (int)g_history.size()) {
				atEdge = true;
				text = g_history[g_historyCursor];
			} else {
				g_historyCursor = next;
				text = g_history[next];
			}
		}
	}
	if (text.empty()) {
		SpeakTransient("History is empty", true);
		return;
	}
	if (atEdge) {
		text = (direction > 0 ? "Oldest message. " : "Newest message. ") + text;
	}
	Access::Log::Spoken("(history) " + text);
	Enqueue(Request{ Op::Speak, ConvertUTF8ToWString(text) }, true);
}

std::string CurrentHistoryEntry() {
	std::lock_guard<std::mutex> lock(g_historyMutex);
	if (g_history.empty()) {
		return std::string();
	}
	return g_history[std::min<size_t>(g_historyCursor, g_history.size() - 1)];
}

bool CopyToClipboard(std::string_view text) {
#if PPSSPP_PLATFORM(WINDOWS)
	std::wstring wide = ConvertUTF8ToWString(text);
	if (!OpenClipboard(nullptr)) {
		return false;
	}
	bool ok = false;
	EmptyClipboard();
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
	if (mem) {
		void *dest = GlobalLock(mem);
		if (dest) {
			memcpy(dest, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
			GlobalUnlock(mem);
			// On success the clipboard owns the memory; only free it on failure.
			ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
		}
		if (!ok) {
			GlobalFree(mem);
		}
	}
	CloseClipboard();
	return ok;
#else
	return false;
#endif
}

bool CopyLastToClipboard() {
	std::string last = LastSpoken();
	return !last.empty() && CopyToClipboard(last);
}

}  // namespace Speech
}  // namespace Access
