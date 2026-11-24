# User guide

How to use PPSSPP Accessibility Scripting by yourself. Part 1 is for anyone who wants to play. Part 2 is for someone who wants to make a game speak properly by writing a small script. No programming background is assumed in part 1; part 2 assumes you can edit a text file and are willing to copy and adapt short examples.

Written 2026-11-15. Everything described here exists in the build. Where something is unfinished, it says so.

## Part 1: playing

### What you need

- **Windows** and the **NVDA** screen reader, running before you start the emulator.
- **The build folder.** It holds `PPSSPPWindows64.exe`, `nvdaControllerClient.dll`, and the folders `scripts` and `accessibility`.
- **Your own game images.** None come with the emulator.

### Starting

1. Start `PPSSPPWindows64.exe`. You hear "PPSSPP accessibility ready".
2. Move through PPSSPP's menus with the arrow keys and Enter. The item under focus is spoken.
3. Choose a game. Its identifier, and whether a script was found for it, are written to `accessibility/logs/access.log`.

### What is spoken without a script

The emulator tries to read any game by itself. What you get depends on the game:

- **Dialogue and help lines** are usually read a moment after they appear.
- **The selected menu item** is read when you move the selection, in menus where the emulator has worked out how the selection is shown. It learns that by seeing the selection move, so the first item of a menu is not announced until you move once.
- **Numbers** such as hit points are read in games that draw them from a strip of digits.

Expect gaps. Some screens stay silent. Early in a session some letters are missing and are spoken as "unknown character" marks, or the line is skipped; this improves as more text is seen. Some games are not read at all (see "What works in which game" below).

### Keys

All keys can be changed in `accessibility/hotkeys.ini`.

| Keys | What it does |
|---|---|
| Ctrl+Shift+A | Changes what is read unasked: nothing, system dialogs only, or dialogs and game text |
| Ctrl+Shift+T | Reads all the text found on the screen now |
| Ctrl+Shift+D | Says how this game draws its text (useful when nothing is read) |
| Ctrl+Shift+L | Writes a report of what was found on the screen (see Part 2) |
| Ctrl+Shift+K | Records ten seconds for a developer (see "When something goes wrong") |
| Ctrl+Shift+R | Repeats the last message |
| Ctrl+Shift+PageUp / PageDown | Moves back and forward through what was spoken |
| Ctrl+Shift+C | Copies the last message to the clipboard |
| Ctrl+Shift+S | Stops speech |
| Ctrl+Shift+H | Lists every hotkey, including those a script added |
| Ctrl+Shift+F12 | Reloads the script and the hotkey file |

If automatic reading is too talkative (in a battle, numbers can be), press Ctrl+Shift+A to turn game text off, and use Ctrl+Shift+T when you want the screen read.

### Using someone else's script

A script makes one game speak far better than the emulator can alone, because its author told it what matters.

1. Find the game's identifier, for example `ULES01500`. It is on the "Game started" line of `accessibility/logs/access.log`, and it is the name of the game's profile file.
2. Put the script in the `scripts` folder as `<identifier>.lua`, for example `scripts/ULES01500.lua`.
3. Start the game. The script loads by itself.
4. Press Ctrl+Shift+H to hear the keys the script added.

A script cannot start programs, delete files or reach files outside its own folder: the emulator takes those abilities away from every script. It can still read and change the game's memory, which may spoil a save, so keep a backup of saves you care about and take scripts from people you trust.

### What the emulator remembers

For each game it keeps a small text file, `accessibility/profiles/<identifier>.ini`. It holds what was learned: letter codes, the letters of fonts, and addresses found. It is read when the game starts. You can delete it at any time; everything is then learned again.

### When something goes wrong

- **Nothing is spoken at all:** check that NVDA is running. Everything spoken is also written to `accessibility/logs/access.log`.
- **A screen stays silent or is read wrongly:** press Ctrl+Shift+K while that screen shows. The emulator records ten seconds into `accessibility/logs/recordings/`. That folder is what a developer needs to find the cause. It contains a copy of the game's memory, so treat it like a save file.
- **A script fails:** the error is written to `access.log` with the line number.

### What works in which game (November 2026)

| Game | Without a script |
|---|---|
| Tactics Ogre: Let Us Cling Together (Europe) | Dialogue, names, help lines, commands, hit points and similar numbers. Not the map, elevation or unit positions. |
| Persona 2 Innocent Sin and Eternal Punishment | Dialogue; the main menu and options with the selected item. Name entry is not read. |
| Patapon 3 | Speech bubbles and hints, with missing letters early on. |
| Final Fantasy Tactics: The War of the Lions | Nothing. |

