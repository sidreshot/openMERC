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
 * Model loading and caching for OpenGL3. Includes the .bsp file format
 *
 * =======================================================================
 *
 * OpenMerc: loads Soldier of Fortune maps (IBSP version 46: 44-byte faces
 * with stored lightmap geometry and per-face lightmap scale, 32-byte leafs,
 * 22 lumps) besides Quake II maps (version 38); fog regions and world lights
 * of SoF maps (package I4a).
 * Implements: SPEC-101, SPEC-410
 */

#include "header/local.h"

enum { MAX_MOD_KNOWN = 512 };

YQ2_ALIGNAS_TYPE(int) static byte mod_novis[MAX_MAP_LEAFS / 8];
gl3model_t mod_known[MAX_MOD_KNOWN];
static int mod_numknown;
static int mod_max = 0;
int registration_sequence;

//===============================================================================

static qboolean
Mod_HasFreeSpace(void)
{
	int		i, used;
	gl3model_t	*mod;

	used = 0;

	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (mod->registration_sequence == registration_sequence)
		{
			used ++;
		}
	}

	if (mod_max < used)
	{
		mod_max = used;
	}

	// should same size of free slots as currently used
	return (mod_numknown + mod_max) < MAX_MOD_KNOWN;
}

const byte*
GL3_Mod_ClusterPVS(int cluster, const gl3model_t *model)
{
	if ((cluster == -1) || !model->vis)
	{
		return mod_novis;
	}

	return Mod_DecompressVis((byte *)model->vis +
			model->vis->bitofs[cluster][DVIS_PVS],
			(model->vis->numclusters + 7) >> 3);
}

void
GL3_Mod_Modellist_f(void)
{
	int i, total, used;
	gl3model_t *mod;
	qboolean freeup;

	total = 0;
	used = 0;
	Com_Printf("Loaded models:\n");

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		char *in_use = "";

		if (mod->registration_sequence == registration_sequence)
		{
			in_use = "*";
			used ++;
		}

		if (!mod->name[0])
		{
			continue;
		}

		Com_Printf("%8i : %s %s\n",
			mod->extradatasize, mod->name, in_use);
		total += mod->extradatasize;
	}

	Com_Printf("Total resident: %i\n", total);
	// update statistics
	freeup = Mod_HasFreeSpace();
	Com_Printf("Used %d of %d models%s.\n", used, mod_max, freeup ? ", has free space" : "");
}

void
GL3_Mod_Init(void)
{
	mod_max = 0;
	memset(mod_novis, 0xff, sizeof(mod_novis));
}

static void
Mod_LoadSubmodels(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dmodel_t *in;
	gl3model_t *out;
	int i, j, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		if (i == 0)
		{
			/* copy parent as template for first model */
			memcpy(out, loadmodel, sizeof(*out));
		}
		else
		{
			/* copy first as template for model */
			memmove(out, loadmodel->submodels, sizeof(*out));
		}

		Com_sprintf (out->name, sizeof(out->name), "*%d", i);

		for (j = 0; j < 3; j++)
		{
			/* spread the mins / maxs by a pixel */
			out->mins[j] = LittleFloat(in->mins[j]) - 1;
			out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
			out->origin[j] = LittleFloat(in->origin[j]);
		}

		out->radius = Mod_RadiusFromBounds(out->mins, out->maxs);
		out->firstnode = LittleLong(in->headnode);
		out->firstmodelsurface = LittleLong(in->firstface);
		out->nummodelsurfaces = LittleLong(in->numfaces);
		// visleafs
		out->numleafs = 0;
		//  check limits
		if (out->firstnode >= loadmodel->numnodes)
		{
			Com_Error(ERR_DROP, "%s: Inline model %i has bad firstnode",
					__func__, i);
		}
	}
}

/*
 * Fills in s->texturemins[] and s->extents[]
 */
