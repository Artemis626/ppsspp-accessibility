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

// The "event" table: every kind of callback a script can register.
//
//   event.onframeend(fn [, name])                    fn()
//   event.onmemorywrite(fn, addr [, name [, len]])   fn(addr, value, size, pc)
//   event.onmemoryread(fn, addr [, name [, len]])    fn(addr, size, pc)
//   event.onmemoryexecute(fn, addr [, name])         fn(addr)
//   event.unregisterbyid(id), event.unregisterbyname(name)
//   emu.registerafter(fn)  (DeSmuME: one per-frame function, replaced each call)
//
// How memory and execute callbacks work: PPSSPP translates game code into
// native code (the JIT), so a hook in its memory-read functions would miss
// almost everything. Instead we use PPSSPP's own debugger breakpoints and
// memory checks, which every CPU backend already supports, marked with
// BREAK_ACTION_SCRIPT so they call us instead of pausing. Changes to the
// callback list are applied once per host frame (SyncBreakpoints), which is
// also when PPSSPP updates its JIT for breakpoint changes.

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "Accessibility/Scripting/LuaInternal.h"
#include "Common/CommonTypes.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"

namespace Access {
namespace Script {

namespace {

enum class Kind {
	Frame,
	Write,
	Read,
	Exec,
};

struct Callback {
	int id;
	Kind kind;
	std::string name;
	int ref;      // Lua registry reference to the function.
	u32 start;    // Memory/exec callbacks: watched range [start, end).
	u32 end;
	bool alive;
};

std::vector<Callback> g_callbacks;
int g_nextId = 1;
int g_registerAfterId = 0;
bool g_needSync = false;

// What we have installed in g_breakpoints, so we can take it out again.
std::set<u32> g_installedExec;
std::map<std::pair<u32, u32>, int> g_installedMem;  // [start, end) -> MemCheckCondition

// Memory events that arrived on another thread (PPSSPP's I/O thread can
// write to memory). Lua is single-threaded, so they wait for the emu thread.
struct QueuedEvent {
	int kind;
	u32 addr;
	int size;
	u32 pc;
	bool hasValue;
	u32 value;
};
std::mutex g_queueMutex;
std::vector<QueuedEvent> g_queue;
const size_t MAX_QUEUED = 4096;

// Same rule as PPSSPP's breakpoint code: the uncached mirror (0x4xxxxxxx) and
// VRAM mirrors are the same memory.
u32 NotCached(u32 addr) {
	if ((addr & 0x3F800000) == 0x04000000) {
		return addr & ~0x40600000;
	}
	return addr & ~0x40000000;
}

// ---- breakpoint syncing ---------------------------------------------------

BreakAction ExistingBreakpointResult(u32 addr, bool *exists) {
	for (const BreakPoint &bp : g_breakpoints.GetBreakpoints()) {
		if (bp.addr == addr && !bp.temporary) {
			*exists = true;
			return bp.result;
		}
	}
	*exists = false;
	return BREAK_ACTION_IGNORE;
}

void RemoveInstalled() {
	for (u32 addr : g_installedExec) {
		bool exists;
		BreakAction result = ExistingBreakpointResult(addr, &exists);
		if (!exists) {
			continue;
		}
		BreakAction remaining = BreakAction(result & ~BREAK_ACTION_SCRIPT);
		if (remaining == BREAK_ACTION_IGNORE) {
			g_breakpoints.RemoveBreakPoint(addr);
		} else {
			g_breakpoints.ChangeBreakPoint(addr, remaining);  // Keep the debugger's own breakpoint.
		}
	}
	g_installedExec.clear();

	for (const auto &range : g_installedMem) {
		MemCheck check;
		if (!g_breakpoints.GetMemCheck(range.first.first, range.first.second, &check)) {
			continue;
		}
		BreakAction remaining = BreakAction(check.result & ~BREAK_ACTION_SCRIPT);
		if (remaining == BREAK_ACTION_IGNORE) {
			g_breakpoints.RemoveMemCheck(range.first.first, range.first.second);
		} else {
			g_breakpoints.ChangeMemCheck(range.first.first, range.first.second, check.cond, remaining);
		}
	}
	g_installedMem.clear();
}

void SyncBreakpoints() {
	g_needSync = false;
	RemoveInstalled();

	// Execute callbacks: one breakpoint per address.
	std::set<u32> exec;
	for (const Callback &cb : g_callbacks) {
		if (cb.alive && cb.kind == Kind::Exec) {
			exec.insert(cb.start);
		}
	}
	for (u32 addr : exec) {
		bool exists;
		BreakAction result = ExistingBreakpointResult(addr, &exists);
		if (!exists) {
			g_breakpoints.AddBreakPoint(addr);  // Adds with PAUSE; replaced on the next line.
			result = BREAK_ACTION_IGNORE;
		}
		g_breakpoints.ChangeBreakPoint(addr, BreakAction(result | BREAK_ACTION_SCRIPT));
		g_installedExec.insert(addr);
	}

	// Read/write callbacks: PPSSPP fires only the FIRST memcheck that overlaps
	// an access, so overlapping script ranges are split into non-overlapping
	// pieces, each with the combined read/write condition.
	std::set<u32> edges;
	for (const Callback &cb : g_callbacks) {
		if (cb.alive && (cb.kind == Kind::Read || cb.kind == Kind::Write)) {
			edges.insert(cb.start);
			edges.insert(cb.end);
		}
	}
	std::vector<u32> points(edges.begin(), edges.end());
	std::vector<std::pair<std::pair<u32, u32>, int>> pieces;
	for (size_t i = 0; i + 1 < points.size(); i++) {
		u32 a = points[i], b = points[i + 1];
		int cond = 0;
		for (const Callback &cb : g_callbacks) {
			if (cb.alive && cb.start <= a && cb.end >= b) {
				if (cb.kind == Kind::Write) cond |= MEMCHECK_WRITE;
				if (cb.kind == Kind::Read) cond |= MEMCHECK_READ;
			}
		}
		if (cond == 0) {
			continue;
		}
		// Merge with the previous piece when it touches and has the same condition.
		if (!pieces.empty() && pieces.back().first.second == a && pieces.back().second == cond) {
			pieces.back().first.second = b;
		} else {
			pieces.push_back({ { a, b }, cond });
		}
	}
	for (const auto &piece : pieces) {
		g_breakpoints.AddMemCheck(piece.first.first, piece.first.second, (MemCheckCondition)piece.second, BREAK_ACTION_SCRIPT);
		g_installedMem[piece.first] = piece.second;
	}
}

// ---- dispatch -------------------------------------------------------------

// For a CPU store that is about to run, work out the value it will write.
// Covers sb, sh, sw, sc and swc1; returns false for others (swl/swr/VFPU stores).
bool DecodeStoreValue(u32 pc, u32 *value) {
	u32 op = Memory::Read_Instruction(pc, true).encoding;
	u32 rt = (op >> 16) & 0x1F;
	switch (op >> 26) {
	case 0x28: *value = currentMIPS->r[rt] & 0xFF; return true;    // sb
	case 0x29: *value = currentMIPS->r[rt] & 0xFFFF; return true;  // sh
	case 0x2B:                                                     // sw
	case 0x38: *value = currentMIPS->r[rt]; return true;           // sc
	case 0x39: *value = currentMIPS->fi[rt]; return true;          // swc1
	default: return false;
	}
}

bool ReadValueNow(u32 addr, int size, u32 *value) {
	if ((size != 1 && size != 2 && size != 4) || !Memory::IsValidRange(addr, size)) {
		return false;
	}
	u32 v = 0;
	memcpy(&v, Memory::GetPointerUnchecked(addr), size);
	*value = v;
	return true;
}

void ReleaseCallback(Callback &cb) {
	lua_State *L = ActiveState();
	if (cb.alive && L) {
		luaL_unref(L, LUA_REGISTRYINDEX, cb.ref);
	}
	cb.alive = false;
	if (cb.kind != Kind::Frame) {
		g_needSync = true;
	}
}

// Calls the callback at index i. On error the callback is turned off.
void CallAt(lua_State *L, size_t i, int nargs) {
	// Copy what we need first: the callback may register new callbacks,
	// which can reallocate g_callbacks.
	std::string where;
	switch (g_callbacks[i].kind) {
	case Kind::Frame: where = "frame callback"; break;
	case Kind::Write: where = "memory write callback"; break;
	case Kind::Read: where = "memory read callback"; break;
	case Kind::Exec: where = "execute callback"; break;
	}
	if (!g_callbacks[i].name.empty()) {
		where += " '" + g_callbacks[i].name + "'";
	}
	if (!ProtectedCall(L, nargs, 0, where + " (it has been turned off)")) {
		if (i < g_callbacks.size()) {
			ReleaseCallback(g_callbacks[i]);
		}
	}
}

void Dispatch(lua_State *L, int kind, u32 addr, int size, u32 pc, bool hasValue, u32 value) {
	u32 a = NotCached(addr);
	u32 b = a + (u32)std::max(size, 1);
	size_t count = g_callbacks.size();
	for (size_t i = 0; i < count && i < g_callbacks.size(); i++) {
		const Callback &cb = g_callbacks[i];
		if (!cb.alive) {
			continue;
		}
		if (kind == 0 && cb.kind == Kind::Exec && cb.start == a) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
			lua_pushinteger(L, addr);
			CallAt(L, i, 1);
		} else if (kind == 2 && cb.kind == Kind::Write && a < cb.end && b > cb.start) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
			lua_pushinteger(L, addr);
			if (hasValue) {
				lua_pushinteger(L, value);
			} else {
				lua_pushnil(L);
			}
			lua_pushinteger(L, size);
			lua_pushinteger(L, pc);
			CallAt(L, i, 4);
		} else if (kind == 1 && cb.kind == Kind::Read && a < cb.end && b > cb.start) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
			lua_pushinteger(L, addr);
			lua_pushinteger(L, size);
			lua_pushinteger(L, pc);
			CallAt(L, i, 3);
		}
	}
}

