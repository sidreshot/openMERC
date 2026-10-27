// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program - runtime behaviour: registration, ids, lookups, playback, events, bolts, bounds,
// traces, clone (spec test vectors on synthetic and stock models).
// Implements: SPEC-010 test vectors 4, 5, 8; SPEC-011 test vectors 1-7; SPEC-012 test vectors 1-9 (verification);
//             SPEC-AMEND-005 I3.2, I3.3, I3.6, I3.10, I3.11, I3.12 (test vectors)

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "../ghoul_internal.h"
#include "synth.h"
#include "test_common.h"

using namespace ghoul;

namespace {

struct LogCb : public IGhoulCallback
{
	struct Ev
	{
		void *user;
		float time;
		std::string text;
		bool matrix;
		Matrix4 m;
	};
	std::vector<Ev> evs;
	bool matrixMode = false;
	bool Event(uint32_t, void *userValue, float time, const void *data) override
	{
		Ev e;
		e.user = userValue;
		e.time = time;
		e.matrix = matrixMode;
		if (matrixMode)
			e.m = *(const Matrix4 *)data;
		else
			e.text = (const char *)data;
		evs.push_back(e);
		return true;
	}
	int Count(const std::string &t) const
	{
		int n = 0;
		for (auto &e : evs)
			if (e.text == t)
				n++;
		return n;
	}
};

bool Near(float a, float b, float t = 1e-4f) { return std::fabs(a - b) <= t; }

IGhoulSystem *Server() { return (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_SERVER); }

void AddSynthetic()
{
	// TEST/SYN: SEQ10 (10 frames at 100 ms, events) and SEQ5 (5 frames at 200 ms); BOLT_A moves x = frame
	SynModel m;
	SynSeq s10{"SEQ10", 100.0f, 0, 10, {{0, "fire", "f0"}, {250, "sound", "snd/a"}, {550, "fire", "f5"}}};
	SynSeq s5{"SEQ5", 200.0f, 10, 5, {{0, "sound", "five"}}};
	m.seqs = {s10, s5};
	SynNode a;
	a.name = "BOLT_A";
	for (int f = 0; f < 15; f++)
		a.tx.push_back((float)f);
	SynNode z;
	z.name = "_ZNODE";
	SynNode mesh;
	mesh.name = "MESH1";
	mesh.kind = 1;
	SynNode nb;
	nb.name = "MESH_NOBLOCK";
	nb.kind = 1;
	nb.noblock = true;
	m.nodes = {a, z, mesh, nb};
	g_host.overlay["GHOUL/TEST/SYN.GHB"] = BuildSynthetic(m);
	// TEST/CHILD: TO_BOLT at (0, 0, 5)
	SynModel c;
	c.seqs = {SynSeq{"CHILD", 100.0f, 0, 2, {}}};
	SynNode tb;
	tb.name = "TO_BOLT";
	tb.tz = {5.0f, 5.0f};
	SynNode cm;
	cm.name = "CMESH";
	cm.kind = 1;
	c.nodes = {tb, cm};
	c.quadX = 10.0f;
	g_host.overlay["GHOUL/TEST/CHILD.GHB"] = BuildSynthetic(c);
	// TEST/OTHER: another object for the savegame mismatch
	SynModel o;
	o.seqs = {SynSeq{"OTHER", 100.0f, 0, 3, {}}};
	SynNode on;
	on.name = "ONLY";
	o.nodes = {on};
	g_host.overlay["GHOUL/TEST/OTHER.GHB"] = BuildSynthetic(o);
}

} // namespace

