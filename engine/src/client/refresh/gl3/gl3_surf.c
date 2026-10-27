/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2016-2017 Daniel Gibson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * Surface generation and drawing
 *
 * =======================================================================
 *
 * OpenMerc: translucent surfaces that have a lightmap (Soldier of Fortune
 * maps) are drawn with it. Package I4a: SoF surface classification of
 * SPEC-410 (fog-region boundaries, sky, alpha surfaces into the sorted pass,
 * alpha images without the alpha flag), texture animation by the .m32 rate,
 * the detail/environment pass and the inline-model rules.
 * Implements: SPEC-101, SPEC-410
 */

#include <assert.h>
#include <stddef.h> // ofsetof()

#include "header/local.h"

int c_visible_lightmaps;
int c_visible_textures;
static vec3_t modelorg; /* relative to viewpoint */
static msurface_t *gl3_alpha_surfaces;

gl3lightmapstate_t gl3_lms;

#define BACKFACE_EPSILON 0.01

extern gl3image_t gl3textures[MAX_GL3TEXTURES];
extern int numgl3textures;

void GL3_SurfInit(void)
{
	// init the VAO and VBO for the standard vertexdata: 10 floats and 1 uint
	// (X, Y, Z), (S, T), (LMS, LMT), (normX, normY, normZ) ; lightFlags - last two groups for lightmap/dynlights

	glGenVertexArrays(1, &gl3state.vao3D);
	GL3_BindVAO(gl3state.vao3D);

	glGenBuffers(1, &gl3state.vbo3D);
	GL3_BindVBO(gl3state.vbo3D);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, texCoord));

	glEnableVertexAttribArray(GL3_ATTRIB_LMTEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_LMTEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, lmTexCoord));

	glEnableVertexAttribArray(GL3_ATTRIB_NORMAL);
	qglVertexAttribPointer(GL3_ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, normal));

	glEnableVertexAttribArray(GL3_ATTRIB_LIGHTFLAGS);
	qglVertexAttribIPointer(GL3_ATTRIB_LIGHTFLAGS, 1, GL_UNSIGNED_INT, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, lightFlags));

	glGenBuffers(1, &gl3state.ebo3D);

	// init VAO and VBO for model vertexdata: 9 floats
	// (X,Y,Z), (S,T), (R,G,B,A)

	glGenVertexArrays(1, &gl3state.vaoAlias);
	GL3_BindVAO(gl3state.vaoAlias);

	glGenBuffers(1, &gl3state.vboAlias);
	GL3_BindVBO(gl3state.vboAlias);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 3*sizeof(GLfloat));

	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 5*sizeof(GLfloat));

	glGenBuffers(1, &gl3state.eboAlias);

	// init VAO and VBO for particle vertexdata: 9 floats
	// (X,Y,Z), (point_size,distace_to_camera), (R,G,B,A)

	glGenVertexArrays(1, &gl3state.vaoParticle);
	GL3_BindVAO(gl3state.vaoParticle);

	glGenBuffers(1, &gl3state.vboParticle);
	GL3_BindVBO(gl3state.vboParticle);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 0);

	// TODO: maybe move point size and camera origin to UBO and calculate distance in vertex shader
	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD); // it's abused for (point_size, distance) here..
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 3*sizeof(GLfloat));

	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 5*sizeof(GLfloat));
}

void GL3_SurfShutdown(void)
{
	glDeleteBuffers(1, &gl3state.ebo3D);
	gl3state.ebo3D = 0;
	glDeleteBuffers(1, &gl3state.vbo3D);
	gl3state.vbo3D = 0;
	glDeleteVertexArrays(1, &gl3state.vao3D);
	gl3state.vao3D = 0;

	glDeleteBuffers(1, &gl3state.eboAlias);
	gl3state.eboAlias = 0;
	glDeleteBuffers(1, &gl3state.vboAlias);
	gl3state.vboAlias = 0;
	glDeleteVertexArrays(1, &gl3state.vaoAlias);
	gl3state.vaoAlias = 0;
}

