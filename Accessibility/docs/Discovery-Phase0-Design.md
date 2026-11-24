# Discovery layer: Phase D0 design

> **Status (10 November 2026):** approved, and Phase D1 is built. This document is the design as proposed and stays as written, apart from this note and the "Your answers" section at the end. For what D1 built and measured, see [Discovery-D1-Results.md](Discovery-D1-Results.md).
>
> Differences found while building D1:
>
> - The upstream hooks came to 41 lines, not the "about ten" of section 14.
> - Neither Tactics Ogre nor Final Fantasy Tactics uses the system font library, so for them the work is in Phase D2.

This is the design for a system-level layer that finds a game's text and data automatically, so that an accessibility modder works with named things ("menu cursor", "dialogue text") instead of hunting raw addresses.

## 1. Names used in this document

- **Discovery layer:** my working name for the whole thing. Change it if you prefer another.
- **Phases D0 to D4:** the phases you listed. I prefix them with "D" because the fork already has its own "Phase 4" and "Phase 5" (see `README.md`), and two different "Phase 4"s would cause mistakes. The old Phase 4 stays paused, as `CLAUDE.md` says.
- **Core:** the platform-independent part. It knows nothing about PSP or PPSSPP.
- **Adapter:** the PPSSPP-specific part. It feeds the core with events and carries out the core's requests.
- **Tap:** a place where the adapter listens to the emulated machine (a system call, a draw command, a memory write).
- **Glyph:** one drawn character image. A **glyph id** identifies *which image* was drawn (by where it lives), without looking at what it looks like.
- **Game map:** the per-game file of everything found, with names and confidence.

## 2. Summary of the proposal

### What I propose

1. A **core library with no PPSSPP includes at all**, enforced by the build, plus a thin **PPSSPP adapter**.
2. **Three families of taps:** platform services (PSP system libraries), the graphics command stream, and the machine itself (memory, execution, save states, input).
3. **Two operating modes:**
   - **Learn mode** uses heavy instrumentation, may run slower than real time, and writes the game map.
   - **Play mode** uses the map with a handful of cheap hooks at full speed.
4. **One central technique for game-independent text**, which I call the *glyph cipher* (section 6.4): treat the sequence of glyph ids on screen as a substitution cipher and search RAM for the buffer that matches its pattern. This finds the text buffer and the game's character table together, with no pixel ever inspected, and it is the technique that carries best to older consoles.
5. **The screen as an oracle for data discovery:** numbers shown on screen are matched against memory to find values such as HP and money; controlled input with save-state rollback finds cursors and lists (section 7).
6. **A harness that measures every technique separately**, so we know which avenue earns its keep.

### Honest expectations

I want to set these before you review, because this is a research problem, not only an engineering one.

- **Solid engineering, high confidence it works:** system dialogs, the system font library path, the on-screen keyboard, the draw-stream tap, the harness, the map format, the Lua API.
- **Should work, needs measuring:** the glyph cipher, number-to-memory matching, cursor discovery by input probing.
- **Genuinely uncertain:** fully automatic structure inference (records, arrays), custom character tables with no anchor, text that the game renders with its own CPU code into a texture.
- **Impossible under your constraint, and I will say so rather than cheat:** text that exists only as a pre-drawn picture (a logo, a menu button painted by an artist). No data path leads to characters there. Section 9 proposes human labelling for those.

The harness exists so that these three bands are replaced by numbers.

## 3. What I found in the PPSSPP source

Everything in this section was read in the v1.20.4 source of this repository. File and line references are given so they can be checked.

### 3.1 Platform services (text arrives as real characters)

| Finding | Where | Why it matters |
|---|---|---|
| `sceFontGetCharGlyphImage(font, charCode, glyphImagePtr)` receives a UCS-2 character code and draws it into a game buffer | `Core/HLE/sceFont.cpp:1425`, `Core/Font/PGF.cpp:552`, struct `GlyphImage` at `Core/Font/PGF.h:119` | We learn "character X now lives at pixel rectangle R of buffer B" as data, not by looking |
| `sceFontGetCharInfo` is called per character for layout | `Core/HLE/sceFont.cpp:1333` | Character codes are visible even when the glyph image is already cached |
| Games that ship their own font module still go through PPSSPP's version | `Core/HLE/HLE.cpp:120` lists `sceFont_Library` / `sceLibFont` among modules replaced by HLE | The font tap is not bypassed by a bundled `libfont.prx` |
| The character-conversion library is emulated at high level (UTF-8, UTF-16, Shift-JIS conversions and string lengths) | `Core/HLE/sceCcc.cpp:94` onwards | Whole strings pass through with a declared encoding |
| Every system dialog draws its text through two functions | `PPGeDrawText` at `Core/Util/PPGeDraw.cpp:1061`, `PPGeDrawTextWrapped` at `:1108` | Two hook lines cover message dialogs, save and load dialogs, the keyboard and network dialogs |
| The on-screen keyboard holds its prompt, initial text and result as UTF-16 | `Core/Dialog/PSPOskDialog.cpp:251`, `Core/Dialog/PSPOskDialog.h:127` | Known text entering the game: useful as an anchor later |
| Every system call from game code passes through two wrapper functions | `CallSyscallWithFlags` and `CallSyscallWithoutFlags`, `Core/HLE/HLE.cpp:838` and `:878` | **One generic tap** covers sceFont, sceCcc, sceUtility, sceIo, sceGe and the rest, instead of one hook per library |