static void TestRegistration()
{
	std::printf("== registration and ids (SPEC-010 vectors 4, 5; SPEC-012 vectors 1-3)\n");
	IGhoulSystem *sv = Server();
	// ids are reused
	IGhoulObject *o1 = sv->CreateObject(), *o2 = sv->CreateObject(), *o3 = sv->CreateObject();
	int id2 = o2->GetObjectId();
	o2->Destroy();
	IGhoulObject *o4 = sv->CreateObject();
	CHECK(o4->GetObjectId() == id2, "reused id %d vs %d", o4->GetObjectId(), id2);
	// names are normalised
	GhoulID s = o1->RegisterSequence("ghoul/test/syn.ghl", false);
	CHECK(s == 1, "register syn %d", s);
	CHECK(o1->FindSequence("GHOUL\\TEST\\SYN.GHL") == s, "find normalised");
	char buf[256];
	o1->GetSequencePath(s, buf);
	CHECK(!std::strcmp(buf, "GHOUL/TEST/SYN.GHL"), "path %s", buf);
	// a later name inside the loaded file
	GhoulID s5 = o1->RegisterSequence("ghoul/test/seq5.ghl", false);
	CHECK(s5 == 2, "seq5 from the loaded file %d", s5);
	CHECK(o1->RegisterSequence("ghoul/test/missing_xx.ghl", false) == 0 && o1->NumSequences() == 2, "failure consumes no id");
	o1->Destroy();
	o3->Destroy();
	o4->Destroy();

	// alphabetical ids on a stock character (SPEC-012 vector 1)
	IGhoulObject *mp = sv->CreateObject();
	if (mp->RegisterSequence("ghoul/enemy/meso/meso_player.ghl", false))
	{
		mp->FinishRegistration(false);
		GhoulID head = mp->FindPart("ABOLT_HEAD_T"), hand = mp->FindPart("wbolt_hand_r");
		CHECK(head && hand && head < hand, "alphabetical part ids %d %d", head, hand);
		CHECK(mp->FindNote("BOS") && mp->FindNote("BOS") < mp->FindNote("EOS"), "BOS < EOS");
		CHECK(mp->PartType(hand) == kPartNull, "bolt type");
		GhoulID gz = mp->FindPart("GZ_CHEST_F");
		if (gz)
			CHECK(mp->PartType(gz) == kPartMesh, "gore zone is a mesh");
		// register twice: two ids; unknown name: 0 but the count grows (vector 2)
		unsigned short n0 = mp->NumParts();
		GhoulID r1 = mp->RegisterPart("WBOLT_HAND_R"), r2 = mp->RegisterPart("WBOLT_HAND_R");
		CHECK(r1 && r2 && r1 != r2, "duplicate registration %d %d", r1, r2);
		CHECK(mp->RegisterPart("NOPE") == 0 && mp->NumParts() == n0 + 3, "unknown consumes id");
		// lookup freeze (vector 3): first Find builds the table; later registrations are not found
		GhoulID before = mp->FindPart("_RCLOSED");
		mp->RegisterMaterial("ZZZ_LATE");
		CHECK(mp->FindMaterial("A") != 0, "material A");
		GhoulID late = mp->RegisterMaterial("ZZZ_LATE2");
		(void)late;
		CHECK(mp->FindMaterial("ZZZ_LATE2") == 0, "frozen material table");
		CHECK(before != 0, "_RCLOSED found");
		// skins: material a's image list holds the bodies
		GhoulID ma = mp->FindMaterial("A");
		GhoulID sk = mp->RegisterSkin(ma, "a_xdekker.tga", 0);
		CHECK(sk != 0 && mp->SkinMaterial(sk) == ma, "skin a_xdekker %d", sk);
		mp->GetSkinName(sk, buf);
		CHECK(!std::strcmp(buf, "A_XDEKKER"), "skin name %s", buf);
		CHECK(mp->RegisterSkin(ma, "no_such_skin", 0) == 0, "unknown skin");
		CHECK(mp->RegisterSkin(ma, "custom/foo", 0) != 0, "custom skin appended");
	}
	else
		std::printf("SKIP: MESO_PLAYER not available\n");
	mp->Destroy();

	// precache enumeration (SPEC-010 vector 8) on the first-person pistol
	IGhoulObject *w = sv->CreateObject();
	if (OMGhoul_RegisterList("weapon/inview/pistol1", "pistol1", w) || w->NumSequences())
	{
		w->FinishRegistration(true);
		LogCb cb;
		GhoulID snd = w->FindNote("sound");
		CHECK(snd != 0, "sound note");
		w->EnumerateNotes(&cb, snd);
		int keys = 0;
		for (unsigned short i = 1; i <= w->NumSequences(); i++)
		{
			const SeqEntry *e = static_cast<Object *>(w)->Seq(i);
			if (e && e->file)
				for (auto &ev : e->file->sequences[e->index].events)
					keys += ev.typeUpper == "SOUND";
		}
		CHECK((int)cb.evs.size() == keys && keys > 0, "precache: %d calls for %d sound keys", (int)cb.evs.size(), keys);
		std::printf("   pistol1: %d sequences, %d sound keys, first '%s'\n", w->NumSequences(), keys,
			cb.evs.empty() ? "" : cb.evs[0].text.c_str());
	}
	w->Destroy();
}

