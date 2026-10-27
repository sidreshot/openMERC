// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - model instances: the 55-method interface, playback control, parts, skins,
// tint, callbacks, bolt-on hierarchy, clone, bounds and ray / swept-box tests.
// Implements: SPEC-011 (instance interface, lifetime, playback, bolt-ons, traces, bounds),
//             SPEC-012 sections 3 (Play/Pause/Resume/switch-on side effects), 6 (bolt-ons), 7 (hit testing and
//             bounds: R composition, box pre-tests, two-sided triangle test, sorted records, file/mesh boxes);
//             SPEC-AMEND-005 I3.3 (method 4 and Pause repeated), I3.2 (Resume cursor), I3.6 (what Clone copies)

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ghoul_internal.h"

namespace ghoul {

void RenderCache::Reset()
{
	valid = false;
	lastDraw = -1e30f;
	smoothInit = false;
	lightColor = lightDir = probeS = probeV = Vec3();
	shadowDir = Vec3(0, 0, 1);
	lights.clear();
	for (auto &i : lastImage)
		i = -1;
}

Inst::Inst(System *s, Object *o, int iid) : sys(s), obj(o), id(iid)
{
	MatIdentity(xf);
	MatIdentity(inv);
	partFlags.assign(o->parts.size(), 1);
	hasModelLightsFlag = o->hasLights;
}

Inst::~Inst() {}

// ------------------------------------------------------------------ transform (methods 0-3)

void Inst::SetTransform(const Matrix4 *m)
{
	if (!m)
		return;
	xf = *m;
	UpdateTransformDerived();
}

void Inst::SetTransform16(const float *m16)
{
	if (!m16)
		return;
	std::memcpy(xf.m, m16, sizeof(xf.m));
	MatUpdateFlags(xf);
	UpdateTransformDerived();
}

void Inst::GetTransform(Matrix4 *out)
{
	if (out)
		*out = xf;
}

void Inst::GetTransform16(float *out16)
{
	if (out16)
		std::memcpy(out16, xf.m, sizeof(xf.m));
}

// ------------------------------------------------------------------ on/off, speed, parts (4-10)

void Inst::SetOnOff(bool v, float time)
{
	on = v;
	// on != 0 restarts event processing from 'time' for it and all its bolt-ons (A := time) on EVERY call, also when
	// it is already on: keys between the old cursor and 'time' are skipped, a paused pose moves to 'time', a mode-3
	// fraction is replaced (SPEC-AMEND-005 I3.3); on = 0 only clears the flag
	if (v)
		RestartEvents(time, false, false, true);
}

bool Inst::IsOn() { return on; }

void Inst::SetSpeed(int speedIndex) { speed = speedIndex; } // a running playback keeps its duration
int Inst::GetSpeed() { return speed; }

void Inst::SetPartOnOff(GhoulID part, bool shown)
{
	if (part == 0)
		return; // part 0 is invalid (the original does not check)
	SetPartFlagsSize();
	if (part > partFlags.size())
		partFlags.resize(part, 1);
	partFlags[part - 1] = shown ? 1 : 0;
}

bool Inst::IsPartOn(GhoulID part) { return PartShown(part); }

void Inst::SetAllPartsOnOff(bool shown)
{
	SetPartFlagsSize();
	for (size_t i = 0; i < obj->parts.size() && i < partFlags.size(); i++)
		if (obj->parts[i].type == kPartMesh)
			partFlags[i] = shown ? 1 : 0;
}

// ------------------------------------------------------------------ skins and tint (11-16)

void Inst::SetSkin(GhoulID material, GhoulID skin, GhoulID part)
{
	skinOverrides[std::make_pair((int)material, (int)part)] = skin;
}

GhoulID Inst::GetSkin(GhoulID material, GhoulID part, int)
{
	auto it = skinOverrides.find(std::make_pair((int)material, (int)part));
	return it == skinOverrides.end() ? 0 : (GhoulID)it->second;
}

void Inst::RemoveSkin(GhoulID material, GhoulID part, int)
{
	skinOverrides.erase(std::make_pair((int)material, (int)part));
}

static uint8_t TintByte(float v)
{
	if (!(v > 0.0f))
		v = 0.0f;
	if (v > 1.0f)
		v = 1.0f;
	return (uint8_t)(v * 255.0f);
}

void Inst::SetTint(float r, float g, float b, float a)
{
	tint[0] = TintByte(r);
	tint[1] = TintByte(g);
	tint[2] = TintByte(b);
	tint[3] = TintByte(a);
}

void Inst::GetTint(float *r, float *g, float *b, float *a)
{
	if (r)
		*r = tint[0] / 255.0f;
	if (g)
		*g = tint[1] / 255.0f;
	if (b)
		*b = tint[2] / 255.0f;
	if (a)
		*a = tint[3] / 255.0f;
}

void Inst::SetTintAll(float r, float g, float b, float a)
{
	SetTint(r, g, b, a);
	for (auto &c : children)
		c.inst->SetTint(r, g, b, a); // direct bolt-ons only
}

// ------------------------------------------------------------------ playback (17-20, 27)

static float Clamp01Phase(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

void Inst::Play(GhoulID s, float t, float offset, bool restart, int m, bool matchPhase, bool rev)
{
	if (s == 0 || s > obj->seqs.size())
		return;
	if (s == seq && m == mode && rev == reverse && !restart)
		return; // keeps its phase
	// old raw cycle phase at t (for matchPhase)
	bool match = matchPhase && seq && (s != seq || rev != reverse);
	float phase = 0.0f;
	if (match)
	{
		float te = paused ? A : t;
		if (D > 0.0f)
		{
			float d = (te - S) / D;
			phase = mode == 1 ? Clamp01Phase(d) : d - std::floor(d);
		}
		else
			phase = A;
		if (rev != reverse)
			phase = 1.0f - phase;
	}
	seq = s;
	mode = m;
	reverse = rev;
	paused = false;
	RecomputeTiming();
	if (mode == 3)
	{
		// static pose at fraction offset / (mode-0 duration), wrapped for negative values, capped at 1
		float d0 = (float)N * F;
		float fr = d0 > 0.0f ? offset / d0 : 0.0f;
		if (fr < 0.0f)
			fr -= std::floor(fr);
		if (fr > 1.0f)
			fr = 1.0f;
		S = t;
		A = fr;
	}
	else
	{
		float elapsed = offset + (match ? phase * D : 0.0f);
		S = t - elapsed;
		A = t;
	}
	cursorTime = t;
	cursorPhase = RawPhase(t);
	rc.valid = false;
}

GhoulID Inst::GetPlayingSequence() { return (GhoulID)seq; }
GhoulID Inst::GetPlayingSequence2() { return (GhoulID)seq; }

void Inst::Pause(float time)
{
	// a second Pause moves the frozen point to 'time' (pose from the unchanged S, as if it had kept playing); a later
	// Resume counts the paused span from the last Pause (SPEC-AMEND-005 I3.3). The event cursor is left alone.
	paused = true;
	A = time;
	pauseEnd = 1e30f;
}

void Inst::Resume(float time)
{
	if (!paused)
		return;
	S += time - A;
	A = time;
	paused = false;
	// the phase stays continuous; nothing is delivered twice or skipped (SPEC-AMEND-005 I3.2)
	cursorTime = time;
	cursorPhase = RawPhase(time);
}

float Inst::SequenceDuration(GhoulID s, int m, float *frameTime)
{
	const SeqEntry *e = obj->Seq(s);
	if (!e || !e->file || e->index < 0)
	{
		if (frameTime)
			*frameTime = 0.0f;
		return 0.0f;
	}
	const GhbSequence &q = e->file->sequences[e->index];
	float f = q.frameMs * 0.001f * SpeedMultiplier(speed);
	int n = (int)q.count;
	float d;
	switch (m)
	{
	case 0:
		d = n * f;
		break;
	case 1:
		d = (n - 1) * f;
		break;
	case 2:
		d = (2 * n - 1) * f;
		break;
	default:
		d = 0.0f;
		break;
	}
	if (frameTime)
		*frameTime = f;
	N = n; // original quirk: the queried frame count replaces the current one until the next Play
	return d < 0.0f ? 0.0f : d;
}

// ------------------------------------------------------------------ user value and callbacks (21-26)

void Inst::SetUserValue(void *value) { user = value; }
void *Inst::GetUserValue() { return user; }

void Inst::AddNoteCallback(IGhoulCallback *cb, GhoulID note)
{
	noteCbs.push_back(NoteCb{cb, (int)note});
	noteOn = true;
}

void Inst::RemoveNoteCallback(IGhoulCallback *cb, GhoulID note)
{
	noteCbs.erase(std::remove_if(noteCbs.begin(), noteCbs.end(),
					  [&](const NoteCb &n) { return n.cb == cb && n.note == (int)note; }),
		noteCbs.end());
}

void Inst::AddMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int m)
{
	matrixCbs.push_back(MatrixCb{cb, (int)bolt, m});
	noteOn = matrixOn = true;
}

void Inst::RemoveMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int m)
{
	matrixCbs.erase(std::remove_if(matrixCbs.begin(), matrixCbs.end(),
						[&](const MatrixCb &x) { return x.cb == cb && x.bolt == (int)bolt && x.mode == m; }),
		matrixCbs.end());
}

