// Copyright (c) 2026- PPSSPP Accessibility Scripting contributors.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Tests for the Discovery core, built and run WITHOUT the emulator:
//   powershell -File Accessibility/tools/discovery-core-test.ps1
// That this file links with only the core's own sources is itself the test
// that the core is platform-independent.
//
// The tests play the part of an adapter: they invent a small machine with a
// font texture and feed the engine the events a real emulator would.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../Engine.h"
#include "../Harness.h"
#include "../Recording.h"
#include "../TextUtil.h"

using namespace Discovery;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool ok, const char *what) {
	g_checks++;
	if (!ok) {
		g_failures++;
		printf("FAIL: %s\n", what);
	}
}

static void CheckEq(const std::string &got, const std::string &want, const char *what) {
	g_checks++;
	if (got != want) {
		g_failures++;
		printf("FAIL: %s\n  got:  \"%s\"\n  want: \"%s\"\n", what, got.c_str(), want.c_str());
	}
}

// A pretend machine: one memory region the tests can write into.
class FakeMachine : public Machine {
public:
	FakeMachine() : memory_(0x10000, 0) {}
	std::string GameId() const override { return "TEST00001"; }
	std::vector<MemoryRegion> Regions() const override {
		return { MemoryRegion{ BASE, (u32)memory_.size(), "test RAM" } };
	}
	bool Read(u32 addr, void *out, u32 size) const override {
		if (addr < BASE || addr - BASE + size > memory_.size()) {
			return false;
		}
		memcpy(out, memory_.data() + (addr - BASE), size);
		return true;
	}
	void Write(u32 addr, const void *data, u32 size) { memcpy(memory_.data() + (addr - BASE), data, size); }
	static const u32 BASE = 0x08800000;

private:
	std::vector<u8> memory_;
};

// A pretend font atlas: 8-bit texture, 256 texels wide, glyph cells 16 x 16.
static const u32 ATLAS = 0x09000000;
static const u32 ATLAS_STRIDE = 256;

struct FakeFont {
	Engine *engine;
	int nextCell = 0;
	// Like a game with a glyph cache: render the character the first time it
	// is needed and remember which cell it went to.
	int cellOf[128];
	FakeFont(Engine *e) : engine(e) {
		for (int &c : cellOf) c = -1;
	}
	int Cell(char ch) {
		int &cell = cellOf[(unsigned char)ch];
		if (cell < 0) {
			cell = nextCell++;
			GlyphCell g;
			g.addr = ATLAS + (cell / 16) * 16 * ATLAS_STRIDE + (cell % 16) * 16;
			g.stride = ATLAS_STRIDE;
			g.bpp = 8;
			g.w = (ch == '.' || ch == ',') ? 3 : 9;
			g.h = (ch == '.' || ch == ',') ? 3 : 12;
			g.codepoint = (u32)ch;
			g.advance = (ch == '.' || ch == ',') ? 4.0f : 10.0f;
			g.left = 0;
			g.pc = 0x08801234;
			engine->OnGlyphRendered(g);
		}
		return cell;
	}
	// Draws a string the way a game does: one rectangle per glyph, no rectangle
	// for a space (the pen just moves on), a shadow pass first if asked.
	void Draw(const char *text, float x, float y, bool shadow = false) {
		for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
			float pen = x + (pass == 0 ? 1 : 0);
			float top = y + (pass == 0 ? 1 : 0);
			for (const char *p = text; *p; p++) {
				if (*p == ' ') {
					pen += 5;
					continue;
				}
				int cell = Cell(*p);
				bool small = *p == '.' || *p == ',';
				float w = small ? 3.0f : 9.0f, h = small ? 3.0f : 12.0f;
				SpriteDraw d;
				d.texAddr = ATLAS;
				d.texStride = ATLAS_STRIDE;
				d.texBpp = 8;
				d.u0 = (float)((cell % 16) * 16);
				d.v0 = (float)((cell / 16) * 16);
				d.u1 = d.u0 + w;
				d.v1 = d.v0 + h;
				// Small punctuation sits on the baseline, at the bottom of the line.
				float gy = small ? top + 9 : top;
				d.screen = RectF{ pen, gy, pen + w, gy + h };
				engine->OnSpriteDraw(d);
				pen += small ? 4 : 10;
			}
		}
	}
};

static std::string AllText(Engine &engine) {
	std::string out;
	for (const TextRun &run : engine.Screen().Runs()) {
		out += (out.empty() ? "" : " | ") + run.text;
	}
	return out;
}

static void TestTextUtil() {
	std::string s;
	AppendUtf8(s, 'A');
	AppendUtf8(s, 0xE8);    // e with grave
	AppendUtf8(s, 0x3042);  // hiragana a
	std::vector<u32> back = DecodeUtf8(s);
	Check(back.size() == 3 && back[0] == 'A' && back[1] == 0xE8 && back[2] == 0x3042, "UTF-8 round trip");
	CheckEq(NormalizeSpaces("  New \n  Game\t"), "New Game", "NormalizeSpaces");
	Check(EditDistance("kitten", "sitting") == 3, "EditDistance");
	CheckEq(JsonString("a\"b\n"), "\"a\\\"b\\n\"", "JsonString");

	std::vector<IniSection> ini = ParseIni("; top\n[case]\ngame = X ; note\nText = a;b\n\n[text.1]\ntext=Hi there\n");
	Check(ini.size() == 2 && ini[0].name == "case", "INI sections");
	CheckEq(ini[0].Get("game"), "X", "INI trailing comment removed");
	CheckEq(ini[0].Get("text"), "a;b", "INI semicolon inside a value kept");
	CheckEq(ini[1].Get("text"), "Hi there", "INI second section");
}

static void TestLedgerAndAssembly() {
	FakeMachine machine;
	Engine engine(&machine);
	FakeFont font(&engine);

	// Two lines, with spaces, punctuation, and a shadow pass.
	font.Draw("New Game", 100, 50, true);
	font.Draw("Load, or quit.", 100, 66, true);
	// A second column on the first line, far to the right.
	font.Draw("Gold", 300, 50);
	engine.OnFrameEnd();
	CheckEq(AllText(engine), "New Game | Gold | Load, or quit.", "lines, spaces, columns, shadow removed");

	const std::vector<TextRun> &runs = engine.Screen().Runs();
	Check(!runs.empty() && runs[0].source == TextSource::SystemFont, "source is the system font path");
	Check(!runs.empty() && runs[0].codePc == 0x08801234, "code location carried to the run");
	Check(!runs.empty() && runs[0].bufferAddr == ATLAS, "glyph buffer carried to the run");
	Check(!runs.empty() && runs[0].rect.x0 == 100 && runs[0].rect.y0 == 50, "run rectangle");

	// Changes: first frame, everything appeared.
	std::vector<TextChange> changes = engine.Screen().TakeChanges();
	Check(changes.size() == 3 && changes[0].kind == ChangeKind::Appeared, "three runs appeared");
	u32 goldId = 0;
	for (const TextRun &run : runs) {
		if (run.text == "Gold") goldId = run.id;
	}

	// Next frame: the menu slid 4 pixels, and "Gold" became "Gems" in place.
	font.Draw("New Game", 104, 50, true);
	font.Draw("Load, or quit.", 104, 66, true);
	font.Draw("Gems", 300, 50);
	engine.OnFrameEnd();
	changes = engine.Screen().TakeChanges();
	Check(changes.size() == 1 && changes[0].kind == ChangeKind::Changed, "one change: text replaced in place");
	if (changes.size() == 1) {
		CheckEq(changes[0].oldText, "Gold", "change remembers the old text");
		CheckEq(changes[0].run.text, "Gems", "change has the new text");
		Check(changes[0].run.id == goldId, "the run kept its id");
	}

	// A frame with nothing drawn must not report the text gone at once...
	engine.OnFrameEnd();
	Check(engine.Screen().TakeChanges().empty(), "one skipped frame is not a disappearance");
	// ...but after the grace period it does.
	for (int i = 0; i < 5; i++) {
		engine.OnFrameEnd();
	}
	changes = engine.Screen().TakeChanges();
	Check(changes.size() == 3 && changes[0].kind == ChangeKind::Disappeared, "runs disappear after the grace period");
}

