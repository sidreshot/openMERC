// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - small vector / matrix helpers (row-vector convention of SPEC-011).
// Implements: SPEC-011 (matrix layout and identity flag), SPEC-012 (composition A*B applies A first)
#pragma once

#include <cmath>
#include <cstring>

#include "ghoul_api.h"

namespace ghoul {

struct Vec3
{
	float x, y, z;
	Vec3() : x(0), y(0), z(0) {}
	Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
	explicit Vec3(const float *p) : x(p[0]), y(p[1]), z(p[2]) {}
	float operator[](int i) const { return (&x)[i]; }
	float &operator[](int i) { return (&x)[i]; }
	Vec3 operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
	Vec3 operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
	Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
	Vec3 operator-() const { return Vec3(-x, -y, -z); }
	Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
	void Store(float *p) const { p[0] = x; p[1] = y; p[2] = z; }
};

inline float Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3 &a, const Vec3 &b)
{
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float Length(const Vec3 &a) { return std::sqrt(Dot(a, a)); }
// SPEC-014: a vector of length <= 1e-10 normalises to the zero vector
inline Vec3 Normalize(const Vec3 &a)
{
	float l = Length(a);
	if (l <= 1e-10f)
		return Vec3();
	float s = 1.0f / l;
	return Vec3(a.x * s, a.y * s, a.z * s);
}

inline void MatIdentity(Matrix4 &m)
{
	std::memset(&m, 0, sizeof(m));
	m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
	m.flags = 1;
}

inline Matrix4 MatIdentity()
{
	Matrix4 m;
	MatIdentity(m);
	return m;
}

// identity flag with the SPEC-011 tolerance 0.0001
inline bool MatIsIdentity(const Matrix4 &m)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			if (std::fabs(m.m[r][c] - (r == c ? 1.0f : 0.0f)) > 0.0001f)
				return false;
	return true;
}

inline void MatUpdateFlags(Matrix4 &m) { m.flags = MatIsIdentity(m) ? 1u : 0u; }

// out = a * b (a applied first). out may alias neither input.
inline void MatMul(const Matrix4 &a, const Matrix4 &b, Matrix4 &out)
{
	if (a.flags & 1)
	{
		out = b;
		return;
	}
	if (b.flags & 1)
	{
		out = a;
		return;
	}
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			out.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] +
				a.m[i][3] * b.m[3][j];
	out.flags = 0;
}

inline Matrix4 MatMul(const Matrix4 &a, const Matrix4 &b)
{
	Matrix4 r;
	MatMul(a, b, r);
	return r;
}

// inverse of an affine matrix (column 3 = 0,0,0,1); a singular 3x3 gives the identity
inline void MatInverse(const Matrix4 &a, Matrix4 &out)
{
	if (a.flags & 1)
	{
		MatIdentity(out);
		return;
	}
	const float(*m)[4] = a.m;
	float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
	float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
	float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
	float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
	if (std::fabs(det) < 1e-30f)
	{
		MatIdentity(out);
		return;
	}
	float id = 1.0f / det;
	Matrix4 r;
	r.m[0][0] = c00 * id;
	r.m[1][0] = c01 * id;
	r.m[2][0] = c02 * id;
	r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * id;
	r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id;
	r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * id;
	r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id;
	r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * id;
	r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id;
	r.m[0][3] = r.m[1][3] = r.m[2][3] = 0.0f;
	for (int j = 0; j < 3; j++)
		r.m[3][j] = -(m[3][0] * r.m[0][j] + m[3][1] * r.m[1][j] + m[3][2] * r.m[2][j]);
	r.m[3][3] = 1.0f;
	r.flags = 0;
	MatUpdateFlags(r);
	out = r;
}

inline Matrix4 MatInverse(const Matrix4 &a)
{
	Matrix4 r;
	MatInverse(a, r);
	return r;
}

inline Vec3 XformPoint(const Vec3 &p, const Matrix4 &m)
{
	return Vec3(p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
		p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
		p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2]);
}

inline Vec3 XformVector(const Vec3 &p, const Matrix4 &m)
{
	return Vec3(p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0],
		p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1],
		p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2]);
}

inline float MatDet3(const Matrix4 &a)
{
	const float(*m)[4] = a.m;
	return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
		m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

inline bool MatNearlyEqual(const Matrix4 &a, const Matrix4 &b, float tol)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			if (std::fabs(a.m[r][c] - b.m[r][c]) > tol)
				return false;
	return true;
}

} // namespace ghoul
