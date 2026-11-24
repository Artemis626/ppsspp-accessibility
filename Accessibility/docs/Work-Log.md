# Work log of the Discovery layer, 2026-11-12 to 2026-11-15

This is the detailed history that used to be in `CLAUDE.md`, moved here on 2026-11-16 so that it is not loaded at the start of every session. The text is unchanged, so "waiting for the user's test" means waiting at the time it was written; the current status is in `CLAUDE.md` and in the status table of `../README.md`. Each phase's own results document has the full detail; this log is the short version, with what the user said at each test.

Read the section about a game before changing how that game is read.

## Where the Discovery work stands (written 2026-11-13; Phase D3 is under way, first part waiting for the user's test)

- **Committed, not pushed:** D0 and D1 (`55998cdd4b`), D2 first part (`009ec5d236`), and the recording work with all the cipher fixes of 2026-11-13 (`e558711b85`). The first part of D3 and the second-game fix are committed too (2026-11-13).
- **The recording work is built and waiting for the user's test.** What it is, the first pass, and every finding: `Accessibility/docs/Discovery-Recording-Results.md`. Read that before touching the cipher.
  - Tested by Claude: the core tests (82 checks), the command-line record run, the offline replay on real recordings.
  - The Ctrl+Shift+K hotkey works: the user made two recordings with it (2026-11-13), in `../builds/access-dev/accessibility/logs/recordings/`. `discover.record()` is still untested.
- **The recordings exist** in `../harness-data/recordings/<GAMEID>/<state>/` for seven of the user's states. Use them; do not run the emulator to learn what they already say.

### Why each game's dialogue is not read (found from the recordings)

- **Tactics Ogre, two causes:**
  1. In game, its fonts are kept in **video memory** (`0x041B7B90` dialogue, `0x041C0BA0` everything else). The tap marks every video-memory texture `fromSurface`, and `LetterSized` rejects those. The fonts are 16 by 16 grids in code order (character = cell number + 32); decoding them offline gives the exact dialogue and battle text.
  2. The dialogue string has control codes between the words (`FF 81 xx 01`), a code for the hero's name (`FF 93`) and one for the line break (`FF 80`). Whole-row search cannot find it; single words are plain.
- **Persona 2 Innocent Sin:** drawn letter by letter after all (the old "painted by CPU code" verdict was wrong). Not read because the font is a cache spread over two textures (rows are built per texture), and the string is 16-bit with the space as `0x1120`.
- **Final Fantasy Tactics:** really painted into one picture (one 149 by 72 rectangle). But the text is in memory in the game's own letter code (digits 0 to 9, capitals from `0x0A`, small letters from `0x24`, space `0x95`), speaker name first.

### The two Tactics Ogre fixes (built 2026-11-13, waiting for the user's test)

- **Fix 1, real surfaces only:** the tap reports every surface the game draws into (`Engine::OnRenderTarget`). `SpriteDraw.fromSurface` now only means "in video memory"; `Engine::IsSurface` is the verdict (in video memory **and** inside a surface drawn into). `LetterSized` is size only.
- **Fix 2, the word search:** `GlyphBook::LearnFromWords`, tried when the whole row is not found. Grid fonts only, strict rule only (code = cell number + one constant), stretches of 6 symbols, and it needs two different stretches found with the same constant.
- **Checked:** core tests 91 checks, 0 failed (new tests for both fixes, including three refusal cases). Offline replay and one real recording pass agree: the dialogue state reads both lines, the battle states read the help line, "Move", "Denam" and "Road"; name entry still reads its 15 texts; Final Fantasy Tactics and Persona 2 produce no text.
- **Known gap:** in the dialogue state alone the speaker's name ("Vyce", 4 letters, its own font texture) stays unread: too short to learn from. In normal play that font is learned from any longer text first. Not verified in play.
- **Recordings made before 2026-11-13** had sprite targets as raw offsets and no `target` lines; the ones in `../harness-data/recordings/` were re-made with the new build.

### Persona 2 and Patapon 3 (built 2026-11-13, waiting for the user's test)

Details: section 7 of `Accessibility/docs/Discovery-Recording-Results.md`.