static void
SetLightFlags(msurface_t *surf)
{
	unsigned int lightFlags = 0;

	if (gl3_worldmodel && (gl3_worldmodel->bspversion == BSPVERSION_SOF))
	{
		if (GL3_DynLight_PerPixel())
		{
			/* GFX-modern r_om_pdlight: the shader applies the same rule per pixel */
			lightFlags = GL3_DynLight_PixelFlags(surf);
		}
		else
		{
			/* OpenMerc (I4c): SoF dynamic light per lightmap sample (gl3_dynlight.c) */
			GL3_DynLight_Face(surf);
		}
	}
	/* OpenMerc (I4c): gl_dynamic 0, lightmaps are not updated after loading (SPEC-410 World diagnostics) */
	else if ((surf->dlightframe == gl3_framecount) && (!gl_dynamic || gl_dynamic->value))
	{
		lightFlags = surf->dlightbits;
	}

	gl3_3D_vtx_t* verts = surf->polys->vertices;

	int numVerts = surf->polys->numverts;
	for(int i=0; i<numVerts; ++i)
	{
		verts[i].lightFlags = lightFlags;
	}
}

static void
SetAllLightFlags(msurface_t *surf)
{
	/* OpenMerc (I4c): no dynamic lights with gl_dynamic 0 */
	unsigned int lightFlags = (!gl_dynamic || gl_dynamic->value) ? 0xffffffff : 0;

	if (gl3_worldmodel && (gl3_worldmodel->bspversion == BSPVERSION_SOF))
	{
		lightFlags = 0;

		if (GL3_DynLight_PerPixel())
		{
			/* GFX-modern r_om_pdlight: the shader applies the SoF rule per pixel */
			lightFlags = GL3_DynLight_PixelFlags(surf);
		}
		else
		{
			/* OpenMerc (I4c): SoF dynamic light per lightmap sample (gl3_dynlight.c) */
			GL3_DynLight_Face(surf);
		}
	}

	gl3_3D_vtx_t* verts = surf->polys->vertices;

	int numVerts = surf->polys->numverts;
	for(int i=0; i<numVerts; ++i)
	{
		verts[i].lightFlags = lightFlags;
	}
}

void
GL3_DrawGLPoly(msurface_t *fa, gl3drawCmd_t drawCmd)
{
	glpoly_t *p = fa->polys;

	drawCmd.emissive = GL3_Post_SurfEmissive(fa); /* GFX-modern bloom source */
	if (GL3_DynLight_PerPixel() && fa->texinfo && fa->texinfo->image)
	{
		/* GFX-modern r_om_pdlight: texture units of the face for the SoF dlight rule */
		drawCmd.texdim[0] = (float)fa->texinfo->image->width;
		drawCmd.texdim[1] = (float)fa->texinfo->image->height;
	}
	GL3_Add3DdrawCmdToBatch(p->vertices, p->numverts, GL_TRIANGLE_FAN, drawCmd);
}

void
GL3_DrawGLFlowingPoly(msurface_t *fa, gl3drawCmd_t drawCmd)
{
	glpoly_t *p;
	float scroll;

	p = fa->polys;
	drawCmd.emissive = GL3_Post_SurfEmissive(fa); /* GFX-modern bloom source */
	if (GL3_DynLight_PerPixel() && fa->texinfo && fa->texinfo->image)
	{
		/* GFX-modern r_om_pdlight: texture units of the face for the SoF dlight rule */
		drawCmd.texdim[0] = (float)fa->texinfo->image->width;
		drawCmd.texdim[1] = (float)fa->texinfo->image->height;
	}

	if (gl3_worldmodel && (gl3_worldmodel->bspversion == BSPVERSION_SOF))
	{
		/* OpenMerc (I4c), SPEC-410 Flowing surfaces: the texinfo value holds two signed 16-bit
		   speeds (s = upper, t = lower half); coordinates are reduced by speed x game clock x 0.01
		   (the texture repeats per 1.0, so only the fraction is kept) */
		short vs = (short)((fa->texinfo->value >> 16) & 0xFFFF);
		short vt = (short)(fa->texinfo->value & 0xFFFF);
		double clock = GL3_GameTime();

		drawCmd.scroll = -(float)fmod(vs * clock * 0.01, 1.0);
		drawCmd.scrollT = -(float)fmod(vt * clock * 0.01, 1.0);
		drawCmd.flags |= DCFlag_UseScroll;
		GL3_Add3DdrawCmdToBatch(p->vertices, p->numverts, GL_TRIANGLE_FAN, drawCmd);
		return;
	}

	scroll = -64.0f * ((r_newrefdef.time / 40.0f) - (int)(r_newrefdef.time / 40.0f));

	if (scroll == 0.0f)
	{
		scroll = -64.0f;
	}
	drawCmd.scroll = scroll;
	drawCmd.scrollT = 0;
	drawCmd.flags |= DCFlag_UseScroll;

	GL3_Add3DdrawCmdToBatch(p->vertices, p->numverts, GL_TRIANGLE_FAN, drawCmd);
}

