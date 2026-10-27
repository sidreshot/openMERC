// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 OpenMerc contributors
//
// OpenMerc client: a thin C layer over the C++ interfaces of the engine's Ghoul model system (package I3,
// engine/src/ghoul/ghoul_api.h) for the client code in cl_ghoul.c - the client system object (SPEC-010 A,
// role "client"), its network methods 23/24/25/7/8 (SPEC-013 client side), instance lookup by server handle
// (system method 9), instance transforms (SPEC-011 methods 1/3) and drawing a menu model with the
// render-parameter block the engine's menu code fills itself (SPEC-011 method 41, SPEC-803 section 1).
//
// Implements: SPEC-010 (A), SPEC-011 (methods 1, 3, 41, 50; render parameters), SPEC-013 (sections 1-5,
//             client), SPEC-803 (section 1, Rendering)

#include <cstring>

#include "../ghoul/ghoul_api.h"
#include "header/cl_ghoul.h"

using namespace ghoul;

extern "C" void SV_Ghoul_Init(void); // engine: the library, once per process (package I1b)

namespace {

IGhoulSystem *Sys()
{
	SV_Ghoul_Init();
	return static_cast<IGhoulSystem *>(OMGhoul_GetSystem(OM_GHOUL_CLIENT));
}

void Reader(om_ghoul_bits_t *b, const unsigned char *data, int len)
{
	b->data = const_cast<unsigned char *>(data); // read only
	b->maxbits = (len > 0 ? len : 0) * 8;
	b->bit = 0;
	b->overflowed = 0;
}

// SPEC-AMEND-004 2: menu models are never culled
class MenuParams : public GhoulRenderParams
{
public:
	bool BoxOutside(const float (*)[3]) override { return false; }
};

void Identity(Matrix4 *m)
{
	std::memset(m, 0, sizeof(*m));
	m->m[0][0] = m->m[1][1] = m->m[2][2] = m->m[3][3] = 1.0f;
	m->flags = 1;
}

} // namespace