static void TestLedgerOverwrite() {
	GlyphLedger ledger;
	GlyphCell a;
	a.addr = ATLAS; a.stride = ATLAS_STRIDE; a.bpp = 8; a.w = 9; a.h = 12; a.codepoint = 'A';
	ledger.Record(a);
	GlyphCell b = a;
	b.addr = ATLAS + 16; b.codepoint = 'B';
	ledger.Record(b);
	std::vector<GlyphLedger::Hit> hits;
	ledger.Find(ATLAS, ATLAS_STRIDE, 8, 16, 0, 25, 12, &hits);
	Check(hits.size() == 1 && hits[0].cell->codepoint == 'B', "lookup finds the right cell");
	ledger.Find(ATLAS, ATLAS_STRIDE, 8, 64, 64, 73, 76, &hits);
	Check(hits.empty(), "lookup of an empty place finds nothing");
	ledger.Find(ATLAS, ATLAS_STRIDE, 4, 16, 0, 25, 12, &hits);
	Check(hits.empty(), "a texture with another pixel size does not match");

	// The game reuses the first cell for another character.
	GlyphCell c = a;
	c.addr = ATLAS + 2; c.codepoint = 'C';
	ledger.Record(c);
	ledger.Find(ATLAS, ATLAS_STRIDE, 8, 0, 0, 12, 12, &hits);
	Check(hits.size() == 1 && hits[0].cell->codepoint == 'C', "drawing over a cell replaces it");
	Check(ledger.Size() == 2, "the overwritten cell was forgotten");
	Check(ledger.CoversRange(ATLAS, 4096) && !ledger.CoversRange(ATLAS + 0x100000, 4096), "CoversRange");

	// Sidecar round trip.
	GlyphLedger copy;
	Check(copy.Deserialize(ledger.Serialize()), "ledger deserializes");
	Check(copy.Size() == 2, "ledger round trip keeps the cells");
	copy.Find(ATLAS, ATLAS_STRIDE, 8, 16, 0, 25, 12, &hits);
	Check(hits.size() == 1 && hits[0].cell->codepoint == 'B', "ledger round trip keeps the content");
	Check(!copy.Deserialize("something else"), "a foreign file is refused");
}

// A game that draws a whole string into a strip and shows the strip with one
// rectangle. Then a shorter string goes into the same strip.
static void TestStringStrip() {
	FakeMachine machine;
	Engine engine(&machine);
	const u32 STRIP = 0x09100000;
	auto render = [&](const char *text) {
		int x = 0;
		for (const char *p = text; *p; p++) {
			if (*p != ' ') {
				GlyphCell g;
				g.addr = STRIP + x; g.stride = 512; g.bpp = 8; g.w = 9; g.h = 12;
				g.codepoint = (u32)*p; g.advance = 10; g.pc = 0x08805678;
				engine.OnGlyphRendered(g);
			}
			x += (*p == ' ') ? 5 : 10;
		}
	};
	auto show = [&]() {
		SpriteDraw d;
		d.texAddr = STRIP; d.texStride = 512; d.texBpp = 8;
		d.u0 = 0; d.v0 = 0; d.u1 = 256; d.v1 = 16;
		d.screen = RectF{ 20, 200, 276, 216 };
		engine.OnSpriteDraw(d);
	};
	render("Hello there");
	show();
	engine.OnFrameEnd();
	CheckEq(AllText(engine), "Hello there", "a string strip drawn as one rectangle");

	for (int i = 0; i < 10; i++) {
		show();
		engine.OnFrameEnd();
	}
	render("Hi");
	show();
	engine.OnFrameEnd();
	CheckEq(AllText(engine), "Hi", "the tail of the older, longer string is not reported");
}

static void TestSystemDialog() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.OnSystemText("Do you want to save?", RectF{ 140, 100, 340, 116 });
	engine.OnSystemText("Yes", RectF{ 195, 150, 211, 166 });
	engine.OnSystemText("No", RectF{ 266, 150, 278, 166 });
	engine.OnSystemHighlight(RectF{ 258, 142, 288, 158 + 16 });
	engine.OnSystemHighlight(RectF{ 0, 26, 480, 27 });  // A decoration line: ignored.
	engine.OnFrameEnd();
	const std::vector<TextRun> &runs = engine.Screen().Runs();
	Check(runs.size() == 3, "three dialog runs");
	if (runs.size() == 3) {
		CheckEq(runs[0].text, "Do you want to save?", "dialog reading order: message first");
		Check(!runs[1].selected && runs[2].selected && runs[2].text == "No", "the highlighted option is marked selected");
		Check(runs[0].confidence == 1.0f && runs[0].source == TextSource::SystemDialog, "dialog text is certain");
	}
	engine.Screen().TakeChanges();

	// The player moves to Yes: no text changed, but the selection did.
	engine.OnSystemText("Do you want to save?", RectF{ 140, 100, 340, 116 });
	engine.OnSystemText("Yes", RectF{ 195, 150, 211, 166 });
	engine.OnSystemText("No", RectF{ 266, 150, 278, 166 });
	engine.OnSystemHighlight(RectF{ 189, 142, 219, 174 });
	engine.OnFrameEnd();
	std::vector<TextChange> changes = engine.Screen().TakeChanges();
	Check(changes.size() == 1 && changes[0].run.text == "Yes" && changes[0].run.selected, "moving the selection is reported");
}

static void TestCallStreamFallback() {
	FakeMachine machine;
	Engine engine(&machine);
	// Glyphs rendered into a scratch buffer that is never drawn from.
	for (const char *p = "Scratch"; *p; p++) {
		GlyphCell g;
		g.addr = 0x09200000; g.stride = 64; g.bpp = 8; g.w = 9; g.h = 12; g.codepoint = (u32)*p; g.pc = 0x0880AAAA;
		engine.OnGlyphRendered(g);
	}
	engine.OnFrameEnd();
	const std::vector<TextRun> &runs = engine.Screen().Runs();
	Check(runs.size() == 1 && runs[0].text == "Scratch" && runs[0].source == TextSource::FontCallStream, "call-order fallback text");
	Check(runs.size() == 1 && !runs[0].hasRect && runs[0].confidence < 0.6f, "fallback text has no position and lower confidence");
	Check(engine.GetSurvey().Verdict().find("call order") != std::string::npos, "survey explains the fallback");
}

static void TestLocate() {
	FakeMachine machine;
	Engine engine(&machine);
	machine.Write(FakeMachine::BASE + 0x100, "Silver Kettle", 13);
	const char wide[] = { 'G', 0, 'e', 0, 'm', 0, 's', 0 };
	machine.Write(FakeMachine::BASE + 0x400, wide, sizeof(wide));
	std::vector<TextLocation> hits = engine.Locate("Silver Kettle");
	Check(hits.size() == 1 && hits[0].addr == FakeMachine::BASE + 0x100 && hits[0].encoding == "utf8", "locate UTF-8 text");
	hits = engine.Locate("Gems");
	Check(hits.size() == 1 && hits[0].addr == FakeMachine::BASE + 0x400 && hits[0].encoding == "utf16le", "locate UTF-16 text");
	Check(engine.Locate("Not present").empty(), "locate finds nothing for absent text");
}

// ---- the glyph cipher ----

