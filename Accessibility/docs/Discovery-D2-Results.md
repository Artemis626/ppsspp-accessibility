# Discovery layer: Phase D2 results and how to test it

> **Status (11 November 2026):** the first part of D2 is built and self-tested. It reads text that a game draws letter by letter with its own font. It is waiting for your testing. The second part (games that paint their text with CPU code) is not started and needs your decision.

## 1. The result in short

- **Tactics Ogre is now read.** Its harness case went from 0 of 10 to **10 of 10 exact**. In a run from boot the layer also read the caution dialog, the letter grid, the name being typed, and the confirmation question about the hero's name, with its Yes and No.
- **No pixel is read.** Letters are identified only by which rectangle of which font texture was drawn, and by matching lines against the game's memory.
- **It costs almost nothing once fixed.** The game holds 60 frames per second with the layer reading.
- **Three of your five games cannot be read this way.** Final Fantasy Tactics and both Persona 2 games paint their text with their own CPU code into one big picture, so there are no separate letters to see. They need the other method (section 6).
- **Patapon 3 is undecided.** In 70 seconds from boot it showed no text line the layer could see; it needs a longer look.

## 2. How it works, in plain words

1. **Seeing letters as unknown symbols.** A game with its own font draws each letter as a small rectangle from a font texture. The layer sees which rectangle and where it lands, but not which letter it is. A line is a row of unknown symbols.
2. **Finding the line in memory.** The string the line was drawn from is somewhere in the game's memory, and it has the same pattern of repeated letters. The layer searches memory for that pattern. A match says which character code each symbol stands for.
3. **Using the font's layout.** Font textures are almost always laid out in code order. Tactics Ogre's fonts all follow "cell number plus 32 equals the character code". Once a few symbols are known, the layer works out this rule and can read letters it has never seen in a matched line.
4. **Reading everything else directly.** After that, new text in the same font is read at once, with no search.

## 3. What keeps it from inventing text

Each of these rules exists because of a wrong answer I saw during testing.

- **A single answer or none.** If two different strings in memory fit a line, the layer does not choose.
- **A short or plain line proves nothing.** A line must be long and have repeated letters before the plain pattern search is believed.
- **A row of neighbouring cells proves nothing.** An on-screen keyboard row (A B C D) fits any table of rising numbers in memory. That produced a false match once, so such rows are not used for learning.
- **A second line must agree.** A font learned from one plain match is not shown until a second, different line matches and agrees. A match that follows the font's layout is trusted at once, because chance does not produce it.
- **Scenery is not text.** In Final Fantasy Tactics, pieces of the 3D scene are letter-sized rectangles. One such row once matched a name in the credits. With the rules above this no longer happens, and nothing false is read in that game.
- **Unknown stays unknown.** A character the layer cannot name is shown as the "unknown character" mark, not guessed.

## 4. What was measured

### Harness

| Game | Case | D1 | D2 |
|---|---|---|---|
| Test program | System message dialog | 2 of 2 | 2 of 2 |
| Tactics Ogre | Name entry | 0 of 10 | 10 of 10 exact |
| Final Fantasy Tactics | Opening dialogue | 0 of 1 | 0 of 1 |

### Speed

| | Reading, per frame | Memory searches, total over 60 seconds | Frame rate |
|---|---|---|---|
| First version | 0.05 ms | 6.1 s | 52 to 54 |
| Now | 0.05 ms | 0.35 s | 60 |

The slow part was searching all of memory. Now the layer keeps a list of the places that could hold text and searches only those, and a line that was not found is retried less and less often.

### The five games

| Game | How it draws text | Read by D2? |
|---|---|---|
| Tactics Ogre | One rectangle per letter, three fonts, plain single-byte text | Yes |
| Final Fantasy Tactics | Painted by CPU code into a picture, shown as one rectangle | No. No false text either |
| Persona 2: Innocent Sin | Whole screen painted by CPU code | Only its system dialog (a title, a one-line message, and the Back prompt) |
| Persona 2: Eternal Punishment | Whole screen painted by CPU code | Only its system dialog, the same |
| Patapon 3 | Not determined in 70 seconds | Nothing yet |

