// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program - host over the extracted data, file listing, PNG writer.
// Implements: SPEC-010 (test host for the Ghoul library)

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "test_common.h"

int g_fail = 0, g_pass = 0;
TestHost g_host;

static std::string Up(const std::string &s)
{
	std::string r(s);
	for (auto &c : r)
	{
		c = (char)std::toupper((unsigned char)c);
		if (c == '\\')
			c = '/';
	}
	return r;
}

bool ReadFileBytes(const std::string &path, std::vector<uint8_t> &out)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::fseek(f, 0, SEEK_END);
	long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	out.resize((size_t)n);
	size_t r = n ? std::fread(out.data(), 1, (size_t)n, f) : 0;
	std::fclose(f);
	return r == (size_t)n;
}

static int HostLoad(void *, const char *path, void **data)
{
	*data = nullptr;
	std::string key = Up(path);
	g_host.loads++;
	auto it = g_host.overlay.find(key);
	if (it != g_host.overlay.end())
	{
		void *p = std::malloc(it->second.size() + 1);
		std::memcpy(p, it->second.data(), it->second.size());
		*data = p;
		return (int)it->second.size();
	}
	static const char *paks[] = {"pak3", "pak2", "pak0"};
	for (const char *pk : paks)
	{
		std::string fp = g_host.root + "/" + pk + "/" + path;
		std::vector<uint8_t> b;
		if (ReadFileBytes(fp, b))
		{
			void *p = std::malloc(b.size() + 1);
			if (!b.empty())
				std::memcpy(p, b.data(), b.size());
			*data = p;
			return (int)b.size();
		}
	}
	return -1;
}

static void HostFree(void *, void *d) { std::free(d); }

static void HostPrint(void *, const char *text)
{
	g_host.log.push_back(text);
	if (!g_host.quiet)
		std::fputs(text, stdout);
}

static void HostFatal(void *, const char *text) { throw std::runtime_error(text); }

void InstallHost(const std::string &root)
{
	g_host.root = root;
	om_ghoul_host_t h;
	std::memset(&h, 0, sizeof(h));
	h.load_file = HostLoad;
	h.free_file = HostFree;
	h.print = HostPrint;
	h.fatal = HostFatal;
	OMGhoul_Init(&h);
}

std::vector<std::string> ListFiles(const std::string &dir, const std::string &ext)
{
	// game paths ("ghoul/...") of every file with this extension in pak0/pak2/pak3, higher pak wins
	std::map<std::string, std::string> found;
	namespace fs = std::filesystem;
	static const char *paks[] = {"pak0", "pak2", "pak3"};
	for (const char *pk : paks)
	{
		fs::path base = fs::path(g_host.root) / pk;
		fs::path d = base / dir;
		std::error_code ec;
		if (!fs::exists(d, ec))
			continue;
		for (auto &e : fs::recursive_directory_iterator(d, ec))
		{
			if (!e.is_regular_file())
				continue;
			std::string x = e.path().extension().string();
			for (auto &c : x)
				c = (char)std::tolower((unsigned char)c);
			if (x != ext)
				continue;
			std::string rel = fs::relative(e.path(), base, ec).generic_string();
			found[Up(rel)] = rel;
		}
	}
	std::vector<std::string> out;
	for (auto &f : found)
		out.push_back(f.second);
	return out;
}

// ------------------------------------------------------------------ PNG (stored deflate)

static uint32_t Crc(const uint8_t *d, size_t n, uint32_t c = 0)
{
	static uint32_t table[256];
	static bool init = false;
	if (!init)
	{
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t v = i;
			for (int k = 0; k < 8; k++)
				v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
			table[i] = v;
		}
		init = true;
	}
	c = ~c;
	for (size_t i = 0; i < n; i++)
		c = table[(c ^ d[i]) & 255] ^ (c >> 8);
	return ~c;
}

static void Be32(std::vector<uint8_t> &v, uint32_t x)
{
	v.push_back((uint8_t)(x >> 24));
	v.push_back((uint8_t)(x >> 16));
	v.push_back((uint8_t)(x >> 8));
	v.push_back((uint8_t)x);
}

static void Chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data)
{
	Be32(out, (uint32_t)data.size());
	std::vector<uint8_t> td(type, type + 4);
	td.insert(td.end(), data.begin(), data.end());
	out.insert(out.end(), td.begin(), td.end());
	Be32(out, Crc(td.data(), td.size()));
}

bool WritePNG(const std::string &path, const Image &img)
{
	std::vector<uint8_t> raw;
	for (int y = 0; y < img.h; y++)
	{
		raw.push_back(0);
		raw.insert(raw.end(), img.rgb.begin() + (size_t)y * img.w * 3, img.rgb.begin() + (size_t)(y + 1) * img.w * 3);
	}
	std::vector<uint8_t> z = {0x78, 0x01};
	size_t p = 0;
	uint32_t a = 1, b = 0;
	for (uint8_t c : raw)
	{
		a = (a + c) % 65521;
		b = (b + a) % 65521;
	}
	do
	{
		size_t n = std::min<size_t>(65535, raw.size() - p);
		z.push_back(p + n == raw.size() ? 1 : 0);
		z.push_back((uint8_t)n);
		z.push_back((uint8_t)(n >> 8));
		z.push_back((uint8_t)~n);
		z.push_back((uint8_t)(~n >> 8));
		z.insert(z.end(), raw.begin() + p, raw.begin() + p + n);
		p += n;
	} while (p < raw.size());
	Be32(z, (b << 16) | a);
	std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
	std::vector<uint8_t> ihdr;
	Be32(ihdr, (uint32_t)img.w);
	Be32(ihdr, (uint32_t)img.h);
	ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
	Chunk(out, "IHDR", ihdr);
	Chunk(out, "IDAT", z);
	Chunk(out, "IEND", {});
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	std::fwrite(out.data(), 1, out.size(), f);
	std::fclose(f);
	return true;
}