void Inst::StopEvents() { noteOn = matrixOn = false; }
void Inst::StopMatrixEvents() { matrixOn = false; }
void Inst::StartNoteEvents(float time) { RestartEvents(time, true, false, true); }
void Inst::StartEvents(float time) { RestartEvents(time, true, true, true); }

// ------------------------------------------------------------------ hierarchy (28-36)

void Inst::DetachFromParent()
{
	if (!parent)
		return;
	auto &v = parent->children;
	v.erase(std::remove_if(v.begin(), v.end(), [this](const Child &c) { return c.inst == this; }), v.end());
	parent = nullptr;
}

void Inst::AttachChild(GhoulID myBolt, IGhoulInst *child, GhoulID childBolt)
{
	Inst *c = static_cast<Inst *>(child);
	if (!c || c == this)
		return;
	// The original does not check for an existing parent; the child is detached from it here so that no
	// dangling child entry can remain (memory-safety deviation).
	c->DetachFromParent();
	c->parent = this;
	c->attachBolt = childBolt;
	children.push_back(Child{c, (int)myBolt});
}

void Inst::SetAttachBolt(GhoulID bolt) { attachBolt = bolt; }
GhoulID Inst::GetAttachBolt() { return (GhoulID)attachBolt; }

void Inst::DetachChild(IGhoulInst *child)
{
	Inst *c = static_cast<Inst *>(child);
	if (c && c->parent == this)
		c->DetachFromParent();
}

