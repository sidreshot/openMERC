// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program - OBJ and glTF 2.0 export of posed triangles (captured from the draw path).
// Implements: SPEC-012 section 4 (test output of evaluated poses)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "test_common.h"

bool WriteOBJ(const std::string &path, const std::vector<PosedTri> &tris)
{
	FILE *f = std::fopen(path.c_str(), "w");
	if (!f)
		return false;
	std::string base = path.substr(path.find_last_of("/\\") + 1);
	std::string mtl = base.substr(0, base.size() - 4) + ".mtl";
	std::fprintf(f, "# OpenMerc omghoul_test: posed Ghoul model (world space, Quake axes)\nmtllib %s\n", mtl.c_str());
	for (auto &t : tris)
		for (int k = 0; k < 3; k++)
			std::fprintf(f, "v %.4f %.4f %.4f\n", t.p[k][0], t.p[k][1], t.p[k][2]);
	for (auto &t : tris)
		for (int k = 0; k < 3; k++)
			std::fprintf(f, "vt %.5f %.5f\n", t.uv[k][0], 1.0f - t.uv[k][1]);
	int cur = -1;
	for (size_t i = 0; i < tris.size(); i++)
	{
		if (tris[i].material != cur)
		{
			cur = tris[i].material;
			std::fprintf(f, "usemtl tex%d\n", cur);
		}
		int b = (int)i * 3 + 1;
		std::fprintf(f, "f %d/%d %d/%d %d/%d\n", b, b, b + 1, b + 1, b + 2, b + 2);
	}
	std::fclose(f);
	std::string mp = path.substr(0, path.size() - 4) + ".mtl";
	FILE *m = std::fopen(mp.c_str(), "w");
	if (m)
	{
		std::set<int> mats;
		for (auto &t : tris)
			mats.insert(t.material);
		std::string stem = base.substr(0, base.size() - 4);
		for (int k : mats)
		{
			std::fprintf(m, "newmtl tex%d\nKd 1 1 1\n", k);
			if (k)
				std::fprintf(m, "map_Kd %s_tex%d.png\n", stem.c_str(), k);
		}
		std::fclose(m);
	}
	return true;
}

static void PutF(std::vector<uint8_t> &b, float v)
{
	uint8_t x[4];
	std::memcpy(x, &v, 4);
	b.insert(b.end(), x, x + 4);
}

static void PutU32(std::vector<uint8_t> &b, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		b.push_back((uint8_t)(v >> (8 * i)));
}

bool WriteGLTF(const std::string &path, const std::vector<PosedTri> &tris)
{
	// one primitive per material; positions converted from Quake axes (x fwd, y left, z up) to glTF (y up)
	std::map<int, std::vector<const PosedTri *>> byMat;
	for (auto &t : tris)
		byMat[t.material].push_back(&t);
	std::vector<uint8_t> bin;
	std::string stem = path.substr(path.find_last_of("/\\") + 1);
	stem = stem.substr(0, stem.size() - 5);
	std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"OpenMerc omghoul_test\"},\"scene\":0,"
					   "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],";
	std::string acc, views, prims, mats, texs, imgs;
	int ai = 0, vi = 0, mi = 0;
	for (auto &e : byMat)
	{
		size_t n = e.second.size() * 3;
		float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
		size_t posOfs = bin.size();
		for (auto *t : e.second)
			for (int k = 0; k < 3; k++)
			{
				float p[3] = {-t->p[k][1], t->p[k][2], -t->p[k][0]}; // glTF: +y up, -z forward
				for (int a = 0; a < 3; a++)
				{
					mn[a] = std::min(mn[a], p[a]);
					mx[a] = std::max(mx[a], p[a]);
					PutF(bin, p[a]);
				}
			}
		size_t uvOfs = bin.size();
		for (auto *t : e.second)
			for (int k = 0; k < 3; k++)
			{
				PutF(bin, t->uv[k][0]);
				PutF(bin, t->uv[k][1]);
			}
		char buf[512];
		if (!views.empty())
			views += ",";
		std::snprintf(buf, sizeof(buf), "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}",
			(unsigned)posOfs, (unsigned)(n * 12), (unsigned)uvOfs, (unsigned)(n * 8));
		views += buf;
		if (!acc.empty())
			acc += ",";
		std::snprintf(buf, sizeof(buf),
			"{\"bufferView\":%d,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},"
			"{\"bufferView\":%d,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"}",
			vi, (unsigned)n, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], vi + 1, (unsigned)n);
		acc += buf;
		if (!prims.empty())
			prims += ",";
		std::snprintf(buf, sizeof(buf), "{\"attributes\":{\"POSITION\":%d,\"TEXCOORD_0\":%d},\"material\":%d}", ai, ai + 1, mi);
		prims += buf;
		if (!mats.empty())
			mats += ",";
		if (e.first)
		{
			int ti = texs.empty() ? 0 : (int)std::count(texs.begin(), texs.end(), '{');
			std::snprintf(buf, sizeof(buf),
				"{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":%d},\"metallicFactor\":0},\"doubleSided\":true}", ti);
			mats += buf;
			if (!texs.empty())
				texs += ",";
			std::snprintf(buf, sizeof(buf), "{\"source\":%d}", ti);
			texs += buf;
			if (!imgs.empty())
				imgs += ",";
			std::snprintf(buf, sizeof(buf), "{\"uri\":\"%s_tex%d.png\"}", stem.c_str(), e.first);
			imgs += buf;
		}
		else
			mats += "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.7,0.7,0.7,1],\"metallicFactor\":0},\"doubleSided\":true}";
		ai += 2;
		vi += 2;
		mi++;
	}
	char buf[256];
	std::snprintf(buf, sizeof(buf), "\"buffers\":[{\"uri\":\"%s.bin\",\"byteLength\":%u}],", stem.c_str(), (unsigned)bin.size());
	json += buf;
	json += "\"bufferViews\":[" + views + "],\"accessors\":[" + acc + "],\"meshes\":[{\"primitives\":[" + prims +
		"]}],\"materials\":[" + mats + "]";
	if (!texs.empty())
		json += ",\"textures\":[" + texs + "],\"images\":[" + imgs + "]";
	json += "}";
	FILE *f = std::fopen(path.c_str(), "w");
	if (!f)
		return false;
	std::fputs(json.c_str(), f);
	std::fclose(f);
	std::string bp = path.substr(0, path.size() - 5) + ".bin";
	FILE *b = std::fopen(bp.c_str(), "wb");
	if (!b)
		return false;
	std::fwrite(bin.data(), 1, bin.size(), b);
	std::fclose(b);
	return true;
}