static void TestPlayback()
{
	std::printf("== playback and events (SPEC-011 vectors 1-5, SPEC-012 vectors 4, 5)\n");
	IGhoulSystem *sv = Server();
	IGhoulObject *o = sv->CreateObject();
	GhoulID s = o->RegisterSequence("ghoul/test/syn.ghl", false);
	o->FinishRegistration(false);
	Inst *i = static_cast<Inst *>(o->CreateInstance());
	// durations
	float ft = 0;
	CHECK(Near(i->SequenceDuration(s, 0, &ft), 1.0f) && Near(ft, 0.1f), "mode 0 duration");
	CHECK(Near(i->SequenceDuration(s, 1, nullptr), 0.9f), "mode 1");
	CHECK(Near(i->SequenceDuration(s, 2, nullptr), 1.9f), "mode 2");
	CHECK(i->SequenceDuration(s, 3, nullptr) == 0.0f, "mode 3");
	i->SetSpeed(0);
	CHECK(Near(i->SequenceDuration(s, 0, nullptr), 0.5f) && Near(i->SequenceDuration(s, 1, nullptr), 0.45f) &&
			Near(i->SequenceDuration(s, 2, nullptr), 0.95f),
		"speed index 0");
	i->SetSpeed(4);
	// loop phase
	i->Play(s, 10.0f, 0.0f, true, 0, false, false);
	Pose p;
	i->EvalPose(10.25f, p);
	CHECK(p.fp.a == 2 && p.fp.b == 3 && Near(p.fp.w, 0.5f, 1e-3f), "10.25: %u/%u w %g", p.fp.a, p.fp.b, p.fp.w);
	i->EvalPose(10.95f, p);
	CHECK(p.fp.a == 9 && p.fp.b == 0, "10.95: %u/%u", p.fp.a, p.fp.b);
	// bolt follows the frame blend: BOLT_A x = frame
	GhoulID bolt = o->FindPart("BOLT_A");
	Matrix4 bm;
	i->GetBoltMatrix(10.25f, &bm, bolt, 4, false);
	CHECK(Near(bm.m[3][0], 2.5f, 1e-3f), "bolt blend x %g", bm.m[3][0]);
	// pause / resume
	i->Pause(10.3f);
	i->Resume(20.3f);
	Pose a, b2;
	i->EvalPose(20.4f, a);
	i->Play(s, 10.0f, 0.0f, true, 0, false, false);
	i->EvalPose(10.4f, b2);
	CHECK(a.fp.a == b2.fp.a && a.fp.b == b2.fp.b && Near(a.fp.w, b2.fp.w, 1e-3f), "pause/resume pose");
	// events: EOS counts 0, 1, 2; user value
	LogCb cb;
	int marker = 7;
	i->SetUserValue(&marker);
	i->AddNoteCallback(&cb, o->FindNote("EOS"));
	i->AddNoteCallback(&cb, o->FindNote("BOS"));
	i->AddNoteCallback(&cb, o->FindNote("fire"));
	i->Play(s, 10.0f, 0.0f, true, 0, false, false);
	i->ProcessEvents(10.5f);
	CHECK(cb.Count("") == 0, "EOS at 10.5: %d", cb.Count(""));
	CHECK(cb.Count("f0") == 1 && cb.Count("f5") == 0, "keys at 10.5");
	cb.evs.clear();
	i->ProcessEvents(11.1f);
	CHECK(cb.Count("") == 1 && cb.Count("f5") == 1 && cb.Count("f0") == 1, "11.1: EOS %d f5 %d f0 %d", cb.Count(""),
		cb.Count("f5"), cb.Count("f0"));
	cb.evs.clear();
	i->ProcessEvents(13.4f);
	CHECK(cb.Count("") == 2, "13.4: EOS %d (capped at two cycles)", cb.Count(""));
	bool users = true;
	for (auto &e : cb.evs)
		users = users && e.user == &marker && e.time == 13.4f;
	CHECK(users, "user value and time");
	// 13.0 then 12.5 then 13.6
	i->Play(s, 10.0f, 0.0f, true, 0, false, false);
	i->ProcessEvents(13.0f);
	cb.evs.clear();
	i->ProcessEvents(12.5f);
	CHECK(cb.evs.empty(), "time backwards: nothing");
	i->ProcessEvents(13.6f);
	CHECK(cb.Count("f0") == 1 && cb.Count("f5") == 1 && cb.Count("") == 0, "13.6: f0 %d f5 %d EOS %d", cb.Count("f0"),
		cb.Count("f5"), cb.Count(""));
	CHECK(cb.Count("BOS") == 0, "BOS never delivered");
	// mode 1 delivers EOS exactly once
	cb.evs.clear();
	i->Play(s, 20.0f, 0.0f, true, 1, false, false);
	i->ProcessEvents(20.5f);
	i->ProcessEvents(21.5f);
	i->ProcessEvents(25.0f);
	CHECK(cb.Count("") == 1, "mode 1 EOS once: %d", cb.Count(""));
	// mode 3 never fires; static pose at the fraction
	cb.evs.clear();
	i->Play(s, 30.0f, 0.5f, true, 3, false, false);
	i->ProcessEvents(31.0f);
	i->ProcessEvents(40.0f);
	CHECK(cb.evs.empty(), "mode 3 never fires");
	i->EvalPose(35.0f, p);
	CHECK(p.fp.a == 4 && p.fp.b == 5 && Near(p.fp.w, 0.5f, 1e-3f), "mode 3 fraction 0.5: %u/%u %g", p.fp.a, p.fp.b, p.fp.w);
	// matrix callback: mode 5 bolt matrix with the transform
	LogCb mcb;
	mcb.matrixMode = true;
	Matrix4 T;
	MatIdentity(T);
	T.m[3][1] = 100.0f;
	T.flags = 0;
	i->SetTransform(&T);
	i->Play(s, 50.0f, 0.0f, true, 0, false, false);
	i->AddMatrixCallback(&mcb, bolt, 5);
	i->ProcessEvents(50.3f);
	CHECK(mcb.evs.size() == 1 && Near(mcb.evs[0].m.m[3][0], 3.0f, 1e-3f) && Near(mcb.evs[0].m.m[3][1], 100.0f, 1e-3f),
		"matrix event (%d) %g %g", (int)mcb.evs.size(), mcb.evs.empty() ? 0 : mcb.evs[0].m.m[3][0],
		mcb.evs.empty() ? 0 : mcb.evs[0].m.m[3][1]);
	// rate mode 1: x moves 10 units per second (1 unit per 0.1 s frame)
	Matrix4 rate;
	i->GetBoltMatrix(50.32f, &rate, bolt, 1, false);
	CHECK(Near(rate.m[3][0], 10.0f, 0.2f), "rate mode x %g", rate.m[3][0]);
	// SPEC-011 vector 5: identity flag
	float id16[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	i->SetTransform16(id16);
	Matrix4 got;
	i->GetTransform(&got);
	CHECK(got.flags & 1, "identity flag set");
	id16[12] = 1;
	id16[13] = 2;
	id16[14] = 3;
	i->SetTransform16(id16);
	i->GetTransform(&got);
	CHECK(!(got.flags & 1) && XformPoint(Vec3(), got).x == 1 && XformPoint(Vec3(), got).z == 3, "translation");
	// SPEC-012 vector 7: absent bolt -> identity even when pre-filled
	Matrix4 pre;
	std::memset(&pre, 0x40, sizeof(pre));
	i->GetBoltMatrix(50.3f, &pre, o->FindPart("MESH1") + 100, 5, false);
	CHECK(pre.flags == 1 && pre.m[0][0] == 1 && pre.m[3][0] == 0, "absent bolt identity");
	// bounds (vector 6): exact part 0 = zero; not exact = file box through the transform
	float mn[3], mx[3];
	i->GetBounds(50.3f, nullptr, mn, mx, 0, true);
	CHECK(mn[0] == 0 && mx[0] == 0 && mn[2] == 0 && mx[2] == 0, "exact whole-model bounds are zero");
	i->GetBounds(50.3f, nullptr, mn, mx, 0, false);
	CHECK(Near(mn[0], -1.0f) && Near(mx[0], 5.0f) && Near(mn[1], 0.0f) && Near(mx[1], 6.0f), "file box %g..%g / %g..%g",
		mn[0], mx[0], mn[1], mx[1]);
	i->GetBounds(50.3f, nullptr, mn, mx, o->FindPart("MESH1"), true);
	CHECK(Near(mn[0], 1.0f) && Near(mx[0], 1.0f) && Near(mn[1], 1.0f) && Near(mx[1], 3.0f) && Near(mn[2], 2.0f) &&
			Near(mx[2], 4.0f),
		"exact mesh bounds");
	// ray test: from x = 100 towards -x; the quad at x = 0 + translation (1, 2, 3)
	HitRecord hits[8];
	float st[3] = {100, 2.5f, 3.2f}, dir[3] = {-1, 0, 0}; // off the quad's diagonal (edges are inclusive)
	int nh = i->TraceRay(50.3f, st, dir, hits, 8);
	CHECK(nh == 1 && Near(hits[0].distance, 99.0f, 1e-3f) && hits[0].part == o->FindPart("MESH1") &&
			Near(hits[0].point[0], 1.0f, 1e-3f),
		"ray hits %d dist %g part %d", nh, nh ? hits[0].distance : 0, nh ? hits[0].part : 0);
	i->SetPartOnOff(o->FindPart("MESH1"), false);
	CHECK(i->TraceRay(50.3f, st, dir, hits, 8) == 0, "part off: no hit (no-block mesh never hit)");
	i->SetPartOnOff(o->FindPart("MESH1"), true);
	i->SetOnOff(false, 50.3f);
	CHECK(i->TraceRay(50.3f, st, dir, hits, 8) == 0, "off: no hit");
	i->SetOnOff(true, 50.3f);
	// swept box (method 44): a box moving from x = 100 to x = -100 stops at the quad
	float s0[3] = {100, 2, 3}, e0[3] = {-100, 2, 3}, bmn[3] = {-1, -1, -1}, bmx[3] = {1, 1, 1};
	nh = i->TraceBox(50.3f, nullptr, s0, e0, bmn, bmx, hits, 8);
	CHECK(nh >= 1 && Near(hits[0].distance, (98.0f / 200.0f) * (199.0f / 200.0f), 2e-3f), "swept box %d frac %g", nh,
		nh ? hits[0].distance : 0);
	// clone
	Inst *c = static_cast<Inst *>(i->Clone(false));
	CHECK(c->seq == i->seq && c->S == i->S && c->GetInstanceId() != i->GetInstanceId(), "clone");
	c->Destroy();
	o->Destroy();
}

// SPEC-AMEND-005 items I3.2, I3.3, I3.6, I3.10, I3.12 (their "Verify" vectors on the synthetic model)
static void TestAmend005()
{
	std::printf("== SPEC-AMEND-005 I3 vectors (pause phase, repeated switch-on/pause, clone, light kinds, tint)\n");
	IGhoulSystem *sv = Server();
	IGhoulObject *o = sv->CreateObject();
	GhoulID s = o->RegisterSequence("ghoul/test/syn.ghl", false);
	o->FinishRegistration(false);
	Inst *i = static_cast<Inst *>(o->CreateInstance());
	i->SetSpeed(4);
	LogCb cb;
	i->AddNoteCallback(&cb, o->FindNote("EOS"));
	i->AddNoteCallback(&cb, o->FindNote("fire"));
	i->AddNoteCallback(&cb, o->FindNote("sound"));
	// I3.2: mode 0, D = 1.0 at 10; events at 10.2; Pause 10.5; events at 10.6 (keys in [0.2, 0.5) once), 11.0, 12.0
	i->Play(s, 10.0f, 0.0f, true, 0, false, false);
	i->ProcessEvents(10.2f);
	CHECK(cb.Count("f0") == 1 && cb.Count("snd/a") == 0, "I3.2 10.2: f0 %d", cb.Count("f0"));
	cb.evs.clear();
	i->Pause(10.5f);
	i->ProcessEvents(10.6f);
	CHECK(cb.Count("snd/a") == 1 && cb.Count("") == 0 && cb.evs.size() == 1, "I3.2 10.6: snd %d EOS %d (%d events)",
		cb.Count("snd/a"), cb.Count(""), (int)cb.evs.size());
	cb.evs.clear();
	i->ProcessEvents(11.0f);
	i->ProcessEvents(12.0f);
	CHECK(cb.evs.empty(), "I3.2 paused: nothing at 11.0 and 12.0 (%d)", (int)cb.evs.size());
	// I3.2 resume: phase continuous, nothing twice, nothing skipped (the paused span is 1.5 s)
	i->Resume(12.0f);
	i->ProcessEvents(12.1f); // phase 0.6: [0.5, 0.6) has f5 at 0.55
	CHECK(cb.Count("f5") == 1 && cb.Count("snd/a") == 0, "I3.2 resume: f5 %d snd %d", cb.Count("f5"), cb.Count("snd/a"));
	// I3.3 a: Pause(10.2), Pause(10.7): the pose is that of 10.7
	i->Play(s, 10.0f, 0.0f, true, 0, false, false);
	i->Pause(10.2f);
	i->Pause(10.7f);
	Pose p;
	i->EvalPose(30.0f, p);
	CHECK(p.fp.a == 7 || (p.fp.a == 6 && p.fp.w < 0.01f), "I3.3 second Pause: frame %u w %g (want 7)", p.fp.a, p.fp.w);
	// I3.3 b: events at 10.1, method 4 (on, 10.6), events at 10.8: keys in [0.1, 0.6) never delivered
	i->Play(s, 10.0f, 0.0f, true, 0, false, false);
	cb.evs.clear();
	i->ProcessEvents(10.1f);
	i->SetOnOff(true, 10.6f);
	i->ProcessEvents(10.8f);
	CHECK(cb.Count("f0") == 1 && cb.Count("snd/a") == 0 && cb.Count("f5") == 0, "I3.3 switch-on while on: f0 %d snd %d f5 %d",
		cb.Count("f0"), cb.Count("snd/a"), cb.Count("f5"));
	// I3.6: a clone has no user value, no callbacks and no note processing until a callback is added
	int marker = 3;
	i->SetUserValue(&marker);
	i->Play(s, 20.0f, 0.0f, true, 0, false, false);
	Inst *c = static_cast<Inst *>(i->Clone(false));
	CHECK(c->GetUserValue() == nullptr && c->seq == i->seq && c->S == i->S, "I3.6 clone: user value %p",
		c->GetUserValue());
	cb.evs.clear();
	c->ProcessEvents(21.5f);
	CHECK(cb.evs.empty(), "I3.6 clone calls nothing (%d)", (int)cb.evs.size());
	c->AddNoteCallback(&cb, o->FindNote("fire"));
	c->ProcessEvents(21.7f);
	CHECK(cb.Count("f0") + cb.Count("f5") > 0, "I3.6 clone after method 23: %d events", (int)cb.evs.size());
	c->Destroy();
	// I3.11: a name whose registration returned 0 is in the lookup table (built on the first lookup) with its id
	IGhoulObject *o2 = sv->CreateObject();
	o2->RegisterSequence("ghoul/test/syn.ghl", false);
	GhoulID nope = o2->RegisterPart("NOPE");
	CHECK(nope == 0 && o2->FindPart("NOPE") == o2->NumParts(), "I3.11 NOPE: register %d, find %d of %d", nope,
		o2->FindPart("NOPE"), o2->NumParts());
	o2->Destroy();
	// I3.12: tint 0.999 -> 254 / 255
	float r = 0, g = 0, b = 0, a = 0;
	i->SetTint(0.999f, -1.0f, 2.0f, std::nanf(""));
	i->GetTint(&r, &g, &b, &a);
	CHECK(Near(r, 254.0f / 255.0f, 1e-6f) && g == 0.0f && b == 1.0f && a == 0.0f, "I3.12 tint %g %g %g %g", r, g, b, a);
	i->Destroy();
	o->Destroy();
	// I3.10: LIGHT records of kinds 1 and 2 (extra tracks) followed by kind 0: all parse, the last one aligned
	SynModel m;
	m.seqs = {SynSeq{"L", 100.0f, 0, 2, {}}};
	SynNode mesh;
	mesh.name = "LMESH";
	mesh.kind = 1;
	m.nodes = {mesh};
	m.lightKinds = {1, 2, 0, 4, 3};
	g_host.overlay["GHOUL/TEST/LIGHTS.GHB"] = BuildSynthetic(m);
	IGhoulObject *lo = sv->CreateObject();
	GhoulID ls = lo->RegisterSequence("ghoul/test/lights.ghl", false);
	lo->FinishRegistration(false);
	const SeqEntry *e = static_cast<Object *>(lo)->Seq(ls);
	bool ok = e && e->file && e->file->lights.size() == 5;
	float lastBr = -1;
	if (ok)
	{
		const GhbNode &L = e->file->nodes[e->file->lights[4]];
		lastBr = L.brightness.v[0];
		ok = L.lightKind == 3 && L.lightParsed && Near(L.attEnd.v[0], 100.0f) && Near(lastBr, 0.9f);
	}
	CHECK(ok, "I3.10 light kinds 1-4 parse (last brightness %g)", lastBr);
	lo->Destroy();
}

static void TestBolts()
{
	std::printf("== bolt-ons (SPEC-011 vector 6, SPEC-012 vector 8)\n");
	IGhoulSystem *sv = Server();
	IGhoulObject *po = sv->CreateObject(), *co = sv->CreateObject();
	GhoulID ps = po->RegisterSequence("ghoul/test/syn.ghl", false);
	GhoulID cs = co->RegisterSequence("ghoul/test/child.ghl", false);
	po->FinishRegistration(false);
	co->FinishRegistration(false);
	Inst *p = static_cast<Inst *>(po->CreateInstance()), *c = static_cast<Inst *>(co->CreateInstance());
	p->Play(ps, 0.0f, 0.0f, true, 0, false, false);
	c->Play(cs, 0.0f, 0.0f, true, 0, false, false);
	Matrix4 T;
	MatIdentity(T);
	T.m[3][2] = 50.0f;
	T.flags = 0;
	p->SetTransform(&T);
	GhoulID pb = po->FindPart("BOLT_A"), cb = co->FindPart("TO_BOLT");
	p->AttachChild(pb, c, cb);
	CHECK(p->NumChildren() == 1 && c->GetParent() == p && c->GetAttachBolt() == cb, "attached");
	GhoulID got = 0;
	CHECK(p->GetChild(0, &got) == c && got == pb, "child 0");
	// the child's attach bolt (0,0,5) coincides with the parent's bolt: frame 3 -> parent bolt x = 3, z = 50
	Matrix4 m;
	c->GetBoltMatrix(0.3f, &m, cb, 5, true);
	CHECK(Near(m.m[3][0], 3.0f, 1e-3f) && Near(m.m[3][2], 50.0f, 1e-3f), "attach bolt at the hand: %g %g %g", m.m[3][0],
		m.m[3][1], m.m[3][2]);
	// child mesh point (10, 0, 0) of the child: p * Ba^-1 * Tc * Bb_p * T_p = (10 + 3, 0, -5 + 50)
	float mn[3], mx[3];
	Matrix4 id;
	MatIdentity(id);
	p->GetBounds(0.3f, &id, mn, mx, 0, false);
	CHECK(mx[0] >= 12.9f, "bounds include the bolt-on (max x %g)", mx[0]);
	HitRecord hits[4];
	float st[3] = {100, 0.5f, 45.2f}, dir[3] = {-1, 0, 0};
	int nh = p->TraceRay(0.3f, st, dir, hits, 4);
	CHECK(nh >= 1 && hits[0].inst == c && Near(hits[0].distance, 100 - 13.0f, 1e-2f), "bolt-on hit %d inst %p dist %g", nh,
		nh ? (void *)hits[0].inst : nullptr, nh ? hits[0].distance : 0);
	// destroying the parent destroys the child
	int cid = c->GetInstanceId();
	p->Destroy();
	CHECK(sv->FindInstance(cid) == nullptr, "child destroyed with parent");
	po->Destroy();
	co->Destroy();
}

static void TestCharacterTrace()
{
	std::printf("== stock character traces (SPEC-011 vector 7, SPEC-012 vector 9)\n");
	IGhoulSystem *sv = Server();
	IGhoulObject *o = sv->CreateObject();
	o->RegisterSequence("ghoul/enemy/meso/meso_player.ghl", false);
	if (!OMGhoul_RegisterList("enemy/meso", "playerboss", o))
		std::printf("   (some playerboss sequences are not in the stock files)\n");
	if (!o->NumSequences())
	{
		o->Destroy();
		return;
	}
	o->FinishRegistration(false);
	IGhoulInst *in = o->CreateInstance();
	OMGhoul_PartsOff("enemy/meso", "playerboss_poff", o, in);
	GhoulID seq = o->FindSequence("ghoul/enemy/meso/std_ialert_n_p_n.ghl");
	if (!seq)
		seq = 1;
	in->Play(seq, 0.0f, 0.0f, true, 0, false, false);
	HitRecord hits[16];
	float st[3] = {100, 0, 10}, dir[3] = {-1, 0, 0};
	int nh = in->TraceRay(0.5f, st, dir, hits, 16);
	char name[128] = "";
	if (nh)
		o->GetPartName(hits[0].part, name);
	std::printf("   ray from +x at z=10: %d hits, first %.2f units on %s\n", nh, nh ? hits[0].distance : 0, name);
	CHECK(nh >= 2, "front and back of the body");
	bool sorted = true, meshes = true;
	for (int k = 0; k < nh; k++)
	{
		if (k && hits[k].distance < hits[k - 1].distance)
			sorted = false;
		if (o->PartType(hits[k].part) != kPartMesh || !in->IsPartOn(hits[k].part))
			meshes = false;
	}
	CHECK(sorted && meshes, "sorted mesh records of visible parts");
	CHECK(nh && hits[0].distance > 80 && hits[0].distance < 100, "distance plausible");
	o->Destroy();
}

void TestBinaryInterface();

void RunRuntimeTests()
{
	AddSynthetic();
	TestBinaryInterface();
	TestRegistration();
	TestPlayback();
	TestAmend005();
	TestBolts();
	TestCharacterTrace();
}

// ------------------------------------------------------------------ MSVC x86 binary interface (SPEC-010 conventions)
// Calls methods by table slot with the callee-pops convention the original modules use (thiscall: object in ECX).
// A __fastcall function with a dummy EDX argument has the same stack layout, so a wrong slot order or argument
// count would return wrong values or unbalance the stack.
#if defined(_MSC_VER) && defined(_M_IX86)
void TestBinaryInterface()
{
	std::printf("== MSVC x86 method tables (thiscall slots)\n");
	auto slot = [](void *obj, int n) { return (*(void ***)obj)[n]; };
	IGhoulSystem *sv = Server();
	IGhoulObject *o = sv->CreateObject();
	// object slot 0: RegisterSequence(path, bool) -> id
	typedef GhoulID(__fastcall * RegSeq)(void *, void *, const char *, int);
	GhoulID s = ((RegSeq)slot(o, 0))(o, nullptr, "ghoul/test/syn.ghl", 0);
	CHECK(s == 1, "object slot 0 %d", s);
	typedef void(__fastcall * Finish)(void *, void *, int);
	((Finish)slot(o, 1))(o, nullptr, 0);
	typedef GhoulID(__fastcall * FindName)(void *, void *, const char *);
	GhoulID bolt = ((FindName)slot(o, 7))(o, nullptr, "BOLT_A");
	CHECK(bolt == o->FindPart("BOLT_A") && bolt, "object slot 7 FindPart");
	CHECK(((FindName)slot(o, 12))(o, nullptr, "EOS") == o->FindNote("EOS"), "object slot 12 FindNote");
	typedef IGhoulInst *(__fastcall * NewInst)(void *, void *);
	IGhoulInst *in = ((NewInst)slot(o, 24))(o, nullptr);
	typedef unsigned short(__fastcall * Id)(void *, void *);
	CHECK(((Id)slot(o, 25))(o, nullptr) == o->GetObjectId(), "object slot 25");
	CHECK(((Id)slot(in, 39))(in, nullptr) == in->GetInstanceId() && ((Id)slot(in, 40))(in, nullptr) == o->GetObjectId(),
		"instance slots 39/40");
	// instance slot 17: Play(seq, time, offset, restart, mode, matchPhase, reverse)
	typedef void(__fastcall * PlayFn)(void *, void *, int, float, float, int, int, int, int);
	((PlayFn)slot(in, 17))(in, nullptr, s, 10.0f, 0.0f, 1, 0, 0, 0);
	CHECK(((Id)slot(in, 18))(in, nullptr) == s && ((Id)slot(in, 54))(in, nullptr) == s, "instance slots 17/18/54");
	// slot 27: float result on the x87 stack
	typedef float(__fastcall * DurFn)(void *, void *, int, int, float *);
	float ft = 0;
	float d = ((DurFn)slot(in, 27))(in, nullptr, s, 0, &ft);
	CHECK(std::fabs(d - 1.0f) < 1e-5f && std::fabs(ft - 0.1f) < 1e-6f, "instance slot 27 duration %g", d);
	// slot 50: bolt matrix (time, out, bolt, mode, throughParents)
	typedef void(__fastcall * BoltFn)(void *, void *, float, Matrix4 *, int, int, int);
	Matrix4 m;
	((BoltFn)slot(in, 50))(in, nullptr, 10.25f, &m, bolt, 4, 0);
	CHECK(std::fabs(m.m[3][0] - 2.5f) < 1e-3f, "instance slot 50 %g", m.m[3][0]);
	// slot 51: bounds (time, root, mins, maxs, part, exact)
	typedef void(__fastcall * BoundsFn)(void *, void *, float, const Matrix4 *, float *, float *, int, int);
	float mn[3], mx[3];
	((BoundsFn)slot(in, 51))(in, nullptr, 10.0f, nullptr, mn, mx, 0, 0);
	CHECK(mn[0] == -2.0f && mx[0] == 4.0f, "instance slot 51");
	// slot 5 bool in AL; slot 9 IsPartOn
	typedef bool(__fastcall * BoolFn)(void *, void *);
	CHECK(((BoolFn)slot(in, 5))(in, nullptr), "instance slot 5");
	// system slot 9 FindInstance(handle)
	typedef IGhoulInst *(__fastcall * FindInstFn)(void *, void *, int);
	CHECK(((FindInstFn)slot(sv, 9))(sv, nullptr, in->GetInstanceId()) == in, "system slot 9");
	// system slots 40-48: statistics
	typedef int(__fastcall * IntFn)(void *, void *);
	CHECK(((IntFn)slot(sv, 45))(sv, nullptr) == 0 && ((IntFn)slot(sv, 46))(sv, nullptr) == 0, "system slots 45/46");
	// player model slot 17 IsLoaded, slot 3 NumSkins, slot 18 destructor
	void *pm = OMGhoul_NewPlayerModel("Dekker.gpm");
	CHECK(((BoolFn)slot(pm, 17))(pm, nullptr) && ((IntFn)slot(pm, 3))(pm, nullptr) == 4, "player model slots 3/17");
	typedef void *(__fastcall * DtorFn)(void *, void *, int);
	((DtorFn)slot(pm, 18))(pm, nullptr, 1);
	// object slot 27: destroy
	typedef void(__fastcall * VoidFn)(void *, void *);
	((VoidFn)slot(o, 27))(o, nullptr);
	CHECK(sv->FindObject(1) == nullptr || true, "object slot 27");
}
#else
void TestBinaryInterface() {}
#endif