int Inst::NumChildren() { return (int)children.size(); }

IGhoulInst *Inst::GetChild(int index, GhoulID *myBolt)
{
	if (index < 0 || index >= (int)children.size())
		return nullptr;
	if (myBolt)
		*myBolt = (GhoulID)children[index].bolt;
	return children[index].inst;
}

IGhoulObject *Inst::GetGhoulObject() { return obj; }
IGhoulInst *Inst::GetParent() { return parent; }

IGhoulInst *Inst::Clone(bool withChildren)
{
	Inst *c = sys->NewInst(obj, 0);
	c->xf = xf;
	c->inv = inv;
	c->detSign = detSign;
	c->scale = scale;
	c->on = on;
	c->speed = speed;
	c->partFlags = partFlags;
	c->skinOverrides = skinOverrides;
	std::memcpy(c->tint, tint, 4);
	c->seq = seq;
	c->mode = mode;
	c->S = S;
	c->A = A;
	c->pauseEnd = pauseEnd;
	c->paused = paused;
	c->reverse = reverse;
	c->D = D;
	c->F = F;
	c->N = N;
	// SPEC-AMEND-005 I3.6: only the instance state (SPEC-013 section 3 fields) plus D, F, N are copied; user value,
	// callbacks, note/matrix processing, event cursor and caches are those of a new instance
	c->attachBolt = attachBolt;
	if (withChildren)
		for (auto &ch : children)
		{
			Inst *cc = static_cast<Inst *>(ch.inst->Clone(true));
			c->AttachChild((GhoulID)ch.bolt, cc, (GhoulID)ch.inst->attachBolt);
		}
	return c;
}

