# Discovery layer, Phase D3: results

Phase D3 is "non-text data and the game map" (design, section 7). It is built in parts. This document is added to as each part is done.

## 1. A fix that came first: a second game in one session read nothing

- **What the user saw (2026-11-13):** Patapon 3 spoke nothing, in either save state, while Persona 2 just before it spoke.
- **What the user's recording showed:** every memory search answered "not in memory", yet the same recording replayed offline was read at once.
- **Cause:** to search fast, the layer keeps a list of the places in memory where text could be, and rebuilds it every five seconds. When a game ended, the frame count went back to zero but the list's age did not. So the second game was searched with the first game's list until its frame count passed the old one. Patapon 3 was closed before that.
- **Fix:** the list is thrown away when a game starts or stops and when a save state is loaded (`Engine::Reset`, `Engine::OnMemoryReplaced`).
- **Test:** `TestSecondGameInOneSession` in the core tests. It failed before the fix.
- **Not verified in play.** The user's test: start Persona 2, close it, start Patapon 3 in the same emulator window.

## 2. First part: numbers on screen (built 2026-11-13)

### What it does

- It reads numbers drawn from a **strip of digits**: hit points, their maximum and the like.
- It finds the **record** in memory the numbers come from.
- For every number on screen it keeps a list of **candidate addresses**, and shortens the list each time the number changes.

### Why numbers need their own method

A number is two or three symbols. The text search needs a row of at least five symbols with repeats, so it can never read "143". But digits have two properties that text does not:

1. **Games keep them in a strip.** Ten images of one size, side by side in the texture, in the order 0 to 9. So a symbol's place in the strip says which digit it is, up to one unknown: which digit the strip starts with (the 0 may not have been drawn yet).
2. **Numbers shown together are kept together.** Hit points and their maximum are a few bytes apart in memory, as plain binary values.

So the layer guesses the strip's first digit, works out which numbers that guess puts on screen, and looks in memory for a place where several of them stand together as 16-bit values. Exactly one guess must find such a place. That confirms the digits and finds the record in the same step.

No pixel is read: a digit is known only by its texture address and rectangle.

### The acceptance rules, and the wrong answer behind them

The first version was wrong on its first real try, in Tactics Ogre:

- With the guess "the strip starts at 1", the padding "000" became 111 and 444, and "35 40" became 46 and 51.
- All of those were found close together in a table of small values, so the wrong guess won.

The rules that came from it:

- **Only numbers of three or more digits count as evidence, and not if all their digits are alike.** Small numbers are everywhere in memory.
- **At least three such numbers, with at least eight digits between them, within 32 bytes either side.**
- **No other guess may do as well.**
- **A strip is only named if at least three of its own digits were part of the evidence.**
- **Each row of a texture is its own strip.** In Tactics Ogre the large digits continue on a second row (the 8 of "1/8"). Guessing how many digits fit in a row would name that 8 without evidence, so it stays unread until a number containing it is confirmed.

Do not loosen these without a test case, as with the text cipher.

### Checked

- **Core tests:** 104 checks, 0 failed. New: numbers read and a change narrowing the candidates to the right address; two refusal cases (numbers not together in memory, with the decoy table above; a strip seen without neighbours); a strip whose first digits were never drawn.
- **Offline replay of all recordings:** Tactics Ogre battle states read; no number invented in Persona 2 (both), Patapon 3, Final Fantasy Tactics, or Tactics Ogre's name entry and dialogue.
- **One real pass** on the Tactics Ogre battle state agrees with the replay.

### What Tactics Ogre shows (battle state, Denam)

- **On screen:** 143 and 143 (hit points and maximum), 000 and 000, 000 and 200, "35 40" twice, "01".
- **Digit strips:** texture `0x09538610`; small digits 9 by 13 at row 1, one every 10; large digits 14 by 19 at row 15, one every 14.2. Both start with 0.
- **The record:** `0x08AD84B4`, six 16-bit values: 143, 0, 0, 143, 0, 200. That is three current values followed by their three maximums. It is 124 bytes after the Goth address from the cheat database (`0x08AD8438`), so it is in the party data.
- **Copies:** the same record at `0x08D40140` and `0x08D60CB0`, and seven more from `0x09E2AA98` on, spaced `0x3AF8` apart with a counter beside them: an array, probably the units in battle or saved turns. Which copy the game reads for the screen is not known yet. A change of hit points will tell: the candidates that do not change are dropped.
- **Chance finds** are listed too (a table at `0x08B8307A` holding 143, 143 and 200 among coordinates). They are wider than the real record, are listed after it, and fall away at the first change.

### Limits

- **Elevation is still not read.** It is one large digit from another part of the texture, and a single digit cannot be confirmed by this method: any small value is everywhere in memory. It needs the value followed over several changes (see "next").
- **Units on the field** are pictures, not numbers or text. They belong to the cursor and map work.
- **Screens with fewer than three numbers of three digits** confirm nothing, so their digits stay unread until a richer screen was seen. Once a strip is confirmed it is read everywhere.
- **Leading zeros** are read as drawn: "000", "01".
- **The "/" of "1/8"** is not a digit and stays unknown.
- **Only 16-bit and 32-bit values at even addresses** are searched. A value kept as text, as a float, or in one byte is not found.
- **Numbers are now spoken like text** when they appear or change, since they are text runs. Whether that is wanted during play is a question for Phase D4.
- **Narrowing by changes is tested only in the core tests,** not in a game: the recordings hold one memory copy each.

