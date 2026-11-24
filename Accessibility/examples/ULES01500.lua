-- Tactics Ogre: Let Us Cling Together (Europe, ULES01500): basic test script.
--
-- Not an accessibility mod: a first check that publicly known addresses can
-- be read with the Phase 3 API. Loads automatically for this game when
-- placed in scripts\ as ULES01500.lua.
--
-- The addresses come from the community CWCheat database
-- (github.com/Saramagrean/CWCheat-Database-Plus-), section ULES-01500:
--   "99999 GOTH"            _L 0x202D8438 -> Goth, 4 bytes at 0x08AD8438
--   "Decoy Scroll x99"      _L 0x002E8530 -> 1 byte at 0x08AE8530
--   "Demonpetal Scroll x99" _L 0x002E853B -> 1 byte at 0x08AE853B
--   "Glass Pumpkin x30"     _L 0x002E8543 -> 1 byte at 0x08AE8543
--   "GOTH Doesn't Decrease" _L 0x2000E558 -> code at 0x0880E558 (uncertain:
--                           the database lists the same value for On and Off)
-- A CWCheat code address 0x2XXXXXXX (or 0x0XXXXXXX) becomes
-- memory.fromcwcheat(code), which adds the 0x08800000 base.
--
-- Hotkeys:
--   Ctrl+Shift+G   say your Goth
--   Ctrl+Shift+I   say the three item counts
--   Ctrl+Shift+E   turn the execute test on or off (off at start)
-- Automatic: every change of Goth is spoken, with a high tone when it goes
-- up and a low tone when it goes down.

local GOTH = memory.fromcwcheat(0x202D8438)
local ITEMS = {
  { name = "Decoy Scroll", addr = memory.fromcwcheat(0x002E8530) },
  { name = "Demonpetal Scroll", addr = memory.fromcwcheat(0x002E853B) },
  { name = "Glass Pumpkin", addr = memory.fromcwcheat(0x002E8543) },
}
local GOTH_CODE = memory.fromcwcheat(0x2000E558)

print(string.format("Goth address %08X, items %08X %08X %08X, code %08X",
  GOTH, ITEMS[1].addr, ITEMS[2].addr, ITEMS[3].addr, GOTH_CODE))
speak("Tactics Ogre test script, disc version " .. gameinfo.getversion() ..
  ". Load a save, then press control shift G for your Goth.")

-- ---- Goth -------------------------------------------------------------------
-- Before a save is loaded this memory holds nothing meaningful, so values
-- are only trusted once they look like a real amount of money.
local function plausible(v) return v ~= nil and v <= 9999999 end

input.registerhotkey("sayGoth", "Ctrl+Shift+G", function()
  local goth = memory.read_u32(GOTH)
  if plausible(goth) then
    speak(goth .. " Goth", true)
  else
    speak("Goth not available yet, value " .. tostring(goth), true)
  end
end, "say Goth")

-- Write watch: fn(addr, value, size, pc). Speak only real changes, and at most
-- every half second, in case the game rewrites the value in a loop.
local lastGoth = memory.read_u32(GOTH)
local nextSpeech = 0
event.onmemorywrite(function(addr, value, size, pc)
  if size ~= 4 or not plausible(value) or value == lastGoth then return end
  local up = lastGoth ~= nil and value > lastGoth
  lastGoth = value
  audio.tone(up and 880 or 330, 60, { volume = 0.35 })
  if emu.vblankcount() >= nextSpeech then
    speak("Goth " .. value)
    nextSpeech = emu.vblankcount() + 30
  end
  print(string.format("Goth written: %d by pc %08X", value, pc))
end, GOTH, "goth watch", 4)

-- ---- items ------------------------------------------------------------------
input.registerhotkey("sayItems", "Ctrl+Shift+I", function()
  local parts = {}
  for _, item in ipairs(ITEMS) do
    parts[#parts + 1] = item.name .. " " .. tostring(memory.read_u8(item.addr))
  end
  speak(table.concat(parts, ", "), true)
end, "say item counts")

-- ---- optional execute test --------------------------------------------------
local execId = nil
input.registerhotkey("toggleExec", "Ctrl+Shift+E", function()
  if execId then
    event.unregisterbyid(execId)
    execId = nil
    speak("Execute test off", true)
    return
  end
  local nextExecSpeech = 0
  execId = event.onmemoryexecute(function(addr)
    print(string.format("code at %08X ran, a0=%08X v0=%08X", addr, emu.getregister("a0"), emu.getregister("v0")))
    if emu.vblankcount() >= nextExecSpeech then
      speak("Goth code ran")
      nextExecSpeech = emu.vblankcount() + 60
    end
  end, GOTH_CODE, "goth code")
  speak("Execute test on. Buy something in a shop.", true)
end, "execute test on or off")
