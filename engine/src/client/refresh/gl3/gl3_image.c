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
 * Texture handling for OpenGL3
 *
 * =======================================================================
 *
 * OpenMerc: Soldier of Fortune image semantics. Names without extension (or
 * ending in .m32) are SoF images: <name>.m32 is loaded with the image types
 * of SPEC-002, alternate (parental lock / texture saving), damaged and detail
 * images, the colour-correction table, mip-level selection, internal formats,
 * per-type filtering and the freeing rules of SPEC-413. Names with another
 * extension keep the Quake II loaders.
 * Implements: SPEC-100, SPEC-002, SPEC-410, SPEC-413
 */

#include "header/local.h"

typedef struct
{
	char *name;
	int minimize, maximize;
} glmode_t;

glmode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

int gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int gl_filter_max = GL_LINEAR;

gl3image_t gl3textures[MAX_GL3TEXTURES];
int numgl3textures = 0;
static int image_max = 0;

/* SPEC-413 §9: colour-correction table applied to images when they are loaded */
byte gl3_colortable[256];

/* SPEC-413 §8 step 3: alternate textures, emptied at BeginRegistration */
#define MAX_SUBST 512
typedef struct
{
	char name[MAX_QPATH];
	gl3image_t *image;
} gl3subst_t;
static gl3subst_t substcache[MAX_SUBST];
static int numsubst;

/* SPEC-413 §8 step 9: images with scrolling detail coordinates */
static gl3image_t *scrolling[MAX_GL3TEXTURES];
static int numscrolling;

static gl3image_t *sof_notexture;

/* extra mip levels to drop for the images loaded now (sky: gl_skymip or rotation, SPEC-410) */
int gl3_sof_extramip;

enum {
	SCRAP_WIDTH = 1024,
	SCRAP_HEIGHT = 1024
};

static short scrap_allocated[SCRAP_WIDTH];
static GLuint gl3_scrap_texnum = 0;
static unsigned scrap_texels[SCRAP_WIDTH * SCRAP_HEIGHT];
qboolean gl3_scrap_dirty = false;

void
GL3_Scrap_Init(void)
{
	memset (scrap_allocated, 0, sizeof(scrap_allocated));	// empty
	memset (scrap_texels, 0, sizeof(scrap_texels));	// transparent

	glGenTextures(1, &gl3_scrap_texnum);
}

/* returns a texture number (relative to g3_scrap_texnum) and the position inside it */
static int
GL3_Scrap_AllocBlock(int w, int h, int *x, int *y, const unsigned *pic)
{
	w += 2;	// add an empty border to all sides
	h += 2;

	int best = SCRAP_HEIGHT;

	for (int i = 0; i < SCRAP_WIDTH - w; i++)
	{
		int j;
		int best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (scrap_allocated[i + j] >= best)
			{
				break;
			}

			if (scrap_allocated[i + j] > best2)
			{
				best2 = scrap_allocated[i + j];
			}
		}

		if (j == w)
		{   /* this is a valid spot */
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > SCRAP_HEIGHT)
	{
		return -1;
	}

	for (int i = 0; i < w; i++)
	{
		scrap_allocated[*x + i] = best + h;
	}
	(*x)++;	// jump the border
	(*y)++;

	int k=0;
	for(int i=0; i < h-2; i++)
	{
		memcpy(&scrap_texels[(*y + i) * SCRAP_WIDTH + *x],
				pic + k, (w - 2) * sizeof(unsigned));
		k += w - 2; // -2 because of w += 2 above
	}

	gl3_scrap_dirty = true;

	return 0;
}

/*
 * OpenMerc: filters of one image by SPEC-413 §8 (pics and fonts nearest; sky,
 * raw and environment images the magnification filter of gl_texturemode; the
 * rest gl_texturemode with anisotropy 2.0 when gl_ext_anisotropic is set).
 * The texture must be bound.
 */
static void
SetImageFilter(gl3image_t *glt)
{
	switch (glt->softype)
	{
		case SOFIMG_PIC:
		case SOFIMG_FONT:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			break;

		case SOFIMG_SKY:
		case SOFIMG_RAW:
		case SOFIMG_ENV:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
			break;

		default:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

			if (gl3config.anisotropic)
			{
				float a = (gl_ext_anisotropic && gl_ext_anisotropic->value) ? 2.0f : 1.0f;

				/* OpenMerc V1: r_anisotropic >= 1 overrides the SoF switch (1 = off, N = N x) */
				if (gl_anisotropic && gl_anisotropic->value >= 1.0f)
				{
					a = gl_anisotropic->value;
				}

				if (gl3config.max_anisotropy >= 1.0f && a > gl3config.max_anisotropy)
				{
					a = gl3config.max_anisotropy;
				}

				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, a);
			}
			break;
	}
}

