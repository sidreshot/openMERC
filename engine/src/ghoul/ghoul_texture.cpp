// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - image lists (.ifl / single .tga), TGA decoding, mip chains and texture upload
// through the renderer's GL table.
// Implements: SPEC-012 section 8 "Textures" (lookup order, COMSKIN fallback, DEFAULT.TGA, CUSTOM/ entries,
//             clamped image index, TGA rules, gamma, 2x2 mip chain, upload level, _NOMIP, wrap), statistics
//             40/41; SPEC-AMEND-002 item 6 (.ifl answers); SPEC-121 (.ifl use)

#include <algorithm>
#include <cstring>

#include "ghoul_gl.h"
#include "ghoul_internal.h"

namespace ghoul {

int g_drawRole = OM_GHOUL_CLIENT; // set by instance drawing: menu objects upload at full detail

static bool LoadBytes(const std::string &path, std::vector<uint8_t> &out)
{
	if (!Host().load_file)
		return false;
	void *d = nullptr;
	int n = Host().load_file(Host().ctx, path.c_str(), &d);
	if (n < 0 || !d)
	{
		if (d && Host().free_file)
			Host().free_file(Host().ctx, d);
		return false;
	}
	out.assign((const uint8_t *)d, (const uint8_t *)d + n);
	if (Host().free_file)
		Host().free_file(Host().ctx, d);
	return true;
}

static std::string StripExt(const std::string &n)
{
	size_t slash = n.find_last_of('/');
	size_t dot = n.find_last_of('.');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
		return n.substr(0, dot);
	return n;
}

static std::string ComskinDir(const std::string &d)
{
	// D cut after "GHOUL" + one character, then "COMSKIN/"
	return d.size() >= 6 ? d.substr(0, 6) + "COMSKIN/" : std::string("GHOUL/COMSKIN/");
}

static void ParseIfl(const std::vector<uint8_t> &data, std::vector<std::string> &out)
{
	std::string cur;
	auto flush = [&]() {
		size_t e = cur.find_last_not_of(" \t\f\v");
		std::string s = e == std::string::npos ? std::string() : cur.substr(0, e + 1); // leading kept
		if (!s.empty())
			out.push_back(StripExt(NormalizeName(s)));
		cur.clear();
	};
	for (uint8_t c : data)
	{
		if (c == '\n' || c == '\r' || c == 0)
			flush();
		else
			cur.push_back((char)c);
	}
	flush();
}

// try <dir><N>.IFL, then <dir><N>.TGA
static bool TryList(const std::string &dir, const std::string &n, ImageList &l)
{
	std::vector<uint8_t> d;
	if (LoadBytes(dir + n + ".IFL", d))
	{
		std::vector<std::string> imgs;
		ParseIfl(d, imgs);
		if (!imgs.empty())
		{
			l.images = imgs;
			l.dir = dir;
			return true;
		}
	}
	if (LoadBytes(dir + n + ".TGA", d))
	{
		l.images.assign(1, n);
		l.dir = dir;
		return true;
	}
	return false;
}

ImageList *AcquireImageList(const std::string &modelDir, const std::string &name)
{
	if (name.empty())
		return nullptr;
	std::string n = NormalizeName(name);
	if (n.size() > 4 && n[n.size() - 4] == '.')
		n = n.substr(0, n.size() - 4);
	std::string d = NormalizeName(modelDir);
	if (!d.empty() && d.back() != '/')
		d.push_back('/');
	std::string key = d + n;
	auto it = G().imageLists.find(key);
	if (it == G().imageLists.end())
	{
		std::unique_ptr<ImageList> l(new ImageList());
		l->key = key;
		if (TryList(d, n, *l) || TryList(ComskinDir(d), n, *l))
			l->found = true;
		else
			Print("Ghoul :%s%s not found\n", ComskinDir(d).c_str(), n.c_str());
		l->textures.assign(l->images.size(), nullptr);
		it = G().imageLists.insert(std::make_pair(key, std::move(l))).first;
	}
	ImageList *l = it->second.get();
	l->refs++;
	return l->found ? l : nullptr;
}

void ReleaseImageList(ImageList *l)
{
	if (l)
		l->refs--;
}

int ImageListAddCustom(ImageList *l, const std::string &upperName)
{
	if (!l)
		return -1;
	l->images.push_back(upperName);
	l->textures.push_back(nullptr);
	return (int)l->images.size() - 1;
}

static Texture *GetTexture(const std::string &name)
{
	auto it = G().textures.find(name);
	if (it != G().textures.end())
		return it->second.get();
	std::unique_ptr<Texture> t(new Texture());
	t->name = name;
	Texture *r = t.get();
	G().textures[name] = std::move(t);
	return r;
}

static bool DecodeFile(Texture *t)
{
	std::vector<uint8_t> d;
	if (!LoadBytes(t->name + ".TGA", d))
		return false;
	return DecodeTGA(d.data(), d.size(), t->name, *t);
}

Texture *ListTexture(ImageList *l, int index, bool load)
{
	if (!l || l->images.empty())
		return nullptr;
	if (index < 0)
		index = 0;
	if (index >= (int)l->images.size())
		index = (int)l->images.size() - 1; // clamped, no wrap-around
	Texture *&slot = l->textures[index];
	if (!slot)
	{
		const std::string &e = l->images[index];
		std::string full = e.compare(0, 7, "CUSTOM/") == 0 ? "GHOUL/" + e : l->dir + e;
		slot = GetTexture(full);
	}
	Texture *t = slot;
	t->usedSincePurge = true;
	if (load && G().gl && !t->decoded)
	{
		if (!t->missing && DecodeFile(t))
			t->decoded = true;
		else if (t->name != "GHOUL/COMSKIN/DEFAULT")
		{
			if (!t->missing)
				Print("Ghoul :%s not found\n", t->name.c_str());
			t->missing = true;
			Texture *def = GetTexture("GHOUL/COMSKIN/DEFAULT");
			if (!def->decoded && !DecodeFile(def))
				Fatal("Default Texture Not Found");
			def->decoded = true;
			def->usedSincePurge = true;
			slot = def;
			t = def;
		}
		else
			Fatal("Default Texture Not Found");
	}
	t->refs = std::max(t->refs, 1);
	return t;
}

// ------------------------------------------------------------------ TGA (SPEC-012 TGA rules)

// fb5: up to 8192 (HD packs); the original stopped at 1024 with a fatal error
static bool Pow2(int v) { return v > 0 && v <= 8192 && (v & (v - 1)) == 0; }

bool DecodeTGA(const uint8_t *d, size_t n, const std::string &name, Texture &out)
{
	if (n < 18)
		return false;
	int idlen = d[0], cmap = d[1], type = d[2];
	int w = d[12] | (d[13] << 8), h = d[14] | (d[15] << 8), bpp = d[16];
	if (type != 2 && type != 10)
		Fatal("LoadTGA: Only type 2 and 10 targa RGB images supported: %s", name.c_str());
	if (cmap != 0 || (bpp != 24 && bpp != 32))
		Fatal("LoadTGA: Only 32 or 24 bit images supported (no colormaps): %s", name.c_str());
	if (!Pow2(w) || !Pow2(h))
		Fatal("LoadTGA: Bitmap is not a power of 2 (1,2,4,8,16,32,64,128,256): %s", name.c_str());
	int bytes = bpp / 8;
	size_t p = 18 + (size_t)idlen;
	std::vector<uint8_t> px((size_t)w * h * 4, 0);
	size_t count = (size_t)w * h;
	auto put = [&](size_t i, const uint8_t *s) {
		uint8_t *o = &px[i * 4];
		o[0] = s[2];
		o[1] = s[1];
		o[2] = s[0];
		o[3] = bytes == 4 ? s[3] : 255;
	};
	if (type == 2)
	{
		for (size_t i = 0; i < count; i++, p += bytes)
		{
			if (p + bytes > n)
				break;
			put(i, d + p);
		}
	}
	else
	{
		size_t i = 0;
		while (i < count && p < n)
		{
			int hdr = d[p++];
			int run = (hdr & 0x7f) + 1;
			if (hdr & 0x80)
			{
				if (p + bytes > n)
					break;
				for (int k = 0; k < run && i < count; k++)
					put(i++, d + p);
				p += bytes;
			}
			else
			{
				for (int k = 0; k < run && i < count; k++, p += bytes)
				{
					if (p + bytes > n)
						break;
					put(i++, d + p);
				}
			}
		}
		std::swap(w, h); // original quirk: RLE images are decoded with width and height exchanged
	}
	// rows are always treated as stored bottom-up: the last stored row is the top (v = 0)
	out.width = w;
	out.height = h;
	out.rgba.assign((size_t)w * h * 4, 0);
	bool alpha = false;
	for (int y = 0; y < h; y++)
	{
		const uint8_t *src = &px[(size_t)(h - 1 - y) * w * 4];
		uint8_t *dst = &out.rgba[(size_t)y * w * 4];
		for (int x = 0; x < w; x++)
		{
			dst[x * 4 + 0] = G().gamma[src[x * 4 + 0]];
			dst[x * 4 + 1] = G().gamma[src[x * 4 + 1]];
			dst[x * 4 + 2] = G().gamma[src[x * 4 + 2]];
			dst[x * 4 + 3] = src[x * 4 + 3];
			if (src[x * 4 + 3] != 255)
				alpha = true;
		}
	}
	out.hasAlpha = alpha;
	out.nomip = name.find("_NOMIP") != std::string::npos;
	return true;
}

// ------------------------------------------------------------------ upload and binding

static void Downsample(const std::vector<uint8_t> &src, int w, int h, std::vector<uint8_t> &dst, int &nw, int &nh)
{
	nw = w > 1 ? w / 2 : 1;
	nh = h > 1 ? h / 2 : 1;
	dst.assign((size_t)nw * nh * 4, 0);
	for (int y = 0; y < nh; y++)
		for (int x = 0; x < nw; x++)
			for (int c = 0; c < 4; c++)
			{
				int x0 = (2 * x) % w, x1 = (2 * x + 1) % w, y0 = (2 * y) % h, y1 = (2 * y + 1) % h;
				int s = src[((size_t)y0 * w + x0) * 4 + c] + src[((size_t)y0 * w + x1) * 4 + c] +
					src[((size_t)y1 * w + x0) * 4 + c] + src[((size_t)y1 * w + x1) * 4 + c];
				dst[((size_t)y * nw + x) * 4 + c] = (uint8_t)((s + 2) >> 2);
			}
}

static void Upload(Texture *t)
{
	const GhoulGLTable *g = G().gl;
	if (!t->glName)
		g->GenTextures(1, &t->glName);
	g->BindTexture(gl::TEXTURE_2D, t->glName);
	int start = g_drawRole == OM_GHOUL_MENU ? 0 : G().textureDetail;
	if (start < 0)
		start = 0;
	std::vector<uint8_t> cur = t->rgba, next;
	int w = t->width, h = t->height, level = 0, glLevel = 0;
	int internal = t->hasAlpha ? (int)gl::RGBA : (int)gl::RGB;
	for (;;)
	{
		bool use = level >= start && w <= G().maxTextureSize && h <= G().maxTextureSize;
		if (use)
		{
			g->TexImage2D(gl::TEXTURE_2D, glLevel, internal, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, cur.data());
			glLevel++;
			if (t->nomip)
				break;
		}
		if (w == 1 && h == 1)
			break;
		int nw, nh;
		Downsample(cur, w, h, next, nw, nh);
		cur.swap(next);
		w = nw;
		h = nh;
		level++;
	}
	if (t->nomip)
	{
		g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
		g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
	}
	else
	{
		g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_NEAREST);
		g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
	}
	t->uploaded = true;
}

