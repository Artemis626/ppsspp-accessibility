# Discovery: recordings, and what the first pass showed

Written 2026-11-12. This covers the "richer logging" work from `CLAUDE.md` and the first recording pass over the user's save states.

## 1. What was built

### The record action

A recording is one folder holding everything the layer saw over a stretch of frames.

- **Hotkey:** Ctrl+Shift+K starts a ten-second recording; pressing it again stops it early. It says "Recording for ten seconds" and then "Recording written, N frames".
- **Lua:** `discover.record([seconds [, note]])` returns the folder (default 5 seconds, at most 120). `discover.stoprecording()` ends it early.
- **Where:** `accessibility/logs/recordings/<GAMEID>-<date>-<time>/` in the build folder.
- **It records over time,** not one frame, so a line that is typed out letter by letter is captured from first letter to last.

### What a recording folder holds

| File | For | Contents |
|---|---|---|
| `summary.txt` | a person | The inspection report at the end; every text seen; every unread row with its full symbol sequence and the answer of its memory search; the font tables (every letter-sized image, its place in its texture, its character and where that came from); every texture drawn from, with where on screen and how big; every draw the tap saw, including those it made nothing of. |
| `timeline.txt` | a person | Frame by frame: text that appeared, changed, went; rows first seen unread; every memory search with its answer. |
| `sprites.txt` | the replay | Every textured rectangle of every frame, in draw order. A frame identical to the one before is written as one line. |
| `memory-0.bin` | the replay, and searches | A complete copy of the game's memory at the end of the recording. |
| `recording.txt` | the replay | The manifest: game, frames, memory areas. |

**One change from the plan in `CLAUDE.md`:** the plan said to store "the text-like stretches of memory". The recording stores the whole memory instead (24 MB per recording). Reason: the stretches are worked out with the cipher's own idea of what text looks like, and the first pass showed that is exactly what was wrong in three games (control codes, a 16-bit space code, a private letter code). Only the whole memory let those be found.

### The offline replay

`Accessibility/tools/discovery-core-test.ps1 -Replay <folder> [-Extra 1200] [-Find "text"]` runs the core's engine on a recording, with no emulator, in about a second.

- It prints each text when it is first read, then the inspection report.
- `-Extra` repeats the last frame, to give the memory searches time. `-Find` looks for a string in the recorded memory.
- `-NoBuild` skips the compile.
- A change to the cipher can now be checked against a real recording before the emulator is ever started.

### The harness mode that records

`Accessibility/tools/harness-record.ps1 -BuildDir ../builds/access-dev [-Import]` records every save state in `../harness-data/<GAMEID>/` in one pass, into `../harness-data/recordings/<GAMEID>/<state>/`, and writes `recordings/index.md`.

- `-Import` first copies the states from the build's memstick folder (copies only; named `slotN-<date>-<time>.ppst`, so nothing is overwritten).
- The emulator option behind it is `--access-record=<folder>` with `--access-record-frames=<n>`, together with `--state=`.

### Files

- New: `Discovery/Core/Recording.h`, `Discovery/Core/Recording.cpp`, `tools/harness-record.ps1`.
- Changed: `Engine.h/.cpp` (recording hooks), `DiscoveryHost.h/.cpp`, `Taps.cpp` (draw notes), `Api_Discovery.cpp`, `Hotkeys.cpp`, `AccessHost.cpp`, `CoreTests.cpp`, `discovery-core-test.ps1`, the project file.
- No new edit to an upstream file.

### Tests run

- Core tests: 82 checks, 0 failed (a new round-trip test records, loads and replays).
- Replay of the real name-entry recording reads the same 15 texts offline as the emulator did.
- **Not run:** `discovery-test.ps1`, `harness-run.ps1`, the Phase 3 and menu tests. The reading code itself was not changed; the name-entry recording shows it still reads that screen.
- **Not tested by anyone yet:** the hotkey and the Lua functions. Only the command-line path was exercised.

## 2. The recording pass

One pass, nine states, 300 frames each, no button presses. One state was skipped: `ULJM06081` (from 2026-11-09) has no image listed in `games.ini`.