extern "C" {

void *CLG_System(void)
{
	return Sys();
}

// system method 24: store (and, once accepting, apply) one reliable chunk
void CLG_ReadReliable(const unsigned char *data, int len, int specular)
{
	om_ghoul_bits_t b;
	Reader(&b, data, len);
	Sys()->ClientReadReliable(0, &b, specular != 0);
}

// system method 25: instance states of one svc_ghoulunreliable, against the svc_frame of the packet
void CLG_ReadUnreliable(int frame, const unsigned char *data, int len, float time)
{
	om_ghoul_bits_t b;
	Reader(&b, data, len);
	Sys()->ClientReadUnreliable(frame, &b, time);
}

void CLG_StartAccepting(void) { Sys()->ClientStartAccepting(); } // method 23
void CLG_Purge(void) { Sys()->Purge(); }                         // method 7
void CLG_Reset(void) { Sys()->Reset(); }                         // method 8

// method 9: the server's handle (the client system negates it internally)
void *CLG_FindInstance(int handle)
{
	return handle ? Sys()->FindInstance(handle) : nullptr;
}

void CLG_GetTransform(void *instance, float m16[16])
{
	static_cast<IGhoulInst *>(instance)->GetTransform16(m16); // method 3
}

void CLG_SetTransform(void *instance, const float m16[16])
{
	static_cast<IGhoulInst *>(instance)->SetTransform16(m16); // method 1 (identity flag computed)
}

// SPEC-803 section 1 Rendering, step 2: the block the engine's menu code fills for instance method 41
void CLG_DrawMenu(void *instance, float x, float y, float time, int camerapart, int notexturestack)
{
	IGhoulInst *inst = static_cast<IGhoulInst *>(instance);
	MenuParams p;

	if (!inst)
		return;

	p.time = time;
	p.lightColor[0] = p.lightColor[1] = p.lightColor[2] = 0.0f;
	p.lightDir[0] = x + 50.0f;
	p.lightDir[1] = y + 50.0f;
	p.lightDir[2] = 200.0f;
	p.origin[0] = p.origin[1] = p.origin[2] = 0.0f;
	p.reserved2c = 0;
	p.dirValid = 0;
	p.useGLLights = 0;
	p.specular = 1;
	p.fastLights = 0;
	p.hardwareLights = 0;
	p.glDirectional = 0;
	p.glAttenuation = 0;
	p.entityFlag1000 = 0;
	p.shadowOnly = 0;
	p.noTextureStack = notexturestack ? 1 : 0;
	p.unused37 = 0;
	p.shadowDarkness = 0.0f;
	p.fogVisibility = 1.0f;
	p.shadowDist = 0.0f;
	p.worldTrace = nullptr;
	p.shadowMode = 0;
	p.maxLights = 0;
	p.viewForward[0] = 0.0f;
	p.viewForward[1] = 0.0f;
	p.viewForward[2] = 1.0f;
	p.noDynamicLights = 0;
	p.padAD[0] = p.padAD[1] = p.padAD[2] = 0;
	p.entity = nullptr;

	if (camerapart)
	{
		// the forced_camera part's bolt matrix through the parents (instance method 50, matrix mode 7)
		inst->GetBoltMatrix(time, &p.entityToWorld, (GhoulID)camerapart,
			kMatrixWithTransform | kMatrixInverse | kMatrixPose, true);
		p.viewOrigin[0] = p.viewOrigin[1] = p.viewOrigin[2] = 0.0f;
	}
	else
	{
		Identity(&p.entityToWorld);
		p.entityToWorld.m[3][0] = x;
		p.entityToWorld.m[3][1] = y;
		p.entityToWorld.flags = (x == 0.0f && y == 0.0f) ? 1u : 0u;
		p.viewOrigin[0] = x;
		p.viewOrigin[1] = y;
		p.viewOrigin[2] = -200.0f;
	}

	inst->Draw(&p);
}

// MENU-SCRATCH: CLG_DrawMenu for a model with no light nodes of its own (no camera part)
void CLG_DrawMenuLit(void *instance, float x, float y, float time, int notexturestack, float light)
{
	IGhoulInst *inst = static_cast<IGhoulInst *>(instance);
	MenuParams p;

	if (!inst)
		return;

	p.time = time;
	// the probe method (ambient half the colour and one wrap light): a key light from the upper left,
	// in front of the model (screen space: x right, y down, the camera at -z)
	p.lightColor[0] = p.lightColor[1] = p.lightColor[2] = light;
	p.lightDir[0] = -0.45f;
	p.lightDir[1] = -0.55f;
	p.lightDir[2] = -1.0f;
	p.origin[0] = p.origin[1] = p.origin[2] = 0.0f;
	p.reserved2c = 0;
	p.dirValid = 1;
	p.useGLLights = 0;
	p.specular = 1;
	p.fastLights = 0;
	p.hardwareLights = 0;
	p.glDirectional = 0;
	p.glAttenuation = 0;
	p.entityFlag1000 = 0;
	p.shadowOnly = 0;
	p.noTextureStack = notexturestack ? 1 : 0;
	p.unused37 = 0;
	p.shadowDarkness = 0.0f;
	p.fogVisibility = 1.0f;
	p.shadowDist = 0.0f;
	p.worldTrace = nullptr;
	p.shadowMode = 0;
	p.maxLights = 0;
	p.viewForward[0] = 0.0f;
	p.viewForward[1] = 0.0f;
	p.viewForward[2] = 1.0f;
	p.noDynamicLights = 0;
	p.padAD[0] = p.padAD[1] = p.padAD[2] = 0;
	p.entity = nullptr;

	{
		Identity(&p.entityToWorld);
		p.entityToWorld.m[3][0] = x;
		p.entityToWorld.m[3][1] = y;
		p.entityToWorld.flags = (x == 0.0f && y == 0.0f) ? 1u : 0u;
		p.viewOrigin[0] = x;
		p.viewOrigin[1] = y;
		p.viewOrigin[2] = -200.0f;
	}

	inst->Draw(&p);
}

} // extern "C"
