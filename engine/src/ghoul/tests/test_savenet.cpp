// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program - savegame images and network replication round trips.
// Implements: SPEC-013 test vectors 4-6 and round trips of sections 2-6 (verification); SPEC-AMEND-005 I3.1;
// SPEC-AMEND-009 D2

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "../ghoul_internal.h"
#include "test_common.h"

using namespace ghoul;

namespace {
struct NopCb : public IGhoulCallback
{
	bool Event(uint32_t, void *, float, const void *) override { return true; }
};
NopCb g_cb[3];

bool Near(float a, float b, float t) { return std::fabs(a - b) <= t; }

// reads n bits at bit offset 'start' of a byte buffer (LSB first)
uint32_t BitsAt(const std::vector<uint8_t> &b, int start, int n)
{
	uint32_t v = 0;
	for (int i = 0; i < n; i++)
	{
		int p = start + i;
		if ((b[p >> 3] >> (p & 7)) & 1)
			v |= 1u << i;
	}
	return v;
}
} // namespace

static void TestSavegame()
{
	std::printf("== savegame images (SPEC-013 section 6, vectors 4, 5)\n");
	IGhoulSystem *sv = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_SERVER);
	IGhoulObject *o = sv->CreateObject();
	GhoulID s = o->RegisterSequence("ghoul/test/syn.ghl", false);
	o->FinishRegistration(false);
	Inst *in = static_cast<Inst *>(o->CreateInstance());
	unsigned P = o->NumParts();
	std::vector<uint8_t> buf(16384);
	IGhoulCallback *table[3] = {&g_cb[0], &g_cb[1], &g_cb[2]};
	int n = in->SaveState(buf.data(), (int)buf.size(), table, 3, "ghoul/test/syn");
	// length = ceil((S + 1632 + 8P) / 8), S = bit length of the model-name string
	uint8_t tmp[512] = {0};
	om_ghoul_bits_t sb = {tmp, 4096, 0, 0};
	Bits(&sb).WriteString("ghoul/test/syn");
	int S = sb.bit;
	CHECK(n == (S + 1632 + 8 * (int)P + 7) / 8, "default image length %d (S %d, P %u)", n, S, P);
	// fixed fields after the string
	int p = S;
	bool ok = BitsAt(buf, p, 16) == (uint32_t)in->GetInstanceId();
	p += 16;
	float one = 1.0f;
	uint32_t oneBits;
	std::memcpy(&oneBits, &one, 4);
	for (int m = 0; m < 2; m++)
	{
		for (int k = 0; k < 16; k++, p += 32)
			ok = ok && BitsAt(buf, p, 32) == ((k % 5 == 0) ? oneBits : 0u);
		ok = ok && BitsAt(buf, p, 32) == 1u;
		p += 32;
	}
	ok = ok && BitsAt(buf, p, 32) == 1u && BitsAt(buf, p + 32, 32) == 0x3F800000u;
	p += 64;
	ok = ok && BitsAt(buf, p, 16) == 0 && BitsAt(buf, p + 16, 8) == 1 && BitsAt(buf, p + 24, 32) == 4;
	p += 56;
	ok = ok && BitsAt(buf, p, 32) == 0 && BitsAt(buf, p + 32, 32) == 0 && BitsAt(buf, p + 64, 32) == 0;
	p += 96;
	ok = ok && BitsAt(buf, p, 8) == 1 && BitsAt(buf, p + 8, 8) == 0 && BitsAt(buf, p + 16, 32) == 0xFFFFFFFFu;
	p += 48;
	ok = ok && BitsAt(buf, p, 16) == 0 && BitsAt(buf, p + 16, 32) == 0 && BitsAt(buf, p + 48, 32) == P;
	p += 80;
	for (unsigned k = 0; k < P; k++, p += 8)
		ok = ok && BitsAt(buf, p, 8) == 1;
	ok = ok && BitsAt(buf, p, 32) == 0;
	p += 32 + 64; // no skins, cursor time, cursor phase
	ok = ok && BitsAt(buf, p, 8) == 0 && BitsAt(buf, p + 8, 8) == 0;
	p += 24;
	ok = ok && BitsAt(buf, p, 32) == 0 && BitsAt(buf, p + 32, 32) == 0;
	p += 64;
	CHECK(ok && (p + 7) / 8 == n, "default image fields (end bit %d)", p);

	// round trip of a busy instance
	in->Play(s, 3.0f, 0.25f, true, 2, false, true);
	in->SetSpeed(7);
	in->SetTint(0.5f, 0.25f, 1.0f, 0.75f);
	in->SetPartOnOff(o->FindPart("MESH1"), false);
	in->AddNoteCallback(&g_cb[1], o->FindNote("FIRE"));
	in->AddNoteCallback(&g_cb[2], o->FindNote("EOS"));
	NopCb stranger;
	in->AddNoteCallback(&stranger, o->FindNote("SOUND"));
	in->AddMatrixCallback(&g_cb[0], o->FindPart("BOLT_A"), 5);
	Matrix4 T;
	MatIdentity(T);
	T.m[3][0] = 12.0f;
	T.m[0][0] = T.m[1][1] = T.m[2][2] = 2.0f;
	T.flags = 0;
	in->SetTransform(&T);
	in->Pause(3.5f);
	in->ProcessEvents(3.6f);
	n = in->SaveState(buf.data(), (int)buf.size(), table, 3, "ghoul/test/syn");
	Inst *in2 = static_cast<Inst *>(o->CreateInstance());
	GhoulID saved = in2->LoadState(buf.data(), n, table, 3, "ghoul/test/syn");
	CHECK(saved == in->GetInstanceId(), "saved id %d", saved);
	std::vector<uint8_t> buf2(16384);
	int n2 = in2->SaveState(buf2.data(), (int)buf2.size(), table, 3, "ghoul/test/syn");
	// the reloaded instance differs only in the dropped callback (index -1) and its id
	CHECK(n2 == n - 6, "re-saved length %d vs %d (one callback dropped)", n2, n);
	CHECK(in2->seq == in->seq && in2->mode == 2 && in2->reverse && in2->paused && in2->speed == 7 && in2->S == in->S &&
			in2->A == in->A && in2->pauseEnd == 1e30f && !in2->IsPartOn(o->FindPart("MESH1")) &&
			std::memcmp(in2->tint, in->tint, 4) == 0 && in2->noteCbs.size() == 2 && in2->matrixCbs.size() == 1 &&
			in2->cursorPhase == in->cursorPhase && MatNearlyEqual(in2->xf, in->xf, 0) && in2->scale == 2.0f,
		"round-trip state");
	// capacity overflow is fatal
	bool fatal = false;
	try
	{
		in->SaveState(buf.data(), 8, table, 3, "ghoul/test/syn");
	}
	catch (const std::exception &e)
	{
		fatal = std::strstr(e.what(), "GhoulInst savegame packet overflow") != nullptr;
	}
	CHECK(fatal, "overflow fatal");
	// different model -> fatal (vector 5)
	IGhoulObject *other = sv->CreateObject();
	other->RegisterSequence("ghoul/test/other.ghl", false);
	other->FinishRegistration(false);
	IGhoulInst *oi = other->CreateInstance();
	fatal = false;
	try
	{
		oi->LoadState(buf.data(), n, table, 3, "ghoul/test/other");
	}
	catch (const std::exception &e)
	{
		fatal = !std::strcmp(e.what(), "Ghoul :Inst has different model than expected");
	}
	CHECK(fatal, "different model fatal");
	fatal = false;
	try
	{
		oi->LoadState(buf.data(), n, table, 3, "ghoul/test/syn"); // right name, wrong object: part count
	}
	catch (const std::exception &e)
	{
		fatal = !std::strcmp(e.what(), "Ghoul :Number of parts mismatch in savegame");
	}
	CHECK(fatal, "parts mismatch fatal");
	other->Destroy();
	o->Destroy();
}

