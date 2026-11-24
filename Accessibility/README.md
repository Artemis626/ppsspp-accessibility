# PPSSPP Accessibility Scripting

## What this is

A fork of the PPSSPP PSP emulator that adds Lua scripting designed first for **screen reader accessibility mods**. It is a platform: modders can write accessibility scripts for any PSP game. The emulator also speaks its own menus.

- **Based on:** PPSSPP v1.20.4 (tag `v1.20.4`).
- **Platform:** Windows first.
- **Licence:** GPL 2.0 or later, like PPSSPP.
- **Repository:** private, `Artemis626/ppsspp-accessibility`, branch `access/main`.

**To use it, read the [user guide](docs/User-Guide.md).** **New here? Read the plain-language report first:** [docs/Project-Report.md](docs/Project-Report.md). It explains everything that has been built, without the technical detail of this page.

## Status

| Phase | What | Status |
|---|---|---|
| 0 | Explore the source, write the plan ([docs/Phase0-Plan.md](docs/Phase0-Plan.md)) | Done |
| 1 | Unmodified build; NVDA speech; spoken PPSSPP menus | Done, tested by the user |
| 2 | Minimal Lua: script loading, frame callbacks, memory reads, `speak()` | Done, tested by the user |
| 3 | Memory writes, read/write/execute callbacks, registers, hotkeys, audio cues, joypad | Done. Hotkeys tested by the user; the rest is self-tested and not yet tried on real game data ([testing guide](docs/Phase3-Testing.md)) |
| 4 | Launch options, auto-loading extras, spoken emulator messages, error handling, `.tbl` text tables | Next |
| 5 | API documentation for modders, example script | Planned |

### Second track: the Discovery layer

A separate, larger goal: find a game's text and data automatically, with no OCR or image recognition of any kind, and build a named per-game map for Lua. Its phases are numbered D0 to D4 so they are not confused with the phases above.

| Phase | What | Status |
|---|---|---|
| D0 | Design ([docs/Discovery-Phase0-Design.md](docs/Discovery-Phase0-Design.md)) | Done, approved |
| D1 | Harness; system dialogs and the system font library as text ([results and testing](docs/Discovery-D1-Results.md)) | Done, tested by the user. The system font library was then dropped as a goal: no tested game uses it |
| D2 | Text from a game's own fonts (the "glyph cipher"), measured with the harness ([results and testing](docs/Discovery-D2-Results.md), [recordings and fixes](docs/Discovery-Recording-Results.md)) | First part (games that draw letters one by one) done, tested by the user in Tactics Ogre, Persona 2 and Patapon 3. Second part (games that paint text with CPU code): not started |
| D2+ | Text in a game's own letter code, such as Persona 2's menus ([results](docs/Discovery-OwnLetterCode-Results.md)) | Built, tested by the user: help lines read, most options silent, some letters skipped |
| D3 | Cursors, lists, values; the per-game map ([results](docs/Discovery-D3-Results.md)) | Numbers on screen and their records: tested by the user (read and update, but too talkative). The selected item and the per-game profile: built, waiting for the user's test. Single digits, the cursor, lists and the map file: not started |
| D4 | Lua integration, confidence reporting, modder documentation | Not started |

The history of the Discovery work, game by game, is in [docs/Work-Log.md](docs/Work-Log.md).

### Phase 4 work already done early

These parts of Phase 4 were needed for testing, so they already exist:

- `--script=` and `--no-script`.
- Auto-loading a script by game ID.
- The error policy: errors are spoken and logged, a failing callback is turned off, and runaway scripts are stopped by a watchdog.
- The reload hotkey.

### Still to do in Phase 4

- `aliases.ini` (one script for several game IDs) and `_global.lua`.
- Speaking PPSSPP's on-screen messages: save state saved or loaded, warnings and errors.
- Speaking boot errors.
- `.tbl` character tables for `memory.readstring`.
- An optional `event.onsavestateload` hook.
- Windows SAPI fallback speech for when NVDA isn't running (recommended in the plan, not yet built).

## Folder layout