// Draws a string with a game's own font: one rectangle per character from a
// font texture. `grid` fonts are laid out in code order (cell = code - 32);
// otherwise each character sits at an arbitrary place, like a packed atlas.
struct OwnFont {
	Engine *engine;
	u32 tex;
	bool grid;
	bool videoMemory = false;
	void Draw(const char *text, float x, float y) {
		float pen = x;
		for (const char *p = text; *p; p++) {
			unsigned char c = (unsigned char)*p;
			SpriteDraw d;
			d.texAddr = tex;
			d.fromSurface = videoMemory;
			d.texW = 512;
			d.texH = 512;
			if (grid) {
				int cell = c - 32;
				d.u0 = (float)((cell % 32) * 16);
				d.v0 = (float)((cell / 32) * 16);
			} else {
				// Scrambled, but the same character always at the same place.
				d.u0 = (float)(((c * 37) % 29) * 17 + 3);
				d.v0 = (float)(((c * 11) % 23) * 19 + 5);
			}
			d.u1 = d.u0 + 16;
			d.v1 = d.v0 + 16;
			d.screen = RectF{ pen, y, pen + 16, y + 16 };
			engine->OnSpriteDraw(d);
			pen += (c == ' ' || c == 'i' || c == 'l') ? 4.0f : 8.0f;
		}
	}
};

static void RunFrames(Engine &engine, int frames, const std::function<void()> &draw) {
	for (int i = 0; i < frames; i++) {
		draw();
		engine.OnFrameEnd();
	}
}

static void TestGlyphCipherGridFont() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	const char *help = "You may type up to 12 letters here, no more.";
	machine.Write(FakeMachine::BASE + 0x2000, help, (u32)strlen(help));
	machine.Write(FakeMachine::BASE + 0x3000, "RobinA", 6);
	// Other text in memory that must not confuse it.
	machine.Write(FakeMachine::BASE + 0x4000, "Sixteen other words that are not on the screen at all.", 54);
	OwnFont font{ &engine, 0x09A00000, true };

	RunFrames(engine, 45, [&]() {
		font.Draw(help, 10, 0);
		font.Draw("RobinA", 180, 40);
	});
	CheckEq(AllText(engine), std::string(help) + " | RobinA", "own grid font read through the cipher");
	const std::vector<TextRun> &runs = engine.Screen().Runs();
	Check(!runs.empty() && runs[0].source == TextSource::GameFont, "source is the game's own font");
	Check(engine.GetSurvey().Get("cipher.lines_matched") >= 1, "a line was matched in memory");

	// A word never seen before is read at once: the layout rule names its letters.
	RunFrames(engine, 2, [&]() { font.Draw("Quick fox", 10, 100); });
	CheckEq(AllText(engine), "Quick fox", "new text in a known font needs no search");
	Check(engine.Book().Describe().find("cell number + 32") != std::string::npos, "the layout rule was found");
}

static void TestGlyphCipherPackedFont() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	const char *line1 = "The quick brown fox jumps over the lazy dog";
	const char *line2 = "Pack my box with five dozen liquor jugs";
	machine.Write(FakeMachine::BASE + 0x2000, line1, (u32)strlen(line1));
	machine.Write(FakeMachine::BASE + 0x2400, line2, (u32)strlen(line2));
	OwnFont font{ &engine, 0x09B00000, false };

	RunFrames(engine, 45, [&]() {
		font.Draw(line1, 10, 0);
		font.Draw(line2, 10, 20);
	});
	CheckEq(AllText(engine), std::string(line1) + " | " + line2, "packed font read from pattern alone");

	// Letters seen before spell new words; a letter never seen stays unknown.
	RunFrames(engine, 2, [&]() { font.Draw("dog and Zebra", 10, 100); });
	CheckEq(AllText(engine), "dog and \xEF\xBF\xBD" "ebra", "an unseen letter is marked unknown, not guessed");
}

static void TestGlyphCipherRefusesGuesses() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	// Two different strings in memory fit the short pattern on screen equally.
	machine.Write(FakeMachine::BASE + 0x2000, "abcde", 5);
	machine.Write(FakeMachine::BASE + 0x2100, "vwxyz", 5);
	OwnFont font{ &engine, 0x09C00000, false };
	RunFrames(engine, 45, [&]() { font.Draw("abcde", 10, 0); });
	CheckEq(AllText(engine), "", "a line with two possible readings is not read");

	// Text that is in memory nowhere is not read either.
	FakeMachine empty;
	Engine engine2(&empty);
	engine2.SetCaptureAllSprites(true);
	OwnFont font2{ &engine2, 0x09C00000, true };
	RunFrames(engine2, 45, [&]() { font2.Draw("Nothing like this in memory", 10, 0); });
	CheckEq(AllText(engine2), "", "text that is not in memory is not invented");
}

// Screen copies and big images are not letters.
static void TestGlyphCipherIgnoresNonLetters() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	SpriteDraw big;
	big.texAddr = 0x09D00000; big.u0 = 0; big.v0 = 0; big.u1 = 256; big.v1 = 128;
	big.screen = RectF{ 0, 0, 480, 272 };
	SpriteDraw copy;
	copy.texAddr = 0x04000000; copy.fromSurface = true; copy.u1 = 32; copy.v1 = 32;
	copy.screen = RectF{ 0, 0, 16, 16 };
	// A screen copy is a texture inside a surface the game draws into.
	engine.OnRenderTarget(0x04000000, 0x88000);
	engine.OnSpriteDraw(big);
	engine.OnSpriteDraw(copy);
	engine.OnFrameEnd();
	Check(engine.Book().Size() == 0, "large images and screen copies are not treated as letters");
	Check(engine.LastSprites().size() == 2, "but they are kept in the draw stream");
}

static void TestHarness() {
	const char *ini =
		"[case]\n"
		"game = TEST00001\n"
		"state = menu.ppst\n"
		"description = Title menu\n"
		"settle_frames = 30\n"
		"complete = yes\n"
		"[text.1]\ntext = New Game\n"
		"[text.2]\ntext = Load, or quit.\n"
		"[text.3]\ntext = Options\n"
		"[text.4]\ntext = quit\nmatch = contains\n"
		"[text.5]\ntext = Game Gold\n"
		"[cursor.1]\nindex = 0\n";
	HarnessCase test;
	std::string error;
	Check(ParseHarnessCase(ini, &test, &error), "case file parses");
	Check(test.texts.size() == 5 && test.settleFrames == 30 && test.complete && test.cursors == 1, "case file contents");

	auto run = [](const char *text) {
		TextRun r;
		r.text = text;
		r.source = TextSource::SystemFont;
		r.confidence = 0.9f;
		return r;
	};
	std::vector<TextRun> found = { run("New Game"), run("Gold"), run("Load, or quit."), run("Optlons"), run("Junk") };
	HarnessResult result = ScoreHarnessCase(test, found);
	Check(result.texts.size() == 5, "five scores");
	if (result.texts.size() == 5) {
		Check(result.texts[0].verdict == TextVerdict::Exact, "exact match");
		Check(result.texts[1].verdict == TextVerdict::Exact, "exact match with punctuation");
		Check(result.texts[2].verdict == TextVerdict::Near, "one wrong character is near, not found");
		Check(result.texts[3].verdict == TextVerdict::Exact, "match = contains");
		Check(result.texts[4].verdict == TextVerdict::Contained, "text split over two runs is contained");
	}
	Check(result.exact == 3 && result.contained == 1 && result.near == 1 && result.missing == 0, "verdict counts");
	Check(result.recall > 0.79f && result.recall < 0.81f, "recall is 4 of 5");
	// Explained: New Game, Gold (part of 'Game Gold'), Load..., Optlons (near). Not: Junk.
	Check(result.precision > 0.79f && result.precision < 0.81f, "precision is 4 of 5");
	std::string json = HarnessResultToJson(test, result, "cold", "\"build\": \"test\"");
	Check(json.find("\"recall\": 0.800") != std::string::npos && json.find("\"build\": \"test\"") != std::string::npos, "JSON output");

	Check(!ParseHarnessCase("[text.1]\ntext = x\n", &test, &error), "a case file without [case] is refused");
}

