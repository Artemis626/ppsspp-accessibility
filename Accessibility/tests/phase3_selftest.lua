-- Automated self-test for the Phase 3 API: memory writes, read/write/execute
-- callbacks, registers, joypad, audio, and script hotkeys. Prints PASS/FAIL
-- lines to accessibility/logs/access.log. Hotkey checks need keys pressed
-- from outside (see tools/phase3-test.ps1); they are reported as KEYTEST lines.

local failures = 0
local function check(name, ok, detail)
  if ok then
    print("PASS " .. name)
  else
    failures = failures + 1
    print("FAIL " .. name .. (detail ~= nil and (": " .. tostring(detail)) or ""))
  end
end

local function frames(n) for _ = 1, n do emu.frameadvance() end end

-- ---- memory writes ----------------------------------------------------
local scratch = 0x09FFFF00  -- near the top of user RAM, unused by small test programs
check("write_u32 returns true", memory.write_u32(scratch, 0x12345678) == true)
check("write_u32 / read_u32 round trip", memory.read_u32(scratch) == 0x12345678)
memory.write_u16_be(scratch, 0xABCD)
check("write_u16_be byte order", memory.read_u8(scratch) == 0xAB and memory.read_u8(scratch + 1) == 0xCD)
memory.write_s8(scratch, -1)
check("write_s8 -1 is 0xFF", memory.read_u8(scratch) == 0xFF)
memory.write_float(scratch, 1.5)
check("write_float round trip", memory.read_float(scratch) == 1.5)
check("writebyterange", memory.writebyterange(scratch, {1, 2, 3, 4}) == true and memory.read_u32(scratch) == 0x04030201)
check("write to invalid address returns false", memory.write_u8(0x10, 1) == false)
check("DeSmuME writedword", memory.writedword(scratch, 7) == true and memory.readdword(scratch) == 7)

-- ---- registers ----------------------------------------------------------
check("getregister sp in RAM", memory.isvalid(emu.getregister("sp")))
check("getregister r29 == sp", emu.getregister("r29") == emu.getregister("sp"))
check("getregister f12 is a number", type(emu.getregister("f12")) == "number")
emu.setregister("zero", 5)
check("zero register stays 0", emu.getregister("zero") == 0)
local okPc, errPc = pcall(emu.setregister, "pc", 0)
check("setregister pc refused", not okPc and tostring(errPc):find("pc can't") ~= nil, errPc)
local okBad, errBad = pcall(emu.getregister, "x99")
check("unknown register name refused", not okBad and tostring(errBad):find("unknown register") ~= nil, errBad)

