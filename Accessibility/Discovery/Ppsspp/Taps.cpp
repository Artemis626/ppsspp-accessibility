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

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "Accessibility/Discovery/Core/Engine.h"
#include "Accessibility/Discovery/Ppsspp/DiscoveryHost.h"
#include "Accessibility/Discovery/Ppsspp/Taps.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Core/HLE/HLE.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/SaveState.h"
#include "Core/Util/PPGeDraw.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

namespace Access {
namespace DiscoveryHost {

namespace {

::Discovery::Engine *g_engine = nullptr;

// The PSP maps the same memory at several addresses (the "uncached" mirror
// at 0x4xxxxxxx). One form everywhere, so ledger and draws agree.
u32 Plain(u32 addr) {
	return addr & 0x3FFFFFFF;
}

// ---- system-call tap -------------------------------------------------------

enum class Call {
	None,
	CountOnly,         // Interesting for the survey, nothing to read.
	FontCharInfo,      // sceFontGetCharInfo(font, charCode, infoPtr)
	FontImageRect,     // sceFontGetCharImageRect(font, charCode, rectPtr)
	FontGlyphImage,    // sceFontGetCharGlyphImage[_Clip](font, charCode, glyphImagePtr, ...)
	CccFromUtf8,       // sceCccUTF8to...(dst, dstSize, src)
	CccFromUtf16,
	CccFromSjis,
	DialogOpened,      // sceUtility...InitStart
};

struct CallInfo {
	Call kind = Call::None;
	std::string counter;  // Survey counter name.
};

// PPSSPP describes each system function with one HLEFunction that lives for
// the whole run, so its address identifies the function.
std::unordered_map<const HLEFunction *, CallInfo> g_calls;

// Arguments saved before the call (the call overwrites the registers).
struct SavedArgs {
	const HLEFunction *info = nullptr;
	u32 a[4] = {};
	u32 ra = 0;
} g_saved;

// What the game learned about a glyph's size from sceFontGetCharInfo, kept so
// the ledger knows how big the glyph drawn a moment later is.
struct Metrics {
	u16 w = 0, h = 0;
	s16 left = 0;
	float advance = 0;
};
std::unordered_map<u64, Metrics> g_metrics;  // (font handle << 16) | char code

bool StartsWith(const char *s, const char *prefix) {
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool EndsWith(const char *s, const char *suffix) {
	size_t n = strlen(s), m = strlen(suffix);
	return n >= m && strcmp(s + n - m, suffix) == 0;
}

CallInfo Classify(const HLEFunction *info) {
	CallInfo c;
	const char *name = info->name;
	if (!name) {
		return c;
	}
	if (StartsWith(name, "sceFont")) {
		c.kind = Call::CountOnly;
		if (!strcmp(name, "sceFontGetCharInfo")) c.kind = Call::FontCharInfo;
		else if (!strcmp(name, "sceFontGetCharImageRect")) c.kind = Call::FontImageRect;
		else if (!strcmp(name, "sceFontGetCharGlyphImage") || !strcmp(name, "sceFontGetCharGlyphImage_Clip")) c.kind = Call::FontGlyphImage;
	} else if (StartsWith(name, "sceCcc")) {
		c.kind = Call::CountOnly;
		if (StartsWith(name, "sceCccUTF8to")) c.kind = Call::CccFromUtf8;
		else if (StartsWith(name, "sceCccUTF16to")) c.kind = Call::CccFromUtf16;
		else if (StartsWith(name, "sceCccSJISto")) c.kind = Call::CccFromSjis;
	} else if (StartsWith(name, "sceUtility") && EndsWith(name, "InitStart")) {
		c.kind = Call::DialogOpened;
	}
	if (c.kind != Call::None) {
		c.counter = std::string("call.") + name;
	}
	return c;
}

// Reads a zero-terminated string of 8-bit units from PSP memory.
std::string ReadBytes(u32 addr, size_t maxLen) {
	std::string out;
	while (out.size() < maxLen && Memory::IsValidAddress(addr)) {
		char c = (char)Memory::ReadUnchecked_U8(addr++);
		if (!c) {
			break;
		}
		out += c;
	}
	return out;
}

std::string ReadUtf16(u32 addr, size_t maxLen) {
	std::u16string wide;
	while (wide.size() < maxLen && Memory::IsValidRange(addr, 2)) {
		char16_t c = (char16_t)Memory::ReadUnchecked_U16(addr);
		addr += 2;
		if (!c) {
			break;
		}
		wide += c;
	}
	return ConvertUCS2ToUTF8(wide);
}

void OnFontGlyphImage(u32 fontHandle, u32 charCode, u32 glyphImagePtr, u32 ra) {
	// struct GlyphImage (Core/Font/PGF.h): pixelFormat, xPos64, yPos64,
	// bufWidth, bufHeight, bytesPerLine, pad, bufferPtr.
	if (!Memory::IsValidRange(glyphImagePtr, 24)) {
		return;
	}
	u32 pixelFormat = Memory::ReadUnchecked_U32(glyphImagePtr);
	s32 xPos64 = (s32)Memory::ReadUnchecked_U32(glyphImagePtr + 4);
	s32 yPos64 = (s32)Memory::ReadUnchecked_U32(glyphImagePtr + 8);
	u32 bytesPerLine = Memory::ReadUnchecked_U16(glyphImagePtr + 16);
	u32 bufferPtr = Memory::ReadUnchecked_U32(glyphImagePtr + 20);
	int bpp;
	switch (pixelFormat) {
	case 0: case 1: bpp = 4; break;   // PSP_FONT_PIXELFORMAT_4, _4_REV
	case 2: bpp = 8; break;
	case 3: bpp = 24; break;
	case 4: bpp = 32; break;
	default: return;
	}
	int x = std::max(xPos64 >> 6, 0);
	int y = std::max(yPos64 >> 6, 0);
	if (bytesPerLine == 0 || !Memory::IsValidAddress(bufferPtr)) {
		return;
	}

	::Discovery::GlyphCell cell;
	cell.addr = Plain(bufferPtr) + (u32)y * bytesPerLine + (u32)x * bpp / 8;
	cell.stride = bytesPerLine;
	cell.bpp = (u8)bpp;
	cell.codepoint = charCode & 0xFFFF;
	cell.pc = ra;
	auto it = g_metrics.find(((u64)fontHandle << 16) | (charCode & 0xFFFF));
	if (it != g_metrics.end()) {
		cell.w = it->second.w;
		cell.h = it->second.h;
		cell.left = it->second.left;
		cell.advance = it->second.advance;
	}
	g_engine->OnGlyphRendered(cell);
}

void OnSyscall(const HLEFunction *info, bool after) {
	auto it = g_calls.find(info);
	if (it == g_calls.end()) {
		it = g_calls.emplace(info, Classify(info)).first;
	}
	const CallInfo &call = it->second;
	if (call.kind == Call::None || !g_engine) {
		return;
	}
	if (!after) {
		g_saved.info = info;
		for (int i = 0; i < 4; i++) {
			g_saved.a[i] = currentMIPS->r[MIPS_REG_A0 + i];
		}
		// The game calls a small stub that makes the system call, so the
		// return address is the place in the game's own code that asked.
		g_saved.ra = currentMIPS->r[MIPS_REG_RA];
		return;
	}
	if (g_saved.info != info) {
		return;  // Not the call we saved arguments for.
	}
	g_saved.info = nullptr;
	const u32 *a = g_saved.a;
	u32 result = currentMIPS->r[MIPS_REG_V0];
	::Discovery::Survey &survey = g_engine->GetSurvey();
	survey.Count(call.counter);

	switch (call.kind) {
	case Call::FontCharInfo:
		survey.Count("font.chars_queried");
		// struct PGFCharInfo: bitmapWidth, bitmapHeight, bitmapLeft, bitmapTop,
		// then ten 26.6 fixed-point metrics; sfp26AdvanceH is the 13th field.
		if (result == 0 && Memory::IsValidRange(a[2], 56)) {
			Metrics m;
			m.w = (u16)std::min(Memory::ReadUnchecked_U32(a[2]), 1024u);
			m.h = (u16)std::min(Memory::ReadUnchecked_U32(a[2] + 4), 1024u);
			m.left = (s16)(s32)Memory::ReadUnchecked_U32(a[2] + 8);
			m.advance = (float)(s32)Memory::ReadUnchecked_U32(a[2] + 48) / 64.0f;
			if (g_metrics.size() > 50000) {
				g_metrics.clear();
			}
			g_metrics[((u64)a[0] << 16) | (a[1] & 0xFFFF)] = m;
		}
		break;
	case Call::FontImageRect:
		if (result == 0 && Memory::IsValidRange(a[2], 4)) {
			Metrics &m = g_metrics[((u64)a[0] << 16) | (a[1] & 0xFFFF)];
			if (m.w == 0) {
				m.w = Memory::ReadUnchecked_U16(a[2]);
				m.h = Memory::ReadUnchecked_U16(a[2] + 2);
			}
		}
		break;
	case Call::FontGlyphImage:
		if (result == 0) {
			OnFontGlyphImage(a[0], a[1], a[2], g_saved.ra);
		}
		break;
	case Call::CccFromUtf8:
		g_engine->OnAnchorText(info->name, ReadBytes(a[2], 256));
		break;
	case Call::CccFromUtf16:
		g_engine->OnAnchorText(info->name, ReadUtf16(a[2], 256));
		break;
	case Call::CccFromSjis: {
		// Shift-JIS needs a conversion table the adapter does not have yet.
		// Plain ASCII is the same in Shift-JIS, so keep those as samples.
		std::string raw = ReadBytes(a[2], 256);
		bool ascii = std::all_of(raw.begin(), raw.end(), [](char c) { return (unsigned char)c < 0x80; });
		g_engine->OnAnchorText(info->name, ascii ? raw : "(Shift-JIS text, " + std::to_string(raw.size()) + " bytes)");
		break;
	}
	case Call::DialogOpened:
		survey.Count("dialog.opened");
		break;
	default:
		break;
	}
}

// ---- dialog tap ------------------------------------------------------------

void OnPPGe(int kind, std::string_view text, float x, float y, float x2, float y2, const PPGeStyle *style) {
	if (!g_engine) {
		return;
	}
	if (kind == 2) {
		::Discovery::RectF r{ std::min(x, x2), std::min(y, y2), std::max(x, x2), std::max(y, y2) };
		g_engine->OnSystemHighlight(r);
		return;
	}
	// x, y is an anchor point; the style says which corner of the text it is.
	float w = 0, h = 0;
	float scale = style ? style->scale : 1.0f;
	if (kind == 1) {
		PPGeMeasureText(&w, &h, text, scale, PPGE_LINE_WRAP_WORD, (int)x2);
	} else {
		PPGeMeasureText(&w, &h, text, scale);
	}
	int align = style ? (int)style->align : 0;
	::Discovery::RectF r;
	r.x0 = x;
	if (align & (int)PPGeAlign::BOX_HCENTER) r.x0 = x - w * 0.5f;
	else if (align & (int)PPGeAlign::BOX_RIGHT) r.x0 = x - w;
	r.y0 = y;
	if (align & (int)PPGeAlign::BOX_VCENTER) r.y0 = y - h * 0.5f;
	else if (align & (int)PPGeAlign::BOX_BOTTOM) r.y0 = y - h;
	r.x1 = r.x0 + std::max(w, 1.0f);
	r.y1 = r.y0 + std::max(h, 1.0f);
	g_engine->OnSystemText(text, r);
}

// ---- GE tap ----------------------------------------------------------------

std::vector<GPUDebugVertex> g_vertices;
std::vector<u16> g_indices;
int g_rectanglesEmitted = 0;  // By the draw being handled.
u32 g_lastTarget = 0;         // The surface last reported to the engine.

const char *PrimName(GEPrimitiveType prim) {
	switch (prim) {
	case GE_PRIM_POINTS: return "points";
	case GE_PRIM_LINES: return "lines";
	case GE_PRIM_LINE_STRIP: return "line strip";
	case GE_PRIM_TRIANGLES: return "triangles";
	case GE_PRIM_TRIANGLE_STRIP: return "triangle strip";
	case GE_PRIM_TRIANGLE_FAN: return "triangle fan";
	case GE_PRIM_RECTANGLES: return "rectangles";
	default: return "other";
	}
}

int BitsPerTexel(GETextureFormat format) {
	switch (format) {
	case GE_TFMT_CLUT4: return 4;
	case GE_TFMT_CLUT8: return 8;
	case GE_TFMT_5650: case GE_TFMT_5551: case GE_TFMT_4444: case GE_TFMT_CLUT16: return 16;
	case GE_TFMT_8888: case GE_TFMT_CLUT32: return 32;
	default: return 0;  // Compressed formats: the font library cannot draw into those.
	}
}

// Reports the rectangle covered by a group of vertices of the current draw.
void EmitQuad(const ::Discovery::SpriteDraw &base, const int *order, int n) {
	::Discovery::SpriteDraw d = base;
	bool first = true;
	for (int k = 0; k < n; k++) {
		int i = order[k];
		if (i < 0 || i >= (int)g_vertices.size()) {
			return;
		}
		const GPUDebugVertex &v = g_vertices[i];
		if (first) {
			d.u0 = d.u1 = v.u;
			d.v0 = d.v1 = v.v;
			d.screen = ::Discovery::RectF{ v.x, v.y, v.x, v.y };
			first = false;
		} else {
			d.u0 = std::min(d.u0, v.u); d.u1 = std::max(d.u1, v.u);
			d.v0 = std::min(d.v0, v.v); d.v1 = std::max(d.v1, v.v);
			d.screen.x0 = std::min(d.screen.x0, v.x); d.screen.x1 = std::max(d.screen.x1, v.x);
			d.screen.y0 = std::min(d.screen.y0, v.y); d.screen.y1 = std::max(d.screen.y1, v.y);
		}
	}
	// The colour the game gave the draw. For a rectangle of two corners the
	// second one carries it; for the other kinds every corner does.
	if (n > 0) {
		const GPUDebugVertex &last = g_vertices[order[n - 1]];
		d.color = (u32)last.c[0] | ((u32)last.c[1] << 8) | ((u32)last.c[2] << 16) | ((u32)last.c[3] << 24);
		// Corners without a colour of their own take the colour set for
		// the whole draw (the "material" colour).
		if ((gstate.vertType & GE_VTYPE_COL_MASK) == 0) {
			d.color = gstate.getMaterialAmbientRGBA();
		}
	}
	g_engine->OnSpriteDraw(d);
	g_rectanglesEmitted++;
}

void HandlePrim(GEPrimitiveType prim, int count, bool sampling);

void OnGeCommand(u32 pc, u32 op) {
	if ((op >> 24) != GE_CMD_PRIM || !g_engine) {
		return;
	}
	const GEPrimitiveType prim = (GEPrimitiveType)((op >> 16) & 7);
	const int count = op & 0xFFFF;
	bool sampling = SurveySampling();
	if (sampling) {
		g_engine->GetSurvey().Count("draw.prims");
	}
	// Which memory the game is drawing into. The engine needs it to tell a
	// screen copy used as a texture from a picture merely kept in video memory.
	u32 target = Plain(gstate.getFrameBufAddress());
	if (target != g_lastTarget) {
		g_lastTarget = target;
		u32 bytesPerPixel = gstate.FrameBufFormat() == GE_FORMAT_8888 ? 4 : 2;
		g_engine->OnRenderTarget(target, (u32)gstate.FrameBufStride() * 272 * bytesPerPixel);
	}
	g_rectanglesEmitted = 0;
	HandlePrim(prim, count, sampling);
	if (g_engine->IsRecording()) {
		// Every draw is noted, also those the tap made nothing of: a recording
		// must show whether a game draws its text in a way the tap misses.
		::Discovery::DrawNote note;
		note.prim = (u8)prim;
		note.primName = PrimName(prim);
		note.clear = gstate.isModeClear();
		note.textured = gstate.isTextureMapEnabled() && !note.clear;
		note.texAddr = Plain(gstate.getTextureAddress(0));
		note.texW = (u16)gstate.getTextureWidth(0);
		note.texH = (u16)gstate.getTextureHeight(0);
		note.texFormat = (u8)gstate.getTextureFormat();
		note.vertices = (u32)count;
		note.rectangles = g_rectanglesEmitted;
		g_engine->OnDrawNote(note);
	}
}

void HandlePrim(GEPrimitiveType prim, int count, bool sampling) {
	if (count < 2 || count > 6000 || gstate.isModeClear() || !gstate.isTextureMapEnabled()) {
		return;
	}
	if (sampling && prim == GE_PRIM_RECTANGLES) {
		g_engine->GetSurvey().Count("draw.textured_rect_prims");
	}

	GETextureFormat format = gstate.getTextureFormat();
	int bpp = BitsPerTexel(format);
	u32 texAddr = gstate.getTextureAddress(0);
	u32 stride = GetTextureBufw(0, texAddr, format) * bpp / 8;
	u32 texBytes = stride * (u32)gstate.getTextureHeight(0);
	// The font-library ledger can only be matched against plain (not
	// rearranged, not compressed) textures. The general draw stream takes
	// every texture: there a glyph is identified by address and rectangle only.
	bool linear = bpp != 0 && stride != 0 && !gstate.isTextureSwizzled();
	bool forLedger = linear && g_engine->WantsTexture(Plain(texAddr), texBytes);
	if (!forLedger && !g_engine->CapturesAllSprites()) {
		return;
	}
	if (!gpuDebug || !gpuDebug->GetCurrentDrawAsDebugVertices(count, g_vertices, g_indices)) {
		return;
	}

	::Discovery::SpriteDraw base;
	base.texAddr = Plain(texAddr);
	base.texStride = linear ? stride : 0;
	base.texBpp = (u8)bpp;
	base.texW = (u16)gstate.getTextureWidth(0);
	base.texH = (u16)gstate.getTextureHeight(0);
	base.texFormat = (u8)format;
	// PSP video memory: a texture there is something the game drew earlier.
	base.fromSurface = (Plain(texAddr) & 0x3F800000) == 0x04000000;
	base.clutAddr = (format >= GE_TFMT_CLUT4 && format <= GE_TFMT_CLUT32) ? Plain(gstate.getClutAddress()) : 0;
	base.target = Plain(gstate.getFrameBufAddress());
	base.dataAddr = gstate_c.vertexAddr;

	// The order the vertices are used in: through the index list, if any.
	auto at = [&](int i) -> int {
		return g_indices.empty() ? i : (i < (int)g_indices.size() ? g_indices[i] : -1);
	};
	int order[6];
	switch (prim) {
	case GE_PRIM_RECTANGLES:  // Two corners per rectangle.
		for (int i = 0; i + 1 < count; i += 2) {
			order[0] = at(i); order[1] = at(i + 1);
			EmitQuad(base, order, 2);
		}
		break;
	case GE_PRIM_TRIANGLES:  // Two triangles per rectangle.
		if (count % 6 == 0) {
			for (int i = 0; i + 5 < count; i += 6) {
				for (int k = 0; k < 6; k++) order[k] = at(i + k);
				EmitQuad(base, order, 6);
			}
		}
		break;
	case GE_PRIM_TRIANGLE_STRIP:
	case GE_PRIM_TRIANGLE_FAN:
		if (count == 4) {
			for (int k = 0; k < 4; k++) order[k] = at(k);
			EmitQuad(base, order, 4);
		} else if (prim == GE_PRIM_TRIANGLE_STRIP && (count + 2) % 6 == 0) {
			// Rectangles chained in one strip: four corners, then two
			// repeated vertices to jump to the next rectangle.
			for (int i = 0; i + 3 < count; i += 6) {
				for (int k = 0; k < 4; k++) order[k] = at(i + k);
				EmitQuad(base, order, 4);
			}
		}
		break;
	default:
		break;
	}
}

// ---- state tap -------------------------------------------------------------

void OnState(const Path &filename, bool loaded) {
	OnStateFile(filename.ToString(), loaded);
}

}  // namespace

void InstallTaps(::Discovery::Engine *engine) {
	g_engine = engine;
	g_metrics.clear();
	g_saved = SavedArgs();
	g_accessSyscallHook = &OnSyscall;
	g_accessPPGeHook = &OnPPGe;
	SaveState::g_accessStateHook = &OnState;
}

void RemoveTaps() {
	g_accessSyscallHook = nullptr;
	g_accessPPGeHook = nullptr;
	g_accessGeHook = nullptr;
	SaveState::g_accessStateHook = nullptr;
	g_engine = nullptr;
	g_metrics.clear();
}

void ResetTapState() {
	g_lastTarget = 0;
	g_metrics.clear();
	g_saved = SavedArgs();
}

void SetGeTapEnabled(bool enabled) {
	g_accessGeHook = (enabled && g_engine) ? &OnGeCommand : nullptr;
}

bool GeTapEnabled() {
	return g_accessGeHook != nullptr;
}

}  // namespace DiscoveryHost
}  // namespace Access