// ---- text the first cipher could not reach (found with the recordings of 2026-11-12) ----

// A font kept in video memory is a font; a screen copy in video memory is not.
static void TestFontInVideoMemory() {
	const char *help = "Walking distance changes with the ground below.";
	{
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		machine.Write(FakeMachine::BASE + 0x2000, help, (u32)strlen(help));
		// The game draws into two surfaces; its font sits beyond them.
		engine.OnRenderTarget(0x04000000, 0x88000);
		engine.OnRenderTarget(0x04088000, 0x88000);
		OwnFont font{ &engine, 0x041C0BA0, true, true };
		RunFrames(engine, 45, [&]() { font.Draw(help, 10, 0); });
		CheckEq(AllText(engine), help, "a font kept in video memory is read");
	}
	{
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		machine.Write(FakeMachine::BASE + 0x2000, help, (u32)strlen(help));
		engine.OnRenderTarget(0x04000000, 0x88000);
		engine.OnRenderTarget(0x04088000, 0x88000);
		// The same draws, but from inside a surface the game draws into.
		OwnFont font{ &engine, 0x04090000, true, true };
		RunFrames(engine, 45, [&]() { font.Draw(help, 10, 0); });
		CheckEq(AllText(engine), "", "letter-sized pieces of a drawing surface are not text");
	}
}

// Dialogue as Tactics Ogre stores it: control codes between the words, the hero's name
// put in by a code, a code for the line break. The row is not in memory as
// one string; its words are.
static void TestCipherFindsWords() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	const char stored[] = "\xFF\xA9It's \xFF\x81\x01\x01" "as \xFF\x81\x01\x01we \xFF\x81\x01\x01" "feared, \xFF\x81\x01\x01\xFF\x93.\xFF\x80Rosamund's \xFF\x81\x01\x01" "arrived.";
	machine.Write(FakeMachine::BASE + 0x2000, stored, (u32)sizeof(stored));
	machine.Write(FakeMachine::BASE + 0x4000, "Sixteen other words that are not on the screen at all.", 54);
	OwnFont font{ &engine, 0x041B7B90, true, true };
	RunFrames(engine, 90, [&]() {
		font.Draw("It's as we feared, Robin.", 127, 47);
		font.Draw("Rosamund's arrived.", 127, 62);
	});
	CheckEq(AllText(engine), "It's as we feared, Robin. | Rosamund's arrived.", "a line stored with control codes between its words is read");
	Check(engine.GetSurvey().Get("cipher.lines_matched_by_words") >= 1, "it was found by its words");
	Check(engine.Book().Describe().find("cell number + 32") != std::string::npos, "the words gave the layout rule");
}

// The word search must not turn a single short coincidence into text.
static void TestCipherWordsRefuseGuesses() {
	{
		// Only one six-letter stretch of the row is in memory: not believed.
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		machine.Write(FakeMachine::BASE + 0x2000, "\x01\x02" "Mother" "\x03\x04", 10);
		OwnFont font{ &engine, 0x09A00000, true };
		RunFrames(engine, 90, [&]() { font.Draw("Right, Mother? Then go.", 10, 0); });
		CheckEq(AllText(engine), "", "one short stretch found in memory is not enough");
	}
	{
		// A row of neighbouring cells fits any table of rising numbers.
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		char table[64];
		for (int i = 0; i < 64; i++) {
			table[i] = (char)(0x30 + i);
		}
		machine.Write(FakeMachine::BASE + 0x2000, table, 64);
		OwnFont font{ &engine, 0x09A00000, true };
		RunFrames(engine, 90, [&]() { font.Draw("ABCDEFGHIJKLMNOP", 10, 0); });
		CheckEq(AllText(engine), "", "a row of neighbouring cells proves nothing");
	}
	{
		// Memory holds the words twice: as characters and as the game's own
		// cell numbers shifted into the printable range. The copy that reads
		// as text wins.
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		const char *words[] = { "Rosamund's ", "arrived." };
		u32 at = FakeMachine::BASE + 0x2000;
		for (const char *w : words) {
			machine.Write(at, w, (u32)strlen(w));
			at += 0x40;
			std::string shifted;
			for (const char *p = w; *p; p++) {
				shifted += (char)(*p - 0x0B);
			}
			machine.Write(at, shifted.data(), (u32)shifted.size());
			at += 0x40;
		}
		OwnFont font{ &engine, 0x09A00000, true };
		RunFrames(engine, 90, [&]() { font.Draw("Rosamund's arrived.", 10, 0); });
		CheckEq(AllText(engine), "Rosamund's arrived.", "of two copies in layout order, the one that reads as text is taken");
	}
}

// A font drawn from two textures, and a game with its own code for the space
// (Persona 2, found with the recordings of 2026-11-13).
struct PagedFont {
	Engine *engine;
	u32 color = 0;  // The colour given to the draws that follow.
	int shuffle = 0;  // Another value puts every letter somewhere else in the textures (a font cache filled in another order).
	// Capitals and punctuation come from one texture, the rest from another;
	// letters are packed tight and have their own widths. Spaces are not drawn.
	void Draw(const char *text, float x, float y) {
		float pen = x;
		for (const char *p = text; *p; p++) {
			unsigned char c = (unsigned char)*p;
			if (c == ' ') {
				pen += 4;
				continue;
			}
			bool first = !(c >= 'a' && c <= 'z');
			// With `shuffle`, each letter sits where another one sat before.
			int slot = c + shuffle;
			float w = (float)(6 + slot % 5);
			SpriteDraw d;
			d.texAddr = first ? 0x090B8C00 : 0x090C0E80;
			d.texW = 256;
			d.texH = 256;
			d.texFormat = 4;
			d.u0 = (float)(((slot * 37) % 19) * 13 + 1);
			d.v0 = (float)(((slot * 11) % 13) * 16);
			d.u1 = d.u0 + w;
			d.v1 = d.v0 + 16;
			d.screen = RectF{ pen, y, pen + w, y + 16 };
			d.color = color;
			engine->OnSpriteDraw(d);
			pen += w;
		}
	}
};

static void WriteWide(FakeMachine &machine, u32 addr, const char *text, u16 space) {
	for (const char *p = text; *p; p++, addr += 2) {
		u16 unit = *p == ' ' ? space : (u16)(unsigned char)*p;
		machine.Write(addr, &unit, 2);
	}
}

static void TestFontOverTwoTextures() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteWide(machine, FakeMachine::BASE + 0x2000, "You look so tired...", 0x20);
	WriteWide(machine, FakeMachine::BASE + 0x2100, "Did you lose something again?", 0x20);
	PagedFont font{ &engine };
	RunFrames(engine, 120, [&]() {
		font.Draw("Old Man", 40, 200);
		font.Draw("You look so tired...", 54, 218);
		font.Draw("Did you lose something again?", 54, 236);
	});
	std::string all = AllText(engine);
	Check(all.find("You look so tired...") != std::string::npos && all.find("Did you lose something again?") != std::string::npos,
		"a row whose letters come from two textures is read whole, capitals included");
	printf("    (two textures: %s)\n", all.c_str());
}

