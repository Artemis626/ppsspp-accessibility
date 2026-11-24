# PPSSPP Accessibility Scripting: Phase 0 Plan

> **Status note (9 November 2026):** Phases 0 to 3 are done. This document is the original design and stays as written, apart from the "Your answers" section. For current status, the API as built, and every change from this plan, see [../README.md](../README.md). The main changes are:
>
> - The Lua API uses Lua's C API instead of sol2.
> - Spoken menus were done in Phase 1.
> - Some Phase 4 items were done early.

This is the architecture proposal for the accessibility-scripting fork of PPSSPP. No code has been written yet. Each section says what I propose and why.

## 1. Base release

### Decision

Base the fork on **PPSSPP v1.20.4**, tag `v1.20.4`, commit `fa50bb197`, released 16 May 2026.

### Why

- GitHub lists v1.20.4 as "Latest". Nothing newer than it has a number; everything after it is the development branch.
- It matches the copy installed in `E:\PPSSPP` (the file version says v1.20.4). So when we compare behaviour, "it worked in my normal PPSSPP" means the same code.
- The source is already cloned, with all 23 submodules, at `E:\Claude projects\PPSSPP Accessibility scripting\ppsspp`. I cloned it "blobless": the full commit history is there, but old file contents download only when needed. That history is what lets us move our changes onto v1.21 later with `git rebase`.

## 2. What I found in the PPSSPP source

### PPSSPP already contains Lua

This is the most important finding. v1.20.4 already ships:

- **Lua 5.4.7**, as the `ext/lua` submodule. On Windows it is compiled into `Common.vcxproj`.
- **sol2 3.5.0**, as `ext/sol`. sol2 is a header-only C++ library that binds C++ functions to Lua.
- **`Core/LuaContext.cpp`**. This is a tiny Lua console used only by the built-in ImGui debugger. It has `print` and `r32`, and nothing else.

What this means for us:

- We add no new Lua dependency and no new build step for Lua.
- Upstream keeps Lua and sol2 up to date for us, so they come along on every rebase.
- We will **not** extend `LuaContext`. Upstream owns that file and is clearly still developing it (it has TODO comments), so editing it would cause merge conflicts on every update. Our scripting gets its **own** Lua state in its own files. The two states can exist side by side without problems.

### The per-frame hook point already exists

`Core/HW/Display.h` has a public listener API: `__DisplayListenFlip(callback, userdata)`. PPSSPP calls the registered callbacks every time the game presents a frame. RetroAchievements and the WebSocket debugger already use it.

- Our per-frame callback registers through this API, so it needs **no changes to core code**.
- It fires on the emulation thread, between CPU instructions. At that moment memory is consistent and safe to read.
- Note: the PSP "frame rate" depends on the game. Many games run at 30 fps, some at 60, some at 20. A "frame" in our API means a frame the game actually presented, which is how BizHawk's `onframeend` behaves too. For scripts that need real time, I'll add `emu.vblankcount()`, which always counts at 60 Hz.

### Memory and execution callbacks: the breakpoint system already does the hard part

`Core/Debugger/Breakpoints.h` (`g_breakpoints`) manages execution breakpoints and memory checks ("memchecks"), for reads, writes, and address ranges. This matters because PPSSPP runs game code through a **JIT** (it translates MIPS code into x86 code). A naive hook in the memory-read functions would miss nearly all accesses, because JIT-compiled code never calls those functions.

The breakpoint system already handles every CPU backend:

- When the JIT compiles code at an address that has a breakpoint, it inserts a call to `ExecBreakPoint`. This is true for the x86 JIT, the IR JIT, ARM64 and the interpreter.
- When any memchecks exist, the JIT adds a range check to each load and store.
- Memory written by the emulated OS instead of the CPU (file reads into RAM, memcpy, and so on) goes through `MemBlockInfo`, which calls the same `ExecMemCheck`.
- A breakpoint whose action is not "ignore" gets compiled in, **even if it never pauses**. A "log only" breakpoint already works this way.

So the plan is: **add one new action flag, `BREAK_ACTION_SCRIPT`**, and two small calls to our hook, in `ExecBreakPoint` and `MemCheck::Action`. That is about 15 lines in one upstream file. It gets JIT-correct exec, read and write callbacks on every CPU backend.

Two details I verified that shape the API:

- **CPU write checks fire just before the write happens.** The memory still holds the old value. `MIPSAnalyst` can decode the instruction, so for CPU writes I can pass the callback the **value about to be written**, as well as the address and size. For OS-level writes the data is already in memory. The callback receives the same arguments in both cases, and the documentation will explain the difference.
- **Some OS-level memory notifications may come from PPSSPP's I/O thread**, not the emulation thread. Lua is single-threaded, so if a hit arrives on another thread we queue it and run it on the emulation thread at the next safe point. This prevents random crashes.

Performance: while any memory callbacks are registered, the JIT slows down, because every load and store gets checked. Exec callbacks cost almost nothing apart from the address they watch. The modder documentation will say so: prefer exec hooks and per-frame polling, and use small write-watch ranges.

### Controller input

- Read: `__CtrlPeekButtons()` and `__CtrlPeekAnalog()`, in `Core/HLE/sceCtrl.h`.
- Inject: `__CtrlUpdateButtons(set, clear)` and `__CtrlSetAnalogXY()`. The WebSocket debugger already uses these exact functions for remote input (`InputSubscriber.cpp`), so they are a tested, supported route. No core changes needed.

### Keyboard (for script hotkeys)

- On Windows, keys arrive through Raw Input (`Windows/RawInput.cpp`). Each key is converted from a Windows virtual-key code to a PPSSPP key code (`NKCODE_*`), then passed to `NativeKey()` in `UI/NativeApp.cpp`.
- One line at the top of `NativeKey()` lets our hotkey manager see each key first. If the key is one of our bindings, we consume it, so the game and PPSSPP never see it.
- **Threading:** `NativeKey` runs on the **window thread**, not the emulation thread. So hotkey presses are put in a queue and run by Lua on the emulation thread. Built-in speech keys (repeat last, copy last, stop) need no Lua, so they are handled immediately. That means they work even while the game is paused or a menu is open.

### Game information

`g_paramSFO.GetDiscID()` returns the game ID, for example `ULUS10041`. `GetValueString("TITLE")` returns the title, and `"DISC_VERSION"` the disc version. Homebrew gets a generated fake ID, so it still works with ID-based script loading.

### Emulator messages (saved, loaded, errors)

Nearly every message PPSSPP shows on screen goes through **one function**: `OnScreenDisplay::Show()` in `Common/System/OSD.cpp`. That covers save state saved or loaded, savedata messages, error popups and warnings. One callback hook there lets us **speak all of them**, filtered by type (info, success, warning, error) as set in a config file. Boot failures are shown differently, through a text view in EmuScreen, so they need one extra explicit call.

### Bonus: PPSSPP's own menus could be made to speak

Every PPSSPP menu widget already has `DescribeText()`, which returns a text description of the item. Every focus change goes through one function, `UI::SetFocusedView()` in `Common/UI/Root.cpp`. A small hook there could speak the focused menu item, which would make PPSSPP's own menus usable with speech. This is **not in your requested phases**. I list it as an optional extra in section 9.

### Lifecycle (game start and stop)

- **Game booted:** `EmuScreen::bootComplete()` in `UI/EmuScreen.cpp`. RetroAchievements uses this point too. Memory is ready and the game ID is known, so this is where we load scripts.
- **Game stopped:** next to the `Achievements::UnloadGame()` calls in `EmuScreen.cpp`, which is where we unload scripts.
- **App start and exit:** `NativeInit()` and `NativeShutdown()` in `UI/NativeApp.cpp`. Speech and audio start here so they work before any game is loaded. `NativeInit()` also parses the command line, so our options go there too.

### Threads, summarised