Other games have not been tried. Many will be read partly; some not at all.

## Part 2: making a game speak

The aim is a script that says the right thing at the right time. The emulator gives you two kinds of help: tools that find where the game keeps its text and numbers, and a small set of functions to read them and speak.

### The idea in one paragraph

A game keeps everything it shows in its memory: the dialogue line, your money, which menu item is selected. Each lives at an address, a number such as `0x08AD8438`. A script reads an address and speaks what it finds, either when you press a key or when the value changes. The hard part has always been finding the addresses. The Discovery layer does much of that for you.

### Step 1: see what the emulator already finds

1. Start the game and go to the screen you care about.
2. Press Ctrl+Shift+L. A report is written to `accessibility/logs/discovery-report.txt`. Open it in a text editor.

The report has these sections:

- **TEXT FOUND:** every text read, with its position on screen.
- **LINES NOT READ YET:** rows it could not read, and why.
- **SELECTED ITEM:** which text is selected, and how the game shows it.
- **NUMBERS ON SCREEN:** each number, with the addresses where it may be kept. The list shortens every time the number changes on screen, until one or a few remain.
- **FONTS WORKED OUT:** the fonts and letter codes it understands.

### Step 2: read the profile

Open `accessibility/profiles/<identifier>.ini`. Its `[findings]` section is an index that grows as you play:

```ini
[findings]
text = 0x08F71A88: Message Speed
record = 0x08AD84B4, 12 bytes: numbers shown together on screen
selection = shown by a mark drawn from texture 0x091D5A00
```

- A **text** line says where a string was found in memory.
- A **record** line says where numbers that appear together are kept together, which is usually one character's or one unit's data.

### Step 3: pin a number down

Suppose the report lists hit points 143 with thirty possible addresses.

1. Make the number change in the game: take damage, spend money.
2. Press Ctrl+Shift+L again. Addresses that did not change with it are gone.
3. Repeat until one address is left, or a few that always agree (games keep copies).

### Step 4: write the script

Create `scripts/<identifier>.lua`. This complete example speaks a 16-bit number on a key press and whenever it changes:

```lua
-- The address found in step 3.
local HP = 0x08AD84B4

-- Say it when Ctrl+Shift+G is pressed.
input.registerhotkey("sayHP", "Ctrl+Shift+G", function()
  speak("Hit points " .. memory.read_u16(HP))
end, "Say hit points")

-- Say it whenever the game changes it.
event.onmemorywrite(function(addr, value)
  speak("Hit points " .. value, true)
end, HP, "hpChanged", 2)
```

Press Ctrl+Shift+F12 to reload the script after each edit; you do not need to restart the game.

A script can keep its own files (notes, settings) in its folder: `io.open("notes.txt", "w")` opens `notes.txt` beside the script. Files elsewhere, starting programs and deleting files are not allowed; the technical README lists exactly what is taken away, under "Script sandbox".

To speak the selected menu item yourself, instead of relying on automatic reading:

```lua
local last = ""
event.onframeend(function()
  for _, run in ipairs(text.onscreen()) do
    if run.selected and run.text ~= last then
      last = run.text
      speak(run.text, true)
    end
  end
end)
```

A larger worked example is `Accessibility/examples/ULES01500.lua` in the source. The full list of functions is in [the technical README](../README.md), under "Lua API".

### Step 5: addresses other people found

Cheat-code collections list thousands of addresses per game. A CWCheat line `_L 0x202D8438 0x...` means an address; `memory.fromcwcheat(0x202D8438)` converts it. Money, item counts and character statistics are often there already.

### What you cannot yet do without help

Be aware of the limits before you invest time:

- **Text the emulator does not read cannot be found for you.** If a screen's text is under "LINES NOT READ YET", or the game is not read at all, you are back to searching memory by hand with PPSSPP's own debugger.
- **A recording can only be studied with programming tools** (a replay program and scripts in the source). There is no spoken tool for it yet.
- **There is no spoken explorer.** A planned tool would let you move through what the layer found, name things and build a map of the game by keyboard and speech alone. It is not built. Today the report and the profile are text files you read.
- **The profile's findings are an index, not a map.** They say where things were found, not what they mean. Naming them is still your work.
- **Lists and positions on a map** (a unit's place on a battlefield) are not found.