bool BindTexture(Texture *t, bool tileU, bool tileV)
{
	const GhoulGLTable *g = G().gl;
	if (!g || !t || !t->decoded)
		return false;
	if (!t->uploaded)
		Upload(t);
	else
		g->BindTexture(gl::TEXTURE_2D, t->glName);
	g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, tileU ? gl::REPEAT : gl::CLAMP);
	g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, tileV ? gl::REPEAT : gl::CLAMP);
	Stats &s = G().stats;
	if (t->statFrame != s.renderFrame)
	{
		t->statFrame = s.renderFrame;
		s.textures++;
		s.textureBytes += t->width * t->height * 2;
	}
	t->usedSincePurge = true;
	return true;
}

void ReleaseAllTextures()
{
	const GhoulGLTable *g = G().gl;
	for (auto &e : G().textures)
	{
		Texture *t = e.second.get();
		if (t->uploaded && g && t->glName)
			g->DeleteTextures(1, &t->glName);
		t->glName = 0;
		t->uploaded = false;
	}
	ReleaseShadowTextures(g);
	G().gl = nullptr;
}

void PurgeImages()
{
	const GhoulGLTable *g = G().gl;
	for (auto &e : G().textures)
	{
		Texture *t = e.second.get();
		if (!t->usedSincePurge)
		{
			if (t->uploaded && g && t->glName)
				g->DeleteTextures(1, &t->glName);
			t->glName = 0;
			t->uploaded = false;
			t->decoded = false;
			t->rgba.clear();
			t->rgba.shrink_to_fit();
		}
		t->usedSincePurge = false;
	}
	for (auto it = G().imageLists.begin(); it != G().imageLists.end();)
		it = it->second->refs <= 0 ? G().imageLists.erase(it) : std::next(it);
}

} // namespace ghoul
