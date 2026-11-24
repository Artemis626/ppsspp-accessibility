# The Discovery harness

The harness measures the Discovery layer. Each **case** is a save state at a known screen, plus a file saying what that screen contains. The harness loads the state, lets the layer look, and reports what was found against what was expected.

## Where things live

| What | Where | In git? |
|---|---|---|
| Case files that need no game (PSP test programs) | `Accessibility/harness/cases/<GAMEID>/<case>.ini` | Yes |
| Case files for real games: they quote the game's own text, so they are kept out of the repository | `harness-data/cases/<GAMEID>/<case>.ini` | **No** |
| Save states | `harness-data/<GAMEID>/<state>.ppst`, beside the repository folder | **No** |
| Your ISO paths | `harness-data/games.ini` | **No** |
| Reports | `harness-data/results/` | **No** |
| Pictures used to write case files | `harness-data/screens/` | **No** |

Save states contain the game's memory, which is copyrighted game data. They and the ISOs never go into the repository.

## Running it

```bash
powershell -File Accessibility/tools/harness-run.ps1 -BuildDir ../builds/access-dev
```

- Add `-Game ULES01500` to run one game's cases.
- Add `-Case shop` to run only cases whose file name contains "shop".
- The report is written to `harness-data/results/latest-report.md`. It is Markdown with one heading per case, and a summary table at the top.
- `harness-data/results/history.csv` gets one line per case per run, so two runs can be compared.

The emulator window opens and closes once per case. Each case takes a few seconds.

## Adding a case

1. **Make the save state** with the fork's own build (`builds/access-dev`), because PPSSPP states are tied to the emulator version. Use PPSSPP's normal save-state key at the screen you want.
2. **Copy the state file** from `builds/access-dev/memstick/PSP/PPSSPP_STATE/` to `harness-data/<GAMEID>/` and give it a name that says what the screen is, for example `shop-buy-list.ppst`. If a file with the same name plus `.access` exists beside it, copy that too (see "Cold and warm" below).
3. **Write the case file.** Either write it yourself, or give Claude the state and one line describing the screen; Claude takes a picture of it with `harness-screenshot.ps1` and writes the file for you to check.

## The case file

```ini
[case]
game = ULES01500
state = shop-buy-list.ppst
description = Shop, buy list, cursor on the second item
settle_frames = 60
complete = no

[text.1]
text = Decoy Scroll

[text.2]
text = Goth
match = contains

[cursor.1]
items = Decoy Scroll | Demonpetal Scroll | Glass Pumpkin
index = 1

[value.1]
label = Goth
value = 12500
```

### The `[case]` section

- **`game`:** the game ID. The ISO is looked up under this ID in `harness-data/games.ini`.
- **`state`:** the save state's file name. Leave it out to measure the game as it is just after booting.
- **`program`:** instead of a game, a PSP program inside the repository (used by the built-in dialog case).
- **`description`:** one line for the report.
- **`settle_frames`:** how many frames to let the game run after loading, before looking. The default is 60, which is one second.
- **`complete`:** `yes` if the case lists **every** text on the screen. Only then can the harness count extra, unexpected text as an error ("precision").

### `[text.N]` sections

- **`text`:** what a sighted player reads. Spaces and line breaks are compared loosely: any run of them counts as one space.
- **`match`:** `exact` (the default) wants a found line to say exactly this. `contains` is satisfied when a found line includes it; use it for text that is typed out letter by letter, or a row of single letters.

### `[cursor.N]` and `[value.N]` sections

They are read and counted now, and scored from Phase D3 on. Writing them early is useful: they become the test for cursor and value discovery.

## Reading the report

- **Exact:** a found line says exactly the expected text.
- **Contained:** the words were found, but the layer split or joined them differently from the case file.
- **Near:** close, with a few wrong characters. It counts as not found.
- **Missing:** not found.
- **Recall:** the share of expected texts found (exact or contained).
- **Precision:** the share of found lines that were expected. Only given for `complete = yes` cases.
- **Source:** which technique found each text. This is how each technique's contribution is measured.

## Cold and warm

Some techniques learn from events as the game runs. A save state does not replay those events, so when a state is saved with this build, the layer writes what it knows to a file beside it: `<state>.ppst.access`.

- A **cold** run ignores that file. It shows what the layer gets from a state made by someone else.
- A **warm** run uses it. It is only run when the file exists.

## Which states are worth making

- Per game, six to ten screens: title menu, a dialogue box, the main menu, an item or shop list, a status screen with numbers, a battle with HP showing, the save or load screen.
- **Pairs are worth more than singles:** the same menu with the cursor one step lower; the same screen after a number changed. Phase D3 needs them.

## The picture helper

```bash
powershell -File Accessibility/tools/harness-screenshot.ps1 -BuildDir ../builds/access-dev -Game ULES01500 -State shop-buy-list.ppst
```

It loads the state, waits, and saves a picture of the window to `harness-data/screens/`. It is a separate script used only to write case files. Nothing in the Discovery layer itself reads pixels.