### 3.2 Memory and provenance

| Finding | Where | Why it matters |
|---|---|---|
| Memory checks work in every CPU backend, and system-library writes report through `NotifyMemInfo` | `Core/Debugger/MemBlockInfo.cpp:484` | The existing callback mechanism already sees both CPU and system writes |
| File reads into RAM are tagged with the file name | `"IoRead/" + filename` at `Core/HLE/sceIo.cpp:1079` | A buffer can be traced back to the disc file it came from |
| Copies report their source address | `NotifyMemInfoCopy`, `Core/Debugger/MemBlockInfo.cpp:541` | Copy chains (file to buffer to buffer) can be followed |
| Save states can be taken and restored in memory | `SaveState::SaveToRam` / `LoadFromRam`, `Core/SaveState.h:89` | Experiments with rollback: try an input, observe, undo |
| A state can be loaded from the command line | `--state=` at `UI/NativeApp.cpp:578` | The harness needs no UI automation to load states |

### 3.3 Code and execution

| Finding | Where | Why it matters |
|---|---|---|
| PPSSPP finds function boundaries and recognises known library functions by hash, including `strlen`, `strcpy`, `strcat`, `strncpy`, `wcslen`, `wcscpy`, `memcpy`, `_vfprintf_r` | `Core/MIPS/MIPSAnalyst.cpp:99` to `:471`, `ScanForFunctions` in `MIPSAnalyst.h:107` | We get a function list for free, and string functions are already named in many games |
| The plain interpreter has a per-instruction loop used when breakpoints exist | `RunUntilWithChecks`, `Core/MIPS/MIPSTables.cpp:993` | One hook there gives a complete execution trace for learn mode |
| An upstream tracer already records executed blocks in the IR backend | `Core/MIPS/MIPSTracer.h`, `Core/MIPS/IR/IRFrontend.cpp:304` | Cheaper block-level coverage is possible later |
| A call-stack walker exists | `Core/MIPS/MIPSStackWalk.h:35` | "Which code asked for this" can include the callers |
| Execute breakpoints are stored in a vector searched linearly | `BreakpointManager::FindBreakpoint`, `Core/Debugger/Breakpoints.cpp:77` | Fine for dozens of hooks in play mode; unsuitable for hooking thousands of functions, so learn mode must use the interpreter trace instead |

### 3.4 Graphics

| Finding | Where | Why it matters |
|---|---|---|
| Display lists run synchronously from the game's own sceGe calls | `Core/HLE/sceGe.cpp:149` and following | Draw commands arrive on the emulation thread, the same thread as Lua. No new threading problem |
| The GE interpreter has a slow loop that visits every command, already used by the frame recorder | `GPUCommon::SlowRunLoop`, `GPU/GPUCommon.cpp:627`; chosen at `:791` | Two lines give us every draw: one to request the slow loop, one notification beside the recorder's |
| In the fast loop, consecutive draws are merged | `GPU/GPUCommonHW.cpp:1091` | Confirms we must use the slow loop to see each glyph quad separately |
| A helper already converts the current draw to screen-space vertices | `GetCurrentDrawAsDebugVertices`, `GPU/Common/SoftwareTransformCommon.cpp:1063` | Text drawn with 3D transforms can still be placed on screen |
| A frame recorder writes replayable GE dumps | `GPU/Debugger/Record.cpp` | Recorded frames can become unit tests for the draw analysis, without the game |

### 3.5 Things specific to your test games

- **Persona 2:** PPSSPP contains a game-specific hook, `persona2_download_frame` (`Core/HLE/ReplaceTables.cpp:1285` and `:1703`, hash at `Core/MIPS/MIPSAnalyst.cpp:516`). It exists because the game reads the screen image back into RAM. That tells us the game does some of its graphics work on the CPU. It does **not** tell us how it draws text; that is the first thing the survey in D1 will establish.
- **Jeanne d'Arc (US):** only a `memcpy` hash is listed (`MIPSAnalyst.cpp:352`). Nothing about text.
- **Your games folder today** contains Tactics Ogre, a Persona 2 *Eternal Punishment* (Japan) CHD, and an ISO with ID ULES-00850. Innocent Sin and Jeanne d'Arc are not there yet (question in section 16).