void
GL3_TextureMode(char *string)
{
	static const int num_modes = ARRLEN(modes);
	int i;

	for (i = 0; i < num_modes; i++)
	{
		if (!Q_stricmp(modes[i].name, string))
		{
			break;
		}
	}

	if (i == num_modes)
	{
		/* SPEC-413: an unknown name changes nothing */
		Com_Printf("Bad filter name\n");
		return;
	}

	gl_filter_min = modes[i].minimize;
	gl_filter_max = modes[i].maximize;

	gl3image_t *glt;

	const char* nolerplist = gl_nolerp_list->string;
	const char* lerplist = r_lerp_list->string;
	qboolean unfiltered2D = r_2D_unfiltered->value != 0;
	GL3_SelectTMU(GL_TEXTURE0);

	/* change all the existing texture objects */
	for (i = 0, glt = gl3textures; i < numgl3textures; i++, glt++)
	{
		qboolean nolerp = false;

		if (glt->texnum == 0)
		{
			continue;
		}

		/* r_2D_unfiltered and gl_nolerp_list allow rendering stuff unfiltered even if gl_filter_* is filtered */
		if (unfiltered2D && glt->type == it_pic)
		{
			// exception to that exception: stuff on the r_lerp_list
			nolerp = (lerplist == NULL) || (strstr(lerplist, glt->name) == NULL);
		}
		else if (nolerplist != NULL && strstr(nolerplist, glt->name) != NULL)
		{
			nolerp = true;
		}

		if ( !GL3_Bind(glt->texnum) )
		{
			continue;	// don't bother changing anything if texture was already set
		}

		if (glt->softype != SOFIMG_INTERNAL)
		{
			/* OpenMerc: SoF images (SPEC-413 §8 filtering table) */
			SetImageFilter(glt);
		}
		else if ((glt->type != it_pic) && (glt->type != it_sky)) /* mipmapped texture */
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
		}
		else /* texture has no mipmaps */
		{
			if (nolerp)
			{
				// this texture shouldn't be filtered at all (no gl_nolerp_list or r_2D_unfiltered case)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}
			else
			{
				// we can't use gl_filter_min which might be GL_*_MIPMAP_*
				// also, there's no anisotropic filtering for textures w/o mipmaps
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
			}
		}
	}
}

/* OpenMerc: re-applies the filters after gl_ext_anisotropic changed */
void
GL3_SoF_ApplyTextureFilters(void)
{
	GL3_TextureMode(gl_texturemode->string);
}

qboolean
GL3_Bind(GLuint texnum)
{
	extern gl3image_t *draw_chars;

	if (gl_nobind->value && draw_chars) /* performance evaluation option */
	{
		texnum = draw_chars->texnum;
	}

	if (gl3state.currenttexture == texnum)
	{
		return false;
	}

	gl3state.currenttexture = texnum;
	GL3_SelectTMU(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texnum);
	return true;
}

void
GL3_BindLightmap(int lightmapnum)
{
	int i=0;
	if(lightmapnum < 0 || lightmapnum >= MAX_LIGHTMAPS)
	{
		Com_Printf("WARNING: Invalid lightmapnum %i used!\n", lightmapnum);
		return;
	}

	if (gl3state.currentlightmap == lightmapnum)
	{
		return;
	}

	gl3state.currentlightmap = lightmapnum;
	for(i=0; i<MAX_LIGHTMAPS_PER_SURFACE; ++i)
	{
		// this assumes that GL_TEXTURE<i+1> = GL_TEXTURE<i> + 1
		// at least for GL_TEXTURE0 .. GL_TEXTURE31 that's true
		GL3_SelectTMU(GL_TEXTURE1+i);
		glBindTexture(GL_TEXTURE_2D, gl3state.lightmap_textureIDs[lightmapnum][i]);
	}

	GL3_DynLight_BindPage(lightmapnum); /* OpenMerc (I4c): dynamic light layer on GL_TEXTURE6 */
}

/*
 * Returns has_alpha
 */
static qboolean
GL3_Upload32(unsigned *data, int width, int height, qboolean mipmap)
{
	qboolean res;

	int i;
	int c = width * height;
	byte *scan = ((byte *)data) + 3;
	int comp = gl3_tex_solid_format;
	int samples = gl3_solid_format;

	for (i = 0; i < c; i++, scan += 4)
	{
		if (*scan != 255)
		{
			samples = gl3_alpha_format;
			comp = gl3_tex_alpha_format;
			break;
		}
	}

	glTexImage2D(GL_TEXTURE_2D, 0, comp, width, height,
	             0, GL_RGBA, GL_UNSIGNED_BYTE, data);

	res = (samples == gl3_alpha_format);

	if (mipmap)
	{
		// TODO: some hardware may require mipmapping disabled for NPOT textures!
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	else // if the texture has no mipmaps, we can't use gl_filter_min which might be GL_*_MIPMAP_*
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}

	return res;
}

/*
 * Returns has_alpha
 */
static qboolean
GL3_Upload8(const byte *data, size_t width, size_t height, qboolean mipmap)
{
	unsigned *trans = NULL;
	qboolean ret;

	trans = R_Convert8to32(data, width, height, d_8to24table);
	if (!trans)
	{
		return false;
	}

	ret = GL3_Upload32(trans, width, height, mipmap);
	free(trans);
	return ret;
}

