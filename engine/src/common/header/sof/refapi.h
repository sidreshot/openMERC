/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I4a (renderer). Other packages: include, do not edit.
 *
 * Soldier of Fortune 1.07f renderer module interface: the import table the
 * engine hands to GetRefAPI (108 bytes), the export table it gets back
 * (220 bytes) and the per-frame scene records (frame record, entity, dynamic
 * light, light style, alpha quad, quad chain). Field names are OpenMerc's;
 * offsets, sizes and meanings are those of SPEC-002 (with SPEC-AMEND-002).
 * The layouts are binary-exact in the 32-bit Windows build; elsewhere the
 * same structures are used with native pointer sizes.
 *
 * Implements: SPEC-002, SPEC-AMEND-002
 */

#ifndef OM_SOF_REFAPI_H
#define OM_SOF_REFAPI_H

#include <stddef.h>

#define SOF_REF_API_VERSION 3

#define SOF_MAX_ENTITIES 512
#define SOF_MAX_DLIGHTS 32
#define SOF_MAX_QUADCHAINS 256
#define SOF_MAX_ALPHAQUADS 1024
#define SOF_MAX_LIGHTSTYLES 256
#define SOF_MAX_IMAGES 800

/* render flags (entity flags, SPEC-002 / SPEC-411) */
#define SOF_RF_MINLIGHT 0x00000001
#define SOF_RF_VIEWERMODEL 0x00000002
#define SOF_RF_WEAPONMODEL 0x00000004
#define SOF_RF_FULLBRIGHT 0x00000008
#define SOF_RF_DEPTHHACK 0x00000010
#define SOF_RF_GLOW 0x00000200
#define SOF_RF_CASTSHADOW 0x00001000
#define SOF_RF_GHOUL 0x00400000
#define SOF_RF_AMBIENTONLY 0x10000000

/* frame flags (refdef rdflags) */
#define SOF_RDF_UNDERWATER 0x1
#define SOF_RDF_NOWORLDMODEL 0x2
#define SOF_RDF_IRGOGGLES 0x4

/* alpha-quad flags (+0x38) */
#define SOF_AQ_HIDDEN 0x0001
#define SOF_AQ_BILLBOARD 0x0002
#define SOF_AQ_BEAM 0x0004
#define SOF_AQ_LIT 0x0008
#define SOF_AQ_ORIENTED 0x0010
#define SOF_AQ_IMMEDIATE 0x0040
#define SOF_AQ_BATCHED 0x0080
#define SOF_AQ_DECAL 0x0100
#define SOF_AQ_ADDITIVE 0x0200
#define SOF_AQ_DARKEN 0x0400
#define SOF_AQ_NODEPTH 0x0800
#define SOF_AQ_DRAWN 0x1000

/* print levels of import 0x20 */
#define SOF_PRINT_ALL 0
#define SOF_PRINT_DEVELOPER 1
#define SOF_PRINT_DEVELOPER2 2
#define SOF_PRINT_ALERT 3

/* cvar object (32 bytes; the OpenMerc cvar_t of shared.h starts with it) */
typedef struct sofcvar_s
{
	char *name;
	char *string;
	char *latched_string;
	int flags;
	void (*changed)(struct sofcvar_s *cvar);
	int modified;
	float value;
	struct sofcvar_s *next;
} sofcvar_t;

/* font reference: Microsoft Visual C++ 6.0 std::string; the renderer reads
   only the character pointer (offset 4) */
typedef struct
{
	unsigned char allocator;
	unsigned char pad[3];
	const char *chars;
	size_t length;
	size_t capacity;
} om_fontref_t;

/* entity, 96 bytes */
typedef struct sofentity_s
{
	void *model;            /* 0x00 model handle, NULL = placeholder */
	float angles[3];        /* 0x04 */
	float bbmin[3];         /* 0x10 decoded solid box (no renderer use found) */
	float bbmax[3];         /* 0x1c */
	float origin[3];        /* 0x28 */
	int frame;              /* 0x34 brush: animation frame; Ghoul: low 16 bits = instance handle */
	int oldframe;           /* 0x38 */
	float backlerp;         /* 0x3c */
	int skinnum;            /* 0x40 */
	int unknown44;          /* 0x44 */
	float alpha;            /* 0x48 */
	int unknown4c;          /* 0x4c */
	void *skin;             /* 0x50 image handle */
	int flags;              /* 0x54 SOF_RF_* */
	float *lightout;        /* 0x58 renderer writes the Ghoul light colour here */
	unsigned char unknown5c;
	unsigned char pad5d[3];
} sofentity_t;

