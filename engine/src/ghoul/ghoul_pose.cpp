// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - playback clock, frame pairs, node/bolt matrices, bolt-on composition and
// event delivery.
// Implements: SPEC-011 (playback, events, bolt matrices), SPEC-012 section 3 (fraction, frame pair, dominant
//             frame, side effects), section 4 (pose evaluation of node transforms), section 5 (events: matrix
//             events, note keys, EOS, at most two cycles, bolt-ons, queue order), section 6 (XF, method 50
//             pose and rate modes, throughParents composition); SPEC-AMEND-005 I3.2 (raw phase of a paused
//             instance)

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ghoul_internal.h"

namespace ghoul {

static float Frac(float x) { return x - std::floor(x); }
static float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// ------------------------------------------------------------------ transform and timing

void Inst::UpdateTransformDerived()
{
	MatInverse(xf, inv);
	detSign = MatDet3(xf) < 0.0f ? -1 : 1;
	scale = std::sqrt(xf.m[0][0] * xf.m[0][0] + xf.m[0][1] * xf.m[0][1] + xf.m[0][2] * xf.m[0][2]);
}

void Inst::RecomputeTiming()
{
	const SeqEntry *e = obj->Seq(seq);
	if (!e || !e->file || e->index < 0)
	{
		D = F = 0.0f;
		N = 0;
		return;
	}
	const GhbSequence &s = e->file->sequences[e->index];
	N = (int)s.count;
	F = s.frameMs * 0.001f * SpeedMultiplier(speed);
	switch (mode)
	{
	case 0:
		D = (float)N * F;
		break;
	case 1:
		D = (float)(N - 1) * F;
		break;
	case 2:
		D = (float)(2 * N - 1) * F;
		break;
	default:
		D = 0.0f;
		break;
	}
	if (D < 0.0f)
		D = 0.0f;
}

// SPEC-012 section 3: fraction f(t)
float Inst::Fraction(float t) const
{
	if (paused)
		t = A;
	if (!on || !seq)
		return 0.0f;
	float f;
	if (D == 0.0f)
		f = A; // holds the mode-3 fraction
	else
	{
		float d = (t - S) / D;
		switch (mode)
		{
		case 0:
			f = Frac(d);
			break;
		case 1:
			f = d;
			break;
		case 2:
		{
			float g = Frac(d);
			f = g < 0.5f ? 2.0f * g : 2.0f - 2.0f * g;
			break;
		}
		default:
			f = A;
			break;
		}
	}
	if (reverse)
		f = 1.0f - f;
	return Clamp01(f);
}

// SPEC-012 section 5: raw (unwrapped) phase used by note delivery
float Inst::RawPhase(float t) const
{
	// a paused instance is evaluated AT time A (not equal to A); only D = 0 gives A (SPEC-AMEND-005 I3.2)
	if (D == 0.0f)
		return A;
	if (paused)
		t = A;
	float d = t - S;
	if (mode == 1 && d > D)
		return 1.0f;
	return d / D;
}

static void FramePairRel(float f, int mode, int N, int &a, int &b, float &w)
{
	if (N <= 0)
	{
		a = b = 0;
		w = 1.0f;
		return;
	}
	if (mode == 0)
	{
		float x = (float)N * f;
		if (x >= (float)N)
			x -= (float)N; // f = 1 is the same pose as f = 0 in a loop
		a = (int)std::floor(x);
		if (a >= N)
			a = N - 1;
		if (a < 0)
			a = 0;
		b = a + 1 == N ? 0 : a + 1;
		w = (float)(a + 1) - x;
	}
	else
	{
		float x = (float)(N - 1) * f;
		a = (int)std::floor(x);
		if (a < 0)
			a = 0;
		if (a < N - 1)
		{
			b = a + 1;
			w = (float)(a + 1) - x;
		}
		else
		{
			a = b = N - 1;
			w = 1.0f;
		}
	}
}

bool Inst::PoseAtFraction(float f, Pose &p) const
{
	p = Pose();
	const SeqEntry *e = obj->Seq(seq);
	if (!e || !e->file || e->index < 0 || !on)
		return false;
	p.file = e->file;
	p.seq = &e->file->sequences[e->index];
	p.f = f;
	int a, b;
	float w;
	// N is the instance's frame count (instance method 27 may have replaced it: original quirk)
	FramePairRel(f, mode, N, a, b, w);
	uint32_t maxf = p.file->frames ? p.file->frames - 1 : 0;
	p.fp.a = std::min<uint32_t>(p.seq->first + (uint32_t)a, maxf);
	p.fp.b = std::min<uint32_t>(p.seq->first + (uint32_t)b, maxf);
	p.fp.w = w;
	p.dominant = p.fp.Dominant();
	p.adjacent = (b == a + 1 || a == b) && b < (int)p.seq->count;
	p.valid = true;
	return true;
}

bool Inst::EvalPose(float t, Pose &p) const
{
	if (!on || !seq)
	{
		p = Pose();
		return false;
	}
	return PoseAtFraction(Fraction(t), p);
}

// ------------------------------------------------------------------ bolts (SPEC-012 section 6)

bool Inst::BoltModel(const Pose &p, int bolt, Matrix4 &out) const
{
	if (!p.valid)
		return false;
	int node = obj->NodeOf(p.file, bolt);
	if (node < 0)
		return false;
	p.file->NodeMatrixPose(p.file->nodes[node], p.fp, out);
	p.file->usedNodes[node] = 1;
	return true;
}

Matrix4 Inst::XF(const Pose &p) const
{
	if (attachBolt)
	{
		Matrix4 ba;
		if (BoltModel(p, attachBolt, ba))
			return MatMul(MatInverse(ba), xf);
	}
	return xf;
}

static void ScaleColumns(Matrix4 &m, float s)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 3; c++)
			m.m[r][c] *= s;
	m.flags = 0;
	MatUpdateFlags(m);
}