static void
Mod_CalcSurfaceExtents(gl3model_t *loadmodel, msurface_t *s)
{
	float mins[2], maxs[2], val;
	int i, j, e;
	mvertex_t *v;
	mtexinfo_t *tex;
	int bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;

	for (i = 0; i < s->numedges; i++)
	{
		e = loadmodel->surfedges[s->firstedge + i];

		if (e >= 0)
		{
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		}
		else
		{
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		}

		for (j = 0; j < 2; j++)
		{
			val = v->position[0] * tex->vecs[j][0] +
				  v->position[1] * tex->vecs[j][1] +
				  v->position[2] * tex->vecs[j][2] +
				  tex->vecs[j][3];

			if (val < mins[j])
			{
				mins[j] = val;
			}

			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = floor(mins[i] / 16);
		bmaxs[i] = ceil(maxs[i] / 16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		/* Quake II: one lightmap sample per 16 texels */
		s->lmstep[i] = 16;
		s->lmsize[i] = (s->extents[i] >> 4) + 1;
	}
}

extern void
GL3_SubdivideSurface(msurface_t *fa, gl3model_t* loadmodel);

/* OpenMerc: face record size and accessors for both BSP versions */
static size_t
Mod_FaceSize(int bspversion)
{
	return (bspversion == BSPVERSION_SOF) ? sizeof(dface46_t) : sizeof(dface_t);
}

static int calcTexinfoAndFacesSize(byte *mod_base, const lump_t *fl, const lump_t *tl,
		int bspversion)
{
	byte* face_in = (void *)(mod_base + fl->fileofs);
	texinfo_t* texinfo_in = (void *)(mod_base + tl->fileofs);
	size_t face_size = Mod_FaceSize(bspversion);

	if (fl->filelen % face_size || tl->filelen % sizeof(*texinfo_in))
	{
		// will error out when actually loading it
		return 0;
	}

	int ret = 0;

	int face_count = fl->filelen / face_size;
	int texinfo_count = tl->filelen / sizeof(*texinfo_in);

	{
		// out = Hunk_Alloc(count * sizeof(*out));
		int baseSize = face_count * sizeof(msurface_t);
		baseSize = (baseSize + 31) & ~31;
		ret += baseSize;

		int ti_size = texinfo_count * sizeof(mtexinfo_t);
		ti_size = (ti_size + 31) & ~31;
		ret += ti_size;
	}

	int numWarpFaces = 0;

	for (int surfnum = 0; surfnum < face_count; surfnum++, face_in += face_size)
	{
		/* numedges and texinfo are at the same offsets in v38 and v46 */
		int numverts = LittleShort(((dface_t *)face_in)->numedges);
		int ti = LittleShort(((dface_t *)face_in)->texinfo);
		if ((ti < 0) || (ti >= texinfo_count))
		{
			return 0; // will error out
		}
		int texFlags = LittleLong(texinfo_in[ti].flags);

		/* set the drawing flags */
		if (texFlags & SURF_WARP)
		{
			if (numverts > 60)
				return 0; // will error out in R_SubdividePolygon()

			// GL3_SubdivideSurface(out, loadmodel); /* cut up polygon for warps */
			// for each (pot. recursive) call to R_SubdividePolygon():
			//   sizeof(glpoly_t) + ((numverts - 4) + 2) * sizeof(gl3_3D_vtx_t)

			// this is tricky, how much is allocated depends on the size of the surface
			// which we don't know (we'd need the vertices etc to know, but we can't load
			// those without allocating...)
			// so we just count warped faces and use a generous estimate below

			++numWarpFaces;
		}
		else
		{
			// GL3_LM_BuildPolygonFromSurface(out);
			// => poly = Hunk_Alloc(sizeof(glpoly_t) + (numverts - 4) * sizeof(gl3_3D_vtx_t));
			int polySize = sizeof(glpoly_t) + (numverts - 4) * sizeof(gl3_3D_vtx_t);
			polySize = (polySize + 31) & ~31;
			ret += polySize;
		}
	}

	// yeah, this is a bit hacky, but it looks like for each warped face
	// 256-55000 bytes are allocated (usually on the lower end),
	// so just assume 48k per face to be safe
	ret += numWarpFaces * 49152;
	ret += 5000000; // and 5MB extra just in case

	return ret;
}

/*
 * OpenMerc: IBSP v46 stores the lightmap geometry of each face
 * (SPEC-101, Face and Lightmaps). Returns false if the face has no
 * usable lightmap; the caller then falls back to the Quake II rules.
 */
static qboolean
Mod_SetSurfaceLightmap46(gl3model_t *loadmodel, msurface_t *out, const dface46_t *in)
{
	int i, numstyles, w, h, ofs;
	size_t size;

	w = in->lm_width;
	h = in->lm_height;

	if ((w == 0) || (h == 0))
	{
		return false; /* unlit face (sky, warp) */
	}

	for (i = 0; i < 2; i++)
	{
		int scale = in->lm_scale[i];

		if ((scale != 1) && (scale != 2))
		{
			Com_DPrintf("%s: face %d has lightmap scale %d\n", __func__,
				(int)(out - loadmodel->surfaces), scale);
			return false;
		}

		out->texturemins[i] = LittleShort(in->lm_mins[i]);
		out->extents[i] = LittleShort(in->lm_extents[i]);
		out->lmstep[i] = 16 * scale;
	}

	out->lmsize[0] = w;
	out->lmsize[1] = h;

	if ((out->extents[0] < 0) || (out->extents[1] < 0) ||
		(out->extents[0] / out->lmstep[0] + 1 != w) ||
		(out->extents[1] / out->lmstep[1] + 1 != h))
	{
		Com_DPrintf("%s: face %d has inconsistent lightmap size\n", __func__,
			(int)(out - loadmodel->surfaces));
	}

	if (w > BLOCK_WIDTH || h > BLOCK_HEIGHT)
	{
		return false;
	}

	/* blocks: one per used style, consecutive from lightofs */
	for (numstyles = 0; numstyles < MAXLIGHTMAPS && in->styles[numstyles] != 255; numstyles++)
	{
	}

	ofs = LittleLong(in->lightofs);
	size = (size_t)w * h * 3 * numstyles;

	if (!loadmodel->lightdata || (ofs < 0) || ((size_t)ofs + size > (size_t)loadmodel->lightdatasize))
	{
		Com_DPrintf("%s: face %d lightmap outside of the lighting lump\n", __func__,
			(int)(out - loadmodel->surfaces));
		out->samples = NULL;
	}
	else
	{
		out->samples = loadmodel->lightdata + ofs;
	}

	return true;
}

static void
Mod_LoadFaces(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dface_t *in;
	dface46_t *in46;
	msurface_t *out;
	int i, count, surfnum;
	int planenum, side;
	int ti;
	size_t face_size = Mod_FaceSize(loadmodel->bspversion);

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % face_size)
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / face_size;
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	GL3_LM_BeginBuildingLightmaps(loadmodel);

	for (surfnum = 0; surfnum < count; surfnum++, out++,
		in = (dface_t *)((byte *)in + face_size))
	{
		qboolean stored_lightmap = false;

		/* v38 and v46 share the first 12 bytes (SPEC-101) */
		in46 = (dface46_t *)in;

		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);
		out->flags = 0;
		out->polys = NULL;

		if ((out->numedges < 3) || (out->firstedge < 0) ||
			(out->firstedge + out->numedges > loadmodel->numsurfedges))
		{
			Com_Error(ERR_DROP, "%s: bad edges in face %d of %s",
					__func__, surfnum, loadmodel->name);
		}

		planenum = LittleShort(in->planenum) & 0xFFFF;
		side = LittleShort(in->side);

		if (side)
		{
			out->flags |= SURF_PLANEBACK;
		}

		if (planenum < 0 || planenum >= loadmodel->numplanes)
		{
			Com_Error(ERR_DROP, "%s: Incorrect %d planenum.",
					__func__, planenum);
		}
		out->plane = loadmodel->planes + planenum;

		ti = LittleShort(in->texinfo);

		if ((ti < 0) || (ti >= loadmodel->numtexinfo))
		{
			Com_Error(ERR_DROP, "%s: bad texinfo number",
					__func__);
		}

		out->texinfo = loadmodel->texinfo + ti;

		if (loadmodel->bspversion == BSPVERSION_SOF)
		{
			/* lighting info, stored lightmap geometry */
			for (i = 0; i < MAX_LIGHTMAPS_PER_SURFACE; i++)
			{
				out->styles[i] = in46->styles[i];
			}

			stored_lightmap = Mod_SetSurfaceLightmap46(loadmodel, out, in46);
		}

		if (!stored_lightmap)
		{
			Mod_CalcSurfaceExtents(loadmodel, out);
		}

		if (loadmodel->bspversion == BSPVERSION_SOF)
		{
			if (!stored_lightmap)
			{
				/* no lightmap: one fullbright style */
				out->samples = NULL;
				out->styles[0] = 0;

				for (i = 1; i < MAX_LIGHTMAPS_PER_SURFACE; i++)
				{
					out->styles[i] = 255;
				}
			}
		}
		else
		{
			/* lighting info */
			for (i = 0; i < MAX_LIGHTMAPS_PER_SURFACE; i++)
			{
				out->styles[i] = in->styles[i];
			}

			i = LittleLong(in->lightofs);

			if (i == -1)
			{
				out->samples = NULL;
			}
			else
			{
				out->samples = loadmodel->lightdata + i;
			}
		}

		/* set the drawing flags */
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;

			for (i = 0; i < 2; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}

			GL3_SubdivideSurface(out, loadmodel); /* cut up polygon for warps */
		}

		if (r_fixsurfsky->value)
		{
			if (out->texinfo->flags & SURF_SKY)
			{
				out->flags |= SURF_DRAWSKY;
			}
		}

		/* create lightmaps and polygons */
		out->lightmaptexturenum = -1;

		/* OpenMerc: SoF stores lightmaps for translucent faces too
		   (only sky and warp faces are unlit, SPEC-101) */
		if (!(out->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)) ||
			(stored_lightmap && out->samples &&
			 !(out->texinfo->flags & (SURF_SKY | SURF_WARP | SURF_FLOWING))))
		{
			if ((out->lmsize[0] < 1) || (out->lmsize[1] < 1) ||
				(out->lmsize[0] >= BLOCK_WIDTH) || (out->lmsize[1] > BLOCK_HEIGHT))
			{
				Com_Error(ERR_DROP, "%s: face %d of %s has a %dx%d lightmap",
						__func__, surfnum, loadmodel->name, out->lmsize[0], out->lmsize[1]);
			}

			GL3_LM_CreateSurfaceLightmap(out);
		}

		if (!(out->texinfo->flags & SURF_WARP))
		{
			GL3_LM_BuildPolygonFromSurface(loadmodel, out);
		}
	}

	GL3_LM_EndBuildingLightmaps();
}

