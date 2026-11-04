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
static cvar_t *r_om_seams, *r_om_seams_width; /* GFX-modern: soft texture seams (SeamDraw) */

gl3lightmapstate_t gl3_lms;

#define BACKFACE_EPSILON 0.01

extern gl3image_t gl3textures[MAX_GL3TEXTURES];
extern int numgl3textures;

void GL3_SurfInit(void)
{
	r_om_seams = ri.Cvar_Get("r_om_seams", "1", CVAR_ARCHIVE);
	r_om_seams_width = ri.Cvar_Get("r_om_seams_width", "12", CVAR_ARCHIVE);

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
	if (drawCmd.emissive == 0.0f)
	{
		drawCmd.emissive = GL3_Mat_SurfClass(fa); /* GFX-modern: material class as a negative value (r_om_ssr) */
	}
	if (drawCmd.emissive == -11.0f)
	{
		GL3_Mirror_Note(fa); /* GFX-modern r_om_mirror: a candidate plane for the next frame */
	}
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
	if (drawCmd.emissive == 0.0f)
	{
		drawCmd.emissive = GL3_Mat_SurfClass(fa); /* GFX-modern: material class as a negative value (r_om_ssr) */
	}
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

/*
 * OpenMerc GFX-modern r_om_seams: soft transitions between different textures on one plane (tiles next to
 * concrete). Such a seam is always an edge two coplanar faces share (the BSP keeps one texture per face). Built once
 * per map: for each such edge a strip on either face, the face clipped to r_om_seams_width units (at most 40 % of
 * the face's depth from the seam, so thin trims stay themselves) between perpendicular caps at the edge's ends.
 * Drawn after the opaque world, before the detail pass: the neighbour's texture with the face's own lightmap and
 * styles (si3DlmSeam), blended by the shader (half at the seam, so both sides meet; raised texels and noise break
 * the line), colour only. Only between surface materials (stone, ground, damp, smooth concrete, tiles); never
 * pictures, glass, mirrors, metal, wood, fabric, trims, hidden alpha images or animated textures; never between
 * variants of one texture family (1_tile, 1_tile_graf3: fitted by the artist) nor between two textures of one material class\n * (two shutter panels); only where the seam shows (the two
 * textures differ along it by more than a quarter of their brightness). r_om_seams 2 paints the strips red.
 */
typedef struct
{
	msurface_t *face;   /* the face the strip lies on: its lightmap, styles, visibility */
	gl3image_t *image;  /* the neighbour's texture */
	int firstvert, numverts;
	vec3_t p;           /* a point on the seam */
	vec3_t d;           /* into the face */
	float width;
} seamstrip_t;

#define SEAM_MAXPOLY 72

static seamstrip_t *seams;
static int numseams, capseams;
static gl3_3D_vtx_t *seamverts;
static int numseamverts, capseamverts;
static int *seamdrawn;       /* gl3_framecount when each world face was last drawn */
static const msurface_t *seamsurfaces;
static float seamwidth;

static qboolean
SeamFace(const msurface_t *s)
{
	static const char *const design[] = {"trim", "edge", "border", "sign", "logo", "stripe", "line", "decal",
		"poster", "pic", "window", "door", "metal", "rib", "pipe", "grate", "vent"};
	const char *name;
	int c, i;

	/* (an image with alpha on a face without the alpha flag is not drawn at all) */
	if (!s->texinfo || !s->texinfo->image || s->texinfo->image->has_alpha || !s->polys ||
		(s->lightmaptexturenum < 0) || s->texinfo->next ||
		(s->flags & (SURF_DRAWSKY | SURF_DRAWTURB)) ||
		(s->texinfo->flags & (SURF_SKY | SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_FLOWING | SURF_LIGHT |
			SURF_NODRAW | SOFSURF_ALPHA)))
	{
		return false;
	}

	c = (int)(-GL3_Mat_SurfClass(s) + 0.5f);

	if ((c != 1) && (c != 6) && (c != 12) && (c != 13) && (c != 15))
	{
		return false;
	}

	name = s->texinfo->image->name;

	for (i = 0; i < (int)(sizeof(design) / sizeof(design[0])); i++)
	{
		if (Q_strcasestr(name, design[i]))
		{
			return false;
		}
	}

	return true;
}

/* Sutherland-Hodgman: keep the part of the polygon with dot(p, n) <= dist */
static int
SeamClip(float in[][3], int n, float out[][3], const vec3_t nrm, float dist)
{
	int i, m = 0;

	for (i = 0; i < n; i++)
	{
		const float *a = in[i], *b = in[(i + 1) % n];
		float da = DotProduct(a, nrm) - dist, db = DotProduct(b, nrm) - dist;

		if ((da <= 0) && (m < SEAM_MAXPOLY))
		{
			VectorCopy(a, out[m]);
			m++;
		}

		if (((da < 0) != (db < 0)) && (fabsf(da - db) > 1e-6f) && (m < SEAM_MAXPOLY))
		{
			float f = da / (da - db);
			int k;

			for (k = 0; k < 3; k++)
			{
				out[m][k] = a[k] + (b[k] - a[k]) * f;
			}
			m++;
		}
	}

	return m;
}

/* the strip on face a showing the texture of face b across the stretch e0 e1 of their common border */
static void
SeamStrip(msurface_t *a, const msurface_t *b, const vec3_t e0, const vec3_t e1)
{
	static float pa[SEAM_MAXPOLY][3], pb[SEAM_MAXPOLY][3];
	const glpoly_t *poly = a->polys;
	const mtexinfo_t *tb = b->texinfo;
	vec3_t t, n, d, w2, c = {0, 0, 0};
	float len, maxd = 0, width, m[3][3] = {{0}}, rs[3] = {0}, rt[3] = {0}, cs[3], ct[3], det;
	int i, k, nv;

	VectorSubtract(e1, e0, t);
	len = VectorNormalize(t);

	if ((len < 1.0f) || (poly->numverts < 3) || (poly->numverts > SEAM_MAXPOLY - 4))
	{
		return;
	}

	VectorCopy(a->plane->normal, n);
	if (a->flags & SURF_PLANEBACK)
	{
		VectorNegate(n, n);
	}

	CrossProduct(n, t, d);
	VectorNormalize(d);

	for (i = 0; i < poly->numverts; i++)
	{
		VectorAdd(c, poly->vertices[i].pos, c);
	}
	VectorScale(c, 1.0f / poly->numverts, c);
	VectorSubtract(c, e0, w2);
	if (DotProduct(w2, d) < 0)
	{
		VectorNegate(d, d);
	}

	for (i = 0; i < poly->numverts; i++)
	{
		VectorSubtract(poly->vertices[i].pos, e0, w2);
		maxd = Q_max(maxd, DotProduct(w2, d));
		VectorCopy(poly->vertices[i].pos, pa[i]);
	}

	width = Q_min(seamwidth, maxd * 0.4f);

	if (width < 1.5f)
	{
		return;
	}

	/* the face within width of the seam, between the caps */
	nv = SeamClip(pa, poly->numverts, pb, d, DotProduct(e0, d) + width);
	VectorNegate(t, w2);
	nv = SeamClip(pb, nv, pa, w2, -DotProduct(e0, t));
	nv = SeamClip(pa, nv, pb, t, DotProduct(e1, t));

	if (nv < 3)
	{
		return;
	}

	/* the face's lightmap coordinates are affine in the plane: fitted to its vertices (least squares) */
	CrossProduct(n, t, w2);
	for (i = 0; i < poly->numverts; i++)
	{
		vec3_t r;
		float x[3];

		VectorSubtract(poly->vertices[i].pos, e0, r);
		x[0] = 1;
		x[1] = DotProduct(r, t);
		x[2] = DotProduct(r, w2);

		for (k = 0; k < 3; k++)
		{
			int j;

			for (j = 0; j < 3; j++)
			{
				m[k][j] += x[k] * x[j];
			}
			rs[k] += x[k] * poly->vertices[i].lmTexCoord[0];
			rt[k] += x[k] * poly->vertices[i].lmTexCoord[1];
		}
	}

	det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

	if (fabsf(det) < 1e-9f)
	{
		return;
	}

	for (k = 0; k < 3; k++)
	{
		/* Cramer: column k replaced by the right-hand side */
		float q[3][3], qs, qt;
		int r, j;

		for (r = 0; r < 3; r++)
		{
			for (j = 0; j < 3; j++)
			{
				q[r][j] = (j == k) ? rs[r] : m[r][j];
			}
		}
		qs = q[0][0] * (q[1][1] * q[2][2] - q[1][2] * q[2][1]) - q[0][1] * (q[1][0] * q[2][2] - q[1][2] * q[2][0]) +
			q[0][2] * (q[1][0] * q[2][1] - q[1][1] * q[2][0]);

		for (r = 0; r < 3; r++)
		{
			q[r][k] = rt[r];
		}
		qt = q[0][0] * (q[1][1] * q[2][2] - q[1][2] * q[2][1]) - q[0][1] * (q[1][0] * q[2][2] - q[1][2] * q[2][0]) +
			q[0][2] * (q[1][0] * q[2][1] - q[1][1] * q[2][0]);

		cs[k] = qs / det;
		ct[k] = qt / det;
	}

	if (numseams == capseams)
	{
		capseams = capseams ? capseams * 2 : 256;
		seams = realloc(seams, capseams * sizeof(*seams));
	}

	if (numseamverts + nv > capseamverts)
	{
		capseamverts = Q_max(capseamverts * 2, numseamverts + nv + 1024);
		seamverts = realloc(seamverts, capseamverts * sizeof(*seamverts));
	}

	if (!seams || !seamverts)
	{
		numseams = numseamverts = capseams = capseamverts = 0;
		return;
	}

	for (i = 0; i < nv; i++)
	{
		gl3_3D_vtx_t *v = &seamverts[numseamverts + i];
		vec3_t r;
		float u, vv;

		VectorCopy(pb[i], v->pos);
		v->texCoord[0] = (DotProduct(pb[i], tb->vecs[0]) + tb->vecs[0][3]) / tb->image->width;
		v->texCoord[1] = (DotProduct(pb[i], tb->vecs[1]) + tb->vecs[1][3]) / tb->image->height;
		VectorSubtract(pb[i], e0, r);
		u = DotProduct(r, t);
		vv = DotProduct(r, w2);
		v->lmTexCoord[0] = cs[0] + cs[1] * u + cs[2] * vv;
		v->lmTexCoord[1] = ct[0] + ct[1] * u + ct[2] * vv;
		VectorCopy(n, v->normal);
		v->lightFlags = 0;
	}

	seams[numseams].face = a;
	seams[numseams].image = tb->image;
	seams[numseams].firstvert = numseamverts;
	seams[numseams].numverts = nv;
	VectorCopy(e0, seams[numseams].p);
	VectorCopy(d, seams[numseams].d);
	seams[numseams].width = width;
	numseams++;
	numseamverts += nv;
}

/* the textures read back at a small mip level (64 texels across at most) while the strips are built */
typedef struct
{
	byte *rgba;
	int w, h;
	qboolean tried;
} seamtex_t;

static seamtex_t *seamtex;

static void
SeamTexel(gl3image_t *img, const mtexinfo_t *ti, const vec3_t p, float out[3])
{
	seamtex_t *st = &seamtex[img - gl3textures];
	float s, t;
	int x, y;
	const byte *c;

	if (!st->tried)
	{
		int level = 0, w = img->width, h = img->height;

		st->tried = true;
		while (((w >> level) > 64) && ((h >> level) > 64))
		{
			level++;
		}

		GL3_SelectTMU(GL_TEXTURE0);
		GL3_Bind(img->texnum);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &st->w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &st->h);

		if ((st->w > 0) && (st->h > 0) && (st->w <= 1024) && (st->h <= 1024))
		{
			st->rgba = malloc(st->w * st->h * 4);
			if (st->rgba)
			{
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, st->rgba);
				glPixelStorei(GL_PACK_ALIGNMENT, 4);
			}
		}
	}

	if (!st->rgba)
	{
		out[0] = out[1] = out[2] = -1;
		return;
	}

	s = (DotProduct(p, ti->vecs[0]) + ti->vecs[0][3]) / img->width;
	t = (DotProduct(p, ti->vecs[1]) + ti->vecs[1][3]) / img->height;
	s -= floorf(s);
	t -= floorf(t);
	x = Q_min((int)(s * st->w), st->w - 1);
	y = Q_min((int)(t * st->h), st->h - 1);
	c = st->rgba + 4 * (y * st->w + x);
	out[0] = c[0] / 255.0f;
	out[1] = c[1] / 255.0f;
	out[2] = c[2] / 255.0f;
}

