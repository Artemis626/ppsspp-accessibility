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

// Speech output for the accessibility layer.
//
// Every function here is thread-safe: call it from the emulation thread, the
// window thread, or anywhere else. Requests go into a queue that a single
// worker thread delivers to the screen reader, so a slow screen reader can
// never stall a frame.
//
// All text is UTF-8.

#include <string>
#include <string_view>
#include <vector>

namespace Access {
namespace Speech {

enum class Backend {
	None,   // No screen reader found; text is still logged and kept in history.
	NVDA,   // NVDA Controller Client (nvdaControllerClient.dll beside the exe).
};

void Init();
void Shutdown();

// Speak text. With interrupt = true, anything still queued or being spoken is
// cancelled first (use it for "the focus moved, say the new thing now").
void Speak(std::string_view text, bool interrupt);

// Silence current speech and drop everything queued.
void Stop();

// Show a message on the braille display (NVDA only).
void Braille(std::string_view text);

Backend ActiveBackend();

// History of spoken messages, newest first. Consecutive duplicates are stored once.
std::vector<std::string> GetHistory(size_t maxCount);
std::string LastSpoken();

// Speak without adding to history: confirmations like "Copied".
void SpeakTransient(std::string_view text, bool interrupt);

// Speak the newest history entry again, with interrupt.
void RepeatLast();

// Move through the history and speak the entry: +1 = older, -1 = newer.
// Speaking anything new returns the position to the newest entry.
void BrowseHistory(int direction);

// The entry BrowseHistory last spoke (the newest entry if not browsing).
std::string CurrentHistoryEntry();

// Put text (UTF-8) on the Windows clipboard. Returns false on failure.
bool CopyToClipboard(std::string_view text);
bool CopyLastToClipboard();

}  // namespace Speech
}  // namespace Access
