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

// The script sandbox. Players are told they may use other people's scripts,
// so a script must not be able to harm the computer or other scripts.
//
// Taken away:
//   - starting programs (os.execute, io.popen), deleting or renaming files
//     (os.remove, os.rename), ending the emulator (os.exit), reading the
//     environment (os.getenv), os.tmpname, os.setlocale;
//   - native code: package.loadlib and the C module searchers, so require()
//     can only load Lua source;
//   - precompiled Lua (bytecode can break out of the Lua machine): load,
//     loadfile, dofile and require accept source text only;
//   - the debug library, except debug.traceback (it could switch the
//     watchdog off and reach the original functions kept below).
// Limited:
//   - Writing: only inside the script's own data folder, emu.getdatadir()
//     (scripts/data/<name>/). A script cannot change its own code, other
//     scripts, scripts/lib, or other scripts' data.
//   - Reading (io.open, io.lines, loadfile, dofile): only inside the data
//     folder and the script's folder. A relative name means the data folder
//     when writing; when reading, the data folder if the file is there,
//     otherwise the script's folder.
//   - Names: relative ones (no drive, no leading slash, no ".."), or full
//     paths that start with emu.getdatadir() or emu.getscriptdir().
//   - Links: after Windows follows any folder link (junction) or symbolic
//     link, the real place must still be inside the allowed folder. A link
//     that arrived inside a downloaded script folder cannot lead out.
//   - require() loads only from the script's folder and scripts/lib, with
//     the same link check.
// Kept: everything else, including os.time, os.clock and os.date.

#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <climits>
#include <cstdlib>
#endif

#include "Common/Data/Encoding/Utf8.h"
#include "LuaInternal.h"

