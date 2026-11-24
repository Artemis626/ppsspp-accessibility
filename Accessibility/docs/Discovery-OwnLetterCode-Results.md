# Discovery layer: text in a game's own letter code (Persona 2's menus)

Built 2026-11-14, from two recordings the user made on the main menu and options screen of Persona 2 Innocent Sin and Eternal Punishment. Waiting for the user's test.

## 1. What the recordings showed

- **The menus were silent for two reasons, not one.**
  1. **The text is not stored in ordinary letter codes.** Each letter is two bytes, high byte first: small letters `0x0101` to `0x011A`, capitals `0x00E0` to `0x00F9`, the space `0x8140`, the end of a string `0xFFFF`. So "Message Speed" is in memory, but no search for letters could find it.
  2. **Narrow letters were thrown away.** In the menu font an "i" or "l" is two texels wide. The layer only accepted images three or more wide as letters, so "Music" arrived as "Mus c" and could not have matched anything.
- **Both games use the same code and the same layout.** The string tables are at different addresses (`0x08C1D074` in Innocent Sin, `0x08F71A88` in Eternal Punishment for "Message Speed").
- **The whole menu string table is there in plain order:** item names, help lines, zodiac signs, the party's names. Decoded offline it reads without a gap.

## 2. What was built

### A search that assumes no letter code

`GlyphBook::LearnFromCodeLine` in `Discovery/Core/GlyphPrivate.cpp`. It is tried last, for a row no other search found.

- It looks for the row among 16-bit values of any kind, by its pattern of repeats alone: the same symbol is always the same value, different symbols are different values.
- A find gives each symbol the game's code for it. It does not yet say which letter that is.

### Working out the letters, with no table supplied

Two facts do it:

1. **The codes of an alphabet follow each other in alphabet order.**
2. **Around the string found are the game's other strings.** In English, "e" is the commonest letter, then "t", "a" and so on. The layer counts every value within 8 KB of the strings found and tries each way of laying the alphabet on the codes. Only one placing gives counts that look like English.

Capitals are placed the same way, against how often each letter begins an English word.

In both Persona 2 games the fit is 0.97 to 0.99 for small letters and 0.92 to 0.95 for capitals (1.0 would be perfect), and the layer reports the code exactly as section 1 describes it.

### Smaller changes that the recordings forced

- **Narrow letters count as letters** (width from 1.5 instead of 3).
- **A narrow letter is not a grid cell.** A two-texel "i" divides any texture evenly, so it was taken for a cell of a grid font, which put a false space after every "i".
- **Once a font's own code is known, the other searches leave it alone.** They had been making chance matches on this font; for twenty frames the replay showed a volume option's help line as three garbled words. Their letters, their trust and their spacing for that font are now dropped when the code is worked out.
- **Short rows fill in single letters.** "Load" and "Title" are too short to search for on their own. Once the code is known they are looked up with their known letters as a guide, and accepted only if there is one reading and the new code is a letter.

## 3. The rules against wrong answers

- **A row must say enough:** ten symbols or more, and four repeats or already-known codes.
- **One reading only.** If memory holds the pattern with two different sets of values, there is no answer.
- **Two symbols never share a code,** within the row or within the font.
- **Nothing is shown until the letters are worked out,** and that needs two different rows found, at least 150 small letters counted, a fit of 0.90 or better, and a clear lead over the next best placing. A chance match gives counts that look like no language, so it shows nothing.
- **Capitals are settled separately** and stay unknown if their fit is not clear.

The refusal test: the same strings with the alphabet laid on the codes in a scrambled order are found in memory but never shown.

## 4. Checked

- **Core tests:** 108 checks, 0 failed. New: text in an own letter code is read; a row never searched for is read from the letters learned; a short row gives the missing letter; scrambled codes are refused.
- **Offline replay of every recording:** both Persona 2 menu recordings read; the older Eternal Punishment options state reads completely; dialogue in both games, Patapon 3, Tactics Ogre (text and numbers) and Final Fantasy Tactics are unchanged; no own-code table is invented in any other game.
- **One real pass** on the Eternal Punishment options state agrees with the replay.

## 5. Limits

- **English only.** The letter counts are English. Another language with the Latin alphabet may fit well enough; Japanese will not.
- **Only 16-bit codes at even addresses.** A one-byte code of a game's own (Final Fantasy Tactics) is not searched for.
- **A letter is still unknown until a found row has contained it.** "Fast" and "Hard" stay "?ast" and "?ard" on the options screen: with one letter missing there are several words they could be, so the layer rightly gives no answer. A longer row with an "F" or "H" settles them.
- **Digits and punctuation in this code are not worked out.** They are not alphabets, so the counting method does not apply.
- **Cost.** One search reads all of memory and takes about 25 milliseconds on the live game. It runs at most once a second, and only while there are unread rows. Whether that is heard as a hitch in play is not known.
- **Nothing is saved.** The code is worked out again in every session, within a few seconds of the first long menu line. Saving it is the per-game profile, still to be designed.
- **Persona 2's name entry screen** found one row and could not work out letters from it alone. Not studied further.
- **Selection is not known.** The layer reads every item of a menu, not which one the cursor is on.

## 6. How to test

1. Start Persona 2 (either game) and open the main menu, then the options screen.
2. Stay on a screen with a long help line for a few seconds. The first lines are read once two different long rows have been seen.
3. Move through the options: the help line at the top should be spoken as it changes.
4. Ctrl+Shift+L: under "FONTS WORKED OUT" the report names the letter code it found.
5. Try the field menu and a battle menu, and record (Ctrl+Shift+K) any screen that stays silent or reads wrongly.
