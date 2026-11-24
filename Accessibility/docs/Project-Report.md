# Project report, in plain language

Written 2026-11-13, brought up to date 2026-11-16. This is the "what exists and what it does" document for a reader who has not followed the work. The technical reference is [../README.md](../README.md); each phase has its own results document in this folder.

## 1. What this project is

It is a private copy ("fork") of the PPSSPP PlayStation Portable emulator, version 1.20.4, with two things added:

1. **A scripting platform for accessibility mods.** A modder writes a small Lua script for a game. The script reads the game's memory and tells the screen reader what to say. The emulator's own menus speak too.
2. **A research tool called the Discovery layer.** It watches a running game and finds its text and numbers by itself, so the modder spends far less time hunting through memory.

It is not a finished accessibility mod for any game, and it is not meant to make games accessible on its own. It is the workshop in which such mods are made.

Everything added lives in the `Accessibility/` folder. The original emulator's files are changed in about 100 lines, each marked `[ACCESS]`.

## 2. Part one: the scripting platform

### What is built

- **Speech.** Text is sent to NVDA through NVDA's own controller library. If NVDA is not running, nothing breaks.
- **Spoken emulator menus.** Moving through PPSSPP's menus speaks the item under focus, including buttons that only show an icon.
- **Lua scripts.** A script named after a game's identifier (for example `ULES01500.lua`) loads by itself when that game starts.
- **Reading and writing memory.** A script can read or change any value in the game's memory, in 8, 16 or 32 bits, and read text.
- **Being told when something happens.** A script can ask to be called when the game reads an address, writes to it, or runs a certain instruction. This is how "say the money when it changes" works with no polling.
- **Hotkeys.** A script binds keys by key name or by physical key position, which matters on non-English keyboards.
- **Sound cues.** Short sounds, with volume and left-right position, for things speech is too slow for.
- **Controller input.** A script can press the PSP's buttons.
- **Cheat-code addresses.** A helper turns an address from a CWCheat code into a real memory address.

### State

Phases 0 to 3 are done. The hotkeys were tested by the user. The rest of Phase 3 is self-tested but has not yet met real game data in the user's hands. Phase 4 (start-up options, spoken emulator messages, better error messages, text tables, speech through Windows when NVDA is not running) has not been started, apart from a few pieces needed early for testing.

## 3. Part two: the Discovery layer

### The idea

To make a game speak, a modder must first find where the game keeps its text and numbers. That hunt is the slow part of every accessibility mod.

The Discovery layer does much of the hunt automatically. It watches what the game draws and compares it with what is in memory. Its results are a report a modder can read, and later a per-game map that scripts can use by name.

### The one hard rule

**No reading of pixels, and no text recognition.** The layer never looks at what an image shows. It only knows *which* image was drawn (its address in memory and its rectangle) and *where* it landed on screen. Everything below follows from that.

The user allowed one exception: things that really are pure graphics with no text behind them. It has not been used.

### How text is read

A game with its own font draws each letter as a small picture taken from a sheet of letters. The layer sees "picture 7, picture 2, picture 9, picture 9, picture 4" but not which letters they are. So a line of text is, at first, a row of unknown symbols.

Somewhere in memory is the sentence that line was drawn from, and it has the same pattern of repeats: third and fourth alike, the others different. The layer searches memory for that pattern. A match tells it which letter each picture is. From then on, every line using those pictures is read directly.

Several refinements were needed, each because a real game did something unexpected:

- **Fonts laid out in order.** Many letter sheets are in alphabet order. Once a few letters are known, the rest follow from their position, without ever having been seen in a matched line.
- **Fonts kept in video memory.** Tactics Ogre keeps its fonts where screen pictures normally are. The layer first mistook them for screen copies.
- **Sentences broken up in memory.** Tactics Ogre puts control codes between the words of its dialogue. The layer now also searches for single words.
- **Fonts spread over two sheets.** Persona 2 keeps capitals in one sheet and small letters in another.
- **A game's own code for the space.** Persona 2 Innocent Sin does not store an ordinary space.
- **Spacing.** Games do not draw spaces. The layer learns from matched lines how wide a gap means "space".
- **A game's own letter code.** Persona 2's menus do not store letters as ordinary codes at all. The layer now finds such text by its pattern of repeats alone, and then works out which code is which letter by itself: an alphabet's codes are in alphabet order, and the game's own strings must show the letter counts of English in exactly one placing.

### How numbers are read

A number is two or three symbols, too short for the pattern search. Numbers get their own method.

Digits are almost always kept as a strip of ten pictures in order. So the position of a picture in its strip says which digit it is, once the strip's first digit is known. The layer tries each possible first digit, works out which numbers that would put on screen, and looks in memory for a place where several of those numbers stand together. Only the right guess finds such a place.