/* dynamic light, 28 bytes */
typedef struct
{
	float origin[3];
	float color[3];
	float intensity;
} sofdlight_t;

/* light style, 16 bytes */
typedef struct
{
	float rgb[3];
	float white;
} soflightstyle_t;

/* alpha quad, 100 bytes read by the renderer */
typedef struct sofalphaquad_s
{
	float origin[3];        /* 0x00 origin; beam start */
	float v0c[3];           /* 0x0c beam: end point; billboard: [0] height, [1] width */
	float f18;              /* 0x18 beam: half width; billboard: rotation cosine */
	float f1c;              /* 0x1c billboard: rotation sine */
	float normal[3];        /* 0x20 used with SOF_AQ_ORIENTED */
	unsigned char color[4]; /* 0x2c RGBA */
	void *image;            /* 0x30 image cache, filled by the renderer */
	const char *texname;    /* 0x34 sprite image name */
	unsigned short flags;   /* 0x38 SOF_AQ_* */
	unsigned char lightdir; /* 0x3a direction byte for lit quads, 255 = down */
	unsigned char pad3b;
	int fogcache;           /* 0x3c < 0: not computed yet (renderer writes) */
	int leaf;               /* 0x40 world leaf for visibility, < 0: always visible */
	unsigned char engine[28]; /* 0x44 engine-only data */
	struct sofalphaquad_s *next; /* 0x60 next quad of a chain */
} sofalphaquad_t;

/* quad chain, 40 bytes */
typedef struct
{
	sofalphaquad_t *first;  /* 0x00 */
	float mins[3];          /* 0x04 */
	float maxs[3];          /* 0x10 */
	float weight;           /* 0x1c coverage weight; renderer stores weight / sqrt(key) */
	int count;              /* 0x20 */
	void *owner;            /* 0x24 engine-only */
} sofquadchain_t;

/* frame record, 132 bytes */
typedef struct
{
	float fps;              /* 0x00 smoothed frame rate */
	int x, y, width, height; /* 0x04 view rectangle */
	float fov_x, fov_y;     /* 0x14 */
	float vieworg[3];       /* 0x1c */
	float viewangles[3];    /* 0x28 */
	float blend[4];         /* 0x34 full-screen tint, 0..1 */
	float time;             /* 0x44 client time, seconds */
	float oldgametime;      /* 0x48 */
	float gametime_delta;   /* 0x4c */
	float gametime;         /* 0x50 game clock, stops while paused */
	int rdflags;            /* 0x54 SOF_RDF_* */
	unsigned char *areabits; /* 0x58 */
	soflightstyle_t *lightstyles; /* 0x5c 256 entries */
	int num_entities;       /* 0x60 */
	sofentity_t **entities; /* 0x64 array of pointers */
	int num_dlights;        /* 0x68 */
	sofdlight_t *dlights;   /* 0x6c room for 32 entries (renderer appends) */
	int num_quadchains;     /* 0x70 */
	sofquadchain_t **quadchains; /* 0x74 */
	int num_alphaquads;     /* 0x78 */
	sofalphaquad_t **alphaquads; /* 0x7c */
	void *scratch;          /* 0x80 engine leaves 0; renderer resets to 0 */
} sofrefdef_t;

typedef float sofvec3_t[3];

/* import table (engine -> renderer), 108 bytes */
typedef struct
{
	void (*Sys_Error)(int err_level, char *fmt, ...);                  /* 0x00 */
	void *unused04;                                                    /* 0x04 */
	void *unused08;                                                    /* 0x08 */
	void (*Cmd_AddCommand)(char *name, void (*fn)(void));             /* 0x0c */
	void (*Cmd_RemoveCommand)(char *name);                            /* 0x10 */
	int (*Cmd_Argc)(void);                                            /* 0x14 */
	char *(*Cmd_Argv)(int i);                                         /* 0x18 */
	void (*Cmd_ExecuteText)(int exec_when, char *text);               /* 0x1c */
	void (*Con_Printf)(int level, char *fmt, ...);                    /* 0x20 */
	void *(*Z_TagMalloc)(int size, int tag);                          /* 0x24 tag: short in a 4-byte slot */
	void (*Z_Free)(void *p);                                          /* 0x28 */
	int (*FS_LoadFile)(char *name, void **buf, int flags);            /* 0x2c */
	void (*FS_FreeFile)(void *buf);                                   /* 0x30 */
	char *(*FS_Gamedir)(void);                                        /* 0x34 */
	char *(*FS_Userdir)(void);                                        /* 0x38 */
	int (*FS_NextFreeName)(char *path);                               /* 0x3c bool in the low byte */
	sofcvar_t *(*Cvar_Get)(char *name, char *value, int flags, void (*changed)(sofcvar_t *)); /* 0x40 */
	sofcvar_t *(*Cvar_Set)(char *name, char *value);                  /* 0x44 */
	void (*Cvar_SetValue)(char *name, float value);                   /* 0x48 */
	void (*Cvar_AllowHidden)(int allow);                              /* 0x4c */
	int (*Vid_GetModeInfo)(int *width, int *height, int mode);        /* 0x50 bool in the low byte */
	void (*Vid_NewWindow)(int width, int height);                     /* 0x54 */
	void *(*GetLump)(char *mapname, int lump, void *converter);       /* 0x58 */
	float (*TraceFraction)(float *start, float *end);                 /* 0x5c */
	float (*flrand)(float min, float max);                            /* 0x60 */
	int (*irand)(int min, int max);                                   /* 0x64 */
	const float (*dirtable)[3];                                       /* 0x68 data pointer */
} sofrefimport_t;

