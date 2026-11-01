/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2015 Daniel Gibson
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
 * File formats supported by stb_image, for now only tga, png, jpg
 * See also https://github.com/nothings/stb
 *
 * =======================================================================
 *
 * OpenMerc: R_FindPic() also looks for Soldier of Fortune .m32 pictures
 * in pics/console/ and pics/. Implements: SPEC-100
 */

#include <stdlib.h>

#include "../ref_shared.h"

// don't need HDR stuff
#define STBI_NO_LINEAR
#define STBI_NO_HDR
// make sure STB_image uses standard malloc(), as we'll use standard free() to deallocate
#define STBI_MALLOC(sz)    malloc(sz)
#define STBI_REALLOC(p,sz) realloc(p,sz)
#define STBI_FREE(p)       free(p)
// Switch of the thread local stuff. Breaks mingw under Windows.
#define STBI_NO_THREAD_LOCALS
// include implementation part of stb_image into this file
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// include resize implementation
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

/*
 * Add extension to file name
 */
void
FixFileExt(const char *origname, const char *ext, char *filename, size_t size)
{
	Q_strlcpy(filename, origname, size);

	/* Add the extension */
	if (strcmp(COM_FileExtension(filename), ext))
	{
		Q_strlcat(filename, ".", size);
		Q_strlcat(filename, ext, size);
	}
}

/*
 * origname: the filename to be opened, might be without extension
 * type: extension of the type we wanna open ("jpg", "png" or "tga")
 * pic: pointer RGBA pixel data will be assigned to
 */
static qboolean
LoadSTB(const char *origname, const char* type, byte **pic, int *width, int *height)
{
	char filename[256];

	FixFileExt(origname, type, filename, sizeof(filename));

	*pic = NULL;

	byte* rawdata = NULL;
	int rawsize = ri.FS_LoadFile(filename, (void **)&rawdata);
	if (rawdata == NULL)
	{
		return false;
	}

	int w, h, bytesPerPixel;
	byte* data = NULL;
	data = stbi_load_from_memory(rawdata, rawsize, &w, &h, &bytesPerPixel, STBI_rgb_alpha);
	if (data == NULL)
	{
		Com_Printf("%s couldn't load data from %s: %s!\n", __func__, filename, stbi_failure_reason());
		ri.FS_FreeFile(rawdata);
		return false;
	}

	ri.FS_FreeFile(rawdata);

	Com_DPrintf("%s() loaded: %s\n", __func__, filename);

	*pic = data;
	*width = w;
	*height = h;
	return true;
}

/*
 * OpenMerc: DirectDraw Surface replacement textures (issue #5): the top mip level of
 * an uncompressed 24/32-bit or a DXT1/DXT3/DXT5 compressed .dds becomes RGBA
 */