### How to test

1. Start Tactics Ogre and load a battle state.
2. Press Ctrl+Shift+L for the inspection report. It has a new section, "NUMBERS ON SCREEN", with each number, its candidate addresses, and the records found.
3. Let a unit take damage, or move the cursor to another unit, and ask for the report again. The hit point lines should show fewer addresses, with "narrowed by 1 change".
4. For the fix in section 1: start Persona 2, close the game, start Patapon 3 in the same window, and load a state with dialogue.

### Where the code is

- `Discovery/Core/GlyphDigits.cpp`: `GlyphBook::LearnDigits`, the strips and the records.
- `Discovery/Core/Engine.cpp`: `LearnNumbers` (at most every two seconds, and only when the rows on screen changed), `TrackNumbers`, `NumberCandidates`, and the report section.
- `Engine::Numbers()` gives the numbers of the last frame to the adapter. Nothing in Lua uses it yet.

## 3. Second part: the selected item (built 2026-11-15)

### What the user's test showed

In Persona 2 the help lines were spoken, but most menu items were not, and the layer could not say which item the cursor was on. Automatic reading speaks new text as it appears; a menu's items all appear at once and then never change, so moving the cursor produced nothing to say.

### What the recordings showed

Persona 2 marks the selected item in three different ways, depending on the screen:

- **Options screen (both games): a mark.** A small bar drawn from its own texture (`0x091D5A00` in Innocent Sin) sits under the selected row and jumps to another row when the selection moves. Nothing else changes.
- **Eternal Punishment's main menu: a place.** The selected item is drawn 8 pixels to the right of the others. When the selection moves, two items change places.
- **Innocent Sin's main menu: neither.** Positions and palettes are the same for every item. It is most likely the colour the game gives the draw, which the layer did not record until now. Not confirmed.

### What was built

`SelectionFinder` in `Discovery/Core/Selection.cpp`. It looks for all three, with no pixel read:

- **A mark:** a small image (at most 6 pieces, at most 320 by 40) that rests by one text, then rests by another, standing by both in the same way.
- **A place:** two texts that shift sideways by the same amount in opposite directions within a third of a second.
- **A colour:** two texts that exchange colours within a third of a second. The colour is the one the game sets in the draw command (the vertex colour, or the draw's own colour when the corners have none). It is not read from the picture. The draw tap now passes it on (`SpriteDraw.color`), and recordings keep it.

For a place or a colour, the selected one of the two is the one now unlike the other texts of its size.

**Nothing is claimed from a still screen.** A title is also "the one in another colour". The layer learns how a screen marks its selection by seeing the selection move once, and follows it from then on.

**What is spoken:** when automatic reading is on (Ctrl+Shift+A, third setting), the selected item is spoken as soon as the selection moves, interrupting earlier speech. If the mark stands by a value, the row's first text comes first: "Message Speed: Normal". In Lua, `text.onscreen()` already had a `selected` field per run; it is now filled for game text too.

### The rules against wrong answers

- **A mark must rest:** stay within 3 pixels for 8 frames. An image passing over a text (a figure walking by) is beside it for a while but never stops.
- **A mark must itself move.** A help line that changes beside a still icon looked like "the icon went to another text"; that was seen in the first replay and is now refused.
- **A mark must stand by both texts alike:** same size, same height relative to the text.
- **Two items only, changing places or colours, give no answer:** there is no third text to say which way round it is.

### Checked

- **Core tests:** 117 checks, 0 failed. New: a mark, a place and a colour are each found after one move and not before; the selected run is flagged; a title in another colour is not taken for a selection; an image drifting over the menu is not a mark.
- **Offline replay:** in both of the user's menu recordings the selection is followed through the options screen (all ten options, from the music setting to the window colour), and by place on Eternal Punishment's main menu. No selection is claimed in any other recording (Tactics Ogre, Patapon 3, Final Fantasy Tactics, dialogue).
- **One real pass** confirmed the build and that draw colours are now recorded (the options font is drawn in four colours).

### Limits

- **The first position is not known.** On opening a menu nothing is said about the selection until it moves once.
- **Innocent Sin's main menu is not verified.** The colour method is built and tested on made-up data only. A new recording of that menu, moving the selection, will show whether it works.
- **Colours may also mark the chosen value** of an option (the options font uses four colours). Changing a value may then be announced as a selection. Not seen in play yet.
- **Screens that mark the selection some other way** (a highlight box behind the text drawn without a texture, a size change) are not found.
- **Field and battle menus were not in the recordings.**
- **Lists that scroll** are not understood as lists: there is no index and no "3 of 12".

## 4. Next in D3 (not started)

1. **The per-game profile:** saving what was learned (letter codes, digit strips, how each screen marks its selection, addresses), so it is known from the first frame.
2. **Single digits and values followed over time** (Tactics Ogre's elevation).
3. **Names for values**, the map file, and the first spoken explorer.