/* export table (renderer -> engine), 220 bytes */
typedef struct
{
	int api_version;                                                               /* 0x00 */
	int (*Init)(void *hinstance, void *wndproc, void *ghoul);                       /* 0x04 */
	void (*Shutdown)(void);                                                        /* 0x08 */
	void (*TouchAllImages)(void);                                                  /* 0x0c */
	void (*BeginRegistration)(char *mapname);                                      /* 0x10 */
	void *(*RegisterModel)(char *name);                                            /* 0x14 */
	void *(*RegisterSkin)(char *name);                                             /* 0x18 */
	void (*UnregisterSkin)(char *name);                                            /* 0x1c */
	void *(*RegisterPic)(char *name);                                              /* 0x20 */
	void (*UnregisterPic)(char *name);                                             /* 0x24 */
	void *(*RegisterSprite)(char *name);                                           /* 0x28 */
	void (*UnregisterSprite)(char *name);                                          /* 0x2c */
	void (*SetSky)(char *name, float rotate, float *axis);                         /* 0x30 */
	void (*SetCloud)(float type, float density, float angle, float speed,
		float red, float green, float blue);                                       /* 0x34 */
	void (*SetTerrain)(char *name);                                                /* 0x38 */
	void (*EndRegistration)(void);                                                 /* 0x3c */
	void (*RenderFrame)(sofrefdef_t *fd);                                          /* 0x40 */
	void (*DrawGetPicSize)(int *w, int *h, char *name);                            /* 0x44 */
	void (*DrawPic)(int x, int y, char *name, unsigned int rgba);                  /* 0x48 */
	void (*DrawPicScaled)(int x, int y, float sx, float sy, const unsigned char *rgba, char *name); /* 0x4c */
	void (*DrawPicPart)(int x, int y, int left, int top, int right, int bottom,
		const unsigned char *rgba, char *name);                                    /* 0x50 */
	void (*DrawStretchPic)(int x, int y, int w, int h, const unsigned char *rgba,
		char *name, int rotation);                                                 /* 0x54 */
	void (*DrawChar)(int x, int y, int c, const unsigned char *rgba);              /* 0x58 */
	void (*DrawString)(int x, int y, char *s, unsigned int rgba, int maxlen);      /* 0x5c */
	void (*DrawConsoleString)(int x, int y, char *s, int maxlen);                  /* 0x60 */
	void (*DrawConsoleStringColor)(int x, int y, char *s, int maxlen,
		const unsigned char *rgba);                                                /* 0x64 */
	void *unused68;                                                                /* 0x68 */
	void (*DrawCharFloat)(float x, float y, float scale, const unsigned char *rgba, int c); /* 0x6c */
	void (*DrawTileClear)(int x, int y, int w, int h, char *name);                 /* 0x70 */
	void (*DrawFill)(int x, int y, int w, int h, const unsigned char *rgba);       /* 0x74 */
	void (*DrawBevelBox)(int x, int y, int w, int h, const unsigned char *rgba);   /* 0x78 */
	void (*DrawLine)(float x1, float y1, float x2, float y2, float width,
		const unsigned char *rgba);                                                /* 0x7c */
	void *unused80;                                                                /* 0x80 */
	void (*DrawFadeScreen)(const unsigned char *rgba);                             /* 0x84 */
	void (*DrawPolygon)(int unused1, int unused2, int n, sofvec3_t *verts,
		const unsigned char *rgba);                                                /* 0x88 */
	void (*SetClipRect)(int x, int y, int w, int h);                               /* 0x8c */
	void (*ResetClipRect)(void);                                                   /* 0x90 */
	void (*ClipRightTo)(int x);                                                    /* 0x94 */
	void (*ClipLeftFrom)(int x);                                                   /* 0x98 */
	void (*ReapplyClipRect)(void);                                                 /* 0x9c */
	void (*SetRawImage)(char *name, int w, int h, unsigned char *rgba);            /* 0xa0 */
	void (*LightPoint)(float *point, float *color_out, int dir);                   /* 0xa4 */
	void (*DamageTexture)(int texinfo, int unused);                                /* 0xa8 short in a 4-byte slot */
	void (*DrawWorldLabel)(float *origin, char *text, char *pic, unsigned int rgba); /* 0xac */
	void (*BeginFrame)(void);                                                      /* 0xb0 */
	void (*Set2D)(int restore);                                                    /* 0xb4 */
	void (*Set3DInMenu)(int flag);                                                 /* 0xb8 */
	void (*EndFrame)(void);                                                        /* 0xbc */
	void (*SetColorCorrection)(float gamma, float brightness, float contrast, float scale); /* 0xc0 */
	void (*AppActivate)(int active);                                               /* 0xc4 */
	void (*DrawFontString)(int x, int y, char *text, unsigned int rgba,
		const om_fontref_t *font, int keep);                                       /* 0xc8 */
	int (*FontStringWidth)(char *text, const om_fontref_t *font);                  /* 0xcc */
	int (*FontLineHeight)(const om_fontref_t *font);                               /* 0xd0 */
	void (*ReadThumbnail)(unsigned char *out, int w, int h);                       /* 0xd4 */
	void (*A3DExportGeometry)(void *a3d, void *geom, void *material, int *control); /* 0xd8 */
} sofrefexport_t;