static void TestGameWithItsOwnSpaceCode() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteWide(machine, FakeMachine::BASE + 0x2000, "Rather rough pupil", 0x1120);
	WriteWide(machine, FakeMachine::BASE + 0x2100, "Haha! This rusty old car of mine won't", 0x1120);
	WriteWide(machine, FakeMachine::BASE + 0x2200, "start without help.", 0x1120);
	PagedFont font{ &engine };
	RunFrames(engine, 200, [&]() {
		font.Draw("Rather rough pupil", 34, 200);
		font.Draw("Haha! This rusty old car of mine won't", 44, 218);
		font.Draw("start without help.", 33, 236);
	});
	std::string all = AllText(engine);
	Check(all.find("rough pupil") != std::string::npos && all.find("car of mine") != std::string::npos
		&& all.find("start without help.") != std::string::npos, "text stored with the game's own space code is read");
	printf("    (own space code: %s)\n", all.c_str());
}

static void TestLooseSearchRefusesGuesses() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	// Memory has text of the same shape as the row only if arbitrary bytes
	// may be skipped between letters: that must not count.
	const char junk[] = "st\x01" "ar\x02t w\x03ith\x04out he\x05lp.";
	machine.Write(FakeMachine::BASE + 0x2000, junk, (u32)sizeof(junk));
	// And a zero (the end of a string) is never a space.
	const char twoStrings[] = "start\0without\0help.";
	machine.Write(FakeMachine::BASE + 0x2100, twoStrings, (u32)sizeof(twoStrings));
	PagedFont font{ &engine };
	RunFrames(engine, 200, [&]() { font.Draw("start without help.", 33, 236); });
	CheckEq(AllText(engine), "", "letters with changing codes or string ends between them are not a line");
}

// ---- text in a game's own letter code (Persona 2's menus) ----

// Persona 2 keeps its menu text two bytes a letter, high byte first: small
// letters from 0x0101, capitals from 0x00E0, the space 0x8140, the end 0xFFFF.
// `scramble` puts the alphabet on the codes in another order: the pattern of
// a row is still found, but the codes no longer follow the alphabet.
static u32 WriteOwnCode(FakeMachine &machine, u32 addr, const char *text, bool scramble) {
	static const char mixed[] = "qwertyuiopasdfghjklzxcvbnm";
	for (const char *p = text; *p; p++, addr += 2) {
		unsigned char c = (unsigned char)*p;
		u16 code = 0x8140;
		if (c >= 'a' && c <= 'z') {
			code = (u16)(0x0101 + (scramble ? mixed[c - 'a'] - 'a' : c - 'a'));
		} else if (c >= 'A' && c <= 'Z') {
			code = (u16)(0x00E0 + (scramble ? mixed[c - 'A'] - 'a' : c - 'A'));
		}
		u8 bytes[2] = { (u8)(code >> 8), (u8)(code & 0xFF) };
		machine.Write(addr, bytes, 2);
	}
	u8 end[2] = { 0xFF, 0xFF };
	machine.Write(addr, end, 2);
	return addr + 2;
}

static void WriteMenuStrings(FakeMachine &machine, bool scramble) {
	static const char *strings[] = {
		// Invented menu text: ordinary English of the same kind and amount as a game's options and field menus.
		"Set how fast the words appear", "Pick sound and screen options", "Word Speed", "Music", "Sound Level",
		"Hint Window", "Turn Camera", "Picture Size", "Challenge", "Quick Loading", "Keep progress", "Load", "Title",
		"Options", "Keep", "Slow", "Normal", "Fast", "Classic", "Modern", "Middle", "Broad", "Gentle", "Tough",
		"Look at and change your gear", "Look at and use your goods", "Look at and use your talents", "Friends",
		"Wear", "Goods", "Talents", "You hold no letters", "You hold no goods", "Nobody has joined you yet",
		"Say goodbye to", "North", "South", "East", "West", "River", "Hill", "Show the faces during fights",
		"Choose which way the view turns", "Choose the picture quality", "Choose how hard the fights are",
		"Helps the game start quicker", "Return to the last choice made", "Begin a new journey",
		"Carry on from where you stopped", "Go back to the opening screen", "Group", "Health", "Setup", "Speak",
		"Chat", "Strike", "Guard", "Run away", "Call", "Plan", "Joint Magic", "Swap places", "Fight",
		"Spare companions", "Letter box", "Coins", "Hours played",
	};
	u32 addr = FakeMachine::BASE + 0x8000;
	for (const char *text : strings) {
		addr = WriteOwnCode(machine, addr, text, scramble);
	}
}

static void TestTextInOwnLetterCode() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteMenuStrings(machine, false);
	PagedFont font{ &engine };
	RunFrames(engine, 900, [&]() {
		font.Draw("Set how fast the words appear", 30, 5);
		font.Draw("Pick sound and screen options", 30, 30);
		font.Draw("Hint Window", 30, 55);
		font.Draw("Load", 30, 80);
	});
	std::string all = AllText(engine);
	printf("    (own letter code: %s)\n", all.c_str());
	Check(all.find("Set how fast the words appear") != std::string::npos && all.find("Pick sound and screen options") != std::string::npos,
		"text kept in a game's own letter code is read");
	Check(all.find("Hint Window") != std::string::npos, "a row never looked for is read from the letters learned");
	Check(all.find("Load") != std::string::npos, "a short row gives the one letter still missing");
}

static void TestOwnLetterCodeRefusesGuesses() {
	// The rows are found, but the codes are not in alphabet order: there is
	// no telling which letter is which, so nothing may be shown.
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteMenuStrings(machine, true);
	PagedFont font{ &engine };
	RunFrames(engine, 900, [&]() {
		font.Draw("Set how fast the words appear", 30, 5);
		font.Draw("Pick sound and screen options", 30, 30);
	});
	CheckEq(AllText(engine), "", "codes that do not follow the alphabet are not turned into letters");
}

// ---- the per-game profile ----

static const char *const kSessionLines[4] = {
	"You look so tired...", "Did you lose something again?", "Haha! This rusty old car of mine won't", "start without help.",
};

// One session of a game with a font of its own: draws two of the lines above
// and one short text, and returns everything read.
static std::string PlaySession(Engine &engine, FakeMachine &machine, int first, int second, const char *shortText, int shuffle) {
	engine.SetCaptureAllSprites(true);
	for (int i = 0; i < 4; i++) {
		WriteWide(machine, FakeMachine::BASE + 0x2000 + i * 0x100, kSessionLines[i], 0x20);
	}
	PagedFont font{ &engine };
	font.shuffle = shuffle;
	RunFrames(engine, 300, [&]() {
		font.Draw(kSessionLines[first], 40, 200);
		font.Draw(kSessionLines[second], 40, 218);
		font.Draw(shortText, 40, 236);
	});
	return AllText(engine);
}

static void TestProfileRemembersAFont() {
	std::string profile;
	{
		FakeMachine machine;
		Engine engine(&machine);
		PlaySession(engine, machine, 2, 3, "cars", 0);
		profile = engine.ExportProfile();
		Check(profile.find("[font]") != std::string::npos && profile.find("[findings]") != std::string::npos,
			"a session's letters and findings are written to the profile");
	}
	{
		// Without the profile: "wolf cow" has a "w", an "f" and a "c" the two lines of this session never showed.
		FakeMachine machine;
		Engine engine(&machine);
		std::string all = PlaySession(engine, machine, 0, 1, "wolf cow", 0);
		Check(all.find("wolf cow") == std::string::npos, "(without a profile the short text has unknown letters)");
	}
	{
		FakeMachine machine;
		Engine engine(&machine);
		engine.ImportProfile(profile);
		std::string all = PlaySession(engine, machine, 0, 1, "wolf cow", 0);
		printf("    (with the profile: %s)\n", all.c_str());
		Check(all.find("wolf cow") != std::string::npos, "letters remembered from an earlier session are used once the font is confirmed");
	}
	{
		// The same game, but this time the font's images are somewhere else:
		// what is remembered must be refused, and never kept again.
		FakeMachine machine;
		Engine engine(&machine);
		engine.ImportProfile(profile);
		std::string all = PlaySession(engine, machine, 0, 1, "wolf cow", 1);
		Check(all.find("You look so tired...") != std::string::npos && all.find("wolf cow") == std::string::npos,
			"remembered letters that do not agree with this session are not used");
		Check(engine.ExportProfile().find("[volatile-font]") != std::string::npos, "and that font is marked as one not to remember");
	}
}