// Installed as g_breakScriptHook while a script is loaded. Runs inside
// emulation (often from JIT code, with registers already flushed to MIPSState).
void OnBreakpointHit(int kind, u32 addr, int size, u32 pc, const char *reason) {
	lua_State *L = ActiveState();
	if (!L) {
		return;
	}
	bool cpu = reason && (!strcmp(reason, "CPU") || !strcmp(reason, "interpret"));
	u32 value = 0;
	bool hasValue = false;
	if (kind == 2) {
		// CPU stores haven't happened yet: decode the value from the instruction.
		// HLE writes (file reads into RAM etc.) have: read it from memory.
		hasValue = cpu ? DecodeStoreValue(pc, &value) : ReadValueNow(addr, size, &value);
	}
	if (!OnEmuThread()) {
		std::lock_guard<std::mutex> lock(g_queueMutex);
		if (g_queue.size() < MAX_QUEUED) {
			g_queue.push_back(QueuedEvent{ kind, addr, size, pc, hasValue, value });
		}
		return;
	}
	Dispatch(L, kind, addr, size, pc, hasValue, value);
}

// ---- Lua functions --------------------------------------------------------

int Add(lua_State *L, int fnIndex, Kind kind, const char *name, u32 start, u32 end) {
	lua_pushvalue(L, fnIndex);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	int id = g_nextId++;
	g_callbacks.push_back(Callback{ id, kind, name ? name : "", ref, NotCached(start), NotCached(start) + (end - start), true });
	if (kind != Kind::Frame) {
		g_needSync = true;
	}
	return id;
}

