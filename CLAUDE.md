# CLAUDE.md: PPSSPP Accessibility Scripting fork

## Who and how

- **The user:** a visually impaired (not blind) gamer and accessibility modder. They use **NVDA**, navigate answers **by Markdown headings**, and type on an **Italian QWERTY** keyboard.
- **Who does what:** the user handles design, testing and memory research. Claude handles code and architecture.
- **Every answer:** structure it with MD headings, and explain the reasoning behind decisions and each step, not just the steps.
- **Phases:** work in phases and **stop after each phase** for the user's testing. Don't start the next phase without their go-ahead.
- **Git:** the repository is **private**: `Artemis626/ppsspp-accessibility`, remote `origin`, branch `access/main`. The `upstream` remote (hrydgard/ppsspp) is fetch-only; its push URL is deliberately disabled. Nothing may be made public until the user decides. Push to `origin` only when asked.

## The project

A fork of PPSSPP **v1.20.4** with two independent parts:

1. **Lua scripting for screen reader accessibility mods:** speech through the NVDA Controller Client, audio cues through miniaudio, hotkeys by key name or scancode (never punctuation by name), spoken PPSSPP menus, memory and execute callbacks through PPSSPP's debugger breakpoints.
2. **The Discovery layer:** finds a game's text and numbers by itself and helps a modder find addresses. Its phases are **D0 to D4**, so they aren't confused with the scripting phases.

Our code lives in `Accessibility/`; upstream files are touched as little as possible.

### Where to read more (read only what the task needs)

- **Status table, API, folder layout, upstream edits:** `Accessibility/README.md`.
- **Plain-language report:** `Accessibility/docs/Project-Report.md`. Keep it current when a phase ends.
- **User guide:** `Accessibility/docs/User-Guide.md`. Keep it true to the build.
- **History, per-game findings and the user's test reports, 2026-11-12 to 11-15:** `Accessibility/docs/Work-Log.md`. Read the part about a game before changing how that game is read.
- **Results per phase:** `Accessibility/docs/Discovery-*-Results.md`. Read `Discovery-Recording-Results.md` before touching the cipher.
- **Original designs:** `Accessibility/docs/Phase0-Plan.md`, `Accessibility/docs/Discovery-Phase0-Design.md`.

## Current status (2026-11-16)

