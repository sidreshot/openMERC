// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G10 - chunk services, pointer encodings and the extension registry.
//
// Chunks go through the engine's imports 85 (write) and 86 (read) (SPEC-001, SPEC-303). For the self-test the two
// import entries are replaced by a memory stream with the same rules (id, exact or any length, NULL buffer =
// allocate or discard), so every package that calls gix.WriteChunk / gix.ReadChunk writes into the same stream.
// Implements: SPEC-752 2.1 (entity, callback/static-table and string encodings), 2.2 (STRG), SPEC-303 / SPEC-001
//             #85/#86 (chunk semantics of the memory stream)

#include "save_local.h"

namespace g10 {

int g_loading;
int g_warnings;

// ---------------------------------------------------------------------------------------------------------------
// memory stream (self-test)

static std::vector<unsigned char> *mem_out;
static const std::vector<unsigned char> *mem_in;
static size_t mem_pos;
static int (*saved_write)(int, const void *, int);
static int (*saved_read)(int, void *, int, void **);

static void IdText(int id, char out[5])
{
	out[0] = (char)(id >> 24);
	out[1] = (char)(id >> 16);
	out[2] = (char)(id >> 8);
	out[3] = (char)id;
	out[4] = 0;
}

static int MemWrite(int id, const void *data, int length)
{
	if (!mem_out || length < 0)
		return 0;
	int32_t h[2] = {id, length};
	const unsigned char *p = (const unsigned char *)h;
	mem_out->insert(mem_out->end(), p, p + 8);
	if (length)
		mem_out->insert(mem_out->end(), (const unsigned char *)data, (const unsigned char *)data + length);
	return 1;
}

static int MemRead(int id, void *buffer, int length, void **allocated)
{
	char want[5], got[5];
	IdText(id, want);
	if (!mem_in || mem_pos + 8 > mem_in->size()) {
		gi.error("Error during loading chunk %s", want);
		return 0;
	}
	int32_t h[2];
	memcpy(h, &(*mem_in)[mem_pos], 8);
	IdText(h[0], got);
	if (h[0] != id) {
		gi.error("Loaded chunk ID (%s) does not match requested chunk ID (%s)", got, want);
		return 0;
	}
	if (length && h[1] != length) {
		gi.error("Loaded chunk (%s) has different length than requested", want);
		return 0;
	}
	if (mem_pos + 8 + h[1] > mem_in->size()) {
		gi.error("Error during loading chunk %s", want);
		return 0;
	}
	const unsigned char *src = &(*mem_in)[mem_pos + 8];
	if (buffer)
		memcpy(buffer, src, h[1]);
	else if (allocated) {
		void *p = gix.TagMalloc(h[1] ? h[1] : 1, TAG_GAME);
		memcpy(p, src, h[1]);
		*allocated = p;
	}
	mem_pos += 8 + h[1];
	return h[1];
}

void MemoryBegin(std::vector<unsigned char> *out, const std::vector<unsigned char> *in)
{
	if (!saved_write) {
		saved_write = gix.WriteChunk;
		saved_read = gix.ReadChunk;
	}
	mem_out = out;
	mem_in = in;
	mem_pos = 0;
	gix.WriteChunk = MemWrite;
	gix.ReadChunk = MemRead;
}

void MemoryEnd(void)
{
	if (saved_write) {
		gix.WriteChunk = saved_write;
		gix.ReadChunk = saved_read;
		saved_write = NULL;
		saved_read = NULL;
	}
	mem_out = NULL;
	mem_in = NULL;
	mem_pos = 0;
}

} // namespace g10

using namespace g10;

// ---------------------------------------------------------------------------------------------------------------
// chunks

int G10_ChunkId(const char *s)
{
	return ((uint8_t)s[0] << 24) | ((uint8_t)s[1] << 16) | ((uint8_t)s[2] << 8) | (uint8_t)s[3];
}

void G10_WriteChunk(const char *id, const void *data, int size)
{
	if (gix.WriteChunk)
		gix.WriteChunk(G10_ChunkId(id), data, size);
}

bool G10_ReadChunk(const char *id, void *data, int size)
{
	if (!gix.ReadChunk)
		return false;
	return gix.ReadChunk(G10_ChunkId(id), data, size, NULL) == size;
}

int G10_ReadChunkAny(const char *id, void **data)
{
	*data = NULL;
	if (!gix.ReadChunk)
		return 0;
	return gix.ReadChunk(G10_ChunkId(id), NULL, 0, data);
}

void G10_Free(void *p)
{
	if (p)
		gix.TagFree(p);
}

void G10_WriteInt(const char *id, int v)
{
	int32_t w = v;
	G10_WriteChunk(id, &w, 4);
}

int G10_ReadInt(const char *id)
{
	int32_t w = 0;
	G10_ReadChunk(id, &w, 4);
	return w;
}

int G10_StringMark(const char *s) { return s ? (int)strlen(s) + 1 : -1; }

void G10_WriteString(const char *s)
{
	if (s)
		G10_WriteChunk("STRG", s, (int)strlen(s) + 1);
}

char *G10_ReadString(int mark)
{
	if (mark < 0)
		return NULL;
	char *out = (char *)gix.TagMalloc(mark > 0 ? mark : 1, TAG_LEVEL);
	out[0] = 0;
	if (mark > 0) {
		// SPEC-752 2.1: the next STRG is read with exactly that length (text and NUL)
		G10_ReadChunk("STRG", out, mark);
		out[mark - 1] = 0;
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// encodings

int G10_EncodeEnt(const edict_t *e)
{
	if (!e || !g_edicts)
		return -1;
	int n = (int)(e - g_edicts);
	return (n >= 0 && n < globals.max_edicts) ? n : -1;
}

edict_t *G10_DecodeEnt(int n)
{
	if (n < 0 || !g_edicts || n >= globals.max_edicts)
		return NULL;
	return &g_edicts[n];
}

int G10_EncodePtr(const void *p)
{
	if (!p)
		return -1;
	const ImageInfo &im = Image();
	const unsigned char *q = (const unsigned char *)p;
	if (!im.base || q < im.base || (uint32_t)(q - im.base) >= im.size)
		return -1;
	return (int)(q - im.base);
}

void *G10_DecodePtr(int code)
{
	if (code < 0)
		return NULL;
	const ImageInfo &im = Image();
	if (!im.base || (uint32_t)code >= im.size)
		return NULL;
	return (void *)(im.base + code);
}

bool G10_Loading(void) { return g_loading != 0; }

// ---------------------------------------------------------------------------------------------------------------
// extension registry

G10Extension *g10_extensions;

G10Extension::G10Extension(const char *i, void (*w)(void), void (*r)(void))
{
	memset(id, 0, sizeof(id));
	strncpy(id, i, 4);
	write = w;
	read = r;
	// sorted by id, so the order never depends on static initialisation order
	G10Extension **pp = &g10_extensions;
	while (*pp && strcmp((*pp)->id, id) < 0)
		pp = &(*pp)->next;
	next = *pp;
	*pp = this;
}
