<!-- [ACCESS] This file replaces PPSSPP's own README, which is kept unchanged as README-PPSSPP.md. -->
# PPSSPP Accessibility Scripting

A fork of the [PPSSPP](https://www.ppsspp.org/) PlayStation Portable emulator, made so that blind and visually impaired players can play PSP games with a screen reader, and so that modders can make any PSP game speak.

It is based on PPSSPP v1.20.4. It runs on Windows and speaks through NVDA. This repository is private while the work is in progress.

## What it adds

1. **The emulator's own menus speak.** Moving through PPSSPP's menus reads the item under focus.
2. **Lua scripting for accessibility mods.** A small script per game reads the game's memory and tells the screen reader what to say. It can react when a value changes, bind hotkeys, play sound cues and press the PSP's buttons.
3. **A layer that finds a game's text by itself,** called the Discovery layer. It reads dialogue, menus, the selected item and on-screen numbers in games it has never been told about, and records where in memory they are kept, so a modder spends far less time searching.

The Discovery layer never reads pixels and uses no text recognition. It knows which image the game drew and where, and compares that with what is in the game's memory.

## Where to start

| If you want to | Read |
|---|---|
| Play, or write a script for a game | [User guide](Accessibility/docs/User-Guide.md) |
| Understand everything that has been built, in plain language | [Project report](Accessibility/docs/Project-Report.md) |
| See the status, the build steps and the full list of script functions | [Technical README](Accessibility/README.md) |
| Read about PPSSPP itself | [PPSSPP's own README](README-PPSSPP.md) |

## What works today

| Game | Read without any script |
|---|---|
| Tactics Ogre: Let Us Cling Together (Europe) | Dialogue, names, help lines, commands, hit points and similar numbers |
| Persona 2 Innocent Sin and Eternal Punishment | Dialogue; the main menu and options, with the selected item |
| Patapon 3 | Speech bubbles and hints |
| Final Fantasy Tactics: The War of the Lions | Nothing yet |

This is partial speech, not a finished accessible game. A game becomes properly playable when someone writes a script for it; the tools here are for making that quicker.

## State of the work

- **Scripting platform:** speech, spoken menus, memory access, callbacks, hotkeys, audio cues and controller input are built.
- **Discovery layer:** text in a game's own font, text in a game's own letter code, numbers, the selected menu item, recordings for offline study, and a per-game profile that remembers what was learned are built. A spoken tool for exploring what was found is not.

Details and known weaknesses are in the [project report](Accessibility/docs/Project-Report.md).

## Building

Windows, Visual Studio 2022. The steps are in the [technical README](Accessibility/README.md#building). All of this fork's code is in the `Accessibility/` folder; PPSSPP's own files are changed in about 100 lines, each marked `[ACCESS]`.

## Game data

This repository contains no game data. Disc images, save states, saves and recordings are kept outside it and are blocked by `.gitignore`.

## Licence and credits

GPL 2.0 or later, the same as PPSSPP. PPSSPP is by Henrik Rydgård and many contributors; see [its README](README-PPSSPP.md). This fork also includes miniaudio and the NVDA Controller Client library.
