// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program - shared helpers (host over the extracted data, checks, images).
// Implements: SPEC-010 (test host), SPEC-012 (test support)
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../ghoul_api.h"
#include "../ghoul_host.h"

extern int g_fail, g_pass;
// true when the data root holds extracted stock Ghoul files; checks of stock models skip without it
extern bool g_haveData;
#define CHECK(c, ...)                                                                                                  \
	do                                                                                                                 \
	{                                                                                                                  \
		if (c)                                                                                                         \
			g_pass++;                                                                                                  \
		else                                                                                                           \
		{                                                                                                              \
			g_fail++;                                                                                                  \
			std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c);                                                   \
			std::printf(__VA_ARGS__);                                                                                  \
			std::printf("\n");                                                                                         \
		}                                                                                                              \
	} while (0)

// test host: game paths resolve below <root>/pak3, pak2, pak0 (higher pak wins); synthetic files overlay them
struct TestHost
{
	std::string root;
	std::map<std::string, std::vector<uint8_t>> overlay; // upper-case game path -> bytes
	std::vector<std::string> log;
	bool quiet = false;
	int loads = 0;
};
extern TestHost g_host;
void InstallHost(const std::string &root);
bool ReadFileBytes(const std::string &path, std::vector<uint8_t> &out);
std::vector<std::string> ListFiles(const std::string &dir, const std::string &ext); // recursive, game paths

// images
struct Image
{
	int w = 0, h = 0;
	std::vector<uint8_t> rgb;
	void Init(int W, int H, uint8_t r, uint8_t g, uint8_t b)
	{
		w = W;
		h = H;
		rgb.assign((size_t)W * H * 3, 0);
		for (size_t i = 0; i < (size_t)W * H; i++)
		{
			rgb[i * 3] = r;
			rgb[i * 3 + 1] = g;
			rgb[i * 3 + 2] = b;
		}
	}
};
bool WritePNG(const std::string &path, const Image &img);

// software GL table (soft_gl.cpp)
struct SoftGL;
SoftGL *SoftGL_Create(int w, int h);
const ghoul::GhoulGLTable *SoftGL_Table();
void SoftGL_Begin(SoftGL *s, const float view[16], const float proj[16], uint8_t clear[3]);
const Image &SoftGL_Image(SoftGL *s);
int SoftGL_Triangles(SoftGL *s);
int SoftGL_TexturedTriangles(SoftGL *s);
void SoftGL_Reset(SoftGL *s);
// I4c: values passed to Lightf(GL_LINEAR_ATTENUATION) since the last call (SPEC-AMEND-005 I4b.5)
std::vector<float> SoftGL_TakeLinearAttenuations();

// exports (export.cpp)
struct PosedTri
{
	float p[3][3];
	float uv[3][2];
	float n[3][3];
	int material;
	std::string group;
};
bool WriteOBJ(const std::string &path, const std::vector<PosedTri> &tris);
bool WriteGLTF(const std::string &path, const std::vector<PosedTri> &tris);