#define LINE_VTX_COUNT (256 * 6)

/* I4c: the lightmapped world faces of the texture chains in a two-pass frame (gl_showtris on SoF maps) */
#define MAX_OUTLINE_SURFS 8192
static const msurface_t *outlinesurfs[MAX_OUTLINE_SURFS];
static int numoutlinesurfs;

static void
DrawTriangleOutlines(void)
{
	static gl3_3D_vtx_t vtx[LINE_VTX_COUNT];
	const msurface_t *surf;
	size_t i, curr_vtx;

	if (!gl_showtris->value)
	{
		return;
	}

	/* OpenMerc (I4c), SPEC-AMEND-006 I4c.3: on SoF maps the outlines cover only the lightmapped world faces
	   drawn from the texture chains, and only in frames drawn by the two-pass path (r_fullbright or night
	   vision; multitexture is always in use); never liquids, sky, alpha or boundary faces, inline models */
	qboolean sof = gl3_worldmodel && (gl3_worldmodel->bspversion == BSPVERSION_SOF);

	if (sof && !numoutlinesurfs)
	{
		return;
	}

	GL3_Draw3DBatchesNow();

	/* OpenMerc (I4c), SPEC-410 World diagnostics: 1 without, 2 (and more) with depth test; the lightmap
	   sample grid of 3 and 4 is not drawn */
	if (gl_showtris->value >= 2)
	{
		glEnable(GL_DEPTH_TEST);
	}
	else
	{
		glDisable(GL_DEPTH_TEST);
	}
	GL3_UseProgram(gl3state.si3DcolorOnly.shaderProgram); /* I4c: through the program cache (a direct call left later batches on this program) */

	gl3state.uniCommonData.color = HMM_Vec4(1.0f, 1.0f, 1.0f, 1.0f);
	GL3_UpdateUBOCommon();
	GL3_BindVAO(gl3state.vao3D);
	GL3_BindVBO(gl3state.vbo3D);

	curr_vtx = 0;

	memset(vtx, 0, sizeof(vtx));
	for (i = 0; i < (sof ? (size_t)numoutlinesurfs : (size_t)gl3_worldmodel->numsurfaces); i++)
	{
		const glpoly_t *p;

		surf = sof ? outlinesurfs[i] : gl3_worldmodel->surfaces + i;

		if (!sof && (surf->visframe != gl3_framecount))
		{
			continue;
		}

		for (p = surf->polys; p != NULL; p = p->chain)
		{
			size_t j;

			for (j = 2; j < p->numverts; j++)
			{
				size_t k;

				if (curr_vtx > (LINE_VTX_COUNT - 6))
				{
					glBufferData(GL_ARRAY_BUFFER, sizeof(vtx), vtx, GL_STREAM_DRAW);
					glDrawArrays(GL_LINES, 0, curr_vtx);
					curr_vtx = 0;
					memset(vtx, 0, sizeof(vtx));
				}

				for (k = 0; k < 3; k++)
				{
					vtx[curr_vtx + 0].pos[k] = p->vertices[0].pos[k];
					vtx[curr_vtx + 1].pos[k] = p->vertices[j - 1].pos[k];

					vtx[curr_vtx + 2].pos[k] = p->vertices[j - 1].pos[k];
					vtx[curr_vtx + 3].pos[k] = p->vertices[j].pos[k];

					vtx[curr_vtx + 4].pos[k] = p->vertices[j].pos[k];
					vtx[curr_vtx + 5].pos[k] = p->vertices[0].pos[k];
				}

				curr_vtx += 6;
			}
		}
	}

	if (curr_vtx)
	{
		glBufferData(GL_ARRAY_BUFFER, sizeof(vtx), vtx, GL_STREAM_DRAW);
		glDrawArrays(GL_LINES, 0, curr_vtx);
	}

	glEnable(GL_DEPTH_TEST);
}

