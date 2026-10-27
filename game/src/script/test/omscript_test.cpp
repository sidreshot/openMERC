/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-140, SPEC-750, SPEC-751, SPEC-752 (tests)
 * Owner: package G6a.
 *
 * usage:
 *   omscript_test decode <root>...      decode every ds/**.os under the roots (structure, counts)
 *   omscript_test run <root>...         run every stock script once against the mock host
 *   omscript_test vectors               SPEC-750 / SPEC-751 test vectors with assembled scripts
 *   omscript_test roundtrip <root>...   save/load in the middle of stock scripts, compare continuations
 * Roots are directories that contain ds/ (extracted paks), highest priority first.
 */
#include "asm.h"
#include "mock_host.h"
#include "script_vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace om_script;

static void find_os(const std::string &dir, const std::string &rel, std::vector<std::string> &out)
{
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "/*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do {
		std::string n = fd.cFileName;
		if (n == "." || n == "..")
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			find_os(dir + "/" + n, rel.empty() ? n : rel + "/" + n, out);
		else if (n.size() > 3 && !_stricmp(n.c_str() + n.size() - 3, ".os"))
			out.push_back(rel.empty() ? n : rel + "/" + n);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
#else
	(void)dir;
	(void)rel;
	(void)out;
#endif
}

/* script names (relative to ds/, without extension), union over roots */
static std::vector<std::string> stock_scripts(const std::vector<std::string> &roots)
{
	std::vector<std::string> all;
	for (const auto &r : roots) {
		std::vector<std::string> f;
		find_os(r + "/ds", "", f);
		for (auto &s : f) {
			s = s.substr(0, s.size() - 3);
			if (std::find(all.begin(), all.end(), s) == all.end())
				all.push_back(s);
		}
	}
	std::sort(all.begin(), all.end());
	return all;
}

static int cmd_decode(const std::vector<std::string> &roots)
{
	int files = 0, bad = 0;
	long code = 0, vars = 0, fields = 0;
	for (const auto &r : roots) {
		std::vector<std::string> f;
		find_os(r + "/ds", "", f);
		for (const auto &rel : f) {
			std::vector<uint8_t> data;
			MockHost h;
			h.roots = {r};
			if (!h.load_file(("ds/" + rel).c_str(), data)) {
				printf("cannot read %s\n", rel.c_str());
				bad++;
				continue;
			}
			files++;
			std::vector<DecodedInstr> ins;
			std::string err;
			if (!decode_script(data.data(), data.size(), ins, err)) {
				printf("FAIL %s/%s: %s\n", r.c_str(), rel.c_str(), err.c_str());
				bad++;
				continue;
			}
			for (const auto &i : ins) {
				if (!i.declaration)
					code++;
				else if (i.op == 0x06)
					fields++;
				else
					vars++;
			}
			if (ins.empty() || ins.back().op != 0x14)
				printf("note %s: does not end with EXIT\n", rel.c_str());
		}
	}
	printf("decode: %d files, %d failures, %ld code instructions, %ld variables, %ld fields\n", files, bad, code,
	       vars, fields);
	return bad ? 1 : 0;
}

static void setup_run(MockHost &h, ScriptSystem &sys, const std::vector<std::string> &roots, const std::string &name,
                      int &handle)
{
	h.sys = &sys;
	h.roots = roots;
	h.auto_entities = true;
	h.trace_on = true;
	int runner = h.add("script_runner", "runner");
	std::vector<std::string> parms;
	for (int i = 1; i <= 10; i++)
		parms.push_back("parm" + std::to_string(i));
	handle = sys.create(runner, name.c_str(), parms);
	h.framenum = 10;
	sys.use(handle, 1);
}