| Thread | What runs there | What we do |
|---|---|---|
| Emulation thread | CPU, `NativeFrame`, flip listeners, breakpoint and memory hits | **All Lua runs here** |
| Window thread | Raw Input, `NativeKey` | Queue hotkeys; handle built-in speech keys directly |
| I/O thread | Some OS memory notifications | Queue to the emulation thread |
| Speech worker (ours) | NVDA calls | Speech never blocks a frame |
| Audio thread (miniaudio's) | Mixing our sounds | Lua only sends commands |

## 3. Technology choices

### Lua version: 5.4.7, as bundled

- It's already in the tree, and upstream maintains it.
- BizHawk moved to Lua 5.4, so modern BizHawk scripts are mostly compatible. DeSmuME uses 5.1, and the differences that matter (integer division and `bit32`) are small. I'll document them, and keep `bit32` turned on, as PPSSPP already does.
- LuaJIT would be faster, but it would be a second Lua runtime to build and keep updated. Accessibility scripts read a few addresses per frame, so speed doesn't matter here.

### Binding approach: sol2, as bundled

- It's header-only and already in the tree.
- It handles converting between Lua and C++ types, and protected calls that catch errors. Lua coroutines, which `emu.frameadvance()` needs, are easy to support with it.
- `sol.hpp` is slow to compile, so I'll include it in only a few of our `.cpp` files.

### Speech: NVDA Controller Client

- The header on your machine (`fft reader nvda\x64\nvdaController.h`) is the modern client. Besides `speakText` and `cancelSpeech`, it has `brailleMessage`, `speakSsml` and `getProcessId`. NVDA 2026.3.1 is installed.
- **I'll load the DLL at runtime with `LoadLibrary`** instead of linking it at build time. Three reasons:
  - If the DLL is missing, the emulator still starts and tells you so, instead of failing to start with a Windows error dialog.
  - We can check `testIfRunning()` and fall back to another speech engine (see the decisions section).
  - We ship the DLL unchanged beside the exe. That is the cleanest way to meet its LGPL 2.1 licence.
- Speech calls go to a **worker thread with a queue**. NVDA calls are quick but cross processes, so they could stall if NVDA is busy, and they must never stall a frame. "Interrupt" clears the queue and calls `cancelSpeech` first.
- Lua strings are UTF-8. They're converted to UTF-16 for NVDA with PPSSPP's existing `ConvertUTF8ToWString`.
- **Speech history:** a ring buffer of the last 200 messages. Repeats of the same message in a row are stored once. It is shared by scripts and emulator messages.
- Built-in functions: repeat last, copy last to clipboard (Win32 `CF_UNICODETEXT`), previous and next item in the history.
- A free extra: NVDA's own "press Ctrl to silence" works with the controller client too.

### Audio cues: miniaudio, a new single-header library

- It's one header file, public domain or MIT-0 licensed (GPL-compatible), and widely used.
- It decodes **WAV, FLAC and MP3** and generates **sine, square, triangle and sawtooth tones**. It has per-sound **volume and stereo pan**.
- **It plays through its own audio stream, separate from the game's sound.** I chose this deliberately:
  - We don't touch PPSSPP's audio code.
  - Cues still play when the game is paused, fast-forwarded or slowed down.
  - Cues are never distorted by emulation speed.
- Windows mixes the two streams. The cost is about 10 to 20 ms of extra latency, which is fine for cues.
- I'll add OGG later if you want it (miniaudio needs one extra file for it).

### Hotkeys: names and scancodes, no punctuation

- **Key names** cover letters A to Z, digits 0 to 9, F1 to F24, all numpad keys (`Numpad0`… `NumpadPlus`, `NumpadEnter`), arrows, Home, End, PageUp, PageDown, Insert, Delete, Space, Enter, Tab, Escape and Backspace. Modifiers: `Ctrl+`, `Shift+`, `Alt+`. Left and right are treated the same unless you write `LCtrl` or `RCtrl`.
- **Scancodes** use the form `sc:0x1E`, a physical key position. When the binding is loaded it is converted with Windows `MapVirtualKey`. That gives the right key on any layout, Italian QWERTY included.
- **Punctuation keys are refused.** If a script or config line binds an `OEM_*` key (the Windows codes for `,` `.` `;` `'` `ò` `à` and similar), the binding is rejected with a spoken warning that suggests a scancode instead. Virtual-key codes for letters, digits, F-keys and the numpad are the same on every Latin layout, so names are safe there.
- **Scripts bind by action name**, for example `input.registerhotkey("readHP", "Shift+F1", fn, "Read HP")`. The user can change the key in a text file without editing the script:

```ini
; accessibility/hotkeys.ini
[global]
repeatLast   = Ctrl+Shift+R
copyLast     = Ctrl+Shift+C
historyPrev  = Ctrl+Shift+PageUp
historyNext  = Ctrl+Shift+PageDown
stopSpeech   = Ctrl+Shift+S
reloadScript = Ctrl+Shift+F12

[ULUS10041]
readHP = Numpad5
```

- If a binding clashes with one of PPSSPP's own keys, our binding wins and a warning goes to the log. Hotkeys only work while the PPSSPP window has focus.

## 4. Where the code lives

### Our own folder

Everything new goes in `ppsspp/Accessibility/`, built as **its own static library project** (`Accessibility.vcxproj`), added to `Windows/PPSSPP.sln` and linked into the exe.

```text
Accessibility/
  AccessHost.h/.cpp       lifecycle, the Lua state, callback registry, error policy
  Config.h/.cpp           access.ini, hotkeys.ini, aliases.ini
  Log.h/.cpp              accessibility/logs/access.log
  Speech/                 Speech.h, NvdaBackend.cpp, (SapiBackend.cpp), History.cpp
  Audio/                  AudioCues.h/.cpp (miniaudio)
  Input/                  Hotkeys.h/.cpp, KeyNames.cpp
  Lua/                    Api_Memory.cpp, Api_Event.cpp, Api_Joypad.cpp, Api_Speech.cpp,
                          Api_Audio.cpp, Api_GameInfo.cpp, Api_Text.cpp, Api_Emu.cpp
  ext/                    miniaudio.h, nvdaController.h
  docs/                   this plan, later the modder API reference
```

### Core edits (all of them)

Every edit is marked with a `// [ACCESS]` comment so we can find them all with one search.

| File | Change | Approx. lines |
|---|---|---|
| `Core/Debugger/Breakpoints.h/.cpp` | `BREAK_ACTION_SCRIPT` flag, plus a hook call in `ExecBreakPoint` and `MemCheck::Action` | ~15 |
| `UI/EmuScreen.cpp` | Call game-started in `bootComplete`, game-stopped next to `UnloadGame` | ~5 |
| `UI/NativeApp.cpp` | Init and shutdown, command-line options, the hotkey hook in `NativeKey`, the per-host-frame pump in `NativeFrame` | ~12 |
| `Common/System/OSD.h/.cpp` | Optional message-listener callback in `Show()` | ~8 |
| `Windows/PPSSPP.sln`, `Windows/PPSSPP.vcxproj` | Add our project; copy the NVDA DLL after the build | project files |
| `Common/UI/View.h` | Public `Button::GetImageID()`, used to name icon-only buttons (added in Phase 1) | 1 |

That's about 40 lines of C++ in 5 upstream files. Every hook is a single call into our code. A conflict on rebase means re-placing one line, not untangling logic.

### Why a host-frame pump as well as the flip listener

The flip listener only fires while the game is running. `NativeFrame` runs about 60 times a second **even when paused or in menus**. Queued hotkeys, timers and finished sounds are handled there, so script hotkeys keep working on the pause menu.

### Git strategy for future updates

- Branch `access/main` starts from tag `v1.20.4`. Each hook is a separate small commit.
- To update later:
  - `git fetch --tags upstream`
  - `git rebase --onto v1.21 v1.20.4 access/main`
  - Rebuild.
- Our folder never conflicts. Only the ~40 hook lines can.

## 5. Lua API design (draft)

Names follow BizHawk first. DeSmuME aliases are added where they differ, so scripts from either community feel familiar. PSP memory is little-endian, so plain `read_u16` is little-endian, and `_be` variants exist as in BizHawk.

### Memory

```lua
memory.read_u8(a)  read_s8  read_u16  read_s16  read_u32  read_s32  read_float
memory.read_u16_be(a) ...   -- big-endian variants
memory.write_u8(a, v) ... write_float
memory.readbyterange(a, len)        -- returns a table of bytes
memory.writebyterange(a, tbl)
memory.readstring(a, maxlen [, encoding_or_table])   -- stops at 0 or the table's end code
memory.isvalid(a)
memory.fromcwcheat(offset)          -- offset + 0x08800000, for CWCheat-style addresses
-- DeSmuME aliases: readbyte, readbytesigned, readword, readwordsigned, readdword,
-- readdwordsigned, writebyte, writeword, writedword
```

Addresses are absolute PSP addresses (user RAM starts at `0x08800000`), as shown in PPSSPP's debugger. A read from an invalid address returns `nil` and logs **once** per address, so a typo doesn't fill the log 60 times a second.

### Text encodings and character tables

- Built-in encodings: `"ascii"`, `"utf8"`, `"sjis"` (PPSSPP already has a Shift-JIS decoder) and `"utf16le"`.
- **Custom tables use the standard ROM-hacking `.tbl` format** ("Thingy" tables): lines like `41=A`, `8140= `, `FF=<END>`. Many fan-translation and modding communities already have `.tbl` files for their games.

```lua
local tbl = text.loadtable("tables/game.tbl")
local name = memory.readstring(0x08A01234, 16, tbl)
local s = tbl:decode(bytes)          -- decode a byte table you already read
```

### Events (callbacks)

```lua
event.onframeend(fn [, name])                  -> id
event.onmemorywrite(fn, addr [, name [, len]]) -> id   -- fn(addr, value, size, pc)
event.onmemoryread(fn, addr [, name [, len]])  -> id   -- fn(addr, size, pc)
event.onmemoryexecute(fn, addr [, name])       -> id   -- fn(addr)
event.ongamestop(fn)
event.unregisterbyid(id)   event.unregisterbyname(name)
-- DeSmuME aliases: memory.register(addr, [size,] fn), memory.registerexec(addr, fn),
--                  emu.registerafter(fn)
```

### Emulator and CPU

```lua
emu.frameadvance()        -- BizHawk/DeSmuME style "while true do ... end" loop, via coroutines
emu.framecount()   emu.vblankcount()
emu.getregister("a0")     -- also v0, sp, ra, pc, f12 ...; mainly for exec callbacks
emu.setregister("v0", x)
```

**`emu.getregister` is the key accessibility feature.** Hook the game's "draw text" function with `onmemoryexecute`, then read the string pointer from `$a0`. That gives every line of dialogue the game prints, without hunting each text buffer separately.

### Input

```lua
joypad.get()          -- {Up=, Down=, Left=, Right=, Cross=, Circle=, Square=, Triangle=,
                      --  L=, R=, Start=, Select=, AnalogX=, AnalogY=}
joypad.set(tbl [, frames])   -- inject; held for N frames (default 1)
input.registerhotkey(action, defaultKey, fn [, description])
```

### Speech and audio

```lua
speak(text [, interrupt])      stopSpeech()            -- the names you asked for
speech.say(text, interrupt)    speech.stop()           -- same functions, in a table
speech.braille(text)           speech.repeatlast()     speech.copylast()
speech.history([n])            -- returns the recent messages as a table

local h = audio.play("sounds/cursor.wav", {volume=0.8, pan=-0.5, loop=false})
audio.tone(440, 120, {volume=0.5, pan=0.3, wave="sine"})   -- frequency Hz, length ms
audio.setpan(h, p)   audio.setvolume(h, v)   audio.stop(h)   audio.stopall()
```

Pan runs from -1 (full left) to +1 (full right). Tones have short fade-in and fade-out so they don't click.

### Game info and logging

```lua
gameinfo.getid()  gameinfo.gettitle()  gameinfo.getversion()
console.log(...)      -- also print(); written to access.log
```

### Script environment

- `require` searches the script's own folder first, then `scripts/lib/`, so modders can share helper libraries (for example a menu reader).
- The standard `io`, `os` and `string` libraries are available, as in BizHawk. Scripts are trusted code that you install yourself, like any mod.

## 6. Files on disk and the command line

### Portable layout

```text
PPSSPPWindows64.exe
nvdaControllerClient.dll
accessibility/
  access.ini            speech backend, history size, which OSD messages to speak, etc.
  hotkeys.ini
  logs/access.log
scripts/
  _global.lua           optional: runs for every game
  aliases.ini           e.g.  ULES00123 = ULUS10041   (one script, several regions)
  lib/                  shared modules
  ULUS10041.lua         single-file script, or:
  ULUS10041/main.lua    folder script, with its own sounds/ and tables/
```

Our build is **portable**: it won't have the `installed.txt` file your `E:\PPSSPP` has. So it keeps its own settings in its own folder and can't change your normal PPSSPP install.

### Command line

```text
PPSSPPWindows64.exe "D:\PSP\game.iso"                      auto-loads scripts by game ID
PPSSPPWindows64.exe "D:\PSP\game.iso" --script="my.lua"    load this script instead
PPSSPPWindows64.exe "D:\PSP\game.iso" --no-script          start without scripts
```

PPSSPP's existing `--state=`, `--fullscreen` and `--escape-exit` keep working. At startup the emulator says, for example, *"Accessibility ready. Script ULUS10041 loaded."* If NVDA is not running, or the DLL is missing, that is said through the fallback voice (if you choose one) and written to the log.

## 7. Error handling

- **Every Lua call is protected**, with a stack trace attached.
- **On an error:**
  - The full trace goes to `access.log`, with a timestamp.
  - A short version is spoken, for example *"Script error, main.lua line 42: attempt to index a nil value"*.
  - **Only the callback that failed is turned off.** The rest of the script keeps running. A broken per-frame function would otherwise speak the same error 60 times a second.
- **Spoken errors are rate-limited.** An identical error is spoken once, and there is at most one spoken error every 2 seconds. All of them are still logged.
- **Load errors** (syntax mistakes, a missing file) are spoken and logged, and the game keeps running without the script.
- **Runaway scripts:** a Lua instruction-count hook stops any single callback that runs too long (for example an infinite loop). Instead of freezing the emulator, it reports *"script took too long"*.
- **Reload hotkey** (Ctrl+Shift+F12 by default): reloads all scripts without restarting the game. This is meant for the edit-and-test loop while researching memory.
- Errors are never shown only on screen. Every error path goes through log plus speech.

## 8. Licensing

- Our code is GPL v2 or later, the same as PPSSPP, with the PPSSPP-style header on each file.
- Lua and sol2: MIT. miniaudio: public domain or MIT-0. NVDA Controller Client: LGPL 2.1, shipped as the unchanged DLL. All compatible with GPL.

## 9. Phases, with what each delivers and how you test it

### Phase 1: unmodified build

- Create branch `access/main` from `v1.20.4`.
- Build Release x64 with MSBuild from your Visual Studio 2022 or 2026 install (the project uses the default toolset, and Windows SDK 10.0.22000 is installed).
- Make the output folder portable.
- **You test:** a game boots and plays exactly as in your installed 1.20.4.

### Phase 2: minimal Lua

- Add the Accessibility project and the Lua state.
- Load a script from `--script` or by game ID.
- Add `event.onframeend` and `emu.frameadvance`, the `memory.read_*` functions, `gameinfo`, `speak` and `stopSpeech` through NVDA, the speech history, and the log file.
- **You get:** a test script that says the game title on boot and speaks a value at an address you choose whenever it changes.

### Phase 3: callbacks, hotkeys and audio

- Add the `BREAK_ACTION_SCRIPT` hook, `onmemorywrite`, `onmemoryread` and `onmemoryexecute`, and `emu.getregister`.
- Add hotkeys from the window thread through the queue, with key names, scancodes and `hotkeys.ini`.
- Add the built-in repeat, copy and history keys.
- Add miniaudio with play, tone, volume and pan.
- Add `joypad.get` and `joypad.set`.

### Phase 4: launch, auto-load, spoken emulator messages, errors

- Finish `--script` and `--no-script`, auto-loading with `aliases.ini` and `_global.lua`, and the OSD message hook with filters.
- Speak boot errors.
- Add the full error policy, the runaway-script guard and the reload key.
- Add the text API: `.tbl` tables and `readstring` encodings.
- Optional: an `event.onsavestateload` hook, which needs one more line in `Core/SaveState.cpp`, so scripts can reset their cached state after you load a save state.

### Phase 5: documentation and an example

- A modder API reference in Markdown with headings, written for screen reader navigation.
- A "getting started with memory research" guide.
- A commented example script that uses every feature.

### Spoken PPSSPP menus: moved into Phase 1

You approved this and asked for it straight away, so it was done in Phase 1 together with the NVDA speech module.

- **How it works:** once per frame, the focused menu item and its description are checked, so no PPSSPP UI code had to change.
- **The one upstream line:** `Button::GetImageID()` in `Common/UI/View.h`. It's needed to name buttons that show only an icon.

## 10. Risks I'm watching

- **Memory callbacks slow the JIT.** I'll measure this in Phase 3 and document it. Fallback: per-frame polling, which is what most accessibility scripts do anyway.
- **Upstream may restructure `Breakpoints.cpp`.** It's the only hook with real logic around it, and it has been stable for years.
- **Hotkey consumption:** while a text field is open in PPSSPP's UI (for example the search box), single-key bindings without a modifier could steal typing. Plan: only bindings that have a modifier stay active in menus. Without a modifier, a binding only works in-game.
- **Frame rate varies by game,** as covered in section 2. `vblankcount` gives scripts a steady clock.

## 11. Decisions I need from you

1. **Fallback speech when NVDA isn't running.** I recommend a Windows SAPI5 fallback. It's built into Windows and adds no new dependency, and it helps you when NVDA is restarting, and sighted testers too. The alternative is NVDA only, with errors written only to the log when NVDA isn't running.
2. **Default built-in hotkeys.** Are the Ctrl+Shift combinations in section 3 OK alongside NVDA and your usual games?
3. **Optional Phase 6 (spoken PPSSPP menus):** add it at the end, or leave it out?
4. **GitHub:** should I set up a public fork on your GitHub account in Phase 1, or keep everything local for now?

### Your answers (9 November 2026)

- The plan is approved as proposed.
- The menu hook is moved forward and done right away, in Phase 1.
- **Git stays local and private** until you decide to publish. The GitHub remote is renamed `upstream`, and pushing to it is disabled.