void
GL3_Scrap_Upload(void)
{
	GL3_Bind(gl3_scrap_texnum);

	GL3_Upload32(scrap_texels, SCRAP_WIDTH, SCRAP_HEIGHT, false);

	if (r_2D_unfiltered->value != 0)
	{
		// 2D textures shouldn't be filtered by default (r_2D_unfiltered),
		// so the scrap shouldn't be filtered
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	else // 2D textures should be filtered by default => filter the scrap
	{
		// we can't use gl_filter_min which might be GL_*_MIPMAP_*
		// also, there's no anisotropic filtering for textures w/o mipmaps
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	gl3_scrap_dirty = false;
}

static gl3image_t *
AllocImageSlot(void)
{
	int i;
	gl3image_t *image;

	/* find a free gl3image_t */
	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (image->texnum == 0 && !image->registration_sequence)
		{
			break;
		}
	}

	if (i == numgl3textures)
	{
		if (numgl3textures == MAX_GL3TEXTURES)
		{
			/* SPEC-002: the renderer holds at most 800 images */
			Com_Error(ERR_DROP, "Max number of allowed textures exceeded");
			return NULL;
		}

		numgl3textures++;
	}

	image = &gl3textures[i];
	memset(image, 0, sizeof(*image));
	return image;
}

/*
 * This is also used as an entry point for the generated r_notexture
 */
gl3image_t *
GL3_LoadPic(char *name, byte *pic, int width, int realwidth,
            int height, int realheight, size_t data_size,
            imagetype_t type, int bits)
{
	gl3image_t *image = NULL;
	GLuint texNum=0;
	int i;

	qboolean nolerp = false;
	qboolean default2Dnolerp = r_2D_unfiltered->value != 0.0f;
	if (default2Dnolerp && type == it_pic)
	{
		// if r_2D_unfiltered is true(ish), nolerp should usually be true,
		// *unless* the texture is on the r_lerp_list
		nolerp = (r_lerp_list->string == NULL) || (strstr(r_lerp_list->string, name) == NULL);
	}
	else if (gl_nolerp_list != NULL && gl_nolerp_list->string != NULL)
	{
		nolerp = strstr(gl_nolerp_list->string, name) != NULL;
	}

	image = AllocImageSlot();

	if (!image)
	{
		return NULL;
	}

	if (strlen(name) >= sizeof(image->name))
	{
		Com_Error(ERR_DROP, "%s: \"%s\" is too long", __func__, name);
		return NULL;
	}

	strcpy(image->name, name);
	image->registration_sequence = registration_sequence;
	image->softype = SOFIMG_INTERNAL;

	image->width = width;
	image->height = height;
	image->type = type;

	if ((type == it_skin) && (bits == 8))
	{
		R_FloodFillSkin(pic, width, height, d_8to24table);
	}

	/* load little pics into the scrap */
	if (nolerp == default2Dnolerp && (image->type == it_pic) &&
		(image->width <= 128) && (image->height <= 128))
	{
		int x=0, y=0;

		/* copy the texels into the scrap block */
		if(bits == 8)
		{
			unsigned *trans = R_Convert8to32(pic, width, height, d_8to24table);
			if (trans)
			{
				int scrapnum = GL3_Scrap_AllocBlock(width, height, &x, &y, trans);
				free(trans);
				if(scrapnum == -1)
					goto nonscrap;
			}
			else
			{
				Sys_Error("Error: Couldn't convert texture '%s' to 32bit?!\n", name);
				return NULL;
			}
		}
		else if(bits == 32)
		{
			if (GL3_Scrap_AllocBlock(image->width, image->height, &x, &y, (unsigned*)pic) == -1)
			{
				goto nonscrap;
			}
		}
		else
		{
			Sys_Error("Error: texture '%s' has %d bits per pixel, only 8 and 32 supported!\n", name, bits);
			return NULL;
		}

		image->texnum = gl3_scrap_texnum;
		image->scrap = true;
		image->has_alpha = true;
		image->sl = (float)x / SCRAP_WIDTH;
		image->sh = (float)(x + image->width) / SCRAP_WIDTH;
		image->tl = (float)y / SCRAP_HEIGHT;
		image->th = (float)(y + image->height) / SCRAP_HEIGHT;
	}
	else
	{
	nonscrap:

		image->is_lava = (strstr(name, "lava") != NULL);
		image->scrap = false;

		glGenTextures(1, &texNum);

		image->texnum = texNum;

		GL3_SelectTMU(GL_TEXTURE0);
		GL3_Bind(texNum);

		if (bits == 8)
		{
			image->has_alpha = GL3_Upload8(pic, width, height,
						(image->type != it_pic && image->type != it_sky));
		}
		else
		{
			image->has_alpha = GL3_Upload32((unsigned *)pic, width, height,
						(image->type != it_pic && image->type != it_sky));
		}

		image->sl = 0;
		image->sh = 1;
		image->tl = 0;
		image->th = 1;

		if (nolerp)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
	}

	if (realwidth && realheight)
	{
		if ((realwidth <= image->width) && (realheight <= image->height))
		{
			image->width = realwidth;
			image->height = realheight;
		}
		else
		{
			Com_DPrintf(
					"Warning, image '%s' has hi-res replacement smaller than the original! (%d x %d) < (%d x %d)\n",
					name, image->width, image->height, realwidth, realheight);
		}
	}

	(void)i;
	return image;
}

/* =====================================================================
 * OpenMerc: Soldier of Fortune images (SPEC-413 §8, §9)
 * ===================================================================== */

/*
 * SPEC-413 §9: T[0] = 0; for i = 1..255: x = (i + 0.5) / 255.5,
 * v = 255 x^(1 - g) + 0.5, k = (1.5 - c)^3 for c < 0.5 else (1.5 - c)^0.5,
 * w = 128 (1 + s (|v - 128| / 128)^k), T[i] = (w + 160 b - 80) d clamped and
 * truncated.
 */
void
GL3_SoF_BuildColorTable(float g, float b, float c, float d)
{
	int i;
	double k = (c < 0.5f) ? pow(1.5 - c, 3.0) : pow(1.5 - c, 0.5);

	gl3_colortable[0] = 0;

	for (i = 1; i < 256; i++)
	{
		double x = (i + 0.5) / 255.5;
		double v = 255.0 * pow(x, 1.0 - g) + 0.5;
		double s = (v >= 128.0) ? 1.0 : -1.0;
		double w = 128.0 * (1.0 + s * pow(fabs(v - 128.0) / 128.0, k));
		double t = (w + 160.0 * b - 80.0) * d;

		if (t < 0)
		{
			t = 0;
		}
		else if (t > 255)
		{
			t = 255;
		}

		gl3_colortable[i] = (byte)t;
	}
}

/*
 * SPEC-410 lightmap building stores every lightmap texel through the colour
 * table above (gl_monolightmap 0). The world keeps one lightmap per light
 * style and sums them in the shader, so the table is applied there, to the
 * sum, through this 256 x 1 lookup texture on GL_TEXTURE5. With r_fullbright
 * or night vision (no lightmaps) every entry is 255.
 */
static GLuint gl3_lighttable_tex;
static byte gl3_lighttable_sent[256];

void
GL3_SoF_BindLightTable(qboolean fullbright)
{
	byte table[256];
	GLenum tmu = gl3state.currenttmu ? gl3state.currenttmu : GL_TEXTURE0;
	qboolean upload = false;

	gl3state.currenttmu = tmu;

	if (fullbright)
	{
		memset(table, 255, sizeof(table));
	}
	else
	{
		memcpy(table, gl3_colortable, sizeof(table));
	}

	if (!gl3_lighttable_tex)
	{
		glGenTextures(1, &gl3_lighttable_tex);
		upload = true;
	}
	else if (memcmp(table, gl3_lighttable_sent, sizeof(table)))
	{
		upload = true;
	}

	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, gl3_lighttable_tex);

	if (upload)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 1, 0, GL_RED, GL_UNSIGNED_BYTE, table);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		memcpy(gl3_lighttable_sent, table, sizeof(table));
	}

	glActiveTexture(tmu);
}