-- ---- execute callbacks ------------------------------------------------------
-- Hook the code the CPU is running right now (pc and return address); the
-- test program runs the same loop every frame, so at least one should fire.
local execHits, execArg = 0, nil
local candidates = {}
event.onframeend(function()
  if #candidates == 0 then
    for _, r in ipairs({ "pc", "ra" }) do
      local a = emu.getregister(r) & ~3
      if memory.isvalid(a) then candidates[#candidates + 1] = a end
    end
    for _, a in ipairs(candidates) do
      event.onmemoryexecute(function(addr) execHits = execHits + 1; execArg = addr end, a, "exec-test")
    end
  end
end, "exec-setup")
local okAlign, errAlign = pcall(event.onmemoryexecute, function() end, 0x08800002)
check("execute address must be aligned", not okAlign and tostring(errAlign):find("multiple of 4") ~= nil, errAlign)

-- ---- write / read callbacks -------------------------------------------------
-- Watch all of user RAM for a moment: any store or load the program makes trips it.
local writeHits, writeArgs, readHits = 0, nil, 0
local wId = event.onmemorywrite(function(addr, value, size, pc)
  writeHits = writeHits + 1
  if writeArgs == nil then writeArgs = { addr, value, size, pc } end
end, 0x08800000, "write-test", 0x01800000)
local rId = event.onmemoryread(function(addr, size, pc) readHits = readHits + 1 end, 0x08800000, "read-test", 0x01800000)
local scriptWriteSeen = false
event.onmemorywrite(function() scriptWriteSeen = true end, scratch + 0x40, "own-write")

frames(60)
check("execute callback fired", execHits > 0, "candidates " .. #candidates .. ", hits " .. execHits)
check("execute callback gets its address", execArg == nil or execArg == candidates[1] or execArg == candidates[2], execArg)
check("write callback fired", writeHits > 0, writeHits)
check("read callback fired", readHits > 0, readHits)
if writeArgs then
  local addr, value, size, pc = table.unpack(writeArgs)
  check("write callback args", memory.isvalid(addr) and (size == 1 or size == 2 or size == 4 or size > 4) and type(pc) == "number",
    string.format("addr %08X size %s pc %08X value %s", addr, tostring(size), pc, tostring(value)))
  print(string.format("first write: addr %08X value %s size %d pc %08X", addr, tostring(value), size, pc))
end
memory.write_u32(scratch + 0x40, 99)
frames(2)
check("script's own writes don't trigger its write callback", not scriptWriteSeen)
check("unregister write watch", event.unregisterbyid(wId))
check("unregister read watch", event.unregisterbyid(rId))
check("unregister exec watches", event.unregisterbyname("exec-test"))
frames(3)
local w, r, e = writeHits, readHits, execHits
frames(10)
check("callbacks stop after unregistering", writeHits == w and readHits == r and execHits == e,
  string.format("%d %d %d", writeHits - w, readHits - r, execHits - e))

-- DeSmuME alias. Compared with a direct watch on the same range over 120
-- frames: once Cross has been pressed (by the joypad test, or by a previous
-- run before a reload) the test program writes in bursts about every two
-- seconds, so a short window can see nothing.
local aliasHits, directHits = 0, 0
local aliasId = memory.register(0x08800000, 0x01800000, function() aliasHits = aliasHits + 1 end)
local directId = event.onmemorywrite(function() directHits = directHits + 1 end, 0x08800000, "direct", 0x01800000)
frames(120)
check("memory.register (DeSmuME) works", aliasHits > 0, aliasHits)
check("memory.register sees the same writes as onmemorywrite", aliasHits == directHits,
  aliasHits .. " vs " .. directHits)
event.unregisterbyid(aliasId)
event.unregisterbyid(directId)

-- ---- joypad -----------------------------------------------------------------
local pad = joypad.get()
check("joypad.get has buttons", pad.Cross ~= nil and pad.Start ~= nil and type(pad.AnalogX) == "number")
joypad.set({ Cross = true, AnalogX = 1.0 }, 3)
frames(1)
check("joypad.set presses Cross", joypad.get().Cross == true)
check("joypad.set moves the stick", joypad.get().AnalogX > 0.9, joypad.get().AnalogX)
frames(4)
check("joypad.set releases after the frames", joypad.get().Cross == false and math.abs(joypad.get().AnalogX) < 0.1)

-- ---- audio ------------------------------------------------------------------
local tone = audio.tone(440, 150, { volume = 0.3, pan = -0.8, wave = "sine" })
check("audio.tone returns a handle", type(tone) == "number", tone)
check("tone is playing", audio.isplaying(tone))
frames(30)
check("tone stops after its duration", not audio.isplaying(tone))
check("audio.tone wave check", not pcall(audio.tone, 440, 100, { wave = "kazoo" }))

-- Write a tiny WAV file with Lua, then play it.
local function u32le(n) return string.char(n & 255, (n >> 8) & 255, (n >> 16) & 255, (n >> 24) & 255) end
local function u16le(n) return string.char(n & 255, (n >> 8) & 255) end
local rate, count = 22050, 4410
local samples = {}
for i = 0, count - 1 do
  local v = math.floor(math.sin(i * 2 * math.pi * 660 / rate) * 8000)
  samples[#samples + 1] = u16le(v & 0xFFFF)
end
local data = table.concat(samples)
local wav = "RIFF" .. u32le(36 + #data) .. "WAVEfmt " .. u32le(16) .. u16le(1) .. u16le(1) ..
  u32le(rate) .. u32le(rate * 2) .. u16le(2) .. u16le(16) .. "data" .. u32le(#data) .. data
local wavPath = emu.getdatadir() .. "selftest_beep.wav"  -- scripts may write only in their data folder
check("emu.getscriptdir ends with /", emu.getscriptdir():sub(-1) == "/", emu.getscriptdir())
local f = io.open(wavPath, "wb")
if f then f:write(wav); f:close() end
local h = audio.play(wavPath, { volume = 0.3, pan = 0.8 })
check("audio.play a WAV file", type(h) == "number", h)
check("audio.setpan / setvolume / setpitch", audio.setpan(h, 0) and audio.setvolume(h, 0.2) and audio.setpitch(h, 1.2))
check("audio.stop", audio.stop(h) and not audio.isplaying(h))
local missing, why = audio.play("no_such_file.wav")
check("missing sound file returns nil + reason", missing == nil and type(why) == "string", why)
-- The file stays: scripts may not delete files (see Scripting/Sandbox.cpp); the next run overwrites it.

-- ---- hotkeys ----------------------------------------------------------------
local key, err = input.registerhotkey("selftestKey", "Ctrl+Shift+F9", function()
  print("KEYTEST script hotkey ran")
  speak("Script hotkey works")
end, "self test hotkey")
check("registerhotkey returns the key", key == "Ctrl+Shift+F9", key or err)
local k2, e2 = input.registerhotkey("clash", "Ctrl+Shift+R", function() end)
check("hotkey clash with built-in refused", k2 == nil and tostring(e2):find("already used") ~= nil, e2)
local k3, e3 = input.registerhotkey("punct", "Ctrl+Comma", function() end)
check("punctuation key name refused", k3 == nil and tostring(e3):find("punctuation") ~= nil, e3)
local k4, e4 = input.registerhotkey("byScancode", "Ctrl+Shift+sc:0x2F", function()
  print("KEYTEST scancode hotkey ran")
end, "scancode test")
check("scancode hotkey accepted", k4 == "Ctrl+Shift+sc:0x2F", k4 or e4)
local k5 = input.registerhotkey("plainKey", "Numpad5", function() print("KEYTEST plain hotkey ran") end, "plain key test")
check("plain key accepted", k5 == "Numpad5", k5)

if failures == 0 then
  print("SELFTEST RESULT: ALL PASSED")
  speak("Self test passed, ready for key test")
else
  print("SELFTEST RESULT: " .. failures .. " FAILED")
  speak("Self test failed, " .. failures .. " failures")
end