GhoulID Inst::GetInstanceId() { return (GhoulID)id; }
GhoulID Inst::GetObjectId() { return (GhoulID)obj->id; }
void Inst::Reserved42(int, int, int) {}

GhoulID Inst::NetOffer(float priority) { return sys->NetOfferInst(this, priority); }

void Inst::Destroy()
{
	// bolt-ons first (recursively), then detach from the parent, free the id
	while (!children.empty())
		children.back().inst->Destroy();
	DetachFromParent();
	obj->insts.erase(id);
	sys->InstDestroyed(this);
	delete this;
}

// ------------------------------------------------------------------ bounds (method 51)

namespace {
typedef Box3 Box;

void CornerPosition(const GhbFile &f, int corner, const std::vector<float> &apos, Vec3 &out)
{
	int p = f.geo.corners[corner * 3];
	if (p < 0)
		out = Vec3(&f.geo.pos[(size_t)~p * 3]);
	else
		out = Vec3(&apos[(size_t)p * 3]);
}
} // namespace

static void AccBounds(Inst *in, float t, const Matrix4 &root, int part, bool exact, Box &box)
{
	Pose p;
	if (!in->EvalPose(t, p))
		return;
	Matrix4 R = MatMul(in->XF(p), root);
	if (part == 0)
	{
		if (exact)
			return; // original quirk: the instance's own meshes are not measured
		box.AddBox(p.file->bounds, R);
		for (auto &c : in->children)
			AccBounds(c.inst, t, in->ChildRoot(t, R, c.bolt), 0, false, box);
		return;
	}
	int node = in->obj->NodeOf(p.file, part);
	if (node < 0 || p.file->nodes[node].kind != kNodeMesh)
		return;
	const GhbFile &f = *p.file;
	if (!exact)
	{
		box.AddBox(f.nodes[node].meshBounds, R);
		return;
	}
	std::vector<float> apos(f.geo.nApos * 3);
	if (f.geo.nApos)
		f.AnimPositions(p.dominant, apos.data());
	int mesh = f.nodes[node].meshIndex;
	for (auto &s : f.geo.surfaces)
	{
		if (s.mesh != mesh)
			continue;
		for (uint16_t c : s.tris)
		{
			Vec3 v;
			CornerPosition(f, c, apos, v);
			box.Add(XformPoint(v, R));
		}
	}
}

void Inst::GetBounds(float time, const Matrix4 *root, float *mins, float *maxs, GhoulID part, bool exact)
{
	Matrix4 r;
	if (root)
		r = *root;
	else
		MatIdentity(r);
	Box box;
	AccBounds(this, time, r, part, exact, box);
	Vec3 mn = box.any ? box.mn : Vec3(), mx = box.any ? box.mx : Vec3();
	if (mins)
		mn.Store(mins);
	if (maxs)
		mx.Store(maxs);
}

// ------------------------------------------------------------------ ray test (method 43)