static unsigned
DDS_U32(const byte *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void
DDS_Color565(unsigned c, byte *out)
{
	out[0] = (byte)(((c >> 11) & 31) * 255 / 31);
	out[1] = (byte)(((c >> 5) & 63) * 255 / 63);
	out[2] = (byte)((c & 31) * 255 / 31);
	out[3] = 255;
}

/* one 4x4 block: format 1 DXT1, 3 DXT3, 5 DXT5 */
static void
DDS_DecodeBlock(const byte *b, int format, byte rgba[16][4])
{
	const byte *cb = (format == 1) ? b : b + 8;
	unsigned c0 = cb[0] | (cb[1] << 8), c1 = cb[2] | (cb[3] << 8);
	unsigned bits = DDS_U32(cb + 4);
	byte pal[4][4];
	int i;

	DDS_Color565(c0, pal[0]);
	DDS_Color565(c1, pal[1]);

	if ((format != 1) || (c0 > c1))
	{
		for (i = 0; i < 3; i++)
		{
			pal[2][i] = (byte)((2 * pal[0][i] + pal[1][i]) / 3);
			pal[3][i] = (byte)((pal[0][i] + 2 * pal[1][i]) / 3);
		}

		pal[2][3] = pal[3][3] = 255;
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			pal[2][i] = (byte)((pal[0][i] + pal[1][i]) / 2);
			pal[3][i] = 0;
		}

		pal[2][3] = 255;
		pal[3][3] = 0; /* DXT1 one-bit alpha */
	}

	for (i = 0; i < 16; i++)
	{
		memcpy(rgba[i], pal[(bits >> (2 * i)) & 3], 4);
	}

	if (format == 3)
	{
		for (i = 0; i < 16; i++)
		{
			rgba[i][3] = (byte)(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
		}
	}
	else if (format == 5)
	{
		unsigned a0 = b[0], a1 = b[1], a[8];
		unsigned long long abits = 0;

		for (i = 0; i < 6; i++)
		{
			abits |= (unsigned long long)b[2 + i] << (8 * i);
		}

		a[0] = a0;
		a[1] = a1;

		for (i = 1; i < 7; i++)
		{
			a[i + 1] = (a0 > a1) ? ((7 - i) * a0 + i * a1) / 7 : 0;
		}

		if (a0 <= a1)
		{
			for (i = 1; i < 5; i++)
			{
				a[i + 1] = ((5 - i) * a0 + i * a1) / 5;
			}

			a[6] = 0;
			a[7] = 255;
		}

		for (i = 0; i < 16; i++)
		{
			rgba[i][3] = (byte)a[(abits >> (3 * i)) & 7];
		}
	}
}

static qboolean
LoadDDS(const char *origname, byte **pic, int *width, int *height)
{
	char filename[256];
	byte *raw = NULL, *out;
	int len, w, h, format = 0, bpp = 0, x, y;
	unsigned pfflags, fourcc, rmask = 0, gmask = 0, bmask = 0, amask = 0;
	const byte *p;

	*pic = NULL;
	FixFileExt(origname, "dds", filename, sizeof(filename));
	len = ri.FS_LoadFile(filename, (void **)&raw);

	if (!raw)
	{
		return false;
	}

	if ((len < 128) || memcmp(raw, "DDS ", 4) || (DDS_U32(raw + 4) != 124))
	{
		Com_Printf("%s: %s is not a DDS file\n", __func__, filename);
		ri.FS_FreeFile(raw);
		return false;
	}

	h = (int)DDS_U32(raw + 12);
	w = (int)DDS_U32(raw + 16);
	pfflags = DDS_U32(raw + 80);
	fourcc = DDS_U32(raw + 84);

	if (pfflags & 0x4)
	{
		format = !memcmp(raw + 84, "DXT1", 4) ? 1 : !memcmp(raw + 84, "DXT3", 4) ? 3 :
			!memcmp(raw + 84, "DXT5", 4) ? 5 : 0;
	}
	else if (pfflags & 0x40)
	{
		bpp = (int)DDS_U32(raw + 88);
		rmask = DDS_U32(raw + 92);
		gmask = DDS_U32(raw + 96);
		bmask = DDS_U32(raw + 100);
		amask = (pfflags & 0x1) ? DDS_U32(raw + 104) : 0;
	}

	if ((w <= 0) || (h <= 0) || (w > 8192) || (h > 8192) || (!format && (bpp != 24) && (bpp != 32)))
	{
		Com_Printf("%s: %s has an unsupported format (fourcc %08x, %d bits)\n", __func__, filename, fourcc, bpp);
		ri.FS_FreeFile(raw);
		return false;
	}

	if ((format && (len < 128 + ((w + 3) / 4) * ((h + 3) / 4) * (format == 1 ? 8 : 16))) ||
		(!format && (len < 128 + w * h * (bpp / 8))))
	{
		Com_Printf("%s: %s is truncated\n", __func__, filename);
		ri.FS_FreeFile(raw);
		return false;
	}

	out = malloc((size_t)w * h * 4);
	p = raw + 128;

	if (format)
	{
		for (y = 0; y < h; y += 4)
		{
			for (x = 0; x < w; x += 4)
			{
				byte block[16][4];
				int i;

				DDS_DecodeBlock(p, format, block);
				p += (format == 1) ? 8 : 16;

				for (i = 0; i < 16; i++)
				{
					int px = x + (i & 3), py = y + (i >> 2);

					if ((px < w) && (py < h))
					{
						memcpy(out + ((size_t)py * w + px) * 4, block[i], 4);
					}
				}
			}
		}
	}
	else
	{
		const unsigned masks[4] = {rmask, gmask, bmask, amask};

		for (y = 0; y < w * h; y++, p += bpp / 8)
		{
			unsigned v = (bpp == 32) ? DDS_U32(p) : ((unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16));
			int c;

			for (c = 0; c < 4; c++)
			{
				unsigned m = masks[c], s = 0;

				if (!m)
				{
					out[y * 4 + c] = (c == 3) ? 255 : 0;
					continue;
				}

				while (!(m & 1))
				{
					m >>= 1;
					s++;
				}

				out[y * 4 + c] = (byte)(((v >> s) & m) * 255 / m);
			}
		}
	}

	ri.FS_FreeFile(raw);
	Com_DPrintf("%s() loaded: %s\n", __func__, filename);
	*pic = out;
	*width = w;
	*height = h;
	return true;
}

qboolean
ResizeSTB(const byte *input_pixels, int input_width, int input_height,
			  byte *output_pixels, int output_width, int output_height)
{
	if (stbir_resize_uint8(input_pixels, input_width, input_height, 0,
			       output_pixels, output_width, output_height, 0, 4))
	{
		return true;
	}

	return false;
}

/* 8 looks good for smoothed textures with whole width as rstep */
#define COLOR_DISTANCE 8

void
SmoothColorImage(unsigned *dst, size_t size, size_t rstep)
{
	const unsigned *full_size;
	unsigned last_color;
	unsigned *last_diff;

	// maximum step for apply
	if (rstep < 2)
	{
		return;
	}

	// step one pixel back as with check one pixel more
	full_size = dst + size - rstep - 1;
	last_diff = dst;
	last_color = *dst;

	// skip current point
	dst ++;

	while (dst < full_size)
	{
		if (last_color != *dst)
		{
			int step = dst - last_diff;
			if (step > 1)
			{
				int a_beg, b_beg, c_beg, d_beg;
				int a_end, b_end, c_end, d_end;
				int a_step, b_step, c_step, d_step;
				int k;

				// minimize effect size to rstep
				if (step > rstep)
				{
					// change place for start effect
					last_diff += (step - rstep);
					step = rstep;
				}

				// compare next pixels
				for (k = 1; k <= step; k ++)
				{
					if (dst[k] != dst[0])
					{
						break;
					}
				}

				// step back as pixel different after previous step
				k --;

				// mirror steps
				if (k < step)
				{
					// Com_Printf("%s %d -> %d\n", __func__, k, step);
					// change place for start effect
					last_diff += (step - k);
					step = k;
				}

				// update step to correct value
				step += k;
				dst += k;

				// get colors
				a_beg = (last_color >> 0 ) & 0xff;
				b_beg = (last_color >> 8 ) & 0xff;
				c_beg = (last_color >> 16) & 0xff;
				d_beg = (last_color >> 24) & 0xff;

				a_end = (*dst >> 0 ) & 0xff;
				b_end = (*dst >> 8 ) & 0xff;
				c_end = (*dst >> 16) & 0xff;
				d_end = (*dst >> 24) & 0xff;

				a_step = a_end - a_beg;
				b_step = b_end - b_beg;
				c_step = c_end - c_beg;
				d_step = d_end - d_beg;

				if (step > 0 &&
					(abs(a_step) < COLOR_DISTANCE) &&
					(abs(b_step) < COLOR_DISTANCE) &&
					(abs(c_step) < COLOR_DISTANCE) &&
					(abs(d_step) < COLOR_DISTANCE) &&
					(
						abs(a_step) > 1 ||
						abs(b_step) > 1 ||
						abs(c_step) > 1 ||
						abs(d_step) > 1
					))
				{
					// generate color change steps
					a_step = (a_step << 16) / step;
					b_step = (b_step << 16) / step;
					c_step = (c_step << 16) / step;
					d_step = (d_step << 16) / step;

					// apply color changes
					for (k = 0; k < step; k++)
					{
						*last_diff = (((a_beg + ((a_step * k) >> 16)) << 0) & 0x000000ff) |
									 (((b_beg + ((b_step * k) >> 16)) << 8) & 0x0000ff00) |
									 (((c_beg + ((c_step * k) >> 16)) << 16) & 0x00ff0000) |
									 (((d_beg + ((d_step * k) >> 16)) << 24) & 0xff000000);
						last_diff++;
					}
				}
			}
			last_color = *dst;
			last_diff = dst;
		}
		dst ++;
	}
}

/* https://en.wikipedia.org/wiki/Pixel-art_scaling_algorithms */

void
scale2x(const byte *src, byte *dst, int width, int height)
{
	/*
		EPX/Scale2×/AdvMAME2×

		x A x
		C P B -> 1 2
		x D x    3 4

		1=P; 2=P; 3=P; 4=P;
		IF C==A AND C!=D AND A!=B => 1=A
		IF A==B AND A!=C AND B!=D => 2=B
		IF D==C AND D!=B AND C!=A => 3=C
		IF B==D AND B!=A AND D!=C => 4=D
	*/
	{
		const byte *in_buff = src;
		byte *out_buff = dst;
		const byte *out_buff_full = dst + ((width * height) << 2);

		while (out_buff < out_buff_full)
		{
			int x;
			for (x = 0; x < width; x ++)
			{
				// copy one source byte to two destinatuion bytes
				*out_buff = *in_buff;
				out_buff ++;
				*out_buff = *in_buff;
				out_buff ++;

				// next source pixel
				in_buff ++;
			}
			// copy last line one more time
			memcpy(out_buff, out_buff - (width << 1), width << 1);
			out_buff += width << 1;
		}
	}

	{
		int y, h, w;
		h = height - 1;
		w = width - 1;
		for (y = 0; y < height; y ++)
		{
			int x;
			for (x = 0; x < width; x ++)
			{
				byte a, b, c, d, p;

				p = src[(width * (y    )) + (x    )];
				a = (y > 0) ? src[(width * (y - 1)) + (x    )] : p;
				b = (x < w) ? src[(width * (y    )) + (x + 1)] : p;
				c = (x > 0) ? src[(width * (y    )) + (x - 1)] : p;
				d = (y < h) ? src[(width * (y + 1)) + (x    )] : p;

				if ((c == a) && (c != d) && (a != b))
				{
					dst[(2 * width * ((y * 2)    )) + ((x * 2)    )] = a;
				}

				if ((a == b) && (a != c) && (b != d))
				{
					dst[(2 * width * ((y * 2)    )) + ((x * 2) + 1)] = b;
				}

				if ((d == c) && (d != b) && (c != a))
				{
					dst[(2 * width * ((y * 2) + 1)) + ((x * 2)    )] = c;
				}

				if ((b == d) && (b != a) && (d != c))
				{
					dst[(2 * width * ((y * 2) + 1)) + ((x * 2) + 1)] = d;
				}
			}
		}
	}
}

void
scale3x(const byte *src, byte *dst, int width, int height)
{
	/*
		Scale3×/AdvMAME3× and ScaleFX

		A B C    1 2 3
		D E F -> 4 5 6
		G H I    7 8 9


		1=E; 2=E; 3=E; 4=E; 5=E; 6=E; 7=E; 8=E; 9=E;
		IF D==B AND D!=H AND B!=F => 1=D
		IF (D==B AND D!=H AND B!=F AND E!=C) OR (B==F AND B!=D AND F!=H AND E!=A) => 2=B
		IF B==F AND B!=D AND F!=H => 3=F
		IF (H==D AND H!=F AND D!=B AND E!=A) OR (D==B AND D!=H AND B!=F AND E!=G) => 4=D
		5=E
		IF (B==F AND B!=D AND F!=H AND E!=I) OR (F==H AND F!=B AND H!=D AND E!=C) => 6=F
		IF H==D AND H!=F AND D!=B => 7=D
		IF (F==H AND F!=B AND H!=D AND E!=G) OR (H==D AND H!=F AND D!=B AND E!=I) => 8=H
		IF F==H AND F!=B AND H!=D => 9=F
	*/
	{
		const byte *in_buff = src;
		byte *out_buff = dst;
		const byte *out_buff_full = dst + ((width * height) * 9);

		while (out_buff < out_buff_full)
		{
			int x;
			for (x = 0; x < width; x ++)
			{
				// copy one source byte to two destinatuion bytes
				*out_buff = *in_buff;
				out_buff ++;
				*out_buff = *in_buff;
				out_buff ++;
				*out_buff = *in_buff;
				out_buff ++;

				// next source pixel
				in_buff ++;
			}
			// copy last line one more time
			memcpy(out_buff, out_buff - (width * 3), width * 3);
			out_buff += width * 3;
			// copy last line one more time
			memcpy(out_buff, out_buff - (width * 3), width * 3);
			out_buff += width * 3;
		}
	}

	{
		int y, z, w;
		z = height - 1;
		w = width - 1;
		for (y = 0; y < height; y ++)
		{
			int x;
			for (x = 0; x < width; x ++)
			{
				byte a, b, c, d, e, f, g, h, i;

				e = src[(width * y) + x];

				a = ((y > 0) && (x > 0)) ? src[(width * (y - 1)) + (x - 1)] : e;
				b = ((y > 0) && (x    )) ? src[(width * (y - 1)) + (x    )] : e;
				c = ((y > 0) && (x < w)) ? src[(width * (y - 1)) + (x + 1)] : e;

				d = (           (x > 0)) ? src[(width * (y    )) + (x - 1)] : e;
				f = (           (x < w)) ? src[(width * (y    )) + (x + 1)] : e;

				g = ((y < z) && (x > 0)) ? src[(width * (y + 1)) + (x - 1)] : e;
				h = ((y < z) && (x    )) ? src[(width * (y + 1)) + (x    )] : e;
				i = ((y < z) && (x < w)) ? src[(width * (y + 1)) + (x + 1)] : e;

				if ((d == b) && (d != h) && (b != f))
				{
					dst[(3 * width * ((y * 3)    )) + ((x * 3)    )] = d;
				}

				if (((d == b) && (d != h) && (b != f) && (e != c)) ||
					((b == f) && (b != d) && (f != h) && (e != a)))
				{
					dst[(3 * width * ((y * 3)    )) + ((x * 3) + 1)] = b;
				}

				if ((b == f) && (b != d) && (f != h))
				{
					dst[(3 * width * ((y * 3)    )) + ((x * 3) + 2)] = f;
				}

				if (((h == d) && (h != f) && (d != b) && (e != a)) ||
					((d == b) && (d != h) && (b != f) && (e != g)))
				{
					dst[(3 * width * ((y * 3) + 1)) + ((x * 3)    )] = d;
				}

				if (((b == f) && (b != d) && (f != h) && (e != i)) ||
					((f == h) && (f != b) && (h != d) && (e != c)))
				{
					dst[(3 * width * ((y * 3) + 1)) + ((x * 3) + 2)] = f;
				}

				if ((h == d) && (h != f) && (d != b))
				{
					dst[(3 * width * ((y * 3) + 2)) + ((x * 3)    )] = d;
				}

				if (((f == h) && (f != b) && (h != d) && (e != g)) ||
					((h == d) && (h != f) && (d != b) && (e != i)))
				{
					dst[(3 * width * ((y * 3) + 2)) + ((x * 3) + 1)] = h;
				}

				if ((f == h) && (f != b) && (h != d))
				{
					dst[(3 * width * ((y * 3) + 2)) + ((x * 3) + 2)] = f;
				}
			}
		}
	}
}

static struct image_s *
LoadHiColorImage(const char *name, const char* namewe, const char *ext,
	imagetype_t type, loadimage_t load_image)
{
	int realwidth = 0, realheight = 0;
	int width = 0, height = 0;
	struct image_s	*image = NULL;
	byte *pic = NULL;

	/* Get size of the original texture */
	if (strcmp(ext, "pcx") == 0)
	{
		GetPCXInfo(name, &realwidth, &realheight);
	}
	else if (strcmp(ext, "wal") == 0)
	{
		GetWalInfo(name, &realwidth, &realheight);
	}
	else if (strcmp(ext, "m8") == 0)
	{
		GetM8Info(name, &realwidth, &realheight);
	}
	else if (strcmp(ext, "m32") == 0)
	{
		GetM32Info(name, &realwidth, &realheight);
	}

	/* try to load a tga, png, jpg or dds (in that order/priority) */
	if (  LoadSTB(namewe, "tga", &pic, &width, &height)
	   || LoadSTB(namewe, "png", &pic, &width, &height)
	   || LoadSTB(namewe, "jpg", &pic, &width, &height)
	   || LoadDDS(namewe, &pic, &width, &height) )
	{
		if (width >= realwidth && height >= realheight)
		{
			if (realheight == 0 || realwidth == 0)
			{
				realheight = height;
				realwidth = width;
			}

			image = load_image(name, pic,
				width, realwidth,
				height, realheight,
				width * height,
				type, 32);
		}
	}

	if (pic)
	{
		free(pic);
	}

	return image;
}

struct image_s *
R_LoadImage(const char *name, const char* namewe, const char *ext, imagetype_t type,
	qboolean r_retexturing, loadimage_t load_image)
{
	struct image_s	*image = NULL;

	// with retexturing and not skin
	if (r_retexturing)
	{
		image = LoadHiColorImage(name, namewe, ext, type, load_image);
	}

	if (!image)
	{
		if (!strcmp(ext, "pcx"))
		{
			byte *pic = NULL;
			byte	*palette = NULL;
			int width = 0, height = 0, bitsPerPixel = 8;

			LoadPCX (namewe, &pic, &palette, &width, &height, &bitsPerPixel);
			if (!pic)
				return NULL;

			image = load_image(name, pic,
				width, width,
				height, height,
				width * height, type, bitsPerPixel);

			if (palette)
			{
				free(palette);
			}
			free(pic);
		}
		else if (!strcmp(ext, "wal"))
		{
			image = LoadWal(namewe, type, load_image);
		}
		else if (!strcmp(ext, "m8"))
		{
			image = LoadM8(namewe, type, load_image);
		}
		else if (!strcmp(ext, "m32"))
		{
			image = LoadM32(namewe, type, load_image);
		}
		else if (!strcmp(ext, "tga") ||
		         !strcmp(ext, "png") ||
		         !strcmp(ext, "jpg"))
		{
			byte *pic = NULL;
			int width = 0, height = 0;

			if (LoadSTB (namewe, ext, &pic, &width, &height) && pic)
			{
				image = load_image(name, pic,
					width, width,
					height, height,
					width * height,
					type, 32);

				free(pic);
			}
		}
	}

	return image;
}

struct image_s*
GetSkyImage(const char *skyname, const char* surfname, qboolean palettedtexture,
	findimage_t find_image)
{
	struct image_s *image = NULL;
	char pathname[MAX_QPATH];

	/* Quake 2 */
	if (palettedtexture)
	{
		Com_sprintf(pathname, sizeof(pathname), "env/%s%s.pcx",
				skyname, surfname);
		image = find_image(pathname, it_sky);
	}

	if (!image)
	{
		Com_sprintf(pathname, sizeof(pathname), "env/%s%s.tga",
				skyname, surfname);
		image = find_image(pathname, it_sky);
	}

	/* Heretic 2 */
	if (!image)
	{
		Com_sprintf(pathname, sizeof(pathname), "pics/Skies/%s%s.m32",
			skyname, surfname);
		image = find_image(pathname, it_sky);
	}

	if (!image)
	{
		Com_sprintf(pathname, sizeof(pathname), "pics/Skies/%s%s.m8",
			skyname, surfname);
		image = find_image(pathname, it_sky);
	}

	return image;
}

struct image_s *
GetTexImage(const char *name, findimage_t find_image)
{
	struct image_s *image = NULL;
	char pathname[MAX_QPATH];

	/* Quake 2 */
	Com_sprintf(pathname, sizeof(pathname), "textures/%s.wal", name);
	image = find_image(pathname, it_wall);

	/* Heretic 2 */
	if (!image)
	{
		Com_sprintf(pathname, sizeof(pathname), "textures/%s.m32", name);
		image = find_image(pathname, it_wall);
	}

	if (!image)
	{
		Com_sprintf(pathname, sizeof(pathname), "textures/%s.m8", name);
		image = find_image(pathname, it_wall);
	}

	return image;
}

struct image_s *
R_FindPic(const char *name, findimage_t find_image)
{
	struct image_s	*image = NULL;

	if ((name[0] != '/') && (name[0] != '\\'))
	{
		char	pathname[MAX_QPATH];

		/* Quake 2 */
		Com_sprintf(pathname, sizeof(pathname), "pics/%s.pcx", name);
		image = find_image(pathname, it_pic);

		/* OpenMerc: Soldier of Fortune keeps the console pictures
		   (conchars, conback, backtile) in pics/console/ as .m32; looked
		   up before Heretic 2's pics/misc/, which SoF does not have (each
		   failed probe there logged "Can't load", every frame for the
		   console background) */
		if (!image)
		{
			Com_sprintf(pathname, sizeof(pathname), "pics/console/%s.m32", name);
			image = find_image(pathname, it_pic);
		}

		/* Heretic 2 */
		if (!image)
		{
			Com_sprintf(pathname, sizeof(pathname), "pics/misc/%s.m32", name);
			image = find_image(pathname, it_pic);
		}

		if (!image)
		{
			Com_sprintf(pathname, sizeof(pathname), "pics/misc/%s.m8", name);
			image = find_image(pathname, it_pic);
		}

		if (!image)
		{
			Com_sprintf(pathname, sizeof(pathname), "pics/%s.m32", name);
			image = find_image(pathname, it_pic);
		}
	}
	else
	{
		image = find_image(name + 1, it_pic);
	}

	return image;
}

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes
=================
*/

typedef struct
{
	short x, y;
} floodfill_t;

/* must be a power of 2 */
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy)	\
	{ \
		if (pos[off] == fillcolor) \
		{ \
			pos[off] = 255;	\
			fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
			inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
		} \
		else if (pos[off] != 255) \
		{ \
			fdc = pos[off];	\
		} \
	}