static void
RenderBrushPoly(entity_t *currententity, msurface_t *fa, gl3drawCmd_t drawCmd)
{
	gl3image_t *image;

	c_brush_polys++;

	image = GL3_SoF_TextureAnimation(fa->texinfo, currententity ? currententity->frame : 0);
	drawCmd.texnum = image->texnum;

	if (fa->flags & SURF_DRAWTURB)
	{
		if (gl3_worldmodel && (gl3_worldmodel->bspversion == BSPVERSION_SOF))
		{
			/* I4c: SoF liquids (SPEC-410 Liquids), opaque in this pass */
			GL3_Liquid_Draw(fa, currententity, false, (drawCmd.flags & DCFlag_UseFog) ? drawCmd.fogregion : -1);
			return;
		}

		GL3_EmitWaterPolys(fa, drawCmd);
		return;
	}


	drawCmd.lmtexnum = fa->lightmaptexturenum;

	// Any dynamic lights on this surface?
	// TODO: maybe put lightstyles into a uniform (it's just 256 vec4)
	//       and put fa->styles[] into the 3d draw vertex?
	memcpy(drawCmd.styles, fa->styles, sizeof(fa->styles));
	drawCmd.flags |= DCFlag_UseLmStyles;

	if (fa->texinfo->flags & SURF_FLOWING)
	{
		GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3DlmFlow);
		GL3_DrawGLFlowingPoly(fa, drawCmd);
	}
	else
	{
		GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3Dlm);
		GL3_DrawGLPoly(fa, drawCmd);
	}

	// Note: lightmap chains are gone, lightmaps are rendered together with normal texture in one pass
}

/*
 * Draw water surfaces and windows.
 * The BSP tree is walked front to back, so unwinding the chain
 * of alpha_surfaces will draw back to front, giving proper ordering.
 */
void
GL3_DrawAlphaSurfaces(void)
{
	msurface_t *s;

	/* go back to the world matrix */
	gl3drawCmd_t drawCmd = GL3_CreateDrawCmd();
	drawCmd.flags |= DCFlag_Blend;

	for (s = gl3_alpha_surfaces; s != NULL; s = s->texturechain)
	{
		drawCmd.texnum = s->texinfo->image->texnum;
		c_brush_polys++;
		if (s->texinfo->flags & SURF_TRANS33)
		{
			drawCmd.alpha = 0.333f;
		}
		else if (s->texinfo->flags & SURF_TRANS66)
		{
			drawCmd.alpha = 0.666f;
		}
		else
		{
			drawCmd.alpha = 1.0f;
		}

		if (s->flags & SURF_DRAWTURB)
		{
			GL3_EmitWaterPolys(s, drawCmd);
		}
		else if (s->lightmaptexturenum >= 0)
		{
			/* OpenMerc: translucent surface with a lightmap (SoF) */
			gl3drawCmd_t lmCmd = drawCmd;

			lmCmd.lmtexnum = s->lightmaptexturenum;
			memcpy(lmCmd.styles, s->styles, sizeof(s->styles));
			lmCmd.flags |= DCFlag_UseLmStyles;
			SetLightFlags(s);
			GL3_SetDrawCmdShader(&lmCmd, &gl3state.si3DlmTrans);
			GL3_DrawGLPoly(s, lmCmd);
		}
		else if (s->texinfo->flags & SURF_FLOWING)
		{
			GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3DtransFlow);
			GL3_DrawGLFlowingPoly(s, drawCmd);
		}
		else
		{
			GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3Dtrans);
			GL3_DrawGLPoly(s, drawCmd);
		}
	}

	gl3_alpha_surfaces = NULL;
}

static void
DrawTextureChains(entity_t *currententity)
{
	int i;
	msurface_t *s;
	gl3image_t *image;

	c_visible_textures = 0;
	numoutlinesurfs = 0;

	/* I4c, SPEC-AMEND-006 I4c.3: the two-pass world path fills the chains the outlines use */
	qboolean outlines = gl_showtris->value && (gl3_worldmodel->bspversion == BSPVERSION_SOF) &&
		((r_fullbright->value != 0) || (r_newrefdef.rdflags & RDF_IRGOGGLES));

	gl3drawCmd_t drawCmd = GL3_CreateDrawCmd();

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (!image->registration_sequence)
		{
			continue;
		}

		s = image->texturechain;

		if (!s)
		{
			continue;
		}

		c_visible_textures++;

		for ( ; s; s = s->texturechain)
		{
			gl3drawCmd_t cmd = drawCmd;

			/* I4c: gl_fogmode 1, interior faces of a fog region get linear fog while drawn */
			cmd.fogregion = GL3_Fog_HardwareRegion(s);

			if (cmd.fogregion >= 0)
			{
				cmd.flags |= DCFlag_UseFog;
			}

			SetLightFlags(s);
			RenderBrushPoly(currententity, s, cmd);

			/* I4c, SPEC-AMEND-006 I4c.2: region faces of the chains are queued for the fog overlay as the chains
			   are drawn, after the boundary and sky faces queued by the walk */
			if (gl3_worldmodel->bspversion == BSPVERSION_SOF)
			{
				GL3_Fog_QueueSurface(s, NULL);
			}

			if (!(s->flags & SURF_DRAWTURB))
			{
				GL3_Detail_Queue(s, image, NULL);

				if (outlines && (numoutlinesurfs < MAX_OUTLINE_SURFS))
				{
					outlinesurfs[numoutlinesurfs++] = s;
				}
			}
		}

		image->texturechain = NULL;
	}

	// TODO: maybe one loop for normal faces and one for SURF_DRAWTURB ???
}

