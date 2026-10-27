/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-750, SPEC-751 (test host)
 * Owner: package G6a.
 */
#include "mock_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace om_script;

static std::string fmtf(const char *f, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, f);
	vsnprintf(buf, sizeof buf, f, ap);
	va_end(ap);
	return buf;
}

MockHost::MockHost()
{
	add("worldspawn");
	MockEnt &p = ents[add("player", "")];
	p.f[F_HEALTH].i = 100;
}

int MockHost::add(const char *classname, const char *targetname, bool ai)
{
	MockEnt e;
	e.classname = classname;
	e.targetname = targetname;
	e.has_ai = ai;
	ents.push_back(e);
	return (int)ents.size() - 1;
}

void MockHost::log(const std::string &s)
{
	if (trace_on)
		trace.push_back(fmtf("%.1f ", level_time()) + s);
}

void MockHost::frame()
{
	framenum++;
	/* AI completions are reported before the script frame, like actions finishing during entity thinks */
	for (size_t i = 0; i < ai_pending.size();) {
		if (ai_pending[i].first <= framenum) {
			sys->ai_action_completed(ai_pending[i].second);
			ai_pending.erase(ai_pending.begin() + i);
		} else {
			i++;
		}
	}
	/* movers integrate velocity like MOVETYPE_PUSH */
	for (auto &e : ents)
		for (int k = 0; k < 3; k++) {
			e.f[F_ORIGIN].v[k] += e.f[F_VELOCITY].v[k] * 0.1f;
			e.f[F_ANGLES].v[k] += e.f[F_ANGLE_VELOCITY].v[k] * 0.1f;
		}
	sys->run_frame();
}

void MockHost::dprint(const char *text)
{
	log(std::string("dprint ") + text);
}

static bool read_disk(const std::string &path, std::vector<uint8_t> &out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize((size_t)n);
	size_t got = n ? fread(out.data(), 1, (size_t)n, f) : 0;
	fclose(f);
	return got == (size_t)n;
}

bool MockHost::load_file(const char *path, std::vector<uint8_t> &out)
{
	auto it = memfiles.find(path);
	if (it != memfiles.end()) {
		out = it->second;
		return true;
	}
	for (const auto &r : roots) /* Windows file system: case-insensitive like the stock game */
		if (read_disk(r + "/" + path, out))
			return true;
	return false;
}

float MockHost::cvar_value(const char *name)
{
	auto it = cvars.find(name);
	return it == cvars.end() ? 0.0f : (float)atof(it->second.c_str());
}

void MockHost::cvar_set(const char *name, const char *value)
{
	cvars[name] = value;
	log(fmtf("cvar %s \"%s\"", name, value));
}

void MockHost::cvar_set_value(const char *name, float value)
{
	cvars[name] = fmtf("%g", value);
	log(fmtf("cvar %s %g", name, value));
}

void MockHost::add_command_text(const char *text)
{
	log(std::string("cmd ") + text);
}

int MockHost::irand(int min, int max)
{
	/* engine formula of SPEC-001 is not needed here; a uniform integer in [min, max] */
	irand_calls++;
	rnd = rnd * 1103515245u + 12345u;
	if (max < min)
		return min;
	return min + (int)((rnd >> 8) % (unsigned)(max - min + 1));
}

bool MockHost::entity_in_use(int ent)
{
	return ent >= 0 && ent < (int)ents.size() && ents[ent].inuse;
}

int MockHost::find_by_targetname(const char *name)
{
	for (size_t i = 0; i < ents.size(); i++)
		if (ents[i].inuse && ents[i].targetname == name)
			return (int)i;
	if (auto_entities && *name) {
		int e = add("auto", name, true);
		return e;
	}
	return ENT_NONE;
}

int MockHost::find_by_scripttarget(const char *name)
{
	for (size_t i = 0; i < ents.size(); i++)
		if (ents[i].inuse && ents[i].scripttarget == name)
			return (int)i;
	if (auto_entities && *name) {
		int e = add("auto", "", true);
		ents[e].scripttarget = name;
		return e;
	}
	return ENT_NONE;
}

int MockHost::find_by_classname(const char *name)
{
	for (size_t i = 0; i < ents.size(); i++)
		if (ents[i].inuse && ents[i].classname == name)
			return (int)i;
	return ENT_NONE;
}

const char *MockHost::entity_name(int ent)
{
	return entity_in_use(ent) ? ents[ent].targetname.c_str() : "";
}

bool MockHost::read_field(int ent, FieldId f, FieldValue &out)
{
	if (!entity_in_use(ent))
		return false;
	const MockEnt &e = ents[ent];
	out = e.f[f];
	if (f == F_ORIGIN_X || f == F_ORIGIN_Y || f == F_ORIGIN_Z)
		out.f = e.f[F_ORIGIN].v[f - F_ORIGIN_X];
	return true;
}

void MockHost::write_field(int ent, FieldId f, const FieldValue &v)
{
	if (!entity_in_use(ent))
		return;
	MockEnt &e = ents[ent];
	if (f == F_ORIGIN_X || f == F_ORIGIN_Y || f == F_ORIGIN_Z)
		e.f[F_ORIGIN].v[f - F_ORIGIN_X] = v.f;
	else
		e.f[f] = v;
	if (f == F_ORIGIN || f == F_ANGLES)
		log(fmtf("set %d %s (%g %g %g)", ent, f == F_ORIGIN ? "origin" : "angles", e.f[f].v[0], e.f[f].v[1],
		         e.f[f].v[2]));
	else if (f == F_VELOCITY || f == F_ANGLE_VELOCITY)
		log(fmtf("set %d %s (%g %g %g)", ent, f == F_VELOCITY ? "velocity" : "avelocity", v.v[0], v.v[1], v.v[2]));
	else
		log(fmtf("set %d field %d", ent, (int)f));
}