bool RemoveById(int id) {
	for (Callback &cb : g_callbacks) {
		if (cb.alive && cb.id == id) {
			ReleaseCallback(cb);
			return true;
		}
	}
	return false;
}

int L_OnFrameEnd(lua_State *L) {
	luaL_checktype(L, 1, LUA_TFUNCTION);
	const char *name = luaL_optstring(L, 2, "");
	lua_pushinteger(L, Add(L, 1, Kind::Frame, name, 0, 0));
	return 1;
}

int AddMemory(lua_State *L, Kind kind) {
	luaL_checktype(L, 1, LUA_TFUNCTION);
	u32 addr = (u32)luaL_checkinteger(L, 2);
	const char *name = luaL_optstring(L, 3, "");
	lua_Integer len = kind == Kind::Exec ? 1 : luaL_optinteger(L, 4, 1);
	luaL_argcheck(L, len >= 1 && len <= 0x04000000, 4, "length must be between 1 and 0x04000000");
	if (kind == Kind::Exec) {
		luaL_argcheck(L, (addr & 3) == 0, 2, "execute address must be a multiple of 4 (MIPS instructions are 4 bytes)");
	}
	lua_pushinteger(L, Add(L, 1, kind, name, addr, addr + (u32)len));
	return 1;
}

int L_OnMemoryWrite(lua_State *L) { return AddMemory(L, Kind::Write); }
int L_OnMemoryRead(lua_State *L) { return AddMemory(L, Kind::Read); }
int L_OnMemoryExecute(lua_State *L) { return AddMemory(L, Kind::Exec); }