```text
Accessibility/
  AccessHost.h/.cpp        The only functions PPSSPP calls: Init, Frame, OnKey, Shutdown
  Log.h/.cpp               accessibility/logs/access.log
  Speech/                  NVDA Controller Client (loaded at runtime), queue, history, clipboard
  Menu/                    Speaks the focused PPSSPP menu item
  Input/Hotkeys.*          Key names, scancodes, hotkeys.ini, built-in actions
  Audio/                   miniaudio wrapper: sound files and tones, volume, pan, pitch
  Discovery/
    Core/                  Platform-independent: glyph cipher, digits, own letter codes,
                           selection, profile, recordings, text assembler, screen text,
                           survey, harness scoring. Compiled with NO PPSSPP include path.
      tests/               The core's own tests and the offline replay program
    Ppsspp/                The PPSSPP adapter: the taps, dialog speech, sidecar file, harness runs
  harness/                 Harness guide and case files (save states live outside the repository)
  Scripting/
    ScriptHost.*           Script lifecycle, main-body coroutine, watchdog, error policy
    Sandbox.cpp            What scripts may not do: programs, file deletion, native code, files outside their folder
    LuaInternal.h          Shared declarations, and the rules for writing API functions
    Api_Memory.cpp         memory.* reads and writes
    Api_Event.cpp          event.*, and syncing PPSSPP breakpoints for memory/exec callbacks
    Api_Info.cpp           gameinfo.*, emu.*, console.log, print
    Api_Speech.cpp         speak, stopSpeech, speech.*
    Api_Input.cpp          input.registerhotkey, joypad.*
    Api_Audio.cpp          audio.*
    Api_Discovery.cpp      text.*, discover.* (Discovery layer)
  ext/nvda/                nvdaControllerClient.dll (LGPL 2.1, unmodified)
  ext/miniaudio/           miniaudio.h 0.11.25 (public domain or MIT-0)
  examples/                Test scripts for the user, one per phase, and the Tactics Ogre example
  tests/                   Automated self-test scripts
  tools/                   package.ps1 and the test-runner scripts
  docs/                    The plans, the results of each phase, the user guide, the project report and the work log
```

## Changes to upstream PPSSPP files

Every change is marked `// [ACCESS]` (or `<!-- [ACCESS] -->`), so they can all be found with one search when moving to a new PPSSPP release.

| File | Change |
|---|---|
| `UI/NativeApp.cpp` | Calls `Access::Init`, `Frame`, `Shutdown`, and `Access::OnKey` first thing in `NativeKey` |
| `Core/Debugger/Breakpoints.h/.cpp` | `BREAK_ACTION_SCRIPT` flag and `g_breakScriptHook`, called from `ExecBreakPoint` and `MemCheck::Action` |
| `Common/UI/View.h` | Public `Button::GetImageID()`, used to name icon-only buttons |
| `Core/HLE/HLE.h/.cpp` | Discovery: `g_accessSyscallHook`, called before and after every system function a game calls |
| `Core/Util/PPGeDraw.h/.cpp` | Discovery: `g_accessPPGeHook`, called for system-dialog text and rectangles |
| `GPU/GPUCommon.h/.cpp` | Discovery: `g_accessGeHook`, called per draw command; while set, display lists run in the slower per-command loop |
| `Core/SaveState.h/.cpp` | Discovery: `g_accessStateHook`, called after a state is loaded or saved, and after a rewind |
| `README.md`, `.gitignore` | The README is this fork's own landing page; PPSSPP's is kept unchanged as `README-PPSSPP.md`. Ignore rules keep game data out of the repository |
| `Windows/PPSSPP.sln`, `Windows/PPSSPP.vcxproj` | Add the Accessibility project; import `Accessibility.targets` |

That's about 100 lines in total (about 60 for scripting, 41 for Discovery). The Discovery hooks are function pointers that stay null unless the layer sets them. Game start and stop, the per-frame callback, menu focus, and the command line are all detected **without** editing PPSSPP: we poll public functions and register a flip listener.

## Building