/* one family of textures: the names (without the set) agree over 70 % of the shorter one (1_tile, 1_tile_graf3,
   1_tile_bottom; floor1, floor2) - variants the artist fitted to each other */
static qboolean
SeamFamily(const char *x, const char *y)
{
	const char *sx = strrchr(x, '/'), *sy = strrchr(y, '/');
	size_t lx, ly, k = 0;

	x = sx ? sx + 1 : x;
	y = sy ? sy + 1 : y;
	lx = strlen(x);
	ly = strlen(y);

	while (x[k] && y[k] && (tolower((unsigned char)x[k]) == tolower((unsigned char)y[k])))
	{
		k++;
	}

	if ((k >= 4) && (k * 10 >= Q_min(lx, ly) * 7))
	{
		return true;
	}

	/* the same stem before the last '_' (1_tile_damage2, 1_tile_graf3) */
	sx = strrchr(x, '_');
	sy = strrchr(y, '_');
	return sx && sy && (sx - x == sy - y) && (sx - x >= 3) && !Q_strncasecmp(x, y, (int)(sx - x));
}

/* does the seam show? The two textures sampled just either side of it, every 4 units: their colours differ there
   by more than a quarter of their brightness */
static qboolean
SeamVisible(const msurface_t *a, const msurface_t *b, const vec3_t e0, const vec3_t e1)
{
	vec3_t t, d;
	float len, diff = 0, lum = 0;
	int i, steps;

	VectorSubtract(e1, e0, t);
	len = VectorNormalize(t);

	if (len < 1.0f)
	{
		return false;
	}

	CrossProduct(a->plane->normal, t, d);
	VectorNormalize(d);

	/* d towards a (the side of a's first vertex off the edge) */
	for (i = 0; i < a->polys->numverts; i++)
	{
		vec3_t r;
		float k;

		VectorSubtract(a->polys->vertices[i].pos, e0, r);
		k = DotProduct(r, d);

		if (fabsf(k) > 0.5f)
		{
			if (k < 0)
			{
				VectorNegate(d, d);
			}
			break;
		}
	}

	steps = Q_clamp((int)(len / 4.0f), 2, 64);

	for (i = 0; i < steps; i++)
	{
		vec3_t p, pa, pb;
		float x[3], y[3];
		int k;

		VectorMA(e0, (i + 0.5f) * len / steps, t, p);
		VectorMA(p, 2.0f, d, pa);
		VectorMA(p, -2.0f, d, pb);
		SeamTexel(a->texinfo->image, a->texinfo, pa, x);
		SeamTexel(b->texinfo->image, b->texinfo, pb, y);

		if ((x[0] < 0) || (y[0] < 0))
		{
			return false;
		}

		for (k = 0; k < 3; k++)
		{
			diff += fabsf(x[k] - y[k]) / 3.0f;
			lum += (x[k] + y[k]) / 6.0f;
		}
	}

	return (diff / steps) > 0.25f * Q_max(lum / steps, 0.08f);
}