static int cmd_run(const std::vector<std::string> &roots)
{
	std::vector<std::string> names = stock_scripts(roots);
	std::map<std::string, int> outcome;
	std::map<std::string, int> errors;
	long frames_total = 0, trace_total = 0;
	for (const auto &n : names) {
		MockHost h;
		ScriptSystem sys(&h);
		int handle;
		setup_run(h, sys, roots, n, handle);
		int idle = 0;
		for (int f = 0; f < 3000 && sys.pending_error().empty(); f++) {
			h.frame();
			int st = sys.script_state(handle);
			if (sys.queued_events(handle) == 0 && h.ai_pending.empty()) {
				if (st == 0) { /* READY with nothing queued: suspended, use it again (terminal-style loops) */
					if (++idle > 3)
						break;
					sys.use(handle, 1);
				} else if (st == 1 || ++idle > 50) {
					break;
				}
			}
			frames_total++;
		}
		trace_total += (long)h.trace.size();
		std::string res;
		if (!sys.pending_error().empty()) {
			res = "error";
			errors[sys.pending_error()]++;
			printf("ERROR %s: %s\n", n.c_str(), sys.pending_error().c_str());
		} else {
			switch (sys.script_state(handle)) {
			case 0: res = "suspended/ready"; break;
			case 1: res = "finished"; break;
			case 3: case 4: res = "waiting for signals"; break;
			case 5: res = "waiting for time"; break;
			default: res = "?"; break;
			}
		}
		outcome[res]++;
	}
	printf("run: %d scripts, %ld frames, %ld trace lines\n", (int)names.size(), frames_total, trace_total);
	for (const auto &o : outcome)
		printf("  %-22s %d\n", o.first.c_str(), o.second);
	for (const auto &e : errors)
		printf("  error x%d: %s\n", e.second, e.first.c_str());
	return 0;
}

/* -- test vectors -------------------------------------------------------------------------------------- */

static int failures = 0;
#define CHECK(c, ...)                                                                                          \
	do {                                                                                                       \
		if (!(c)) {                                                                                            \
			printf("  FAIL line %d: ", __LINE__);                                                              \
			printf(__VA_ARGS__);                                                                               \
			printf("\n");                                                                                      \
			failures++;                                                                                        \
		}                                                                                                      \
	} while (0)

struct Bench {
	MockHost h;
	ScriptSystem sys{&h};
	int handle = -1;
	Bench() { h.sys = &sys; }
	int script(const Asm &a, const char *name = "t", std::vector<std::string> parms = {})
	{
		h.memfiles[std::string("ds/") + name + ".os"] = a.b;
		handle = sys.create(h.add("script_runner", name), name, parms);
		return handle;
	}
	int var_i(const char *name)
	{
		FieldValue v;
		int t;
		return sys.variable_by_name(handle, name, v, t) ? (t == om_script::VT_FLOAT ? (int)v.f : v.i) : -9999;
	}
	float var_f(const char *name)
	{
		FieldValue v;
		int t;
		return sys.variable_by_name(handle, name, v, t) ? v.f : -9999.0f;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; i++)
			h.frame();
	}
};

static void v1_budget()
{
	printf("vector 1: instruction budget\n");
	Bench b;
	Asm a;
	a.decl(0x02, "x", om_script::VT_INT, 0);
	int L = a.here();
	a.push_int(1);
	a.push_var(0);
	a.op(0x0C);
	a.jump(0x10, L);
	b.script(a);
	b.sys.use(b.handle, 1);
	b.sys.run_frame();
	CHECK(b.sys.pending_error() == "Runaway loop for script OR more than 1000 instructions", "error '%s'",
	      b.sys.pending_error().c_str());
}

static void v2_wait()
{
	printf("vector 2: WAIT quantization\n");
	Bench b;
	Asm a;
	a.decl(0x02, "x", om_script::VT_INT, 0);
	a.decl(0x02, "y", om_script::VT_INT, 1);
	a.push_float(0.0f);
	a.op(0x16); /* wait 0: no yield */
	a.push_int(1);
	a.push_var(1);
	a.op(0x07); /* y = 1 */
	a.push_float(0.33f);
	a.op(0x16);
	a.push_int(1);
	a.push_var(0);
	a.op(0x07); /* x = 1 */
	a.op(0x14);
	b.script(a);
	b.h.framenum = 100;
	b.sys.use(b.handle, 1);
	b.sys.run_frame();
	CHECK(b.var_i("y") == 1, "wait 0 yielded");
	CHECK(b.var_i("x") == 0, "x set too early");
	b.frames(2); /* 10.1, 10.2 */
	CHECK(b.var_i("x") == 0, "x set before 10.3");
	b.frames(1); /* 10.3 */
	CHECK(b.var_i("x") == 1, "x not set at 10.3");
}