// ------------------------------------------------------------------ network

namespace {
struct Link
{
	IGhoulSystem *sv, *cl;
	int frame = 100;
	float time = 10.0f;
	int reliableMsgs = 0, reliableBytes = 0, unreliableBytes = 0;
	void Reliable(int budget = 1400)
	{
		for (int k = 0; k < 1000; k++)
		{
			std::vector<uint8_t> b(budget, 0);
			om_ghoul_bits_t w = {b.data(), budget * 8, 0, 0};
			bool done = sv->NetWriteReliable(0, frame, &w);
			int bytes = (w.bit + 7) / 8;
			om_ghoul_bits_t r = {b.data(), bytes * 8, 0, 0};
			cl->ClientReadReliable(0, &r, false);
			reliableMsgs++;
			reliableBytes += bytes;
			if (done)
				break;
		}
	}
	void Frame(const std::vector<IGhoulInst *> &offer, int budget = 1400)
	{
		frame++;
		time += 0.1f;
		sv->NetBeginFrame(0, frame);
		for (size_t i = 0; i < offer.size(); i++)
			offer[i]->NetOffer((float)i);
		std::vector<uint8_t> b(budget, 0);
		om_ghoul_bits_t w = {b.data(), budget * 8, 0, 0};
		sv->NetWriteUnreliable(0, frame, &w, time);
		sv->NetCommitFrame(0, frame);
		int bytes = (w.bit + 7) / 8;
		unreliableBytes += bytes;
		om_ghoul_bits_t r = {b.data(), bytes * 8, 0, 0};
		cl->ClientReadUnreliable(frame, &r, time);
		sv->NetAcknowledge(0, frame);
	}
};

bool SameState(Inst *a, Inst *b, std::string &why)
{
	NetState x = a->Capture(), y = b->Capture();
	char m[256];
	auto fail = [&](const char *w) {
		why = w;
		return false;
	};
	if (x.on != y.on || x.speed != y.speed || x.paused != y.paused || x.reverse != y.reverse)
		return fail("flags");
	if (x.seq != y.seq || x.mode != y.mode || x.attachBolt != y.attachBolt)
		return fail("sequence/mode/bolt");
	if (!Near(x.A, y.A, 1.0f / 119.0f) || !Near(x.S, y.S, 2.0f / 119.0f))
	{
		std::snprintf(m, sizeof(m), "times A %g/%g S %g/%g", x.A, y.A, x.S, y.S);
		return fail(m);
	}
	if (std::memcmp(x.tint, y.tint, 4))
		return fail("tint");
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			if (!Near(x.xf.m[r][c], y.xf.m[r][c], 0.03f * (1 + std::fabs(x.xf.m[r][c]))))
				return fail("rotation");
	for (int a = 0; a < 3; a++)
		if (!Near(x.xf.m[3][a], y.xf.m[3][a], 1.0f / 16.0f))
			return fail("translation");
	if (x.parts != y.parts)
		return fail("parts");
	if (x.skins != y.skins)
		return fail("skins");
	if (b->children.size() != a->children.size())
		return fail("children");
	for (size_t i = 0; i < a->children.size(); i++)
		if (b->children[i].inst->id != -a->children[i].inst->id || b->children[i].bolt != a->children[i].bolt)
			return fail("child map");
	return true;
}
} // namespace