void Inst::BoltMatrix(float t, Matrix4 &out, int bolt, int mmode, bool throughParents) const
{
	MatIdentity(out);
	Pose p;
	if (!EvalPose(t, p))
		return;
	Matrix4 bb;
	if (!BoltModel(p, bolt, bb))
		return;
	Matrix4 xfm = XF(p);
	Matrix4 own;
	if (mmode & 4)
	{
		switch (mmode & 3)
		{
		case 0:
			own = bb;
			break;
		case 1:
			own = MatMul(bb, xfm);
			break;
		case 2:
			own = MatInverse(bb);
			break;
		default:
			own = MatMul(MatInverse(xfm), MatInverse(bb));
			break;
		}
	}
	else
	{
		// rate: difference between the pose at the phase and 0.001 later (or earlier), per second
		if (D <= 0.0f)
			return;
		float pf = p.f;
		bool back;
		if (mode == 0)
		{
			float lim = N > 0 ? (float)(N - 1) / (float)N - 0.001f : 0.0f;
			back = pf > lim;
			if (back)
				pf = lim;
		}
		else
			back = pf + 0.001f > 1.0f;
		Pose p0, p1;
		PoseAtFraction(pf, p0);
		PoseAtFraction(back ? pf - 0.001f : pf + 0.001f, p1);
		Matrix4 m0, m1;
		if (!BoltModel(p0, bolt, m0) || !BoltModel(p1, bolt, m1))
			return;
		Matrix4 delta = back ? MatMul(m0, MatInverse(m1)) : MatMul(m1, MatInverse(m0));
		own = (mmode & 1) ? MatMul(delta, xfm) : delta;
		if (mmode & 2)
			own = MatInverse(own); // meaningless in the original too
		ScaleColumns(own, 1.0f / (0.001f * D));
	}
	if (throughParents && parent)
	{
		int hang = 0;
		for (auto &c : parent->children)
			if (c.inst == this)
			{
				hang = c.bolt;
				break;
			}
		Matrix4 pm;
		parent->BoltMatrix(t, pm, hang, mmode, true);
		out = (mmode & 2) ? MatMul(pm, own) : MatMul(own, pm);
	}
	else
		out = own;
	MatUpdateFlags(out);
}

Matrix4 Inst::ChildRoot(float t, const Matrix4 &myR, int myBolt) const
{
	Pose p;
	Matrix4 bb;
	if (EvalPose(t, p) && BoltModel(p, myBolt, bb))
		return MatMul(bb, myR);
	return myR;
}

bool Inst::PartShown(int part) const
{
	if (part <= 0)
		return true;
	if (part > (int)partFlags.size())
		return true;
	return partFlags[part - 1] != 0;
}