### 3.6 Ground truth we already have

- **`pspautotests`** ships small PSP programs **with source code** for the font library (`pspautotests/tests/font/`) and the message and save dialogs (`pspautotests/tests/utility/`). Because we can read their source, we know exactly what they display. They are redistributable, so they can be automated tests in the repository, as `displayWait.prx` already is.
- **The CWCheat database** you already use is ground truth for values. Example: Goth in Tactics Ogre is at `0x08AD8438`. If value discovery is any good, it must find that address by itself.

### 3.7 Prior art

The visual-novel text-hooking community (Textractor and its predecessors) reads text from PPSSPP by hooking the same sceFont and sceCcc functions, and has a "search for hooks" feature that tests every function for string-like arguments. I am recalling this from memory and did not re-verify it today. It supports two of the avenues below (6.2 and 6.6), but those tools stop at dialogue text; they do not link text to draws, discover data, or build a map.

## 4. Architecture

### 4.1 Layers

```text
Lua scripts            Built-in spoken explorer (hotkeys)
      |                         |
      +----------- Lua binding: text.*, map.*, discover.* 
                                |
+----------------------- Discovery core -------------------------+
|  Observation store      everything seen, with provenance       |
|  Text:      glyph ledger, run assembler, encoding detector,    |
|             charset learner (glyph cipher)                     |
|  Linking:   buffer <-> code <-> file provenance                |
|  Probing:   snapshot differ, input prober, coverage differ     |
|  Structure: pointer graph, arrays and records, string tables   |
|  Game map store, confidence engine                             |
+------------------------ Machine interface ----------------------+
                                |
+----------------------- PPSSPP adapter -------------------------+
|  Syscall tap, dialog tap, GE draw tap, CPU trace tap,          |
|  memory checks, save states, joypad, module and heap info      |
+----------------------------------------------------------------+
```

### 4.2 Decision: the core is a separate build project

The core lives in `Accessibility/Discovery/Core/` and is compiled as its own static library whose include path does **not** contain the PPSSPP source tree.

**Why:** you intend to move this to other emulators. "Platform-independent" is easy to claim and easy to erode one `#include` at a time. If the core cannot see PPSSPP headers, it cannot depend on them, and the compiler enforces the boundary for us.

### 4.3 Decision: observations first, conclusions second

Taps produce **observations** (facts with a frame number and provenance: "character 0x0041 was rendered into buffer B", "address A changed from 2 to 3 after Down was pressed"). Analyses turn observations into **claims** ("A is a menu cursor"), each with a list of the observations that support it.

**Why:**

- Confidence (your goal 4) needs evidence to count. A claim with no recorded evidence can only have a made-up number.
- New analyses can be added later and run over the same observations.
- The harness can report *which technique* found each thing.

### 4.4 Learn mode and play mode

| | Learn mode | Play mode |
|---|---|---|
| Purpose | Discover, fill the game map | Use the map while playing |
| CPU backend | Interpreter with full trace when needed | JIT, full speed |
| Taps | All, including wide memory tracing | System-library taps, draw tap, and only the hooks the map names |
| Who uses it | The modder, and the harness | The player |

**Why two modes:** the complete trace needs the interpreter, which is several times slower than the JIT. A player should not pay that. A modder analysing a menu for ten seconds can.

### 4.5 Threads

All taps I plan fire on the emulation thread, which is where Lua already runs. File reads from PPSSPP's I/O thread are the known exception, and the existing queue in `Api_Event.cpp` already handles that case.

## 5. The machine interface (what an adapter provides)

The interface is organised in tiers so that an emulator with fewer abilities still gets the analyses its tiers allow.

| Tier | The adapter provides | Enables |
|---|---|---|
| 0 | Memory regions and reads; frame boundary; game identity; input injection; in-memory save and restore | Snapshot diffing, input probing, RAM text scans, the map and Lua API |
| 1 | Memory read and write events with the program counter; execute hooks; registers | Provenance (which code wrote this), map hooks, value tracking |
| 2 | A normalised **draw stream**: for each drawn image, an image id (where the source pixels live, and which rectangle), the screen rectangle, draw order, and the address of the vertex or tile data | Glyph runs, the glyph cipher, cursor and highlight detection |
| 3 | Deep trace: calls and returns with argument registers, block or instruction execution; call stack | Hook search, coverage diffing, access-pattern structure inference |
| 4 (optional) | Platform services: characters from a system font, system dialogs and keyboard, file-read provenance, heap blocks, function boundaries | The reliable shortcuts of D1 |

The adapter also supplies a small **architecture description** (pointer size, byte order, argument registers), so the core never decodes instructions itself.

### What carries over to another emulator