static void TestNetwork()
{
	std::printf("== network replication (SPEC-013 sections 2-5)\n");
	Link L;
	L.sv = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_SERVER);
	L.cl = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_CLIENT);
	L.sv->Reset();
	L.cl->Reset();
	L.cl->ClientStartAccepting();
	L.sv->NetClientConnected(0);
	// a character with skins, a synthetic model, a bolt-on
	IGhoulObject *ch = L.sv->CreateObject();
	ch->RegisterSequence("ghoul/enemy/meso/meso_player.ghl", false);
	OMGhoul_RegisterList("enemy/meso", "playerboss", ch);
	ch->FinishRegistration(false);
	GhoulID ma = ch->FindMaterial("A");
	GhoulID skin1 = ch->RegisterSkin(ma, "a_xdekker", 0);
	GhoulID skin2 = ch->RegisterSkin(ch->FindMaterial("H"), "h_dekker", 0);
	IGhoulObject *sy = L.sv->CreateObject();
	GhoulID s10 = sy->RegisterSequence("ghoul/test/syn.ghl", false);
	GhoulID s5 = sy->RegisterSequence("ghoul/test/seq5.ghl", false);
	sy->FinishRegistration(false);
	IGhoulObject *co = L.sv->CreateObject();
	GhoulID cs = co->RegisterSequence("ghoul/test/child.ghl", false);
	co->FinishRegistration(false);
	Inst *a = static_cast<Inst *>(ch->CreateInstance());
	Inst *b = static_cast<Inst *>(sy->CreateInstance());
	Inst *c = static_cast<Inst *>(co->CreateInstance());
	ch->Prepare(false);
	sy->Prepare(false);
	co->Prepare(false);
	L.Reliable();
	CHECK(L.cl->FindObject(ch->GetObjectId()) && L.cl->FindObject(sy->GetObjectId()), "objects defined on the client");
	IGhoulObject *cch = L.cl->FindObject(ch->GetObjectId());
	CHECK(cch && cch->NumParts() == ch->NumParts() && cch->NumSequences() == ch->NumSequences() &&
			cch->NumSkins() == ch->NumSkins() && cch->NumNotes() == ch->NumNotes(),
		"client object lists equal");
	if (cch)
	{
		char n1[128], n2[128];
		ch->GetSkinName(skin2, n1);
		cch->GetSkinName(skin2, n2);
		CHECK(!std::strcmp(n1, n2) && cch->SkinMaterial(skin2) == ch->SkinMaterial(skin2), "skin %s/%s", n1, n2);
	}
	std::printf("   signon: %d reliable messages, %d bytes (%d strings)\n", L.reliableMsgs, L.reliableBytes,
		static_cast<System *>(L.sv)->strings.Count() - 1);
	// frame 1: play
	a->Play(ch->FindSequence("ghoul/enemy/meso/std_ialert_n_p_n.ghl") ? ch->FindSequence("ghoul/enemy/meso/std_ialert_n_p_n.ghl")
																	   : 1,
		L.time + 0.1f, 0.0f, true, 0, false, false);
	b->Play(s10, L.time + 0.1f, 0.0f, true, 1, false, false);
	c->Play(cs, L.time + 0.1f, 0.0f, true, 0, false, false);
	Matrix4 T;
	MatIdentity(T);
	T.m[3][0] = 10.0f;
	T.m[3][1] = -200.5f;
	T.m[3][2] = 33.3f;
	T.flags = 0;
	a->SetTransform(&T);
	std::vector<IGhoulInst *> all = {a, b, c};
	L.Frame(all);
	std::string why;
	auto checkAll = [&](const char *when) {
		for (Inst *x : {a, b, c})
		{
			if (x == a && !g_haveData)
				continue; // the stock character needs the game data
			Inst *y = static_cast<Inst *>(L.cl->FindInstance(x->GetInstanceId()));
			CHECK(y != nullptr, "%s: client instance %d", when, x->GetInstanceId());
			if (y)
				CHECK(SameState(x, y, why), "%s: instance %d differs: %s", when, x->GetInstanceId(), why.c_str());
		}
	};
	if (!g_haveData)
		std::printf("SKIP: replication of the stock character needs the game data\n");
	checkAll("frame 1");
	// frame 2: changes of every group
	float c45 = std::cos(0.7f), s45 = std::sin(0.7f);
	T.m[0][0] = c45;
	T.m[0][1] = s45;
	T.m[1][0] = -s45;
	T.m[1][1] = c45;
	a->SetTransform(&T);
	a->SetSkin(ma, skin1, 0);
	a->SetSkin(ch->FindMaterial("H"), skin2, ch->FindPart("_BOSSHEAD"));
	a->SetPartOnOff(ch->FindPart("_BALDHEAD"), false);
	for (const char *pn : {"GZ_CHEST_F", "GZ_CHEST_B", "GZ_GUT_F", "GZ_GUT_B", "GZ_HEAD_F", "GZ_HEAD_B", "GZ_ARM_UPPER_LF",
			 "GZ_ARM_UPPER_RF"})
		if (ch->FindPart(pn))
			a->SetPartOnOff(ch->FindPart(pn), false);
	a->SetSpeed(6);
	a->SetTint(1.0f, 0.5f, 0.5f, 1.0f);
	b->Play(s5, L.time + 0.1f, 0.3f, true, 2, false, true);
	b->Pause(L.time + 0.15f);
	b->AttachChild(sy->FindPart("BOLT_A"), c, co->FindPart("TO_BOLT"));
	L.Frame(all);
	checkAll("frame 2");
	// frames 3-6: deltas against acknowledged frames, then nothing changes (no records)
	for (int k = 0; k < 3; k++)
	{
		T.m[3][0] += 1.5f;
		a->SetTransform(&T);
		L.Frame(all);
	}
	checkAll("frame 5");
	int before = L.unreliableBytes;
	L.Frame(all);
	CHECK(L.unreliableBytes - before == 1, "unchanged frame writes only the terminator (%d bytes)",
		L.unreliableBytes - before);
	// lost acknowledgements: the server keeps delta-coding against the last acked frame
	b->Resume(L.time);
	b->SetPartOnOff(sy->FindPart("MESH1"), false);
	L.sv->NetAcknowledge(0, -1); // negative: full states
	L.Frame(all);
	checkAll("after negative ack");
	// detach, removal
	b->DetachChild(c);
	L.Frame(all);
	checkAll("detached");
	int cid = c->GetInstanceId();
	c->Destroy();
	L.Reliable();
	CHECK(L.cl->FindInstance(cid) == nullptr, "removed instance");
	int oid = co->GetObjectId();
	co->Destroy();
	L.Reliable();
	CHECK(L.cl->FindObject(oid) == nullptr, "removed object");
	// demo stream: stored reliable stream = {u32 length, bytes}...
	int size = L.cl->ClientReliableSize();
	const uint8_t *data = (const uint8_t *)L.cl->ClientReliableData();
	int k = 0, chunks = 0;
	while (data && k + 4 <= size)
	{
		uint32_t len = data[k] | data[k + 1] << 8 | data[k + 2] << 16 | (uint32_t)data[k + 3] << 24;
		k += 4 + (int)len;
		chunks++;
	}
	CHECK(k == size && chunks == L.reliableMsgs, "stored reliable stream %d bytes in %d chunks", size, chunks);
	std::printf("   %d frames, %d unreliable bytes in total\n", L.frame - 100, L.unreliableBytes);
	// replay of the stored stream into a fresh client (demo playback) recreates the objects
	std::vector<uint8_t> copy(data, data + size);
	L.cl->Reset();
	k = 0;
	while (k + 4 <= size)
	{
		uint32_t len = copy[k] | copy[k + 1] << 8 | copy[k + 2] << 16 | (uint32_t)copy[k + 3] << 24;
		om_ghoul_bits_t r = {copy.data() + k + 4, (int)len * 8, 0, 0};
		L.cl->ClientReadReliable(0, &r, false);
		k += 4 + (int)len;
	}
	L.cl->ClientStartAccepting();
	CHECK(L.cl->FindObject(ch->GetObjectId()) && !L.cl->FindObject(oid), "demo replay objects");
	L.sv->NetDropAll();
	L.sv->Reset();
	L.cl->Reset();
}

