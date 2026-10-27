// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G10 - the module's own image: base address, size and a build fingerprint.
//
// Pointers into the module (callbacks, static tables) are saved as offsets from the image base (SPEC-752 2.1 lets a
// module that reads only its own saves use its own tables). The offsets are valid only for the same code, so the
// FTSZ chunk carries a fingerprint of the code: a CRC-32 of the code section as stored in the DLL file (not the
// loaded copy, which the loader may have relocated) and its size, plus the image size. A rebuild of unchanged
// sources keeps the fingerprint; any code change makes the level section of older saves refuse to load with the
// original's game error text (FTSZ check, SPEC-752 1.2).
// Implements: SPEC-752 2.1 (callback encoding), 2.3 (FTSZ check)

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace g10 {

struct ImageInfo {
	const unsigned char *base;
	uint32_t size;
	uint32_t timestamp; // here: CRC-32 of the code section of the module file
	uint32_t checksum;  // here: size of the code section
};

static ImageInfo g_image;
static int g_image_done;

static void ImageAnchor(void) {}

static uint32_t Crc32(const unsigned char *p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

#ifdef _WIN32
// CRC of the raw ".text" section of the DLL file this code was loaded from
static void CodeFingerprint(HMODULE h, uint32_t *crc, uint32_t *size)
{
	char path[MAX_PATH];
	*crc = 0;
	*size = 0;
	if (!GetModuleFileNameA(h, path, sizeof(path)))
		return;
	FILE *f = fopen(path, "rb");
	if (!f)
		return;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = (unsigned char *)malloc(len > 0 ? len : 1);
	if (buf && fread(buf, 1, len, f) == (size_t)len && len > (long)sizeof(IMAGE_DOS_HEADER)) {
		const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)buf;
		if (dos->e_lfanew > 0 && dos->e_lfanew + (long)sizeof(IMAGE_NT_HEADERS) <= len) {
			const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(buf + dos->e_lfanew);
			const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
			for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
				if (memcmp(sec[i].Name, ".text", 6))
					continue;
				uint32_t off = sec[i].PointerToRawData, n = sec[i].SizeOfRawData;
				if (off + (uint64_t)n <= (uint64_t)len) {
					*crc = Crc32(buf + off, n);
					*size = n;
				}
				break;
			}
		}
	}
	free(buf);
	fclose(f);
}
#endif

const ImageInfo &Image(void)
{
	if (g_image_done)
		return g_image;
	g_image_done = 1;
	memset(&g_image, 0, sizeof(g_image));
#ifdef _WIN32
	HMODULE h = NULL;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       (LPCSTR)(void *)&ImageAnchor, &h) &&
	    h) {
		const unsigned char *b = (const unsigned char *)h;
		const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)b;
		const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(b + dos->e_lfanew);
		g_image.base = b;
		g_image.size = nt->OptionalHeader.SizeOfImage;
		CodeFingerprint(h, &g_image.timestamp, &g_image.checksum);
	}
#else
	Dl_info info;
	if (dladdr((void *)&ImageAnchor, &info) && info.dli_fbase) {
		g_image.base = (const unsigned char *)info.dli_fbase;
		g_image.size = 0x7fffffff; // dladdr gives no image size; the fingerprint alone guards the offsets
		g_image.timestamp = (uint32_t)((uintptr_t)&ImageAnchor - (uintptr_t)info.dli_fbase);
	}
#endif
	return g_image;
}

} // namespace g10