static void TestProfileRemembersALetterCode() {
	std::string profile;
	{
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		WriteMenuStrings(machine, false);
		PagedFont font{ &engine };
		RunFrames(engine, 600, [&]() {
			font.Draw("Set how fast the words appear", 30, 5);
			font.Draw("Pick sound and screen options", 30, 30);
		});
		profile = engine.ExportProfile();
		Check(profile.find("[letter-code]") != std::string::npos, "a letter code worked out is written to the profile");
	}
	{
		// One row only: by itself not enough to work the letters out.
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		WriteMenuStrings(machine, false);
		PagedFont font{ &engine };
		RunFrames(engine, 300, [&]() { font.Draw("Show the faces during fights", 30, 5); });
		CheckEq(AllText(engine), "", "(without a profile one row is not enough)");
		Engine again(&machine);
		again.SetCaptureAllSprites(true);
		again.ImportProfile(profile);
		PagedFont font2{ &again };
		RunFrames(again, 300, [&]() { font2.Draw("Show the faces during fights", 30, 5); });
		CheckEq(AllText(again), "Show the faces during fights", "with the letter code remembered, the first row found is read at once");
	}
}

// ---- the selected item (Phase D3, second part) ----

static const char *const kMenuItems[3] = { "Word Speed setting", "Hint Window setting", "Sound Level setting" };

static void WriteMenuItems(FakeMachine &machine) {
	for (int i = 0; i < 3; i++) {
		WriteWide(machine, FakeMachine::BASE + 0x2000 + i * 0x100, kMenuItems[i], 0x20);
	}
}

// A small image that is not a letter: a bar, a hand.
static void DrawMark(Engine &engine, float x, float y) {
	SpriteDraw d;
	d.texAddr = 0x091D5A00;
	d.texW = 64;
	d.texH = 16;
	d.texFormat = 4;
	d.u0 = 0;
	d.v0 = 0;
	d.u1 = 60;
	d.v1 = 8;
	d.screen = RectF{ x, y, x + 60, y + 8 };
	engine.OnSpriteDraw(d);
}

static void TestSelectionByMark() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteMenuItems(machine);
	PagedFont font{ &engine };
	int selected = 0;
	auto draw = [&]() {
		for (int i = 0; i < 3; i++) {
			font.Draw(kMenuItems[i], 30, 40.0f + i * 20);
		}
		DrawMark(engine, 250, 44.0f + selected * 20);
	};
	RunFrames(engine, 200, draw);
	Check(!engine.Selected().known, "a still screen says nothing about the selection");
	selected = 1;
	RunFrames(engine, 40, draw);
	Check(engine.Selected().known && engine.Selected().text == kMenuItems[1], "a mark that jumps to another text shows the selection");
	selected = 2;
	RunFrames(engine, 40, draw);
	CheckEq(engine.Selected().text, kMenuItems[2], "and is followed from then on");
	bool flagged = false;
	for (const TextRun &run : engine.Screen().Runs()) {
		flagged = flagged || (run.selected && run.text == kMenuItems[2]);
	}
	Check(flagged, "the selected text is marked as selected");
}

static void TestSelectionByPlace() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteMenuItems(machine);
	PagedFont font{ &engine };
	int selected = 0;
	auto draw = [&]() {
		for (int i = 0; i < 3; i++) {
			font.Draw(kMenuItems[i], i == selected ? 38.0f : 30.0f, 40.0f + i * 20);
		}
	};
	RunFrames(engine, 200, draw);
	Check(!engine.Selected().known, "an item set to the side says nothing until the selection moves");
	selected = 2;
	RunFrames(engine, 40, draw);
	CheckEq(engine.Selected().text, kMenuItems[2], "two texts changing places show the selection");
}

static void TestSelectionByColour() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteMenuItems(machine);
	WriteWide(machine, FakeMachine::BASE + 0x2400, "Settings of the game", 0x20);
	PagedFont font{ &engine };
	int selected = 0;
	auto draw = [&]() {
		// A title in a colour of its own: never the selection.
		font.color = 0xFF00FFFF;
		font.Draw("Settings of the game", 30, 10);
		for (int i = 0; i < 3; i++) {
			font.color = i == selected ? 0xFFFFFFFF : 0xFF808080;
			font.Draw(kMenuItems[i], 30, 40.0f + i * 20);
		}
	};
	RunFrames(engine, 200, draw);
	Check(!engine.Selected().known, "a text in another colour is not taken for the selection");
	selected = 1;
	RunFrames(engine, 40, draw);
	CheckEq(engine.Selected().text, kMenuItems[1], "two texts changing colours show the selection");
}

static void TestSelectionRefusesPassersBy() {
	// A small image drifting down the screen over the menu: it is beside each
	// text for a while, but never stops.
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteMenuItems(machine);
	PagedFont font{ &engine };
	float y = 30;
	RunFrames(engine, 400, [&]() {
		for (int i = 0; i < 3; i++) {
			font.Draw(kMenuItems[i], 30, 40.0f + i * 20);
		}
		DrawMark(engine, 250, y);
		y += 0.5f;
		if (y > 110) {
			y = 30;
		}
	});
	Check(!engine.Selected().known, "an image passing over the texts is not a selection mark");
}

// ---- numbers (Phase D3, first part) ----

// A strip of digits as Tactics Ogre has it: ten images of 9 by 13 side by
// side, one every 10, in the order 0 to 9. A second, larger strip below it.
struct DigitFont {
	Engine *engine;
	void Draw(const char *text, float x, float y, bool big = false) {
		float pen = x;
		for (const char *p = text; *p; p++) {
			if (*p == ' ') {
				pen += 3;
				continue;
			}
			int digit = *p - '0';
			SpriteDraw d;
			d.texAddr = 0x09538610;
			d.texW = 128;
			d.texH = 256;
			d.texFormat = 4;
			d.u0 = big ? 1.0f + digit * 14.25f : 1.0f + digit * 10.0f;
			d.v0 = big ? 15.0f : 1.0f;
			d.u1 = d.u0 + (big ? 14 : 9);
			d.v1 = d.v0 + (big ? 19 : 13);
			d.u0 = (float)(int)(d.u0 + 0.5f);
			d.u1 = d.u0 + (big ? 14 : 9);
			float w = big ? 14.0f : 9.0f, h = big ? 19.0f : 13.0f;
			d.screen = RectF{ pen, y, pen + w, y + h };
			engine->OnSpriteDraw(d);
			pen += big ? 9 : 7;
		}
	}
};

static void WriteValues(FakeMachine &machine, u32 addr, std::initializer_list<int> values) {
	for (int v : values) {
		u16 unit = (u16)v;
		machine.Write(addr, &unit, 2);
		addr += 2;
	}
}

static const ScreenNumber *NumberAt(Engine &engine, float x, float y) {
	for (const ScreenNumber &n : engine.Numbers()) {
		if (std::abs(n.rect.x0 - x) < 0.5f && std::abs(n.rect.y0 - y) < 0.5f) {
			return &n;
		}
	}
	return nullptr;
}

