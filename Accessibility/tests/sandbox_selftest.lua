-- Automated self-test for the script sandbox (Scripting/Sandbox.cpp).
-- Prints PASS/FAIL lines to accessibility/logs/access.log, then
-- "SANDBOX RESULT: ...". Run with --script=<this file>.
--
-- It writes small files only in its data folder (scripts/data/sandbox_selftest/).
-- The link test needs a folder link named "escape" in that data folder,
-- pointing anywhere outside it; the test runner makes one. Without it, that
-- check is skipped and says so.

local failures = 0
local function check(name, ok, detail)
  if ok then
    print("PASS " .. name)
  else
    failures = failures + 1
    print("FAIL " .. name .. (detail and (": " .. tostring(detail)) or ""))
  end
end
local function refused(fn, ...)
  local ok, err = pcall(fn, ...)
  return not ok, err
end

-- Taken away
check("os.execute is gone", os.execute == nil)
check("os.remove is gone", os.remove == nil)
check("os.rename is gone", os.rename == nil)
check("os.exit is gone", os.exit == nil)
check("os.getenv is gone", os.getenv == nil)
check("io.popen is gone", io.popen == nil)
check("package.loadlib is gone", package.loadlib == nil)
check("package.cpath is empty", package.cpath == "")
check("debug keeps only traceback", type(debug.traceback) == "function" and debug.getupvalue == nil and debug.sethook == nil)
check("require('debug') is the small table", require("debug") == debug)

-- Kept
check("os.time and os.clock remain", type(os.time()) == "number" and type(os.clock()) == "number")
check("string, table, math remain", string.format("%d", 3) == "3" and table.concat({ "a", "b" }) == "ab" and math.floor(2.5) == 2)

-- Bytecode refused, source accepted
local fn = load("return 1 + 1")
check("load of source works", fn and fn() == 2)
local bin, berr = load(string.dump(function() return 5 end))
check("load of bytecode is refused", bin == nil, berr)

-- The two folders
local data, here = emu.getdatadir(), emu.getscriptdir()
check("emu.getdatadir is scripts/data/sandbox_selftest/", data:gsub("\\", "/"):find("/scripts/data/sandbox_selftest/$") ~= nil, data)

-- Names that lead out
local isBad, err = refused(io.open, "C:/Windows/win.ini", "r")
check("absolute path refused", isBad, err)
isBad, err = refused(io.open, "../outside.txt", "w")
check("parent folder refused", isBad, err)
isBad, err = refused(io.open, "/etc/passwd")
check("leading slash refused", isBad, err)
isBad, err = refused(io.lines, "sub\\..\\..\\x.txt")
check("backslash parent refused", isBad, err)
isBad, err = refused(dofile, "C:/x.lua")
check("dofile outside refused", isBad, err)
isBad, err = refused(io.open, data .. "../other_script/steal.txt", "w")
check("another script's data folder refused", isBad, err)

-- Code is read-only
isBad, err = refused(io.open, here .. "sandbox_selftest.lua", "a")
check("a script cannot change its own code", isBad, err)
isBad, err = refused(io.open, here .. "new_script.lua", "w")
check("a script cannot add files beside itself", isBad, err)
local own = io.open(here .. "sandbox_selftest.lua", "r")
check("a script can read its own folder", own ~= nil)
if own then own:close() end

-- Its data folder is writable
local f, ferr = io.open("sandbox_selftest.tmp", "w")
check("a relative name is written in the data folder", f ~= nil, ferr)
if f then
  f:write("return 42\n")
  f:close()
  local r = io.open(data .. "sandbox_selftest.tmp", "r")
  check("and is found there by full path", r ~= nil and r:read("l") == "return 42")
  if r then r:close() end
  local again = io.open("sandbox_selftest.tmp", "r")
  check("a relative read finds the data file first", again ~= nil)
  if again then again:close() end
  check("loadfile from the data folder works", loadfile("sandbox_selftest.tmp")() == 42)
  check("dofile works", dofile("sandbox_selftest.tmp") == 42)
end

-- Links: the runner puts a folder link "escape" (pointing outside) and a
-- marker file LINK_PRESENT in the data folder.
local hasLink = io.open(data .. "LINK_PRESENT", "r")
if hasLink then
  hasLink:close()
  isBad, err = refused(io.open, "escape/sandbox_link_test.txt", "w")
  check("a folder link that leads outside is refused for writing", isBad, err)
  isBad, err = refused(io.open, "escape/target.txt", "r")
  check("and for reading", isBad, err)
else
  print("SKIP link test: no LINK_PRESENT marker (the runner makes the link)")
end

if failures == 0 then
  print("SANDBOX RESULT: ALL PASSED")
else
  print("SANDBOX RESULT: " .. failures .. " FAILED")
end