- **Carries over unchanged:** everything in the core. That is the text run assembler, glyph cipher, encoding detection, number matching, input prober, snapshot and coverage differs, structure inference, map store, confidence engine, harness logic and the Lua binding.
- **Must be rewritten per emulator:** the taps. For PPSSPP that is roughly the system-call tap, the dialog tap, the GE tap, the trace tap and glue to breakpoints and save states.
- **Tier 4 mostly disappears on older systems,** as you said. Tiers 0 to 3 do not, and the design leans on them for exactly that reason.
- **Tier 2 gets easier on tile-based consoles.** On a SNES, Game Boy or Mega Drive the "draw stream" for text is the tilemap: a grid in video memory where each cell holds a tile number. That *is* a sequence of glyph ids on a fixed grid, with no vertex decoding. The glyph cipher has less work to do there than on PSP.
- **A minimal adapter is useful by itself.** Many emulators expose memory, save states and input but no draw stream. With Tier 0 and 1 alone, input probing and snapshot diffing still find cursors and values, with a human saying what is on screen.

## 6. Text capture: the avenues

For each avenue: what it is, how reliable I expect it to be, and what it costs.

### 6.1 System dialogs and the on-screen keyboard

- **How:** hook `PPGeDrawText` and `PPGeDrawTextWrapped`; read dialog state (which save slot is selected, which button) from the dialog objects.
- **Reliability:** very high. The text is already UTF-8 inside PPSSPP.
- **Reach:** narrow but valuable. On PSP, save and load screens are usually the system's dialog, in almost every game. This avenue alone makes saving and loading speak everywhere.
- **Portability:** none (Tier 4).
- **Phase:** D1.

### 6.2 The system font library, joined to the draw stream

- **How:**
  1. The system-call tap sees each `sceFontGetCharGlyphImage` call: character code, destination buffer, position in the buffer. The adapter records this in a **glyph ledger**: "rectangle R of buffer B holds character X".
  2. The GE tap sees each textured draw: which texture address, which rectangle of it, where on screen.
  3. The core joins the two: a draw that samples rectangle R of buffer B is character X at that screen position. The run assembler groups characters into lines by position.
- **Provenance:** the call stack at the font call names the game's text routine. The assembled string is then searched in RAM (now with known characters, so an exact search under a few encodings), which names the buffer.
- **Reliability:** high *in games that use the system font library*. I believe that is a large share of Japanese-developed PSP games, but I have not verified it for any of your test games. The D1 survey measures it in the first hour.
- **Known weak point:** a game may render each glyph into a scratch buffer and then copy it into its texture with its own code, which breaks the join in step 3. The design degrades in steps: follow the copy when PPSSPP reports it; otherwise fall back to the characters in call order with code provenance but without screen positions. Text is kept; layout is lost.
- **Save-state weak point:** see 6.8.
- **Portability:** none for the font tap; the run assembler and the join are core code reused by 6.4.
- **Phase:** D1.

### 6.3 Other system-call riders

Cheap extras from the same tap, all in D1: sceCcc conversions (whole strings with a declared encoding), save-data titles and details passed to the save utility, and text printed to the debug output. Each is a few lines once the generic tap exists. They mostly serve as **anchors** (known text) for 6.4.

### 6.4 The glyph cipher (the main game-independent technique)

This is my main addition to your list of avenues.

- **The situation:** a game with its own bitmap font draws a row of textured quads. Each quad samples one cell of a font texture. We know *which* cell (the glyph id) and where it lands on screen. We do not know which character the cell is, and we may not look.
- **The idea:** a displayed line is a sequence of glyph ids, for example `7 2 9 9 4`. Somewhere in RAM is the string the game drew it from, as a sequence of codes with the **same repetition pattern**: the third and fourth codes are equal, the others differ. Search RAM for sequences with that pattern, using code sizes of one and two bytes, and for known variable-width encodings (Shift-JIS, UTF-8) after splitting memory by their lead-byte rules.
- **What a match gives us, all at once:**
  1. The **text buffer address** (your "which memory buffer").
  2. A table from **code to glyph id** for every character seen so far: the game's character table, expressed in glyph ids.
  3. A test for standard encodings: if the matched codes decode as sensible ASCII, Shift-JIS or UTF-16, the table from code to character is the standard one and the text is readable immediately.
- **Why it is reliable:** a ten-character line with a couple of repeated letters has a pattern that almost no random memory matches. Each further line that matches *with the same code-to-glyph table* multiplies the confidence. Short or repetition-free runs are simply not used as evidence.
- **Cost:** a pattern search over 24 to 64 MB. To keep it fast, the search starts in memory written recently, and only falls back to all of RAM.
- **When the encoding is custom** (codes are not a standard encoding), we have codes and glyph ids but still need characters. Without looking at pixels, the sources of characters are, in order of preference:
  1. **Known text:** a name typed on the system keyboard, save titles, text from 6.2 or 6.3 in the same game, or the expected strings you confirm in harness files.
  2. **Numbers:** section 7.1 learns the ten digits from values in memory.
  3. **Atlas order:** font cells are usually stored in code order. If cell positions step in the same order as a standard encoding, the table follows. This uses a cell's *position*, never its picture.
  4. **Community tables:** ROM-hacking `.tbl` files exist for many games, and `.tbl` support is already on the fork's to-do list.
  5. **Symbolic solving:** letter-frequency and dictionary solving of the substitution, for alphabetic languages. Always low confidence until a person confirms.
  6. **A person fills in the rest.** The layer says plainly: "text found, 41 distinct symbols, 9 unmapped".