static void
RenderLightmappedPoly(entity_t *currententity, msurface_t *surf, gl3drawCmd_t drawCmd)
{
	gl3image_t *image = GL3_SoF_TextureAnimation(surf->texinfo, currententity ? currententity->frame : 0);



	// Any dynamic lights on this surface?
	memcpy(drawCmd.styles, surf->styles, sizeof(surf->styles));
	drawCmd.flags |= DCFlag_UseLmStyles;

	c_brush_polys++;

	drawCmd.texnum = image->texnum;
	drawCmd.lmtexnum = surf->lightmaptexturenum;

	if (surf->texinfo->flags & SURF_FLOWING)
	{
		GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3DlmFlow);
		GL3_DrawGLFlowingPoly(surf, drawCmd);
	}
	else
	{
		GL3_SetDrawCmdShader(&drawCmd, &gl3state.si3Dlm);
		GL3_DrawGLPoly(surf, drawCmd);
	}
}

/* OpenMerc: 0x800 faces of an inline model, drawn after its opaque faces with one light colour */
static void
DrawInlineAlphaFaces(entity_t *currententity, gl3model_t *currentmodel, msurface_t **faces, int numfaces,
	int modelregion)
{
	gl3fxstate_t st;
	vec3_t centre, light;
	vec3_t fwd, right, up, angles;
	int i, j;

	if (numfaces == 0)
	{
		return;
	}

	/* light sampled at the model's bounding-box centre (SPEC-410, UNVERIFIED point) */
	for (j = 0; j < 3; j++)
	{
		centre[j] = currententity->origin[j] + (currentmodel->mins[j] + currentmodel->maxs[j]) * 0.5f;
	}

	GL3_SoF_LightPoint(centre, light, 255);

	/* model space to world: yaw, -pitch, -roll (SPEC-411 section 8) */
	angles[0] = -currententity->angles[0];
	angles[1] = currententity->angles[1];
	angles[2] = -currententity->angles[2];
	AngleVectors(angles, fwd, right, up);

	GL3_Draw3DBatchesNow();
	GL3_FX_DefaultState(&st);
	st.blend = 1;
	st.alphatest = true;

	for (i = 0; i < numfaces; i++)
	{
		msurface_t *surf = faces[i];
		gl3image_t *image = GL3_SoF_TextureAnimation(surf->texinfo, currententity->frame);
		glpoly_t *p;

		c_brush_polys++; /* I4c: r_speeds 1 "wp" counts inline-model alpha faces (SPEC-AMEND-006 I4c.4) */

		gl3fxstate_t fst = st;

		fst.texnum = image->texnum;
		/* I4c: gl_fogmode 1 linear fog of the model's region */
		GL3_Fog_SetHardwareFxState(GL3_Fog_HardwareModelRegion(surf, modelregion), &fst);
		GL3_FX_SetState(&fst);

		for (p = surf->polys; p; p = p->chain)
		{
			gl3fxvtx_t v[64];
			int n = (p->numverts > 64) ? 64 : p->numverts;

			for (j = 0; j < n; j++)
			{
				const float *lp = p->vertices[j].pos;
				int k;

				for (k = 0; k < 3; k++)
				{
					v[j].pos[k] = currententity->origin[k] + lp[0] * fwd[k] - lp[1] * right[k] + lp[2] * up[k];
				}

				v[j].st[0] = p->vertices[j].texCoord[0];
				v[j].st[1] = p->vertices[j].texCoord[1];
				v[j].rgba[0] = light[0];
				v[j].rgba[1] = light[1];
				v[j].rgba[2] = light[2];
				v[j].rgba[3] = 1;
			}

			GL3_FX_Add(v, n, GL_TRIANGLE_FAN);
		}
	}

	GL3_FX_Flush();
}