/* SPEC-413 internal format names (case-insensitive) and their GL values */
typedef struct
{
	const char *name;
	int value;
} gl3format_t;

static const gl3format_t sofformats[] = {
	{"GL_SOLID_FORMAT", 3}, {"GL_ALPHA_FORMAT", 4},
	{"GL_RGB_S3TC", 0x83A0}, {"GL_RGB4_S3TC", 0x83A1}, {"GL_RGBA_S3TC", 0x83A2}, {"GL_RGBA4_S3TC", 0x83A3},
	{"GL_COMPRESSED_RGB_S3TC_DXT1_EXT", 0x83F0}, {"GL_COMPRESSED_RGBA_S3TC_DXT1_EXT", 0x83F1},
	{"GL_COMPRESSED_RGBA_S3TC_DXT3_EXT", 0x83F2}, {"GL_COMPRESSED_RGBA_S3TC_DXT5_EXT", 0x83F3},
	{"GL_ALPHA4", 0x803B}, {"GL_ALPHA8", 0x803C}, {"GL_ALPHA12", 0x803D}, {"GL_ALPHA16", 0x803E},
	{"GL_LUMINANCE4", 0x803F}, {"GL_LUMINANCE8", 0x8040}, {"GL_LUMINANCE12", 0x8041}, {"GL_LUMINANCE16", 0x8042},
	{"GL_LUMINANCE4_ALPHA4", 0x8043}, {"GL_LUMINANCE6_ALPHA2", 0x8044}, {"GL_LUMINANCE8_ALPHA8", 0x8045},
	{"GL_LUMINANCE12_ALPHA4", 0x8046}, {"GL_LUMINANCE12_ALPHA12", 0x8047}, {"GL_LUMINANCE16_ALPHA16", 0x8048},
	{"GL_INTENSITY", 0x8049}, {"GL_INTENSITY4", 0x804A}, {"GL_INTENSITY8", 0x804B}, {"GL_INTENSITY12", 0x804C},
	{"GL_INTENSITY16", 0x804D}, {"GL_R3_G3_B2", 0x2A10}, {"GL_RGB4", 0x804F}, {"GL_RGB5", 0x8050},
	{"GL_RGB8", 0x8051}, {"GL_RGB10", 0x8052}, {"GL_RGB12", 0x8053}, {"GL_RGB16", 0x8054},
	{"GL_RGBA2", 0x8055}, {"GL_RGBA4", 0x8056}, {"GL_RGB5_A1", 0x8057}, {"GL_RGBA8", 0x8058},
	{"GL_RGB10_A2", 0x8059}, {"GL_RGBA12", 0x805A}, {"GL_RGBA16", 0x805B}
};

static int
FormatValue(const char *name)
{
	int i;

	for (i = 0; i < (int)ARRLEN(sofformats); i++)
	{
		if (!Q_stricmp(sofformats[i].name, name))
		{
			return sofformats[i].value;
		}
	}

	return -1;
}

/*
 * SPEC-413 / SPEC-AMEND-002 1.1: an unknown r_isf / r_iaf name prints the list
 * of valid names and resets the cvar. Returns true if the value was valid.
 */
qboolean
GL3_SoF_CheckFormatCvar(cvar_t *cvar, const char *def)
{
	int i;

	if (!cvar || (FormatValue(cvar->string) >= 0))
	{
		return true;
	}

	Com_Printf("Avaiable formats: \n\n");

	for (i = 0; i < (int)ARRLEN(sofformats); i++)
	{
		Com_Printf("%s\n", sofformats[i].name);
	}

	ri.Cvar_Set(cvar->name, (char *)def);
	return false;
}

/*
 * Internal GL format used for an upload. The core profile has no luminance,
 * intensity or alpha-only formats; those, and the unsized "solid"/"alpha"
 * values 3/4, become RGB8/RGBA8 (the picture is the same: images without the
 * alpha flag are opaque anyway). Compressed formats are used when the driver
 * offers S3TC.
 */
static GLint
InternalFormat(qboolean has_alpha)
{
	cvar_t *cv = has_alpha ? r_iaf : r_isf;
	int v = cv ? FormatValue(cv->string) : -1;

	if ((v >= 0x83F0) && (v <= 0x83F3) && gl3config.s3tc)
	{
		if (!has_alpha)
		{
			return 0x83F0; /* GL_COMPRESSED_RGB_S3TC_DXT1_EXT */
		}

		return v;
	}

	return has_alpha ? GL_RGBA8 : GL_RGB8;
}