- **Portability:** complete. It needs Tier 0 plus Tier 2.
- **Phase:** D2.

### 6.5 RAM and file string scanning with encoding heuristics

- **How:** scan memory and disc files for byte runs that decode as plausible text under ASCII, Shift-JIS, UTF-8, UTF-16, or a table learned in 6.4.
- **My assessment:** useful as a *supporting* technique, weak as a primary one. A scan finds thousands of strings and cannot say which are on screen. Its jobs are to propose candidates for other analyses, to find string tables (section 7.4), and to locate the whole script on disc once a table is known.
- **Phase:** D2.

### 6.6 String functions and hook search

- **Known string functions:** PPSSPP already labels `strcpy`, `strlen`, `sprintf`-family and wide-string functions in many games by hash. Hooking the labelled ones captures strings as they move, with the caller's address.
- **Hook search:** in learn mode, with the interpreter trace, examine the arguments of every function call. A function whose argument repeatedly points at text that then appears on screen is a text routine. Rank candidates by how well their strings match what 6.2 or 6.4 saw on screen.
- **The result is the best kind of map entry:** "the dialogue function is at address F, the string pointer is in register a1". A hook on F delivers each line the moment the game asks for it, which is the "decoded source" feeling you described.
- **Portability:** complete (Tier 3).
- **Phase:** D2.

### 6.7 Text rendered by the game's CPU code

- **The case:** the game draws glyphs with its own code into a RAM texture or frame buffer, then shows that as one big image. The draw stream then shows one quad, not one per glyph. Persona 2 *might* be such a game; older consoles' bitmap modes certainly are.
- **How:** watch writes to memory that is later used as a texture. The code doing the writing is the glyph blitter; the loop that calls it once per character is the text routine; its inputs are the character codes. The calls then replace the quads as the "glyph sequence" for 6.4.
- **My assessment:** the hardest avenue. I will only build it in D2 if the survey shows a test game needs it, and I would rather have the harness numbers for 6.4 first.
- **Portability:** complete (Tier 1 and 3).

### 6.8 Decision: a sidecar file for save states

- **The problem:** techniques 6.2 and 6.6 learn from *events*. A save state made at a static screen restores memory, but the events (the glyph renders, the string copies) happened before it was saved and are not replayed. Loading the state gives the layer amnesia.
- **Decision:** when a state is saved by our build, the layer writes its own knowledge (the glyph ledger, live provenance) to a sidecar file beside it and reads it back on load.
- **Why not put it inside the state file:** that changes PPSSPP's save-state format, which is exactly the kind of upstream divergence this fork avoids.
- **Consequence for the harness:** every case is measured **cold** (state only) and, when a sidecar exists, **warm**. Cold results tell us what a player gets from someone else's save state; draw-stream techniques (6.4) are immune to the problem, because the game redraws every frame.

### 6.9 Avenues I considered and am not proposing now

- **Full taint tracking** (following every byte through every instruction from string to pixel). It is the most general way to link a draw to its source. It is also a large build, covering every MIPS and vector instruction with shadow memory. The glyph cipher and access patterns should give most of the benefit for a fraction of the cost. I would revisit it only if the harness shows a gap that nothing else closes.
- **Decompilation or static analysis of the game binary.** Out of scope; dynamic observation is cheaper and survives compressed and encrypted executables.

## 7. Discovering non-text data

### 7.1 Numbers on screen matched to memory

- **How:** when the screen shows a run of digits (known from 6.2, or learned in 6.4), search memory for that number as an 8, 16 or 32-bit integer, as decimal digits, and as a float. One screen leaves many candidates. Each time the number changes, candidates that did not change to the new value are dropped. This is the cheat-search method, with the screen answering instead of a person.
- **What it gives:** the address of HP, money, counts, with the label text beside the number on screen proposed as the name ("HP", "Goth").
- **It also runs backwards:** if a value's address is known (from CWCheat, say), the glyphs displaying it must be its digits. That teaches the digit glyphs to 6.4 with no human.
- **Phase:** D3.

### 7.2 Cursors and lists by input probing with rollback

- **How**, in learn mode, on a menu screen:
  1. Save a snapshot in memory.
  2. Run some frames with no input. Everything that changes is **noise** (timers, animation) and is excluded.
  3. Restore. Press Down for one frame, let it settle, record what changed in memory and on screen.
  4. Restore. Do the same for Up, Left, Right.
  5. Repeat Down several times without restoring, to walk the list.