| Game | State | Shows | Read |
|---|---|---|---|
| Tactics Ogre | slot 0, and `name-entry` | Name entry | 15 texts (as before) |
| Tactics Ogre | slot 1 | Dialogue: Vyce, a two-line speech bubble | nothing |
| Tactics Ogre | slot 2 | Battle, help line about the Warrior class | nothing |
| Tactics Ogre | slot 3 | Battle, command menu on "Move" | nothing |
| Final Fantasy Tactics | slot 0 | Dialogue: Knight, a one-line speech box | nothing |
| Final Fantasy Tactics | slot 1 | Battle, command menu | nothing |
| Persona 2 Innocent Sin | slot 0 | Dialogue: a speaker's name and two lines | nothing |

## 3. Why Tactics Ogre's dialogue is not read

There are two causes. The first hides everything; the second would remain after the first is fixed.

### Cause 1: the font is kept in video memory, and one rule discards all of it

- In the dialogue state the game draws **88 rectangles of 16 by 16 per frame** exactly over the speech bubble (x 126 to 297, y 46 to 78), from a 512 by 512 texture at **`0x041B7B90`**. That is the two lines, each drawn twice (a shadow and the letter). The name "Vyce" is 8 more, from `0x041C0BA0`.
- Both addresses are in the PSP's video memory. The tap marks every texture in video memory as "a surface the game draws on", and the reader skips those. So the letters never even became an unread row. That is why the report said nothing at all.
- The rule is too coarse. The game's real drawing surfaces in this recording are at `0x04044000` and `0x04088000`; the fonts sit beyond them, in spare video memory used as storage.
- **The same applies to the battle screens:** the help line, "Move", "Denam" and "Road" are all drawn from `0x041C0BA0`.
- On the name-entry screen the same fonts are still in ordinary memory (`0x09A82C30` and neighbours), which is the only reason that screen was read. This explains the user's result exactly: description boxes early on read, in-game text silent.

**Proof, done offline from the recording:** the font is a grid in character-code order. Taking each rectangle's cell number and adding 32 gives:

- Dialogue: both lines of the speech bubble, exactly as shown, and the name `Vyce`.
- Battle, slot 3: `Denam`, `Road`, `Move`, and the whole help line about movement.
- Battle, slot 2: the whole two-sentence help line about the Warrior class.

The dialogue font is a small-capitals face: the game draws lower-case codes with small capital shapes. The codes are the true ones, so speech gets normal mixed case.

### Cause 2: the dialogue line is not in memory as one plain string

The user's guess was right. The line is stored at `0x09999FAA` (and a working copy at `0x09AE832A`) like this:

`FF A9` `<word> ` `FF 81 01 01` `<word> ` `FF 81 01 01` `<word> ` `FF 81 01 01` `<word>, ` `FF 81 01 01` `FF 93` `.` `FF 80` `<name> ` `FF 81 01 01` `<word>.` `00`

