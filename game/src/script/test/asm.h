/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-140 (instruction encoding, used to build test scripts)
 * Owner: package G6a. Tiny assembler for .os test programs.
 */
#ifndef OM_SCRIPT_TEST_ASM_H
#define OM_SCRIPT_TEST_ASM_H

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

struct Asm {
	std::vector<uint8_t> b;
	Asm() { i32(3); }
	int here() const { return (int)b.size(); }
	void u8(int v) { b.push_back((uint8_t)v); }
	void i32(int v)
	{
		uint8_t t[4];
		memcpy(t, &v, 4);
		b.insert(b.end(), t, t + 4);
	}
	void f32(float f)
	{
		int v;
		memcpy(&v, &f, 4);
		i32(v);
	}
	void str(const char *s)
	{
		b.insert(b.end(), s, s + strlen(s));
		u8(0);
	}
	void patch(int at, int v) { memcpy(&b[at], &v, 4); }

	void decl(int op, const char *name, int type, int index)
	{
		u8(op);
		str(name);
		u8(type);
		i32(index);
	}
	void field(const char *name, int type, int id) { decl(0x06, name, type, id); }
	void push_int(int v) { u8(0x11); u8(0); i32(v); }
	void push_float(float v) { u8(0x11); u8(1); f32(v); }
	void push_vec(float x, float y, float z) { u8(0x11); u8(2); f32(x); f32(y); f32(z); }
	void push_str(const char *s) { u8(0x11); u8(4); str(s); }
	void push_var(int slot) { u8(0x11); u8(5); i32(slot); }
	void push_field(int slot, int field) { u8(0x11); u8(6); i32(slot); i32(field); }
	void push_fn(int id) { u8(0x11); u8(7); u8(id); }
	void op(int o) { u8(o); }
	void op_b(int o, int v) { u8(o); u8(v); }
	void op_i(int o, int v) { u8(o); i32(v); }
	/* returns the offset of the jump operand for patching */
	int jump(int o, int target) { u8(o); int at = here(); i32(target); return at; }
	int if_(int cmp, int target) { u8(0x13); u8(cmp); int at = here(); i32(target); return at; }
};

#endif