1. Use Visual Studio 2022's MSBuild. On the development machine it's the only MSBuild that has the C++ tools.
2. Build the solution:

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" Windows/PPSSPP.sln -p:Configuration=Release -p:Platform=x64 -m -v:minimal
```

3. The exe is written to the repository root, as `PPSSPPWindows64.exe`. A full build takes about 2.5 minutes; incremental builds take seconds.
4. Package a portable copy. It has no `installed.txt`, so its settings stay in its own folder:

```bash
powershell -File Accessibility/tools/package.ps1 -Dest "../builds/access-dev"
```

## Testing

### Automated tests

They boot `pspautotests/demos/displayWait.prx`, a small PSP test program, and read the results from `access.log`:

- **Phase 2 API:** run the build with `--script=Accessibility/tests/phase2_selftest.lua`.
- **Phase 3 API, including real key presses and a reload:** `powershell -File Accessibility/tools/phase3-test.ps1 -BuildDir ../builds/access-dev`
- **Spoken menus:** `powershell -File Accessibility/tools/menu-speech-test.ps1 -BuildDir ../builds/access-dev`

- **Discovery, Phase D1:** `powershell -File Accessibility/tools/discovery-test.ps1 -BuildDir ../builds/access-dev`. It runs the core's own tests without PPSSPP, then a system-dialog program and two font-library programs from pspautotests.
- **Discovery core only:** `powershell -File Accessibility/tools/discovery-core-test.ps1`. It compiles the core with no PPSSPP include path, which is also the proof that the core is platform-independent.
- **Discovery harness:** `powershell -File Accessibility/tools/harness-run.ps1 -BuildDir ../builds/access-dev`. See [harness/README.md](harness/README.md).
- **Recording all save states:** `powershell -ExecutionPolicy Bypass -File Accessibility/tools/harness-record.ps1 -BuildDir ../builds/access-dev -Import`. One pass; the recordings go to `../harness-data/recordings/`.
- **Replaying a recording without the emulator:** `powershell -ExecutionPolicy Bypass -File Accessibility/tools/discovery-core-test.ps1 -Replay <recording folder>`.

### Notes for whoever writes tests

- **Sending keys:** use `keybd_event`, not `WScript.Shell.SendKeys`. PPSSPP reads the keyboard through Raw Input, which ignores SendKeys.
- **The test program changes after a Cross press:** after `joypad.set` presses Cross, displayWait writes memory in bursts about every two seconds. Use windows of at least 120 frames when counting writes.

## Using it

### Files beside the exe

```text
scripts/<GAMEID>.lua  or  scripts/<GAMEID>/main.lua   loaded automatically for that game
scripts/lib/                                          shared modules for require()
accessibility/hotkeys.ini                             all hotkeys (created on first start)
accessibility/logs/access.log                         script output, errors, everything spoken
accessibility/profiles/<GAMEID>.ini                   what the Discovery layer learned about that game (see docs/User-Guide.md)
```

### Command line

- `PPSSPPWindows64.exe game.iso --script=file.lua` loads that file instead of the game-ID lookup.
- `--no-script` loads no script at all.
- `--access-harness=case.ini` runs one harness case, writes the result and exits. `--access-harness-out=file` says where the result goes; `--access-no-sidecar` ignores the layer's own file beside a save state. These are used by `harness-run.ps1`.
- `--access-record=folder` records into that folder once the state given with `--state=` has loaded, then exits. `--access-record-frames=n` sets the length (default 300). These are used by `harness-record.ps1`.

### Built-in hotkeys

These are the defaults; change them in `hotkeys.ini`.

| Keys | Action |
|---|---|
| Ctrl+Shift+R | Repeat the last message |
| Ctrl+Shift+C | Copy the last message (or the one you browsed to) to the clipboard |
| Ctrl+Shift+PageUp / PageDown | Older / newer message in the speech history |
| Ctrl+Shift+S | Stop speech |
| Ctrl+Shift+H | List all hotkeys |
| Ctrl+Shift+F12 | Reload the script and `hotkeys.ini` |
| Ctrl+Shift+T | Read the text found on the game screen (Discovery) |
| Ctrl+Shift+D | Say how this game draws its text (Discovery survey) |
| Ctrl+Shift+A | Change what is read automatically: nothing, system dialogs, or dialogs and game text |
| Ctrl+Shift+L | Write an inspection report of the screen to `accessibility/logs/discovery-report.txt` |
| Ctrl+Shift+K | Record everything the discovery layer sees for ten seconds into `accessibility/logs/recordings/`; press again to stop early |

### Key rules

- **Names:** keys are named (A to Z, 0 to 9, F1 to F12, Numpad0 to Numpad9, arrows, PageUp and so on) or given by scancode, for example `sc:0x1E`.
- **Punctuation:** punctuation keys are refused by name, because they move between keyboard layouts. Use a scancode for them.
- **Plain keys:** keys without a modifier only work while the game runs with no menu open.

## Lua API (current)

This is a quick reference. The full modder documentation is Phase 5. Names follow BizHawk, with DeSmuME aliases.

### Memory

Addresses are absolute, as shown in PPSSPP's debugger; user RAM starts at `0x08800000`.

- **Reads:** `memory.read_u8 / s8 / u16 / s16 / u32 / s32`, plus `_le` and `_be` variants, and `read_float`.
- **Writes:** `memory.write_*`, with the same set of names.
- **Ranges and strings:** `memory.readbyterange(addr, len)` and `writebyterange(addr, table)`, both 1-based. `memory.readstring(addr, maxlen [, "ascii"|"latin1"|"utf8"|"sjis"|"utf16le"])`.
- **Helpers:** `memory.isvalid(addr [, size])`, and `memory.fromcwcheat(offset)` to convert CWCheat addresses.
- **DeSmuME aliases:** `readbyte`, `readword`, `readdword`, the signed versions, `writebyte`, `writeword`, `writedword`, `memory.register`, `memory.registerwrite`, `memory.registerexec`.
- **Behaviour:** an invalid read returns `nil`, and an invalid write returns `false`. Either way the problem is logged once per address.
- **Script writes:** they invalidate JIT code at that address, and they don't trigger the script's own write callbacks.

### Callbacks

Each `event.on*` returns an id:

- `event.onframeend(fn [, name])` calls `fn()` once per game frame.
- `event.onmemorywrite(fn, addr [, name [, len]])` calls `fn(addr, value, size, pc)`. For CPU stores, `value` is the value about to be written; it's `nil` for the rare store types it can't decode.
- `event.onmemoryread(fn, addr [, name [, len]])` calls `fn(addr, size, pc)`.
- `event.onmemoryexecute(fn, addr [, name])` calls `fn(addr)`. It runs before the instruction, so at a function's start `a0` to `a3` are its arguments.
- `event.unregisterbyid(id)` and `event.unregisterbyname(name)`.
- `emu.registerafter(fn)` is the DeSmuME version: one per-frame function, replaced on each call.

### Emulator

- `emu.frameadvance()` pauses the main body until the next frame, for a BizHawk/DeSmuME `while true do ... end` loop.
- `emu.framecount()` counts game frames; `emu.vblankcount()` is a steady 60-per-second clock.
- `emu.getregister(name)` and `emu.setregister(name, v)` cover a0, v0, sp, ra, r0 to r31, f0 to f31, hi and lo; `pc` can be read but not written.
- `emu.getscriptdir()` returns the running script's folder.

### Game

- `gameinfo.getid()`, `gameinfo.gettitle()` (also `getromname`), `gameinfo.getversion()`.

### Speech

- `speak(text [, interrupt])` and `stopSpeech()`.
- `speech.say`, `stop`, `braille`, `history([n])`, `last`, `repeatlast`, `copylast`, `copy(text)`.

### Input

- `input.registerhotkey(action, defaultKey, fn [, description])` returns the key actually bound, or `nil` plus an error.
- `input.unregisterhotkey(action)`.
- `joypad.get()` returns Up, Down, Left, Right, Cross, Circle, Square, Triangle, L, R, Start, Select, AnalogX and AnalogY.
- `joypad.set(buttons [, frames])`: `true` presses a button, `false` holds it released.

### Audio

- `audio.play(file [, {volume, pan, pitch, loop}])` returns a handle. Relative paths are in the script's folder.
- `audio.tone(hz, ms [, {volume, pan, pitch, wave}])`.
- `audio.stop`, `stopall`, `isplaying`, `setvolume`, `setpan`, `setpitch`, `setmastervolume`.

### Discovery (subset built so far)

- `text.onscreen()` returns the text runs on screen, in reading order. Each run has `text`, `source` (`"system_dialog"`, `"game_font"`, `"system_font"` or `"font_call_stream"`), `confidence`, `id`, `selected`, `code`, `buffer`, and `x`, `y`, `w`, `h` when its place is known.
- `text.locate(string [, max])` returns where a string is stored in game memory: an array of `{addr=, encoding=}`, with encoding `"utf8"` or `"utf16le"`.
- `discover.survey()` returns `{verdict=, counters=, samples=}`: what the taps have seen in this game.
- `discover.report()` returns the inspection report as a string and writes it to `accessibility/logs/discovery-report.txt`.
- `discover.record([seconds [, note]])` records everything the layer sees for that long (default 5, at most 120 seconds) and returns the recording's folder, or `nil` and a reason. `discover.stoprecording()` ends it early. What a recording holds, and how to replay one without the emulator, is in [docs/Discovery-Recording-Results.md](docs/Discovery-Recording-Results.md).
- `discover.sprites()` returns every textured rectangle of the last frame, in draw order, for studying how a game draws.

### Logging

- `print(...)` and `console.log(...)` write to `access.log`.

## Script sandbox

Players are told to use scripts written by other people, so a script must not be able to harm the computer (`Scripting/Sandbox.cpp`, since 2026-11-24).

- **Taken away:** `os.execute`, `io.popen`, `os.remove`, `os.rename`, `os.exit`, `os.getenv`, `os.tmpname`, `os.setlocale`; `package.loadlib` and native C modules; precompiled Lua (bytecode); the `debug` library except `debug.traceback`.
- **Limited:** `io.open`, `io.lines`, `loadfile` and `dofile` only reach files inside the script's own folder: either a relative name such as `"data/notes.txt"`, or a full path that starts with `emu.getscriptdir()`. Names with a drive, a leading slash or `..` are refused with an error.
- **Kept:** everything else in Lua's standard library, including `os.time`, `os.clock` and `os.date`. `require` still finds Lua modules in the script's folder and in `scripts/lib/`.
- **Test:** run the build with `--script=Accessibility/tests/sandbox_selftest.lua`.

## Error policy

- **Logging:** every Lua call is protected. Errors go to `access.log` with a stack trace, and the first line is spoken.
- **Rate limit:** at most one spoken error every 2 seconds, and the same error isn't spoken again within 10 seconds.
- **Failing callbacks:** a failing frame, memory or execute callback is turned off. A failing hotkey stays registered, because it only runs when pressed.
- **Watchdog:** a single call that runs longer than 3 seconds is stopped.