For Final Fantasy Tactics I traced one step further: one routine at `0x0880C700` copies the dialogue picture into the texture each frame from a buffer at `0x08BB0B20`. The letters are painted into that buffer by other code. Following that chain is the job of the second method.

### Automated tests

- Core without PPSSPP: 67 checks passed, including a font laid out in code order, a font with letters at scrambled positions, and cases that must be refused.
- Discovery tests (dialogs, font programs, Lua): all passed.
- Phase 3 regression: passed.

## 5. What is new for you to use

### Hotkeys

| Keys | Action |
|---|---|
| Ctrl+Shift+T | Read the text found on the game screen |
| Ctrl+Shift+A | Change what is read automatically: nothing, system dialogs only, or dialogs and game text |
| Ctrl+Shift+L | Write an inspection report of this screen and say a one-line summary |
| Ctrl+Shift+D | Say how this game draws its text (the survey) |

- **Automatic reading now starts in "dialogs and game text".** A line is spoken once its text has stopped changing for a third of a second. So a line typed out letter by letter is read once, complete, and a counter that never stops is not read.
- **Only text the layer is fairly sure of is spoken** (confidence 0.6 or more).

### The inspection report

This is my answer to your point about studying what the layer sees. Ctrl+Shift+L, or `discover.report()` in Lua, writes `accessibility/logs/discovery-report.txt`. It has these parts, each under its own heading in capitals:

- **TEXT FOUND:** every line, with its source, confidence, position and font texture.
- **LINES NOT READ YET:** rows of letter-sized images the layer cannot read, with the reason the memory search gave no answer, and the row's symbols.
- **FONTS WORKED OUT:** each font texture, how many symbols are known, and its layout rule.
- **TEXTURES DRAWN FROM IN THE LAST FRAME.**
- **SURVEY:** all counters, including what the layer costs.

The reasons a search gives no answer are: `not_in_memory`, `says_too_little`, `several_readings`, `two_layout_readings` and `too_short`.

### Lua

- `discover.report()` returns the report as a string.
- `discover.sprites()` returns every textured rectangle of the last frame, for studying a game's drawing yourself.
- `text.onscreen()` now also returns runs with the source `"game_font"`.

## 6. The decision I need from you

The method built so far needs the game to draw letters one by one through the graphics chip. Three of your five games do not.

**The other method** watches the memory a game paints its text into, finds the code that does the painting, and reads the character codes that code is given. It was avenue 6.7 in the design, and I had called it the hardest.

- **For:** it is clearly needed. In your folder it covers more games than the method just built.
- **For:** it is the same method older consoles need, so the work carries over to your later targets.
- **Against:** it is the least certain part of the project. I expect it to work on some games and to need per-game help on others.
- **Extra difficulty in Final Fantasy Tactics:** its text is also in a custom character table, so two hard problems meet there. Persona 2 is probably the same.

**My recommendation:** before building it, let me spend a short investigation on Persona 2 and Final Fantasy Tactics to see how far the chain from picture back to character codes can be followed automatically. That gives you a real estimate before you commit to it.

## 7. How to test

1. **Tactics Ogre.** Start a new game or load a save and move through menus. You should hear text as it appears. Ctrl+Shift+T reads the whole screen again.
2. **If something is wrong or missing,** press Ctrl+Shift+L on that screen and send me `accessibility/logs/discovery-report.txt`. It says what the layer saw and why it could not read a line.
3. **Too much speech?** Ctrl+Shift+A cycles through the reading modes.
4. **Save states for the harness** are still very welcome, especially pairs (the same menu with the cursor one step lower).

## 8. Known limits

- **Accented letters.** A line containing an accented letter is not searched for yet; allowing those bytes made every search many times slower. The font is still learned from other lines, but accented letters themselves show as unknown.
- **Extra symbols beyond plain ASCII** (long dashes, curly quotes) show as unknown until a line in memory shows what they are.
- **Japanese and other two-byte text** is not handled yet.
- **Texture memory that is reused.** If a game loads a different font of the same size at the same address, the layer may read with the old font's knowledge until it notices. Loading a save state clears everything it has learned.
- **Learning is per session.** Nothing is saved to disk yet; a game's fonts are learned again each time, which takes a second or two on the first text screen.
- **The first second of a new font** is silent while it is being learned.
