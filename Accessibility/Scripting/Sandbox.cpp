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

// The script sandbox. Players are told to drop other people's scripts into
// the scripts folder, so a script must not be able to harm the computer.
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
//   - io.open, io.lines, loadfile and dofile take only names inside the
//     script's own folder: relative ones (no drive, no leading slash, no
//     ".."), or full paths that start with emu.getscriptdir().
// Kept: everything else, including os.time, os.clock and os.date.

#include <cstring>

#include "LuaInternal.h"

namespace Access {
namespace Script {

namespace {

// Runs with the script's folder as its one argument. The originals it needs
// are kept in locals, which scripts cannot reach without the debug library.
const char *const kSandbox = R"LUA(
local dir = (...):gsub("\\", "/"):gsub("/+$", "")
local prefix = dir:lower() .. "/"
local io_open, io_lines, raw_load = io.open, io.lines, load
local searchpath = package.searchpath

local function inside(name, level)
  if type(name) ~= "string" or name == "" then
    error("a file name is needed", level)
  end
  -- emu.getscriptdir() .. "data.txt" is the documented way to name a file:
  -- a full path inside the script's folder is taken as the relative rest.
  local slashed = name:gsub("\\", "/")
  if slashed:lower():sub(1, #prefix) == prefix then
    name = slashed:sub(#prefix + 1)
  end
  local refused = name:find("^[/\\]") or name:find(":", 1, true) or name:find("\0", 1, true)
  if not refused then
    for part in name:gmatch("[^/\\]+") do
      if part == ".." then refused = true end
    end
  end
  if refused then
    error("scripts may only use files inside their own folder (a name like \"data/notes.txt\"), not " .. name, level)
  end
  return dir .. "/" .. name
end

io.open = function(name, mode) return io_open(inside(name, 3), mode) end
io.lines = function(name, ...)
  if name == nil then error("io.lines needs a file name", 2) end
  return io_lines(inside(name, 3), ...)
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
loadfile = function(name, mode, env) return loadsource(inside(name, 3), "@" .. name, env) end
dofile = function(name)
  local fn, err = loadsource(inside(name, 3), "@" .. name)
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
    local fn, lerr = loadsource(path, "@" .. path)
    if not fn then error("error loading module '" .. modname .. "' from file '" .. path .. "':\n\t" .. lerr, 2) end
    return fn, path
  end,
}

debug = { traceback = debug.traceback }
package.loaded.debug = debug
)LUA";

}  // namespace

bool ApplySandbox(lua_State *L, const std::string &scriptDir, std::string *error) {
	if (luaL_loadbufferx(L, kSandbox, strlen(kSandbox), "=sandbox", "t") != LUA_OK) {
		*error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "the sandbox did not load";
		lua_pop(L, 1);
		return false;
	}
	lua_pushstring(L, scriptDir.c_str());
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		*error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "the sandbox failed";
		lua_pop(L, 1);
		return false;
	}
	return true;
}

}  // namespace Script
}  // namespace Access