static void
DrawInlineBModel(entity_t *currententity, gl3model_t *currentmodel, gl3drawCmd_t drawCmd)
{
	int i, k;
	cplane_t *pplane;
	float dot;
	msurface_t *psurf;
	dlight_t *lt;
	qboolean sof = gl3_worldmodel && (gl3_worldmodel->bspversion == BSPVERSION_SOF);
	static msurface_t *alphafaces[1024];
	int numalphafaces = 0;
	int fogregion = -1;

	if (sof)
	{
		/* the model's fog region: the leaf at its bounding-box centre plus origin */
		vec3_t centre;
		int j;

		for (j = 0; j < 3; j++)
		{
			centre[j] = currententity->origin[j] + (currentmodel->mins[j] + currentmodel->maxs[j]) * 0.5f;
		}

		fogregion = GL3_Fog_RegionOfPoint(centre);
	}

	/* calculate dynamic lighting for bmodel */
	lt = r_newrefdef.dlights;

	for (k = 0; k < r_newrefdef.num_dlights; k++, lt++)
	{
		R_MarkLights(lt, 1 << k, currentmodel->nodes + currentmodel->firstnode,
			r_dlightframecount, GL3_MarkSurfaceLights);
	}

	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	if (!sof && (currententity->flags & RF_TRANSLUCENT))
	{
		drawCmd.flags |= DCFlag_Blend;
	}

	/* draw texture */
	for (i = 0; i < currentmodel->nummodelsurfaces; i++, psurf++)
	{
		/* find which side of the node we are on */
		pplane = psurf->plane;

		dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		/* draw the polygon */
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (sof)
			{
				/* OpenMerc: SoF inline models (SPEC-410 Inline brush models) */
				int flags = psurf->texinfo->flags;

				if (flags & (SOFSURF_FOGBOUNDARY | SOFSURF_SKY))
				{
					continue;
				}

				if (fogregion >= 0)
				{
					GL3_Fog_QueueModelSurface(psurf, fogregion, currententity->origin, currententity->angles);
				}

				if (flags & SOFSURF_ALPHA)
				{
					if (numalphafaces < 1024)
					{
						alphafaces[numalphafaces++] = psurf;
					}

					continue;
				}

				{
					/* I4c: gl_fogmode 1 fog of the model's region while drawn */
					gl3drawCmd_t cmd = drawCmd;

					cmd.fogregion = GL3_Fog_HardwareModelRegion(psurf, fogregion);

					if (cmd.fogregion >= 0)
					{
						cmd.flags |= DCFlag_UseFog;
					}

					if (psurf->flags & SURF_DRAWTURB)
					{
						RenderBrushPoly(currententity, psurf, cmd);
					}
					else
					{
						SetAllLightFlags(psurf);
						RenderLightmappedPoly(currententity, psurf, cmd);

						/* I4c: the detail/environment pass also applies to inline models (SPEC-410) */
						GL3_Detail_Queue(psurf, GL3_SoF_TextureAnimation(psurf->texinfo, currententity->frame),
							currententity);
					}
				}

				continue;
			}

			if (psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
			{
				/* add to the translucent chain */
				psurf->texturechain = gl3_alpha_surfaces;
				gl3_alpha_surfaces = psurf;
			}
			else if(!(psurf->flags & SURF_DRAWTURB))
			{
				SetAllLightFlags(psurf);
				RenderLightmappedPoly(currententity, psurf, drawCmd);
			}
			else
			{
				RenderBrushPoly(currententity, psurf, drawCmd);
			}
		}
	}

	if (sof)
	{
		GL3_Draw3DBatchesNow();
		DrawInlineAlphaFaces(currententity, currentmodel, alphafaces, numalphafaces, fogregion);

		/* I4c: opaque faces, then 0x800 faces, then the detail pass (SPEC-410 Inline brush models) */
		GL3_Detail_DrawPass();
	}
}