#if (defined(_WIN32) && !defined(_WIN64)) || defined(__i386__) /* 32-bit x86 layout */
typedef char sofrefimport_size_check[(sizeof(sofrefimport_t) == 108) ? 1 : -1];
typedef char sofrefexport_size_check[(sizeof(sofrefexport_t) == 220) ? 1 : -1];
typedef char sofrefdef_size_check[(sizeof(sofrefdef_t) == 132) ? 1 : -1];
typedef char sofentity_size_check[(sizeof(sofentity_t) == 96) ? 1 : -1];
typedef char sofalphaquad_size_check[(sizeof(sofalphaquad_t) == 100) ? 1 : -1];
typedef char sofquadchain_size_check[(sizeof(sofquadchain_t) == 40) ? 1 : -1];
typedef char sofcvar_size_check[(sizeof(sofcvar_t) == 32) ? 1 : -1];
typedef char sofrefdef_ofs_check[(offsetof(sofrefdef_t, alphaquads) == 0x7c) ? 1 : -1];
typedef char sofrefexport_ofs_check[(offsetof(sofrefexport_t, A3DExportGeometry) == 0xd8) ? 1 : -1];
#endif

/* The OpenMerc GL3 renderer (ref_gl3.dll / ref_gles3.dll, loaded through the
   Yamagi interface) also exports this function: it returns the SPEC-002 export
   table bound to the running renderer, for engine code that draws through the
   SoF slots before the engine hosts SPEC-002 itself. Init, Shutdown, BeginFrame,
   EndFrame and AppActivate in that table are no-ops there (the Yamagi interface
   owns the window and the frame). */
typedef const sofrefexport_t *(*OM_GetSoFRefExports_t)(void);
#define OM_SOFREF_EXPORTS_NAME "OM_GetSoFRefExports"
/* OpenMerc V1: uniform scale of the SoF 2D slots (HUD scaling, scr_hudscale); a
   second DLL export because the table above is the fixed-size SoF ABI */
typedef void (*OM_Set2DScale_t)(float scale);
#define OM_SOFREF_SETSCALE_NAME "OM_Set2DScale"
/* OpenMerc MENU-SCRATCH: RGBA image with its alpha, kept like a font page (menu
   TrueType glyph atlases, client/menu/m_font.c); rgba NULL only asks whether
   the name is loaded. Returns whether it is loaded afterwards. */
typedef int (*OM_SetAlphaImage_t)(const char *name, int w, int h, const unsigned char *rgba);
#define OM_SOFREF_ALPHAIMAGE_NAME "OM_SetAlphaImage"

#endif
