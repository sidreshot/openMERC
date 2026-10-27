// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - network replication of model objects and instance states.
// Implements: SPEC-013 sections 1-5 (string table, object definitions, baseline, instance-state groups,
//             transform sub-format, part-flag runs, server equality / scheduling / delta bases, client rules),
//             SPEC-201 Appendix B.4-B.6 (reliable and unreliable payloads), SPEC-010 A methods 12-27,
//             SPEC-011 method 52; SPEC-AMEND-005 I3.1 (delta bases per instance and client)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>

#include "ghoul_internal.h"

namespace ghoul {

// ------------------------------------------------------------------ per-system network data

struct ServerClient
{
	int stringsSent = 0;            // strings 1..stringsSent are known by the client
	std::set<int> known;            // objects defined for the client
	std::deque<int> pendingDefs;    // objects to define
	std::deque<int> removedObjects; // to send in section C
	std::deque<int> removedInsts;   // to send in section D
	std::set<int> sentInsts;        // instances ever sent (removal is sent for these)
	std::deque<int> framesSent; // at most 100
	std::map<int, std::vector<int>> frameSets; // frame -> instances whose records were written and fitted
	// frame being written
	int curFrame = 0;
	std::multimap<float, int> queue;     // priority -> instance id
	std::vector<std::pair<int, NetState>> written; // records of the current frame, committed by method 20
};

struct ClientInstStates
{
	std::map<int, NetState> states; // frame -> decoded state
};

struct NetData
{
	// server
	std::map<int, ServerClient> clients;
	int curClient = -1;
	// client / menu
	bool accepting = false;
	std::vector<unsigned char> stream; // stored reliable stream: {u32 length, bytes}...
	size_t applied = 0;                // bytes of the stream already applied
	StringTable rstrings;              // received string table
	std::map<int, ClientInstStates> instStates;
	struct Widths
	{
		int w0 = 0, w1 = 0, w2 = 0, w3 = 0, parts = 0;
	};
	std::map<int, Widths> objWidths; // received object id (negative) -> widths
};

static NetData &ND(System *s)
{
	if (!s->net)
		s->net = new NetData();
	return *s->net;
}

void NetResetData(System *s)
{
	delete s->net;
	s->net = nullptr;
}

// ------------------------------------------------------------------ state coding (SPEC-013 section 3)

struct Widths
{
	int w0, w1, w2, w3;
};

static Widths ObjectWidths(Object *o)
{
	return Widths{BitsFor((int)o->seqs.size()), BitsFor((int)o->parts.size()), BitsFor((int)o->mats.size()),
		BitsFor((int)o->skins.size())};
}

static NetState DefaultState(int parts)
{
	NetState s;
	s.parts.assign((size_t)parts, 1);
	return s;
}

static int32_t TimeUnits(float x)
{
	// trunc(x * 120 + 0.5), truncation toward zero
	return (int32_t)(x * 120.0f + 0.5f);
}

static float RowLength(const Matrix4 &m, int r)
{
	return std::sqrt(m.m[r][0] * m.m[r][0] + m.m[r][1] * m.m[r][1] + m.m[r][2] * m.m[r][2]);
}

static bool PartsEqual(const NetState &a, const NetState &b)
{
	size_t n = std::max(a.parts.size(), b.parts.size());
	for (size_t i = 0; i < n; i++)
	{
		uint8_t x = i < a.parts.size() ? a.parts[i] : 1, y = i < b.parts.size() ? b.parts[i] : 1;
		if ((x != 0) != (y != 0))
			return false;
	}
	return true;
}

// server equality: is the instance sent at all
static bool StatesEqual(const NetState &a, const NetState &b)
{
	if (!MatNearlyEqual(a.xf, b.xf, 1e-4f))
		return false;
	if (std::fabs(a.S - b.S) > 1.0f / 30.0f || std::fabs(a.A - b.A) > 1.0f / 30.0f)
		return false;
	if (a.paused && a.pauseEnd != b.pauseEnd)
		return false;
	if (a.on != b.on || a.speed != b.speed || a.paused != b.paused || a.reverse != b.reverse ||
		a.attachBolt != b.attachBolt || a.seq != b.seq || a.mode != b.mode || std::memcmp(a.tint, b.tint, 4))
		return false;
	return PartsEqual(a, b) && a.boltons == b.boltons && a.skins == b.skins;
}

static void WriteTransform(Bits &b, const Matrix4 &cur, const Matrix4 &base)
{
	uint32_t rc = EncodePackedRotation(cur), rb = EncodePackedRotation(base);
	if (rc != rb)
	{
		b.Write(1, 1);
		b.Write(rc, 32);
	}
	else
		b.Write(0, 1);
	float sc = RowLength(cur, 0);
	if (sc >= 1.01f || sc <= 0.99f)
	{
		b.Write(1, 1);
		b.WriteFloat(sc);
	}
	else
		b.Write(0, 1);
	bool axis[3];
	bool any = false;
	for (int a = 0; a < 3; a++)
	{
		axis[a] = cur.m[3][a] != base.m[3][a];
		any = any || axis[a];
	}
	b.Write(any ? 1 : 0, 1);
	if (!any)
		return;
	for (int a = 0; a < 3; a++)
	{
		b.Write(axis[a] ? 1 : 0, 1);
		if (!axis[a])
			continue;
		float v = cur.m[3][a];
		if (v >= -64.0f && v <= 64.0f)
		{
			b.Write(1, 1);
			int q = (int)std::floor((v + 64.0f) * 16.0f);
			q = q < 0 ? 0 : (q > 2047 ? 2047 : q);
			b.Write((uint32_t)q, 11);
		}
		else
		{
			b.Write(0, 1);
			b.WriteFloat(v);
		}
	}
}

static void ReadTransform(Bits &b, Matrix4 &m)
{
	// starts from the base matrix (already in m)
	if (b.Read(1))
	{
		float r[3][3];
		DecodePackedRotation(b.Read(32), r);
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				m.m[i][j] = r[i][j];
	}
	float sc = 1.0f;
	if (b.Read(1))
		sc = b.ReadFloat();
	for (int r = 0; r < 3; r++)
	{
		float l = RowLength(m, r);
		float k = l > 1e-10f ? sc / l : 0.0f;
		for (int c = 0; c < 3; c++)
			m.m[r][c] *= k;
		m.m[r][3] = 0.0f;
	}
	if (b.Read(1))
		for (int a = 0; a < 3; a++)
		{
			if (!b.Read(1))
				continue;
			if (b.Read(1))
				m.m[3][a] = (float)b.Read(11) / 16.0f - 64.0f;
			else
				m.m[3][a] = b.ReadFloat();
		}
	m.m[3][3] = 1.0f;
	m.flags = 0;
	MatUpdateFlags(m);
}

static void WritePartMask(Bits &b, const std::vector<uint8_t> &x)
{
	size_t i = 0, n = x.size();
	while (i < n)
	{
		size_t run = 1;
		while (i + run < n && x[i + run] == x[i])
			run++;
		if (run >= 6)
		{
			size_t c = std::min<size_t>(run, 37) - 6;
			b.Write(1, 1);
			b.Write(x[i], 1);
			b.Write((uint32_t)c, 5);
			i += c + 6;
		}
		else
		{
			b.Write(0, 1);
			size_t k = std::min<size_t>(4, n - i);
			for (size_t j = 0; j < k; j++)
				b.Write(x[i + j], 1);
			i += k;
		}
	}
}

static void ReadPartMask(Bits &b, std::vector<uint8_t> &parts)
{
	size_t i = 0, n = parts.size();
	while (i < n && !b.Overflowed())
	{
		if (b.Read(1))
		{
			uint32_t v = b.Read(1);
			uint32_t c = b.Read(5) + 6;
			for (uint32_t j = 0; j < c && i < n; j++, i++)
				parts[i] = (uint8_t)((parts[i] != 0) ^ v);
		}
		else
		{
			size_t k = std::min<size_t>(4, n - i);
			for (size_t j = 0; j < k; j++, i++)
				parts[i] = (uint8_t)((parts[i] != 0) ^ b.Read(1));
		}
	}
}

static void WriteState(Bits &b, const NetState &cur, const NetState &base, float frameTime, const Widths &w)
{
	b.Write(cur.on ? 1 : 0, 1);
	if (cur.speed != base.speed)
	{
		b.Write(1, 1);
		b.Write((uint32_t)cur.speed, 4);
	}
	else
		b.Write(0, 1);
	b.Write(cur.paused ? 1 : 0, 1);
	b.Write(cur.reverse ? 1 : 0, 1);
	if (w.w1 > 0)
	{
		if (cur.attachBolt != base.attachBolt)
		{
			b.Write(1, 1);
			b.Write((uint32_t)cur.attachBolt, w.w1);
		}
		else
			b.Write(0, 1);
	}
	if (w.w0 > 0)
	{
		if (cur.seq != base.seq)
		{
			b.Write(1, 1);
			b.Write((uint32_t)cur.seq, w.w0);
		}
		else
			b.Write(0, 1);
	}
	if (cur.mode != base.mode)
	{
		b.Write(1, 1);
		b.Write((uint32_t)cur.mode, 2);
	}
	else
		b.Write(0, 1);
	if (cur.S != base.S || cur.A != base.A)
	{
		b.Write(1, 1);
		b.WriteTime(TimeUnits(cur.A - frameTime));
		b.WriteTime(TimeUnits(cur.S - cur.A));
	}
	else
		b.Write(0, 1);
	if (cur.paused)
	{
		if (cur.pauseEnd != base.pauseEnd)
		{
			b.Write(1, 1);
			if (base.pauseEnd > 1e10f) // writer quirk: decided by the base value
				b.Write(1, 1);
			else
			{
				b.Write(0, 1);
				b.WriteFloat(cur.pauseEnd);
			}
		}
		else
			b.Write(0, 1);
	}
	if (std::memcmp(cur.tint, base.tint, 4))
	{
		b.Write(1, 1);
		for (int i = 0; i < 4; i++)
		{
			if (cur.tint[i] != base.tint[i])
			{
				b.Write(1, 1);
				b.Write(cur.tint[i], 8);
			}
			else
				b.Write(0, 1);
		}
	}
	else
		b.Write(0, 1);
	if (!MatNearlyEqual(cur.xf, base.xf, 1e-4f))
	{
		b.Write(1, 1);
		WriteTransform(b, cur.xf, base.xf);
	}
	else
		b.Write(0, 1);
	std::vector<uint8_t> x(base.parts.size(), 0);
	bool anyPart = false;
	for (size_t i = 0; i < x.size(); i++)
	{
		uint8_t c = i < cur.parts.size() ? (cur.parts[i] != 0) : 1;
		x[i] = (uint8_t)(c ^ (base.parts[i] != 0));
		anyPart = anyPart || x[i];
	}
	if (anyPart)
	{
		b.Write(1, 1);
		WritePartMask(b, x);
	}
	else
		b.Write(0, 1);
	if (cur.boltons != base.boltons)
	{
		b.Write(1, 1);
		for (auto &e : cur.boltons)
		{
			auto it = base.boltons.find(e.first);
			if (e.first > 0 && (it == base.boltons.end() || it->second != e.second))
			{
				b.WriteId((uint32_t)e.first);
				b.Write((uint32_t)e.second, w.w1);
			}
		}
		b.WriteId(0);
		for (auto &e : base.boltons)
			if (e.first > 0 && !cur.boltons.count(e.first))
			{
				b.WriteId((uint32_t)e.first);
				b.Write((uint32_t)e.second, w.w1);
			}
		b.WriteId(0);
	}
	else
		b.Write(0, 1);
	if (cur.skins != base.skins)
	{
		b.Write(1, 1);
		for (auto &e : cur.skins)
		{
			auto it = base.skins.find(e.first);
			if (it == base.skins.end() || it->second != e.second)
			{
				b.Write(1, 1);
				b.Write((uint32_t)e.first.first, w.w2);
				b.Write((uint32_t)e.first.second, w.w1);
				b.Write((uint32_t)e.second, w.w3);
			}
		}
		b.Write(0, 1);
		for (auto &e : base.skins)
			if (!cur.skins.count(e.first))
			{
				b.Write(1, 1);
				b.Write((uint32_t)e.first.first, w.w2);
				b.Write((uint32_t)e.first.second, w.w1);
			}
		b.Write(0, 1);
	}
	else
		b.Write(0, 1);
}

static void ReadState(Bits &b, NetState &s, float frameTime, const Widths &w)
{
	s.on = b.Read(1) != 0;
	if (b.Read(1))
		s.speed = (int)b.Read(4);
	s.paused = b.Read(1) != 0;
	s.reverse = b.Read(1) != 0;
	if (w.w1 > 0)
	{
		if (b.Read(1))
			s.attachBolt = (int)b.Read(w.w1);
	}
	else
		s.attachBolt = 0;
	if (w.w0 > 0)
	{
		if (b.Read(1))
			s.seq = (int)b.Read(w.w0);
	}
	else
		s.seq = 0;
	if (b.Read(1))
		s.mode = (int)b.Read(2);
	if (b.Read(1))
	{
		int32_t t1 = b.ReadTime(), t2 = b.ReadTime();
		s.A = frameTime + (float)t1 / 120.0f;
		s.S = s.A + (float)t2 / 120.0f;
	}
	if (s.paused && b.Read(1))
		s.pauseEnd = b.Read(1) ? 1e30f : b.ReadFloat();
	if (b.Read(1))
		for (int i = 0; i < 4; i++)
			if (b.Read(1))
				s.tint[i] = (uint8_t)b.Read(8);
	if (b.Read(1))
		ReadTransform(b, s.xf);
	if (b.Read(1))
		ReadPartMask(b, s.parts);
	if (b.Read(1))
	{
		for (;;)
		{
			uint32_t id = b.ReadId();
			if (!id || b.Overflowed())
				break;
			s.boltons[(int)id] = (int)b.Read(w.w1);
		}
		for (;;)
		{
			uint32_t id = b.ReadId();
			if (!id || b.Overflowed())
				break;
			b.Read(w.w1);
			s.boltons.erase((int)id);
		}
	}
	if (b.Read(1))
	{
		while (b.Read(1) && !b.Overflowed())
		{
			int m = (int)b.Read(w.w2), p = (int)b.Read(w.w1), k = (int)b.Read(w.w3);
			s.skins[std::make_pair(m, p)] = k;
		}
		while (b.Read(1) && !b.Overflowed())
		{
			int m = (int)b.Read(w.w2), p = (int)b.Read(w.w1);
			s.skins.erase(std::make_pair(m, p));
		}
	}
}

// exported for tests
void NetWriteStateForTest(om_ghoul_bits_t *bits, const NetState &cur, const NetState &base, float frameTime, int w0,
	int w1, int w2, int w3)
{
	Bits b(bits);
	WriteState(b, cur, base, frameTime, Widths{w0, w1, w2, w3});
}
void NetReadStateForTest(om_ghoul_bits_t *bits, NetState &s, float frameTime, int w0, int w1, int w2, int w3)
{
	Bits b(bits);
	ReadState(b, s, frameTime, Widths{w0, w1, w2, w3});
}

// ------------------------------------------------------------------ baseline (SPEC-013 section 2)

void Object::TakeBaseline()
{
	if (baselineTaken)
		return;
	baselineTaken = true;
	delete baseline;
	if (!insts.empty())
		baseline = new NetState(insts.begin()->second->Capture()); // lowest-id instance
	else
		baseline = new NetState(DefaultState((int)parts.size()));
	baseline->parts.resize(parts.size(), 1);
}

// ------------------------------------------------------------------ server

void System::NetObjectCreated(Object *o)
{
	for (auto &c : ND(this).clients)
		c.second.pendingDefs.push_back(o->id);
}

void System::NetObjectRemoved(Object *o)
{
	if (!net)
		return;
	for (auto &c : net->clients)
	{
		ServerClient &sc = c.second;
		sc.pendingDefs.erase(std::remove(sc.pendingDefs.begin(), sc.pendingDefs.end(), o->id), sc.pendingDefs.end());
		if (sc.known.erase(o->id))
			sc.removedObjects.push_back(o->id);
	}
}

void System::NetInstRemoved(Inst *i)
{
	if (!net)
		return;
	if (!IsServer())
	{
		net->instStates.erase(i->id);
		return;
	}
	for (auto &c : net->clients)
	{
		ServerClient &sc = c.second;
		if (sc.sentInsts.erase(i->id))
			sc.removedInsts.push_back(i->id);
		for (auto it = sc.queue.begin(); it != sc.queue.end();)
			it = it->second == i->id ? sc.queue.erase(it) : std::next(it);
		sc.written.erase(std::remove_if(sc.written.begin(), sc.written.end(),
							 [i](const std::pair<int, NetState> &w) { return w.first == i->id; }),
			sc.written.end());
	}
}

void System::NetClientConnected(int client)
{
	if (!IsServer())
		return;
	ServerClient sc;
	for (auto &o : objects)
		sc.pendingDefs.push_back(o.first);
	ND(this).clients[client] = sc;
	for (auto &i : insts)
		i.second->net.erase(client);
}


void System::NetClientDropped(int client)
{
	if (!net)
		return;
	net->clients.erase(client);
	for (auto &i : insts)
		i.second->net.erase(client);
}

void System::NetResendAll()
{
	if (!net)
		return;
	std::vector<int> ids;
	for (auto &c : net->clients)
		ids.push_back(c.first);
	for (int c : ids)
	{
		NetClientDropped(c);
		NetClientConnected(c);
	}
}

void System::NetDropAll()
{
	if (!net)
		return;
	net->clients.clear();
	for (auto &i : insts)
	{
		i.second->net.clear();
		i.second->netStates.clear();
	}
}

static void EnsureStrings(System *sys, Object *o)
{
	StringTable &st = sys->strings;
	for (auto &x : o->seqs)
		st.Add(x.path);
	for (auto &x : o->parts)
		st.Add(x.name);
	for (auto &x : o->notes)
		st.Add(x);
	for (auto &x : o->mats)
		st.Add(x.name);
	for (auto &x : o->skins)
		st.Add(x.name);
}

static void WriteDefinition(Bits &b, System *sys, Object *o)
{
	if (!o->baselineTaken)
		o->TakeBaseline();
	StringTable &st = sys->strings;
	b.WriteId((uint32_t)o->id);
	auto list = [&](const std::vector<int> &idx) {
		b.WriteId((uint32_t)idx.size());
		int prev = -1;
		for (int i : idx)
		{
			b.WriteId((uint32_t)((i - (prev + 1)) & 0xFFFF));
			prev = i;
		}
	};
	std::vector<int> l;
	for (auto &s : o->seqs)
		l.push_back(st.Add(s.path));
	list(l);
	l.clear();
	for (auto &p : o->parts)
		l.push_back(st.Add(p.name));
	list(l);
	l.clear();
	for (auto &n : o->notes)
		l.push_back(st.Add(n));
	list(l);
	l.clear();
	for (auto &m : o->mats)
		l.push_back(st.Add(m.name));
	list(l);
	b.WriteId((uint32_t)o->skins.size());
	int prev = -1, prevMat = -1;
	for (size_t i = 0; i < o->skins.size(); i++)
	{
		const SkinEntry &s = o->skins[i];
		bool extra = i == 0 || s.material != prevMat;
		b.Write(extra ? 1 : 0, 1);
		if (extra)
		{
			if (s.material == 0)
			{
				b.Write(1, 1); // material 0: present bit 1 + value 0
				b.Write(0, 2);
				b.Write(0, 4);
			}
			else
				b.WriteId((uint32_t)s.material);
		}
		prevMat = s.material;
		int idx = st.Add(s.name);
		b.WriteId((uint32_t)((idx - (prev + 1)) & 0xFFFF));
		prev = idx;
	}
	NetState def = DefaultState((int)o->parts.size());
	WriteState(b, *o->baseline, def, 0.0f, ObjectWidths(o));
}

bool System::NetWriteReliable(int client, int frame, om_ghoul_bits_t *msg)
{
	(void)frame;
	if (!IsServer() || !msg || !net || !net->clients.count(client))
		return true;
	ServerClient &sc = net->clients[client];
	Bits b(msg);
	int limit = msg->maxbits;
	msg->maxbits = limit - 4; // room for the four list terminators
	bool full = false;
	auto record = [&](const std::function<void()> &fn) -> bool {
		int save = b.Pos();
		fn();
		if (b.Overflowed())
		{
			b.ClearOverflow();
			b.Seek(save);
			full = true;
			return false;
		}
		return true;
	};
	// A: string table additions
	while (!full && sc.stringsSent + 1 < strings.Count())
	{
		int i = sc.stringsSent + 1;
		if (!record([&] {
				b.WriteId((uint32_t)i);
				b.WriteString(strings.Get(i));
			}))
			break;
		sc.stringsSent = i;
	}
	msg->maxbits = limit;
	b.WriteId(0);
	msg->maxbits = limit - 3;
	// B: object definitions (only when the strings are complete)
	while (!full && !sc.pendingDefs.empty())
	{
		int oid = sc.pendingDefs.front();
		auto it = objects.find(oid);
		if (it == objects.end())
		{
			sc.pendingDefs.pop_front();
			continue;
		}
		// every name of the definition must already be in the client's string table
		EnsureStrings(this, it->second);
		if (sc.stringsSent + 1 < strings.Count())
			break; // new strings go first, in a later message
		if (!record([&] { WriteDefinition(b, this, it->second); }))
			break;
		sc.known.insert(oid);
		sc.pendingDefs.pop_front();
	}
	msg->maxbits = limit;
	b.WriteId(0);
	msg->maxbits = limit - 2;
	// C: removed objects
	while (!full && !sc.removedObjects.empty())
	{
		int oid = sc.removedObjects.front();
		if (!record([&] { b.WriteId((uint32_t)oid); }))
			break;
		sc.removedObjects.pop_front();
	}
	msg->maxbits = limit;
	b.WriteId(0);
	msg->maxbits = limit - 1;
	// D: removed instances
	while (!full && !sc.removedInsts.empty())
	{
		int iid = sc.removedInsts.front();
		if (!record([&] { b.WriteId((uint32_t)iid); }))
			break;
		sc.removedInsts.pop_front();
	}
	msg->maxbits = limit;
	b.WriteId(0);
	return !NetReliablePending(client);
}

bool System::NetReliablePending(int client)
{
	if (!net || !net->clients.count(client))
		return false;
	ServerClient &sc = net->clients[client];
	return sc.stringsSent + 1 < strings.Count() || !sc.pendingDefs.empty() || !sc.removedObjects.empty() ||
		!sc.removedInsts.empty();
}

void System::NetBeginFrame(int client, int frame)
{
	if (!IsServer())
		return;
	ServerClient &sc = ND(this).clients[client];
	sc.curFrame = frame;
	sc.queue.clear();
	sc.written.clear();
	net->curClient = client;
}

GhoulID System::NetOfferInst(Inst *i, float priority)
{
	if (!IsServer() || !net || net->curClient < 0 || !net->clients.count(net->curClient))
		return 0;
	ServerClient &sc = net->clients[net->curClient];
	if (!sc.known.count(i->obj->id))
		return 0;
	if (!i->seq && !i->parent)
		return 0;
	for (auto &c : i->children)
		if (!NetOfferInst(c.inst, priority - 0.1f))
			return 0;
	bool queued = false;
	for (auto &q : sc.queue)
		if (q.second == i->id)
			queued = true;
	if (!queued)
		sc.queue.insert(std::make_pair(priority, i->id));
	return (GhoulID)i->id;
}

bool System::NetWriteUnreliable(int client, int frame, om_ghoul_bits_t *msg, float time)
{
	if (!IsServer() || !msg || !net || !net->clients.count(client))
		return false;
	ServerClient &sc = net->clients[client];
	Bits b(msg);
	int limit = msg->maxbits;
	msg->maxbits = limit - 1; // terminator
	bool any = false;
	for (auto &q : sc.queue)
	{
		auto it = insts.find(q.second);
		if (it == insts.end())
			continue;
		Inst *in = it->second;
		// I1d: the id's removal is still queued for this client - the instance was destroyed and its id taken by a
		// new one before the reliable removal went out (SPEC-010: freed ids are reused; the reliable stream can lag
		// the frames by its acknowledgements). The new instance is not written until the removal has gone, or the
		// client, which still holds the old instance under this id, would apply the new one's records to it.
		if (std::find(sc.removedInsts.begin(), sc.removedInsts.end(), in->id) != sc.removedInsts.end())
			continue;
		Object *o = in->obj;
		if (!o->baselineTaken)
			o->TakeBaseline();
		NetState cur = in->Capture();
		const NetState *base = o->baseline;
		uint32_t offset = 0;
		auto &cn = in->net[client]; // a new instance for this client starts with no acknowledged frame
		// SPEC-AMEND-005 I3.1 rule 5: the base is the state stored for exactly this instance's acknowledged frame;
		// none, or a state no longer stored, gives the object baseline (offset 0)
		if (cn.ack >= 0)
		{
			auto f = in->netStates.find(cn.ack);
			if (f != in->netStates.end())
			{
				base = &f->second;
				offset = (uint32_t)(frame - f->first);
			}
		}
		NetState bcopy = *base;
		bcopy.parts.resize(o->parts.size(), 1);
		// rule 6: not written, no state stored - but only once the client holds the instance. An instance this
		// client never received must get its first record even when it equals the object baseline (after a
		// `load` the baseline is taken with the restored instances present, so the lowest-id instance of every
		// object equals it; skipping it left the parents' bolt-on maps naming children the client never got:
		// "Missing Child" x 4-8 on kos1 after `load`, docs/impl/I1b.md "Needs from I3"; I1d)
		if (StatesEqual(cur, bcopy) && sc.sentInsts.count(in->id))
			continue;
		// rule 6: stored under the current frame unless one is stored already (the oldest go beyond 20), also when the
		// record is then dropped by the byte budget (such a state never becomes a base: its frame set lacks it)
		if (!in->netStates.count(frame))
		{
			in->netStates[frame] = cur;
			while (in->netStates.size() > 20)
				in->netStates.erase(in->netStates.begin());
		}
		int save = b.Pos();
		b.WriteId((uint32_t)in->id);
		b.WriteId((uint32_t)o->id);
		b.WriteFrameOffset(offset);
		WriteState(b, cur, bcopy, time, ObjectWidths(o));
		if (b.Overflowed())
		{
			// this record and all later ones are dropped for this frame
			b.ClearOverflow();
			b.Seek(save);
			break;
		}
		any = true;
		sc.written.push_back(std::make_pair(in->id, cur));
		sc.sentInsts.insert(in->id);
	}
	msg->maxbits = limit;
	b.WriteId(0);
	sc.queue.clear();
	return any;
}

void System::NetCommitFrame(int client, int frame)
{
	if (!net || !net->clients.count(client))
		return;
	ServerClient &sc = net->clients[client];
	// SPEC-AMEND-005 I3.1 rule 1: the frame remembers the instances whose records were written and fitted
	if (!sc.written.empty())
	{
		std::vector<int> &set = sc.frameSets[frame];
		set.clear();
		for (auto &w : sc.written)
			set.push_back(w.first);
	}
	sc.written.clear();
	sc.framesSent.push_back(frame);
	while (sc.framesSent.size() > 100)
		sc.framesSent.pop_front();
}

void System::NetAcknowledge(int client, int frame)
{
	if (!net || !net->clients.count(client))
		return;
	ServerClient &sc = net->clients[client];
	// SPEC-AMEND-005 I3.1 rule 4: a negative acknowledgement sets "none" for every instance
	if (frame < 0)
	{
		for (auto &i : insts)
		{
			auto it = i.second->net.find(client);
			if (it != i.second->net.end())
				it->second.ack = -1;
		}
		sc.frameSets.clear();
		return;
	}
	// rule 2: only the instances of exactly that frame's set move their acknowledged frame; older sets are discarded
	auto fs = sc.frameSets.find(frame);
	if (fs == sc.frameSets.end())
	{
		sc.frameSets.erase(sc.frameSets.begin(), sc.frameSets.lower_bound(frame));
		return;
	}
	std::vector<int> ids = fs->second;
	sc.frameSets.erase(sc.frameSets.begin(), sc.frameSets.lower_bound(frame));
	for (int id : ids)
	{
		auto it = insts.find(id);
		if (it == insts.end())
			continue;
		Inst *in = it->second;
		in->net[client].ack = frame;
		// rule 3: states older than the smallest acknowledged frame of all clients of the instance are discarded
		// (a client with none keeps everything; the acknowledged states themselves are kept)
		int minAck = frame;
		for (auto &c : in->net)
			if (c.second.ack < minAck)
				minAck = c.second.ack;
		if (minAck >= 0)
			in->netStates.erase(in->netStates.begin(), in->netStates.lower_bound(minAck));
	}
}

// ------------------------------------------------------------------ client

int g_lastReliableBitsUsed = 0; // test hook: bits consumed by the last applied reliable chunk

static void ApplyChunk(System *sys, const unsigned char *data, int bytes, bool specular)
{
	NetData &nd = ND(sys);
	om_ghoul_bits_t raw = {const_cast<unsigned char *>(data), bytes * 8, 0, 0};
	Bits b(&raw);
	// A: strings
	for (;;)
	{
		uint32_t id = b.ReadId();
		if (!id)
			break;
		std::string s = b.ReadString();
		nd.rstrings.Add(s); // appended in order; the value is ignored
		if (b.Overflowed())
			Fatal("StringTable underflowed");
	}
	// B: object definitions
	for (;;)
	{
		uint32_t oid = b.ReadId();
		if (!oid)
			break;
		int cid = -(int)oid;
		Object *o = sys->NewObject(cid, true);
		auto readList = [&](std::vector<std::string> &out) {
			uint32_t n = b.ReadId();
			int prev = -1;
			for (uint32_t i = 0; i < n && !b.Overflowed(); i++)
			{
				int idx = (int)((b.ReadId() + (uint32_t)(prev + 1)) & 0xFFFF);
				out.push_back(nd.rstrings.Get(idx));
				prev = idx;
			}
		};
		std::vector<std::string> l1, l2, l3, l4;
		readList(l1);
		readList(l2);
		readList(l3);
		readList(l4);
		for (auto &s : l1)
			o->RegisterSequence(s.c_str(), false);
		for (auto &s : l2)
			o->RegisterPart(s.c_str());
		for (auto &s : l3)
			o->RegisterNote(s.c_str());
		for (auto &s : l4)
			o->RegisterMaterial(s.c_str());
		uint32_t ns = b.ReadId();
		int prev = -1, mat = 0;
		for (uint32_t i = 0; i < ns && !b.Overflowed(); i++)
		{
			if (b.Read(1))
				mat = (int)b.ReadId();
			int idx = (int)((b.ReadId() + (uint32_t)(prev + 1)) & 0xFFFF);
			prev = idx;
			o->RegisterSkin((GhoulID)mat, nd.rstrings.Get(idx).c_str(), 0);
		}
		o->Prepare(specular);
		NetData::Widths w;
		w.w0 = BitsFor((int)l1.size());
		w.w1 = BitsFor((int)l2.size());
		w.w2 = BitsFor((int)l4.size());
		w.w3 = BitsFor((int)ns);
		w.parts = (int)l2.size();
		nd.objWidths[cid] = w;
		NetState base = DefaultState(w.parts);
		ReadState(b, base, 0.0f, Widths{w.w0, w.w1, w.w2, w.w3});
		delete o->baseline;
		o->baseline = new NetState(base);
		o->baselineTaken = true;
		if (b.Overflowed())
			Fatal("Reliable underflowed");
	}
	// C: removed objects
	for (;;)
	{
		uint32_t oid = b.ReadId();
		if (!oid)
			break;
		auto it = sys->objects.find(-(int)oid);
		if (it != sys->objects.end())
			it->second->DestroyNow(false);
		nd.objWidths.erase(-(int)oid);
	}
	// D: removed instances
	for (;;)
	{
		uint32_t iid = b.ReadId();
		if (!iid)
			break;
		auto it = sys->insts.find(-(int)iid);
		if (it != sys->insts.end())
			it->second->Destroy();
		nd.instStates.erase(-(int)iid);
	}
	if (b.Overflowed())
		Fatal("Reliable underflowed");
	g_lastReliableBitsUsed = raw.bit;
}

static void ApplyStored(System *sys, bool specular)
{
	NetData &nd = ND(sys);
	while (nd.applied + 4 <= nd.stream.size())
	{
		const unsigned char *p = nd.stream.data() + nd.applied;
		uint32_t len = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
		if (nd.applied + 4 + len > nd.stream.size())
			break;
		ApplyChunk(sys, p + 4, (int)len, specular);
		nd.applied += 4 + len;
	}
}

void System::ClientStartAccepting()
{
	NetData &nd = ND(this);
	nd.accepting = true;
	ApplyStored(this, G().specular);
}

void System::ClientReadReliable(int, om_ghoul_bits_t *msg, bool specular)
{
	if (!msg)
		return;
	NetData &nd = ND(this);
	uint32_t len = (uint32_t)((msg->maxbits + 7) / 8);
	unsigned char hdr[4] = {(unsigned char)len, (unsigned char)(len >> 8), (unsigned char)(len >> 16),
		(unsigned char)(len >> 24)};
	nd.stream.insert(nd.stream.end(), hdr, hdr + 4);
	nd.stream.insert(nd.stream.end(), msg->data, msg->data + len);
	msg->bit = msg->maxbits;
	if (nd.accepting)
		ApplyStored(this, specular);
}

void System::ClientReadUnreliable(int frame, om_ghoul_bits_t *msg, float time)
{
	if (!msg)
		return;
	NetData &nd = ND(this);
	Bits b(msg);
	for (;;)
	{
		uint32_t iid = b.ReadId();
		if (!iid)
			break;
		uint32_t oid = b.ReadId();
		uint32_t offset = b.ReadFrameOffset();
		int cid = -(int)iid;
		Inst *in = nullptr;
		auto it = insts.find(cid);
		if (it != insts.end())
			in = it->second;
		// AMEND-009 D2 (G decision 2026-10-26): records are matched to an instance by id only; for an existing
		// instance the record's object id is read but not compared and the delta is applied to it. The server
		// keeps the id-reuse avoidance (ServerWriteUnreliable: no record for an id with a queued removal).
		if (!in)
		{
			if (!oid)
				Fatal("Null object to create remote instance from.");
			auto ot = objects.find(-(int)oid);
			if (ot == objects.end())
				Fatal("No object found to create remote instance from.");
			in = NewInst(ot->second, cid);
		}
		Object *o = in->obj;
		NetData::Widths w = nd.objWidths.count(o->id) ? nd.objWidths[o->id] : NetData::Widths();
		ClientInstStates &cs = nd.instStates[cid];
		NetState s;
		int baseFrame = frame - (int)offset;
		if (offset == 0 || !cs.states.count(baseFrame))
		{
			s = o->baseline ? *o->baseline : DefaultState(w.parts);
			if (offset != 0)
				Print("Ghoul :missing delta base %d for instance %d\n", baseFrame, (int)iid);
		}
		else
			s = cs.states[baseFrame];
		s.parts.resize((size_t)w.parts, 1);
		NetState before = in->Capture();
		std::map<int, int> baseBoltons = s.boltons;
		ReadState(b, s, time, Widths{w.w0, w.w1, w.w2, w.w3});
		if (b.Overflowed())
			Fatal("Delta underflowed");
		cs.states[frame] = s;
		cs.states.erase(cs.states.begin(), cs.states.lower_bound(offset ? baseFrame : frame));
		if (cs.states.size() > 1000)
			Fatal("More than 1000 unacked deltas from server");
		// the live instance takes the decoded state
		NetState applied = s;
		in->Apply(applied, false);
		bool boltChange = baseBoltons != s.boltons || offset == 0;
		if (boltChange)
		{
			for (size_t k = 0; k < in->children.size();)
			{
				Inst *c = in->children[k].inst;
				if (c->id < 0)
					c->DetachFromParent();
				else
					k++;
			}
			for (auto &e : s.boltons)
			{
				auto ci = insts.find(-e.first);
				if (ci == insts.end())
				{
					// the original prints the bare line; the ids help the test logs (I1d)
					Print("Missing Child (instance %d bolt %d of %d)\n", e.first, e.second, (int)iid);
					continue;
				}
				Inst *c = ci->second;
				c->DetachFromParent();
				c->parent = in;
				in->children.push_back(Inst::Child{c, e.second});
			}
		}
		if (before.seq != s.seq || before.mode != s.mode || before.A != s.A || before.S != s.S)
		{
			in->cursorTime = in->A;
			in->cursorPhase = in->RawPhase(in->A);
		}
	}
	if (b.Overflowed())
		Fatal("Delta underflowed");
}

int System::ClientReliableSize() { return net ? (int)net->stream.size() : 0; }
void *System::ClientReliableData() { return net && !net->stream.empty() ? net->stream.data() : nullptr; }

} // namespace ghoul
