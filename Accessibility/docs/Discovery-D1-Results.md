# Discovery layer: Phase D1 results and how to test it

> **Status (10 November 2026):** Phase D1 is built and has passed its automated tests. It is waiting for your testing. Phase D2 has not been started.

The design is in [Discovery-Phase0-Design.md](Discovery-Phase0-Design.md). This document says what D1 built, what it measured on your two games, and how you can test it.

## 1. The result in short

- **The machinery works.** System dialogs are captured as text, located in game memory, and spoken. The system font library is tapped. The harness loads save states, measures, and writes a report.
- **Neither of your two games uses the system font library.** Tactics Ogre and Final Fantasy Tactics both draw text with their own fonts. So D1's own capture finds none of their in-game text: the harness measures **0 of 10** and **0 of 1**. That is the baseline Phase D2 has to raise.
- **The two games turn out to be a good pair for D2:**
  - **Tactics Ogre** stores its text as plain single-byte text. The name on the name-entry screen, "DenamA", is in memory exactly once, at `0x09644CD0`.
  - **Final Fantasy Tactics** does not: none of the words of its opening line are in memory as plain text. It uses its own character table.
  - So one game tests the easy case of the Phase D2 method (own font, standard encoding) and the other tests the hard case (own font, custom encoding).

## 2. What was measured

### The survey

The survey counts what the taps see. These numbers come from the harness runs on the two states described in section 4.

| | Tactics Ogre (ULES01500) | Final Fantasy Tactics (ULES00850) |
|---|---|---|
| System font library calls | none | none |
| System dialog text | none | none |
| Save-data utility started | 3 times during boot, without showing a dialog | 2 times during boot, without showing a dialog |
| Draw commands per frame | about 270 | about 270 |
| Of those, textured rectangle batches | about 260 | about 6 |

What the last two rows mean for D2:

- **Tactics Ogre** draws its screen almost entirely as textured rectangles. That is the simplest form for reading glyph positions.
- **Final Fantasy Tactics** draws almost nothing as rectangles; it uses triangles. D2's draw analysis must handle rectangles built from triangles. D1's tap already decodes those shapes, so this is known ground.

### A limit of this survey

The games were only run for about a minute from boot, with buttons pressed automatically to get past the logos. A game could start using the system font library later (in a particular menu, for example). The survey hotkey lets you check at any point while you play.

### The save screens

Both games start the PSP's save-data utility during boot without showing its dialog. I have not yet seen either game's save or load screen, so I do not know whether those screens are the system dialog (which D1 reads) or the game's own (which needs D2). That is one of the things your testing can tell us.

## 3. What was built

### The core (platform-independent)

In `Accessibility/Discovery/Core/`. It is compiled with no PPSSPP include path, so an emulator header inside it is a compile error.

| File | What it does |
|---|---|
| `Types.h`, `Machine.h` | Shared types, and the interface an emulator adapter implements |
| `GlyphLedger` | Remembers which character the system font library drew at which memory address |
| `TextAssembler` | Turns single placed characters into lines: removes shadow copies, groups lines, finds spaces, splits columns |
| `ScreenText` | The text on screen now, with stable ids, and what appeared, changed or disappeared |
| `Engine` | The front door: receives events from the adapter, produces text; also the survey and the memory search |
| `Harness` | Reads case files, scores found against expected, writes the result |
| `TextUtil` | UTF-8, INI reading, JSON writing, edit distance |

### The PPSSPP adapter

In `Accessibility/Discovery/Ppsspp/`.

| File | What it does |
|---|---|
| `Taps.cpp` | The four taps: system calls, dialog drawing, draw commands, save states |
| `DiscoveryHost.cpp` | Game start and stop, dialog speech, hotkey actions, the save-state sidecar file, harness runs |

### Changes to PPSSPP's own files

41 added lines in 8 files, all marked `// [ACCESS]`. This is more than the "about ten" I estimated in the design: each hook needs a declaration and a definition as well as its call, and save states needed four call sites because rewinding replaces memory too.

| File | Change |
|---|---|
| `Core/HLE/HLE.h`, `.cpp` | `g_accessSyscallHook`, called before and after every system function a game calls |
| `Core/Util/PPGeDraw.h`, `.cpp` | `g_accessPPGeHook`, called for dialog text, wrapped dialog text and plain rectangles |
| `GPU/GPUCommon.h`, `.cpp` | `g_accessGeHook`, called per draw command; while set, display lists use PPSSPP's slower per-command loop |
| `Core/SaveState.h`, `.cpp` | `g_accessStateHook`, called after a state is loaded or saved, and after a rewind |

All four are function pointers that stay null unless the accessibility layer sets them, so the headless build and other front ends are unaffected.

### A design decision made while building: when the draw tap is on

The draw tap makes PPSSPP interpret display lists more slowly. So it is only on:

- for the first 10 seconds of a game, and for 10 seconds after each survey request, to count draws;
- whenever the glyph ledger has entries (the game uses the system font library);
- during a harness run.

In a game like your two, it is off during normal play.

## 4. Automated test results

| Test | Result |
|---|---|
| Core on its own, without PPSSPP (`discovery-core-test.ps1`) | 56 checks, all passed |
| System dialog: text captured, found in memory, spoken (`discovery-test.ps1`, dialog program) | passed |
| System font library: glyph draws seen by the tap (two font programs) | passed: 1 and 23 glyphs |
| Lua functions (`discovery_selftest.lua`) | all passed |
| Harness on the dialog program | 2 of 2 exact, precision 100% |
| Harness on a Tactics Ogre save state | runs; 0 of 10, as the survey predicts |
| Harness on a Final Fantasy Tactics save state | runs; 0 of 1, as the survey predicts |
| Phase 3 regression test (`phase3-test.ps1`) | all passed |
| The three new hotkeys, pressed as real keys | all spoke |

### What is not tested end to end

**The join between font-library glyphs and draws on screen, and the sidecar file, have only been tested inside the core's own tests**, with invented data. No program I have draws system-font glyphs to the screen: the pspautotests font programs only render into memory, and your two games do not use the library. This code path needs a game that uses the system font library. Until one is tested, treat it as unproven.

### The two save states I made

To test the harness's save-state path, I made one state per game by letting each game boot with automatic button presses, then pressing PPSSPP's save-state key:

- `harness-data/ULES01500/name-entry.ppst`: Tactics Ogre's own name-entry screen.
- `harness-data/ULES00850/opening-prayer.ppst`: the first dialogue line of Final Fantasy Tactics.

They also exist as slot 1 in the build's own state folder (`builds/access-dev/memstick/PSP/PPSSPP_STATE/`); no state of yours was there to overwrite. Replace them with your own whenever you like.

## 5. How to test it yourself

### The new hotkeys

| Keys | Action |
|---|---|
| Ctrl+Shift+T | Read the text found on the game screen |
| Ctrl+Shift+D | Say how this game draws its text (the survey) |
| Ctrl+Shift+A | Turn automatic reading of system dialogs on or off |

They can be changed in `accessibility/hotkeys.ini` under `[global]`, with the action names `readScreen`, `survey` and `toggleDialogReading`. Your existing file does not list them; the defaults apply until you add lines for them.

### Test 1: a system dialog speaks

1. Start a game whose save or load screen you know, and open that screen.
2. **If it is the PSP's system dialog**, you should hear its text without pressing anything, and hear new text when you move between save slots. Ctrl+Shift+T should read the whole dialog again.
3. **If you hear nothing**, the game draws its own save screen. That is useful to know; tell me which game.

### Test 2: the survey

1. Start any PSP game you own and play for a little.
2. Press Ctrl+Shift+D. You hear one or two sentences saying whether the game uses the system font library.
3. The full numbers are in `accessibility/logs/access.log`.
4. **The most useful thing you can do for D1:** run this on other games you own. A game that answers "its glyphs were matched to draws on screen" is the one that can prove the part of D1 I could not test.

### Test 3: the harness

1. Run:

```bash
powershell -File Accessibility/tools/harness-run.ps1 -BuildDir ../builds/access-dev
```

2. Three emulator windows open and close in turn.
3. Open `harness-data/results/latest-report.md`. The summary table should show the dialog case at 100% and the two game cases at 0%.

### Test 4: Lua

In a script, `text.onscreen()` returns the runs on screen, `text.locate("some text")` returns where a string is stored in game memory, and `discover.survey()` returns the survey. `Accessibility/tests/discovery_selftest.lua` shows all three in use.

## 6. Known limits of D1

- **On-screen keyboard:** its keys are captured as text, but which key is selected is not spoken yet. When it opens you hear "On-screen keyboard".
- **Shift-JIS strings** passed to the character-conversion library are only kept as samples when they are plain ASCII. A conversion table is needed for the rest.
- **Text drawn to an off-screen surface** and shown later is reported for the frame it was drawn, not for as long as it is visible.
- **A glyph buffer the game clears and reuses** can leave stale entries in the ledger, because clears are not watched yet.
- **Confidence numbers** are starting values (1.0 for dialogs, 0.95 or 0.8 for font-library text, 0.5 for call-order text). They become measured rates once the harness has enough cases to calibrate them.

## 7. What I suggest for D2

Given what the survey found, the order I would work in:

1. **The draw stream for every textured rectangle**, not only those in the ledger: glyph ids from texture address and rectangle, assembled into runs.
2. **The glyph cipher on Tactics Ogre first.** Its text is plain single-byte text and it draws rectangles, so it is the easy case, and the name-entry screen is ideal: it shows the whole alphabet, and "DenamA" is in memory once.
3. **Then Final Fantasy Tactics**, which needs triangles decoded and a custom character table learned.

This is a proposal. D2 does not start until you say so.
