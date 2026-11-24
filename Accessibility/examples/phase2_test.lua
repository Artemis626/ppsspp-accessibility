-- Phase 2 test script: works with any game.
--
-- What it does:
--   1. When the game boots, says the game's title and ID.
--   2. Watches one memory address and speaks its value whenever it changes.
--   3. Every HEARTBEAT_SECONDS, says how many frames have passed, so you can
--      hear that the per-frame callback keeps running.
--
-- To watch a value you found in your memory research, put its address
-- below. Use the address as PPSSPP's debugger shows it (starts with 0x08 or
-- 0x09). For a CWCheat address, write memory.fromcwcheat(0x2XXXXXXX) instead.

local ADDRESS = 0x08800000   -- the address to watch
local SIZE = 1               -- 1, 2 or 4 bytes
local SIGNED = false         -- true if the value can be negative
local HEARTBEAT_SECONDS = 30 -- 0 turns the heartbeat off

-- Pick the right reader for SIZE and SIGNED.
local readers = {
  [1] = SIGNED and memory.read_s8 or memory.read_u8,
  [2] = SIGNED and memory.read_s16 or memory.read_u16,
  [4] = SIGNED and memory.read_s32 or memory.read_u32,
}
local read = readers[SIZE]
if read == nil then
  error("SIZE must be 1, 2 or 4")
end

-- 1. Say which game this is. The ID is also the file name an auto-loaded
-- script needs: scripts/<ID>.lua
speak("Game: " .. gameinfo.gettitle() .. ", ID " .. gameinfo.getid())
print(string.format("Watching 0x%08X, %d byte(s)", ADDRESS, SIZE))

-- 2. Speak the value whenever it changes. Runs once per frame.
local last = nil
event.onframeend(function()
  local value = read(ADDRESS)
  if value ~= last then
    if value == nil then
      speak("Address not readable")
    else
      speak("Value " .. value)
    end
    last = value
  end
end, "watch value")

-- 3. Heartbeat, written in the BizHawk/DeSmuME main-loop style.
-- vblankcount runs at 60 per second whatever the game's frame rate.
if HEARTBEAT_SECONDS > 0 then
  local nextBeat = emu.vblankcount() + HEARTBEAT_SECONDS * 60
  while true do
    if emu.vblankcount() >= nextBeat then
      speak("Still running, " .. emu.framecount() .. " frames")
      nextBeat = nextBeat + HEARTBEAT_SECONDS * 60
    end
    emu.frameadvance()
  end
end
