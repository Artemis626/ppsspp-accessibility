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

#include <cstdio>
#include <ctime>
#include <mutex>

#include "Accessibility/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Log.h"

namespace Access {
namespace Log {

namespace {

std::mutex g_mutex;
FILE *g_file = nullptr;

void Write(const char *tag, std::string_view text) {
	// Mirror into PPSSPP's own log too, so both logs tell the whole story.
	INFO_LOG(::Log::System, "[Access] %s %.*s", tag, (int)text.size(), text.data());

	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_file) {
		return;
	}
	time_t now = time(nullptr);
	struct tm local {};
	localtime_s(&local, &now);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
	fprintf(g_file, "%s %s %.*s\n", stamp, tag, (int)text.size(), text.data());
	// Flush every line: if the emulator crashes, the last lines are what
	// you need to see.
	fflush(g_file);
}

}  // namespace

void Init() {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_file) {
		return;
	}
	Path dir = File::GetExeDirectory() / "accessibility" / "logs";
	File::CreateFullPath(dir);
	Path logPath = dir / "access.log";
	Path oldPath = dir / "access.old.log";
	if (File::Exists(logPath)) {
		File::Delete(oldPath, true);
		File::Rename(logPath, oldPath);
	}
	g_file = File::OpenCFile(logPath, "wb");
	if (g_file) {
		// UTF-8 byte order mark, so Notepad and other editors pick the right encoding.
		fputs("\xEF\xBB\xBF", g_file);
		time_t now = time(nullptr);
		struct tm local {};
		localtime_s(&local, &now);
		char stamp[64];
		strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
		fprintf(g_file, "PPSSPP accessibility log, started %s\n", stamp);
		fflush(g_file);
	}
}

void Shutdown() {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_file) {
		fclose(g_file);
		g_file = nullptr;
	}
}

void Info(std::string_view text) { Write("INFO", text); }
void Warn(std::string_view text) { Write("WARNING", text); }
void Error(std::string_view text) { Write("ERROR", text); }
void Script(std::string_view text) { Write("SCRIPT", text); }
void Spoken(std::string_view text) { Write("SAY", text); }

}  // namespace Log
}  // namespace Access