static void v3_move()
{
	printf("vector 3: MOVE timing and signal\n");
	Bench b;
	int wall = b.h.add("func_wall", "wall");
	Asm a;
	a.decl(0x02, "sig", om_script::VT_INT, 0);
	a.decl(0x02, "done", om_script::VT_INT, 1);
	a.push_str("wall");
	a.push_fn(0);
	a.push_vec(0, 0, 96);
	a.push_float(3.0f);
	a.push_var(0);
	a.op_b(0x19, 0x05); /* move(wall, (0,0,96), 3, sig) */
	a.push_var(0);
	a.op_b(0x17, 0x81); /* waitsignals(sig), clear after */
	a.push_int(1);
	a.push_var(1);
	a.op(0x07);
	a.op(0x14);
	b.script(a);
	b.h.framenum = 10;
	b.sys.use(b.handle, 1);
	b.sys.run_frame();
	float vz = b.h.ents[wall].f[F_VELOCITY].v[2];
	CHECK(fabsf(vz - 960.0f / 31.0f) < 1e-3f, "velocity %g, expected %g", vz, 960.0f / 31.0f);
	b.frames(30); /* up to 4.0 */
	CHECK(b.var_i("done") == 0, "finished before 3.1 s");
	b.frames(1); /* 4.1 = 1.0 + 3.1 */
	CHECK(b.var_i("done") == 1, "not finished at 3.1 s");
	CHECK(b.h.ents[wall].f[F_ORIGIN].v[2] == 96.0f, "origin z %g", b.h.ents[wall].f[F_ORIGIN].v[2]);
	CHECK(b.var_i("sig") == 0, "signal not cleared (%d)", b.var_i("sig"));
}

static void v4_use()
{
	printf("vector 4: use while waiting / after SUSPEND / after EXIT\n");
	Bench b;
	Asm a;
	a.decl(0x02, "x", om_script::VT_INT, 0);
	int L = a.here();
	a.push_int(1);
	a.push_var(0);
	a.op(0x0C);
	a.push_float(1.0f);
	a.op(0x16);
	a.op(0x15); /* suspend */
	a.jump(0x10, L);
	b.script(a);
	b.h.framenum = 0;
	b.sys.use(b.handle, 1);
	b.sys.run_frame();
	CHECK(b.var_i("x") == 1, "first use: x=%d", b.var_i("x"));
	b.frames(5);
	b.sys.use(b.handle, 1); /* while waiting: no effect */
	b.frames(6);            /* wait ends at 1.0, SUSPEND */
	CHECK(b.var_i("x") == 1, "use while waiting ran: x=%d", b.var_i("x"));
	b.sys.use(b.handle, 1);
	b.frames(1);
	CHECK(b.var_i("x") == 2, "use after SUSPEND: x=%d", b.var_i("x"));

	Bench c;
	Asm e;
	e.decl(0x02, "x", om_script::VT_INT, 0);
	e.push_int(1);
	e.push_var(0);
	e.op(0x0C);
	e.op(0x14);
	c.script(e);
	c.sys.use(c.handle, 1);
	c.frames(1);
	c.sys.use(c.handle, 1);
	c.frames(1);
	CHECK(c.var_i("x") == 1, "use after EXIT ran: x=%d", c.var_i("x"));
}

static void v5_fn8()
{
	printf("vector 5: fn8 argument order (engine random called with (b, a))\n");
	Bench b;
	Asm a;
	a.decl(0x02, "hey", om_script::VT_FLOAT, 0);
	a.push_int(2);
	a.push_int(15);
	a.push_fn(8);
	a.push_var(0);
	a.op(0x07);
	a.op(0x14);
	b.script(a);
	b.h.rnd = 7;
	b.sys.use(b.handle, 1);
	b.frames(1);
	/* the mock irand(min, max) returns min when max < min: the call must be irand(15, 2) */
	CHECK(b.h.irand_calls == 1 && b.var_f("hey") == 15.0f, "irand called %d times, hey=%g", b.h.irand_calls,
	      b.var_f("hey"));
}