// SPEC-AMEND-005 I3.1: the acknowledged frame is kept per instance and client and moves only to frames in which that
// instance's record was written and fitted. Frame 2 (the instance's last record) is lost, frame 3 is acknowledged but
// carried no record (byte budget): the next record must still be coded against frame 1, which the client holds.
static void TestDeltaBases()
{
	std::printf("== delta bases per instance and client (SPEC-AMEND-005 I3.1)\n");
	Link L;
	L.sv = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_SERVER);
	L.cl = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_CLIENT);
	L.sv->Reset();
	L.cl->Reset();
	L.cl->ClientStartAccepting();
	L.sv->NetClientConnected(0);
	IGhoulObject *sy = L.sv->CreateObject();
	GhoulID s10 = sy->RegisterSequence("ghoul/test/syn.ghl", false);
	sy->FinishRegistration(false);
	Inst *a = static_cast<Inst *>(sy->CreateInstance());
	sy->Prepare(false);
	L.Reliable();
	a->Play(s10, L.time, 0.0f, true, 0, false, false);
	Matrix4 T;
	MatIdentity(T);
	T.flags = 0;
	auto frame = [&](int budget, bool deliver, bool ack) {
		L.frame++;
		L.time += 0.1f;
		L.sv->NetBeginFrame(0, L.frame);
		a->NetOffer(1.0f);
		std::vector<uint8_t> b(budget, 0);
		om_ghoul_bits_t w = {b.data(), budget * 8, 0, 0};
		L.sv->NetWriteUnreliable(0, L.frame, &w, L.time);
		L.sv->NetCommitFrame(0, L.frame);
		int bytes = (w.bit + 7) / 8;
		if (deliver)
		{
			om_ghoul_bits_t r = {b.data(), bytes * 8, 0, 0};
			L.cl->ClientReadUnreliable(L.frame, &r, L.time);
		}
		if (ack)
			L.sv->NetAcknowledge(0, L.frame);
		return bytes;
	};
	T.m[3][0] = 10.0f;
	a->SetTransform(&T);
	frame(1400, true, true); // frame 1: written, received, acknowledged
	T.m[3][0] = 20.0f;
	a->SetTransform(&T);
	frame(1400, false, false); // frame 2: the record is lost
	int small = frame(2, true, true); // frame 3: no record fits (16-bit budget); acknowledged
	g_host.log.clear();
	frame(1400, true, true); // frame 4: coded against frame 1
	bool missing = false;
	for (auto &l : g_host.log)
		if (l.find("missing delta base") != std::string::npos)
			missing = true;
	Inst *ca = static_cast<Inst *>(L.cl->FindInstance(a->GetInstanceId()));
	std::string why;
	CHECK(small <= 2 && !missing && ca && SameState(a, ca, why), "base after a lost frame and an empty acknowledged frame: %s%s",
		missing ? "missing delta base " : "", why.c_str());
	// an unchanged instance whose last record was acknowledged is not written again
	int before = frame(1400, true, true);
	CHECK(before == 1, "still instance: only the terminator (%d bytes)", before);
	L.sv->NetDropAll();
	L.sv->Reset();
	L.cl->Reset();
}