/*
 * Fill background pixels so mipmapping doesn't have haloes
 */
void
R_FloodFillSkin(byte *skin, int skinwidth, int skinheight, const unsigned *table_8to24)
{
	byte fillcolor = *skin; /* assume this is the pixel to fill */
	static floodfill_t fifo[FLOODFILL_FIFO_SIZE];
	int inpt = 0, outpt = 0;
	int filledcolor = 0;
	int i;

	// NOTE: there was a if (filledcolor == -1) which didn't make sense b/c filledcolor used to be initialized to -1
	/* attempt to find opaque black */
	for (i = 0; i < 256; ++i)
	{
		if (LittleLong(table_8to24[i]) == (255 << 0)) /* alpha 1.0 */
		{
			filledcolor = i;
			break;
		}
	}

	/* can't fill to filled color or to transparent color (used as visited marker) */
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int x = fifo[outpt].x, y = fifo[outpt].y;
		int fdc = filledcolor;
		byte *pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)
		{
			FLOODFILL_STEP(-1, -1, 0);
		}

		if (x < skinwidth - 1)
		{
			FLOODFILL_STEP(1, 1, 0);
		}

		if (y > 0)
		{
			FLOODFILL_STEP(-skinwidth, 0, -1);
		}

		if (y < skinheight - 1)
		{
			FLOODFILL_STEP(skinwidth, 0, 1);
		}

		skin[x + skinwidth * y] = fdc;
	}
}

