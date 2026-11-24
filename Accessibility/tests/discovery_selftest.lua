-- Automated self-test for the Discovery layer's Phase D1 Lua functions.
-- Run by tools/discovery-test.ps1 on PSP test programs; prints PASS/FAIL
-- lines and every text it sees to accessibility/logs/access.log.

local failures = 0
local function check(name, ok, detail)
  if ok then
    print("PASS " .. name)
  else
    failures = failures + 1
    print("FAIL " .. name .. (detail and (": " .. tostring(detail)) or ""))
  end
end

-- The functions exist and return the documented shapes, even before any text.
check("text.onscreen returns a table", type(text.onscreen()) == "table")
check("text.locate returns a table", type(text.locate("no such text anywhere")) == "table")
check("text.locate rejects a missing argument", not pcall(text.locate))
check("text.locate rejects a bad max", not pcall(text.locate, "abc", 0))
local s = discover.survey()
check("discover.survey has a verdict", type(s) == "table" and type(s.verdict) == "string", s and s.verdict)
check("discover.survey has counters", type(s.counters) == "table")
check("discover.survey has samples", type(s.samples) == "table")

-- Watch the screen: print each text the first time it is seen.
local seen = {}
local sawDialog, sawSelected, located = false, false, false
local frames = 0

event.onframeend(function()
  frames = frames + 1
  for _, run in ipairs(text.onscreen()) do
    if not seen[run.text] then
      seen[run.text] = true
      print(string.format("DISCOVERY TEXT [%s, confidence %.2f%s] %s",
        run.source, run.confidence, run.selected and ", selected" or "", run.text))
      if run.source == "system_dialog" then
        sawDialog = true
        check("dialog run has a place on screen", type(run.x) == "number" and run.w > 0 and run.h > 0)
        check("dialog run is certain", run.confidence == 1)
        check("run has an id", type(run.id) == "number" and run.id > 0)
        -- The dialog's message comes from the game's own memory: find it there.
        if #run.text >= 20 and not located then
          local hits = text.locate(run.text)
          located = #hits > 0
          check("text.locate finds the dialog message in game memory", located, #hits)
          if located then
            print(string.format("DISCOVERY LOCATED at 0x%08X as %s", hits[1].addr, hits[1].encoding))
            check("located text reads back", memory.readstring(hits[1].addr, #run.text, "utf8") == run.text)
          end
        end
      end
    end
    if run.selected then sawSelected = true end
  end
  if frames == 240 then
    local survey = discover.survey()
    print("DISCOVERY SURVEY " .. survey.verdict)
    for name, count in pairs(survey.counters) do
      print("DISCOVERY COUNTER " .. name .. " = " .. count)
    end
    print("DISCOVERY SAW dialog=" .. tostring(sawDialog) .. " selected=" .. tostring(sawSelected))
    print("DISCOVERY SELFTEST RESULT: " .. (failures == 0 and "all passed" or (failures .. " FAILED")))
  end
end, "discovery selftest")