namespace Access {
namespace Script {

namespace {

#ifdef _WIN32
// The real place of an existing file or folder, after every link is followed.
bool RealPath(const std::string &utf8, std::string *out) {
	std::wstring wide = ConvertUTF8ToWString(utf8);
	HANDLE h = CreateFileW(wide.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return false;
	}
	wchar_t buffer[4096];
	DWORD n = GetFinalPathNameByHandleW(h, buffer, (DWORD)(sizeof(buffer) / sizeof(buffer[0])), FILE_NAME_NORMALIZED);
	CloseHandle(h);
	if (n == 0 || n >= sizeof(buffer) / sizeof(buffer[0])) {
		return false;
	}
	*out = ConvertWStringToUTF8(std::wstring(buffer, n));
	return true;
}
#else
bool RealPath(const std::string &utf8, std::string *out) {
	char buffer[PATH_MAX];
	if (!realpath(utf8.c_str(), buffer)) {
		return false;
	}
	*out = buffer;
	return true;
}
#endif

std::string Folded(std::string s) {
	for (char &c : s) {
		if (c == '\\') c = '/';
#ifdef _WIN32
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
#endif
	}
	while (!s.empty() && s.back() == '/') s.pop_back();
	return s;
}

// within(path, root) -> true if the real place of `path` is `root` or inside
// it. `path` need not exist yet: its nearest existing folder is resolved and
// the rest (already checked to hold no "..") is appended.
int L_Within(lua_State *L) {
	std::string path = luaL_checkstring(L, 1);
	std::string root = luaL_checkstring(L, 2);
	std::string realRoot;
	if (!RealPath(root, &realRoot)) {
		lua_pushboolean(L, 0);
		return 1;
	}
	std::string rest;
	std::string probe = path;
	std::string real;
	while (!RealPath(probe, &real)) {
		size_t cut = probe.find_last_of("/\\");
		if (cut == std::string::npos || cut == 0) {
			lua_pushboolean(L, 0);
			return 1;
		}
		rest = "/" + probe.substr(cut + 1) + rest;
		probe = probe.substr(0, cut);
	}
	std::string a = Folded(real + rest);
	std::string b = Folded(realRoot);
	bool inside = a == b || (a.size() > b.size() && a.compare(0, b.size(), b) == 0 && a[b.size()] == '/');
	lua_pushboolean(L, inside ? 1 : 0);
	return 1;
}

// Runs with the script's folder, its data folder, scripts/lib and the link
// check as its arguments. The originals it needs are kept in locals, which
// scripts cannot reach without the debug library.
const char *const kSandbox = R"LUA(
local function clean(p) return (p:gsub("\\", "/"):gsub("/+$", "")) end
local dir, datadir, libdir, within = ...
dir, datadir, libdir = clean(dir), clean(datadir), clean(libdir)
local io_open, io_lines, raw_load = io.open, io.lines, load
local searchpath = package.searchpath

local function under(slashed, base)
  local prefix = base:lower() .. "/"
  if slashed:lower():sub(1, #prefix) == prefix then
    return slashed:sub(#prefix + 1)
  end
  return nil
end

local function refuse(name, why, level)
  error("not allowed: " .. why .. " (" .. tostring(name) .. "). Scripts write only in emu.getdatadir() and read only there and in emu.getscriptdir().", level + 1)
end

-- The full path a script may use for `name`, or an error.
local function resolve(name, write, level)
  level = level + 1
  if type(name) ~= "string" or name == "" then
    error("a file name is needed", level)
  end
  local slashed = name:gsub("\\", "/")
  local base, rel
  local inData = under(slashed, datadir)
  if inData then
    base, rel = datadir, inData
  else
    local inDir = under(slashed, dir)
    if inDir then base, rel = dir, inDir else rel = slashed end
  end
  if rel:find("^/") or rel:find(":", 1, true) or rel:find("\0", 1, true) then
    refuse(name, "a path outside the script's folders", level)
  end
  for part in rel:gmatch("[^/]+") do
    if part == ".." then refuse(name, "a path that climbs out with ..", level) end
  end
  if not base then
    if write then
      base = datadir
    else
      local f = io_open(datadir .. "/" .. rel, "r")
      if f then f:close(); base = datadir else base = dir end
    end
  end
  if write and base ~= datadir then
    refuse(name, "writing outside the script's data folder", level)
  end
  local full = base .. "/" .. rel
  if not within(full, base) then
    refuse(name, "a link that leads outside the script's folders", level)
  end
  return full
end

local function writes(mode) return mode ~= nil and tostring(mode):find("[wa+]") ~= nil end

io.open = function(name, mode) return io_open(resolve(name, writes(mode), 2), mode) end
io.lines = function(name, ...)
  if name == nil then error("io.lines needs a file name", 2) end
  return io_lines(resolve(name, false, 2), ...)
end
io.popen, io.input, io.output = nil, nil, nil

os.execute, os.remove, os.rename, os.exit = nil, nil, nil, nil
os.tmpname, os.getenv, os.setlocale = nil, nil, nil

load = function(chunk, name, mode, env) return raw_load(chunk, name, "t", env) end
local function loadsource(path, chunkname, env)
  local f, err = io_open(path, "r")
  if not f then return nil, err end
  local code = f:read("a")
  f:close()
  if code:sub(1, 3) == "\239\187\191" then code = code:sub(4) end
  return raw_load(code, chunkname, "t", env)
end
loadfile = function(name, mode, env) return loadsource(resolve(name, false, 2), "@" .. name, env) end
dofile = function(name)
  local fn, err = loadsource(resolve(name, false, 2), "@" .. name)
  if not fn then error(err, 2) end
  return fn()
end

package.loadlib = nil
package.cpath = ""
package.searchers = {
  package.searchers[1],
  function(modname)
    local path, err = searchpath(modname, package.path)
    if not path then return "\n\t" .. err end
    if not (within(path, dir) or within(path, libdir)) then
      error("module '" .. modname .. "' leads outside the script's folder and scripts/lib", 2)
    end
    local fn, lerr = loadsource(path, "@" .. path)
    if not fn then error("error loading module '" .. modname .. "' from file '" .. path .. "':\n\t" .. lerr, 2) end
    return fn, path
  end,
}

debug = { traceback = debug.traceback }
package.loaded.debug = debug
)LUA";

}  // namespace

bool ApplySandbox(lua_State *L, const std::string &scriptDir, const std::string &dataDir, const std::string &libDir, std::string *error) {
	if (luaL_loadbufferx(L, kSandbox, strlen(kSandbox), "=sandbox", "t") != LUA_OK) {
		*error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "the sandbox did not load";
		lua_pop(L, 1);
		return false;
	}
	lua_pushstring(L, scriptDir.c_str());
	lua_pushstring(L, dataDir.c_str());
	lua_pushstring(L, libDir.c_str());
	lua_pushcfunction(L, &L_Within);
	if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
		*error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "the sandbox failed";
		lua_pop(L, 1);
		return false;
	}
	return true;
}

}  // namespace Script
}  // namespace Access