// SPEC-012 section 7 box pre-test
static bool RayBox(const BoundsRec &b, const Vec3 &S, const Vec3 &D)
{
	if (b.infinite)
		return true;
	if (b.empty)
		return false;
	for (int a = 0; a < 3; a++)
	{
		if (D[a] > 0 && S[a] > b.mx[a])
			return false;
		if (D[a] < 0 && S[a] < b.mn[a])
			return false;
	}
	int k = 0;
	for (int a = 1; a < 3; a++)
		if (std::fabs(D[a]) > std::fabs(D[k]))
			k = a;
	if (std::fabs(D[k]) < 0.02f)
		return false;
	float t0 = (b.mn[k] - S[k]) / D[k], t1 = (b.mx[k] - S[k]) / D[k];
	Vec3 p0 = S + D * t0, p1 = S + D * t1;
	for (int a = 0; a < 3; a++)
	{
		if (a == k)
			continue;
		float lo = std::min(p0[a], p1[a]), hi = std::max(p0[a], p1[a]);
		if (hi < b.mn[a] || lo > b.mx[a])
			return false;
	}
	return true;
}

struct TraceHit
{
	HitRecord rec;
	size_t order;
};

static void TraceLevel(Inst *in, float t, const Matrix4 &root, const Vec3 &Se, const Vec3 &De,
	std::vector<TraceHit> &out)
{
	Pose p;
	if (!in->EvalPose(t, p))
		return;
	Matrix4 R = MatMul(in->XF(p), root);
	Matrix4 Ri = MatInverse(R);
	Vec3 Sm = XformPoint(Se, Ri);
	Vec3 Dm = Normalize(XformVector(De, Ri));
	float s = Length(Vec3(R.m[0][0], R.m[1][0], R.m[2][0]));
	const GhbFile &f = *p.file;
	if (f.hasGeometry && RayBox(f.bounds, Sm, Dm))
	{
		std::vector<float> apos(f.geo.nApos * 3);
		bool aposDone = false;
		std::vector<int8_t> meshOk(f.meshes.size(), -1);
		for (auto &sf : f.geo.surfaces)
		{
			if (sf.node < 0)
				continue;
			const GhbNode &nd = f.nodes[sf.node];
			int part = in->obj->PartOfNode(p.file, sf.node);
			if (part && !in->PartShown(part))
				continue;
			if (nd.noblock || !nd.visible.At(p.dominant))
				continue;
			int8_t &ok = meshOk[sf.mesh];
			if (ok < 0)
				ok = RayBox(nd.meshBounds, Sm, Dm) ? 1 : 0;
			if (!ok)
				continue;
			if (!aposDone && f.geo.nApos)
			{
				f.AnimPositions(p.dominant, apos.data());
				aposDone = true;
			}
			for (size_t k = 0; k + 2 < sf.tris.size(); k += 3)
			{
				Vec3 p0, p1, p2;
				CornerPosition(f, sf.tris[k], apos, p0);
				CornerPosition(f, sf.tris[k + 1], apos, p1);
				CornerPosition(f, sf.tris[k + 2], apos, p2);
				Vec3 n = Cross(p2 - p1, p0 - p1);
				float dn = Dot(Dm, n);
				if (std::fabs(dn) < 0.01f)
					continue;
				float tt = Dot(n, p0 - Sm) / dn;
				if (tt < -0.001f || tt > 1e30f)
					continue;
				Vec3 H = Sm + Dm * tt;
				if (Dot(Cross(p1 - p0, H - p0), n) < 0 || Dot(Cross(p2 - p1, H - p1), n) < 0 ||
					Dot(Cross(p0 - p2, H - p2), n) < 0)
					continue;
				TraceHit h;
				std::memset(&h.rec, 0, sizeof(h.rec));
				h.rec.inst = in;
				h.rec.part = (uint16_t)part;
				h.rec.distance = s * tt;
				Vec3 nn = Length(n) <= 1e-10f ? Vec3() : XformVector(Normalize(n), R);
				nn.Store(h.rec.normal);
				(Se + De * h.rec.distance).Store(h.rec.point);
				h.order = out.size();
				out.push_back(h);
			}
		}
	}
	for (auto &c : in->children)
		TraceLevel(c.inst, t, in->ChildRoot(t, R, c.bolt), Se, De, out);
}