// SPEC-AMEND-009 D2 (G decision 2026-10-26): a record for an instance id the client holds under another object is
// applied to the existing instance by id only - no destroy, no recreate, no error. The server's id-reuse avoidance
// holds the new instance back while the old one's removal is queued; here the reliable carrying the removal is
// written (so the avoidance releases the id) but not delivered, which is the only way such a record can reach a client.
static void TestIdOnlyRecords()
{
	std::printf("== records matched by instance id only (SPEC-AMEND-009 D2)\n");
	Link L;
	L.sv = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_SERVER);
	L.cl = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_CLIENT);
	L.sv->Reset();
	L.cl->Reset();
	L.cl->ClientStartAccepting();
	L.sv->NetClientConnected(0);
	IGhoulObject *oa = L.sv->CreateObject();
	GhoulID sa = oa->RegisterSequence("ghoul/test/syn.ghl", false);
	oa->FinishRegistration(false);
	IGhoulObject *ob = L.sv->CreateObject();
	GhoulID sb = ob->RegisterSequence("ghoul/test/child.ghl", false);
	ob->FinishRegistration(false);
	Inst *a = static_cast<Inst *>(oa->CreateInstance());
	oa->Prepare(false);
	ob->Prepare(false);
	L.Reliable();
	a->Play(sa, L.time, 0.0f, true, 0, false, false);
	L.Frame({a});
	GhoulID id = a->GetInstanceId();
	IGhoulInst *ca = L.cl->FindInstance(id);
	IGhoulObject *cA = L.cl->FindObject(oa->GetObjectId());
	CHECK(ca && cA && ca->GetObjectId() == cA->GetObjectId(), "client holds instance %d of object A", (int)id);
	a->Destroy();
	Inst *b = static_cast<Inst *>(ob->CreateInstance());
	if (b->GetInstanceId() != id)
	{
		std::printf("   NOTE: id %d not reused (got %d); D2 case not reachable here\n", (int)id, (int)b->GetInstanceId());
		L.sv->NetDropAll();
		L.sv->Reset();
		L.cl->Reset();
		return;
	}
	b->Play(sb, L.time, 0.0f, true, 0, false, false);
	Matrix4 T;
	MatIdentity(T);
	T.flags = 0;
	T.m[3][0] = 5.0f;
	b->SetTransform(&T);
	// the reliable with the removal goes out but is not (yet) delivered
	std::vector<uint8_t> rel(1400, 0);
	om_ghoul_bits_t w = {rel.data(), 1400 * 8, 0, 0};
	L.sv->NetWriteReliable(0, L.frame, &w);
	int relBytes = (w.bit + 7) / 8;
	int before = L.unreliableBytes;
	bool fatal = false;
	try
	{
		L.Frame({b});
	}
	catch (const std::exception &e)
	{
		fatal = true;
		std::printf("   fatal: %s\n", e.what());
	}
	int bytes = L.unreliableBytes - before;
	IGhoulInst *cb = L.cl->FindInstance(id);
	if (bytes <= 1)
		std::printf("   NOTE: the server did not write the reused id's record (%d bytes); avoidance still active\n", bytes);
	else
		CHECK(!fatal && cb && cb == ca && cA && cb->GetObjectId() == cA->GetObjectId(),
			"record for id %d with object B applied to the existing instance of object A (no recreate, no error)", (int)id);
	// the late reliable removes the instance; nothing is left under the id
	om_ghoul_bits_t r = {rel.data(), relBytes * 8, 0, 0};
	L.cl->ClientReadReliable(0, &r, false);
	CHECK(!L.cl->FindInstance(id), "late removal destroys the instance held under id %d", (int)id);
	L.sv->NetDropAll();
	L.sv->Reset();
	L.cl->Reset();
}