- **What marks a cursor:** a small integer that goes up by a fixed step on Down and down on Up, stays in a small range, and wraps.
- **Joining to the screen:** on each step, the draw stream shows which text run changed colour or which cursor sprite moved beside which run. That ties index 0, 1, 2 to the item texts, which yields the **menu structure**: an ordered list of items plus the variable that selects among them.
- **Safety:** the snapshot is restored at the end, so the game is exactly where it was. Probing is started deliberately by the modder (a hotkey or a Lua call), never silently.
- **Phase:** D3.

### 7.3 Coverage diffing

- **How:** record which functions execute in two situations (dialogue open or closed; Down pressed or not) and subtract.
- **What it gives:** the handler functions, and corroboration for 7.2: a cursor candidate *written by the function that only runs on Down* is far more believable than one that merely changed.
- **Phase:** D3.

### 7.4 Structure inference

- **From one anchor to a table:** once one HP address is known, look at regular distances before and after it for memory with the same field layout. That finds the other party members' records and the record size.
- **From access patterns:** in the trace, one instruction reading addresses in a regular progression is walking an array; several instructions reading the same progression at different offsets are reading fields of a record.
- **Pointer graph:** values that are valid addresses into allocated memory are pointers. An array of pointers to strings is a string table.
- **My assessment:** anchor-based inference is realistic. Fully unsupervised "find all structures" produces a lot of low-value output; I would only surface structures connected to something already found on screen.
- **Phase:** D3.

## 8. Confidence

- **Each claim's confidence is computed from its evidence**, not assigned by feel. Independent kinds of evidence are combined so that agreement raises confidence quickly and a single weak signal cannot.
  - Example for a cursor: "changes with Down and Up" is one signal; "written by a Down-only function" is a second; "its value matches the highlighted row in all 6 probes" is a third.
- **Calibration comes from the harness.** For each technique, the harness measures how often its claims were right on your labelled states. Those measured rates are what the numbers are built from. The aim is that "0.9" means "right about nine times in ten", and the harness report shows whether that holds.
- **Presentation:** a number from 0 to 1, a word (high, medium, low), and the reasons in plain language. Speech uses the word; the map file stores all three.
- **Human decisions override:** a confirmed entry is locked. A rejected entry is kept as a rejection, so the layer does not propose it again.

## 9. The no-OCR constraint: where exactly the line is

### The rule I will build to

**The product never reads pixel or texel values to decide what a character or image is.** Images are identified only by *where they are* (memory address and rectangle).

### What that rules out, including tempting shortcuts

- Reading the frame buffer or textures to recognise text. Obviously.
- **Re-rendering system-font glyphs and comparing them with a game's texture** to recover which characters a cached atlas holds. The comparison would be exact, not fuzzy, but it is still matching rendered glyph shapes, so I treat it as forbidden. The sidecar file (6.8) solves the same problem honestly.

### One grey area that needs your ruling

- **Content hashes as identity keys.** A picture-only menu button can be labelled by a person once ("this image is 'New Game'"). To recognise *the same image* next session, when it may load at a different address, the layer would need a key that survives relocation. A hash of the image's bytes is such a key. It recognises nothing: it only says "these are the same bytes a person already labelled".
- **My recommendation:** allow hashes only as keys for human-given labels, never to derive characters. Until you rule on it, the design uses no pixel data of any kind, and image labels are keyed by file provenance (which disc file and offset the image was loaded from), which is weaker but clean.

### Screenshots

Used only by the harness tooling, by me, to write expected-result files. The screenshot helper is a separate script and is never linked into the Accessibility library.

## 10. The game map

### Decision: two files per game

1. **`maps/<GAMEID>.map.ini`** is for people. One section per entry, one fact per line, comments allowed.
2. **`maps/<GAMEID>.evidence.json`** is for the machine. It holds the bulky evidence behind each entry and is regenerated freely.

**Why INI for the human file:**

- It reads well with a screen reader: one `key = value` per line, no nested brackets to keep track of.
- PPSSPP already has an INI reader and writer, and the fork already uses `hotkeys.ini`, so there is no new dependency and no new format for modders to learn.
- It diffs cleanly in git, so modders can share and review maps.

**Why the evidence is separate:** it is large, nested and uninteresting to edit. Mixing it in would bury the twenty lines a person cares about.

### Sketch of the human file

```ini
[game]
id = ULES01500
version = 1.00
map_format = 1

[menu.cursor]
id = e0017
kind = cursor
type = u8
at = 0x08A1234C
range = 0..5
list = menu.items
confidence = 0.86 high
status = auto
found_by = input_probe, highlight_match, coverage
note =

[dialogue.text]
id = e0003
kind = text_hook
at = exec 0x0881F2A0
string = register a1
encoding = table main
confidence = 0.93 high
status = confirmed
```

