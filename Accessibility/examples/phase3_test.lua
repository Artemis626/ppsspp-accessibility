-- Phase 3 test script: works with any game.
--
-- Hotkeys (all changeable in accessibility/hotkeys.ini, section [<game ID>]):
--   Ctrl+Shift+1   say the watched value now
--   Ctrl+Shift+2   stereo test: five tones from left to right
--   Ctrl+Shift+3   say which PSP buttons are held right now
--   Ctrl+Shift+4   press Cross for the game (input injection test)
--   Numpad5        say "Numpad works" (in game only, not in menus; needs Num Lock)
-- Built-in hotkeys: Ctrl+Shift+H lists them all.
--
-- Memory:
--   Watches ADDRESS with a WRITE callback: every time the game writes a NEW
--   value there you hear a short click and the value (at most 4 per second).
--   If you set EXEC_ADDRESS, you hear "code ran" when the game executes the
--   instruction there, with the first argument register a0.

local ADDRESS = 0x08800000   -- the address to watch (as shown by PPSSPP's debugger)
local SIZE = 4               -- bytes the game writes there: 1, 2 or 4
local EXEC_ADDRESS = nil     -- e.g. 0x08804618; must be a multiple of 4. nil = off

-- ---- helpers ----------------------------------------------------------------
local readers = { [1] = memory.read_u8, [2] = memory.read_u16, [4] = memory.read_u32 }
local read = readers[SIZE] or error("SIZE must be 1, 2 or 4")

-- Speak at most every `seconds`, so busy memory can't flood speech.
local function limiter(seconds)
  local nextAllowed = 0
  return function(text)
    local now = emu.vblankcount()
    if now >= nextAllowed then
      speak(text)
      nextAllowed = now + math.floor(seconds * 60)
    end
  end
end

-- ---- start ------------------------------------------------------------------
audio.tone(660, 80, { volume = 0.4 })
speak("Phase 3 test ready. Control shift H lists hotkeys.")

-- ---- memory write watch -------------------------------------------------------
local sayWrite = limiter(0.25)
local last = read(ADDRESS)
event.onmemorywrite(function(addr, value, size, pc)
  -- value is what the instruction writes (nil for some rare store types),
  -- so read the address on the next frame if it's missing.
  if value == nil then return end
  if value ~= last then
    last = value
    audio.tone(1200, 15, { volume = 0.3 })
    sayWrite(string.format("Written %d at pc %08X", value, pc))
  end
end, ADDRESS, "watch", SIZE)

-- ---- optional execute watch ------------------------------------------------------
if EXEC_ADDRESS then
  local sayExec = limiter(1)
  event.onmemoryexecute(function()
    sayExec(string.format("Code ran, a0 is %08X", emu.getregister("a0")))
  end, EXEC_ADDRESS, "exec watch")
end

-- ---- hotkeys -----------------------------------------------------------------
input.registerhotkey("sayValue", "Ctrl+Shift+1", function()
  local v = read(ADDRESS)
  speak(v and ("Value " .. v) or "Address not readable", true)
end, "say the watched value")

input.registerhotkey("stereoTest", "Ctrl+Shift+2", function()
  -- Five tones from full left to full right, rising in pitch: one per frame
  -- group, played by a short frame callback that removes itself at the end.
  local step, wait = 0, 0
  local id
  id = event.onframeend(function()
    wait = wait - 1
    if wait > 0 then return end
    local pan = -1 + step * 0.5
    audio.tone(400 + step * 100, 150, { volume = 0.5, pan = pan })
    step = step + 1
    wait = 15  -- quarter of a second at 60 frames per second
    if step > 4 then event.unregisterbyid(id) end
  end, "stereo test")
end, "stereo test")

input.registerhotkey("sayButtons", "Ctrl+Shift+3", function()
  local pad = joypad.get()
  local held = {}
  for _, name in ipairs({ "Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle", "L", "R", "Start", "Select" }) do
    if pad[name] then held[#held + 1] = name end
  end
  -- Round to one decimal; "+ 0.0" after rounding turns -0.0 into 0.0.
  local function r(x) return math.floor(x * 10 + 0.5) / 10 + 0.0 end
  local stick = string.format("stick %.1f, %.1f", r(pad.AnalogX), r(pad.AnalogY))
  speak((#held > 0 and table.concat(held, ", ") or "No buttons") .. ", " .. stick, true)
end, "say held buttons")

input.registerhotkey("pressCross", "Ctrl+Shift+4", function()
  joypad.set({ Cross = true }, 4)
  speak("Pressed cross", true)
end, "press Cross for the game")

input.registerhotkey("numpadTest", "Numpad5", function()
  speak("Numpad works", true)
end, "numpad test")