static void
Mod_LoadLeafs(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dleaf_t *in;
	dleaf46_t *in46;
	mleaf_t *out;
	int i, j, count, p;
	/* OpenMerc: v46 leafs are 32 bytes, with a volume index (SPEC-101) */
	qboolean v46 = (loadmodel->bspversion == BSPVERSION_SOF);
	size_t leaf_size = v46 ? sizeof(*in46) : sizeof(*in);

	in = (void *)(mod_base + l->fileofs);
	in46 = (void *)(mod_base + l->fileofs);

	if (l->filelen % leaf_size)
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / leaf_size;
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i = 0; i < count; i++, out++)
	{
		unsigned firstleafface;
		short leafmins[3], leafmaxs[3];
		int contents, cluster, area, firstface, numfaces;

		if (v46)
		{
			for (j = 0; j < 3; j++)
			{
				leafmins[j] = in46->mins[j];
				leafmaxs[j] = in46->maxs[j];
			}

			contents = in46->contents;
			cluster = in46->cluster;
			area = in46->area;
			firstface = in46->firstleafface;
			numfaces = in46->numleaffaces;
			in46++;
		}
		else
		{
			for (j = 0; j < 3; j++)
			{
				leafmins[j] = in->mins[j];
				leafmaxs[j] = in->maxs[j];
			}

			contents = in->contents;
			cluster = in->cluster;
			area = in->area;
			firstface = in->firstleafface;
			numfaces = in->numleaffaces;
			in++;
		}

		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = LittleShort(leafmins[j]);
			out->minmaxs[3 + j] = LittleShort(leafmaxs[j]);
		}

		p = LittleLong(contents);
		out->contents = p;

		out->cluster = LittleShort(cluster);
		out->area = LittleShort(area);

		// make unsigned long from signed short
		firstleafface = LittleShort(firstface) & 0xFFFF;
		out->nummarksurfaces = LittleShort(numfaces) & 0xFFFF;

		out->firstmarksurface = loadmodel->marksurfaces + firstleafface;
		if ((firstleafface + out->nummarksurfaces) > loadmodel->nummarksurfaces)
		{
			Com_Error(ERR_DROP, "%s: wrong marksurfaces position in %s",
				__func__, loadmodel->name);
		}
	}
}