void Inst::SetPartFlagsSize()
{
	if (partFlags.size() < obj->parts.size())
		partFlags.resize(obj->parts.size(), 1);
}

// ------------------------------------------------------------------ events (SPEC-012 section 5)

void Inst::RestartEvents(float t, bool notes, bool matrices, bool setAux)
{
	if (notes)
		noteOn = true;
	if (matrices)
		matrixOn = true;
	if (setAux)
		A = t;
	cursorTime = A;
	cursorPhase = RawPhase(A);
	for (auto &c : children)
		c.inst->RestartEvents(c.inst->A, false, false, false);
}

void Inst::CollectEvents(float t, std::vector<QueuedEvent> &q, const Matrix4 *root)
{
	if (!seq || !on || !noteOn)
		return;
	Pose p;
	if (!EvalPose(t, p))
		return;
	cursorTime = t;
	// 1. matrix events, ascending bolt then mode, for bolts present in the current sequence's file
	bool matrices = matrixOn && (!parent || parent->matrixOn);
	if (matrices && !matrixCbs.empty())
	{
		std::vector<MatrixCb> cbs(matrixCbs);
		std::stable_sort(cbs.begin(), cbs.end(), [](const MatrixCb &a, const MatrixCb &b) {
			return a.bolt != b.bolt ? a.bolt < b.bolt : a.mode < b.mode;
		});
		for (auto &mc : cbs)
		{
			if (obj->NodeOf(p.file, mc.bolt) < 0)
				continue;
			QueuedEvent e;
			e.cb = mc.cb;
			e.user = user;
			e.time = t;
			e.isMatrix = true;
			BoltMatrix(t, e.m, mc.bolt, mc.mode, false);
			if (root)
				e.m = (mc.mode & 2) ? MatMul(MatInverse(*root), e.m) : MatMul(e.m, *root);
			q.push_back(e);
		}
	}
	// 2. note keys and EOS
	float r = RawPhase(t);
	float r0 = cursorPhase;
	if (r >= r0)
	{
		cursorPhase = r;
		float span = r - std::floor(r0);
		if (span > 2.0f)
			span = 2.0f;
		float lo = Frac(r0) * D * 1000.0f;
		int eos = obj->FindNote("EOS");
		const GhbSequence &s = *p.seq;
		for (;;)
		{
			float hi = span * D * 1000.0f;
			for (uint32_t k : s.keysByTime)
			{
				const GhbEvent &ev = s.events[k];
				if (ev.timeMs < lo || ev.timeMs >= hi)
					continue;
				int note = obj->FindNote(ev.typeUpper.c_str());
				if (!note)
					continue;
				for (auto &nc : noteCbs)
					if (nc.note == note)
					{
						QueuedEvent e;
						e.cb = nc.cb;
						e.user = user;
						e.time = t;
						e.isMatrix = false;
						e.text = ev.arg;
						q.push_back(e);
					}
			}
			span -= 1.0f;
			if (span > -0.0001f && eos)
				for (auto &nc : noteCbs)
					if (nc.note == eos)
					{
						QueuedEvent e;
						e.cb = nc.cb;
						e.user = user;
						e.time = t;
						e.isMatrix = false;
						q.push_back(e);
					}
			if (span <= 0.0f)
				break;
			lo = 0.0f;
		}
	}
	// 3. bolt-ons, in attach order, after their parent; their matrices are in the top entity space
	if (!children.empty())
	{
		Matrix4 myR = root ? MatMul(XF(p), *root) : XF(p);
		for (size_t i = 0; i < children.size(); i++)
		{
			Inst *c = children[i].inst;
			Matrix4 cr = ChildRoot(t, myR, children[i].bolt);
			c->CollectEvents(t, q, &cr);
		}
	}
}

void Inst::ProcessEvents(float t)
{
	std::vector<QueuedEvent> q;
	CollectEvents(t, q, nullptr);
	// all queued callbacks are called after the traversal, in queue order; matrix memory is the engine's
	for (auto &e : q)
	{
		if (e.isMatrix)
		{
			Matrix4 *m = new Matrix4(e.m);
			e.cb->Event(0, e.user, e.time, m);
			delete m;
		}
		else
			e.cb->Event(0, e.user, e.time, e.text.c_str());
	}
}

} // namespace ghoul