static int
M32Int(const byte *m, int ofs)
{
	return LittleLong(*(const int *)(m + ofs));
}

static float
M32Float(const byte *m, int ofs)
{
	return LittleFloat(*(const float *)(m + ofs));
}

static void
M32String(const byte *m, int ofs, char *out, size_t size)
{
	size_t n = 0;

	while ((n < 127) && (n + 1 < size) && m[ofs + n])
	{
		out[n] = m[ofs + n];
		n++;
	}

	out[n] = 0;
}

static gl3image_t *
FindLoaded(const char *name)
{
	int i;
	gl3image_t *image;

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (image->registration_sequence && (image->softype != SOFIMG_INTERNAL) &&
			!strcmp(name, image->name))
		{
			return image;
		}
	}

	return NULL;
}

static void
MarkUsed(gl3image_t *image)
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

static void
FreeImage(gl3image_t *image)
{
	int i;

	if (image->texnum && !image->scrap)
	{
		glDeleteTextures(1, &image->texnum);
	}

	for (i = 0; i < numscrolling; i++)
	{
		if (scrolling[i] == image)
		{
			scrolling[i] = scrolling[--numscrolling];
			break;
		}
	}

	/* forget references from other images */
	for (i = 0; i < numgl3textures; i++)
	{
		if (gl3textures[i].detail == image)
		{
			gl3textures[i].detail = NULL;
		}

		if (gl3textures[i].damage == image)
		{
			gl3textures[i].damage = NULL;
		}
	}

	for (i = 0; i < numsubst; i++)
	{
		if (substcache[i].image == image)
		{
			substcache[i] = substcache[--numsubst];
			i--;
		}
	}

	if (gl3state.currenttexture == image->texnum)
	{
		gl3state.currenttexture = 0;
	}

	memset(image, 0, sizeof(*image));
}

/*
 * Uploads the mip levels of a .m32 starting at the level chosen by
 * SPEC-413 §8 (gl_picmip, texture saving, driver limit, kind bit 0x1).
 */