static void v6_expression()
{
	printf("vector 6: AI expression form\n");
	Bench b;
	int sam = b.h.add("m_x_mskinhead1", "sam", true);
	Asm a;
	a.push_str("sam");
	a.push_fn(0);
	a.push_int(6);
	a.push_float(2.3f);  /* 0x40 */
	a.push_int(6);       /* 0x400 */
	a.op_i(0x21, 0x440);
	a.op(0x14);
	b.script(a);
	b.sys.use(b.handle, 1);
	b.frames(1);
	bool found = false;
	for (const auto &t : b.h.trace)
		if (t.find("ai " + std::to_string(sam) + " cmd 6 kind 0 speed 0 anim - dur 2.3 count 1 expr 6") !=
		    std::string::npos)
			found = true;
	CHECK(found, "expression command not seen");
}

static void v7_fields()
{
	printf("vector 7: field read type rule\n");
	Bench b;
	int door = b.h.add("func_door", "door");
	b.h.ents[door].f[F_WAIT].f = 2.5f;
	Asm a;
	a.field("wait", om_script::VT_FLOAT, 0);
	a.decl(0x02, "e", om_script::VT_ENTITY, 0);
	a.decl(0x02, "t", om_script::VT_INT, 1);
	a.decl(0x02, "u", om_script::VT_FLOAT, 2);
	a.push_str("door");
	a.push_fn(0);
	a.push_var(0);
	a.op(0x07); /* e = door */
	a.push_field(0, 0);
	a.push_var(1);
	a.op(0x07); /* t = e.wait */
	a.push_field(0, 0);
	a.push_var(2);
	a.op(0x07); /* u = e.wait */
	a.op(0x14);
	b.script(a);
	b.sys.use(b.handle, 1);
	b.frames(1);
	CHECK(b.var_i("t") == 0, "int t = e.wait gave %d", b.var_i("t"));
	CHECK(b.var_f("u") == 2.5f, "float u = e.wait gave %g", b.var_f("u"));
}

static void v8_early_signal()
{
	printf("vector 8: signal before WAIT_SIGNALS\n");
	Bench b;
	b.h.add("func_wall", "wall");
	Asm a;
	a.decl(0x02, "sig", om_script::VT_INT, 0);
	a.decl(0x02, "done", om_script::VT_INT, 1);
	a.push_str("wall");
	a.push_fn(0);
	a.push_vec(0, 0, 8);
	a.push_float(0.1f);
	a.push_var(0);
	a.op_b(0x19, 0x05);
	a.push_float(1.0f);
	a.op(0x16);
	a.push_var(0);
	a.op_b(0x17, 0x81);
	a.push_int(1);
	a.push_var(1);
	a.op(0x07);
	a.op(0x14);
	b.script(a);
	b.sys.use(b.handle, 1);
	b.frames(5);
	CHECK(b.var_i("sig") == 1, "sig=%d before the wait ends", b.var_i("sig"));
	b.frames(10);
	CHECK(b.var_i("done") == 1 && b.var_i("sig") == 0, "done=%d sig=%d", b.var_i("done"), b.var_i("sig"));
}

static void v_wait_any_quirk()
{
	printf("WAIT_ANY with two signals already set never holds\n");
	Bench b;
	Asm a;
	a.decl(0x02, "s1", om_script::VT_INT, 0);
	a.decl(0x02, "s2", om_script::VT_INT, 1);
	a.decl(0x02, "done", om_script::VT_INT, 2);
	a.push_int(1);
	a.push_var(0);
	a.op(0x07);
	a.push_int(1);
	a.push_var(1);
	a.op(0x07);
	a.push_var(0);
	a.push_var(1);
	a.op_b(0x18, 0x02);
	a.push_int(1);
	a.push_var(2);
	a.op(0x07);
	a.op(0x14);
	b.script(a);
	b.sys.use(b.handle, 1);
	b.frames(3);
	CHECK(b.var_i("done") == 0 && b.sys.script_state(b.handle) == 4, "done=%d state=%d", b.var_i("done"),
	      b.sys.script_state(b.handle));
}