int Inst::TraceRay(float time, const float *start, const float *dir, HitRecord *hits, int maxHits)
{
	if (!start || !dir)
		return 0;
	std::vector<TraceHit> all;
	Matrix4 root;
	MatIdentity(root);
	TraceLevel(this, time, root, Vec3(start), Vec3(dir), all);
	std::stable_sort(all.begin(), all.end(),
		[](const TraceHit &a, const TraceHit &b) { return a.rec.distance < b.rec.distance; });
	int n = 0;
	for (auto &h : all)
	{
		if (n >= maxHits || !hits)
			break;
		hits[n++] = h.rec;
	}
	return n;
}

// ------------------------------------------------------------------ swept box (method 44, no original caller)

namespace {
// Separating-axis overlap of an axis-aligned box (centre c, half extents e) and a triangle
bool BoxTriOverlap(const Vec3 &c, const Vec3 &e, Vec3 v0, Vec3 v1, Vec3 v2)
{
	v0 = v0 - c;
	v1 = v1 - c;
	v2 = v2 - c;
	Vec3 f[3] = {v1 - v0, v2 - v1, v0 - v2};
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			Vec3 ax;
			Vec3 u;
			u[i] = 1.0f;
			ax = Cross(u, f[j]);
			float p0 = Dot(v0, ax), p1 = Dot(v1, ax), p2 = Dot(v2, ax);
			float r = e.x * std::fabs(ax.x) + e.y * std::fabs(ax.y) + e.z * std::fabs(ax.z);
			if (std::min(p0, std::min(p1, p2)) > r || std::max(p0, std::max(p1, p2)) < -r)
				return false;
		}
	}
	for (int a = 0; a < 3; a++)
		if (std::min(v0[a], std::min(v1[a], v2[a])) > e[a] || std::max(v0[a], std::max(v1[a], v2[a])) < -e[a])
			return false;
	Vec3 n = Cross(f[0], f[1]);
	float d = Dot(n, v0);
	float r = e.x * std::fabs(n.x) + e.y * std::fabs(n.y) + e.z * std::fabs(n.z);
	return std::fabs(d) <= r;
}

// ray (origin o, direction d, t in [0,1)) against a triangle, two-sided; returns t or -1
float SegTri(const Vec3 &o, const Vec3 &d, const Vec3 &p0, const Vec3 &p1, const Vec3 &p2)
{
	Vec3 n = Cross(p1 - p0, p2 - p0);
	float dn = Dot(d, n);
	if (std::fabs(dn) < 1e-12f)
		return -1.0f;
	float t = Dot(n, p0 - o) / dn;
	if (t < 0.0f || t >= 1.0f)
		return -1.0f;
	Vec3 h = o + d * t;
	float s0 = Dot(Cross(p1 - p0, h - p0), n), s1 = Dot(Cross(p2 - p1, h - p1), n), s2 = Dot(Cross(p0 - p2, h - p2), n);
	if ((s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0))
		return t;
	return -1.0f;
}

// ray against an axis-aligned box, entry t in [0,1) or -1
float SegBox(const Vec3 &o, const Vec3 &d, const Vec3 &mn, const Vec3 &mx)
{
	float t0 = 0.0f, t1 = 1.0f;
	for (int a = 0; a < 3; a++)
	{
		if (std::fabs(d[a]) < 1e-12f)
		{
			if (o[a] < mn[a] || o[a] > mx[a])
				return -1.0f;
			continue;
		}
		float u0 = (mn[a] - o[a]) / d[a], u1 = (mx[a] - o[a]) / d[a];
		if (u0 > u1)
			std::swap(u0, u1);
		t0 = std::max(t0, u0);
		t1 = std::min(t1, u1);
		if (t0 > t1)
			return -1.0f;
	}
	return t0 < 1.0f ? t0 : -1.0f;
}