- **Renaming** is changing the section name. The `id` line is the stable identity, so evidence and scripts survive a rename.
- **`status`** is `auto`, `confirmed` or `rejected`.
- **`kind`** covers at least: `text_hook`, `text_buffer`, `value`, `cursor`, `list`, `record_array`, `string_table`, `charset`, `image_label`.

### Where an entry points (`at`)

| Form | Example | Used when |
|---|---|---|
| Fixed address | `0x08AD8438` | Global variables |
| Pointer path | `[0x08A00010] + 0x34` | Objects on the heap, which move |
| Code hook | `exec 0x0881F2A0`, value in a register | Text routines; the most robust form within one game version |

**Other regions and versions of a game** have the same code at different addresses. PPSSPP's function hashing could relocate code hooks across versions. I note it as a later extension, not part of D0 to D4.

## 11. Lua API (proposal)

It follows the existing API's style: lowercase tables, BizHawk-like names, `nil` on failure.

### Text

- `text.onscreen()` returns an array of runs. Each run has `text`, `x`, `y`, `w`, `h`, `confidence`, `source` and a stable `id`.
- `event.ontext(fn [, filter])` calls `fn(run, change)` when a run appears, changes or disappears. `filter` can restrict by screen region or by map name.
- `text.sources()` lists discovered text buffers and hooks.
- `text.unmapped()` lists symbols seen but not yet mapped to characters.

### The map

- `map.get(name)` returns an entry object, or `nil`.
  - `entry:read()` returns its current value (a number, a string, or a table for lists and records).
  - `entry:onchange(fn)` calls `fn(new, old)`.
  - `entry.confidence`, `entry.status`, `entry.kind`.
- `map.list([filter])`, `map.rename(old, new)`, `map.confirm(name)`, `map.reject(name)`, `map.save()`.

### Discovery (learn mode)

- `discover.survey()` reports which taps this game triggers.
- `discover.menu()` runs the input probe of 7.2 on the current screen.
- `discover.number([value])` runs 7.1; without an argument it uses the numbers on screen.
- `discover.status()` describes what is running and what was found.

### What a mod could look like when this works

```lua
local cursor = map.get("menu.cursor")
local items  = map.get("menu.items")
cursor:onchange(function(i) speak(items:read()[i + 1]) end)
event.ontext(function(run) speak(run.text) end, { source = "dialogue.text" })
```

### Without writing Lua

A built-in spoken explorer on hotkeys: read all text on screen, move through runs, start a probe, hear the findings, confirm, reject and name them. A blind modder should be able to build a first map with no script and no sighted help. Its design belongs to D3 and D4; I mention it now because it shapes the core's interfaces.

## 12. The harness

### Layout

- **In the repository:** `Accessibility/harness/` holds the runner script, the format description, and each case's expected-results file.
- **Outside the repository:** `../harness-data/<GAMEID>/` holds the save states, and a local settings file holds your ISO paths.

**Why the states stay out of git:** a save state contains the game's memory, which is copyrighted code and data. Keeping it out of the repository means nothing has to be scrubbed if you ever make the repository public.

### One case

```ini
[case]
game = ULES01500
state = shop-buy-list.ppst
description = Shop, buy list, cursor on the second item
settle_frames = 30
complete = yes        ; all visible text is listed, so extra text counts as an error

[text.1]
text = Decoy Scroll
match = exact

[text.2]
text = 12500
label = Goth
match = exact

[cursor.1]
items = Decoy Scroll | Demonpetal Scroll | Glass Pumpkin
index = 1

[value.1]
label = Goth
value = 12500
```

### How a run works

1. The runner starts the build with the ISO, `--state=`, and a harness option naming the case.
2. The layer lets the game settle, performs the case's steps (nothing; scripted input; or a probe), writes a result file, and the build exits.
3. The runner compares found with expected and writes a report.

### What is measured

- **Text:** how much expected text was found (recall), how much found text was wrong or extra (precision), and character error rate for near misses.
- **Provenance:** whether a buffer and a code location were named for each text.
- **Cursors:** whether a variable was found whose value matches the expected index. With paired states (same screen, cursor one lower), whether it tracks.
- **Values:** whether the found address reads the expected value in every state of the pair or series.
- **Per technique:** every found item names what found it, so the report shows each avenue's contribution.
- **Cold and warm**, as explained in 6.8.

### The report

- A Markdown file with one heading per game and per case, so you can move through it with NVDA, and a summary table at the top.
- A history file with one line per run, so progress between runs is a comparison of numbers.

### Automatic ground truth

In a game that uses the system font, 6.2 gives the true characters. The harness can then run the glyph cipher (6.4) *blind* on the same game and score it against 6.2 with no human labels. This lets the game-independent technique be tested on thousands of lines, not just the handful of states either of us can label.