static void v_watch()
{
	printf("WATCH fires once when true, RESUME restores the wait\n");
	Bench b;
	int hawk = b.h.add("m_x_hawk", "hawk", true);
	b.h.ents[hawk].f[F_HEALTH].i = 100;
	Asm a;
	a.field("health", om_script::VT_INT, 0);
	a.decl(0x02, "h", om_script::VT_ENTITY, 0);
	a.decl(0x02, "hits", om_script::VT_INT, 1);
	a.decl(0x02, "after", om_script::VT_INT, 2);
	a.push_str("hawk");
	a.push_fn(0);
	a.push_var(0);
	a.op(0x07);
	a.op(0x33);
	a.push_field(0, 0);
	a.push_int(0);
	a.u8(2); /* <= */
	int tgt = a.here();
	a.i32(0);
	a.push_float(5.0f);
	a.op(0x16); /* wait 5 */
	a.push_int(1);
	a.push_var(2);
	a.op(0x07); /* after = 1 */
	a.op(0x14);
	a.patch(tgt, a.here());
	a.push_int(1);
	a.push_var(1);
	a.op(0x0C); /* hits += 1 */
	a.op(0x34); /* resume */
	b.script(a);
	b.sys.use(b.handle, 1);
	b.frames(2);
	CHECK(b.var_i("hits") == 0, "watch fired early");
	b.h.ents[hawk].f[F_HEALTH].i = 0;
	b.frames(2);
	CHECK(b.var_i("hits") == 1, "watch fired %d times", b.var_i("hits"));
	b.frames(60);
	CHECK(b.var_i("after") == 1, "wait not restored by RESUME (after=%d)", b.var_i("after"));
}

static void v_rof()
{
	printf("SPEC-751 vector 1: ROF frames at T0 .. T0+2.4, signal at T0+2.5\n");
	Bench b;
	int door = b.h.add("func_door_rotating", "door");
	std::vector<uint8_t> rof = {'R', 'O', 'F', 'F'};
	auto put = [&](const void *p, int n) { rof.insert(rof.end(), (const uint8_t *)p, (const uint8_t *)p + n); };
	int ver = 1;
	float cnt = 25.0f;
	put(&ver, 4);
	put(&cnt, 4);
	for (int i = 0; i < 25; i++) {
		float fr[6] = {0, 0, 0, 0, 1.0f, 0};
		put(fr, 24);
	}
	b.h.memfiles["ds/roff/kick.rof"] = rof;
	Asm a;
	a.decl(0x02, "sig", om_script::VT_INT, 0);
	a.decl(0x02, "done", om_script::VT_INT, 1);
	a.push_str("door");
	a.push_fn(0);
	a.push_str("kick.rof");
	a.push_var(0);
	a.op_b(0x29, 0x01);
	a.push_var(0);
	a.op_b(0x17, 0x81);
	a.push_int(1);
	a.push_var(1);
	a.op(0x07);
	a.op(0x14);
	b.script(a);
	b.h.framenum = 10;
	b.sys.use(b.handle, 1);
	b.sys.run_frame(); /* T0 = 1.0: frame 0 applied */
	CHECK(b.h.ents[door].f[F_ANGLES].v[1] == 1.0f, "yaw %g after frame 0", b.h.ents[door].f[F_ANGLES].v[1]);
	b.frames(24); /* 1.1 .. 3.4 */
	CHECK(b.h.ents[door].f[F_ANGLES].v[1] == 25.0f, "yaw %g after 25 frames", b.h.ents[door].f[F_ANGLES].v[1]);
	CHECK(b.var_i("done") == 0, "signal before T0+2.5");
	b.frames(1); /* 3.5 */
	CHECK(b.var_i("done") == 1, "no signal at T0+2.5");
}

