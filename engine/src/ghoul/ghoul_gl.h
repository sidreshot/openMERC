// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - the OpenGL 1.1 enumerants used through the renderer's GL table.
// Implements: SPEC-010 G (GL entry-point table), SPEC-012 section 8 (drawing state)
#pragma once

namespace ghoul {
namespace gl {
enum : unsigned int
{
	TRIANGLES = 0x0004,
	NEVER = 0x0200,
	LESS = 0x0201,
	LEQUAL = 0x0203,
	GREATER = 0x0204,
	ZERO = 0,
	ONE = 1,
	SRC_ALPHA = 0x0302,
	ONE_MINUS_SRC_ALPHA = 0x0303,
	FRONT = 0x0404,
	BACK = 0x0405,
	CULL_FACE = 0x0B44,
	LIGHTING = 0x0B50,
	LIGHT_MODEL_AMBIENT = 0x0B53,
	FOG = 0x0B60,
	DEPTH_TEST = 0x0B71,
	ALPHA_TEST = 0x0BC0,
	BLEND = 0x0BE2,
	TEXTURE_GEN_S = 0x0C60,
	TEXTURE_GEN_T = 0x0C61,
	UNPACK_ALIGNMENT = 0x0CF5,
	TEXTURE_2D = 0x0DE1,
	UNSIGNED_BYTE = 0x1401,
	ALPHA = 0x1906,
	RGB = 0x1907,
	RGBA = 0x1908,
	AMBIENT = 0x1200,
	DIFFUSE = 0x1201,
	SPECULAR = 0x1202,
	POSITION = 0x1203,
	CONSTANT_ATTENUATION = 0x1207,
	LINEAR_ATTENUATION = 0x1208,
	QUADRATIC_ATTENUATION = 0x1209,
	FRONT_AND_BACK = 0x0408,
	EMISSION = 0x1600,
	SHININESS = 0x1601,
	AMBIENT_AND_DIFFUSE = 0x1602,
	MODELVIEW = 0x1700,
	PROJECTION = 0x1701,
	TEXTURE = 0x1702,
	FLAT = 0x1D00,
	SMOOTH = 0x1D01,
	S = 0x2000,
	T = 0x2001,
	MODULATE = 0x2100,
	TEXTURE_ENV_MODE = 0x2200,
	TEXTURE_ENV = 0x2300,
	TEXTURE_GEN_MODE = 0x2500,
	SPHERE_MAP = 0x2402,
	NEAREST = 0x2600,
	LINEAR = 0x2601,
	LINEAR_MIPMAP_NEAREST = 0x2701,
	// OpenMerc GFX-modern: Materialfv name for the renderer's bloom (not an OpenGL enumerant; the renderer
	// takes it out before OpenGL). params[0] = how strongly the next primitive is a light source: the
	// self-illumination, 1 for additive materials, 0 for none. It applies to the next Begin/End only.
	OM_BLOOM_SOURCE = 0x7F10,
	TEXTURE_MAG_FILTER = 0x2800,
	TEXTURE_MIN_FILTER = 0x2801,
	TEXTURE_WRAP_S = 0x2802,
	TEXTURE_WRAP_T = 0x2803,
	CLAMP = 0x2900,
	REPEAT = 0x2901,
	POLYGON_OFFSET_FILL = 0x8037,
	LIGHT0 = 0x4000,
	MODELVIEW_MATRIX = 0x0BA6
};
} // namespace gl
} // namespace ghoul