void
GL3_DrawBrushModel(entity_t *e, gl3model_t *currentmodel)
{
	vec3_t mins, maxs;
	int i;
	qboolean rotated;

	if (currentmodel->nummodelsurfaces == 0)
	{
		return;
	}

	gl3drawCmd_t drawCmd = GL3_CreateDrawCmd();
	if(e->flags & RF_TRANSLUCENT)
		drawCmd.flags |= DCFlag_DisableDepthMask;


	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;

		for (i = 0; i < 3; i++)
		{
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd(e->origin, currentmodel->mins, mins);
		VectorAdd(e->origin, currentmodel->maxs, maxs);
	}

	if (!r_nocull->value && R_CullBox(mins, maxs, frustum))
	{
		return;
	}

	if (gl_zfix->value)
	{
		drawCmd.flags |= DCFlag_PolyOffsetFill;
	}

	VectorSubtract(r_newrefdef.vieworg, e->origin, modelorg);

	if (rotated)
	{
		vec3_t temp;
		vec3_t forward, right, up;

		VectorCopy(modelorg, temp);

		if (gl3_sofframe.active)
		{
			/* SPEC-411 section 8 step 4: angle vectors of (-pitch, yaw, -roll) */
			vec3_t a = {-e->angles[0], e->angles[1], -e->angles[2]};

			AngleVectors(a, forward, right, up);
		}
		else
		{
			AngleVectors(e->angles, forward, right, up);
		}
		modelorg[0] = DotProduct(temp, forward);
		modelorg[1] = -DotProduct(temp, right);
		modelorg[2] = DotProduct(temp, up);
	}

	//glPushMatrix();

	if (gl3_sofframe.active)
	{
		/* OpenMerc: SoF brush models turn by yaw, -pitch, -roll (SPEC-411 section 8) */
		GL3_RotateForEntity(e, &drawCmd);
	}
	else
	{
		e->angles[0] = -e->angles[0];
		e->angles[2] = -e->angles[2];
		GL3_RotateForEntity(e, &drawCmd);
		e->angles[0] = -e->angles[0];
		e->angles[2] = -e->angles[2];
	}

	DrawInlineBModel(e, currentmodel, drawCmd);

	// glPopMatrix();

}