/* SPEC-AMEND-003 test vectors */
static void a1_two_entities()
{
	printf("AMEND-003 vector 1: one signal variable for two movers\n");
	Bench b;
	b.h.add("func_wall", "A");
	b.h.add("func_wall", "B");
	Asm a;
	a.decl(0x02, "s", om_script::VT_INT, 0);
	a.decl(0x02, "x", om_script::VT_INT, 1);
	a.decl(0x02, "y", om_script::VT_INT, 2);
	a.push_str("A");
	a.push_fn(0);
	a.push_vec(0, 0, 64);
	a.push_float(64.0f);
	a.push_var(0);
	a.op_b(0x19, 0x06);
	a.push_str("B");
	a.push_fn(0);
	a.push_vec(0, 0, 128);
	a.push_float(64.0f);
	a.push_var(0);
	a.op_b(0x19, 0x06);
	a.push_var(0);
	a.op_b(0x17, 0x81);
	a.push_int(1);
	a.push_var(1);
	a.op(0x07);
	a.push_float(1.5f);
	a.op(0x16);
	a.push_var(0);
	a.op_b(0x17, 0x81);
	a.push_int(1);
	a.push_var(2);
	a.op(0x07);
	a.op(0x14);
	b.script(a);
	b.h.framenum = 10;
	b.sys.use(b.handle, 1);
	b.sys.run_frame();
	b.frames(10); /* 2.0 */
	CHECK(b.var_i("x") == 0 && b.sys.pending_error().empty(), "x=%d before 2.1, error '%s'", b.var_i("x"),
	      b.sys.pending_error().c_str());
	b.frames(1); /* 2.1: A arrives */
	CHECK(b.var_i("x") == 1, "not continued when A arrived");
	b.frames(15); /* 3.6 */
	CHECK(b.var_i("y") == 1, "second wait not satisfied by B's late signal (y=%d)", b.var_i("y"));
}

static void a3_same_mover()
{
	printf("AMEND-003 vector 3: second MOVE of a busy mover\n");
	Bench b;
	int A = b.h.add("func_wall", "A");
	Asm a;
	a.decl(0x02, "s", om_script::VT_INT, 0);
	a.decl(0x02, "x", om_script::VT_INT, 1);
	for (int k = 0; k < 2; k++) {
		a.push_str("A");
		a.push_fn(0);
		a.push_vec(0, 0, 64);
		a.push_float(64.0f);
		a.push_var(0);
		a.op_b(0x19, 0x06);
		if (k == 0) {
			a.push_float(0.5f);
			a.op(0x16);
		}
	}
	a.push_var(0);
	a.op_b(0x17, 0x01);
	a.push_int(1);
	a.push_var(1);
	a.op(0x07);
	a.op(0x14);
	b.script(a);
	b.h.framenum = 10;
	b.sys.use(b.handle, 1);
	b.sys.run_frame();
	b.frames(11); /* 2.1 */
	float z = b.h.ents[A].f[F_ORIGIN].v[2];
	CHECK(b.var_i("x") == 1 && b.var_i("s") == 2, "x=%d s=%d at 2.1", b.var_i("x"), b.var_i("s"));
	CHECK(fabsf(z - 93.09f) < 0.05f, "origin z %g, expected about 93.1", z);
}

static void a4_redeclare()
{
	printf("AMEND-003 vector 4: parameter declaration executed again\n");
	Bench b;
	Asm a;
	a.decl(0x02, "n", om_script::VT_INT, 1);
	int L0 = a.here();
	a.decl(0x04, "p", om_script::VT_INT, 0);
	a.push_int(1);
	a.push_var(1);
	a.op(0x0C);
	a.push_var(1);
	a.push_int(2);
	a.if_(4, L0); /* if not (n >= 2) goto L0 */
	a.op(0x14);
	b.script(a, "t", {"5"});
	b.sys.use(b.handle, 1);
	b.frames(1);
	CHECK(b.sys.pending_error().empty(), "error '%s'", b.sys.pending_error().c_str());
	CHECK(b.var_i("p") == 0 && b.var_i("n") == 2, "p=%d n=%d", b.var_i("p"), b.var_i("n"));
}