- **Scripting:** Phases 0 to 3 done; the Phase 3 hotkeys were tested by the user (2026-11-09). **Phase 4 is next; do not start it until the user says so.** Its list is in `Accessibility/README.md`, "Still to do in Phase 4".
- **Discovery, tested by the user:** D1; D2 first part (the glyph cipher, with the Tactics Ogre, Persona 2 and Patapon 3 fixes); D3 numbers (read and update, but too chatty); Persona 2 menus in the game's own letter code (help lines read, most options silent, some letters skipped).
- **Discovery, built and waiting for the user's test:** the selected item (`Discovery/Core/Selection.*`) and the per-game profile (`Discovery/Core/Profile.cpp`, `accessibility/profiles/<GAMEID>.ini`).
- **Known open problems, not built:** letters skipped in dialogue and menus (proposed remedy: for a trusted font, look for a row's unknown letters near where its other rows were found); half-read rows spoken, then spoken again when complete; numbers spoken unasked (recommended: keep them in the report and API only; not approved).
- **Core tests:** 125 checks, all passing (2026-11-24, with invented text instead of game text).
- **Script sandbox** (`Scripting/Sandbox.cpp`, 2026-11-24): no programs, file deletion, native code, bytecode or debug library; files only inside the script's folder. Self-test: `--script=Accessibility/tests/sandbox_selftest.lua`.
- **Publishing (2026-11-24):** the public history is one squashed commit on top of `v1.20.4`, authored with the GitHub noreply address. The old private history is kept only in the local branch `private-history`; never push that branch.
- **Model:** Fable 5.2 built everything up to 2026-11-15; from 2026-11-16 the work continues with Opus. For new cipher rules, write the refusal test case first and replay every recording.

### Waiting, do not do unprompted

- **Persona 2 is the focus game** (both Innocent Sin and Eternal Punishment). No further D3 work until the user says so.
- **D2 second part** (text painted by CPU code into a picture): not started; needs the user's decision.
- **Final Fantasy Tactics:** set aside by the user (2026-11-13).
- **The next parts of D3** (single digits such as elevation, the cursor, names and the map file), **Persona 2's name entry**, **a spoken explorer**, **the off switch for Discovery** (agreed in principle; the user said "Wait").
- **System font library:** dropped as a goal (2026-11-11). Its code stays, dormant; build nothing more on it.
- **Open question:** which languages come first.

## Discovery rules

- **Hard constraint: no OCR and no image recognition.** Images are identified only by memory address and rectangle. Screenshots are allowed only in harness tooling, to write expected-result files.
- **Pixel rule, as amended by the user (2026-11-11):** pixels may be read only for elements that really are pure graphics with no text underneath. Anything with text behind it: no pixel reading.
- **The goal, in the user's words:** a helper for accessibility hooking that relies on Lua. Hunting is made easier, not removed, so a modder must be able to study what the layer sees (Ctrl+Shift+L, `discover.report()`).
- **False matches are the main risk.** Every acceptance rule in `GlyphBook` (the cipher, the digit evidence rules) exists because of a wrong answer seen in testing. Do not loosen one without a test case.
- **Core and adapter:** `Accessibility/Discovery/Core/` is platform-independent and compiled with only its own folder on the include path. Never include a PPSSPP header there. New core files need the same per-file include settings in `Accessibility.vcxproj`. The adapter is `Accessibility/Discovery/Ppsspp/`. Namespaces: `::Discovery` (core), `Access::DiscoveryHost` (adapter).
- **Taps are installed once at start-up** and stay, because a short test program can run and end between two host frames.
- **Game data stays outside git:** save states, ISO paths, reports, pictures, recordings, and harness cases for real games (`../harness-data/cases/`) live in `../harness-data/`. Only cases that need no game are in `Accessibility/harness/cases/`.
- **No verbatim game text in the repository** (the user's rule since publishing, 2026-11-24): tests use invented lines of the same shape; docs describe a game's lines instead of quoting them. Names and single menu words are allowed.
- **Recordings** exist in `../harness-data/recordings/<GAMEID>/<state>/`. Use them; do not run the emulator to learn what they already say.

## Working lean (agreed with the user 2026-11-12, binding)

A single session can use up the user's tokens. The evidence behind these rules is in the work log.

- **Fewer emulator runs.** Before any run, state the question it answers and what follows from each result. If that cannot be stated, do not run. Say so when a run's usefulness is doubtful.
- **Log first, theorize second.** When something is not read, the first step is one recording that captures everything about it, not a code change.
- **Do not reach a screen by automatic button presses.** Ask the user for a save state. The user makes "crucial" states (dialogue, battle menus) in `builds/access-dev/memstick/PSP/PPSSPP_STATE/`; copy (never move or overwrite) into `../harness-data/<GAMEID>/`.
- **Test offline first:** core tests and replays. Run the emulator once after a change, to confirm.
- **One fresh session per piece of work.** This file and the result documents carry the state.

### Recording and replay

- **Record:** Ctrl+Shift+K, `discover.record()`, or `--access-record=<folder>` with `--state=`. Format: `Discovery/Core/Recording.h`. A recording holds the whole 24 MB memory: every "not in memory" answer so far was a wrong idea about how text is stored.
- **All states in one pass:** `tools/harness-record.ps1 -BuildDir ../builds/access-dev -Import`.
- **Replay offline:** `tools/discovery-core-test.ps1 -Replay <folder> [-Extra n] [-Find "text"] [-NoBuild]`.
- **Studying memory copies:** Python is installed (`python`). A pattern search that assumes nothing about the letter code (same repeats, any 8-bit or 16-bit values) finds strings the cipher's search misses.

## Build, package, test

- **PowerShell scripts need `-ExecutionPolicy Bypass`** on the command line in this environment.
- **Build** with VS 2022 MSBuild (VS 2026 is installed but has no C++ tools):
  `"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" Windows/PPSSPP.sln -p:Configuration=Release -p:Platform=x64 -m -v:minimal -nologo "-clp:ErrorsOnly;Summary"`
  The exe goes to the repository root (`PPSSPPWindows64.exe`). One warning, LNK4281 from `headless/Headless.vcxproj`, is upstream's and expected.
- **Package:** `powershell -File Accessibility/tools/package.ps1 -Dest "../builds/access-dev"`. The user's desktop shortcuts point at `builds/access-dev`; the "test script" shortcut passes `--script=<phase test script>` (copy `Accessibility/examples/phaseN_test.lua` into `builds/access-dev/scripts/` when it changes).
- **Discovery tests:** `tools/discovery-core-test.ps1` (core only, seconds), `tools/discovery-test.ps1 -BuildDir ../builds/access-dev` (core, dialogs, font programs), `tools/harness-run.ps1 -BuildDir ../builds/access-dev`.
- **Scripting tests** boot `pspautotests/demos/displayWait.prx` and read `builds/access-dev/accessibility/logs/access.log`: Phase 2 `--script=Accessibility/tests/phase2_selftest.lua`; Phase 3 `tools/phase3-test.ps1 -BuildDir ../builds/access-dev`; menus `tools/menu-speech-test.ps1 -BuildDir ../builds/access-dev -Keys "Down,Right"`.
- **Baseline:** an unmodified v1.20.4 build is in `../builds/v1.20.4-baseline`.
- **Test game:** Tactics Ogre, Europe, ULES01500, disc 1.00, the user's own dump in their PSP games folder (its path is in `../harness-data/games.ini`). Its example script is `Accessibility/examples/ULES01500.lua`, installed in `builds/access-dev/scripts/`. All games in that folder may be used for testing: Final Fantasy Tactics (ULES00850), Persona 2 Innocent Sin and Eternal Punishment, Patapon 3 (UCES01421).

## Rules and conventions

- **Marking upstream edits:** every edit to an upstream file is marked `// [ACCESS]`. Keep each hook to one call into `Accessibility/`. The list of edits is in `Accessibility/README.md`. Prefer polling public PPSSPP functions over adding hooks.
- **New source files** must be added to `Accessibility/Accessibility.vcxproj` (generated from `UI/UI.vcxproj` settings).
- **The Lua API uses Lua's plain C API, not sol2** (sol2's safety macros conflict with PPSSPP's own use of it). Lua errors use longjmp, so call every `luaL_check*` and `luaL_error` **before** creating C++ objects with destructors (see `Scripting/LuaInternal.h`). `luaL_tolstring` pushes onto the stack, so read later arguments first.
- **Logging inside `namespace Access`:** use `::Log::System` for PPSSPP's log category, because `Access::Log` shadows it.
- **Threads:** all Lua runs on the emulation thread. `NativeKey` runs on the window thread: hotkeys queue script actions, built-in actions use only the thread-safe speech module. Memory events from other threads are queued. Breakpoint changes are applied in `ProcessEventQueue` once per host frame, before PPSSPP's `g_breakpoints.Frame()`.
- **miniaudio:** include it only through `Audio/MiniaudioConfig.h`, because its settings change struct layouts.

## Lessons learned (don't re-learn these)

- **Never delete or overwrite files in `builds/access-dev/memstick`:** it holds the user's saves and save states. A script that makes a state must check first and refuse if one exists.
- **Scratch test scripts must close the emulator by its own process object,** not by a shell process number: two emulators were once left running that way.
- **Key events:** `WScript.Shell.SendKeys` does not reach PPSSPP (Raw Input). Use `keybd_event`.
- **The test program after Cross:** displayWait then writes memory only in bursts about every 2 s. Count writes over 120 frames or more.
- **Holding references:** `BreakpointManager::ExecBreakPoint` held a reference into `breakPoints_` after unlocking; the hook copies `result` first.
- **An upstream oddity:** `RemoveMemCheck` decrements `MemBlockReleaseDetailed` on every removal, `AddMemCheck` increments only on the first add. Harmless for memchecks; worth reporting upstream someday.
- **Execute candidates:** at flip time `pc` and `ra` usually sit in PPSSPP's HLE idle code (`0x08000000`), not game code.
- **Icon names:** `ImageID`'s only member is a private `std::string_view`; `MenuSpeech` copies it out, with a `static_assert` guarding the layout.
- **Backslashes in Bash heredocs** lose one level of escaping here. Edit C++ files with the Edit tool, or write a script to a file first.
- **Clone history:** the clone started "blobless"; the full `v1.20.4` history was fetched later (`--refetch`), so pushing works.

## Moving to a new PPSSPP release

1. `git fetch --tags upstream`
2. `git rebase --onto vX.Y.Z v1.20.4 access/main`
3. Re-check every `[ACCESS]` hook.
4. Rebuild and run all the tests above.