static void
RecursiveWorldNode(entity_t *currententity, mnode_t *node)
{
	int c, side, sidebit;
	cplane_t *plane;
	msurface_t *surf, **mark;
	mleaf_t *pleaf;
	float dot;
	gl3image_t *image;

	if (node->contents == CONTENTS_SOLID)
	{
		return; /* solid */
	}

	if (node->visframe != gl3_visframecount)
	{
		return;
	}

	if (!r_nocull->value && R_CullBox(node->minmaxs, node->minmaxs + 3, frustum))
	{
		return;
	}

	/* if a leaf node, draw stuff */
	if (node->contents != CONTENTS_NODE)
	{
		pleaf = (mleaf_t *)node;

		/* check for door connected areas */
		// check for door connected areas
		if (!R_AreaVisible(r_newrefdef.areabits, pleaf))
			return;	// not visible

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = gl3_framecount;
				mark++;
			}
			while (--c);
		}

		return;
	}

	/* node is just a decision point, so go down the apropriate
	   sides find which side of the node we are on */
	plane = node->plane;

	switch (plane->type)
	{
		case PLANE_X:
			dot = modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			dot = modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			dot = modelorg[2] - plane->dist;
			break;
		default:
			dot = DotProduct(modelorg, plane->normal) - plane->dist;
			break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

	/* recurse down the children, front side first */
	RecursiveWorldNode(currententity, node->children[side]);

	/* draw stuff */
	for (c = node->numsurfaces,
		 surf = gl3_worldmodel->surfaces + node->firstsurface;
		 c; c--, surf++)
	{
		if (surf->visframe != gl3_framecount)
		{
			continue;
		}

		if ((surf->flags & SURF_PLANEBACK) != sidebit)
		{
			continue; /* wrong side */
		}

		if (gl3_worldmodel->bspversion == BSPVERSION_SOF)
		{
			/* OpenMerc: SPEC-410 Surface classification during the walk */
			int flags = surf->texinfo->flags;
			qboolean fog = (GL3_Fog_SurfaceRegion(surf) >= 0);

			if (flags & SOFSURF_FOGBOUNDARY)
			{
				GL3_Fog_QueueSurface(surf, NULL);
				GL3_Fog_MarkBoundaryPlane(surf);
			}
			else if (flags & SOFSURF_SKY)
			{
				GL3_AddSkySurface(surf);

				if (fog)
				{
					GL3_Fog_QueueSurface(surf, NULL);
				}
			}
			else if (flags & SOFSURF_ALPHA)
			{
				/* I4c: queued for the first overlay pass by the walk (SPEC-410 classification step 3) and drawn
				   again right after the face in the sorted pass (GL3_Fog_DrawSurfaceNow) */
				GL3_FX_AddAlphaSurface(surf, NULL);

				if (fog)
				{
					GL3_Fog_QueueSurface(surf, NULL);
				}
			}
			else
			{
				image = GL3_SoF_TextureAnimation(surf->texinfo, currententity->frame);

				/* an image with alpha on a surface without the alpha flag is not drawn */
				if (image->has_alpha && (image->softype != SOFIMG_INTERNAL))
				{
					continue;
				}

				surf->texturechain = image->texturechain;
				image->texturechain = surf;
				/* its fog overlay is queued when the chain is drawn (SPEC-AMEND-006 I4c.2: queue order) */
			}

			continue;
		}

		if (surf->texinfo->flags & SURF_SKY)
		{
			/* just adds to visible sky bounds */
			GL3_AddSkySurface(surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		{
			/* add to the translucent chain */
			surf->texturechain = gl3_alpha_surfaces;
			gl3_alpha_surfaces = surf;
			gl3_alpha_surfaces->texinfo->image = R_TextureAnimation(currententity, surf->texinfo);
		}
		else
		{
			/* the polygon is visible, so add it to the texture sorted chain */
			image = R_TextureAnimation(currententity, surf->texinfo);
			surf->texturechain = image->texturechain;
			image->texturechain = surf;
		}
	}

	/* recurse down the back side */
	RecursiveWorldNode(currententity, node->children[!side]);
}

void
GL3_DrawWorld(void)
{
	entity_t ent;

	if (!r_drawworld->value)
	{
		return;
	}

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		return;
	}

	VectorCopy(r_newrefdef.vieworg, modelorg);

	/* auto cycle the world frame for texture animation */
	memset(&ent, 0, sizeof(ent));
	ent.frame = (int)(GL3_GameTime() * 2); /* OpenMerc: SoF animates by the game clock */

	gl3state.currenttexture = -1;

	GL3_ClearSkyBox();
	RecursiveWorldNode(&ent, gl3_worldmodel->nodes);
	DrawTextureChains(&ent);
	GL3_Draw3DBatchesNow();
	GL3_Detail_DrawPass();
	GL3_DrawSkyBox();
	GL3_Terrain_Draw(); /* I4c: SPEC-410 Terrain sets, after the sky box, before the outlines */
	DrawTriangleOutlines();

	// make sure all this is drawed
	GL3_Draw3DBatchesNow();
}

/*
 * Mark the leaves and nodes that are
 * in the PVS for the current cluster
 */
void
GL3_MarkLeaves(void)
{
	const byte *vis;
	YQ2_ALIGNAS_TYPE(int) byte fatvis[MAX_MAP_LEAFS / 8];
	mnode_t *node;
	int i, c;
	mleaf_t *leaf;
	int cluster;

	if ((gl3_oldviewcluster == gl3_viewcluster) &&
		(gl3_oldviewcluster2 == gl3_viewcluster2) &&
		!r_novis->value &&
		(gl3_viewcluster != -1))
	{
		return;
	}

	/* development aid to let you run around
	   and see exactly where the pvs ends */
	if (r_lockpvs->value)
	{
		return;
	}

	gl3_visframecount++;
	gl3_oldviewcluster = gl3_viewcluster;
	gl3_oldviewcluster2 = gl3_viewcluster2;

	if (r_novis->value || (gl3_viewcluster == -1) || !gl3_worldmodel->vis)
	{
		/* mark everything */
		for (i = 0; i < gl3_worldmodel->numleafs; i++)
		{
			gl3_worldmodel->leafs[i].visframe = gl3_visframecount;
		}

		for (i = 0; i < gl3_worldmodel->numnodes; i++)
		{
			gl3_worldmodel->nodes[i].visframe = gl3_visframecount;
		}

		return;
	}

	vis = GL3_Mod_ClusterPVS(gl3_viewcluster, gl3_worldmodel);

	/* may have to combine two clusters because of solid water boundaries */
	if (gl3_viewcluster2 != gl3_viewcluster)
	{
		memcpy(fatvis, vis, (gl3_worldmodel->numleafs + 7) / 8);
		vis = GL3_Mod_ClusterPVS(gl3_viewcluster2, gl3_worldmodel);
		c = (gl3_worldmodel->numleafs + 31) / 32;

		for (i = 0; i < c; i++)
		{
			((int *)fatvis)[i] |= ((int *)vis)[i];
		}

		vis = fatvis;
	}

	for (i = 0, leaf = gl3_worldmodel->leafs;
		 i < gl3_worldmodel->numleafs;
		 i++, leaf++)
	{
		cluster = leaf->cluster;

		if (cluster == -1)
		{
			continue;
		}

		if (vis[cluster >> 3] & (1 << (cluster & 7)))
		{
			node = (mnode_t *)leaf;

			do
			{
				if (node->visframe == gl3_visframecount)
				{
					break;
				}

				node->visframe = gl3_visframecount;
				node = node->parent;
			}
			while (node);
		}
	}
}