### How expected files get written

1. You give me a state and one line saying what the screen is.
2. A harness script loads it and saves a screenshot. I read the screenshot and write the expected file.
3. You review the expected file by ear. It is plain text.

### What I need from you for the harness

- States made with the fork's own build (`builds/access-dev`), because PPSSPP states are tied to the emulator version.
- Per game, ideally six to ten screens: title menu, a dialogue box, the main menu, an item or shop list, a status screen with numbers, a battle with HP showing, the save or load dialog, and name entry if the game has it.
- **Pairs** are worth more than singles: the same menu with the cursor one step lower; the same screen after a number changed.

## 13. Test games

- **Persona 2 and Jeanne d'Arc**, as you chose.
- **Tactics Ogre (ULES01500):** already set up, text-heavy menus and many numbers, and CWCheat addresses as ground truth for D3.
- **`pspautotests` font and dialog programs:** tiny, with source, redistributable. These are the automated regression tests.
- **My request for the remaining slots:** choose by *how the game draws text*, which the D1 survey reveals, not by title. The set should contain at least:
  1. one game that uses the system font library,
  2. one game with its own bitmap font and standard encoding,
  3. one game with a custom character table,
  4. one Japanese-language game (two-byte characters),
  5. if one exists among your games, one that renders text on the CPU.
- **Process:** in D1 I build the survey first. You run it on whatever games you own (it takes a minute each and speaks its result), and we assign games to the five slots from real data.

## 14. Planned changes to upstream files

Each is one call into `Accessibility/`, marked `// [ACCESS]`, and does nothing when the layer is off.

| File | Change | Lines |
|---|---|---|
| `Core/HLE/HLE.cpp` | Before and after the system-call body in the two wrapper functions | 4 |
| `Core/Util/PPGeDraw.cpp` | In `PPGeDrawText` and `PPGeDrawTextWrapped` | 2 |
| `GPU/GPUCommon.cpp` | Request the slow loop; notify per command | 2 |
| `Core/MIPS/MIPSTables.cpp` | Learn-mode trace in the per-instruction loop | 2 |
| `Core/SaveState.cpp` | Possibly one notification for the sidecar, if polling proves insufficient | 0 or 1 |

About ten lines in total, on top of the roughly sixty the fork has now. Function lists, stack walks, memory checks, save states and input need no new edits.

## 15. Phases and what each delivers

### D1: harness, plus the most reliable text capture

- Core skeleton and machine interface; the build rule that keeps the core independent.
- System-call tap, dialog tap, GE draw tap.
- The survey (`discover.survey()` and a hotkey).
- Avenues 6.1, 6.2, 6.3. Run assembler. Glyph ledger and its sidecar.
- Harness runner, case format, report, screenshot helper.
- Automated tests on the `pspautotests` programs.
- **You test:** the survey on your games; hearing dialog and font-library text; the first harness report.

### D2: broader text capture, measured

- Avenue 6.4 (glyph cipher) with the encoding detector and charset learner.
- Avenues 6.5 and 6.6; avenue 6.7 only if the survey demands it.
- Blind scoring of 6.4 against 6.2.
- **You test:** games with their own fonts; the unmapped-symbol workflow.

### D3: non-text data and the game map

- Sections 7.1 to 7.4. The map files. The confidence engine with calibration from the harness.
- First version of the spoken explorer.
- **You test:** probing menus in Tactics Ogre; whether Goth is found unaided.

### D4: Lua integration, confidence reporting, documentation

- The `text.*`, `map.*` and `discover.*` API; play mode driven by the map.
- Modder documentation, including a section for authors of adapters for other emulators.

## 16. Questions for you

1. **The grey area in section 9:** may the layer use a hash of an image's bytes purely as a key for labels a person has given? My recommendation is yes, for that purpose only.
2. **Persona 2:** your folder has *Eternal Punishment* (Japan) as a CHD, not *Innocent Sin*. Which do you want as the test game, or both? A Japanese-language game is valuable for the two-byte slot either way.
3. **Languages:** which languages matter first for your players: English, Italian, Japanese? It sets the order in which encodings and dictionaries are built.
4. **Harness data outside git** (section 12): do you agree?
5. **Names:** are "Discovery layer" and "D0 to D4" acceptable?

## 17. Your answers (10 November 2026)

- **Test games:** set Persona 2 aside for now; use Tactics Ogre and Final Fantasy Tactics: The War of the Lions, both in the games folder. Persona 2 comes later.
- **Harness data outside git:** yes. Save states and other game assets stay out of the repository.
- **Go-ahead:** start D1.
- **Not answered yet:** question 1 (image hashes as keys for human labels) and question 3 (languages). Until question 1 is answered, the layer uses no pixel data of any kind. The names in question 5 were not objected to and are in use.