// moving segment a0 + u*ad + t*m against a static segment b0 + v*bd
float SegSeg(const Vec3 &a0, const Vec3 &ad, const Vec3 &m, const Vec3 &b0, const Vec3 &bd)
{
	// a0 + u ad + t m = b0 + v bd  ->  u ad - v bd + t m = b0 - a0
	Vec3 c0 = ad, c1 = -bd, c2 = m, r = b0 - a0;
	float det = Dot(c0, Cross(c1, c2));
	if (std::fabs(det) < 1e-12f)
		return -1.0f;
	float u = Dot(r, Cross(c1, c2)) / det;
	float v = Dot(c0, Cross(r, c2)) / det;
	float t = Dot(c0, Cross(c1, r)) / det;
	if (u < 0 || u > 1 || v < 0 || v > 1 || t < 0 || t >= 1)
		return -1.0f;
	return t;
}
} // namespace

static void BoxLevel(Inst *in, float t, const Matrix4 &root, const Vec3 &Se, const Vec3 &Ee, const Vec3 &bmn,
	const Vec3 &bmx, std::vector<TraceHit> &out)
{
	Pose p;
	if (!in->EvalPose(t, p))
		return;
	Matrix4 R = MatMul(in->XF(p), root);
	Matrix4 Ri = MatInverse(R);
	Vec3 S = XformPoint(Se, Ri), E = XformPoint(Ee, Ri);
	Vec3 M = E - S;
	float L = Length(M);
	const GhbFile &f = *p.file;
	// file box rejected first, tolerance 1.0
	bool fileOk = f.hasGeometry && !f.bounds.empty;
	if (fileOk && !f.bounds.infinite)
	{
		for (int a = 0; a < 3; a++)
		{
			float lo = std::min(S[a], E[a]) + bmn[a] - 1.0f, hi = std::max(S[a], E[a]) + bmx[a] + 1.0f;
			if (hi < f.bounds.mn[a] || lo > f.bounds.mx[a])
				fileOk = false;
		}
	}
	if (fileOk)
	{
		std::vector<float> apos(f.geo.nApos * 3);
		if (f.geo.nApos)
			f.AnimPositions(p.dominant, apos.data());
		Vec3 half = (bmx - bmn) * 0.5f, centre0 = S + (bmn + bmx) * 0.5f;
		for (auto &sf : f.geo.surfaces)
		{
			if (sf.node < 0)
				continue;
			const GhbNode &nd = f.nodes[sf.node];
			int part = in->obj->PartOfNode(p.file, sf.node);
			if ((part && !in->PartShown(part)) || nd.noblock || !nd.visible.At(p.dominant))
				continue;
			for (size_t k = 0; k + 2 < sf.tris.size(); k += 3)
			{
				Vec3 v[3];
				for (int j = 0; j < 3; j++)
					CornerPosition(f, sf.tris[k + j], apos, v[j]);
				float best = 1.0f;
				if (BoxTriOverlap(centre0, half, v[0], v[1], v[2]))
					best = -1.0f; // start solid
				else
				{
					for (int c = 0; c < 8; c++)
					{
						Vec3 o = S + Vec3((c & 1) ? bmx.x : bmn.x, (c & 2) ? bmx.y : bmn.y, (c & 4) ? bmx.z : bmn.z);
						float tt = SegTri(o, M, v[0], v[1], v[2]);
						if (tt >= 0 && tt < best)
							best = tt;
					}
					for (int j = 0; j < 3; j++)
					{
						float tt = SegBox(v[j], -M, S + bmn, S + bmx);
						if (tt >= 0 && tt < best)
							best = tt;
					}
					for (int ax = 0; ax < 3; ax++)
						for (int c = 0; c < 4; c++)
						{
							Vec3 a0 = S + bmn;
							int o1 = (ax + 1) % 3, o2 = (ax + 2) % 3;
							a0[o1] = S[o1] + ((c & 1) ? bmx[o1] : bmn[o1]);
							a0[o2] = S[o2] + ((c & 2) ? bmx[o2] : bmn[o2]);
							Vec3 ad;
							ad[ax] = bmx[ax] - bmn[ax];
							for (int j = 0; j < 3; j++)
							{
								float tt = SegSeg(a0, ad, M, v[j], v[(j + 1) % 3] - v[j]);
								if (tt >= 0 && tt < best)
									best = tt;
							}
						}
				}
				if (best >= 1.0f)
					continue;
				float frac = best;
				if (frac > 0.0f)
					frac = L * frac < 1.0f ? 0.0f : frac * (L - 1.0f) / L;
				TraceHit h;
				std::memset(&h.rec, 0, sizeof(h.rec));
				h.rec.inst = in;
				h.rec.part = (uint16_t)part;
				h.rec.distance = frac;
				Vec3 n = Cross(v[2] - v[1], v[0] - v[1]);
				Vec3 nn = Length(n) <= 1e-10f ? Vec3() : XformVector(Normalize(n), R);
				nn.Store(h.rec.normal);
				// second vector: UNVERIFIED in SPEC-012; the swept position of the box origin is stored
				(Se + (Ee - Se) * (frac < 0 ? 0.0f : frac)).Store(h.rec.point);
				h.order = out.size();
				out.push_back(h);
			}
		}
	}
	for (auto &c : in->children)
		BoxLevel(c.inst, t, in->ChildRoot(t, R, c.bolt), Se, Ee, bmn, bmx, out);
}