static void TestNumbersFromDigitStrip() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	const u32 record = FakeMachine::BASE + 0x4000;
	// Hit points, magic points, a third value; then the three maximums.
	WriteValues(machine, record, { 143, 0, 0, 143, 0, 200 });
	// A table elsewhere that happens to hold the same values further apart.
	WriteValues(machine, FakeMachine::BASE + 0x6000, { 143, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 143, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 200 });
	DigitFont font{ &engine };
	int hp = 143;
	auto draw = [&]() {
		char text[16];
		snprintf(text, sizeof(text), "%03d", hp);
		font.Draw(text, 65, 225, true);
		font.Draw("143", 93, 231);
		font.Draw("000", 123, 225, true);
		font.Draw("000", 151, 231);
		font.Draw("000", 181, 225, true);
		font.Draw("200", 209, 231);
		font.Draw("35 40", 231, 239);
	};
	RunFrames(engine, 300, draw);
	std::string all = AllText(engine);
	printf("    (numbers: %s)\n", all.c_str());
	Check(all.find("143") != std::string::npos && all.find("200") != std::string::npos, "numbers drawn from a strip of digits are read");
	Check(all.find("35 40") != std::string::npos, "two numbers side by side are kept apart");
	const ScreenNumber *n = NumberAt(engine, 65, 225);
	Check(n && n->value == 143 && n->candidates.size() == 2 && n->candidates[0] == record, "a number's place in memory is among the candidates");
	Check(engine.Book().NumberRecords().size() == 1 && engine.Book().NumberRecords()[0].addr == record, "the record the numbers come from is found, and only that");
	// The hero is hit: the value changes in memory and on screen.
	hp = 120;
	WriteValues(machine, record, { 120 });
	RunFrames(engine, 60, draw);
	n = NumberAt(engine, 65, 225);
	Check(n && n->value == 120 && n->candidates.size() == 1 && n->candidates[0] == record && n->changes == 1, "a change on screen narrows the candidates to the right address");
}

static void TestNumbersRefuseGuesses() {
	// The same screen, but memory does not hold these numbers together.
	{
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		WriteValues(machine, FakeMachine::BASE + 0x4000, { 143 });
		WriteValues(machine, FakeMachine::BASE + 0x5000, { 143 });
		WriteValues(machine, FakeMachine::BASE + 0x6000, { 200 });
		// What a wrong first digit would make of the screen (seen in Tactics
		// Ogre): padding zeros become 111, and small numbers are everywhere.
		WriteValues(machine, FakeMachine::BASE + 0x7000, { 111, 111, 46, 51, 45, 111, 444, 444, 254, 12 });
		DigitFont font{ &engine };
		RunFrames(engine, 300, [&]() {
			font.Draw("143", 65, 225, true);
			font.Draw("143", 93, 231);
			font.Draw("000", 151, 231);
			font.Draw("200", 209, 231);
			font.Draw("35 40", 231, 239);
		});
		CheckEq(AllText(engine), "", "numbers not found together in memory are not read");
	}
	// Equal images that are not digits: nothing in memory answers them.
	{
		FakeMachine machine;
		Engine engine(&machine);
		engine.SetCaptureAllSprites(true);
		WriteValues(machine, FakeMachine::BASE + 0x4000, { 143, 0, 0, 143, 0, 200 });
		DigitFont font{ &engine };
		RunFrames(engine, 300, [&]() {
			font.Draw("579", 65, 225);
			font.Draw("975", 93, 231);
			font.Draw("759", 151, 231);
		});
		CheckEq(AllText(engine), "", "a strip seen only in part, with no neighbours, is left alone");
	}
}

static void TestNumbersWithoutTheZero() {
	// The strip's first images have not been drawn yet: the lowest one seen
	// is the 2. Memory says which digit it is.
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteValues(machine, FakeMachine::BASE + 0x4000, { 234, 432, 0, 342 });
	DigitFont font{ &engine };
	RunFrames(engine, 300, [&]() {
		font.Draw("234", 65, 225);
		font.Draw("432", 93, 245);
		font.Draw("342", 151, 265);
	});
	std::string all = AllText(engine);
	Check(all.find("234") != std::string::npos && all.find("432") != std::string::npos && all.find("342") != std::string::npos,
		"a strip whose first digits were never drawn is still read");
	printf("    (without the zero: %s)\n", all.c_str());
}

// A second game started in the same emulator session. The frame count starts
// again at zero; the list of places text could be, made for the first game,
// must not be kept (seen with Patapon 3 started after Persona 2: no text read).
static void TestSecondGameInOneSession() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	WriteWide(machine, FakeMachine::BASE + 0x2000, "Rather rough pupil", 0x20);
	WriteWide(machine, FakeMachine::BASE + 0x2100, "start without help.", 0x20);
	PagedFont font{ &engine };
	RunFrames(engine, 2000, [&]() {
		font.Draw("Rather rough pupil", 34, 200);
		font.Draw("start without help.", 34, 218);
	});
	Check(AllText(engine).find("rough pupil") != std::string::npos, "the first game's text is read");
	engine.Reset();
	engine.SetCaptureAllSprites(true);
	WriteWide(machine, FakeMachine::BASE + 0x9000, "Beat the drums together", 0x20);
	WriteWide(machine, FakeMachine::BASE + 0x9100, "when the light shines!", 0x20);
	RunFrames(engine, 300, [&]() {
		font.Draw("Beat the drums together", 34, 200);
		font.Draw("when the light shines!", 34, 218);
	});
	std::string all = AllText(engine);
	Check(all.find("Beat the drums together") != std::string::npos, "a second game in the same session is read");
	printf("    (second game: %s)\n", all.c_str());
}

// ---- recordings ----

// A recording kept in memory: what the engine writes can be read straight back.
class MemoryFolder : public RecordingSink, public RecordingSource {
public:
	bool Append(const std::string &file, const void *data, size_t size) override {
		files[file].append((const char *)data, size);
		return true;
	}
	bool ReadAll(const std::string &file, std::string *out) override {
		auto it = files.find(file);
		if (it == files.end()) {
			return false;
		}
		*out = it->second;
		return true;
	}
	std::map<std::string, std::string> files;
};

// A recording folder on disk.
class DiskFolder : public RecordingSource {
public:
	explicit DiskFolder(const std::string &folder) : folder_(folder) {}
	bool ReadAll(const std::string &file, std::string *out) override {
		std::ifstream in(folder_ + "/" + file, std::ios::binary);
		if (!in) {
			return false;
		}
		std::ostringstream buffer;
		buffer << in.rdbuf();
		*out = buffer.str();
		return true;
	}

private:
	std::string folder_;
};