static void a5_zero_speed()
{
	printf("AMEND-003 vector 5: zero speed\n");
	{
		Bench b;
		int e = b.h.add("func_wall", "e");
		Asm a;
		a.decl(0x02, "s", om_script::VT_INT, 0);
		a.decl(0x02, "x", om_script::VT_INT, 1);
		a.push_str("e");
		a.push_fn(0);
		a.push_vec(0, 0, 64);
		a.push_float(0.0f);
		a.push_var(0);
		a.op_b(0x19, 0x06);
		a.push_var(0);
		a.op_b(0x17, 0x81);
		a.push_int(1);
		a.push_var(1);
		a.op(0x07);
		a.op(0x14);
		b.script(a);
		b.sys.use(b.handle, 1);
		b.frames(50);
		CHECK(b.var_i("x") == 0 && b.h.ents[e].f[F_ORIGIN].v[2] == 0.0f, "speed-0 MOVE: x=%d z=%g", b.var_i("x"),
		      b.h.ents[e].f[F_ORIGIN].v[2]);
	}
	{
		Bench b;
		int e = b.h.add("func_wall", "e");
		Asm a;
		a.decl(0x02, "s", om_script::VT_INT, 0);
		a.decl(0x02, "x", om_script::VT_INT, 1);
		a.push_str("e");
		a.push_fn(0);
		a.push_vec(0, 90, 0);
		a.push_float(0.0f);
		a.push_var(0);
		a.op_b(0x1C, 0x06);
		a.push_var(0);
		a.op_b(0x17, 0x81);
		a.push_int(1);
		a.push_var(1);
		a.op(0x07);
		a.op(0x14);
		b.script(a);
		b.h.framenum = 10;
		b.sys.use(b.handle, 1);
		b.sys.run_frame();
		CHECK(b.var_i("x") == 1 && b.h.ents[e].f[F_ANGLES].v[1] == 0.0f, "speed-0 ROTATE: x=%d yaw=%g",
		      b.var_i("x"), b.h.ents[e].f[F_ANGLES].v[1]);
	}
	{
		Bench b;
		int e = b.h.add("func_wall", "e");
		Asm a;
		a.decl(0x02, "s", om_script::VT_INT, 0);
		a.decl(0x02, "x", om_script::VT_INT, 1);
		a.push_str("e");
		a.push_fn(0);
		a.push_vec(0, 0, 64);
		a.push_float(0.0f);
		a.push_var(0);
		a.op_b(0x19, 0x05); /* time = 0 */
		a.push_var(0);
		a.op_b(0x17, 0x81);
		a.push_int(1);
		a.push_var(1);
		a.op(0x07);
		a.op(0x14);
		b.script(a);
		b.h.framenum = 10;
		b.sys.use(b.handle, 1);
		b.sys.run_frame();
		CHECK(b.var_i("x") == 0, "time-0 MOVE finished in the same frame");
		b.frames(1);
		CHECK(b.var_i("x") == 1 && b.h.ents[e].f[F_ORIGIN].v[2] == 64.0f, "time-0 MOVE: x=%d z=%g after 0.1 s",
		      b.var_i("x"), b.h.ents[e].f[F_ORIGIN].v[2]);
	}
}

/* -- save/load ----------------------------------------------------------------------------------------- */

struct MemChunks : ChunkWriter, ChunkReader {
	std::vector<std::pair<std::string, std::vector<uint8_t> > > chunks;
	size_t pos = 0;
	void write(const char *id, const void *data, int size) override
	{
		chunks.push_back(std::make_pair(std::string(id, 4),
		                                std::vector<uint8_t>((const uint8_t *)data, (const uint8_t *)data + size)));
	}
	bool read(const char *id, void *data, int size) override
	{
		if (pos >= chunks.size() || chunks[pos].first != std::string(id, 4) || (int)chunks[pos].second.size() != size)
			return false;
		if (size)
			memcpy(data, chunks[pos].second.data(), (size_t)size);
		pos++;
		return true;
	}
};