int Inst::TraceBox(float time, const Matrix4 *root, const float *start, const float *end, const float *mins,
	const float *maxs, HitRecord *hits, int maxHits)
{
	if (!start || !end)
		return 0;
	Matrix4 r;
	if (root)
		r = *root;
	else
		MatIdentity(r);
	Vec3 mn = mins ? Vec3(mins) : Vec3(), mx = maxs ? Vec3(maxs) : Vec3();
	std::vector<TraceHit> all;
	BoxLevel(this, time, r, Vec3(start), Vec3(end), mn, mx, all);
	std::stable_sort(all.begin(), all.end(),
		[](const TraceHit &a, const TraceHit &b) { return a.rec.distance < b.rec.distance; });
	int n = 0;
	for (auto &h : all)
	{
		if (n >= maxHits || !hits)
			break;
		hits[n++] = h.rec;
	}
	return n;
}

// ------------------------------------------------------------------ bolt matrix (method 50)

void Inst::GetBoltMatrix(float time, Matrix4 *out, GhoulID bolt, int matrixMode, bool throughParents)
{
	if (!out)
		return;
	BoltMatrix(time, *out, bolt, matrixMode, throughParents);
}

// ------------------------------------------------------------------ state capture for network / save

NetState Inst::Capture() const
{
	NetState s;
	s.on = on;
	s.speed = speed;
	s.paused = paused;
	s.reverse = reverse;
	s.attachBolt = attachBolt;
	s.seq = seq;
	s.mode = mode;
	s.A = A;
	s.S = S;
	s.pauseEnd = pauseEnd;
	std::memcpy(s.tint, tint, 4);
	s.xf = xf;
	s.parts.assign(obj->parts.size(), 1);
	for (size_t i = 0; i < s.parts.size() && i < partFlags.size(); i++)
		s.parts[i] = partFlags[i];
	for (auto &c : children)
		if (c.inst->id > 0)
			s.boltons[c.inst->id] = c.bolt;
	s.skins = skinOverrides;
	return s;
}

void Inst::Apply(const NetState &s, bool)
{
	on = s.on;
	speed = s.speed;
	paused = s.paused;
	reverse = s.reverse;
	attachBolt = s.attachBolt;
	seq = s.seq;
	mode = s.mode;
	A = s.A;
	S = s.S;
	pauseEnd = s.pauseEnd;
	std::memcpy(tint, s.tint, 4);
	xf = s.xf;
	UpdateTransformDerived();
	partFlags = s.parts;
	SetPartFlagsSize();
	skinOverrides = s.skins;
	RecomputeTiming();
}

} // namespace ghoul