/* one texture on both faces, but its placement jumps along the common border (another offset, a mirrored axis:
   the artist fitted each face on its own). Invisible on the blurry retail art, a hard line through the bricks of
   an HD replacement. Texture coordinates in retail texels, compared modulo the tile at five points */
static qboolean
SeamMapJump(const msurface_t *a, const msurface_t *b, const vec3_t e0, const vec3_t e1)
{
	const mtexinfo_t *ta = a->texinfo, *tb = b->texinfo;
	int i, k;

	for (i = 0; i < 5; i++)
	{
		vec3_t p;

		VectorLerp(e0, e1, i / 4.0f, p);

		for (k = 0; k < 2; k++)
		{
			float size = k ? ta->image->height : ta->image->width;
			float d = (DotProduct(p, ta->vecs[k]) + ta->vecs[k][3]) - (DotProduct(p, tb->vecs[k]) + tb->vecs[k][3]);

			d -= size * floorf(d / size + 0.5f);

			if (fabsf(d) > 1.0f)
			{
				return true;
			}
		}
	}

	return false;
}

/* one side of a world face, for matching the borders of coplanar faces */
typedef struct
{
	const cplane_t *plane;
	int back;
	int face;
	vec3_t a, b;
} seamedge_t;

static int
SeamEdgeCmp(const void *x, const void *y)
{
	const seamedge_t *p = x, *q = y;

	if (p->plane != q->plane)
	{
		return (p->plane < q->plane) ? -1 : 1;
	}

	return p->back - q->back;
}