static void
Mod_LoadMarksurfaces(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	int i, j, count;
	short *in;
	msurface_t **out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for (i = 0; i < count; i++)
	{
		j = LittleShort(in[i]) & 0xFFFF; /* unsigned face index */

		if ((j < 0) || (j >= loadmodel->numsurfaces))
		{
			Com_Error(ERR_DROP, "%s: bad surface number", __func__);
		}

		out[i] = loadmodel->surfaces + j;
	}
}

static void
Mod_LoadBrushModel(gl3model_t *mod, void *buffer, int modfilelen)
{
	int i;
	dheader_t *header;
	byte *mod_base;

	if (mod != mod_known)
	{
		Com_Error(ERR_DROP, "Loaded a brush model after the world");
	}

	header = (dheader_t *)buffer;

	if (modfilelen < (int)sizeof(dheader_t))
	{
		Com_Error(ERR_DROP, "%s: %s is too short", __func__, mod->name);
	}

	i = LittleLong(header->version);

	/* OpenMerc: accept Soldier of Fortune maps (SPEC-101) */
	if ((i != BSPVERSION) && (i != BSPVERSION_SOF))
	{
		Com_Error(ERR_DROP, "%s: %s has wrong version number (%i should be %i or %i)",
				__func__, mod->name, i, BSPVERSION, BSPVERSION_SOF);
	}

	mod->bspversion = i;

	if ((i == BSPVERSION_SOF) && (modfilelen < (int)sizeof(dheader46_t)))
	{
		Com_Error(ERR_DROP, "%s: %s is too short", __func__, mod->name);
	}

	/* swap all the lumps */
	mod_base = (byte *)header;

	for (i = 0; i < ((mod->bspversion == BSPVERSION_SOF) ?
			sizeof(dheader46_t) : sizeof(dheader_t)) / 4; i++)
	{
		((int *)header)[i] = LittleLong(((int *)header)[i]);
	}

	/* OpenMerc: the lumps used here must lie inside the file */
	for (i = 0; i < HEADER_LUMPS; i++)
	{
		const lump_t *l = &header->lumps[i];

		if ((l->fileofs < 0) || (l->filelen < 0) || (l->fileofs > modfilelen) ||
			(l->filelen > modfilelen - l->fileofs))
		{
			Com_Error(ERR_DROP, "%s: %s has a bad lump %d", __func__, mod->name, i);
		}
	}

	// calculate the needed hunksize from the lumps
	int hunkSize = 0;
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_VERTEXES], sizeof(dvertex_t), sizeof(mvertex_t), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_EDGES], sizeof(dedge_t), sizeof(medge_t), 0);
	hunkSize += sizeof(medge_t) + 31; // for count+1 in Mod_LoadEdges()
	int surfEdgeCount = (header->lumps[LUMP_SURFEDGES].filelen+sizeof(int)-1)/sizeof(int);
	if(surfEdgeCount < MAX_MAP_SURFEDGES) // else it errors out later anyway
		hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_SURFEDGES], sizeof(int), sizeof(int), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LIGHTING], 1, 1, 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_PLANES], sizeof(dplane_t), sizeof(cplane_t)*2, 0);
	hunkSize += calcTexinfoAndFacesSize(mod_base, &header->lumps[LUMP_FACES], &header->lumps[LUMP_TEXINFO],
		mod->bspversion);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LEAFFACES], sizeof(short), sizeof(msurface_t *), 0); // yes, out is indeed a pointer!
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_VISIBILITY], 1, 1, 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LEAFS],
		(mod->bspversion == BSPVERSION_SOF) ? sizeof(dleaf46_t) : sizeof(dleaf_t),
		sizeof(mleaf_t), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_NODES], sizeof(dnode_t), sizeof(mnode_t), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_MODELS], sizeof(dmodel_t), sizeof(gl3model_t), 0);

	mod->extradata = Hunk_Begin(hunkSize);
	mod->type = mod_brush;

	/* load into heap */
	Mod_LoadVertexes(mod->name, &mod->vertexes, &mod->numvertexes, mod_base,
		&header->lumps[LUMP_VERTEXES], 0);
	Mod_LoadEdges(mod->name, &mod->edges, &mod->numedges,
		mod_base, &header->lumps[LUMP_EDGES], 1);
	Mod_LoadSurfedges(mod->name, &mod->surfedges, &mod->numsurfedges,
		mod_base, &header->lumps[LUMP_SURFEDGES], 0);
	Mod_LoadLighting(&mod->lightdata, mod_base, &header->lumps[LUMP_LIGHTING]);
	mod->lightdatasize = mod->lightdata ? header->lumps[LUMP_LIGHTING].filelen : 0;
	Mod_LoadPlanes (mod->name, &mod->planes, &mod->numplanes,
		mod_base, &header->lumps[LUMP_PLANES], 0);
	Mod_LoadTexinfo (mod->name, &mod->texinfo, &mod->numtexinfo,
		mod_base, &header->lumps[LUMP_TEXINFO], (findimage_t)GL3_FindImage,
		(mod->bspversion == BSPVERSION_SOF) ? GL3_SoF_NoTexture() : gl3_notexture, 0);
	Mod_LoadFaces(mod, mod_base, &header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces(mod, mod_base, &header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility(&mod->vis, mod_base, &header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs(mod, mod_base, &header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes(mod->name, mod->planes, mod->numplanes, mod->leafs,
		mod->numleafs, &mod->nodes, &mod->numnodes, mod_base,
		&header->lumps[LUMP_NODES]);
	Mod_LoadSubmodels (mod, mod_base, &header->lumps[LUMP_MODELS]);
	mod->numframes = 2;

	/* GFX-modern: the compiler's sun keys of worldspawn for the sun shadows (SPEC-AMEND-016 R16.2) */
	GL3_CSM_LoadSun((const char *)(mod_base + header->lumps[LUMP_ENTITIES].fileofs),
		header->lumps[LUMP_ENTITIES].filelen); /* regular and alternate animation */

	/* OpenMerc (I4a): fog regions (lumps 19, 21) and the Ghoul world lights (lump 20) */
	if (mod->bspversion == BSPVERSION_SOF)
	{
		const dheader46_t *h46 = (const dheader46_t *)header;

		for (i = LUMP_FACEPOLYS; i <= LUMP_VOLUMES; i++)
		{
			const lump_t *l = &h46->lumps[i];

			if ((l->fileofs < 0) || (l->filelen < 0) || (l->fileofs > modfilelen) ||
				(l->filelen > modfilelen - l->fileofs))
			{
				Com_Error(ERR_DROP, "%s: %s has a bad lump %d", __func__, mod->name, i);
			}
		}

		GL3_Fog_LoadRegions(mod, mod_base, &h46->lumps[LUMP_FACES], &h46->lumps[LUMP_FACEPOLYS],
			&h46->lumps[LUMP_VOLUMES], &h46->lumps[LUMP_LEAFS]);
		GL3_SoF_WorldLights(mod_base + h46->lumps[LUMP_LIGHTS].fileofs, h46->lumps[LUMP_LIGHTS].filelen);
	}
	else
	{
		GL3_Fog_FreeRegions();
	}
}

static void
Mod_Free(gl3model_t *mod)
{
	Hunk_Free(mod->extradata);
	memset(mod, 0, sizeof(*mod));
}

void
GL3_Mod_FreeAll(void)
{
	int i;

	for (i = 0; i < mod_numknown; i++)
	{
		if (mod_known[i].extradatasize)
		{
			Mod_Free(&mod_known[i]);
		}
	}
}

/*
 * Loads in a model for the given name
 */
static gl3model_t *
Mod_ForName(const char *name, gl3model_t *parent_model, qboolean crash)
{
	gl3model_t *mod;
	void *buf;
	int i, modfilelen;

	if (!name[0])
	{
		Com_Error(ERR_DROP, "%s: NULL name", __func__);
	}

	/* inline models are grabbed only from worldmodel */
	if (name[0] == '*' && parent_model)
	{
		i = (int)strtol(name + 1, (char **)NULL, 10);

		if (i < 1 || i >= parent_model->numsubmodels)
		{
			Com_Error(ERR_DROP, "%s: bad inline model number",
					__func__);
		}

		return &parent_model->submodels[i];
	}

	/* search the currently loaded models */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			continue;
		}

		if (!strcmp(mod->name, name))
		{
			return mod;
		}
	}

	/* find a free model slot spot */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			break; /* free spot */
		}
	}

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
		{
			Com_Error(ERR_DROP, "mod_numknown == MAX_MOD_KNOWN");
		}

		mod_numknown++;
	}

	strcpy(mod->name, name);

	/* load the file */
	modfilelen = ri.FS_LoadFile(mod->name, (void **)&buf);

	if (!buf)
	{
		if (crash)
		{
			Com_Error(ERR_DROP, "%s: %s not found",
					__func__, mod->name);
		}

		memset(mod->name, 0, sizeof(mod->name));
		return NULL;
	}

	/* call the apropriate loader */
	switch (LittleLong(*(unsigned *)buf))
	{
		case IDALIASHEADER:
			{
				mod->extradata = Mod_LoadMD2(mod->name, buf, modfilelen,
					mod->mins, mod->maxs,
					(struct image_s **)mod->skins, (findimage_t)GL3_FindImage,
					&(mod->type));
				if (!mod->extradata)
				{
					Com_Error(ERR_DROP, "%s: Failed to load %s",
						__func__, mod->name);
				}
			};
			break;

		case IDSPRITEHEADER:
			{
				mod->extradata = Mod_LoadSP2(mod->name, buf, modfilelen,
					(struct image_s **)mod->skins, (findimage_t)GL3_FindImage,
					&(mod->type));
				if (!mod->extradata)
				{
					Com_Error(ERR_DROP, "%s: Failed to load %s",
						__func__, mod->name);
				}
			}
			break;

		case IDBSPHEADER:
			Mod_LoadBrushModel(mod, buf, modfilelen);
			break;

		default:
			Com_Error(ERR_DROP, "%s: unknown fileid for %s",
					__func__, mod->name);
			break;
	}

	mod->extradatasize = Hunk_End();

	ri.FS_FreeFile(buf);

	return mod;
}