bool MockHost::entity_has_use(int ent)
{
	return entity_in_use(ent) && ents[ent].has_use;
}

void MockHost::use_entity(int ent, int other, int activator)
{
	ents[ent].uses++;
	log(fmtf("use %d other %d activator %d", ent, other, activator));
}

void MockHost::free_entity(int ent)
{
	if (ent > 1 && entity_in_use(ent))
		ents[ent].inuse = false;
	log(fmtf("free %d", ent));
}

int MockHost::spawn_entity(const std::vector<std::pair<std::string, std::string> > &keys)
{
	int e = add("");
	for (const auto &kv : keys) {
		if (kv.first == "classname")
			ents[e].classname = kv.second;
	}
	log(fmtf("spawn %d %s", e, ents[e].classname.c_str()));
	return e;
}

void MockHost::relink(int ent)
{
	(void)ent;
}

void MockHost::set_move_destination(int ent, const float dest[3])
{
	memcpy(ents[ent].move_dest, dest, sizeof ents[ent].move_dest);
}

void MockHost::finish_move(int ent)
{
	if (!entity_in_use(ent))
		return;
	MockEnt &e = ents[ent];
	memset(e.f[F_VELOCITY].v, 0, sizeof e.f[F_VELOCITY].v);
	memcpy(e.f[F_ORIGIN].v, e.move_dest, sizeof e.move_dest);
	log(fmtf("move_done %d (%g %g %g)", ent, e.move_dest[0], e.move_dest[1], e.move_dest[2]));
}

void MockHost::finish_rotate(int ent)
{
	if (!entity_in_use(ent))
		return;
	memset(ents[ent].f[F_ANGLE_VELOCITY].v, 0, sizeof ents[ent].f[F_ANGLE_VELOCITY].v);
	log(fmtf("rotate_done %d", ent));
}

void MockHost::make_trigger(int ent) { log(fmtf("enable %d", ent)); }
void MockHost::disable_trigger(int ent) { log(fmtf("disable %d", ent)); }
void MockHost::set_view_yaw(int player, float yaw) { log(fmtf("viewyaw %d %g", player, yaw)); }
void MockHost::set_music_state(int value) { log(fmtf("music %d", value)); }

int MockHost::sound_index(const char *name)
{
	log(std::string("soundindex ") + name);
	return 1;
}

void MockHost::unload_sound(const char *name) { log(std::string("unloadsound ") + name); }

void MockHost::play_sound(int ent, int channel, int sound, float volume, float attenuation, float timeofs)
{
	log(fmtf("sound %d chan %d idx %d vol %g attn %g ofs %g", ent, channel, sound, volume, attenuation, timeofs));
}

void MockHost::print_text(int target, int level, const char *text)
{
	log(fmtf("print %d %d %s", target, level, text));
}

void MockHost::sp_print(int target, int string_id)
{
	log(fmtf("sp_print %d 0x%04x", target, string_id));
}

void MockHost::register_string_package(const char *name) { log(std::string("stringpackage ") + name); }

bool MockHost::has_ai(int ent)
{
	return entity_in_use(ent) && ents[ent].has_ai;
}

void MockHost::ai_command(int ent, const AiCommand &cmd)
{
	log(fmtf("ai %d cmd %d kind %d speed %d anim %s dur %g count %d expr %d done %d", ent, cmd.command, cmd.kind,
	         cmd.speed, cmd.animation ? cmd.animation : "-", cmd.duration, cmd.count, cmd.expression, cmd.done_id));
	if (cmd.done_id && cmd.count > 0)
		ai_pending.push_back(std::make_pair(framenum + ai_complete_delay, cmd.done_id));
}

void MockHost::ai_reset(int ent) { log(fmtf("ai_reset %d", ent)); }
void MockHost::ai_clear_call_for_help(int ent) { (void)ent; }

int MockHost::heli_command(int ent, int command, int target, const float vec[3], float scalar)
{
	if (!has_ai(ent))
		return -2;
	int id = next_action++;
	ents[ent].heli_last_done = id; /* the mock finishes every vehicle action at once */
	log(fmtf("heli %d cmd %d target %d vec (%g %g %g) num %g -> %d", ent, command, target, vec[0], vec[1], vec[2],
	         scalar, id));
	return id;
}

int MockHost::vehicle_command(int ent, int command, int target, const float vec[3], float scalar)
{
	if (!has_ai(ent))
		return -2;
	int id = next_action++;
	ents[ent].heli_last_done = id;
	log(fmtf("vehicle %d cmd %d target %d vec (%g %g %g) num %g -> %d", ent, command, target, vec[0], vec[1],
	         vec[2], scalar, id));
	return id;
}

bool MockHost::vehicle_action_done(int ent, int action_id)
{
	return entity_in_use(ent) && ents[ent].heli_last_done == action_id;
}

void MockHost::attach_script(int runner, int handle)
{
	log(fmtf("attach %d %d", runner, handle));
}