static bool roundtrip_one(const std::vector<std::string> &roots, const std::string &name, int at_frame, int &chunks)
{
	MockHost h1;
	ScriptSystem s1(&h1);
	int handle;
	setup_run(h1, s1, roots, name, handle);
	for (int f = 0; f < at_frame && s1.pending_error().empty(); f++)
		h1.frame();
	if (!s1.pending_error().empty())
		return true; /* nothing to compare */
	MemChunks mc;
	s1.save(mc);
	chunks = (int)mc.chunks.size();

	MockHost h2 = h1; /* same entities and time */
	h2.trace.clear();
	ScriptSystem s2(&h2);
	h2.sys = &s2;
	h2.trace_on = false;
	if (!s2.load(mc) || mc.pos != mc.chunks.size()) {
		printf("  LOAD FAIL %s (chunk %d of %d)\n", name.c_str(), (int)mc.pos, (int)mc.chunks.size());
		return false;
	}
	/* AI-done tokens are renumbered on load: complete the pending ones in both through their events */
	h2.ai_pending.clear();
	h1.ai_pending.clear();
	h2.trace_on = true;
	h1.trace.clear();
	for (int f = 0; f < 200; f++) {
		h1.frame();
		h2.frame();
	}
	/* drop the attach line; AI-done tokens are internal and renumbered on load, so compare without them */
	auto norm = [](std::string t) {
		size_t at = t.find(" done ");
		return at == std::string::npos ? t : t.substr(0, at) + " done #";
	};
	std::vector<std::string> t2;
	for (auto &t : h2.trace)
		if (t.find("attach") == std::string::npos)
			t2.push_back(norm(t));
	for (auto &t : h1.trace)
		t = norm(t);
	if (h1.trace != t2 || s1.script_state(handle) != s2.script_state(handle) ||
	    s1.script_ip(handle) != s2.script_ip(handle)) {
		printf("  DIFF %s: %d vs %d trace lines, state %d/%d ip %d/%d\n", name.c_str(), (int)h1.trace.size(),
		       (int)t2.size(), s1.script_state(handle), s2.script_state(handle), s1.script_ip(handle),
		       s2.script_ip(handle));
		for (size_t k = 0; k < h1.trace.size() && k < t2.size(); k++)
			if (h1.trace[k] != t2[k]) {
				printf("    first difference at line %d:\n    orig:   %s\n    loaded: %s\n", (int)k, h1.trace[k].c_str(),
				       t2[k].c_str());
				break;
			}
		return false;
	}
	return true;
}

static int cmd_roundtrip(const std::vector<std::string> &roots)
{
	std::vector<std::string> names = stock_scripts(roots);
	int ok = 0, bad = 0;
	long chunks_total = 0;
	for (size_t i = 0; i < names.size(); i++) {
		int chunks = 0;
		if (roundtrip_one(roots, names[i], 5 + (int)(i % 40), chunks))
			ok++;
		else
			bad++;
		chunks_total += chunks;
	}
	printf("roundtrip: %d ok, %d differ, %ld chunks written\n", ok, bad, chunks_total);
	return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: omscript_test decode|run|vectors|roundtrip [roots...]\n");
		return 2;
	}
	std::string cmd = argv[1];
	std::vector<std::string> roots;
	for (int i = 2; i < argc; i++)
		roots.push_back(argv[i]);
	if (cmd == "decode")
		return cmd_decode(roots);
	if (cmd == "run")
		return cmd_run(roots);
	if (cmd == "roundtrip")
		return cmd_roundtrip(roots);
	if (cmd == "trace" && roots.size() >= 2) {
		/* omscript_test trace <script> <root>... : run one stock script and print the host trace */
		std::string name = roots[0];
		roots.erase(roots.begin());
		MockHost h;
		ScriptSystem sys(&h);
		int handle;
		setup_run(h, sys, roots, name, handle);
		for (int f = 0; f < 600 && sys.pending_error().empty(); f++)
			h.frame();
		for (const auto &t : h.trace)
			printf("%s\n", t.c_str());
		printf("state %d ip %d error '%s'\n", sys.script_state(handle), sys.script_ip(handle),
		       sys.pending_error().c_str());
		return 0;
	}
	if (cmd == "vectors") {
		v1_budget();
		v2_wait();
		v3_move();
		v4_use();
		v5_fn8();
		v6_expression();
		v7_fields();
		v8_early_signal();
		v_wait_any_quirk();
		v_watch();
		v_rof();
		a1_two_entities();
		a3_same_mover();
		a4_redeclare();
		a5_zero_speed();
		printf("vectors: %d failures\n", failures);
		return failures ? 1 : 0;
	}
	printf("unknown command %s\n", cmd.c_str());
	return 2;
}