static void
SeamBuild(void)
{
	gl3model_t *mod = gl3_worldmodel;
	/* the faces of the world itself (submodel 0), not of its doors and lifts */
	int first = mod->numsubmodels ? mod->submodels[0].firstmodelsurface : 0;
	int count = mod->numsubmodels ? mod->submodels[0].nummodelsurfaces : mod->numsurfaces;
	seamedge_t *ed;
	vec3_t d;
	int numed = 0, maxed = 0, i, j, g0, g1;

	numseams = numseamverts = 0;
	free(seamdrawn);
	seamdrawn = calloc(mod->numsurfaces, sizeof(int));
	seamsurfaces = mod->surfaces;
	seamtex = calloc(MAX_GL3TEXTURES, sizeof(*seamtex));

	for (i = first; i < first + count; i++)
	{
		if (SeamFace(&mod->surfaces[i]))
		{
			maxed += mod->surfaces[i].polys->numverts;
		}
	}

	ed = malloc((maxed + 1) * sizeof(*ed));

	if (!seamdrawn || !ed || !seamtex)
	{
		free(ed);
		free(seamtex);
		seamtex = NULL;
		return;
	}

	/* the sides of the faces as drawn. The BSP shares an edge between two faces only where their corners agree;
	   a face beside a split one (a T-junction, the usual case along a long seam) has its own edge there, so the
	   borders are matched by position: collinear sides of two faces on one plane that overlap */
	for (i = first; i < first + count; i++)
	{
		const msurface_t *s = &mod->surfaces[i];

		if (!SeamFace(s))
		{
			continue;
		}

		for (j = 0; j < s->polys->numverts; j++)
		{
			seamedge_t *e = &ed[numed];

			e->plane = s->plane;
			e->back = (s->flags & SURF_PLANEBACK) ? 1 : 0;
			e->face = i;
			VectorCopy(s->polys->vertices[j].pos, e->a);
			VectorCopy(s->polys->vertices[(j + 1) % s->polys->numverts].pos, e->b);
			VectorSubtract(e->b, e->a, d);

			if (DotProduct(d, d) >= 1.0f)
			{
				numed++;
			}
		}
	}

	qsort(ed, numed, sizeof(*ed), SeamEdgeCmp);

	for (g0 = 0; g0 < numed; g0 = g1)
	{
		for (g1 = g0 + 1; (g1 < numed) && !SeamEdgeCmp(&ed[g0], &ed[g1]); g1++)
		{
		}

		for (i = g0; i < g1; i++)
		{
			vec3_t t, off;
			float len;

			VectorSubtract(ed[i].b, ed[i].a, t);
			len = VectorNormalize(t);

			for (j = i + 1; j < g1; j++)
			{
				msurface_t *a = &mod->surfaces[ed[i].face], *b = &mod->surfaces[ed[j].face];
				vec3_t r0, r1, p0, p1;
				float s0, s1, lo, hi;

				qboolean same = (a->texinfo->image == b->texinfo->image);

				if ((ed[i].face == ed[j].face) || (same && (a->texinfo == b->texinfo)))
				{
					continue;
				}

				/* on the line of side i, within a tenth of a unit */
				VectorSubtract(ed[j].a, ed[i].a, r0);
				VectorSubtract(ed[j].b, ed[i].a, r1);
				s0 = DotProduct(r0, t);
				s1 = DotProduct(r1, t);
				VectorMA(r0, -s0, t, off);
				if (DotProduct(off, off) > 0.01f)
				{
					continue;
				}
				VectorMA(r1, -s1, t, off);
				if (DotProduct(off, off) > 0.01f)
				{
					continue;
				}

				lo = Q_max(0.0f, Q_min(s0, s1));
				hi = Q_min(len, Q_max(s0, s1));

				if (hi - lo < 1.0f)
				{
					continue;
				}

				VectorMA(ed[i].a, lo, t, p0);
				VectorMA(ed[i].a, hi, t, p1);

				if (same)
				{
					/* one texture: a seam only where its placement jumps */
					if (!SeamMapJump(a, b, p0, p1))
					{
						continue;
					}
				}
				/* only a seam that shows: the variants of one surface (dirty tiles, tiles with graffiti, a frieze)
				   were fitted to each other at their seams by the artist */
				else if (SeamFamily(a->texinfo->image->name, b->texinfo->image->name) ||
					((int)(-GL3_Mat_SurfClass(a) + 0.5f) == (int)(-GL3_Mat_SurfClass(b) + 0.5f)) ||
					!SeamVisible(a, b, p0, p1))
				{
					continue;
				}

				SeamStrip(a, b, p0, p1);
				SeamStrip(b, a, p0, p1);
			}
		}
	}

	free(ed);
	for (i = 0; i < MAX_GL3TEXTURES; i++)
	{
		free(seamtex[i].rgba);
	}
	free(seamtex);
	seamtex = NULL;
	R_Printf(PRINT_DEVELOPER, "r_om_seams: %d strips\n", numseams);
}