unsigned *
R_Convert8to32(const byte *data, size_t width, size_t height, const unsigned *table_8to24)
{
	unsigned *trans;
	size_t i, s;

	if (height == 0 || width > INT_MAX / sizeof(*trans) / height)
	{
		Com_Error(ERR_DROP, "%s: invalid dimensions", __func__);
		return NULL;
	}

	s = width * height;

	trans = malloc(s * sizeof(unsigned));
	YQ2_COM_CHECK_OOM(trans, "malloc()",
		s * sizeof(unsigned))
	if (!trans)
	{
		/* unaware about YQ2_ATTR_NORETURN_FUNCPTR? */
		return NULL;
	}

	for (i = 0; i < s; i++)
	{
		byte p = data[i];
		trans[i] = table_8to24[p];

		/* transparent, so scan around for
		   another color to avoid alpha fringes */
		if (p == 255)
		{
			if ((i > width) && (data[i - width] != 255))
			{
				p = data[i - width];
			}
			else if ((i < s - width) && (data[i + width] != 255))
			{
				p = data[i + width];
			}
			else if ((i > 0) && (data[i - 1] != 255))
			{
				p = data[i - 1];
			}
			else if ((i < s - 1) && (data[i + 1] != 255))
			{
				p = data[i + 1];
			}
			else
			{
				p = 0;
			}

			/* copy rgb components */
			((byte *)&trans[i])[0] = ((byte *)&table_8to24[p])[0];
			((byte *)&trans[i])[1] = ((byte *)&table_8to24[p])[1];
			((byte *)&trans[i])[2] = ((byte *)&table_8to24[p])[2];
		}
	}

	return trans;
}