static void TestRecordingRoundTrip() {
	FakeMachine machine;
	Engine engine(&machine);
	engine.SetCaptureAllSprites(true);
	const char *help = "You may type up to 12 letters here, no more.";
	machine.Write(FakeMachine::BASE + 0x2000, help, (u32)strlen(help));
	OwnFont font{ &engine, 0x09A00000, true };
	engine.OnRenderTarget(0x04000000, 0x88000);

	SpriteDraw odd;
	odd.texAddr = 0x09123456;
	odd.texStride = 64;
	odd.texBpp = 8;
	odd.texW = 64;
	odd.texH = 32;
	odd.texFormat = 5;
	odd.fromSurface = true;
	odd.clutAddr = 0x08ABCDEF;
	odd.u0 = 0.5f; odd.v0 = 1.25f; odd.u1 = 63.3333321f; odd.v1 = 31.1f;
	odd.screen = RectF{ -3.75f, 10.1f, 200.123f, 99.9f };
	odd.target = 0x04088000;
	odd.dataAddr = 0x08901234;
	SpriteDraw back;
	Check(ParseSprite(FormatSprite(odd).c_str(), &back) && back.texAddr == odd.texAddr && back.texStride == odd.texStride
		&& back.texBpp == odd.texBpp && back.texW == odd.texW && back.texH == odd.texH && back.texFormat == odd.texFormat
		&& back.fromSurface && back.clutAddr == odd.clutAddr && back.u1 == odd.u1 && back.v0 == odd.v0
		&& back.screen.x0 == odd.screen.x0 && back.screen.x1 == odd.screen.x1 && back.screen.y1 == odd.screen.y1
		&& back.target == odd.target && back.dataAddr == odd.dataAddr, "a sprite line reads back exactly");

	MemoryFolder folder;
	Check(engine.StartRecording(&folder, 45, "round trip test"), "a recording starts");
	Check(!engine.StartRecording(&folder, 45, "second"), "a second recording is refused while one runs");
	DrawNote note;
	note.prim = 6;
	note.primName = "rectangles";
	note.textured = true;
	note.texAddr = 0x09A00000;
	note.vertices = 2;
	note.rectangles = 1;
	engine.OnDrawNote(note);
	RunFrames(engine, 45, [&]() { font.Draw(help, 10, 0); });
	Check(!engine.IsRecording() && engine.RecordedFrames() == 45, "the recording ends by itself after its frames");
	CheckEq(AllText(engine), help, "the text was read while recording");

	const std::string &summary = folder.files["summary.txt"];
	const std::string &timeline = folder.files["timeline.txt"];
	Check(summary.find(help) != std::string::npos && summary.find("FONT TABLES") != std::string::npos
		&& summary.find("code 0x59 \"Y\"") != std::string::npos, "the summary has the text and the font table");
	Check(summary.find("rectangles, texture 0x09A00000") != std::string::npos, "the summary has the draw notes");
	Check(timeline.find("found in memory at 0x08802000") != std::string::npos, "the timeline has the memory search and its answer");
	Check(timeline.find("  ? x 10") != std::string::npos, "the timeline has the row while it was unread");
	Check(folder.files["sprites.txt"].find(" same\n") != std::string::npos, "frames that are the same are written once");
	Check(folder.files["memory-0.bin"].size() == 0x10000, "the memory copy is whole");

	// Reading it back and running a fresh engine on it gives the same text.
	RecordedMachine recorded;
	std::string error;
	Check(LoadRecording(&folder, &recorded, &error), "the recording loads");
	Check(!recorded.frames.empty() && recorded.frames[0].targets.size() == 1 && recorded.frames[0].targets[0].addr == 0x04000000
		&& recorded.frames[0].targets[0].bytes == 0x88000, "the surfaces drawn into read back");
	Check(recorded.game == "TEST00001" && recorded.note == "round trip test" && recorded.frames.size() == 45
		&& recorded.regions.size() == 1 && recorded.regions[0].name == "test RAM", "the manifest reads back");
	Engine replay(&recorded);
	replay.SetCaptureAllSprites(true);
	for (const RecordedFrame &f : recorded.frames) {
		for (const RenderTarget &t : f.targets) {
			replay.OnRenderTarget(t.addr, t.bytes);
		}
		for (const SpriteDraw &d : f.sprites) {
			replay.OnSpriteDraw(d);
		}
		replay.OnFrameEnd();
	}
	CheckEq(AllText(replay), help, "a replay of the recording reads the same text");

	MemoryFolder empty;
	Check(!LoadRecording(&empty, &recorded, &error) && !error.empty(), "an empty folder is refused with a reason");
}

// Runs the engine on a recording folder and prints what it reads:
//   CoreTests --replay <folder> [--extra N] [--find "text"]...
// --extra repeats the last frame N more times, to give the memory searches
// time (default 1200); --find looks for a string in the recorded memory.
static int Replay(const std::string &folder, int extra, const std::vector<std::string> &finds) {
	DiskFolder disk(folder);
	RecordedMachine recorded;
	std::string error;
	if (!LoadRecording(&disk, &recorded, &error)) {
		printf("Could not load the recording in %s: %s\n", folder.c_str(), error.c_str());
		return 2;
	}
	printf("Replay of %s (%s), %d frames recorded, then %d more of the last frame.\n%s%s\n",
		folder.c_str(), recorded.game.c_str(), (int)recorded.frames.size(), extra,
		recorded.note.empty() ? "" : "Note: ", recorded.note.c_str());
	Engine engine(&recorded);
	engine.SetCaptureAllSprites(true);
	std::set<std::string> seen;
	std::string lastSelection;
	printf("\nTEXT AS IT IS FIRST READ\n");
	int total = (int)recorded.frames.size() + (recorded.frames.empty() ? 0 : extra);
	for (int i = 0; i < total; i++) {
		const RecordedFrame &f = recorded.frames[std::min(i, (int)recorded.frames.size() - 1)];
		for (const RenderTarget &t : f.targets) {
			engine.OnRenderTarget(t.addr, t.bytes);
		}
		for (const SpriteDraw &d : f.sprites) {
			engine.OnSpriteDraw(d);
		}
		engine.OnFrameEnd();
		if (engine.Selected().text != lastSelection) {
			lastSelection = engine.Selected().text;
			printf("  replay frame %d (recorded frame %d): SELECTED by %s: %s\n", i + 1, f.frame,
				engine.Selected().known ? engine.Selected().how : "nothing", lastSelection.empty() ? "(not known)" : lastSelection.c_str());
		}
		for (const TextRun &run : engine.Screen().Runs()) {
			if (seen.insert(run.text).second) {
				printf("  replay frame %d (recorded frame %d): [%.2f, x %.0f, y %.0f, texture 0x%08X] %s\n",
					i + 1, f.frame, run.confidence, run.rect.x0, run.rect.y0, run.bufferAddr, run.text.c_str());
			}
		}
	}
	if (seen.empty()) {
		printf("  none\n");
	}
	printf("\n%s", engine.InspectionReport().c_str());
	for (const std::string &find : finds) {
		std::vector<TextLocation> hits = engine.Locate(find, 32);
		printf("\nFIND \"%s\": %d places\n", find.c_str(), (int)hits.size());
		for (const TextLocation &hit : hits) {
			printf("  0x%08X as %s\n", hit.addr, hit.encoding.c_str());
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	if (argc >= 3 && !strcmp(argv[1], "--replay")) {
		int extra = 1200;
		std::vector<std::string> finds;
		for (int i = 3; i + 1 < argc; i += 2) {
			if (!strcmp(argv[i], "--extra")) {
				extra = std::max(atoi(argv[i + 1]), 0);
			} else if (!strcmp(argv[i], "--find")) {
				finds.push_back(argv[i + 1]);
			}
		}
		return Replay(argv[2], extra, finds);
	}
	TestTextUtil();
	TestLedgerOverwrite();
	TestLedgerAndAssembly();
	TestStringStrip();
	TestSystemDialog();
	TestCallStreamFallback();
	TestLocate();
	TestGlyphCipherGridFont();
	TestGlyphCipherPackedFont();
	TestGlyphCipherRefusesGuesses();
	TestGlyphCipherIgnoresNonLetters();
	TestHarness();
	TestFontInVideoMemory();
	TestCipherFindsWords();
	TestCipherWordsRefuseGuesses();
	TestFontOverTwoTextures();
	TestGameWithItsOwnSpaceCode();
	TestLooseSearchRefusesGuesses();
	TestTextInOwnLetterCode();
	TestOwnLetterCodeRefusesGuesses();
	TestProfileRemembersAFont();
	TestProfileRemembersALetterCode();
	TestSelectionByMark();
	TestSelectionByPlace();
	TestSelectionByColour();
	TestSelectionRefusesPassersBy();
	TestNumbersFromDigitStrip();
	TestNumbersRefuseGuesses();
	TestNumbersWithoutTheZero();
	TestSecondGameInOneSession();
	TestRecordingRoundTrip();
	printf("CORE TESTS: %d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