/* after the opaque world: the strips of the faces drawn this frame, within 2500 units */
static void
SeamDraw(void)
{
	gl3ShaderInfo_t *si = &gl3state.si3DlmSeam;
	GLint locp, locd, locs, loct;
	float w;
	int i;

	if (!r_om_seams || !r_om_seams->value || !si->shaderProgram || !gl3_worldmodel ||
		(gl3_worldmodel->bspversion != BSPVERSION_SOF))
	{
		return;
	}

	w = Q_clamp(r_om_seams_width->value, 2.0f, 64.0f);

	if ((seamsurfaces != gl3_worldmodel->surfaces) || (seamwidth != w))
	{
		seamwidth = w;
		SeamBuild();
	}

	if (!numseams)
	{
		return;
	}

	GL3_Draw3DBatchesNow();
	GL3_UseProgram(si->shaderProgram);
	locp = glGetUniformLocation(si->shaderProgram, "omSeamP");
	locd = glGetUniformLocation(si->shaderProgram, "omSeamD");
	locs = glGetUniformLocation(si->shaderProgram, "omSeamS");
	loct = glGetUniformLocation(si->shaderProgram, "omSeamT");
	glUniform1i(glGetUniformLocation(si->shaderProgram, "omSeam"), (r_om_seams->value >= 2) ? 2 : 1);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPolygonOffset(-1.0f, -2.0f);
	for (i = 1; i < 4; i++)
	{
		glColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); /* colour only: the face keeps its material */
	}

	for (i = 0; i < numseams; i++)
	{
		seamstrip_t *s = &seams[i];
		msurface_t *f = s->face;
		gl3drawCmd_t cmd;
		vec3_t r;
		int k;

		if (seamdrawn[f - gl3_worldmodel->surfaces] != gl3_framecount)
		{
			continue;
		}

		VectorSubtract(s->p, r_newrefdef.vieworg, r);
		if (DotProduct(r, r) > 2500.0f * 2500.0f)
		{
			continue;
		}

		GL3_UseProgram(si->shaderProgram);
		glUniform4f(locp, s->p[0], s->p[1], s->p[2], s->width);
		glUniform4f(locd, s->d[0], s->d[1], s->d[2], 0.0f);
		{
			/* the face's own texture for the height blend: its texture axes, its image on unit 10 */
			const mtexinfo_t *ti = f->texinfo;
			float iw = 1.0f / ti->image->width, ih = 1.0f / ti->image->height;

			glUniform4f(locs, ti->vecs[0][0] * iw, ti->vecs[0][1] * iw, ti->vecs[0][2] * iw, ti->vecs[0][3] * iw);
			glUniform4f(loct, ti->vecs[1][0] * ih, ti->vecs[1][1] * ih, ti->vecs[1][2] * ih, ti->vecs[1][3] * ih);
			GL3_SelectTMU(GL_TEXTURE10);
			glBindTexture(GL_TEXTURE_2D, ti->image->texnum);
			GL3_SelectTMU(GL_TEXTURE0);
		}

		cmd = GL3_CreateDrawCmd();
		memcpy(cmd.styles, f->styles, sizeof(f->styles));
		cmd.flags |= DCFlag_UseLmStyles | DCFlag_Blend | DCFlag_DisableDepthMask | DCFlag_PolyOffsetFill;
		cmd.texnum = s->image->texnum;
		cmd.lmtexnum = f->lightmaptexturenum;
		cmd.emissive = GL3_Mat_SurfClass(f);
		cmd.fogregion = GL3_Fog_HardwareRegion(f);
		if (cmd.fogregion >= 0)
		{
			cmd.flags |= DCFlag_UseFog;
		}
		GL3_SetDrawCmdShader(&cmd, si);

		for (k = 0; k < s->numverts; k++)
		{
			seamverts[s->firstvert + k].lightFlags = f->polys->vertices[0].lightFlags;
		}

		GL3_Add3DdrawCmdToBatch(&seamverts[s->firstvert], s->numverts, GL_TRIANGLE_FAN, cmd);
		GL3_Draw3DBatchesNow();
	}

	for (i = 1; i < 4; i++)
	{
		glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
	glPolygonOffset(0.0f, 0.0f);
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

			/* GFX-modern r_om_seams: the strips of this face are drawn after the chains */
			if (seamdrawn && (seamsurfaces == gl3_worldmodel->surfaces))
			{
				seamdrawn[s - gl3_worldmodel->surfaces] = gl3_framecount;
			}

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

	/* GFX-modern r_om_fogcull: the whole model lies in the opaque fog of the eye's region */
	if (GL3_Fog_CullModel(currentmodel, mins, maxs))
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

			/* GFX-modern r_om_fogcull: hidden in the opaque fog of the eye's region, with its overlay */
			if (GL3_Fog_CullSurface(surf))
			{
				continue;
			}
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
	SeamDraw(); /* GFX-modern r_om_seams */
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
/* GFX-modern r_om_csm: some face of the sky lies in the view's PVS (set when the PVS is marked) - without it no
   outdoor place can be seen and the sun shadows are not drawn */
qboolean gl3_pvssky = true;

static qboolean
LeafHasSky(const mleaf_t *leaf)
{
	int c;

	for (c = 0; c < leaf->nummarksurfaces; c++)
	{
		if (leaf->firstmarksurface[c]->texinfo->flags & SOFSURF_SKY)
		{
			return true;
		}
	}

	return false;
}

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
	gl3_pvssky = true;

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

	gl3_pvssky = false;

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
			if (!gl3_pvssky && LeafHasSky(leaf))
			{
				gl3_pvssky = true;
			}

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