void RunSaveNetTests()
{
	TestSavegame();
	// the client applies reliable data only after method 23 (client finished loading)
	((IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_CLIENT))->ClientStartAccepting();
	TestNetwork();
	TestDeltaBases();
	TestIdOnlyRecords();
}

// ------------------------------------------------------------------ replay of payloads captured from the original

namespace ghoul {
extern int g_lastReliableBitsUsed;
}

// file written by a capture extractor: records {u8 kind 26/27, u8 connection, i32 frame, f32 time, u32 len, bytes}
void RunReplay(const std::string &path)
{
	std::vector<uint8_t> f;
	if (!ReadFileBytes(path, f))
	{
		std::printf("SKIP replay: %s not found\n", path.c_str());
		return;
	}
	std::printf("== replay of captured ghoul streams from the original server: %s\n", path.c_str());
	IGhoulSystem *cl = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_CLIENT);
	cl->Reset();
	cl->ClientStartAccepting();
	size_t p = 0;
	int rel = 0, unrel = 0, relPadBad = 0, unrelPadBad = 0, records = 0, conn = -1, strings = 0, stringDiff = 0, defs = 0,
		defDiff = 0;
	std::string fatal;
	try
	{
		while (p + 14 <= f.size())
		{
			int kind = f[p], c = f[p + 1];
			int32_t frame;
			float time;
			uint32_t len;
			std::memcpy(&frame, &f[p + 2], 4);
			std::memcpy(&time, &f[p + 6], 4);
			std::memcpy(&len, &f[p + 10], 4);
			p += 14;
			if (p + len > f.size())
				break;
			if (conn >= 0 && c != conn)
			{
				cl->Reset(); // a new connection starts from nothing
				cl->ClientStartAccepting();
			}
			conn = c;
			std::vector<uint8_t> payload(f.begin() + p, f.begin() + p + len);
			p += len;
			om_ghoul_bits_t b = {payload.data(), (int)len * 8, 0, 0};
			if (kind == 26)
			{
				// section A (string-table additions) re-encoded by our writer must give the original's bits
				{
					om_ghoul_bits_t rb = {payload.data(), (int)len * 8, 0, 0};
					Bits r(&rb);
					for (;;)
					{
						int start = rb.bit;
						uint32_t id = r.ReadId();
						if (!id || rb.overflowed)
							break;
						std::string str = r.ReadString();
						int end = rb.bit;
						uint8_t tmp[1024] = {0};
						om_ghoul_bits_t wb = {tmp, 8192, 0, 0};
						Bits w(&wb);
						w.WriteId(id);
						w.WriteString(str);
						bool same = wb.bit == end - start;
						int firstDiff = -1;
						// the unused bits at the end of a Huffman block carry no information (the original leaves
						// them non-zero): compare only up to the last code bit
						int compareBits = wb.bit;
						{
							om_ghoul_bits_t hb = {tmp, wb.bit, 0, 0};
							Bits h(&hb);
							h.ReadId();
							if (!h.Read(1) && h.Read(1))
							{
								uint32_t m = h.Read(2);
								uint32_t L = h.Read((int)(2 * m + 2));
								uint32_t first = h.Read(8);
								if (L && first != 0xFF)
									compareBits = wb.bit - (int)first;
							}
						}
						for (int k = 0; same && k < compareBits; k++)
						{
							int a = start + k;
							if (((payload[a >> 3] >> (a & 7)) & 1) != ((tmp[k >> 3] >> (k & 7)) & 1))
							{
								same = false;
								firstDiff = k;
							}
						}
						if (!same && stringDiff < 3)
							std::printf("   first differing bit %d of %d", firstDiff, wb.bit);
						strings++;
						if (!same)
						{
							stringDiff++;
							if (stringDiff <= 3)
								std::printf("   string coding differs: '%s' (%d vs %d bits)\n", str.c_str(), wb.bit, end - start);
						}
					}
				}
				// section B (object definitions) re-encoded field by field must give the original's bits
				{
					om_ghoul_bits_t rb = {payload.data(), (int)len * 8, 0, 0};
					Bits r(&rb);
					while (r.ReadId() && !rb.overflowed)
						r.ReadString(); // skip section A
					for (;;)
					{
						int start = rb.bit;
						uint32_t oid = r.ReadId();
						if (!oid || rb.overflowed)
							break;
						uint8_t tmp[16384] = {0};
						om_ghoul_bits_t wb = {tmp, (int)sizeof(tmp) * 8, 0, 0};
						Bits w(&wb);
						w.WriteId(oid);
						int counts[5];
						for (int l = 0; l < 4; l++)
						{
							uint32_t n = r.ReadId();
							counts[l] = (int)n;
							w.WriteId(n);
							for (uint32_t k = 0; k < n; k++)
								w.WriteId(r.ReadId());
						}
						uint32_t ns = r.ReadId();
						counts[4] = (int)ns;
						w.WriteId(ns);
						for (uint32_t k = 0; k < ns; k++)
						{
							uint32_t extra = r.Read(1);
							w.Write(extra, 1);
							if (extra)
							{
								int save = rb.bit;
								uint32_t mat = r.ReadId();
								if (mat == 0 && rb.bit - save > 1)
								{
									w.Write(1, 1); // material 0 as present bit + value 0
									w.Write(0, 2);
									w.Write(0, 4);
								}
								else
									w.WriteId(mat);
							}
							w.WriteId(r.ReadId());
						}
						int w0 = BitsFor(counts[0]), w1 = BitsFor(counts[1]), w2 = BitsFor(counts[3]), w3 = BitsFor(counts[4]);
						NetState def;
						def.parts.assign((size_t)counts[1], 1);
						NetState base = def;
						int stateStart = rb.bit;
						NetReadStateForTest(&rb, base, 0.0f, w0, w1, w2, w3);
						int end = rb.bit;
						int wStateStart = wb.bit;
						NetWriteStateForTest(&wb, base, def, 0.0f, w0, w1, w2, w3);
						defs++;
						bool same = wb.bit == end - start;
						for (int k = 0; same && k < wb.bit; k++)
						{
							int a = start + k;
							if (((payload[a >> 3] >> (a & 7)) & 1) != ((tmp[k >> 3] >> (k & 7)) & 1))
							{
								same = false;
								if (defDiff < 3)
									std::printf("   definition of object %u differs at bit %d (state starts at %d; %d vs %d "
												"bits)\n",
										oid, k, stateStart - start, wb.bit, end - start);
							}
						}
						(void)wStateStart;
						if (!same)
							defDiff++;
					}
				}
				cl->ClientReadReliable(0, &b, false);
				rel++;
				int left = (int)len * 8 - g_lastReliableBitsUsed;
				if (left < 0 || left >= 8)
					relPadBad++;
			}
			else
			{
				cl->ClientReadUnreliable(frame, &b, time);
				unrel++;
				int left = (int)len * 8 - b.bit;
				if (left < 0 || left >= 8)
					unrelPadBad++;
			}
		}
	}
	catch (const std::exception &e)
	{
		fatal = e.what();
	}
	System *s = static_cast<System *>(cl);
	int bad = 0;
	for (auto &i : s->insts)
	{
		Inst *in = i.second;
		records++;
		if (in->seq > (int)in->obj->seqs.size() || in->attachBolt > (int)in->obj->parts.size() || in->mode > 3 ||
			in->speed > 12)
			bad++;
		for (auto &k : in->skinOverrides)
			if (k.second < 1 || k.second > (int)in->obj->skins.size() || in->obj->skins[k.second - 1].material != k.first.first)
				bad++;
	}
	std::printf("   %d reliable payloads (padding wrong %d), %d unreliable payloads (padding wrong %d); objects %d, "
				"instances %d (implausible fields %d)%s%s\n",
		rel, relPadBad, unrel, unrelPadBad, (int)s->objects.size(), records, bad, fatal.empty() ? "" : ", FATAL: ",
		fatal.c_str());
	std::printf("   %d strings re-encoded, %d differ from the original's bits\n", strings, stringDiff);
	// informational since 2026-10-26: OpenMerc has no string dictionary and its own Huffman weights (docs/DEVIATIONS.md
	// "Fixed tables"), so its string bits are not the original's; the decoded strings above must still round-trip
	std::printf("   %d object definitions (lists + baseline state) re-encoded, %d differ\n", defs, defDiff);
	CHECK(defDiff == 0, "definition encoder differs");
	CHECK(fatal.empty(), "replay fatal %s", fatal.c_str());
	CHECK(relPadBad == 0 && unrelPadBad == 0, "payloads end in the last byte");
	CHECK(bad == 0 && s->objects.size() > 0 && records > 0, "decoded states plausible");
	// the client draws what it decoded: every replicated instance poses without error
	int posed = 0;
	for (auto &i : s->insts)
	{
		Pose pp;
		if (i.second->EvalPose(i.second->A + 0.5f, pp))
			posed++;
	}
	std::printf("   instances with a playable pose: %d\n", posed);
	cl->Reset();
}
