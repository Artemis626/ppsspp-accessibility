-- Automated self-test for the script sandbox (Scripting/Sandbox.cpp).
-- Prints PASS/FAIL lines to accessibility/logs/access.log, then
-- "SANDBOX RESULT: ...". Run with --script=<this file>; it writes and reads
-- back one small file, sandbox_selftest.tmp, beside itself.

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
local dumped = string.dump(function() return 5 end)
local bin, berr = load(dumped)
check("load of bytecode is refused", bin == nil, berr)

-- Files: only inside the script's folder
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

local f, ferr = io.open("sandbox_selftest.tmp", "w")
check("a file in the script folder can be written", f ~= nil, ferr)
if f then
  f:write("return 42\n")
  f:close()
  local r = io.open("sandbox_selftest.tmp", "r")
  check("and read back", r ~= nil and r:read("l") == "return 42")
  if r then r:close() end
  check("loadfile of a source file in the folder works", loadfile("sandbox_selftest.tmp")() == 42)
  check("dofile works", dofile("sandbox_selftest.tmp") == 42)
  local full = io.open(emu.getscriptdir() .. "sandbox_selftest.tmp", "r")
  check("a full path inside the script folder is accepted", full ~= nil)
  if full then full:close() end
  isBad, err = refused(io.open, emu.getscriptdir() .. "../outside.txt", "w")
  check("a full path that climbs out is refused", isBad, err)
end

if failures == 0 then
  print("SANDBOX RESULT: ALL PASSED")
else
  print("SANDBOX RESULT: " .. failures .. " FAILED")
end