/*
 * Specifies the model that will be used as the world
 */
void
GL3_BeginRegistration(const char *model)
{
	char fullname[MAX_QPATH];
	cvar_t *flushmap;

	registration_sequence++;
	gl3_oldviewcluster = -1; /* force markleafs */

	gl3state.currentlightmap = -1;

	Com_sprintf(fullname, sizeof(fullname), "maps/%s.bsp", model);

	/* explicitly free the old map if different
	   this guarantees that mod_known[0] is the
	   world map */
	flushmap = ri.Cvar_Get("flushmap", "0", 0);

	if (strcmp(mod_known[0].name, fullname) || flushmap->value)
	{
		Mod_Free(&mod_known[0]);
	}

	/* OpenMerc: the substitution cache is emptied for each cycle (SPEC-413 §8) */
	GL3_SoF_BeginRegistrationImages();
	GL3_SoF_ClearLightCache();

	gl3_worldmodel = Mod_ForName(fullname, NULL, true);

	/* OpenMerc: a world kept from the last cycle still uses its images */
	if (gl3_worldmodel)
	{
		int i;

		gl3_worldmodel->registration_sequence = registration_sequence;

		for (i = 0; i < gl3_worldmodel->numtexinfo; i++)
		{
			gl3image_t *image = gl3_worldmodel->texinfo[i].image;

			if (image)
			{
				image->registration_sequence = registration_sequence;

				if (image->detail)
				{
					image->detail->registration_sequence = registration_sequence;
				}

				if (image->damage)
				{
					image->damage->registration_sequence = registration_sequence;
				}
			}
		}
	}

	gl3_viewcluster = -1;
}

