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

// The "memory" table: reading and writing PSP memory.
//
// Addresses are absolute PSP addresses, as shown in PPSSPP's debugger
// (user RAM starts at 0x08800000). The PSP is little-endian, so read_u16 and
// friends are little-endian; the _be variants exist for data a game stores
// big-endian. Reading an invalid address returns nil and writes a warning to
// the log once per address, rather than raising an error, so one wrong
// address doesn't stop a whole script.

#include "ppsspp_config.h"

#include <cstring>
#include <string>
#include <unordered_set>

#include "Accessibility/Log.h"
#include "Accessibility/Scripting/LuaInternal.h"
#include "Common/CommonTypes.h"
#include "Common/StringUtils.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#endif

namespace Access {
namespace Script {

namespace {

const u32 CWCHEAT_BASE = 0x08800000;
const lua_Integer MAX_RANGE = 1 << 20;   // readbyterange limit (1 MB)
const lua_Integer MAX_STRING = 65536;    // readstring limit
const size_t MAX_WARNED_ADDRESSES = 200;

std::unordered_set<u32> g_warnedAddresses;

u32 CheckAddress(lua_State *L, int arg) {
	// Wraps to 32 bits, so 0x08800000 and negative values from read_s32 both work.
	return (u32)luaL_checkinteger(L, arg);
}

void WarnInvalid(u32 address, u32 size, bool write = false) {
	if (g_warnedAddresses.size() >= MAX_WARNED_ADDRESSES || !g_warnedAddresses.insert(address).second) {
		return;
	}
	Log::Warn(StringFromFormat(write ? "memory write of %u byte(s) at invalid address 0x%08X was ignored. (Shown once per address.)" : "memory read of %u byte(s) at invalid address 0x%08X returned nil. (Shown once per address.)", size, address));
}

bool ReadRaw(u32 address, void *dest, u32 size) {
	if (!Memory::IsValidRange(address, size)) {
		WarnInvalid(address, size);
		return false;
	}
	memcpy(dest, Memory::GetPointerUnchecked(address), size);
	return true;
}

template <typename T>
T ByteSwap(T value) {
	T out;
	const u8 *src = (const u8 *)&value;
	u8 *dst = (u8 *)&out;
	for (size_t i = 0; i < sizeof(T); i++) {
		dst[i] = src[sizeof(T) - 1 - i];
	}
	return out;
}

// One template for all the integer readers: read_u8 ... read_s32_be.
template <typename T, bool BE>
int L_ReadInt(lua_State *L) {
	u32 address = CheckAddress(L, 1);
	T value;
	if (!ReadRaw(address, &value, sizeof(T))) {
		lua_pushnil(L);
		return 1;
	}
	if (BE) {
		value = ByteSwap(value);
	}
	lua_pushinteger(L, (lua_Integer)value);
	return 1;
}

template <bool BE>
int L_ReadFloat(lua_State *L) {
	u32 address = CheckAddress(L, 1);
	u32 bits;
	if (!ReadRaw(address, &bits, 4)) {
		lua_pushnil(L);
		return 1;
	}
	if (BE) {
		bits = ByteSwap(bits);
	}
	float f;
	memcpy(&f, &bits, 4);
	lua_pushnumber(L, f);
	return 1;
}

// memory.readbyterange(address, length) -> table of bytes, indexed from 1
int L_ReadByteRange(lua_State *L) {
	u32 address = CheckAddress(L, 1);
	lua_Integer length = luaL_checkinteger(L, 2);
	luaL_argcheck(L, length >= 0 && length <= MAX_RANGE, 2, "length must be between 0 and 1048576");
	if (length > 0 && !Memory::IsValidRange(address, (u32)length)) {
		WarnInvalid(address, (u32)length);
		lua_pushnil(L);
		return 1;
	}
	const u8 *src = length > 0 ? Memory::GetPointerUnchecked(address) : nullptr;
	lua_createtable(L, (int)length, 0);
	for (lua_Integer i = 0; i < length; i++) {
		lua_pushinteger(L, src[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// ---- writes ----
//
// Scripts write straight into PSP memory. That means:
//  - writes made by a script do NOT trigger the script's own onmemorywrite
//    callbacks (no feedback loops);
//  - after writing, any code the JIT compiled from those bytes is thrown
//    away, the same way PPSSPP's own memory copy does, so code patches work.

bool WriteRaw(u32 address, const void *src, u32 size) {
	if (!Memory::IsValidRange(address, size)) {
		WarnInvalid(address, size, true);
		return false;
	}
	memcpy(Memory::GetPointerWriteUnchecked(address), src, size);
	mipsr4k.InvalidateICache(address & ~3, (int)((size + (address & 3) + 3) & ~3));
	return true;
}

// write_u8 ... write_u32_be: value is truncated to the size, so signed and
// unsigned writes are the same operation (write_s16(a, -1) writes 0xFFFF).
template <typename T, bool BE>
int L_WriteInt(lua_State *L) {
	u32 address = CheckAddress(L, 1);
	T value = (T)luaL_checkinteger(L, 2);
	if (BE) {
		value = ByteSwap(value);
	}
	lua_pushboolean(L, WriteRaw(address, &value, sizeof(T)));
	return 1;
}

template <bool BE>
int L_WriteFloat(lua_State *L) {
	u32 address = CheckAddress(L, 1);
	float f = (float)luaL_checknumber(L, 2);
	u32 bits;
	memcpy(&bits, &f, 4);
	if (BE) {
		bits = ByteSwap(bits);
	}
	lua_pushboolean(L, WriteRaw(address, &bits, 4));
	return 1;
}

// memory.writebyterange(address, bytes) - bytes is a table indexed from 1.
int L_WriteByteRange(lua_State *L) {
	u32 address = CheckAddress(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	lua_Integer length = (lua_Integer)lua_rawlen(L, 2);
	luaL_argcheck(L, length <= MAX_RANGE, 2, "at most 1048576 bytes");
	if (length == 0) {
		lua_pushboolean(L, 1);
		return 1;
	}
	if (!Memory::IsValidRange(address, (u32)length)) {
		WarnInvalid(address, (u32)length, true);
		lua_pushboolean(L, 0);
		return 1;
	}
	u8 *dest = Memory::GetPointerWriteUnchecked(address);
	for (lua_Integer i = 0; i < length; i++) {
		lua_rawgeti(L, 2, i + 1);
		int isNumber = 0;
		lua_Integer v = lua_tointegerx(L, -1, &isNumber);
		lua_pop(L, 1);
		if (!isNumber) {
			return luaL_error(L, "writebyterange: entry %d is not a whole number", (int)(i + 1));
		}
		dest[i] = (u8)v;
	}
	mipsr4k.InvalidateICache(address & ~3, (int)((length + (address & 3) + 3) & ~3));
	lua_pushboolean(L, 1);
	return 1;
}

// memory.isvalid(address [, size]) -> boolean
int L_IsValid(lua_State *L) {
	u32 address = CheckAddress(L, 1);
	lua_Integer size = luaL_optinteger(L, 2, 1);
	luaL_argcheck(L, size >= 1, 2, "size must be at least 1");
	lua_pushboolean(L, Memory::IsValidRange(address, (u32)size));
	return 1;
}

// memory.fromcwcheat(offset) -> absolute address
// Accepts a plain offset (0x00123456) or a CWCheat code address (0x20123456).
int L_FromCwCheat(lua_State *L) {
	u32 value = CheckAddress(L, 1);
	lua_pushinteger(L, (lua_Integer)((value & 0x0FFFFFFF) + CWCHEAT_BASE));
	return 1;
}

#if PPSSPP_PLATFORM(WINDOWS)
std::string WideToUtf8(const wchar_t *wide, int count) {
	if (count <= 0) {
		return std::string();
	}
	int needed = WideCharToMultiByte(CP_UTF8, 0, wide, count, nullptr, 0, nullptr, nullptr);
	std::string out(needed, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide, count, &out[0], needed, nullptr, nullptr);
	return out;
}
#endif

enum class Encoding { Ascii, Latin1, Utf8, Sjis, Utf16le };

// memory.readstring(address, maxlength [, encoding]) -> string (UTF-8)
// Stops at the first zero byte (two zero bytes for utf16le) or at maxlength
// bytes. Encodings: "ascii" (default), "latin1", "utf8", "sjis", "utf16le".
// Custom character tables (.tbl files) arrive in Phase 4.
int L_ReadString(lua_State *L) {
	u32 address = CheckAddress(L, 1);
	lua_Integer maxLength = luaL_checkinteger(L, 2);
	luaL_argcheck(L, maxLength >= 1 && maxLength <= MAX_STRING, 2, "maxlength must be between 1 and 65536");
	const char *encodingName = luaL_optstring(L, 3, "ascii");
	Encoding encoding;
	if (!strcmp(encodingName, "ascii")) {
		encoding = Encoding::Ascii;
	} else if (!strcmp(encodingName, "latin1")) {
		encoding = Encoding::Latin1;
	} else if (!strcmp(encodingName, "utf8")) {
		encoding = Encoding::Utf8;
	} else if (!strcmp(encodingName, "sjis")) {
		encoding = Encoding::Sjis;
	} else if (!strcmp(encodingName, "utf16le")) {
		encoding = Encoding::Utf16le;
	} else {
		return luaL_argerror(L, 3, "unknown encoding (use \"ascii\", \"latin1\", \"utf8\", \"sjis\" or \"utf16le\")");
	}
	if (!Memory::IsValidAddress(address)) {
		WarnInvalid(address, 1);
		lua_pushnil(L);
		return 1;
	}

	// No Lua errors can happen below this line (see LuaInternal.h).
	std::string raw;
	const u32 step = encoding == Encoding::Utf16le ? 2 : 1;
	for (u32 i = 0; i + step <= (u32)maxLength; i += step) {
		if (!Memory::IsValidRange(address + i, step)) {
			break;
		}
		const u8 *p = Memory::GetPointerUnchecked(address + i);
		if (p[0] == 0 && (step == 1 || p[1] == 0)) {
			break;
		}
		raw.append((const char *)p, step);
	}

	std::string out;
	switch (encoding) {
	case Encoding::Ascii:
		out.reserve(raw.size());
		for (unsigned char c : raw) {
			out += (c >= 0x20 && c < 0x7F) || c == '\n' || c == '\t' ? (char)c : '?';
		}
		break;
	case Encoding::Latin1:
		for (unsigned char c : raw) {
			if (c < 0x80) {
				out += (char)c;
			} else {
				out += (char)(0xC0 | (c >> 6));
				out += (char)(0x80 | (c & 0x3F));
			}
		}
		break;
	case Encoding::Utf8:
		out = raw;
		break;
#if PPSSPP_PLATFORM(WINDOWS)
	case Encoding::Sjis:
	{
		// Windows code page 932 is Shift-JIS (Microsoft's variant, as used by PSP games).
		int wideCount = MultiByteToWideChar(932, 0, raw.data(), (int)raw.size(), nullptr, 0);
		std::wstring wide(wideCount, L'\0');
		MultiByteToWideChar(932, 0, raw.data(), (int)raw.size(), &wide[0], wideCount);
		out = WideToUtf8(wide.data(), wideCount);
		break;
	}
	case Encoding::Utf16le:
		out = WideToUtf8((const wchar_t *)raw.data(), (int)(raw.size() / 2));
		break;
#else
	default:
		out = raw;
		break;
#endif
	}
	lua_pushlstring(L, out.data(), out.size());
	return 1;
}

}  // namespace

void ResetMemoryApiState() {
	g_warnedAddresses.clear();
}

void RegisterMemoryApi(lua_State *L) {
	static const luaL_Reg funcs[] = {
		// BizHawk names
		{ "read_u8", &L_ReadInt<u8, false> },
		{ "read_s8", &L_ReadInt<s8, false> },
		{ "read_u16", &L_ReadInt<u16, false> },
		{ "read_s16", &L_ReadInt<s16, false> },
		{ "read_u32", &L_ReadInt<u32, false> },
		{ "read_s32", &L_ReadInt<s32, false> },
		{ "read_u16_le", &L_ReadInt<u16, false> },
		{ "read_s16_le", &L_ReadInt<s16, false> },
		{ "read_u32_le", &L_ReadInt<u32, false> },
		{ "read_s32_le", &L_ReadInt<s32, false> },
		{ "read_u16_be", &L_ReadInt<u16, true> },
		{ "read_s16_be", &L_ReadInt<s16, true> },
		{ "read_u32_be", &L_ReadInt<u32, true> },
		{ "read_s32_be", &L_ReadInt<s32, true> },
		{ "read_float", &L_ReadFloat<false> },
		{ "read_float_be", &L_ReadFloat<true> },
		{ "readbyterange", &L_ReadByteRange },
		{ "write_u8", &L_WriteInt<u8, false> },
		{ "write_s8", &L_WriteInt<u8, false> },
		{ "write_u16", &L_WriteInt<u16, false> },
		{ "write_s16", &L_WriteInt<u16, false> },
		{ "write_u32", &L_WriteInt<u32, false> },
		{ "write_s32", &L_WriteInt<u32, false> },
		{ "write_u16_le", &L_WriteInt<u16, false> },
		{ "write_s16_le", &L_WriteInt<u16, false> },
		{ "write_u32_le", &L_WriteInt<u32, false> },
		{ "write_s32_le", &L_WriteInt<u32, false> },
		{ "write_u16_be", &L_WriteInt<u16, true> },
		{ "write_s16_be", &L_WriteInt<u16, true> },
		{ "write_u32_be", &L_WriteInt<u32, true> },
		{ "write_s32_be", &L_WriteInt<u32, true> },
		{ "write_float", &L_WriteFloat<false> },
		{ "write_float_be", &L_WriteFloat<true> },
		{ "writebyterange", &L_WriteByteRange },
		{ "writebyte", &L_WriteInt<u8, false> },
		{ "writeword", &L_WriteInt<u16, false> },
		{ "writedword", &L_WriteInt<u32, false> },
		{ "readstring", &L_ReadString },
		{ "isvalid", &L_IsValid },
		{ "fromcwcheat", &L_FromCwCheat },
		// DeSmuME names
		{ "readbyte", &L_ReadInt<u8, false> },
		{ "readbytesigned", &L_ReadInt<s8, false> },
		{ "readword", &L_ReadInt<u16, false> },
		{ "readwordsigned", &L_ReadInt<s16, false> },
		{ "readdword", &L_ReadInt<u32, false> },
		{ "readdwordsigned", &L_ReadInt<s32, false> },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, funcs);
	lua_setglobal(L, "memory");
}

}  // namespace Script
}  // namespace Access