- **Findings:** Eternal Punishment (patched) stores ordinary spaces, Innocent Sin its own space code `0x1120`. Persona 2's font is spread over two textures (capitals in one). Persona 2 menus and name entry use a private two-byte code. Patapon 3 (`UCES01421`) draws letters from a packed font at `0x08E80B00` and keeps plain 16-bit text.
- **Built:** rows across the textures of one font (texture families in `GlyphBook`); `LearnFromLooseLine` (spaces not trusted, a game's own space code allowed); spacing learned from matched lines; a line found twice and thrown out is not searched for again.
- **Checked:** core tests 94 checks; offline replay of all recordings and one real pass agree. Innocent Sin, Eternal Punishment and Patapon 3 dialogue is read; Tactics Ogre unchanged; no false text elsewhere.
- **Limits:** a letter stays unknown until a matched line has contained it; search cost on Persona 2 menu screens not measured in play.
- **The user's test of the Tactics Ogre fixes (2026-11-13):** dialogue, names, descriptions, terrain and actions were spoken. Not spoken: elevation and units on the field (Phase D3). Polishing Tactics Ogre is not wanted now.

### Phase D3, first part: numbers (built 2026-11-13, waiting for the user's test)

Details, rules and test steps: `Accessibility/docs/Discovery-D3-Results.md`.

- **The user's report before it:** Innocent Sin dialogue is spoken like Eternal Punishment's. Patapon 3 spoke nothing.
- **Patapon 3's silence was a bug, now fixed:** the list of places text could be was kept from the game played before in the same session. `Engine::Reset` and `OnMemoryReplaced` now drop it. Not verified in play.
- **Numbers:** digits drawn from a strip (ten images of one size, in order) are read by their place in the strip; the strip's first digit is confirmed by finding several of the numbers together in memory (`GlyphBook::LearnDigits` in `Discovery/Core/GlyphDigits.cpp`). The place found is the record they come from. Each number on screen keeps candidate addresses, narrowed at each change (`Engine::Numbers()`, report section "NUMBERS ON SCREEN").
- **Tactics Ogre:** hit points 143 of 143 are read; the record is at `0x08AD84B4` (six 16-bit values: three current, three maximums), with copies at `0x08D40140`, `0x08D60CB0` and an array from `0x09E2AA98` spaced `0x3AF8`.
- **The evidence rules exist because of a wrong answer** (padding zeros read as 111 and 444 and found in a table). Do not loosen them without a test case.
- **Checked:** core tests 104 checks; offline replay of every recording (no number invented anywhere); one real pass agrees.
- **Limits:** elevation (a single digit) and units on the field are still not read; narrowing by changes is tested only in the core tests.

### Focus and hold (the user, 2026-11-13)

- **Persona 2 is the focus game,** both Innocent Sin and Eternal Punishment (largely identical). Why: the user's own interest, and what it needs (the game profile, the search that assumes no letter code) also cures Patapon 3's missing letters.
- **No coding and no further D3 until the user says so.** When work resumes, open with a short design proposal for the game profile.
- **The user's test of the D3 first part:** Tactics Ogre's stats are read and update in play; dialogue flows; but the numbers are chatty (a long tail of stats before dialogue). Recommended, not approved or built: keep numbers in the report and API, do not speak them unasked. Patapon 3 speaks after another game (the fix works), with missing letters as in Persona 2.
- **The user makes recordings** with Ctrl+Shift+K; study those offline. Run the emulator once after a change, to confirm.
- **Plain-language report of everything built:** `Accessibility/docs/Project-Report.md`. Keep it current when a phase ends.
- **Pushed to `origin` on 2026-11-13** at the user's request.

### Persona 2 menus: text in a game's own letter code (built 2026-11-14, waiting for the user's test)

Details, rules and test steps: `Accessibility/docs/Discovery-OwnLetterCode-Results.md`.

- **Found from the user's two menu recordings:** the menu text of both games is two bytes a letter, high byte first (small `0x0101` to `0x011A`, capitals `0x00E0` to `0x00F9`, space `0x8140`, end `0xFFFF`). And letters two texels wide ("i", "l") were being thrown away.
- **Built:** `GlyphBook::LearnFromCodeLine` (`Discovery/Core/GlyphPrivate.cpp`), a search by pattern of repeats among 16-bit values of any kind. The letters are then worked out with no table supplied: alphabet order plus English letter counts in the game's strings around the rows found. Once a font's own code is known the other searches leave it alone. Short rows ("Load") fill in single letters.
- **Checked:** core tests 108 checks; offline replay of every recording (no regressions, no table invented elsewhere); one real pass on the Eternal Punishment options state agrees.
- **Limits:** English only; nothing saved between sessions (the game profile is still to be designed); about 25 ms per search, at most once a second; digits and punctuation in this code not worked out; selection not known.
- Committed and pushed 2026-11-15.

### Phase D3, second part: the selected item (built 2026-11-15, waiting for the user's test)

Details: section 3 of `Accessibility/docs/Discovery-D3-Results.md`.

- **The user's test of the menu work (2026-11-15):** help lines were read every time in the field menu and Settings; some highlighted options were read, most options stayed silent; a few letters are still skipped in dialogue and menu items.
- **Built:** `SelectionFinder` (`Discovery/Core/Selection.*`). It finds the selected item by a mark that jumps between texts, by two texts changing places, or by two texts changing colours. Nothing is claimed until the selection has moved once. `SpriteDraw.color` is new (the draw's colour from the draw command, not from pixels); recordings made before 2026-11-15 lack it. The adapter speaks the selected item when it changes (auto-read mode 2); `text.onscreen()` fills `selected`.
- **Persona 2:** options screen uses a mark (texture `0x091D5A00` in Innocent Sin); Eternal Punishment's main menu sets the selected item 8 pixels right; Innocent Sin's main menu is probably colour, not verified.
- **Checked:** core tests 117 checks; offline replay of every recording; one real pass.
- Committed and pushed 2026-11-15.

### The per-game profile, the user guide and the landing page (built 2026-11-15, waiting for the user's test)

- **Profile:** `Discovery/Core/Profile.cpp`; the adapter keeps it in `accessibility/profiles/<GAMEID>.ini` beside the exe (read at game start, written every ten seconds if changed and at game end). Sections: `[letter-code]` (used again as soon as one row is found and all its codes are letters under the table), `[font]` (image to letter; believed only after the font is confirmed afresh and at least 5 letters agree with none against), `[volatile-font]` (fonts that disagreed: caches, never remembered again), `[findings]` (addresses for the modder; not used by the layer).
- **Harness and recording runs neither read nor write profiles,** so they stay repeatable. Offline replays never have one.
- **Honest result:** for Persona 2 the profile saves little, since its fonts are caches. Verified in two real runs that the file is written and read back.
- **Still open, from the user's test:** letters skipped in dialogue and menus. The remedy I see: for a trusted font, look for a row's unknown letters near where its other rows were found, with the known letters as a guide. Not built.
- **Also seen:** half-read rows are spoken while letters are still being learned, then again when complete. Not addressed.
- **User guide:** `Accessibility/docs/User-Guide.md` (playing; making a game speak). Keep it true to the build.
- **Landing page:** the root `README.md` is now this fork's own; PPSSPP's is `README-PPSSPP.md`.
- **Checked:** core tests 125 checks. Committed and pushed to `origin` on 2026-11-15 at the user's request, with the menu and selection work.

## What went wrong in D2, as evidence for the lean rules

- About fifteen emulator runs went on one bug (a font not learned from boot). Each cost a rebuild, a 70 to 90 second run, and reading the output.
- Four theories in a row were wrong. The bug was found at once when the report printed the line's actual symbol sequence (false gaps between letters).
- Boot runs with automatic button presses could only reach the name-entry screen and its pop-ups.

## Tactics Ogre example script: addresses (from CLAUDE.md)

- **Script:** `Accessibility/examples/ULES01500.lua`, installed as `builds/access-dev/scripts/ULES01500.lua`, so it auto-loads with the normal shortcut. The "test script" shortcut would force `phase3_test.lua` instead.
  - Ctrl+Shift+G speaks Goth; Goth changes are spoken through a write watch.
  - Ctrl+Shift+I speaks the item counts.
  - Ctrl+Shift+E toggles an execute test.
- **Addresses** come from the community CWCheat database (`github.com/Saramagrean/CWCheat-Database-Plus-`, `cheat.db`, section `_S ULES-01500`):
  - Goth: u32 at `0x08AD8438` (cheat `0x202D8438`).
  - Decoy Scroll count: u8 at `0x08AE8530`.
  - Demonpetal Scroll count: u8 at `0x08AE853B`.
  - Glass Pumpkin count: u8 at `0x08AE8543`.
  - Code at `0x0880E558`, from "GOTH Doesn't Decrease". It's uncertain: the database lists the same value for its On and Off codes.
- **Verified so far:** the script loads on the user's disc, and the addresses decode correctly. **Not yet verified:** the values against the game, which needs a loaded save. That's the user's next test.
- **Decoding CWCheat:** `_L 0xTAAAAAAA 0xVVVVVVVV`. T=0 is an 8-bit write, 1 is 16-bit, 2 is 32-bit; the address is `0x08800000 + AAAAAAA`. This is what `memory.fromcwcheat()` computes.
