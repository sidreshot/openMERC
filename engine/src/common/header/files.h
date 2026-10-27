/*
 * Copyright (C) 1997-2001 Id Software, Inc.
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
 *  The prototypes for most file formats used by Quake II
 *
 * =======================================================================
 *
 * OpenMerc: added the Soldier of Fortune IBSP version 46 records
 * (dheader46_t, dface46_t, dleaf46_t) at the end of the file.
 * Implements: SPEC-101
 */

#ifndef CO_FILES_H
#define CO_FILES_H

/* The .pak files are just a linear collapse of a directory tree */

#define IDPAKHEADER (('K' << 24) + ('C' << 16) + ('A' << 8) + 'P')

typedef struct
{
	char name[56];
	int filepos, filelen;
} dpackfile_t;

typedef struct
{
	int ident; /* == IDPAKHEADER */
	int dirofs;
	int dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK 4096

/* PCX files are used for as many images as possible */

typedef struct
{
	char manufacturer;
	char version;
	char encoding;
	char bits_per_pixel;
	unsigned short xmin, ymin, xmax, ymax;
	unsigned short hres, vres;
	unsigned char palette[48];
	char reserved;
	char color_planes;
	unsigned short bytes_per_line;
	unsigned short palette_type;
	char filler[58];
	unsigned char data;   /* unbounded */
} pcx_t;

/* .MD2 triangle model file format */

#define IDALIASHEADER (('2' << 24) + ('P' << 16) + ('D' << 8) + 'I')
#define ALIAS_VERSION 8

#define MAX_TRIANGLES 4096
#define MAX_VERTS 2048
#define MAX_FRAMES 512
#define MAX_MD2SKINS 32
#define MAX_SKINNAME 64

typedef struct
{
	short s;
	short t;
} dstvert_t;

typedef struct
{
	short index_xyz[3];
	short index_st[3];
} dtriangle_t;

typedef struct
{
	byte v[3]; /* scaled byte to fit in frame mins/maxs */
	byte lightnormalindex;
} dtrivertx_t;

#define DTRIVERTX_V0 0
#define DTRIVERTX_V1 1
#define DTRIVERTX_V2 2
#define DTRIVERTX_LNI 3
#define DTRIVERTX_SIZE 4

typedef struct
{
	float scale[3];       /* multiply byte verts by this */
	float translate[3];   /* then add this */
	char name[16];        /* frame name from grabbing */
	dtrivertx_t verts[1]; /* variable sized */
} daliasframe_t;

/* the glcmd format:
 * - a positive integer starts a tristrip command, followed by that many
 *   vertex structures.
 * - a negative integer starts a trifan command, followed by -x vertexes
 *   a zero indicates the end of the command list.
 * - a vertex consists of a floating point s, a floating point t,
 *   and an integer vertex index. */

typedef struct
{
	int ident;
	int version;

	int skinwidth;
	int skinheight;
	int framesize;  /* byte size of each frame */

	int num_skins;
	int num_xyz;
	int num_st;     /* greater than num_xyz for seams */
	int num_tris;
	int num_glcmds; /* dwords in strip/fan command list */
	int num_frames;

	int ofs_skins;  /* each skin is a MAX_SKINNAME string */
	int ofs_st;     /* byte offset from start for stverts */
	int ofs_tris;   /* offset for dtriangles */
	int ofs_frames; /* offset for first frame */
	int ofs_glcmds;
	int ofs_end;    /* end of file */
} dmdl_t;

/* .SP2 sprite file format */

#define IDSPRITEHEADER (('2' << 24) + ('S' << 16) + ('D' << 8) + 'I') /* little-endian "IDS2" */
#define SPRITE_VERSION 2

typedef struct
{
	int width, height;
	int origin_x, origin_y;  /* raster coordinates inside pic */
	char name[MAX_SKINNAME]; /* name of pcx file */
} dsprframe_t;

typedef struct
{
	int ident;
	int version;
	int numframes;
	dsprframe_t frames[1]; /* variable sized */
} dsprite_t;

/* .WAL texture file format */

#define MIPLEVELS 4
typedef struct miptex_s
{
	char name[32];
	unsigned width, height;
	unsigned offsets[MIPLEVELS]; /* four mip maps stored */
	char animname[32];           /* next frame in animation chain */
	int flags;
	int contents;
	int value;
} miptex_t;

/* .M8 texture file format */

#define M8_MIP_LEVELS 16
#define M8_VERSION 0x2

typedef struct {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} rgb_t;

typedef struct m8tex_s
{
	unsigned version;
	char name[32];
	unsigned width[M8_MIP_LEVELS];
	unsigned height[M8_MIP_LEVELS];
	unsigned offsets[M8_MIP_LEVELS]; /* 16 mip maps stored */
	char animname[32];           /* next frame in animation chain */
	rgb_t palette[256];
	int flags;
	int contents;
	int value;
} m8tex_t;

/* .M32 texture file format */

#define M32_VERSION     0x4
#define M32_MIP_LEVELS  16

typedef struct m32tex_s
{
	int version;
	char name[128];
	char altname[128];                   // texture substitution
	char animname[128];                  // next frame in animation chain
	char damagename[128];                // image that should be shown when damaged
	unsigned width[M32_MIP_LEVELS], height[M32_MIP_LEVELS];
	unsigned offsets[M32_MIP_LEVELS];
	int flags;
	int contents;
	int value;
	float scale_x, scale_y;
	int mip_scale;

	// detail texturing info
	char dt_name[128];                  // detailed texture name
	float dt_scale_x, dt_scale_y;
	float dt_u, dt_v;
	float dt_alpha;
	int dt_src_blend_mode, dt_dst_blend_mode;

	int unused[20];                     // future expansion to maintain compatibility with h2
} m32tex_t;

/* .BSP file format */

#define IDBSPHEADER (('P' << 24) + ('S' << 16) + ('B' << 8) + 'I') /* little-endian "IBSP" */
#define BSPVERSION 38

/* upper design bounds: leaffaces, leafbrushes, planes, and 
 * verts are still bounded by 16 bit short limits */
#define MAX_MAP_MODELS 1024
#define MAX_MAP_BRUSHES 8192
#define MAX_MAP_ENTITIES 2048
#define MAX_MAP_ENTSTRING 0x40000
#define MAX_MAP_TEXINFO 8192

#define MAX_MAP_AREAS 256
#define MAX_MAP_AREAPORTALS 1024
#define MAX_MAP_PLANES 65536
#define MAX_MAP_NODES 65536
#define MAX_MAP_BRUSHSIDES 65536
#define MAX_MAP_LEAFS 65536
#define MAX_MAP_VERTS 65536
#define MAX_MAP_FACES 65536
#define MAX_MAP_LEAFFACES 65536
#define MAX_MAP_LEAFBRUSHES 65536
#define MAX_MAP_PORTALS 65536
#define MAX_MAP_EDGES 128000
#define MAX_MAP_SURFEDGES 256000
#define MAX_MAP_LIGHTING 0x200000
#define MAX_MAP_VISIBILITY 0x100000

/* key / value pair sizes */

#define MAX_KEY 32
#define MAX_VALUE 1024

/* ================================================================== */

typedef struct
{
	int fileofs, filelen;
} lump_t;

#define LUMP_ENTITIES 0
#define LUMP_PLANES 1
#define LUMP_VERTEXES 2
#define LUMP_VISIBILITY 3
#define LUMP_NODES 4
#define LUMP_TEXINFO 5
#define LUMP_FACES 6
#define LUMP_LIGHTING 7
#define LUMP_LEAFS 8
#define LUMP_LEAFFACES 9
#define LUMP_LEAFBRUSHES 10
#define LUMP_EDGES 11
#define LUMP_SURFEDGES 12
#define LUMP_MODELS 13
#define LUMP_BRUSHES 14
#define LUMP_BRUSHSIDES 15
#define LUMP_POP 16
#define LUMP_AREAS 17
#define LUMP_AREAPORTALS 18
#define HEADER_LUMPS 19

typedef struct
{
	int ident;
	int version;
	lump_t lumps[HEADER_LUMPS];
} dheader_t;

typedef struct
{
	float mins[3], maxs[3];
	float origin[3];         /* for sounds or lights */
	int headnode;
	int firstface, numfaces; /* submodels just draw faces without
							    walking the bsp tree */
} dmodel_t;

typedef struct
{
	float point[3];
} dvertex_t;

/* 0-2 are axial planes */
#define PLANE_X 0
#define PLANE_Y 1
#define PLANE_Z 2

/* 3-5 are non-axial planes snapped to the nearest */
#define PLANE_ANYX 3
#define PLANE_ANYY 4
#define PLANE_ANYZ 5

/* planes (x&~1) and (x&~1)+1 are always opposites */

typedef struct
{
	float normal[3];
	float dist;
	int type; /* PLANE_X - PLANE_ANYZ */
} dplane_t;

/* contents flags are seperate bits
 * - given brush can contribute multiple content bits
 * - multiple brushes can be in a single leaf */

/* lower bits are stronger, and will eat weaker brushes completely */
#define CONTENTS_SOLID 1  /* an eye is never valid in a solid */
#define CONTENTS_WINDOW 2 /* translucent, but not watery */
#define CONTENTS_AUX 4
#define CONTENTS_LAVA 8
#define CONTENTS_SLIME 16
#define CONTENTS_WATER 32
#define CONTENTS_MIST 64
#define LAST_VISIBLE_CONTENTS 64

/* remaining contents are non-visible, and don't eat brushes */
#define CONTENTS_AREAPORTAL 0x8000

#define CONTENTS_PLAYERCLIP 0x10000
#define CONTENTS_MONSTERCLIP 0x20000

/* currents can be added to any other contents, and may be mixed */
#define CONTENTS_CURRENT_0 0x40000
#define CONTENTS_CURRENT_90 0x80000
#define CONTENTS_CURRENT_180 0x100000
#define CONTENTS_CURRENT_270 0x200000
#define CONTENTS_CURRENT_UP 0x400000
#define CONTENTS_CURRENT_DOWN 0x800000

#define CONTENTS_ORIGIN 0x1000000       /* removed before bsping an entity */

#define CONTENTS_MONSTER 0x2000000      /* should never be on a brush, only in game */
#define CONTENTS_DEADMONSTER 0x4000000
#define CONTENTS_DETAIL 0x8000000       /* brushes to be added after vis leafs */
#define CONTENTS_TRANSLUCENT 0x10000000 /* auto set if any surface has trans */
#define CONTENTS_LADDER 0x20000000

#define SURF_LIGHT 0x1    /* value will hold the light strength */

#define SURF_SLICK 0x2    /* effects game physics */

#define SURF_SKY 0x4      /* don't draw, but add to skybox */
#define SURF_WARP 0x8     /* turbulent water warp */
#define SURF_TRANS33 0x10
#define SURF_TRANS66 0x20
#define SURF_FLOWING 0x40 /* scroll towards angle */
#define SURF_NODRAW 0x80  /* don't bother referencing the texture */

typedef struct
{
	int planenum;
	int children[2];         /* negative numbers are -(leafs+1), not nodes */
	short mins[3];           /* for frustom culling */
	short maxs[3];
	unsigned short firstface;
	unsigned short numfaces; /* counting both sides */
} dnode_t;

typedef struct texinfo_s
{
	float vecs[2][4]; /* [s/t][xyz offset] */
	int flags;        /* miptex flags + overrides light emission, etc */
	int value;           
	char texture[32]; /* texture name (textures*.wal) */
	int nexttexinfo;  /* for animations, -1 = end of chain */
} texinfo_t;

/* note that edge 0 is never used, because negative edge 
   nums are used for counterclockwise use of the edge in
   a face */
typedef struct
{
	unsigned short v[2]; /* vertex numbers */
} dedge_t;

#define MAXLIGHTMAPS 4
typedef struct
{
	unsigned short planenum;
	short side;

	int firstedge; /* we must support > 64k edges */
	short numedges;
	short texinfo;

	/* lighting info */
	byte styles[MAXLIGHTMAPS];
	int lightofs; /* start of [numstyles*surfsize] samples */
} dface_t;

typedef struct
{
	int contents; /* OR of all brushes (not needed?) */

	short cluster;
	short area;

	short mins[3]; /* for frustum culling */
	short maxs[3];

	unsigned short firstleafface;
	unsigned short numleaffaces;

	unsigned short firstleafbrush;
	unsigned short numleafbrushes;
} dleaf_t;

typedef struct
{
	unsigned short planenum; /* facing out of the leaf */
	short texinfo;
} dbrushside_t;

typedef struct
{
	int firstside;
	int numsides;
	int contents;
} dbrush_t;

#define ANGLE_UP -1
#define ANGLE_DOWN -2

/* the visibility lump consists of a header with a count, then 
 * byte offsets for the PVS and PHS of each cluster, then the raw 
 * compressed bit vectors */
#define DVIS_PVS 0
#define DVIS_PHS 1
typedef struct
{
	int numclusters;
	int bitofs[8][2]; /* bitofs[numclusters][2] */
} dvis_t;

/* each area has a list of portals that lead into other areas
 * when portals are closed, other areas may not be visible or
 * hearable even if the vis info says that it should be */
typedef struct
{
	int portalnum;
	int otherarea;
} dareaportal_t;

typedef struct
{
	int numareaportals;
	int firstareaportal;
} darea_t;

/* ================================================================== */

/*
 * OpenMerc: Soldier of Fortune maps, IBSP version 46.
 * Implements: SPEC-101
 *
 * Same ident as Quake II; 22 lumps instead of 19. Lumps 0-18 keep the
 * Quake II numbering, record layouts and meaning, except faces (44 bytes)
 * and leafs (32 bytes). Lumps 19-21 are new.
 */

#define BSPVERSION_SOF 46
#define HEADER_LUMPS_SOF 22

#define LUMP_FACEPOLYS 19 /* sub-polygons of faces, 8 bytes each */
#define LUMP_LIGHTS 20    /* light source list, 36 bytes each */
#define LUMP_VOLUMES 21   /* volume (fog?) records, 76 bytes each */

#define CONTENTS_VOLUME_SOF 0x100 /* leaf/brush contents bit, leaf has a volume */

/* texinfo / .m32 surface flag (SPEC-100): the image has alpha */
#define SURF_ALPHA_SOF 0x800

typedef struct
{
	int ident;
	int version;
	lump_t lumps[HEADER_LUMPS_SOF];
} dheader46_t;

typedef struct
{
	unsigned short planenum;
	short side;

	int firstedge;
	short numedges;
	short texinfo;

	short volume;         /* -1 or index into LUMP_VOLUMES */
	unsigned short pad0;  /* always 0 */
	int firstpoly;        /* -1 or index into LUMP_FACEPOLYS */
	unsigned short numpolys;

	byte styles[MAXLIGHTMAPS]; /* unused slots 255 */
	unsigned short pad1;  /* always 0 */
	int lightofs;         /* byte offset into the lighting lump */

	/* lightmap geometry, stored instead of computed at load time */
	byte lm_width;        /* samples along s */
	byte lm_height;       /* samples along t */
	byte lm_scale[2];     /* 1 or 2: one sample covers 16 * scale texels */
	short lm_mins[2];     /* texture space minimum (s, t), multiple of 16 */
	short lm_extents[2];  /* texture space extent (s, t), multiple of 16 */
} dface46_t;

typedef struct
{
	int contents;

	short cluster;
	short area;
	short volume;         /* -1 or index into LUMP_VOLUMES */

	short mins[3];
	short maxs[3];

	unsigned short firstleafface;
	unsigned short numleaffaces;

	unsigned short firstleafbrush;
	unsigned short numleafbrushes;

	unsigned short pad;   /* always 0 */
} dleaf46_t;

/* compile-time record size checks (negative array size on mismatch) */
typedef char dheader46_size_check[(sizeof(dheader46_t) == 184) ? 1 : -1];
typedef char dface46_size_check[(sizeof(dface46_t) == 44) ? 1 : -1];
typedef char dleaf46_size_check[(sizeof(dleaf46_t) == 32) ? 1 : -1];

#endif