static void
UploadM32(gl3image_t *image, byte *m, int size, int kind, int pictipskip)
{
	int numlevels, start, level, maxsize = 0;
	GLint ifmt = InternalFormat(image->has_alpha);
	GLenum srcfmt = GL_RGBA;

	for (numlevels = 0; numlevels < M32_MIP_LEVELS; numlevels++)
	{
		int w = M32Int(m, M32_OFS_WIDTHS + 4 * numlevels);
		int h = M32Int(m, M32_OFS_HEIGHTS + 4 * numlevels);
		int ofs = M32Int(m, M32_OFS_OFFSETS + 4 * numlevels);

		if ((w <= 0) || (h <= 0) || (ofs <= 0) || ((size_t)ofs + (size_t)w * h * 4 > (size_t)size))
		{
			break;
		}
	}

	if (numlevels == 0)
	{
		return;
	}

	start = (int)gl_picmip->value + gl3_sof_extramip;

	if (gl_pictip->value)
	{
		start += pictipskip;
	}

	if ((start < 0) || (start > 14))
	{
		start = 0;
	}

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsize);

	while ((start < numlevels) && (maxsize > 0) &&
		((M32Int(m, M32_OFS_WIDTHS + 4 * start) > maxsize) ||
		 (M32Int(m, M32_OFS_HEIGHTS + 4 * start) > maxsize)))
	{
		start++;
	}

	if (start >= numlevels)
	{
		start = numlevels - 1;
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	for (level = start; level < numlevels; level++)
	{
		int w = M32Int(m, M32_OFS_WIDTHS + 4 * level);
		int h = M32Int(m, M32_OFS_HEIGHTS + 4 * level);
		int ofs = M32Int(m, M32_OFS_OFFSETS + 4 * level);

		glTexImage2D(GL_TEXTURE_2D, level - start, ifmt, w, h, 0, srcfmt, GL_UNSIGNED_BYTE, m + ofs);

		if (glGetError() != GL_NO_ERROR)
		{
			/* SPEC-413 §8 step 6 */
			if (image->has_alpha)
			{
				Com_Printf("Unsupported format : restoring GL_ALPHA_FORMAT\n");
				ri.Cvar_Set("r_iaf", "GL_ALPHA_FORMAT");
				ifmt = GL_RGBA8;
			}
			else
			{
				Com_Printf("Unsupported format : restoring GL_SOLID_FORMAT\n");
				ri.Cvar_Set("r_isf", "GL_SOLID_FORMAT");
				ifmt = GL_RGB8;
			}

			glTexImage2D(GL_TEXTURE_2D, level - start, ifmt, w, h, 0, srcfmt, GL_UNSIGNED_BYTE, m + ofs);
		}

		if (kind & 0x1)
		{
			level++;
			break; /* only level s */
		}
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level - start - 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

/* SPEC-413 §9: colour correction of every level, except detail and environment images */
static void
CorrectColours(byte *m, int size, int softype)
{
	int level;

	for (level = 0; level < M32_MIP_LEVELS; level++)
	{
		int w = M32Int(m, M32_OFS_WIDTHS + 4 * level);
		int h = M32Int(m, M32_OFS_HEIGHTS + 4 * level);
		int ofs = M32Int(m, M32_OFS_OFFSETS + 4 * level);
		byte *p;
		int i, n;

		if ((w <= 0) || (h <= 0) || (ofs <= 0) || ((size_t)ofs + (size_t)w * h * 4 > (size_t)size))
		{
			break;
		}

		p = m + ofs;
		n = w * h;

		if (softype == SOFIMG_DETAIL)
		{
			/* uploaded as luminance from the red channel (SPEC-413 §8 step 5) */
			for (i = 0; i < n; i++, p += 4)
			{
				p[1] = p[2] = p[0];
				p[3] = 255;
			}
		}
		else if (softype != SOFIMG_ENV)
		{
			for (i = 0; i < n; i++, p += 4)
			{
				p[0] = gl3_colortable[p[0]];
				p[1] = gl3_colortable[p[1]];
				p[2] = gl3_colortable[p[2]];
			}
		}
	}
}

static gl3image_t *LoadSoFImage(const char *name, int softype, qboolean substitute);

/* SPEC-413 §8 lookup; create = load the file if it is not loaded yet */
gl3image_t *
GL3_SoF_FindImage(const char *name, int softype, qboolean create)
{
	gl3image_t *image;
	int i;
	size_t len;

	if (!name)
	{
		Com_Printf("GL_FindImage: Invalid null name\n");
		return NULL;
	}

	len = strlen(name);

	if (len == 0)
	{
		Com_Printf("GL_FindImage: Name (%s) too short\n", name);
		return NULL;
	}

	if (len >= 63)
	{
		Com_Printf("GL_FindImage: Name (%s) too long\n", name);
		return NULL;
	}

	image = FindLoaded(name);

	if (image)
	{
		MarkUsed(image);
		return image;
	}

	for (i = 0; i < numsubst; i++)
	{
		if (!strcmp(substcache[i].name, name))
		{
			MarkUsed(substcache[i].image);
			return substcache[i].image;
		}
	}

	if (!create)
	{
		return NULL;
	}

	return LoadSoFImage(name, softype, true);
}

static gl3image_t *
LoadSoFImage(const char *name, int softype, qboolean substitute)
{
	char path[MAX_QPATH + 8], other[MAX_QPATH + 16], tmp[128];
	byte *m = NULL;
	int size, kind, flags, value, pictipskip;
	gl3image_t *image;

	Com_sprintf(path, sizeof(path), "%s.m32", name);
	size = ri.FS_LoadFile(path, (void **)&m);

	if (!m || (size < M32_HEADER_SIZE))
	{
		if (m)
		{
			ri.FS_FreeFile(m);
		}

		Com_Printf("GL_FindImage: Can't load (%s)\n", name);
		return NULL;
	}

	if (M32Int(m, 0) != M32_VERSION)
	{
		ri.FS_FreeFile(m);
		Com_Printf("GL_FindImage: Invalid version for (%s)\n", name);
		return NULL;
	}

	kind = M32Int(m, M32_OFS_KIND);
	flags = M32Int(m, M32_OFS_FLAGS);
	value = M32Int(m, M32_OFS_VALUE);
	pictipskip = M32Int(m, M32_OFS_PICTIPSKIP);

	/* type override from the .m32 kind: 0x2 detail, 0x8 environment map (wins) */
	if (kind & 0x8)
	{
		softype = SOFIMG_ENV;
	}
	else if (kind & 0x2)
	{
		softype = SOFIMG_DETAIL;
	}

	/* alternate texture (texture saving, gl_pictip) */
	M32String(m, M32_OFS_ALTNAME, tmp, sizeof(tmp));

	if (tmp[0] && substitute &&
		gl_pictip->value)
	{
		gl3image_t *alt;

		ri.FS_FreeFile(m);
		Com_sprintf(other, sizeof(other), "textures/%s", tmp);
		alt = FindLoaded(other);

		if (alt)
		{
			MarkUsed(alt);
		}
		else
		{
			alt = LoadSoFImage(other, softype, false);
		}

		if (!alt)
		{
			alt = GL3_SoF_NoTexture();
		}

		if (alt && (numsubst < MAX_SUBST))
		{
			Q_strlcpy(substcache[numsubst].name, name, sizeof(substcache[numsubst].name));
			substcache[numsubst].image = alt;
			numsubst++;
		}

		return alt;
	}

	CorrectColours(m, size, softype);

	image = AllocImageSlot();

	if (!image)
	{
		ri.FS_FreeFile(m);
		return NULL;
	}

	Q_strlcpy(image->name, name, sizeof(image->name));
	image->registration_sequence = registration_sequence;
	image->softype = softype;
	image->type = (softype == SOFIMG_PIC || softype == SOFIMG_FONT) ? it_pic :
		(softype == SOFIMG_SKY) ? it_sky : (softype == SOFIMG_SKIN) ? it_skin :
		(softype == SOFIMG_SPRITE) ? it_sprite : it_wall;
	image->width = M32Int(m, M32_OFS_WIDTHS);
	image->height = M32Int(m, M32_OFS_HEIGHTS);
	image->m32flags = flags;
	image->kind = kind;
	image->animrate = value & 0xFF;
	image->has_alpha = (flags & SOFSURF_ALPHA) != 0;
	image->sl = image->tl = 0;
	image->sh = image->th = 1;

	glGenTextures(1, &image->texnum);
	GL3_SelectTMU(GL_TEXTURE0);
	GL3_Bind(image->texnum);
	UploadM32(image, m, size, kind, pictipskip);
	SetImageFilter(image);

	/* damaged variant */
	M32String(m, M32_OFS_DAMAGENAME, tmp, sizeof(tmp));

	if (tmp[0])
	{
		Com_sprintf(other, sizeof(other), "textures/%s", tmp);

		if (!Q_stricmp(other, name))
		{
			Com_Printf("GL_FindImage: Recursvive damage texture (%s)\n", name);
		}
		else
		{
			image->damage = GL3_SoF_FindImage(other, SOFIMG_WALL, true);
		}
	}

	/* second-pass (detail or environment) texture */
	M32String(m, M32_OFS_DETAILNAME, tmp, sizeof(tmp));

	if (tmp[0])
	{
		Com_sprintf(other, sizeof(other), "textures/%s", tmp);

		if (!Q_stricmp(other, name))
		{
			Com_Printf("GL_FindImage: Recursvive detail texture (%s)\n", name);
		}
		else
		{
			image->detail = GL3_SoF_FindImage(other, SOFIMG_DETAIL, true);
			image->detail_scale[0] = M32Float(m, M32_OFS_DETAILSCALE);
			image->detail_scale[1] = M32Float(m, M32_OFS_DETAILSCALE + 4);
			image->detail_shift[0] = M32Float(m, M32_OFS_DETAILSHIFT);
			image->detail_shift[1] = M32Float(m, M32_OFS_DETAILSHIFT + 4);

			if (((image->detail_shift[0] != 0) || (image->detail_shift[1] != 0)) &&
				(numscrolling < MAX_GL3TEXTURES))
			{
				scrolling[numscrolling++] = image;
			}
		}
	}

	ri.FS_FreeFile(m);
	return image;
}

/* every 3D frame: scroll offset += clock delta x shift, fractional part above 1 */
void
GL3_SoF_UpdateDetailScroll(float delta)
{
	int i, j;

	for (i = 0; i < numscrolling; i++)
	{
		gl3image_t *image = scrolling[i];

		for (j = 0; j < 2; j++)
		{
			image->detail_scroll[j] += delta * image->detail_shift[j];

			if (image->detail_scroll[j] > 1.0f)
			{
				image->detail_scroll[j] -= (float)(int)image->detail_scroll[j];
			}
		}
	}
}

/* SPEC-002 default image pics/console/notexture (8 x 8), or the generated one */
gl3image_t *
GL3_SoF_NoTexture(void)
{
	if (!sof_notexture || !sof_notexture->texnum)
	{
		sof_notexture = GL3_SoF_FindImage("pics/console/notexture", SOFIMG_PIC, true);
	}

	return sof_notexture ? sof_notexture : gl3_notexture;
}

void
GL3_SoF_InitImages(void)
{
	numsubst = 0;
	numscrolling = 0;
	sof_notexture = NULL;
}

/* SPEC-002 BeginRegistration: the substitution cache is emptied */
void
GL3_SoF_BeginRegistrationImages(void)
{
	numsubst = 0;
}

/* SPEC-002 slots 0x1c / 0x24 / 0x2c: free the named image if it has that type */
void
GL3_SoF_FreeImageByName(const char *name, int softype)
{
	gl3image_t *image;

	if (!name)
	{
		return;
	}

	image = FindLoaded(name);

	if (image && (image->softype == softype) && (image != sof_notexture))
	{
		FreeImage(image);
	}
}

/* SPEC-002 slot 0xa0: raw RGBA image, uploaded as RGB, one level */
void
GL3_SoF_SetRawImage(const char *name, int w, int h, const byte *rgba)
{
	gl3image_t *image;

	if (!name || !name[0] || !rgba || (w <= 0) || (h <= 0))
	{
		return;
	}

	image = FindLoaded(name);

	if (image && (image->softype != SOFIMG_RAW))
	{
		FreeImage(image);
		image = NULL;
	}

	if (!image)
	{
		image = AllocImageSlot();

		if (!image)
		{
			return;
		}

		Q_strlcpy(image->name, name, sizeof(image->name));
		image->softype = SOFIMG_RAW;
		image->type = it_pic;
		glGenTextures(1, &image->texnum);
	}

	image->registration_sequence = registration_sequence;
	image->width = w & 0xFFFF;
	image->height = h & 0xFFFF;
	image->sl = image->tl = 0;
	image->sh = image->th = 1;

	GL3_SelectTMU(GL_TEXTURE0);
	GL3_Bind(image->texnum);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	SetImageFilter(image);
}

/*
 * OpenMerc MENU-SCRATCH (menu TrueType text, client/menu/m_font.c): an RGBA
 * image WITH its alpha, kept like a font page (SOFIMG_FONT: nearest-sampled,
 * not freed at the end of a registration cycle). Not a SPEC-002 slot; reached
 * through the OM_SetAlphaImage DLL export.
 */
void
GL3_SoF_SetAlphaImage(const char *name, int w, int h, const byte *rgba)
{
	gl3image_t *image;

	if (!name || !name[0] || !rgba || (w <= 0) || (h <= 0))
	{
		return;
	}

	image = FindLoaded(name);

	if (image && (image->softype != SOFIMG_FONT))
	{
		FreeImage(image);
		image = NULL;
	}

	if (!image)
	{
		image = AllocImageSlot();

		if (!image)
		{
			return;
		}

		Q_strlcpy(image->name, name, sizeof(image->name));
		image->softype = SOFIMG_FONT;
		image->type = it_pic;
		glGenTextures(1, &image->texnum);
	}

	image->registration_sequence = registration_sequence;
	image->width = w & 0xFFFF;
	image->height = h & 0xFFFF;
	image->sl = image->tl = 0;
	image->sh = image->th = 1;

	GL3_SelectTMU(GL_TEXTURE0);
	GL3_Bind(image->texnum);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	SetImageFilter(image);
}

/* MENU-SCRATCH: is an image of this name loaded (an alpha image survives a
   registration cycle, not a renderer restart) */
qboolean
GL3_SoF_ImageLoaded(const char *name)
{
	return name && FindLoaded(name) != NULL;
}

/* SPEC-002 slot 0x0c: bind every image once */
void
GL3_SoF_TouchAllImages(void)
{
	int i;

	for (i = 0; i < numgl3textures; i++)
	{
		if (gl3textures[i].texnum)
		{
			GL3_Bind(gl3textures[i].texnum);
		}
	}
}

/*
 * SPEC-002 / SPEC-413: at the end of a registration cycle raw images are
 * always freed, other images not used in the cycle are freed except pics and
 * fonts; the default notexture image is kept.
 */
void
GL3_SoF_FreeUnusedImages(void)
{
	int i;
	gl3image_t *image;

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (!image->registration_sequence || (image->softype == SOFIMG_INTERNAL))
		{
			continue;
		}

		if (image == sof_notexture)
		{
			continue;
		}

		if (image->softype == SOFIMG_RAW)
		{
			FreeImage(image);
			continue;
		}

		if ((image->softype == SOFIMG_PIC) || (image->softype == SOFIMG_FONT))
		{
			continue;
		}

		if (image->registration_sequence != registration_sequence)
		{
			FreeImage(image);
		}
	}
}

/*
 * Finds or loads the given image or NULL
 */
gl3image_t *
GL3_FindImage(const char *originname, imagetype_t type)
{
	char namewe[256], name[256] = {0};
	gl3image_t *image;
	const char* ext;
	size_t len;
	int i;

	if (!originname)
	{
		return NULL;
	}

	Q_strlcpy(name, originname, sizeof(name));

	/* fix backslashes */
	Q_replacebackslash(name);

	ext = COM_FileExtension(name);

	/* OpenMerc: SoF images are named without extension (or with .m32) */
	if (!ext[0] || !Q_stricmp(ext, "m32"))
	{
		static const int types[] = {SOFIMG_SKIN, SOFIMG_SPRITE, SOFIMG_WALL, SOFIMG_PIC, SOFIMG_SKY};

		if (ext[0])
		{
			name[(ext - name) - 1] = 0;
		}

		return GL3_SoF_FindImage(name, types[type], true);
	}

	/* Remove the extension */
	len = (ext - name) - 1;
	if ((len < 1) || (len > sizeof(namewe) - 1))
	{
		Com_DPrintf("%s: Bad filename %s\n", __func__, name);
		return NULL;
	}

	memcpy(namewe, name, len);
	namewe[len] = 0;

	/* look for it */
	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (image->registration_sequence && !strcmp(name, image->name))
		{
			image->registration_sequence = registration_sequence;
			return image;
		}
	}

	//
	// load the pic from disk
	//
	image = (gl3image_t *)R_LoadImage(name, namewe, ext, type,
		r_retexturing->value, (loadimage_t)GL3_LoadPic);

	if (!image && r_validation->value)
	{
		Com_Printf("%s: can't load %s\n", __func__, name);
	}

	return image;
}