struct model_s *
GL3_RegisterModel(const char *name)
{
	gl3model_t *mod;

	mod = Mod_ForName(name, gl3_worldmodel, false);

	if (mod)
	{
		mod->registration_sequence = registration_sequence;

		/* register any images used by the models */
		if (mod->type == mod_brush)
		{
			int i;

			for (i = 0; i < mod->numtexinfo; i++)
			{
				mod->texinfo[i].image->registration_sequence = registration_sequence;
			}
		}
		else
		{
			/* numframes is unused for SP2 but lets set it also  */
			mod->numframes = Mod_ReLoadSkins((struct image_s **)mod->skins,
				(findimage_t)GL3_FindImage, mod->extradata, mod->type);
		}
	}

	return mod;
}

/*
 * OpenMerc: SPEC-002 export slot 0x14. "*N" is inline model N (1..256 and
 * below the world's submodel count, else a drop error); any other name returns
 * the loaded world whatever the name; without a world, a name ending in "bsp"
 * is loaded as the world and anything else gives NULL.
 */
struct model_s *
GL3_SoF_RegisterModel(const char *name)
{
	size_t len;

	if (!name || !name[0])
	{
		Com_Error(ERR_DROP, "Mod_ForName: NULL name");
		return NULL;
	}

	if (name[0] == '*')
	{
		int i = (int)strtol(name + 1, (char **)NULL, 10);

		if (!gl3_worldmodel || (i < 1) || (i > 256) || (i >= gl3_worldmodel->numsubmodels))
		{
			Com_Error(ERR_DROP, "Bad inline model number");
			return NULL;
		}

		gl3_worldmodel->submodels[i].registration_sequence = registration_sequence;
		return &gl3_worldmodel->submodels[i];
	}

	if (gl3_worldmodel)
	{
		gl3_worldmodel->registration_sequence = registration_sequence;
		return gl3_worldmodel;
	}

	len = strlen(name);

	if ((len >= 3) && !Q_stricmp(name + len - 3, "bsp"))
	{
		gl3_worldmodel = Mod_ForName(name, NULL, true);
		return gl3_worldmodel;
	}

	return NULL;
}

void
GL3_EndRegistration(void)
{
	int i;
	gl3model_t *mod;

	/* OpenMerc: SoF frees unused images at the end of every registration
	   cycle (SPEC-002, SPEC-413 §8); models are kept while there is room */
	GL3_FreeUnusedImages();

	if (Mod_HasFreeSpace() && GL3_ImageHasFreeSpace())
	{
		// should be enough space for load next maps
		return;
	}

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			continue;
		}

		if (mod->registration_sequence != registration_sequence)
		{
			/* don't need this model */
			Mod_Free(mod);
		}
	}

	GL3_FreeUnusedImages();
}
