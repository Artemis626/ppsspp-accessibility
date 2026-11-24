# Phase 3 Testing Guide

## Before you start

### What you need

- The desktop shortcut **PPSSPP Accessibility - test script**. It runs `scripts\phase3_test.lua` with every game.
- A game where you know **one memory address that changes while you play**, such as HP, money or a cursor position, and its size (1, 2 or 4 bytes).
- A text editor for two files in `E:\Claude projects\PPSSPP Accessibility scripting\builds\access-dev\`:
  - `scripts\phase3_test.lua`, the test script.
  - `accessibility\hotkeys.ini`, the hotkey file. It's created the first time the build starts.
- Num Lock **on**, for the Numpad5 tests.

### How to report

For each section, tell me the step numbers that failed and what you heard instead. If anything fails, also send `accessibility\logs\access.log` (the previous run is kept as `access.old.log`).

## 1. Setup

1. Open `scripts\phase3_test.lua`. At the top, set `ADDRESS` to your address and `SIZE` to its size. Leave `EXEC_ADDRESS = nil` for now. Save.
2. Start the shortcut and load your game.
3. **Expected:**
   - "PPSSPP accessibility ready".
   - Then "Script phase3_test.lua loaded".
   - A short beep.
   - Then "Phase 3 test ready. Control shift H lists hotkeys."
4. Find your game ID: open `access.log` and look for the line `Game started: <ID>`. You need it in section 6.

## 2. Memory write watch

1. In game, do something that changes your value (take damage, spend money, move the cursor).
2. **Expected:** a short click and "Written <new value> at pc <hex number>" each time the value changes.
3. Make the value change quickly several times in a row. **Expected:** clicks for every change, but speech at most 4 times a second. It must never pile up a long queue.
4. Stand still so the value doesn't change. **Expected:** silence. Writes of the **same** value are ignored on purpose.
5. Press **Ctrl+Shift+1**. **Expected:** "Value <current value>", interrupting anything else.
6. Compare that number with what the game shows. They should match (or match the raw number you saw in your research).

## 3. Script hotkeys

1. **Ctrl+Shift+2:** five tones, one every quarter second, rising in pitch and moving **left, half left, centre, half right, right**. Headphones make it clear. Check that the panning really moves.
2. **Ctrl+Shift+3** with nothing held. **Expected:** "No buttons, stick 0.0, 0.0".
3. Hold a PSP button (on a gamepad, or the mapped keyboard key) and press **Ctrl+Shift+3**. **Expected:** that button's name, for example "Cross, stick 0.0, 0.0". Try holding the analog stick: the stick numbers should change (−1 to 1).
4. Go to a game menu where Cross confirms. Press **Ctrl+Shift+4**. **Expected:** "Pressed cross", and the game reacts as if you pressed Cross.
5. In game, press **Numpad5**. **Expected:** "Numpad works".
6. Press **Escape** to open PPSSPP's pause menu, then press **Numpad5**. **Expected:** **no** "Numpad works". Keys without a modifier are off while a menu is open. Arrow keys in the pause menu should still be spoken as usual. Close the menu.

## 4. Built-in speech hotkeys

1. **Ctrl+Shift+R:** repeats the last message.
2. **Ctrl+Shift+PageUp**, pressed several times: steps back through older messages. At the oldest you hear "Oldest message" before it.
3. **Ctrl+Shift+PageDown:** steps forward again. At the end you hear "Newest message".
4. Browse back to an older message, press **Ctrl+Shift+C**, and hear "Copied". Paste into Notepad: it must be **that** message, not the newest one.
5. Press a hotkey that speaks a long message, such as Ctrl+Shift+H, then **Ctrl+Shift+S** straight away. **Expected:** speech stops.
6. **Ctrl+Shift+H:** lists the 7 built-in hotkeys, then "Script hotkeys:" followed by the 5 script ones with their descriptions.
7. These keys should also work **while the pause menu is open**, because they all have modifiers.

## 5. Reload and spoken script errors

1. In game, press **Ctrl+Shift+F12**. **Expected:** "Reloading script", then "Script phase3_test.lua loaded" and the ready message. The game doesn't restart.
2. Edit `phase3_test.lua`: change the text in the `speak("Phase 3 test ready...")` line. Save, press Ctrl+Shift+F12, and hear the new text. This is your edit-and-test loop for memory research.
3. Make a deliberate syntax error: on any line, type `x = = 1`. Save and reload. **Expected:** "Script error. phase3_test.lua:<line>: unexpected symbol near '='". The game keeps running with no script.
4. Remove the error and reload. **Expected:** the script loads normally again.
5. Make a runtime error: in the Ctrl+Shift+1 function, change `read(ADDRESS)` to `read(nil)`. Reload and press Ctrl+Shift+1. **Expected:** a spoken error naming `bad argument #1`. Press it a few times quickly: errors are spoken at most once every 2 seconds, and the same error isn't repeated within 10 seconds. Undo the change and reload.

## 6. hotkeys.ini

Replace `<ID>` with your game ID from section 1. After each change, save and press **Ctrl+Shift+F12**.

1. Add this at the end of the file:

   ```ini
   [<ID>]
   stereoTest = Ctrl+Shift+5
   ```

   **Expected:** Ctrl+Shift+5 plays the stereo test, Ctrl+Shift+2 does nothing, and Ctrl+Shift+H lists the new key.
2. Change it to `stereoTest = none`. **Expected:** no key plays the stereo test, and it disappears from the Ctrl+Shift+H list.
3. Change it to `stereoTest = Ctrl+Comma`. **Expected:** a spoken error explaining that punctuation keys can't be used by name. The hotkey isn't bound.
4. Change it to `stereoTest = Ctrl+Shift+sc:0x06`. That's the physical **5** key above the letters, by scancode. **Expected:** Ctrl+Shift+5 works, and Ctrl+Shift+H lists it as `Ctrl+Shift+sc:0x6`.
5. Change it to `stereoTest = Ctrl+Shift+R`. **Expected:** a spoken error saying Ctrl+Shift+R is already used by repeatLast.
6. Built-in keys: in `[global]`, change `repeatLast = Ctrl+Shift+R` to `repeatLast = Ctrl+Shift+F5`. Reload. **Expected:** Ctrl+Shift+F5 repeats the last message.
7. Put everything back as it was and reload.

## 7. Execute watch (optional)

This needs the address of an instruction you know the game runs, from your research or the PPSSPP debugger. The address must be a multiple of 4: it ends in 0, 4, 8 or C in hex.

1. Set `EXEC_ADDRESS = 0x...` in `phase3_test.lua` and reload.
2. Do whatever makes the game run that code. **Expected:** "Code ran, a0 is <hex number>", at most once a second.
3. If the address is the start of a function (for example a text-drawing function), `a0` is usually its first argument, often a pointer to the text or to a structure.

## 8. Check that nothing broke

1. With the original shortcut **PPSSPP Accessibility**, navigate the menus. They should speak as in Phase 1, including values and checkbox states.
2. Play for a few minutes with the test script running. Compare speed and sound with your normal PPSSPP. Watching one address should feel no different.
3. Close PPSSPP with a script running. It should close normally, with no hang or error.