gl3image_t *
GL3_RegisterSkin(const char *name)
{
	return GL3_FindImage(name, it_skin);
}

/*
 * Any image that was not touched on
 * this registration sequence
 * will be freed.
 */
void
GL3_FreeUnusedImages(void)
{
	int i;
	gl3image_t *image;

	/* never free r_notexture or particle texture */
	gl3_notexture->registration_sequence = registration_sequence;
	gl3_particletexture->registration_sequence = registration_sequence;

	/* OpenMerc: SoF images follow SPEC-413 */
	GL3_SoF_FreeUnusedImages();

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (image->softype != SOFIMG_INTERNAL)
		{
			continue;
		}

		if (image->registration_sequence == registration_sequence)
		{
			continue; /* used this sequence */
		}

		if (!image->registration_sequence)
		{
			continue; /* free image_t slot */
		}

		if (image->type == it_pic)
		{
			continue; /* don't free pics */
		}

		/* free it */
		FreeImage(image);
	}
}

qboolean
GL3_ImageHasFreeSpace(void)
{
	int		i, used;
	gl3image_t	*image;

	used = 0;

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (!image->name[0])
			continue;
		if (image->registration_sequence == registration_sequence)
		{
			used ++;
		}
	}

	if (image_max < used)
	{
		image_max = used;
	}

	// should same size of free slots as currently used
	return (numgl3textures + used) < MAX_GL3TEXTURES;
}