This gives two results at once: the numbers are read, and the place found is the record they come from (for example a character's hit points and maximum). Each number then keeps a list of possible addresses, and the list shrinks every time the number changes on screen.

### Guarding against wrong answers

The main danger of this whole approach is a false match: finding a pattern in memory by chance and "reading" nonsense. Every acceptance rule in the code exists because a wrong answer was actually seen in testing. Two examples:

- A font is only trusted after two different lines agree, or after one match that follows the sheet's order.
- For numbers, only numbers of three or more digits count as evidence, because small numbers are everywhere in memory. An early version read 143 as 254 for exactly this reason.

The rule for future work is: never loosen one of these without a test that shows it is safe.

### Recordings

Running the emulator again and again to learn one thing each time is slow and costly. So the layer can record: for ten seconds it writes down everything it sees, plus a full copy of the game's memory.

- **To record:** press Ctrl+Shift+K while the screen in question is showing.
- **A recording can be replayed without the emulator,** in seconds, with any newer version of the layer. Most problems are now found and fixed this way.
- **Recordings contain game memory.** Like save states, they are game data and are kept out of this repository (see section 6).

### The inspection report

Ctrl+Shift+L writes a plain-text report of what the layer knows about the current screen: the text it read, the rows it could not read and why, the fonts it worked out, the numbers on screen with their possible addresses, and what the work cost in time.

### What it finds in each test game

| Game | Text | Numbers | Notes |
|---|---|---|---|
| Tactics Ogre (Europe) | Dialogue, names, help lines, terrain, commands | Hit points and other stats, updating as they change | The character record was found at `0x08AD84B4`. Elevation and units on the map are not read. |
| Persona 2 Innocent Sin | Dialogue, with some letters missing at first; main menu and options | Not yet | The menus use a letter code of the game's own, which the layer works out by itself. Name entry is not read. |
| Persona 2 Eternal Punishment (English patch) | Same as Innocent Sin | Not yet | The two games behave almost identically. |
| Patapon 3 | Speech bubbles and hints, with some letters missing at first | Not yet | Was silent when started after another game; fixed. |
| Final Fantasy Tactics | Nothing | Nothing | It paints text into a picture and stores it in its own letter code. Set aside by the user. |

### Known weaknesses

- **Missing letters.** A letter is unknown until a matched line has contained it. Early lines come out with holes, and everything is learned again in each session.
- **Private letter codes** are worked out for English text in two-byte codes only (built 2026-11-14 for Persona 2's menus). Digits and punctuation in such a code are not.
- **Chatty numbers.** Numbers are spoken like text when they appear or change. In a battle this is a long tail of speech.
- **Single digits** (Tactics Ogre's elevation) cannot be confirmed by the number method.
- **What is saved between sessions is limited.** The per-game profile (built 2026-11-15) keeps letter codes, the letters of fonts whose images stay put, and an index of addresses found. Fonts that are caches, such as Persona 2's, hold other letters each session, so their letters cannot be kept; for those games the profile saves little time.
- **The selected item** is found on screens that mark it by a moving mark, by position or by colour, and only after the selection has moved once (built 2026-11-15). Lists are not yet understood as lists.

## 4. What comes next

The user chose Persona 2 (both games) as the focus on 2026-11-13, out of personal interest and because what Persona 2 needs is what other games need too.

### Already done since that choice

- **A search that assumes no letter code** was built on 2026-11-14 ([results](Discovery-OwnLetterCode-Results.md)). It reads Persona 2's menus.
- **The selected item** was built on 2026-11-15 (section 3 of the [D3 results](Discovery-D3-Results.md)). It is waiting for the user's test.
- **A per-game profile** was built on 2026-11-15: `accessibility/profiles/<game>.ini`, plain text, described in the [user guide](User-Guide.md). It keeps what the letter-code search learns. It does not cure Persona 2's missing letters, because that game's fonts are caches. It is waiting for the user's test.

### Not started

1. **A remedy for missing letters:** look for a row's unknown letters next to where its other rows were found, using the letters already known as a guide.
2. **Numbers kept but not spoken unasked,** so a script decides what is said. Recommended, not yet approved.
3. **The cursor, single digits and the map file** (the rest of Phase D3), then the Lua interface to all of it (Phase D4).

Work proceeds one piece at a time, and stops after each piece for the user's test. The history of each piece, with the user's test reports, is in the [work log](Work-Log.md).

## 5. How the work is done

- **Phases, with a stop after each** for the user's test.
- **Few emulator runs.** The user makes save states and recordings on the screens that matter; they are studied offline. The emulator is run once after a change, to confirm the built program agrees with the offline result.
- **Tests without the emulator.** The Discovery core has its own test program (125 checks on 2026-11-16, all passing) that runs in seconds.
- **The core is emulator-independent.** The part of the Discovery layer that does the thinking includes nothing from PPSSPP, so it can later be attached to another emulator such as RetroArch.

## 6. Copyright and what is kept out of the repository

This repository holds only source code and documents. Game data is kept outside it, in folders beside it, and is also blocked by the ignore rules in `.gitignore`:

- **Disc images** (`.iso`, `.cso`, `.chd`, `.pbp`).
- **Save states and saves** (`.ppst`, the `memstick` folder).
- **Recordings,** which contain a copy of the game's memory.
- **The harness data folder** with the user's states and reports.

What the repository does contain from games:

- **Short quotations of on-screen text** in the results documents and test cases (a line of dialogue, a menu word), as evidence of what was read.
- **Memory addresses and descriptions of data layouts.**

Third-party code included: PPSSPP itself (GPL 2.0 or later), miniaudio (public domain or MIT-0), and the unmodified NVDA Controller Client library (LGPL 2.1).

The repository is private. Nothing is public until the owner decides.
