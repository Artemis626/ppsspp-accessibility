-- Automated self-test for the Phase 2 Lua API. Not an example for modders
-- (see examples/ for that): it exercises every function, including error
-- paths, and prints PASS/FAIL lines to accessibility/logs/access.log.

local failures = 0
local function check(name, ok, detail)
  if ok then
    print("PASS " .. name)
  else
    failures = failures + 1
    print("FAIL " .. name .. (detail and (": " .. tostring(detail)) or ""))
  end
end

-- gameinfo
local id = gameinfo.getid()
check("gameinfo.getid returns a string", type(id) == "string" and #id > 0, id)
check("gameinfo.gettitle returns a string", type(gameinfo.gettitle()) == "string")
check("gameinfo.getromname is gettitle", gameinfo.getromname() == gameinfo.gettitle())
print("Game: " .. id .. " / " .. gameinfo.gettitle() .. " / version " .. gameinfo.getversion())

-- memory: valid reads
local base = 0x08800000
check("isvalid user RAM", memory.isvalid(base))
check("isvalid rejects 0", not memory.isvalid(0))
local b = memory.read_u8(base)
check("read_u8 is a byte", type(b) == "number" and b >= 0 and b <= 255, b)
local w = memory.read_u32(base)
check("read_u32 is a number", type(w) == "number" and w >= 0 and w <= 0xFFFFFFFF, w)
check("read_u16 matches two bytes",
  memory.read_u16(base) == memory.read_u8(base) + memory.read_u8(base + 1) * 256)
check("read_u16_be swaps", memory.read_u16_be(base) == memory.read_u8(base) * 256 + memory.read_u8(base + 1))
local s8 = memory.read_s8(base)
check("read_s8 sign", (b < 128 and s8 == b) or (b >= 128 and s8 == b - 256), s8)
check("DeSmuME alias readdword", memory.readdword(base) == w)
check("read_float returns a number", type(memory.read_float(base)) == "number")
local range = memory.readbyterange(base, 4)
check("readbyterange length 4, 1-based", #range == 4 and range[1] == b, #range)
check("fromcwcheat offset", memory.fromcwcheat(0x123456) == 0x08923456)
check("fromcwcheat code address", memory.fromcwcheat(0x20123456) == 0x08923456)
local str = memory.readstring(base, 16)
check("readstring returns a string", type(str) == "string", str)
check("readstring sjis", type(memory.readstring(base, 16, "sjis")) == "string")

-- memory: invalid reads return nil, no error
check("read_u32 invalid -> nil", memory.read_u32(0x00000010) == nil)
check("readstring invalid -> nil", memory.readstring(0x00000010, 8) == nil)

-- memory: bad arguments raise Lua's standard errors
local ok, err = pcall(memory.read_u8, nil)
check("read_u8(nil) raises", not ok and tostring(err):find("bad argument #1") ~= nil, err)
ok, err = pcall(memory.readstring, base, 8, "klingon")
check("readstring unknown encoding raises", not ok and tostring(err):find("unknown encoding") ~= nil, err)

-- speech
check("speak exists", type(speak) == "function" and type(stopSpeech) == "function")
speak("Self test running for " .. id)
check("speech.last is the last message", speech.last() == "Self test running for " .. id, speech.last())
check("speech.history newest first", speech.history(1)[1] == speech.last())
speak(12345)
check("speak converts numbers", speech.last() == "12345", speech.last())

-- frameadvance inside a callback must give a clear error
local cbErr
event.onframeend(function()
  if cbErr == nil then
    local ok2, e = pcall(emu.frameadvance)
    cbErr = e or "no error"
  end
end, "frameadvance-probe")

-- frame callbacks and counters
local cbFrames = 0
local cbId = event.onframeend(function() cbFrames = cbFrames + 1 end, "counter")
check("onframeend returns an id", type(cbId) == "number", cbId)

local afterCalls = 0
emu.registerafter(function() afterCalls = afterCalls + 1 end)

-- A callback that errors once: it must be reported, spoken and turned off,
-- while the rest of the script keeps running.
local brokenCalls = 0
event.onframeend(function()
  brokenCalls = brokenCalls + 1
  local t = nil
  return t.field  -- deliberate error
end, "deliberately-broken")

-- main body with frameadvance (BizHawk style)
local startFrame = emu.framecount()
local startVblank = emu.vblankcount()
for i = 1, 30 do
  emu.frameadvance()
end
check("frameadvance advanced 30 frames", emu.framecount() - startFrame == 30, emu.framecount() - startFrame)
check("vblankcount advances", emu.vblankcount() > startVblank)
check("onframeend ran every frame", cbFrames >= 30, cbFrames)
check("registerafter ran", afterCalls >= 30, afterCalls)
check("broken callback ran exactly once then was turned off", brokenCalls == 1, brokenCalls)
check("frameadvance in callback gives clear error",
  tostring(cbErr):find("main body") ~= nil, cbErr)
check("unregisterbyid", event.unregisterbyid(cbId) == true)
check("unregisterbyname", event.unregisterbyname("frameadvance-probe") == true)
local before = cbFrames
emu.frameadvance()
emu.frameadvance()
check("unregistered callback stopped", cbFrames == before, cbFrames - before)

if failures == 0 then
  print("SELFTEST RESULT: ALL PASSED")
  speak("Self test passed")
else
  print("SELFTEST RESULT: " .. failures .. " FAILED")
  speak("Self test failed, " .. failures .. " failures")
end