void
GL3_ShutdownImages(void)
{
	int i;
	gl3image_t *image;

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (!image->registration_sequence)
		{
			continue; /* free image_t slot */
		}

		/* free it */
		if (image->texnum && !image->scrap)
		{
			glDeleteTextures(1, &image->texnum);
		}

		memset(image, 0, sizeof(*image));
	}

	numgl3textures = 0;
	numsubst = 0;
	numscrolling = 0;
	sof_notexture = NULL;

	glDeleteTextures(1, &gl3_scrap_texnum);

	if (gl3_lighttable_tex)
	{
		glDeleteTextures(1, &gl3_lighttable_tex);
		gl3_lighttable_tex = 0;
	}
}

/* SPEC-413 imagelist */
void
GL3_ImageList_f(void)
{
	int i, texels = 0, count = 0;
	gl3image_t *image;
	static const char letters[] = "MSWP D F ";

	Com_Printf("M = Skin, S = Sprite, W = wall, P = Pic, D = Detail. F = Font\n");
	Com_Printf("------------------\n");

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		char letter;

		if (!image->registration_sequence || (image->texnum == 0))
		{
			continue;
		}

		letter = (image->softype < SOFIMG_INTERNAL) ? letters[image->softype] : ' ';
		texels += image->width * image->height;
		count++;
		Com_Printf("%c %3i %3i RGB: %s\n", letter, image->width, image->height, image->name);
	}

	Com_Printf("Total texel count   : %i\n", texels);
	Com_Printf("Total image count   : %i\n", count);
}