(The words themselves are left out here: they are the game's text.)

- `FF 81 xx 01` sits between every two words (the working copy has changing values in `xx`, so it is probably timing).
- `FF 93` stands for the hero's name: "Denam" is not in the line at all.
- `FF 80` is the line break.

The cipher looks for a whole row as one unbroken run of printable characters, so it cannot find this line. Single words are there in plain form.

### What would fix it (not done; needs the go-ahead)

1. **Tell a real surface from a picture stored in video memory.** A texture counts as a surface only if its address is one the game has actually drawn to. All the needed facts are in the recordings, so this is testable offline.
2. **When a whole row is not found, search for its words.** Only under the strict rule that already exists (every code is the cell number plus one constant), and only for words long enough to mean something. The two words of the second line are enough to give the layout rule, and the rule then names the whole font.
   - This loosens an acceptance rule of the cipher, so it needs test cases first. The nine recordings are the test set: the fix must read the Tactics Ogre states and must not produce text in the others.

With fix 1 alone the battle screens would very likely be read (their help lines are probably whole in memory), and the dialogue would appear as an unread row. With both, the dialogue is read.

## 4. What the other recordings show

### Persona 2 Innocent Sin: drawn letter by letter after all

My earlier verdict ("painted by CPU code") was wrong, as the user suspected.

- The dialogue is drawn as small rectangles, 3 to 12 wide and 16 high, from **two** 256 by 256 textures: `0x0916B800` and `0x09163700`. Rows were formed: the first is `s74 s75 s75 _ s76 s77 s78 s79 s79 s80 s75 s77 _ ...`, which is the speaker's name without its capital, with the repeats in the right places.
- **Why it was not read, three reasons:**
  1. **The font is spread over two textures.** The capitals L, H and T of this screen are in the other one. The reader builds rows per texture, so "Less" lost its L.
  2. **The font is a cache, not a table.** Letters are not in code order (capitals sit near the bottom, punctuation at the top), so the layout rule can never apply. Every symbol has to be learned from matched lines. That is what the cipher is for.
  3. **The string is in memory as 16-bit codes with a special space.** At `0x0935AD44` (the second word of the line): letters are their ordinary codes (`0x0074` for t), but the space is `0x1120`. The cipher expects `0x0020`, so the search answered "not in memory".
- **Fix:** the same word-by-word search as for Tactics Ogre (here with the ordinary pattern rule, since there is no layout), plus rows that may span the textures of one font.

### Final Fantasy Tactics: one painted picture, but the text is in memory

- The whole speech box is **one rectangle** of 149 by 72 from a texture at `0x08CE8820`. The battle menu is the same kind of thing. So this game really does paint its text into a picture with CPU code; there are no letters in the draw stream to read.
- **The text is in memory, in the game's own letter code:** digits are 0 to 9, capitals start at `0x0A`, small letters at `0x24`, the space is `0x95`. Just before `0x094494A6` (where the line starts):
  `E3 38` `<speaker>` `F8` `E3 30` `<name>` `DA 74` `<rest of the line>` `5F` `FE`
  The speaker's name is right before the line, and the next lines of the scene follow.
- The battle words are there too in the same code: Move, Wait, Status, Squire, Bravery, Aamza.
- So for this game "D2, second part" would not need pixels. It needs: a letter table per game (here three ranges in alphabet order), and a way to know which string is on screen now (watching what the painting code reads). That is a design decision for the user.

### Noise

Map tiles and unit sprites are letter-sized too and form short junk rows (3 to 7 symbols) in every battle recording. None was mistaken for text. Numbers (hit points and the like) are drawn from their own small textures; they have no string in memory and belong to Phase D3 (values).

## 5. Lessons

- **One recording answered what fifteen runs could not.** The Tactics Ogre cause was visible in the first `summary.txt`: a texture with 26,312 rectangles of 16 by 16 over the speech bubble, marked "a drawing surface".
- **Keep whole memory in recordings.** All three "not in memory" answers were wrong ideas about how text is stored, and only a full copy could show the truth.
- **"Not in memory" needs a second opinion.** The offline pattern search with no assumption about the letter code (any 8-bit or 16-bit values, same repeats) found the Persona 2 string at once. It could become part of the recorder's summary.

## 6. The two Tactics Ogre fixes (2026-11-13)

Both fixes from section 3 are built. The user gave the go-ahead on 2026-11-13.

### Fix 1: a texture is a surface only if the game draws into it

- The tap now tells the engine every surface the game draws into. A texture in video memory counts as a screen copy only if it lies inside one of those.
- In the recordings Tactics Ogre draws into `0x04000000`, `0x04044000` and `0x04088000`. Its fonts at `0x041B7B90` and `0x041C0BA0` lie beyond them, and the summary now calls them "a picture kept in video memory".

### Fix 2: when a row is not found, look for its words

- Only for grid fonts, and only under the strict rule (every code is the cell number plus one constant).
- It looks for stretches of six symbols. An answer needs two different stretches of the row found with the same constant.
- It also serves rows longer than the 96 symbols the whole-row search accepts, such as the Warrior help line.

### Test cases written for it

- A line stored with control codes between its words is read (the Tactics Ogre dialogue, rebuilt in the test).
- A font in video memory is read; letter-sized pieces of a real drawing surface are not.
- Three refusals: one short stretch alone is not believed; a row of neighbouring cells proves nothing; of two copies in layout order, the one that reads as text wins.

### Results

Core tests: 91 checks, 0 failed. The offline replay of the old recordings and one new recording pass with the real emulator gave the same answers:

| State | Read |
|---|---|
| Tactics Ogre dialogue | Both lines of the speech bubble |
| Tactics Ogre battle, slot 2 | The whole Warrior help line, "Denam", "Road" |
| Tactics Ogre battle, slot 3 | The whole movement help line, "Move", "Denam", "Road" |
| Tactics Ogre name entry | The same 15 texts as before |
| Final Fantasy Tactics, Persona 2 | Nothing, as expected: no false text |

### What is still not read in Tactics Ogre

- **The speaker's name in the dialogue state ("Vyce").** It is four letters in its own font texture, too short to learn from. In normal play that font is learned from any longer text first (it is the font of the battle screens). Not checked in play.
- **Numbers** (hit points and the like): their own small textures, no string in memory. Phase D3.
- **Focus:** everything on screen is read; knowing the selected item is Phase D3.

## 7. Persona 2 (both games) and Patapon 3 (2026-11-13)

The user made new states: Persona 2 Innocent Sin (name entry, menu, dialogue), Eternal Punishment with the English patch (dialogue, menu, name entry), Patapon 3 (cutscene bubble, in-game bubble). One recording pass, then fixes developed offline, then one confirming pass.

### What the recordings showed

- **Why Eternal Punishment spoke and Innocent Sin did not.** Both games draw dialogue the same way and keep it as 16-bit codes with ordinary letter codes. The patched game stores an ordinary space (`0x0020`); the official Innocent Sin stores its own space code (`0x1120`). The cipher expected ordinary spaces. So yes: it is the translation patch.
- **The dropped letters ("old oman").** The Persona 2 font is spread over two textures: capitals and punctuation in one, small letters in the other. Rows were built per texture, so the capitals fell out of every row.
- **Persona 2 menus and name entry.** They are drawn letter by letter from other font textures, and rows are formed. Their strings are in memory in a private two-byte code (small letters `01 xx`, capitals `00 E0+xx`, space `81 40`), so nothing matches. This needs the code-free search and a per-game letter table: not done.
- **Patapon 3.** Its bubble text is drawn letter by letter from one packed font texture at `0x08E80B00`, and the strings are plain 16-bit text in memory with markup such as `\<N1>`. It was not read because the guess at where the spaces are was wrong on most rows (the bubble wobbles and the letters are packed tight), so no row equalled its string.

### What was changed

1. **Rows may run across the textures of one font.** Letter-sized images are now grouped by texture size and format, and a row passes from one texture to another only where both letters have exactly the same top and height. Textures met that way are joined into a family, and trust is kept per family.
2. **A looser search, `GlyphBook::LearnFromLooseLine`,** tried after the whole-row searches. It ignores our guess at the spaces: memory may have a space between any two letters, or one other code the game uses as its space (the same one all along the line, never zero). If the row's first letters are not found, the rest is tried without them.
   - Because it forgives more, it asks more: at least 12 letters, at least 4 repeats, and a reading that is at least half small letters.
3. **Spacing learned from memory.** A loosely matched line shows where the spaces really are. The font's letter distances with and without a space are remembered, and later rows of that font use the learned distance instead of the general rule.
4. **A chance fit is not searched for for ever.** A line "found" twice and thrown out both times is left alone. Before this, name-entry and menu screens of Persona 2 kept finding and discarding the same wrong strings, 30 to 45 times in 30 seconds.
5. A gap exactly a quarter of the letter height now counts as a space (it was just under the limit in Persona 2).

### Results (offline replay and the real emulator agree)

| State | Read |
|---|---|
| Innocent Sin dialogue | The speaker's name and both lines |
| Eternal Punishment dialogue | Both lines; the speaker's name lacks its two capitals in this state |
| Patapon 3 in game | Five lines of the hint text, and two lines with letters still unknown |
| Patapon 3 cutscene | Two lines, and one line with letters still unknown |
| Tactics Ogre, all four | Unchanged |
| Persona 2 menus and name entry, Final Fantasy Tactics | Nothing, and no false text |

Core tests: 94 checks, 0 failed.

### Known limits

- **A letter is unknown until a matched line has contained it.** The cipher learns each letter from lines found in memory. Capital O and W of "Old Woman" appear in no long line of that one screen, so the name is incomplete there. In play the gaps close as more lines pass. Patapon 3 shows the same thing.
- **Cost.** On Persona 2 menu and name-entry screens the searches took about 0.4 to 0.7 seconds over 30 seconds. A single search can take longer than one frame. Not measured in live play.
- **Markup is not handled.** Patapon 3's `\<N1>` and similar are not drawn, so they do no harm, but a line broken by markup in the middle would not match.