int L_UnregisterById(lua_State *L) {
	int id = (int)luaL_checkinteger(L, 1);
	lua_pushboolean(L, RemoveById(id));
	return 1;
}

int L_UnregisterByName(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	bool removed = false;
	for (Callback &cb : g_callbacks) {
		if (cb.alive && cb.name == name) {
			ReleaseCallback(cb);
			removed = true;
		}
	}
	lua_pushboolean(L, removed);
	return 1;
}

int L_RegisterAfter(lua_State *L) {
	if (!lua_isnoneornil(L, 1)) {
		luaL_checktype(L, 1, LUA_TFUNCTION);
	}
	if (g_registerAfterId) {
		RemoveById(g_registerAfterId);
		g_registerAfterId = 0;
	}
	if (!lua_isnoneornil(L, 1)) {
		g_registerAfterId = Add(L, 1, Kind::Frame, "emu.registerafter", 0, 0);
	}
	return 0;
}

void RemoveDead() {
	g_callbacks.erase(std::remove_if(g_callbacks.begin(), g_callbacks.end(),
		[](const Callback &cb) { return !cb.alive; }), g_callbacks.end());
}

}  // namespace

void RunFrameCallbacks(lua_State *L) {
	size_t count = g_callbacks.size();
	for (size_t i = 0; i < count && i < g_callbacks.size(); i++) {
		if (g_callbacks[i].alive && g_callbacks[i].kind == Kind::Frame) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, g_callbacks[i].ref);
			CallAt(L, i, 0);
		}
	}
}

void ProcessEventQueue(lua_State *L) {
	std::vector<QueuedEvent> events;
	{
		std::lock_guard<std::mutex> lock(g_queueMutex);
		events.swap(g_queue);
	}
	for (const QueuedEvent &e : events) {
		Dispatch(L, e.kind, e.addr, e.size, e.pc, e.hasValue, e.value);
	}
	// Outside emulation now, so it's safe to change breakpoints and to drop
	// dead entries (nothing is iterating the list).
	if (g_needSync) {
		SyncBreakpoints();
	}
	RemoveDead();
	g_breakScriptHook = &OnBreakpointHit;
}

void ClearCallbacks() {
	g_breakScriptHook = nullptr;
	RemoveInstalled();
	// The Lua references die with lua_close; no need to unref one by one.
	g_callbacks.clear();
	g_nextId = 1;
	g_registerAfterId = 0;
	g_needSync = false;
	std::lock_guard<std::mutex> lock(g_queueMutex);
	g_queue.clear();
}

void RegisterEventApi(lua_State *L) {
	static const luaL_Reg eventFuncs[] = {
		{ "onframeend", &L_OnFrameEnd },
		{ "onmemorywrite", &L_OnMemoryWrite },
		{ "onmemoryread", &L_OnMemoryRead },
		{ "onmemoryexecute", &L_OnMemoryExecute },
		{ "unregisterbyid", &L_UnregisterById },
		{ "unregisterbyname", &L_UnregisterByName },
		{ nullptr, nullptr },
	};
	luaL_newlib(L, eventFuncs);
	lua_setglobal(L, "event");

	lua_getglobal(L, "emu");
	lua_pushcfunction(L, &L_RegisterAfter);
	lua_setfield(L, -2, "registerafter");
	lua_pop(L, 1);

	// DeSmuME aliases on the memory table. memory.register(addr, [size,] fn)
	// is written in Lua so the optional middle argument is easy to handle.
	luaL_dostring(L,
		"memory.register = function(addr, a, b)\n"
		"  if b == nil then return event.onmemorywrite(a, addr) end\n"
		"  return event.onmemorywrite(b, addr, nil, a)\n"
		"end\n"
		"memory.registerwrite = memory.register\n"
		"memory.registerexec = function(addr, a, b)\n"
		"  return event.onmemoryexecute(b or a, addr)\n"
		"end\n");
}

}  // namespace Script
}  // namespace Access
