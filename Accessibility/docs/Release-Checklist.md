# Release checklist

Go through this list before every release, and before anything else is published (a new public repository, a post, a shared build). Each line exists because of a real problem found before the first public release on 2026-11-24.

## 1. Safety

- [ ] **Who writes each input, and who runs it?** List every file the build reads that could come from someone else (scripts, profiles, hotkey files, recordings). Is each one handled as untrusted?
- [ ] **The sandbox test passes:** `powershell -ExecutionPolicy Bypass -File Accessibility/tools/sandbox-test.ps1 -BuildDir <build>`.
- [ ] **Nothing new escapes the sandbox.** Any Lua function added since the last release that touches files, processes or the network is listed in the README's "Script sandbox" section and checked by `tests/sandbox_selftest.lua`.
- [ ] **A security review was run** on the changes since the last release (`/security-review` in Claude Code, typed by the owner), and its findings were fixed or written down.

## 2. Copyright

- [ ] **No game data:** no disc images, save states, saves, recordings, memory dumps, screenshots or font pictures in the repository or the download.
- [ ] **No verbatim game text:** tests use invented lines; documents describe a game's lines rather than quoting them. Names and single menu words are allowed. Harness cases for real games stay in `harness-data/cases/`.
- [ ] **Search the new commits** for quoted dialogue: `git log -p <last release>..HEAD` and look at every quoted sentence.

## 3. Privacy

- [ ] **No personal details** in files or commits: real names, home-folder paths, e-mail addresses (commits use the GitHub noreply address).
- [ ] **The download holds nothing from the owner's own build folder:** no `memstick`, no `accessibility/logs`, no `accessibility/profiles`, no `hotkeys.ini`. Package into a new, empty folder.
- [ ] **No scripts are shipped.** The release contains no game scripts (the owner's decision); players add scripts themselves, and the user guide tells them how to judge one.

## 4. Quality

- [ ] **All tests pass:** the core tests (`tools/discovery-core-test.ps1`), the sandbox test, and the Phase 3 test (`tools/phase3-test.ps1`, which presses real keys: run it when the desktop is free).
- [ ] **The documents match the build:** the README's status table, the user guide, the project report.

## 5. The download

- [ ] **Licences included:** GPL 2.0 or later (PPSSPP and this fork), LGPL 2.1 (the NVDA Controller Client), miniaudio's licence, and a line saying where the source code is.
- [ ] **The release note says:** an AI experiment; Windows and NVDA only; bring your own games; no scripts included, and how to judge a script; Windows may warn because the program is not signed.
