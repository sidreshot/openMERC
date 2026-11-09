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
 * OpenGL3 refresher: Handling shaders
 *
 * =======================================================================
 *
 * OpenMerc: lightmapped world surfaces are alpha-tested (texel alpha < 0.5
 * is discarded) for .m32 textures with alpha (SPEC-100 flag 0x800);
 * si3DlmTrans draws translucent surfaces with their lightmap (SPEC-101).
 * Package I4c: s and t scroll of flowing surfaces, gl_fogmode 1 linear fog
 * and the gl_monolightmap texel modes in the lightmapped shaders, the
 * SoF liquid rules in the water shaders (SPEC-410).
 * Implements: SPEC-100, SPEC-101, SPEC-410
 */

#include "header/local.h"

// TODO: remove eprintf() usage
#define eprintf(...)  R_Printf(PRINT_ALL, __VA_ARGS__)


static GLuint
CompileShader(GLenum shaderType, const char* shaderSrc, const char* shaderSrc2)
{
	GLuint shader = glCreateShader(shaderType);

#ifdef YQ2_GL3_GLES3
	const char* version = GL3_GLES_HEADER;
#else // Desktop GL
	const char* version = "#version 150\n";
#endif
	char* mp = NULL;
#ifdef YQ2_GL3_GLES3
	/* r_om_fs_mediump bit 1 (measurement): the fragment body after the shared uniform blocks in half precision */
	if ((shaderType == GL_FRAGMENT_SHADER) && shaderSrc2 &&
		((int)ri.Cvar_Get("r_om_fs_mediump", "0", 0)->value & 1))
	{
		size_t l = strlen(shaderSrc2) + 32;

		mp = malloc(l);
		if (mp)
		{
			snprintf(mp, l, "precision mediump float;\n%s", shaderSrc2);
			shaderSrc2 = mp;
		}
	}
#endif
	const char* sources[3] = { version, shaderSrc, shaderSrc2 };
	int numSources = shaderSrc2 != NULL ? 3 : 2;
	/* OpenMerc GFX-modern: every fragment shader also writes the emissive target of the post path */
	char* wrapped = (shaderType == GL_FRAGMENT_SHADER) ? GL3_Post_WrapFragment(shaderSrc, shaderSrc2) : NULL;

	if(wrapped != NULL)
	{
		sources[1] = wrapped;
		numSources = 2;
	}

	glShaderSource(shader, numSources, sources, NULL);
	glCompileShader(shader);
	free(wrapped);
	free(mp);
	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if(status != GL_TRUE)
	{
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);
		GLint infoLogLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
		if(infoLogLength >= bufLen)
		{
			bufPtr = malloc(infoLogLength+1);
			bufLen = infoLogLength+1;
			if(bufPtr == NULL)
			{
				bufPtr = buf;
				bufLen = sizeof(buf);
				eprintf("WARN: In CompileShader(), malloc(%d) failed!\n", infoLogLength+1);
			}
		}

		glGetShaderInfoLog(shader, bufLen, NULL, bufPtr);

		const char* shaderTypeStr = "";
		switch(shaderType)
		{
			case GL_VERTEX_SHADER:   shaderTypeStr = "Vertex"; break;
			case GL_FRAGMENT_SHADER: shaderTypeStr = "Fragment"; break;
			// we don't use geometry shaders and GLES3.0 doesn't support them
			// case GL_GEOMETRY_SHADER: shaderTypeStr = "Geometry"; break;
			/* not supported in OpenGL3.2 and we're unlikely to need/use them anyway
			case GL_COMPUTE_SHADER:  shaderTypeStr = "Compute"; break;
			case GL_TESS_CONTROL_SHADER:    shaderTypeStr = "TessControl"; break;
			case GL_TESS_EVALUATION_SHADER: shaderTypeStr = "TessEvaluation"; break;
			*/
		}
		eprintf("ERROR: Compiling %s Shader failed: %s\n", shaderTypeStr, bufPtr);
		glDeleteShader(shader);

		if(bufPtr != buf)  free(bufPtr);

		return 0;
	}

	return shader;
}

static GLuint
CreateShaderProgram(int numShaders, const GLuint* shaders)
{
	int i=0;
	GLuint shaderProgram = glCreateProgram();
	if(shaderProgram == 0)
	{
		eprintf("ERROR: Couldn't create a new Shader Program!\n");
		return 0;
	}

	for(i=0; i<numShaders; ++i)
	{
		glAttachShader(shaderProgram, shaders[i]);
	}

	// make sure all shaders use the same attribute locations for common attributes
	// (so the same VAO can easily be used with different shaders)
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_POSITION, "position");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_TEXCOORD, "texCoord");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_LMTEXCOORD, "lmTexCoord");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_COLOR, "vertColor");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_NORMAL, "normal");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_LIGHTFLAGS, "lightFlags");

	// OpenMerc GFX-modern: two fragment outputs (scene, emissive)
	GL3_Post_BindOutputs(shaderProgram);

	glLinkProgram(shaderProgram);

	GLint status;
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &status);
	if(status != GL_TRUE)
	{
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);
		GLint infoLogLength;
		glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &infoLogLength);
		if(infoLogLength >= bufLen)
		{
			bufPtr = malloc(infoLogLength+1);
			bufLen = infoLogLength+1;
			if(bufPtr == NULL)
			{
				bufPtr = buf;
				bufLen = sizeof(buf);
				eprintf("WARN: In CreateShaderProgram(), malloc(%d) failed!\n", infoLogLength+1);
			}
		}

		glGetProgramInfoLog(shaderProgram, bufLen, NULL, bufPtr);

		eprintf("ERROR: Linking shader program failed: %s\n", bufPtr);

		glDeleteProgram(shaderProgram);

		if(bufPtr != buf)  free(bufPtr);

		return 0;
	}

	for(i=0; i<numShaders; ++i)
	{
		// after linking, they don't need to be attached anymore.
		// no idea  why they even are, if they don't have to..
		glDetachShader(shaderProgram, shaders[i]);
	}

	return shaderProgram;
}

#define MULTILINE_STRING(...) #__VA_ARGS__

// ############## shaders for 2D rendering (HUD, menus, console, videos, ..) #####################

static const char* vertexSrc2D = MULTILINE_STRING(

		in vec2 position; // GL3_ATTRIB_POSITION
		in vec2 texCoord; // GL3_ATTRIB_TEXCOORD

		// for UBO shared between 2D shaders
		layout (std140) uniform uni2D
		{
			mat4 trans;
		};

		out vec2 passTexCoord;

		void main()
		{
			gl_Position = trans * vec4(position, 0.0, 1.0);
			passTexCoord = texCoord;
		}
);

static const char* fragmentSrc2D = MULTILINE_STRING(

		in vec2 passTexCoord;

		// for UBO shared between all shaders (incl. 2D)
		layout (std140) uniform uniCommon
		{
			float gamma;
			float intensity;
			float intensity2D; // for HUD, menu etc

			vec4 color;
		};

		uniform sampler2D tex;

		out vec4 outColor;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);
			// the gl1 renderer used glAlphaFunc(GL_GREATER, 0.666);
			// and glEnable(GL_ALPHA_TEST); for 2D rendering
			// this should do the same
			if(texel.a <= 0.666)
				discard;

			// apply gamma correction and intensity
			texel.rgb *= intensity2D;
			outColor.rgb = pow(texel.rgb, vec3(gamma));
			outColor.a = texel.a; // I think alpha shouldn't be modified by gamma and intensity
		}
);

// like fragmentSrc2D, but also multiplies by color uniform for tinting (e.g. crosshair color)
static const char* fragmentSrc2Dtinted = MULTILINE_STRING(

		in vec2 passTexCoord;

		// for UBO shared between all shaders (incl. 2D)
		layout (std140) uniform uniCommon
		{
			float gamma;
			float intensity;
			float intensity2D; // for HUD, menu etc

			vec4 color;
		};

		uniform sampler2D tex;

		out vec4 outColor;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			if(texel.a <= 0.666)
				discard;

			// apply color tint
			texel.rgb *= color.rgb;

			// apply gamma correction and intensity
			texel.rgb *= intensity2D;
			outColor.rgb = pow(texel.rgb, vec3(gamma));
			outColor.a = texel.a;
		}
);

static const char* fragmentSrc2Dpostprocess = MULTILINE_STRING(
		in vec2 passTexCoord;

		uniform sampler2D tex;
		uniform vec4 v_blend;

		out vec4 outColor;

		void main()
		{
			// no gamma or intensity here, it has been applied before
			// (this is just for postprocessing)
			vec4 res = texture(tex, passTexCoord);
			// apply the v_blend, usually blended as a colored quad with:
			// glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			res.rgb = v_blend.a * v_blend.rgb + (1.0 - v_blend.a)*res.rgb;
			outColor =  res;
		}
);

static const char* fragmentSrc2DpostprocessWater = MULTILINE_STRING(
		in vec2 passTexCoord;

		const float PI = 3.14159265358979323846;

		uniform sampler2D tex;

		uniform float time;
		uniform vec4 v_blend;

		out vec4 outColor;

		void main()
		{
			vec2 uv = passTexCoord;

			// warping based on vkquake2
			// here uv is always between 0 and 1 so ignore all that scrWidth and gl_FragCoord stuff
			//float sx = pc.scale - abs(pc.scrWidth  / 2.0 - gl_FragCoord.x) * 2.0 / pc.scrWidth;
			//float sy = pc.scale - abs(pc.scrHeight / 2.0 - gl_FragCoord.y) * 2.0 / pc.scrHeight;
			float sx = 1.0 - abs(0.5-uv.x)*2.0;
			float sy = 1.0 - abs(0.5-uv.y)*2.0;
			float xShift = 2.0 * time + uv.y * PI * 10.0;
			float yShift = 2.0 * time + uv.x * PI * 10.0;
			vec2 distortion = vec2(sin(xShift) * sx, sin(yShift) * sy) * 0.00666;

			uv += distortion;
			uv = clamp(uv, vec2(0.0, 0.0), vec2(1.0, 1.0));

			// no gamma or intensity here, it has been applied before
			// (this is just for postprocessing)
			vec4 res = texture(tex, uv);
			// apply the v_blend, usually blended as a colored quad with:
			// glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			res.rgb = v_blend.a * v_blend.rgb + (1.0 - v_blend.a)*res.rgb;
			outColor =  res;
		}
);

// 2D color only rendering, GL3_Draw_Fill(), GL3_Draw_FadeScreen()
static const char* vertexSrc2Dcolor = MULTILINE_STRING(

		in vec2 position; // GL3_ATTRIB_POSITION

		// for UBO shared between 2D shaders
		layout (std140) uniform uni2D
		{
			mat4 trans;
		};

		void main()
		{
			gl_Position = trans * vec4(position, 0.0, 1.0);
		}
);

static const char* fragmentSrc2Dcolor = MULTILINE_STRING(

		// for UBO shared between all shaders (incl. 2D)
		layout (std140) uniform uniCommon
		{
			float gamma;
			float intensity;
			float intensity2D; // for HUD, menus etc

			vec4 color;
		};

		out vec4 outColor;

		void main()
		{
			vec3 col = color.rgb * intensity2D;
			outColor.rgb = pow(col, vec3(gamma));
			outColor.a = color.a;
		}
);

// ############## shaders for 3D rendering #####################

static const char* vertexCommon3D = MULTILINE_STRING(

		in vec3 position;   // GL3_ATTRIB_POSITION
		in vec2 texCoord;   // GL3_ATTRIB_TEXCOORD
		in vec2 lmTexCoord; // GL3_ATTRIB_LMTEXCOORD
		in vec4 vertColor;  // GL3_ATTRIB_COLOR
		in vec3 normal;     // GL3_ATTRIB_NORMAL
		in uint lightFlags; // GL3_ATTRIB_LIGHTFLAGS

		out vec2 passTexCoord;

		// for UBO shared between all 3D shaders
		layout (std140) uniform uni3D
		{
			mat4 transProjView;
			mat4 transModel;

			float scroll; // for SURF_FLOWING
			float time;
			float alpha;
			float overbrightbits;
			float particleFadeFactor;
			float lightScaleForTurb; // surfaces with SURF_DRAWTURB (water, lava) don't have lightmaps, use this instead
			float scrollT; // OpenMerc (I4c): flowing surfaces scroll along t too (SPEC-410)
			float omEmissive; // OpenMerc GFX-modern: bloom weight of SURF_LIGHT faces (keeps the size of the old padding)
			vec4 omFogColor;  // OpenMerc (I4c): linear fog of gl_fogmode 1
			vec4 omFogPlane;  // view forward, -forward . eye
			vec4 omFogParams; // start, end, enabled
			vec4 omTexDim;    // OpenMerc GFX-modern: texture size of the face (r_om_pdlight)
			vec4 omPdl;       // OpenMerc GFX-modern: per-pixel SoF dlights: enabled, scale, normal term
		};
);

static const char* fragmentCommon3D = MULTILINE_STRING(

		in vec2 passTexCoord;

		out vec4 outColor;

		// for UBO shared between all shaders (incl. 2D)
		layout (std140) uniform uniCommon
		{
			float gamma; // this is 1.0/vid_gamma
			float intensity;
			float intensity2D; // for HUD, menus etc

			vec4 color; // really?
		};
		// for UBO shared between all 3D shaders
		layout (std140) uniform uni3D
		{
			mat4 transProjView;
			mat4 transModel;

			float scroll; // for SURF_FLOWING
			float time;
			float alpha;
			float overbrightbits;
			float particleFadeFactor;
			float lightScaleForTurb; // surfaces with SURF_DRAWTURB (water, lava) don't have lightmaps, use this instead
			float scrollT; // OpenMerc (I4c): flowing surfaces scroll along t too (SPEC-410)
			float omEmissive; // OpenMerc GFX-modern: bloom weight of SURF_LIGHT faces (keeps the size of the old padding)
			vec4 omFogColor;  // OpenMerc (I4c): linear fog of gl_fogmode 1
			vec4 omFogPlane;  // view forward, -forward . eye
			vec4 omFogParams; // start, end, enabled
			vec4 omTexDim;    // OpenMerc GFX-modern: texture size of the face (r_om_pdlight)
			vec4 omPdl;       // OpenMerc GFX-modern: per-pixel SoF dlights: enabled, scale, normal term
		};
);

static const char* vertexSrc3D = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		void main()
		{
			passTexCoord = texCoord;
			gl_Position = transProjView * transModel * vec4(position, 1.0);
		}
);

static const char* vertexSrc3Dflow = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		void main()
		{
			passTexCoord = texCoord + vec2(scroll, scrollT); // OpenMerc (I4c): SoF scrolls s and t
			gl_Position = transProjView * transModel * vec4(position, 1.0);
		}
);

static const char* vertexSrc3Dlm = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec2 passLMcoord;
		out vec3 passWorldCoord;
		out vec3 passNormal;
		flat out uint passLightFlags;

		void main()
		{
			passTexCoord = texCoord;
			passLMcoord = lmTexCoord;
			vec4 worldCoord = transModel * vec4(position, 1.0);
			passWorldCoord = worldCoord.xyz;
			vec4 worldNormal = transModel * vec4(normal, 0.0f);
			passNormal = normalize(worldNormal.xyz);
			passLightFlags = lightFlags;

			gl_Position = transProjView * worldCoord;
		}
);

static const char* vertexSrc3DlmFlow = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec2 passLMcoord;
		out vec3 passWorldCoord;
		out vec3 passNormal;
		flat out uint passLightFlags;

		void main()
		{
			passTexCoord = texCoord + vec2(scroll, scrollT); // OpenMerc (I4c): SoF scrolls s and t
			passLMcoord = lmTexCoord;
			vec4 worldCoord = transModel * vec4(position, 1.0);
			passWorldCoord = worldCoord.xyz;
			vec4 worldNormal = transModel * vec4(normal, 0.0f);
			passNormal = normalize(worldNormal.xyz);
			passLightFlags = lightFlags;

			gl_Position = transProjView * worldCoord;
		}
);

static const char* fragmentSrc3D = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// apply intensity and gamma
			texel.rgb *= intensity;
			outColor.rgb = pow(texel.rgb, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrc3Dwater = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec2 tc = passTexCoord;
			tc.s += sin( passTexCoord.t*0.125 + time ) * 4.0;
			tc.s += scroll;
			tc.t += sin( passTexCoord.s*0.125 + time ) * 4.0;
			tc *= 1.0/64.0; // do this last

			vec4 texel = texture(tex, tc);

			// apply intensity and gamma
			texel.rgb *= intensity * lightScaleForTurb;
			outColor.rgb = pow(texel.rgb, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrc3Dlm = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		struct DynLight { // gl3UniDynLight in C
			vec3 lightOrigin;
			float _pad;
			//vec3 lightColor;
			//float lightIntensity;
			vec4 lightColor; // .a is intensity; this way it also works on OSX...
			// (otherwise lightIntensity always contained 1 there)
		};

		layout (std140) uniform uniLights
		{
			DynLight dynLights[32];
			uint numDynLights;
			uint _pad1; uint _pad2; uint _pad3; // FFS, AMD!
		};

		uniform sampler2D tex;

		uniform sampler2D lightmap0;
		uniform sampler2D lightmap1;
		uniform sampler2D lightmap2;
		uniform sampler2D lightmap3;

		uniform vec4 lmScales[4];

		// OpenMerc: 1 for translucent surfaces with lightmap (si3DlmTrans)
		uniform int omTranslucent;

		// OpenMerc GFX-modern r_om_seams: 1 for the strips that blend a texture seam (si3DlmSeam, gl3_surf.c);
		// the seam line (a point on it, the strip width) and the direction into the face
		uniform int omSeam;
		uniform vec4 omSeamP;
		uniform vec4 omSeamD;
		// the face's own texture (GL_TEXTURE10) and its texture axes divided by the image size
		uniform sampler2D omSeamOwn;
		uniform vec4 omSeamS;
		uniform vec4 omSeamT;

		// OpenMerc: SPEC-410 lightmap colour correction (256 x 1, GL_TEXTURE5;
		// all 255 for r_fullbright and night vision)
		uniform sampler2D omColorTable;

		// OpenMerc (I4c): dynamic light of SoF maps per lightmap sample (gl3_dynlight.c), 0 elsewhere
		uniform sampler2D omDynLight;
		// OpenMerc: the detail texture in the program (gl3_detail.c GL3_Detail_InShader), GL_TEXTURE6 while the
		// per-pixel lights leave the CPU light layer unused; scale st, scroll st (0 = no detail)
		uniform sampler2D omDetail;
		uniform vec4 omDetailP;

		// OpenMerc (I4c): gl_monolightmap latched at map load: 0 '0', 1 L, 2 I, 3 C, 4 A, 5 other
		uniform int omMono;

		in vec2 passLMcoord;
		in vec3 passWorldCoord;
		in vec3 passNormal;
		flat in uint passLightFlags;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// OpenMerc: alpha-tested world textures (SoF .m32 flag 0x800,
			// grates, fences; also Quake II palette index 255); GFX-modern r_om_a2c: sample coverage under MSAA
			float omCov = omCoverage(texel.a);
			if(omTranslucent == 0 && omCov <= 0.0)
				discard;

			// apply intensity
			texel.rgb *= intensity;

			// apply lightmap
			vec4 lmTex = texture(lightmap0, passLMcoord) * lmScales[0];
			// OpenMerc: the other light styles only where the face has them (most faces: style 0 alone; uniform branch, no fetch)
			if(lmScales[1] != vec4(0.0)) lmTex += texture(lightmap1, passLMcoord) * lmScales[1];
			if(lmScales[2] != vec4(0.0)) lmTex += texture(lightmap2, passLMcoord) * lmScales[2];
			if(lmScales[3] != vec4(0.0)) lmTex += texture(lightmap3, passLMcoord) * lmScales[3];
			vec3 omLm0 = lmTex.rgb; // the lightmap before the shadows of moving casters (their common floor)
			omCsmApply(lmTex.rgb, passWorldCoord, passNormal); // GFX-modern: sun shadows of moving casters (gl3_csm.c)
			omLsApply(lmTex.rgb, omLm0, passWorldCoord, passNormal); // GFX-modern: lamp shadows of moving casters (gl3_lshadow.c)
			lmTex.rgb += omLightningLight(passWorldCoord, passNormal); // GFX-modern r_om_rain: lightning
			if(omPdl.x < 0.5) // r_om_pdlight replaces the CPU dlight layer (it stays black then)
				lmTex.rgb += texture(omDynLight, passLMcoord).rgb; // OpenMerc (I4c): not multiplied by gl_modulate

			if(passLightFlags != 0u && omPdl.x > 0.5)
			{
				lmTex.rgb += omSoFDlights(passLightFlags); // GFX-modern r_om_pdlight
			}
			else if(passLightFlags != 0u)
			{
				// TODO: or is hardcoding 32 better?
				for(uint i=0u; i<numDynLights; ++i)
				{
					// I made the following up, it's probably not too cool..
					// it basically checks if the light is on the right side of the surface
					// and, if it is, sets intensity according to distance between light and pixel on surface

					// dyn light number i does not affect this plane, just skip it
					if((passLightFlags & (1u << i)) == 0u)  continue;

					float intens = dynLights[i].lightColor.a;

					vec3 lightToPos = dynLights[i].lightOrigin - passWorldCoord;
					float distLightToPos = length(lightToPos);
					float fact = max(0.0, intens - distLightToPos - 52.0);

					// move the light source a bit further above the surface
					// => helps if the lightsource is so close to the surface (e.g. grenades, rockets)
					//    that the dot product below would return 0
					// (light sources that are below the surface are filtered out by lightFlags)
					lightToPos += passNormal*32.0;

					// also factor in angle between light and point on surface
					fact *= max(0.0, dot(passNormal, normalize(lightToPos)));


					lmTex.rgb += dynLights[i].lightColor.rgb * fact * (1.0/256.0);
				}
			}

			// OpenMerc, SPEC-410 lightmap building: the summed light is rounded and
			// clamped at 0, scaled down when its brightest channel exceeds 255, and
			// stored through the image colour table
			vec3 omL = max(floor(lmTex.rgb * 255.0 + 0.5), vec3(0.0));
			float omA = max(omL.r, max(omL.g, omL.b));
			if(omA > 255.0)
			{
				omL = floor(omL * (255.0 / omA));
				omA = floor(omA * (255.0 / omA));
			}
			// OpenMerc (I4c): the texel by gl_monolightmap (SPEC-410 Lightmaps): 0 colour table (RGB),
			// L luminance A, I intensity A, C colour-weighted alpha (RGBA), A (0,0,0,255-A) RGBA, others RGB black;
			// the lightmap alpha reaches the alpha test (texture x lightmap, multitexture modulate)
			float omLmAlpha = 1.0;
			if(omMono == 0)
			{
				lmTex.r = texture(omColorTable, vec2((omL.r + 0.5) / 256.0, 0.5)).r;
				lmTex.g = texture(omColorTable, vec2((omL.g + 0.5) / 256.0, 0.5)).r;
				lmTex.b = texture(omColorTable, vec2((omL.b + 0.5) / 256.0, 0.5)).r;
			}
			else if(omMono == 1)
			{
				lmTex.rgb = vec3(omA / 255.0);
			}
			else if(omMono == 2)
			{
				lmTex.rgb = vec3(omA / 255.0);
				omLmAlpha = omA / 255.0;
			}
			else if(omMono == 3)
			{
				float omC = 255.0 - floor((omL.r + omL.g + omL.b) / 3.0);
				lmTex.rgb = floor(omL * (omC / 255.0)) / 255.0;
				omLmAlpha = omC / 255.0;
			}
			else
			{
				lmTex.rgb = vec3(0.0);
				omLmAlpha = (omMono == 4) ? (255.0 - omA) / 255.0 : 1.0;
			}
			if(omTranslucent == 0 && omLmAlpha < 1.0 && texel.a * omLmAlpha <= 0.05) // (omLmAlpha 1: tested above)
				discard;

			lmTex.rgb *= overbrightbits;
			// OpenMerc GFX-modern r_om_ssr: material of opaque faces (translucent ones keep the default and fade
			// what is behind them); the reflection pass smooths it across texture seams and adds the wet darkening
			// and the lamp glints (gl3_post.c)
			float omWetK = 0.0;
			vec4 omMat = (omTranslucent == 0) ? omMaterial(passWorldCoord, passNormal, omWetK) : vec4(0.0);
			outColor = lmTex*texel;
			float omFogK = 1.0;
			// OpenMerc (I4c): gl_fogmode 1, linear fog of the face's fog region (SPEC-410)
			if(omFogParams.z != 0.0)
			{
				float omFd = abs(dot(omFogPlane.xyz, passWorldCoord) + omFogPlane.w);
				float omDen = omFogParams.y - omFogParams.x;
				if(abs(omDen) < 0.0001) omDen = 0.0001;
				omFogK = clamp((omFogParams.y - omFd) / omDen, 0.0, 1.0);
				outColor.rgb = mix(omFogColor.rgb, outColor.rgb, omFogK);
			}
			outColor.rgb = pow(outColor.rgb, vec3(gamma)); // apply gamma correction to result

			outColor.a = (omTranslucent != 0) ? texel.a*alpha : omCov; // OpenMerc: SoF translucent surfaces have lightmaps (opaque: 1, or the r_om_a2c coverage)
			// OpenMerc GFX-modern: SURF_LIGHT faces are bloom sources, hidden by the fog in front of them
			if(omEmissive > 0.0)
				outEmissive = vec4(pow(lmTex.rgb * texel.rgb, vec3(gamma)) * (omFogK * omEmissive), outColor.a);
			if(omTranslucent == 0)
				outMaterial = vec4(omMat.rg * omFogK, omWetK * omFogK, clamp(dot(lmTex.rgb, vec3(0.3, 0.5, 0.2)), 0.0, 1.0)); // OpenMerc GFX-modern r_om_ssr: reflectivity, x roughness, wetness, light
			// OpenMerc GFX-modern r_om_mirror: a face on the mirrored plane shows the mirror image (no screen-space reflection)
			if(abs(omEmissive + 11.0) < 0.5 && (omMirror.x > 0.5 || omMirror.y > 0.5))
			{
				vec3 omMc = omMirrorColor(outColor.rgb, passWorldCoord);
				if(omMc != outColor.rgb)
				{
					outColor.rgb = omMc;
					outMaterial = vec4(0.0, 0.0, 0.0, 1.0);
				}
			}
			// OpenMerc: the detail pass's 2 x dst x src (FX blend 5) on the finished colour; the emissive output
			// keeps its value as under the pass (its neutral 0.5)
			if(omDetailP.x != 0.0)
				outColor.rgb *= 2.0 * texture(omDetail, passTexCoord * omDetailP.xy + omDetailP.zw).rgb;
			// OpenMerc GFX-modern r_om_seams: height blend of the two textures (Mishkinis, "Advanced Terrain Texture
			// Splatting"; Unity "Material by Height"): each texture's height (texel brightness above its mean) plus
			// its share - the neighbour's half at the seam, none at the strip's inner end, a world-space noise moving
			// the line - and only the higher one shows, mixed within a narrow depth: stones of one side poke into
			// the grout of the other instead of a straight cross-fade
			if(omSeam != 0)
			{
				float omSb = clamp(1.0 - dot(passWorldCoord - omSeamP.xyz, omSeamD.xyz) / omSeamP.w, 0.0, 1.0);
				vec3 omLw = vec3(0.3, 0.59, 0.11);
				float omSh = dot(texture(tex, passTexCoord).rgb - textureLod(tex, passTexCoord, 12.0).rgb, omLw);
				vec2 omOwnTc = vec2(dot(passWorldCoord, omSeamS.xyz) + omSeamS.w, dot(passWorldCoord, omSeamT.xyz) + omSeamT.w);
				float omOh = dot(texture(omSeamOwn, omOwnTc).rgb - textureLod(omSeamOwn, omOwnTc, 12.0).rgb, omLw);
				vec3 omQ = passWorldCoord * 0.11;
				vec3 omQi = floor(omQ);
				vec3 omQf = fract(omQ);
				omQf = omQf * omQf * (3.0 - 2.0 * omQf);
				vec4 omHk = vec4(0.0, 1.0, 57.0, 58.0) + dot(omQi, vec3(1.0, 57.0, 113.0));
				vec4 omN0 = fract(sin(omHk) * 43758.5453);
				vec4 omN1 = fract(sin(omHk + 113.0) * 43758.5453);
				vec4 omNz = mix(omN0, omN1, omQf.z);
				vec2 omNy = mix(omNz.xz, omNz.yw, omQf.x);
				float omSn = mix(omNy.x, omNy.y, omQf.y);
				float omSm = omSb * (1.0 - omSb) * 4.0;
				float omA2 = clamp(0.5 * omSb + omSm * 0.35 * (omSn - 0.5), 0.0, 1.0);
				float omH2 = omA2 + 1.5 * omSh * omSm, omH1 = (1.0 - omA2) + 1.5 * omOh * omSm;
				float omHm = max(omH1, omH2) - 0.12;
				float omW1 = max(omH1 - omHm, 0.0), omW2 = max(omH2 - omHm, 0.0);
				outColor.a = omW2 / max(omW1 + omW2, 1e-4);
				if(outColor.a < 0.004)
					discard;
				if(omSeam == 2)
					outColor.rgb = vec3(1.0, 0.0, 0.0); // r_om_seams 2: where the strips lie
			}
		}
);

/* GFX-modern r_om_pdlight: spliced into the lightmapped fragment shaders before main() */
static const char* fragmentSrcOmPdl =
	"// OpenMerc GFX-modern (r_om_pdlight): the SoF dynamic-light rule of SPEC-410 evaluated at the\n"
	"// fragment instead of at the lightmap samples: d = distance of the light from the plane,\n"
	"// r = intensity - |d| (nothing below 64), ds/dt = distances from the light's projection along\n"
	"// the texture axes in texture units, dist = max + min/2, light (r - dist) inside r - 64. The\n"
	"// hard 64 step at the edge, which the sample grid smooths bilinearly over one sample, gets a\n"
	"// linear ramp of 16 texture units around it.\n"
	"vec3 omSoFDlights(uint flags)\n"
	"{\n"
	"	vec3 P = passWorldCoord;\n"
	"	vec3 N = normalize(passNormal);\n"
	"	vec3 px = dFdx(P);\n"
	"	vec3 py = dFdy(P);\n"
	"	vec2 st = passTexCoord * omTexDim.xy;\n"
	"	vec2 sx = dFdx(st);\n"
	"	vec2 sy = dFdy(st);\n"
	"	float a = dot(px, px);\n"
	"	float b = dot(px, py);\n"
	"	float c = dot(py, py);\n"
	"	float det = a * c - b * b;\n"
	"	bool axes = (omTexDim.x > 0.0) && (det > 1.0e-6 * a * c);\n"
	"	vec3 gS = vec3(0.0);\n"
	"	vec3 gT = vec3(0.0);\n"
	"	if(axes)\n"
	"	{\n"
	"		gS = ((c * sx.x - b * sy.x) * px + (a * sy.x - b * sx.x) * py) / det;\n"
	"		gT = ((c * sx.y - b * sy.y) * px + (a * sy.y - b * sx.y) * py) / det;\n"
	"	}\n"
	"	vec3 sum = vec3(0.0);\n"
	"	for(uint i = 0u; i < numDynLights; ++i)\n"
	"	{\n"
	"		if((flags & (1u << i)) == 0u) continue;\n"
	"		vec3 L = dynLights[i].lightOrigin;\n"
	"		float d = dot(L - P, N);\n"
	"		float r = dynLights[i].lightColor.a - abs(d);\n"
	"		if(r < 64.0) continue;\n"
	"		vec3 v = (L - d * N) - P;\n"
	"		float ds = axes ? abs(dot(v, gS)) : length(v);\n"
	"		float dt = axes ? abs(dot(v, gT)) : 0.0;\n"
	"		float x = r - (max(ds, dt) + 0.5 * min(ds, dt));\n"
	"		// omPdl.w > 0 (r_om_pdlight_soft): the edge eases in over w units from the same start\n"
	"		float add = (omPdl.w > 0.0) ? x * smoothstep(56.0, 56.0 + omPdl.w, x)\n"
	"			: ((x >= 72.0) ? x : 72.0 * clamp((x - 56.0) / 16.0, 0.0, 1.0));\n"
	"		if(omPdl.z > 0.0)\n"
	"			add *= mix(1.0, abs(dot(N, normalize(L - P))), omPdl.z);\n"
	"		sum += dynLights[i].lightColor.rgb * add;\n"
	"	}\n"
	"	return sum * (omPdl.y / 255.0);\n"
	"}\n"
	;

/* GFX-modern: functions of the world shaders - lightning light, reflection material, mirrors */
static const char* fragmentSrcOmWorld =
	/* GFX-modern r_om_a2c: the alpha test of opaque faces (grates, fences, wires). omA2C 1 (MSAA target, the draw has
	   GL_SAMPLE_ALPHA_TO_COVERAGE on, gl3_main.c): the 0.05 threshold is widened to about one pixel by the alpha's
	   screen derivative and the result becomes the share of covered samples, so the edge is antialiased like a
	   geometry edge; 0: the plain test */
	"uniform int omA2C;\n"
	"float omCoverage(float a)\n"
	"{\n"
	"	float w = fwidth(a);\n"
	"	if (omA2C == 0)\n"
	"		return (a <= 0.05) ? 0.0 : 1.0;\n"
	"	return clamp((a - 0.05) / max(w, 0.0001) + 0.5, 0.0, 1.0);\n"
	"}\n"
	/* GFX-modern r_om_rain: the light of a lightning strike (gl3_post.c). The channel is kilometres away: its light
	   comes in parallel along omLt.xyz and reaches only what sees it - a shadow map of the static world made along
	   that direction once per strike decides, so nothing behind a wall or under a roof is lit. The lit cloud deck adds
	   a little light from straight above where the sky is directly overhead (the top-down map, no sideways walk). */
	"uniform vec4 omLt;\n"        /* unit direction towards the channel, strength (0 = none) */
	"uniform vec4 omLtCol;\n"
	"uniform mat4 omLtOccVP;\n"   /* top-down depth map of the static world (rain, wet ground, the cloud light) */
	"uniform sampler2D omLtOcc;\n"
	"uniform mat4 omLtShVP;\n"    /* the strike's shadow map */
	"uniform sampler2D omLtSh;\n"
	"vec3 omLightningLight(vec3 wp, vec3 nrm)\n"
	"{\n"
	"	if (omLt.w <= 0.0)\n"
	"		return vec3(0.0);\n"
	"	vec3 N = normalize(nrm);\n"
	"	float ndl = dot(N, omLt.xyz);\n"
	"	float direct = 0.0, sky = 0.0;\n"
	"	if (ndl > 0.0)\n"
	"	{\n"
	/* moved off the face along the normal and biased in depth, both about a unit and more only where the light
	   grazes the face (acne): 3 units of offset and 3.6 of bias lit every face within ~7 units under a ceiling - the
	   ceiling slab has no upper face in the BSP, the moved point went through it (lit cornices and room corners).
	   3x3 taps one texel (2 units) apart */
	"		float tg = sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.05);\n"
	"		vec4 sc = omLtShVP * vec4(wp + N * (0.5 + 1.5 * (1.0 - ndl)), 1.0);\n"
	"		vec3 su = sc.xyz * 0.5 + 0.5;\n"
	"		if (all(greaterThan(su.xy, vec2(0.0))) && all(lessThan(su.xy, vec2(1.0))))\n"
	"		{\n"
	/* lit only where all nine taps see the channel: behind the outer walls of a map there is no face at all, a tap that
	   fell past the wall saw open sky and lit the joint of wall and ceiling */
	"			float vis = 1.0, t = 1.0 / 2048.0, bias = 0.000111 * min(0.75 + tg, 1.5);\n"
	"			for (int j = -1; j <= 1; j++)\n"
	"				for (int i = -1; i <= 1; i++)\n"
	"					vis = min(vis, step(su.z - bias, texture(omLtSh, su.xy + vec2(float(i), float(j)) * t).r));\n"
	"			direct = ndl * vis;\n"
	"		}\n"
	"	}\n"
	"	{\n"
	/* the top-down map (3 units a texel, 1 unit = 0.0002 of its depth). The texel a wall runs through holds what is on
	   its other side (an open street: lit): a wall point is moved a texel and a half into its own room, a unit up or
	   down; the nearest occluder of the 3x3 texels around (a cornice by the wall sees the ceiling over it), a tolerance
	   of 1.5 units, up to 4 on slopes. (It was 4 units along the normal and 7.5 of tolerance: every face within ~8
	   units under a ceiling was lit - cornices, room corners) */
	"		float ts = sqrt(max(1.0 - N.z * N.z, 0.0)) / max(abs(N.z), 0.05);\n"
	"		vec4 oc = omLtOccVP * vec4(wp + vec3(N.xy * 4.5, N.z), 1.0);\n"
	"		vec2 ou = oc.xy * 0.5 + 0.5;\n"
	"		if (all(greaterThan(ou, vec2(0.0))) && all(lessThan(ou, vec2(1.0))))\n"
	"		{\n"
	"			float od = 1.0, t = 1.0 / 1024.0;\n"
	"			for (int j = -1; j <= 1; j++)\n"
	"				for (int i = -1; i <= 1; i++)\n"
	"					od = min(od, texture(omLtOcc, ou + vec2(float(i), float(j)) * t).r);\n"
	/* the slope tolerance only for ground and roofs; a wall is lit from above only where nothing is over it at all
	   (with the slope rule the top 4 units of every wall under a ceiling were lit) */
	"			float e0 = N.z > 0.3 ? 0.0 : -1.0, e1 = N.z > 0.3 ? clamp(1.5 + 3.0 * ts, 1.5, 4.0) : -0.25;\n"
	"			sky = (1.0 - smoothstep(0.0002 * e0, 0.0002 * e1, oc.z * 0.5 + 0.5 - od)) * (0.5 + 0.5 * N.z);\n"
	"		}\n"
	"	}\n"
	"	if (omLtCol.a > 1.5) return omLtCol.a > 2.5 ? vec3(0.0, sky * 4.0, 0.0) : vec3(direct * 4.0, 0.0, 0.0);\n" /* r_om_lightning_test 2 / 3 */
	"	return omLtCol.rgb * omLt.w * (direct + 0.22 * sky);\n"
	"}\n"
	/* GFX-modern r_om_ssr: what the reflection pass (gl3_post.c) gets from a face: reflectivity and roughness by the
	   SoF material class and how smooth the texture is (little fine detail = polished), floors more than walls;
	   under the rain (the same top-down map as the lightning) open ground is wet, with puddles from a noise */
	"uniform vec4 omSsr;\n"   /* x reflections on (0 = off), y rain wetness (0 = dry) */
	/* GFX-modern r_om_mirror: the world seen in the mirror plane (gl3_post.c), replaces the faces on that plane */
	"uniform vec4 omMirror;\n"       /* x first plane on, y second plane on */
	"uniform vec4 omMirrorPlane;\n"  /* normal, dist */
	"uniform vec4 omMirrorPlane2;\n"
	"uniform vec4 omMirrorRect;\n"   /* 1/width, 1/height of the textures, strength */
	"uniform sampler2D omMirrorTex;\n"
	"uniform sampler2D omMirrorTex2;\n"
	"vec3 omMirrorColor(vec3 c, vec3 wp)\n"
	"{\n"
	"	vec2 su = gl_FragCoord.xy * omMirrorRect.xy;\n"
	"	if (omMirror.x > 0.5 && abs(dot(omMirrorPlane.xyz, wp) - omMirrorPlane.w) <= 1.5)\n"
	"		return mix(c, texture(omMirrorTex, su).rgb, omMirrorRect.z * 0.92);\n"
	"	if (omMirror.y > 0.5 && abs(dot(omMirrorPlane2.xyz, wp) - omMirrorPlane2.w) <= 1.5)\n"
	"		return mix(c, texture(omMirrorTex2, su).rgb, omMirrorRect.z * 0.92);\n"
	"	return c;\n"
	"}\n"
	"float omH2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
	"float omVN(vec2 p)\n"
	"{\n"
	"	vec2 i = floor(p), f = fract(p);\n"
	"	f = f * f * (3.0 - 2.0 * f);\n"
	"	return mix(mix(omH2(i), omH2(i + vec2(1.0, 0.0)), f.x), mix(omH2(i + vec2(0.0, 1.0)), omH2(i + vec2(1.0)), f.x), f.y);\n"
	"}\n"
	"float omWet(vec3 wp, vec3 N)\n"
	"{\n"
	"	if (omSsr.y <= 0.0 || N.z < -0.3)\n"
	"		return 0.0;\n"
	"	vec4 oc = omLtOccVP * vec4(wp + N * 4.0, 1.0);\n"
	"	vec2 ou = oc.xy * 0.5 + 0.5;\n"
	"	if (any(lessThan(ou, vec2(0.0))) || any(greaterThan(ou, vec2(1.0))))\n"
	"		return 0.0;\n"
	"	float z = oc.z * 0.5 + 0.5, t = 2.0 / 1024.0, s = 0.0;\n"
	"	for (int j = 0; j < 4; j++)\n"
	"	{\n"
	"		vec2 o = vec2((j & 1) == 0 ? -t : t, (j & 2) == 0 ? -t : t);\n"
	"		s += 1.0 - smoothstep(0.0, 0.002, z - texture(omLtOcc, ou + o).r);\n"
	"	}\n"
	"	return s * 0.25 * omSsr.y;\n"
	"}\n"
	/* the material classes (gl3_material.c, materials/om_materials.txt) and the material map of the texture */
	"uniform vec4 omMatA[24];\n"    /* floor reflectivity, roughness, wall factor, ceiling factor */
	"uniform vec4 omMatB[24];\n"    /* gloss map weight, stain weight, natural dampness, rain water held */
	"uniform sampler2D omMatMap;\n" /* R gloss (dark grout, cracks, grime and grit are dull), G height, B rust/dirt, A window panes */
	"vec4 omMaterial(vec3 wp, vec3 nrm, out float wet)\n"
	"{\n"
	"	wet = 0.0;\n"
	"	if (omSsr.x <= 0.0 || omEmissive > 0.0)\n"
	"		return vec4(0.0, 0.0, 0.0, 1.0);\n"
	"	vec3 N = normalize(nrm);\n"
	"	int mc = clamp(omEmissive < 0.0 ? int(-omEmissive + 0.5) : 0, 0, 23);\n"
	"	vec4 A = omMatA[mc], B = omMatB[mc];\n"
	"	float fl = smoothstep(0.6, 0.85, N.z);\n"
	"	vec4 mm = texture(omMatMap, passTexCoord);\n"
	/* the texels decide where the class shines: dull spots do not reflect at all, they only get rougher */
	/* glass: only the panes of a painted window shine (A), not its frame, sill or the wall around it */
	"	float g = mix(1.0, mc == 4 ? mm.a : mm.r, B.x) * (1.0 - mm.b * B.y);\n"
	"	float r = A.x * smoothstep(0.08, 0.55, g);\n"
	"	float rough = min(1.0, A.y + 0.45 * (1.0 - g));\n"
	/* walls and ceilings by their own factor of the class (corridor walls seen at grazing angles looked as glossy as
	   the floor); mirrors and glass keep theirs */
	"	r *= mix(N.z < -0.7 ? A.w : A.z, 1.0, fl);\n"
	/* wet: open to the rain, or damp by nature (sewers, catacombs). What holds water (soak) gets puddles from a noise
	   and water in the low parts of the texture first (grout, cracks); porous ground only darkens and sheens a little,
	   carpets only darken */
	"	wet = max(omWet(wp, N), B.z * omSsr.w);\n"
	"	if (wet > 0.0)\n"
	"	{\n"
	"		float low = smoothstep(0.42, 0.22, mm.g) * fl;\n"
	"		float pud = max(smoothstep(0.56, 0.6, omVN(wp.xy / 96.0) * 0.7 + omVN(wp.xy / 23.0) * 0.3) * fl, low * 0.7)\n"
	"			* step(0.5, B.w);\n"
	"		float wr = (mix(0.16, 0.5, pud) * fl + 0.04 * B.z * (1.0 - fl)) * B.w;\n"
	"		r = mix(r, max(r, wr), wet);\n"
	"		rough = mix(rough, mix(0.3, 0.06, pud) - 0.1 * B.z * (1.0 - fl), wet * B.w);\n"
	"		wet *= 0.6 + 0.4 * pud;\n"
	"	}\n"
	"	return vec4(r, r * clamp(rough, 0.0, 1.0), 0.0, 1.0);\n"
	"}\n"
	;
static const char* fragmentSrc3DlmNoColor = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		struct DynLight { // gl3UniDynLight in C
			vec3 lightOrigin;
			float _pad;
			//vec3 lightColor;
			//float lightIntensity;
			vec4 lightColor; // .a is intensity; this way it also works on OSX...
			// (otherwise lightIntensity always contained 1 there)
		};

		layout (std140) uniform uniLights
		{
			DynLight dynLights[32];
			uint numDynLights;
			uint _pad1; uint _pad2; uint _pad3; // FFS, AMD!
		};

		uniform sampler2D tex;

		uniform sampler2D lightmap0;
		uniform sampler2D lightmap1;
		uniform sampler2D lightmap2;
		uniform sampler2D lightmap3;

		uniform vec4 lmScales[4];

		// OpenMerc: 1 for translucent surfaces with lightmap (si3DlmTrans)
		uniform int omTranslucent;

		// OpenMerc GFX-modern r_om_seams: 1 for the strips that blend a texture seam (si3DlmSeam, gl3_surf.c);
		// the seam line (a point on it, the strip width) and the direction into the face
		uniform int omSeam;
		uniform vec4 omSeamP;
		uniform vec4 omSeamD;
		// the face's own texture (GL_TEXTURE10) and its texture axes divided by the image size
		uniform sampler2D omSeamOwn;
		uniform vec4 omSeamS;
		uniform vec4 omSeamT;

		// OpenMerc: SPEC-410 lightmap colour correction (256 x 1, GL_TEXTURE5;
		// all 255 for r_fullbright and night vision)
		uniform sampler2D omColorTable;

		// OpenMerc (I4c): dynamic light of SoF maps per lightmap sample (gl3_dynlight.c), 0 elsewhere
		uniform sampler2D omDynLight;
		// OpenMerc: the detail texture in the program (gl3_detail.c GL3_Detail_InShader), GL_TEXTURE6 while the
		// per-pixel lights leave the CPU light layer unused; scale st, scroll st (0 = no detail)
		uniform sampler2D omDetail;
		uniform vec4 omDetailP;

		// OpenMerc (I4c): gl_monolightmap latched at map load: 0 '0', 1 L, 2 I, 3 C, 4 A, 5 other
		uniform int omMono;

		in vec2 passLMcoord;
		in vec3 passWorldCoord;
		in vec3 passNormal;
		flat in uint passLightFlags;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// OpenMerc: alpha-tested world textures (SoF .m32 flag 0x800,
			// grates, fences; also Quake II palette index 255); GFX-modern r_om_a2c: sample coverage under MSAA
			float omCov = omCoverage(texel.a);
			if(omTranslucent == 0 && omCov <= 0.0)
				discard;

			// apply intensity
			texel.rgb *= intensity;

			// apply lightmap
			vec4 lmTex = texture(lightmap0, passLMcoord) * lmScales[0];
			// OpenMerc: the other light styles only where the face has them (most faces: style 0 alone; uniform branch, no fetch)
			if(lmScales[1] != vec4(0.0)) lmTex += texture(lightmap1, passLMcoord) * lmScales[1];
			if(lmScales[2] != vec4(0.0)) lmTex += texture(lightmap2, passLMcoord) * lmScales[2];
			if(lmScales[3] != vec4(0.0)) lmTex += texture(lightmap3, passLMcoord) * lmScales[3];
			vec3 omLm0 = lmTex.rgb; // the lightmap before the shadows of moving casters (their common floor)
			omCsmApply(lmTex.rgb, passWorldCoord, passNormal); // GFX-modern: sun shadows of moving casters (gl3_csm.c)
			omLsApply(lmTex.rgb, omLm0, passWorldCoord, passNormal); // GFX-modern: lamp shadows of moving casters (gl3_lshadow.c)
			lmTex.rgb += omLightningLight(passWorldCoord, passNormal); // GFX-modern r_om_rain: lightning
			if(omPdl.x < 0.5) // r_om_pdlight replaces the CPU dlight layer (it stays black then)
				lmTex.rgb += texture(omDynLight, passLMcoord).rgb; // OpenMerc (I4c): not multiplied by gl_modulate

			if(passLightFlags != 0u && omPdl.x > 0.5)
			{
				lmTex.rgb += omSoFDlights(passLightFlags); // GFX-modern r_om_pdlight
			}
			else if(passLightFlags != 0u)
			{
				// TODO: or is hardcoding 32 better?
				for(uint i=0u; i<numDynLights; ++i)
				{
					// I made the following up, it's probably not too cool..
					// it basically checks if the light is on the right side of the surface
					// and, if it is, sets intensity according to distance between light and pixel on surface

					// dyn light number i does not affect this plane, just skip it
					if((passLightFlags & (1u << i)) == 0u)  continue;

					float intens = dynLights[i].lightColor.a;

					vec3 lightToPos = dynLights[i].lightOrigin - passWorldCoord;
					float distLightToPos = length(lightToPos);
					float fact = max(0.0, intens - distLightToPos - 52.0);

					// move the light source a bit further above the surface
					// => helps if the lightsource is so close to the surface (e.g. grenades, rockets)
					//    that the dot product below would return 0
					// (light sources that are below the surface are filtered out by lightFlags)
					lightToPos += passNormal*32.0;

					// also factor in angle between light and point on surface
					fact *= max(0.0, dot(passNormal, normalize(lightToPos)));


					lmTex.rgb += dynLights[i].lightColor.rgb * fact * (1.0/256.0);
				}
			}

			// turn lightcolor into grey for gl3_colorlight 0
			lmTex.rgb = vec3(0.333 * (lmTex.r+lmTex.g+lmTex.b));

			// OpenMerc, SPEC-410 lightmap building: the summed light is rounded and
			// clamped at 0, scaled down when its brightest channel exceeds 255, and
			// stored through the image colour table
			vec3 omL = max(floor(lmTex.rgb * 255.0 + 0.5), vec3(0.0));
			float omA = max(omL.r, max(omL.g, omL.b));
			if(omA > 255.0)
			{
				omL = floor(omL * (255.0 / omA));
				omA = floor(omA * (255.0 / omA));
			}
			// OpenMerc (I4c): the texel by gl_monolightmap (SPEC-410 Lightmaps): 0 colour table (RGB),
			// L luminance A, I intensity A, C colour-weighted alpha (RGBA), A (0,0,0,255-A) RGBA, others RGB black;
			// the lightmap alpha reaches the alpha test (texture x lightmap, multitexture modulate)
			float omLmAlpha = 1.0;
			if(omMono == 0)
			{
				lmTex.r = texture(omColorTable, vec2((omL.r + 0.5) / 256.0, 0.5)).r;
				lmTex.g = texture(omColorTable, vec2((omL.g + 0.5) / 256.0, 0.5)).r;
				lmTex.b = texture(omColorTable, vec2((omL.b + 0.5) / 256.0, 0.5)).r;
			}
			else if(omMono == 1)
			{
				lmTex.rgb = vec3(omA / 255.0);
			}
			else if(omMono == 2)
			{
				lmTex.rgb = vec3(omA / 255.0);
				omLmAlpha = omA / 255.0;
			}
			else if(omMono == 3)
			{
				float omC = 255.0 - floor((omL.r + omL.g + omL.b) / 3.0);
				lmTex.rgb = floor(omL * (omC / 255.0)) / 255.0;
				omLmAlpha = omC / 255.0;
			}
			else
			{
				lmTex.rgb = vec3(0.0);
				omLmAlpha = (omMono == 4) ? (255.0 - omA) / 255.0 : 1.0;
			}
			if(omTranslucent == 0 && omLmAlpha < 1.0 && texel.a * omLmAlpha <= 0.05) // (omLmAlpha 1: tested above)
				discard;

			lmTex.rgb *= overbrightbits;
			// OpenMerc GFX-modern r_om_ssr: material of opaque faces (translucent ones keep the default and fade
			// what is behind them); the reflection pass smooths it across texture seams and adds the wet darkening
			// and the lamp glints (gl3_post.c)
			float omWetK = 0.0;
			vec4 omMat = (omTranslucent == 0) ? omMaterial(passWorldCoord, passNormal, omWetK) : vec4(0.0);
			outColor = lmTex*texel;
			float omFogK = 1.0;
			// OpenMerc (I4c): gl_fogmode 1, linear fog of the face's fog region (SPEC-410)
			if(omFogParams.z != 0.0)
			{
				float omFd = abs(dot(omFogPlane.xyz, passWorldCoord) + omFogPlane.w);
				float omDen = omFogParams.y - omFogParams.x;
				if(abs(omDen) < 0.0001) omDen = 0.0001;
				omFogK = clamp((omFogParams.y - omFd) / omDen, 0.0, 1.0);
				outColor.rgb = mix(omFogColor.rgb, outColor.rgb, omFogK);
			}
			outColor.rgb = pow(outColor.rgb, vec3(gamma)); // apply gamma correction to result

			outColor.a = (omTranslucent != 0) ? texel.a*alpha : omCov; // OpenMerc: SoF translucent surfaces have lightmaps (opaque: 1, or the r_om_a2c coverage)
			// OpenMerc GFX-modern: SURF_LIGHT faces are bloom sources, hidden by the fog in front of them
			if(omEmissive > 0.0)
				outEmissive = vec4(pow(lmTex.rgb * texel.rgb, vec3(gamma)) * (omFogK * omEmissive), outColor.a);
			if(omTranslucent == 0)
				outMaterial = vec4(omMat.rg * omFogK, omWetK * omFogK, clamp(dot(lmTex.rgb, vec3(0.3, 0.5, 0.2)), 0.0, 1.0)); // OpenMerc GFX-modern r_om_ssr: reflectivity, x roughness, wetness, light
			// OpenMerc GFX-modern r_om_mirror: a face on the mirrored plane shows the mirror image (no screen-space reflection)
			if(abs(omEmissive + 11.0) < 0.5 && (omMirror.x > 0.5 || omMirror.y > 0.5))
			{
				vec3 omMc = omMirrorColor(outColor.rgb, passWorldCoord);
				if(omMc != outColor.rgb)
				{
					outColor.rgb = omMc;
					outMaterial = vec4(0.0, 0.0, 0.0, 1.0);
				}
			}
			// OpenMerc: the detail pass's 2 x dst x src (FX blend 5) on the finished colour; the emissive output
			// keeps its value as under the pass (its neutral 0.5)
			if(omDetailP.x != 0.0)
				outColor.rgb *= 2.0 * texture(omDetail, passTexCoord * omDetailP.xy + omDetailP.zw).rgb;
			// OpenMerc GFX-modern r_om_seams: height blend of the two textures (Mishkinis, "Advanced Terrain Texture
			// Splatting"; Unity "Material by Height"): each texture's height (texel brightness above its mean) plus
			// its share - the neighbour's half at the seam, none at the strip's inner end, a world-space noise moving
			// the line - and only the higher one shows, mixed within a narrow depth: stones of one side poke into
			// the grout of the other instead of a straight cross-fade
			if(omSeam != 0)
			{
				float omSb = clamp(1.0 - dot(passWorldCoord - omSeamP.xyz, omSeamD.xyz) / omSeamP.w, 0.0, 1.0);
				vec3 omLw = vec3(0.3, 0.59, 0.11);
				float omSh = dot(texture(tex, passTexCoord).rgb - textureLod(tex, passTexCoord, 12.0).rgb, omLw);
				vec2 omOwnTc = vec2(dot(passWorldCoord, omSeamS.xyz) + omSeamS.w, dot(passWorldCoord, omSeamT.xyz) + omSeamT.w);
				float omOh = dot(texture(omSeamOwn, omOwnTc).rgb - textureLod(omSeamOwn, omOwnTc, 12.0).rgb, omLw);
				vec3 omQ = passWorldCoord * 0.11;
				vec3 omQi = floor(omQ);
				vec3 omQf = fract(omQ);
				omQf = omQf * omQf * (3.0 - 2.0 * omQf);
				vec4 omHk = vec4(0.0, 1.0, 57.0, 58.0) + dot(omQi, vec3(1.0, 57.0, 113.0));
				vec4 omN0 = fract(sin(omHk) * 43758.5453);
				vec4 omN1 = fract(sin(omHk + 113.0) * 43758.5453);
				vec4 omNz = mix(omN0, omN1, omQf.z);
				vec2 omNy = mix(omNz.xz, omNz.yw, omQf.x);
				float omSn = mix(omNy.x, omNy.y, omQf.y);
				float omSm = omSb * (1.0 - omSb) * 4.0;
				float omA2 = clamp(0.5 * omSb + omSm * 0.35 * (omSn - 0.5), 0.0, 1.0);
				float omH2 = omA2 + 1.5 * omSh * omSm, omH1 = (1.0 - omA2) + 1.5 * omOh * omSm;
				float omHm = max(omH1, omH2) - 0.12;
				float omW1 = max(omH1 - omHm, 0.0), omW2 = max(omH2 - omHm, 0.0);
				outColor.a = omW2 / max(omW1 + omW2, 1e-4);
				if(outColor.a < 0.004)
					discard;
				if(omSeam == 2)
					outColor.rgb = vec3(1.0, 0.0, 0.0); // r_om_seams 2: where the strips lie
			}
		}
);

static const char* fragmentSrc3Dcolor = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			vec4 texel = color;

			// apply gamma correction and intensity
			// texel.rgb *= intensity; TODO: use intensity here? (this is used for beams)
			outColor.rgb = pow(texel.rgb, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrc3Dsky = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;
		uniform vec4 omSkyTint; // OpenMerc GFX-modern r_om_rain: overcast sky, lightning flash (gl3_post.c)

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// TODO: something about GL_BLEND vs GL_ALPHATEST etc

			// apply gamma correction
			// texel.rgb *= intensity; // TODO: really no intensity for sky?
			vec3 omTint = (omSkyTint.r + omSkyTint.g + omSkyTint.b > 0.0) ? omSkyTint.rgb : vec3(1.0);
			outColor.rgb = pow(texel.rgb * omTint, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrc3Dsprite = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// apply gamma correction and intensity
			texel.rgb *= intensity;
			outColor.rgb = pow(texel.rgb, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrc3DspriteAlpha = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			if(texel.a <= 0.666)
				discard;

			// apply gamma correction and intensity
			texel.rgb *= intensity;
			outColor.rgb = pow(texel.rgb, vec3(gamma));
			//outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
			outColor.a = texel.a; // I think in this case alpha from uni3d shouldn't be used
		}
);

static const char* vertexSrc3Dwater = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D
		void main()
		{
			passTexCoord = texCoord;

			gl_Position = transProjView * transModel * vec4(position, 1.0);
		}
);

static const char* vertexSrcAlias = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec4 passColor;

		void main()
		{
			passColor = vertColor*overbrightbits;
			passTexCoord = texCoord;
			gl_Position = transProjView* transModel * vec4(position, 1.0);
		}
);

static const char* fragmentSrcAlias = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		in vec4 passColor;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// apply gamma correction and intensity
			texel.rgb *= intensity;
			texel.a *= alpha; // is alpha even used here?
			texel *= min(vec4(1.5), passColor);

			outColor.rgb = pow(texel.rgb, vec3(gamma));
			outColor.a = texel.a; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrcAliasColor = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		in vec4 passColor;

		void main()
		{
			vec4 texel = passColor;

			// apply gamma correction and intensity
			// texel.rgb *= intensity; // TODO: color-only rendering probably shouldn't use intensity?
			texel.a *= alpha; // is alpha even used here?
			outColor.rgb = pow(texel.rgb, vec3(gamma));
			outColor.a = texel.a; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* vertexSrcParticles = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec4 passColor;

		void main()
		{
			passColor = vertColor;
			gl_Position = transProjView * transModel * vec4(position, 1.0);

			// abusing texCoord for pointSize, pointDist for particles
			float pointDist = texCoord.y*0.1; // with factor 0.1 it looks good.

			gl_PointSize = texCoord.x/pointDist;
		}
);

static const char* fragmentSrcParticles = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		in vec4 passColor;

		void main()
		{
			vec2 offsetFromCenter = 2.0*(gl_PointCoord - vec2(0.5, 0.5)); // normalize so offset is between 0 and 1 instead 0 and 0.5
			float distSquared = dot(offsetFromCenter, offsetFromCenter);
			if(distSquared > 1.0) // this makes sure the particle is round
				discard;

			vec4 texel = passColor;

			// apply gamma correction and intensity
			//texel.rgb *= intensity; TODO: intensity? Probably not?
			outColor.rgb = pow(texel.rgb, vec3(gamma));

			// I want the particles to fade out towards the edge, the following seems to look nice
			texel.a *= min(1.0, particleFadeFactor*(1.0 - distSquared));

			outColor.a = texel.a; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrcParticlesSquare = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		in vec4 passColor;

		void main()
		{
			// outColor = passColor;
			// so far we didn't use gamma correction for square particles, but this way
			// uniCommon is referenced so hopefully Intels Ivy Bridge HD4000 GPU driver
			// for Windows stops shitting itself (see https://github.com/yquake2/yquake2/issues/391)
			outColor.rgb = pow(passColor.rgb, vec3(gamma));
			outColor.a = passColor.a;
		}
);


// ############## OpenMerc: Soldier of Fortune 2D and effects (SPEC-412) #####################

// 2D with the fixed-function state of the original per vertex:
// mode.x = texture mode (0 modulate, 1 replace, 2 untextured), mode.y = alpha test (> 0.05)
static const char* vertexSrcSoF2D = MULTILINE_STRING(

		in vec2 position;   // GL3_ATTRIB_POSITION
		in vec2 texCoord;   // GL3_ATTRIB_TEXCOORD
		in vec4 vertColor;  // GL3_ATTRIB_COLOR
		in vec2 lmTexCoord; // GL3_ATTRIB_LMTEXCOORD, used as mode

		layout (std140) uniform uni2D
		{
			mat4 trans;
		};

		out vec2 passTexCoord;
		out vec4 passColor;
		flat out vec2 passMode;

		void main()
		{
			gl_Position = trans * vec4(position, 0.0, 1.0);
			passTexCoord = texCoord;
			passColor = vertColor;
			passMode = lmTexCoord;
		}
);

static const char* fragmentSrcSoF2D = MULTILINE_STRING(

		in vec2 passTexCoord;
		in vec4 passColor;
		flat in vec2 passMode;

		uniform sampler2D tex;

		out vec4 outColor;

		void main()
		{
			vec4 c;

			if(passMode.x > 1.5)
			{
				c = passColor;
			}
			else
			{
				vec4 t = texture(tex, passTexCoord);
				c = (passMode.x > 0.5) ? t : t * passColor;
			}

			if(passMode.y > 0.5 && c.a <= 0.05)
				discard;

			outColor = c;
		}
);

// 3D effects: alpha quads, chains, alpha surfaces, fog overlays, detail pass
static const char* vertexSrcSoFfx = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec4 passColor;
		out float passFogDist;
		out vec3 passFxWorld;
		out float passLiqSky; // r_om_water: how open the sky is over the vertex (gl3_liquid.c, lightmap slot)

		uniform vec4 fogplane; // view forward and -forward . eye
		uniform vec4 omLiq;    // r_om_water: x mode (0 off), y time
		uniform vec4 omLiqEye; // r_om_water: eye xyz
		uniform vec4 omLiqRefl; // r_om_water: w 1 = the ripple tile, its heights in b
		uniform sampler2D omLiqTile;

		// GFX-modern r_om_water: the slow swell (three long waves; rough and calm patches drift over it) - its height
		// lifts the water's vertices (the warped faces are cut into 64-unit pieces), its slope tilts the ripples
		float omLiqGroup(vec2 p, float t)
		{
			return 0.6 + 0.4 * sin(dot(p, vec2(0.0041, 0.0029)) + t * 0.31);
		}

		float omLiqSwellH(vec2 p, float t)
		{
			return omLiqGroup(p, t) * (1.6 * sin(dot(p, vec2(0.020, 0.012)) + t * 1.05)
				+ 1.0 * sin(dot(p, vec2(-0.013, 0.027)) + t * 1.42)
				+ 0.6 * sin(dot(p, vec2(0.031, -0.024)) + t * 1.9));
		}

		void main()
		{
			vec4 wp = transModel * vec4(position, 1.0);

			if(omLiq.x > 0.5)
			{
				wp.z += omLiqSwellH(wp.xy, omLiq.y);
				// r_om_water_3d: the ripples' own heights (the two big layers of the tile the normal is made of),
				// fading out by 1100 units where the faces are no longer cut small (gl3_liquid.c)
				if(omLiqRefl.w > 1.01)
				{
					vec2 p = wp.xy;
					float t = omLiq.y;
					float hh = (textureLod(omLiqTile, mat2(0.80, 0.60, -0.60, 0.80) * p * 0.00195 + t * vec2(0.0090, 0.0047), 0.0).b - 0.5) * 2.0
						+ (textureLod(omLiqTile, mat2(-0.28, 0.96, -0.96, -0.28) * p * 0.0047 + t * vec2(-0.0123, 0.0101), 0.0).b - 0.5) * 1.0;
					wp.z += hh * (omLiqRefl.w - 1.0) * (1.0 - smoothstep(700.0, 1100.0, distance(wp.xyz, omLiqEye.xyz)));
				}
			}

			passTexCoord = texCoord;
			passColor = vertColor;
			passFogDist = abs(dot(fogplane.xyz, wp.xyz) + fogplane.w);
			passFxWorld = wp.xyz;
			passLiqSky = lmTexCoord.x;
			gl_Position = transProjView * wp;
		}
);

static const char* fragmentSrcSoFfx = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		in vec4 passColor;
		in float passFogDist;
		in vec3 passFxWorld;

		uniform sampler2D tex;
		uniform int texmode;   // 0 modulate, 1 replace, 2 untextured
		uniform int alphatest; // > 0.05
		uniform int fogmode;   // 0 off, 1 linear
		uniform float fogstart;
		uniform float fogend;
		uniform vec4 fogcolor;

		// OpenMerc GFX-modern (gl3_post.c): emissive output (x, y, z, mode) and soft particles
		uniform vec4 omEmis;   // mode 0: (0, 0, 0, a), 1: colour * x, 2: constant xyz
		uniform vec4 omSoftP;  // projection C, D; depth range min, max
		uniform vec4 omSoftV;  // 1 / framebuffer size, fade distance, mode (0 off, 1 alpha, 2 colour)
		uniform sampler2D omDepth;

		// r_om_ppfog: the SoF fog region overlay alpha per fragment (gl3_fog.c OverlayAlpha)
		uniform vec4 omPF0;    // eye xyz, enabled
		uniform vec4 omPF1;    // start, range, max alpha 0..255, height falloff
		uniform vec4 omPF2;    // maxs z, size z, alpha scale, planes
		uniform vec4 omPFPl[5];

		float omPPFog(vec3 p, float shimmer)
		{
			vec3 d = p - omPF0.xyz;
			float D0 = dot(d, d);
			float D = D0;
			int n = int(omPF2.w);
			for(int k = 0; k < 5; k++)
			{
				if(k >= n) break;
				float a = dot(omPFPl[k].xyz, omPF0.xyz) - omPFPl[k].w;
				float b = omPFPl[k].w - dot(omPFPl[k].xyz, p);
				if(a != 0.0 && b != 0.0 && ((a > 0.0) != (b > 0.0))) continue;
				float s = a * a + b * b;
				if(s > 0.0) D = min(D, D0 * b * b / s);
			}
			D += shimmer;
			float al = (omPF1.y != 0.0) ? (D - omPF1.x) / omPF1.y : 0.0;
			al = clamp(al, 0.0, omPF1.z);
			if(omPF1.w > 0.5 && omPF2.y > 0.0)
			{
				float h = (omPF2.x - p.z) / omPF2.y;
				al *= h * h;
			}
			float fa = floor(al) / 255.0;
			return fa * mix(omPF2.z, 1.0, fa); // gl3_fog.c GL3_Fog_DimAlpha
		}

		// the same with the parameters of region r from the region table (gl3_fog.c FogTableUpload, 8 texels a row)
		uniform highp sampler2D omFogTab;

		float omPPFogTab(vec3 p, float shimmer, int r)
		{
			vec4 t0 = texelFetch(omFogTab, ivec2(0, r), 0); // start, range, max alpha, height falloff
			vec4 t1 = texelFetch(omFogTab, ivec2(1, r), 0); // maxs z, size z, dim, planes
			vec3 d = p - omPF0.xyz;
			float D0 = dot(d, d);
			float D = D0;
			int n = int(t1.w);
			for(int k = 0; k < 5; k++)
			{
				if(k >= n) break;
				vec4 pl = texelFetch(omFogTab, ivec2(2 + k, r), 0);
				float a = dot(pl.xyz, omPF0.xyz) - pl.w;
				float b = pl.w - dot(pl.xyz, p);
				if(a != 0.0 && b != 0.0 && ((a > 0.0) != (b > 0.0))) continue;
				float s = a * a + b * b;
				if(s > 0.0) D = min(D, D0 * b * b / s);
			}
			D += shimmer;
			float al = (t0.y != 0.0) ? (D - t0.x) / t0.y : 0.0;
			al = clamp(al, 0.0, t0.z);
			if(t0.w > 0.5 && t1.y > 0.0)
			{
				float h = (t1.x - p.z) / t1.y;
				al *= h * h;
			}
			float fa = floor(al) / 255.0;
			return fa * mix(t1.z, 1.0, fa);
		}

		uniform vec4 omFxMat; // GFX-modern r_om_ssr: reflectivity, roughness of liquids (0 = the default material)

		// GFX-modern r_om_water (gl3_liquid.c): x mode (0 off, 1 water, 2 the full set: r_om_water_quality), y time
		// (s), z highlight strength, w 1 = the traced reflection (r_om_ssr) reflects; eye xyz, w 1 = depth effects
		// (translucent, soft mode 3), 2 = and the scene under it (GL3_Post_WaterCopy, half size)
		uniform vec4 omLiq;
		uniform vec4 omLiqEye;
		uniform sampler2D omSceneCol;
		// x 1 = omLiqReflTex is the planar reflection (r_om_water_quality 3) read at the window position x yz, else the
		// sky panorama (yaw across, elevation 0..90 up); w >= 1 = the ripple tile omLiqTile (slopes rg, height b), above
		// 1 the lift of the vertices (r_om_water_3d)
		uniform vec4 omLiqRefl;
		uniform sampler2D omLiqTile;
		uniform sampler2D omLiqReflTex;
		in float passLiqSky;
		// GFX-modern: the rings of the last splashes: position, age in s (-1 off)
		uniform vec4 omLiqRip[12];

		// the splash rings at world point p: their slope added to g, the foam of their young crests returned
		float omLiqRings(vec3 p, inout vec2 g)
		{
			float foam = 0.0;
			for(int i = 0; i < 12; i++)
			{
				float a = omLiqRip[i].w;
				if(a < 0.0 || abs(omLiqRip[i].z - p.z) > 24.0)
					continue;
				vec2 d = p.xy - omLiqRip[i].xy;
				float r = length(d);
				float front = 6.0 + 70.0 * a;          // the ring runs out at 70 units/s
				if(r > front + 40.0)
					continue;
				float x = r - front;
				float env = exp(-x * x / 260.0) * exp(-a * 1.6) / (1.0 + r * 0.02);
				// a train of three crests behind the front; its slope is radial
				float w = cos(x * 0.45) * env;
				g += (d / max(r, 0.5)) * (-sin(x * 0.45) * 0.9 * env);
				foam += max(w, 0.0) * exp(-a * 3.0);
			}
			return foam;
		}

		// GFX-modern r_om_water: the slow swell (three long waves; rough and calm patches drift over it) - its height
		// lifts the water's vertices (the warped faces are cut into 64-unit pieces), its slope tilts the ripples
		float omLiqGroup(vec2 p, float t)
		{
			return 0.6 + 0.4 * sin(dot(p, vec2(0.0041, 0.0029)) + t * 0.31);
		}

		vec2 omLiqSwellG(vec2 p, float t)
		{
			return omLiqGroup(p, t) * (vec2(0.020, 0.012) * 1.6 * cos(dot(p, vec2(0.020, 0.012)) + t * 1.05)
				+ vec2(-0.013, 0.027) * 1.0 * cos(dot(p, vec2(-0.013, 0.027)) + t * 1.42)
				+ vec2(0.031, -0.024) * 0.6 * cos(dot(p, vec2(0.031, -0.024)) + t * 1.9));
		}

		// a directional wave's slope: direction d, frequency f (rad / unit), speed s (rad / s), height a
		vec2 omLiqWave(vec2 p, vec2 d, float f, float s, float a, float t)
		{
			return d * (f * a * cos(dot(d, p) * f + t * s));
		}

		float omLiqHash(vec2 p)
		{
			p = fract(p * vec2(123.34, 456.21));
			p += dot(p, p + 45.32);
			return fract(p.x * p.y);
		}

		float omLiqNoise(vec2 p)
		{
			vec2 i = floor(p), f = fract(p);
			f = f * f * (3.0 - 2.0 * f);
			return mix(mix(omLiqHash(i), omLiqHash(i + vec2(1.0, 0.0)), f.x),
				mix(omLiqHash(i + vec2(0.0, 1.0)), omLiqHash(i + vec2(1.0, 1.0)), f.x), f.y);
		}

		// the slope of value noise (four hashes): irregular chop, not the rows of summed sines
		vec2 omLiqNoiseG(vec2 p)
		{
			vec2 i = floor(p), f = fract(p);
			vec2 u = f * f * (3.0 - 2.0 * f), du = 6.0 * f * (1.0 - f);
			float a = omLiqHash(i), b = omLiqHash(i + vec2(1.0, 0.0));
			float c = omLiqHash(i + vec2(0.0, 1.0)), d = omLiqHash(i + vec2(1.0, 1.0));
			return du * vec2(b - a + (a - b - c + d) * u.y, c - a + (a - b - c + d) * u.x);
		}

		// the water's normal at world xy p: the swell, three ripple waves, drifting noise chop (mode 2: the ripples
		// over a slowly warped plane, a second finer chop layer)
		vec2 omLiqTileG(vec2 uv)
		{
			return texture(omLiqTile, uv).xy * 2.0 - 1.0;
		}

		vec3 omLiqNormal(vec2 p, float t, float mode)
		{
			if(omLiqRefl.w > 0.5)
			{
				// the ripple tile read three times: other scales (513, 213, 83 units), turned by odd angles,
				// drifting other ways - every direction has the same weight, no row of waves shows; the swell
				// only tilts it a little
				vec2 h = omLiqSwellG(p, t) * 0.3;
				h += omLiqTileG(mat2(0.80, 0.60, -0.60, 0.80) * p * 0.00195 + t * vec2(0.0090, 0.0047)) * 0.45;
				h += omLiqTileG(mat2(-0.28, 0.96, -0.96, -0.28) * p * 0.0047 + t * vec2(-0.0123, 0.0101)) * 0.35;
				if(mode > 1.5)
					h += omLiqTileG(mat2(0.47, -0.88, 0.88, 0.47) * p * 0.0121 + t * vec2(0.0207, -0.0168)) * 0.12;
				return normalize(vec3(-h, 1.0));
			}
			vec2 g = omLiqSwellG(p, t);
			if(mode > 1.5)
				p += 6.0 * vec2(sin(p.y * 0.021 + t * 0.4), cos(p.x * 0.017 - t * 0.33));
			g += omLiqWave(p, vec2(0.866, 0.5), 0.050, 1.10, 1.3, t);
			g += omLiqWave(p, vec2(-0.342, 0.940), 0.083, 1.60, 0.9, t);
			g += omLiqWave(p, vec2(-0.906, -0.423), 0.161, 2.40, 0.5, t);
			g += omLiqNoiseG(p * 0.09 + vec2(t * 0.35, t * 0.12)) * 0.22;
			if(mode > 1.5)
				g += omLiqNoiseG(p * 0.21 - vec2(t * 0.21, -t * 0.47)) * 0.12;
			return normalize(vec3(-g, 1.0));
		}

		// the light focused by the waves: a moving net of bright lines (0 .. ~3)
		float omLiqCaustic(vec2 p, float t)
		{
			vec2 q = p * 0.045;
			float c = 0.0;
			for(int i = 0; i < 3; i++)
			{
				q += vec2(cos(t * 0.7 - q.y * 1.3 + float(i)), sin(t * 0.6 + q.x * 1.1 - float(i))) * 0.55;
				c += abs(sin(q.x + q.y * 0.3) * sin(q.y - q.x * 0.2));
			}
			c = clamp(1.0 - c / 3.0, 0.0, 1.0);
			c *= c;
			return c * c * 3.5;
		}

		float omEyeDist(float z)
		{
			float ndc = (z - omSoftP.z) / (omSoftP.w - omSoftP.z) * 2.0 - 1.0;
			return omSoftP.y / (ndc + omSoftP.x);
		}

		void main()
		{
			vec4 c;
			vec3 liqN = vec3(0.0, 0.0, 1.0);
			vec3 liqV = vec3(0.0, 0.0, 1.0);
			vec4 liqT = vec4(1.0); // r_om_water: the texture sample (the water's colour)
			float return_env = 0.0; // r_om_water_glint -1: what the water reflects, nothing else

			float liqFoam = 0.0;
			if(omLiq.x > 0.5)
			{
				liqN = omLiqNormal(passFxWorld.xy, omLiq.y, omLiq.x);
				{
					vec2 rg = vec2(0.0);
					liqFoam = omLiqRings(passFxWorld, rg);
					liqN = normalize(liqN + vec3(-rg, 0.0));
				}
				liqV = normalize(omLiqEye.xyz - passFxWorld);
				if(liqV.z < 0.0) liqN.z = -liqN.z; // seen from below (under water): the side toward the eye
			}

			if(omPF0.w > 1.5)
			{
				c = vec4(passColor.rgb, omPPFogTab(passFxWorld, passTexCoord.y, int(passTexCoord.x + 0.5)));
			}
			else if(omPF0.w > 0.5)
			{
				c = vec4(passColor.rgb, omPPFog(passFxWorld, passColor.a));
			}
			else if(texmode == 2)
			{
				c = passColor;
			}
			else
			{
				// r_om_water: the texture moves with the ripples per pixel (the warp only moves the vertices)
				vec4 t = texture(tex, passTexCoord + liqN.xy * 0.03);
				liqT = t;
				c = (texmode == 1) ? t : t * passColor;
			}

			if(alphatest != 0 && c.a <= 0.05)
				discard;

			if(omLiq.x > 0.5)
			{
				vec3 lit = passColor.rgb; // the light at the water (vertex samples)
				float ndv = max(dot(liqN, liqV), 0.0);
				float fm = 1.0 - ndv;
				float fres = 0.02 + 0.98 * (fm * fm) * (fm * fm) * fm;
				// the light: above the view and a little ahead (the map's sun is not used - most water is under a
				// roof the sun does not reach); its strength is the light the water really gets (the vertex samples)
				vec3 L = normalize(vec3(-liqV.xy, liqV.z + 0.6));
				float spec = pow(max(dot(reflect(-liqV, liqN), L), 0.0), 220.0);

				float luma = dot(lit, vec3(0.3, 0.5, 0.2));
				vec3 tex0 = c.rgb; // the water's colour (texture x light)

				// the ripples and the swell catch the light: slopes facing the viewer brighter, the others darker
				vec2 toEye = liqV.xy / max(length(liqV.xy), 0.001);
				float slopeLit = max(1.0 + dot(liqN.xy, toEye) * 1.0, 0.35);
				c.rgb *= slopeLit;

				// mode 2: the light under the surface dances (caustics through the texture)
				if(omLiq.x > 1.5)
					c.rgb *= 1.0 + 0.3 * omLiqCaustic(passFxWorld.xy + liqN.xy * 30.0, omLiq.y) * (0.3 + 0.5 * luma);

				// what the surface reflects: the planar reflection moved by the ripples, or (the env_cubemap of the
				// Source engine's water) under open sky the map's sky blurred, under a roof the room blurred - the
				// light around the water with soft blotches of brighter and darker walls
				vec3 R = reflect(-liqV, liqN);
				vec3 env;
				if(omLiqRefl.x > 0.5)
				{
					env = texture(omLiqReflTex, gl_FragCoord.xy * omLiqRefl.yz + liqN.xy * 0.06).rgb;
				}
				else
				{
					vec3 Rs = vec3(R.xy, max(R.z, 0.02));
					vec2 suv = vec2(atan(Rs.y, Rs.x) * 0.15915494 + 0.5, asin(min(Rs.z, 1.0)) * 0.63661977);
					vec3 sky = texture(omLiqReflTex, suv).rgb;
					float blot = (omLiqRefl.w > 0.5) ? texture(omLiqTile, R.xy * 0.35 + passFxWorld.xy * 0.0007).b : 0.5;
					vec3 room = lit * (0.55 + 0.45 * blot) * mix(1.0, 0.65, clamp(R.z, 0.0, 1.0));
					env = mix(room, sky, clamp(passLiqSky, 0.0, 1.0));
				}
				// tinted a little by the water's own colour; weaker where the traced reflection (r_om_ssr) adds its own
				vec3 envT = env * mix(vec3(1.0), normalize(liqT.rgb + 0.05) * 1.6, 0.25);
				float rf = clamp(0.2 + 0.8 * fres, 0.0, 1.0) * ((omLiq.w > 0.5) ? 0.6 : 0.85);
				if(liqV.z < 0.0) rf *= 0.3;
				vec3 body = c.rgb;
				c.rgb = mix(body * 0.8, envT, rf);

				c.rgb += spec * max(omLiq.z, 0.0) * lit * 0.6;
				if(omLiq.z < -0.5) { c = vec4(env, 1.0); return_env = 1.0; }

				if(return_env < 0.5 && omLiqEye.w > 1.5 && liqV.z > 0.0)
				{
					// translucent water over the scene under it (mode 2): the scene refracted by the ripples (less
					// near the shore so the edge does not tear; only from what lies behind the surface), the light
					// focused on the bottom, the water's colour taking over with the path through it, foam where it
					// meets walls and objects; the surface itself opaque but for a soft edge
					vec2 uv = gl_FragCoord.xy * omSoftV.xy;
					float surfD = omEyeDist(gl_FragCoord.z);
					float scene = omEyeDist(texture(omDepth, uv).r);
					float thick = max(scene - surfD, 0.0);
					vec2 ruv = uv + liqN.xy * 0.08 * clamp(thick / 30.0, 0.0, 1.0) / max(surfD * 0.003, 1.0);
					float rD = omEyeDist(texture(omDepth, ruv).r);
					if(rD < surfD)
					{
						ruv = uv;
						rD = scene;
					}
					vec3 behind = texture(omSceneCol, ruv).rgb;
					vec3 floorP = omLiqEye.xyz + (passFxWorld - omLiqEye.xyz) * (rD / max(surfD, 1.0));
					float under = clamp((passFxWorld.z - floorP.z) / 24.0, 0.0, 1.0);
					behind *= 1.0 + omLiqCaustic(floorP.xy + liqN.xy * 20.0, omLiq.y) * under * (0.25 + 0.6 * luma);
					vec3 tint = normalize(liqT.rgb + 0.02) * 0.9;
					vec3 trans = exp(-max(rD - surfD, 0.0) * (1.0 - tint) * 0.012);
					vec3 refr = mix(tex0 * 0.6, behind * mix(vec3(1.0), tint * 1.3, 0.35), trans);
					// the ripples lighten and darken what is seen through them too, the water keeps a colour of its own
					// (its texture, a third) - clear shallow water must not vanish; the reflection over all
					refr *= mix(1.0, slopeLit, 0.7);
					refr = mix(refr, body, 0.3);
					c.rgb = mix(refr, envT, rf) + spec * max(omLiq.z, 0.0) * lit * 0.6;
					float shore = 1.0 - clamp(thick / (10.0 + 6.0 * sin(omLiq.y * 1.3 + passFxWorld.x * 0.05 +
						passFxWorld.y * 0.04)), 0.0, 1.0);
					float fn = omLiqNoise(passFxWorld.xy * 0.12 + liqN.xy * 3.0 + omLiq.y * 0.3) * 0.6 +
						omLiqNoise(passFxWorld.xy * 0.35 - omLiq.y * 0.5) * 0.4;
					c.rgb = mix(c.rgb, lit * 1.1, smoothstep(0.35, 0.75, fn * shore * 1.6) * 0.8);
					c.a = clamp(thick / 3.0, 0.0, 1.0);
				}
				else if(omLiqEye.w > 0.5 && liqV.z > 0.0)
				{
					// translucent water over the scene depth (seen from above: from below it is the air): soft where it meets the ground and walls, more opaque
					// and darker where it is deep
					float scene = omEyeDist(texture(omDepth, gl_FragCoord.xy * omSoftV.xy).r);
					float thick = max(scene - omEyeDist(gl_FragCoord.z), 0.0);
					float deep = clamp(thick / 320.0, 0.0, 1.0);
					c.a = mix(c.a, max(c.a, 0.9), deep);
					c.rgb *= mix(1.0, 0.8, deep);
					c.a *= clamp(thick / 14.0, 0.0, 1.0);
				}

				// the splash rings' young crests: a little white water
				c.rgb = mix(c.rgb, lit * 1.3 + vec3(0.1), clamp(liqFoam * 0.9, 0.0, 0.75));

				// with distance the surface darkens toward the deep water (not much: the Source engine's water fog
				// over the surface)
				if(liqV.z > 0.0)
					c.rgb *= mix(1.0, 0.62, smoothstep(250.0, 2200.0, length(omLiqEye.xyz - passFxWorld)));

				if(omLiqEye.w < 1.5 || liqV.z <= 0.0)
					c.a = mix(c.a, 1.0, fres * 0.6); // less see-through at grazing angles
			}

			if(fogmode != 0)
			{
				float f = clamp((fogend - passFogDist) / max(fogend - fogstart, 0.0001), 0.0, 1.0);
				c.rgb = mix(fogcolor.rgb, c.rgb, f);
			}

			if(omSoftV.w > 0.5 && omSoftV.w < 2.5)
			{
				float scene = omEyeDist(texture(omDepth, gl_FragCoord.xy * omSoftV.xy).r);
				float fade = clamp((scene - omEyeDist(gl_FragCoord.z)) / omSoftV.z, 0.0, 1.0);
				if(omSoftV.w < 1.5) c.a *= fade;
				else c.rgb *= fade;
			}

			// intensity is always 1 here; referenced so the uniCommon block stays active
			outColor = vec4(c.rgb * intensity, c.a);

			if(omEmis.w > 1.5) outEmissive = vec4(omEmis.xyz, outColor.a);
			else if(omEmis.w > 0.5) outEmissive = vec4(outColor.rgb * omEmis.x, outColor.a);
			// r_om_water: wetness 1 marks the rippled water for the traced reflection (it rebuilds the same normal)
			if(omFxMat.x > 0.0) outMaterial = vec4(omFxMat.x, omFxMat.x * omFxMat.y, (omLiq.x > 0.5) ? 1.0 : 0.0, outColor.a);
		}
);

#undef MULTILINE_STRING

enum {
	GL3_BINDINGPOINT_UNICOMMON,
	GL3_BINDINGPOINT_UNI2D,
	GL3_BINDINGPOINT_UNI3D,
	GL3_BINDINGPOINT_UNILIGHTS
};

static qboolean
initShader2D(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc,
	qboolean uniCommonRequired)
{
	GLuint shaders2D[2] = {0};
	GLuint prog = 0;

	if(shaderInfo->shaderProgram != 0)
	{
		Com_Printf("WARNING: calling %s for gl3ShaderInfo_t that already has a shaderProgram!\n",
			__func__);
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	//shaderInfo->uniColor = shaderInfo->uniProjMatrix = shaderInfo->uniModelViewMatrix = -1;
	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScalesOrTime = -1;
	shaderInfo->uniVblend = -1;

	shaders2D[0] = CompileShader(GL_VERTEX_SHADER, vertSrc, NULL);
	if(shaders2D[0] == 0)  return false;

	shaders2D[1] = CompileShader(GL_FRAGMENT_SHADER, fragSrc, NULL);
	if(shaders2D[1] == 0)
	{
		glDeleteShader(shaders2D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders2D);

	// I think the shaders aren't needed anymore once they're linked into the program
	glDeleteShader(shaders2D[0]);
	glDeleteShader(shaders2D[1]);

	if(prog == 0)
	{
		return false;
	}

	shaderInfo->shaderProgram = prog;
	GL3_UseProgram(prog);

	// Bind the buffer object to the uniform blocks
	GLuint blockIndex = GL_INVALID_INDEX;
	if (uniCommonRequired)
	{
		blockIndex = glGetUniformBlockIndex(prog, "uniCommon");
	}

	if (blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uniCommonData))
		{
			Com_Printf("WARNING: OpenGL driver disagrees with us about UBO size of 'uniCommon': %i vs %i\n",
					blockSize, (int)sizeof(gl3state.uniCommonData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNICOMMON);
	}
	else if (uniCommonRequired)
	{
		Com_Printf("WARNING: Couldn't find uniform block index 'uniCommon'\n");
		// TODO: clean up?
		return false;
	}
	blockIndex = glGetUniformBlockIndex(prog, "uni2D");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uni2DData))
		{
			Com_Printf("WARNING: OpenGL driver disagrees with us about UBO size of 'uni2D'\n");
			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNI2D);
	}
	else
	{
		Com_Printf("WARNING: Couldn't find uniform block index 'uni2D'\n");
		goto err_cleanup;
	}

	shaderInfo->uniLmScalesOrTime = glGetUniformLocation(prog, "time");
	if(shaderInfo->uniLmScalesOrTime != -1)
	{
		glUniform1f(shaderInfo->uniLmScalesOrTime, 0.0f);
	}

	shaderInfo->uniVblend = glGetUniformLocation(prog, "v_blend");
	if(shaderInfo->uniVblend != -1)
	{
		glUniform4f(shaderInfo->uniVblend, 0, 0, 0, 0);
	}

	return true;

err_cleanup:

	glDeleteProgram(prog);

	return false;
}

static qboolean
initShader3D(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc)
{
	GLuint shaders3D[2] = {0};
	GLuint prog = 0;
	int i=0;

	if(shaderInfo->shaderProgram != 0)
	{
		Com_Printf("WARNING: calling initShader3D for gl3ShaderInfo_t that already has a shaderProgram!\n");
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScalesOrTime = -1;
	shaderInfo->uniVblend = -1;

	shaders3D[0] = CompileShader(GL_VERTEX_SHADER, vertexCommon3D, vertSrc);
	if(shaders3D[0] == 0)  return false;

	shaders3D[1] = CompileShader(GL_FRAGMENT_SHADER, fragmentCommon3D, fragSrc);
	if(shaders3D[1] == 0)
	{
		glDeleteShader(shaders3D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders3D);

	if(prog == 0)
	{
		goto err_cleanup;
	}

	GL3_UseProgram(prog);

	// Bind the buffer object to the uniform blocks
	GLuint blockIndex = glGetUniformBlockIndex(prog, "uniCommon");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uniCommonData))
		{
			Com_Printf("WARNING: OpenGL driver disagrees with us about UBO size of 'uniCommon'\n");

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNICOMMON);
	}
	else
	{
		Com_Printf("WARNING: Couldn't find uniform block index 'uniCommon'\n");

		goto err_cleanup;
	}
	blockIndex = glGetUniformBlockIndex(prog, "uni3D");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uni3DData))
		{
			Com_Printf("WARNING: OpenGL driver disagrees with us about UBO size of 'uni3D'\n");
			Com_Printf("         driver says %d, we expect %d\n", blockSize, (int)sizeof(gl3state.uni3DData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNI3D);
	}
	else
	{
		Com_Printf("WARNING: Couldn't find uniform block index 'uni3D'\n");

		goto err_cleanup;
	}
	blockIndex = glGetUniformBlockIndex(prog, "uniLights");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uniLightsData))
		{
			Com_Printf("WARNING: OpenGL driver disagrees with us about UBO size of 'uniLights'\n");
			Com_Printf("         OpenGL says %d, we say %d\n", blockSize, (int)sizeof(gl3state.uniLightsData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNILIGHTS);
	}
	// else: as uniLights is only used in the LM shaders, it's ok if it's missing

	// make sure texture is GL_TEXTURE0
	GLint texLoc = glGetUniformLocation(prog, "tex");
	if(texLoc != -1)
	{
		glUniform1i(texLoc, 0);
	}

	// ..  and the 4 lightmap texture use GL_TEXTURE1..4
	char lmName[10] = "lightmapX";
	for(i=0; i<4; ++i)
	{
		lmName[8] = '0'+i;
		GLint lmLoc = glGetUniformLocation(prog, lmName);
		if(lmLoc != -1)
		{
			glUniform1i(lmLoc, i+1); // lightmap0 belongs to GL_TEXTURE1, lightmap1 to GL_TEXTURE2 etc
		}
	}

	/* OpenMerc: lightmap colour table on GL_TEXTURE5 (GL3_SoF_BindLightTable) */
	GLint ctLoc = glGetUniformLocation(prog, "omColorTable");
	if(ctLoc != -1)
	{
		glUniform1i(ctLoc, 5);
	}

	GLint mmLoc = glGetUniformLocation(prog, "omMatMap"); /* r_om_ssr: material map of the texture (gl3_material.c) */
	if(mmLoc != -1)
	{
		glUniform1i(mmLoc, 14);
	}

	GLint soLoc = glGetUniformLocation(prog, "omSeamOwn"); /* r_om_seams: the face's own texture (gl3_surf.c SeamDraw) */
	if(soLoc != -1)
	{
		glUniform1i(soLoc, 10);
	}

	/* OpenMerc (I4c): dynamic light layer of the lightmap page on GL_TEXTURE6 (gl3_dynlight.c) */
	GLint dlLoc = glGetUniformLocation(prog, "omDynLight");
	if(dlLoc != -1)
	{
		glUniform1i(dlLoc, 6);
	}
	GLint dtLoc = glGetUniformLocation(prog, "omDetail");
	if(dtLoc != -1)
	{
		glUniform1i(dtLoc, 6);
	}

	/* GFX-modern: sun shadow maps (uniform block uniOMcsm, GL_TEXTURE8) */
	GL3_CSM_BindProgram(prog);

	GLint lmScalesLoc = glGetUniformLocation(prog, "lmScales");
	shaderInfo->uniLmScalesOrTime = lmScalesLoc;
	if(lmScalesLoc != -1)
	{
		shaderInfo->lmScales[0] = HMM_Vec4(1.0f, 1.0f, 1.0f, 1.0f);

		for(i=1; i<4; ++i)  shaderInfo->lmScales[i] = HMM_Vec4(0.0f, 0.0f, 0.0f, 0.0f);

		glUniform4fv(lmScalesLoc, 4, shaderInfo->lmScales[0].Elements);
	}

	shaderInfo->shaderProgram = prog;

	// I think the shaders aren't needed anymore once they're linked into the program
	glDeleteShader(shaders3D[0]);
	glDeleteShader(shaders3D[1]);

	return true;

err_cleanup:

	glDeleteShader(shaders3D[0]);
	glDeleteShader(shaders3D[1]);

	if(prog != 0)  glDeleteProgram(prog);

	return false;
}

static void initUBOs(void)
{
	/* OpenMerc: SoF applies gamma, brightness and contrast to the images when
	   they are loaded (SPEC-413 §9), not in the shaders */
	gl3state.uniCommonData.gamma = 1.0f;
	gl3state.uniCommonData.intensity = 1.0f;
	gl3state.uniCommonData.intensity2D = 1.0f;
	gl3state.uniCommonData.color = HMM_Vec4(1, 1, 1, 1);

	glGenBuffers(1, &gl3state.uniCommonUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uniCommonUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNICOMMON, gl3state.uniCommonUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uniCommonData), &gl3state.uniCommonData, GL_DYNAMIC_DRAW);

	// the matrix will be set to something more useful later, before being used
	gl3state.uni2DData.transMat4 = HMM_Mat4();

	glGenBuffers(1, &gl3state.uni2DUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uni2DUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI2D, gl3state.uni2DUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uni2DData), &gl3state.uni2DData, GL_DYNAMIC_DRAW);

	// the matrices will be set to something more useful later, before being used
	gl3state.uni3DData.transProjViewMat4 = HMM_Mat4();
	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
	gl3state.uni3DData.scroll = 0.0f;
	gl3state.uni3DData.time = 0.0f;
	gl3state.uni3DData.alpha = 1.0f;
	// gl3_overbrightbits 0 means "no scaling" which is equivalent to multiplying with 1
	gl3state.uni3DData.overbrightbits = 1.0f; /* OpenMerc: SPEC-410, no overbright factor */
	gl3state.uni3DData.particleFadeFactor = gl3_particle_fade_factor->value;
	gl3state.uni3DData.lightScaleForTurb = 1.0f;

	glGenBuffers(1, &gl3state.uni3DUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uni3DUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI3D, gl3state.uni3DUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uni3DData), &gl3state.uni3DData, GL_DYNAMIC_DRAW);

	glGenBuffers(1, &gl3state.uniLightsUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uniLightsUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNILIGHTS, gl3state.uniLightsUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uniLightsData), &gl3state.uniLightsData, GL_DYNAMIC_DRAW);

	gl3state.currentUBO = gl3state.uniLightsUBO;
}

/* the world shaders were made with the lamp shadow receiver (r_om_lshadow on; gl3_main.c remakes them on a change) */
qboolean gl3_world_lamps = true;
qboolean gl3_world_lite = false; /* r_om_worldlite: the world shaders without sun shadows, lightning, material, mirrors */


/* OpenMerc: a variant of the lightmapped world shader with its per-program switches as constants. omTranslucent
   (si3DlmTrans) and omSeam (si3DlmSeam) are set once per program and never change, but as uniforms every world
   pixel carried the seam blend's code and registers: the compiler now drops the paths a program cannot take
   (Adreno 610: the seam path alone was 0.7 ms of a 28 ms frame). translucent: 0 or 1; seam: keep the seam path. */
/* the gl_monolightmap mode folded into the world programs (0: the colour-table path alone), -1 = all modes */
int gl3_shader_mono = -1;
/* the dynamic light path folded into the world programs: 1 per pixel (r_om_pdlight on a SoF world), 0 the CPU
   sample layer and the Quake II per-pixel loop; GL3_DynLight_ShaderWants tells which one the next views use */
int gl3_shader_pdl = -1;

static void
FoldAll(char *out, const char *from, const char *to)
{
	char *at;
	size_t ol = strlen(from), nl = strlen(to);

	while ((at = strstr(out, from)) != NULL)
	{
		memcpy(at, to, nl);
		memset(at + nl, ' ', ol - nl);
	}
}

static char *
LmVariant(const char *src, int translucent, qboolean seam)
{
	static const char *const fold[2][2][2] = {
		{{"omTranslucent == 0", "true "}, {"omTranslucent != 0", "false"}},
		{{"omTranslucent == 0", "false"}, {"omTranslucent != 0", "true "}}};
	char *out = strdup(src), *at;
	int i;

	if (!out)
	{
		return NULL;
	}

	/* same length replacements (padded): the source is edited in place */
	for (i = 0; i < 2; i++)
	{
		size_t ol = strlen(fold[translucent][i][0]), nl = strlen(fold[translucent][i][1]);

		while ((at = strstr(out, fold[translucent][i][0])) != NULL)
		{
			memcpy(at, fold[translucent][i][1], nl);
			memset(at + nl, ' ', ol - nl);
		}
	}

	/* gl_monolightmap 0 (latched at map load; the default, and night vision / r_fullbright use mode 0 too): the
	   colour-table path alone - the other modes' code, and the discard by the lightmap alpha they need, go */
	if (gl3_shader_mono == 0)
	{
		static const char *const from = "omMono == 0", *const to = "true";

		while ((at = strstr(out, from)) != NULL)
		{
			memcpy(at, to, strlen(to));
			memset(at + strlen(to), ' ', strlen(from) - strlen(to));
		}
	}

	/* omPdl.x (per view: the per-pixel SoF dlights) as the constant the views will use: the path not taken - the
	   32-light loop of Quake II maps and the CPU layer fetch, or the per-pixel SoF rule - goes */
	if (gl3_shader_pdl >= 0)
	{
		FoldAll(out, "omPdl.x < 0.5", gl3_shader_pdl ? "false" : "true");
		FoldAll(out, "omPdl.x > 0.5", gl3_shader_pdl ? "true" : "false");
	}

	if (!seam)
	{
		static const char *const from = "if(omSeam != 0)", *const to = "if(false)";

		while ((at = strstr(out, from)) != NULL)
		{
			memcpy(at, to, strlen(to));
			memset(at + strlen(to), ' ', strlen(from) - strlen(to));
		}
	}

	return out;
}

static qboolean createShaders(void)
{
	if (!initShader2D(&gl3state.si2D, vertexSrc2D, fragmentSrc2D, true))
	{
		Com_Printf("WARNING: Failed to create shader program for textured 2D rendering!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2Dtinted, vertexSrc2D, fragmentSrc2Dtinted, true))
	{
		Com_Printf("WARNING: Failed to create shader program for tinted 2D rendering!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2Dcolor, vertexSrc2Dcolor, fragmentSrc2Dcolor, true))
	{
		Com_Printf("WARNING: Failed to create shader program for color-only 2D rendering!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2DpostProcess, vertexSrc2D, fragmentSrc2Dpostprocess, false))
	{
		Com_Printf("WARNING: Failed to create shader program to render framebuffer object!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2DpostProcessWater, vertexSrc2D, fragmentSrc2DpostprocessWater, false))
	{
		Com_Printf("WARNING: Failed to create shader program to render framebuffer object under water!\n");
		return false;
	}

	/* GFX-modern: the lightmapped shaders receive the sun shadows (gl3_csm.c) */
	static char* lmFragCsm = NULL;
	cvar_t *r_om_lshadow_world = ri.Cvar_Get("r_om_lshadow", "1", CVAR_ARCHIVE);
	cvar_t *r_om_worldlite_c = ri.Cvar_Get("r_om_worldlite", "0", CVAR_ARCHIVE);
	gl3_world_lamps = r_om_lshadow_world->value != 0.0f;
	free(lmFragCsm);
	{
		/* GFX-modern r_om_pdlight: the per-pixel SoF dlight function goes before main() */
		const char *lmsrc = (gl3_colorlight->value == 0.0f) ? fragmentSrc3DlmNoColor : fragmentSrc3Dlm;
		const char *m = strstr(lmsrc, "void main()");
		size_t pre = m ? (size_t)(m - lmsrc) : strlen(lmsrc);
		size_t ins = strlen(fragmentSrcOmPdl), insb = strlen(fragmentSrcOmWorld);
		char *joined = malloc(strlen(lmsrc) + ins + insb + 1);
		if(joined == NULL)
		{
			return false;
		}
		memcpy(joined, lmsrc, pre);
		memcpy(joined + pre, fragmentSrcOmPdl, ins);
		memcpy(joined + pre + ins, fragmentSrcOmWorld, insb); /* GFX-modern: lightning, reflection material, mirrors */
		strcpy(joined + pre + ins + insb, lmsrc + pre);
		/* r_om_lshadow 0: the world shaders without the lamp shadow receiver (gl3_main.c recompiles them on a change) */
		lmFragCsm = GL3_CSM_PrependLamps(joined, gl3_world_lamps);
		free(joined);

		/* r_om_worldlite (the mobile preset): the world shaders without the sun shadows, the lightning light, the
		   reflection material and the mirror faces - calls cut out of the source, so the compiler drops those paths and
		   their registers too (gl3_main.c remakes the shaders on a change) */
		gl3_world_lite = r_om_worldlite_c->value != 0.0f;

		if (gl3_world_lite && lmFragCsm)
		{
			static const char *const cut[][2] = {
				{"omCsmApply(lmTex.rgb, passWorldCoord, passNormal);", ""},
				{"lmTex.rgb += omLightningLight(passWorldCoord, passNormal);", ""},
				{"(omTranslucent == 0) ? omMaterial(passWorldCoord, passNormal, omWetK) : vec4(0.0)", "vec4(0.0)"},
				{"if(abs(omEmissive + 11.0) < 0.5 && (omMirror.x > 0.5 || omMirror.y > 0.5))", "if(false)"},
			};
			int c, done = 0;

			for (c = 0; c < (int)(sizeof(cut) / sizeof(cut[0])); c++)
			{
				char *at;
				size_t ol = strlen(cut[c][0]), nl = strlen(cut[c][1]);

				while ((at = strstr(lmFragCsm, cut[c][0])) != NULL)
				{
					memmove(at + nl, at + ol, strlen(at + ol) + 1);
					memcpy(at, cut[c][1], nl);
					done++;
				}
			}

			R_Printf(PRINT_DEVELOPER, "r_om_worldlite: %d cuts in the world shaders\n", done);
		}
	}
	if(lmFragCsm == NULL)
	{
		return false;
	}
	/* the world programs' variants (LmVariant): opaque and flowing faces, translucent faces, seam strips */
	static char *lmMain = NULL, *lmTrans = NULL, *lmSeamSrc = NULL;
	free(lmMain);
	free(lmTrans);
	free(lmSeamSrc);
	{
		extern int gl3_monolightmap_mode;

		gl3_shader_mono = (gl3_monolightmap_mode == 0) ? 0 : -1;
		gl3_shader_pdl = GL3_DynLight_ShaderWants();
	}
	lmMain = LmVariant(lmFragCsm, 0, false);
	lmTrans = LmVariant(lmFragCsm, 1, false);
	lmSeamSrc = LmVariant(lmFragCsm, 0, true);
	if(!lmMain || !lmTrans || !lmSeamSrc)
	{
		return false;
	}
	const char* lightmappedFrag = lmMain;

	if(!initShader3D(&gl3state.si3Dlm, vertexSrc3Dlm, lightmappedFrag))
	{
		Com_Printf("WARNING: Failed to create shader program for textured 3D rendering with lightmap!\n");
		return false;
	}
	/* OpenMerc: the opaque variant - every discard of the lightmapped shader becomes an empty statement (GL3_WorldShaderFor) */
	{
		char *opq = strdup(lightmappedFrag), *d;

		if(opq == NULL)
		{
			return false;
		}

		while((d = strstr(opq, "discard;")) != NULL)
		{
			memcpy(d, "        ", 8); /* keeps "if(...) ;" valid */
		}

		if(!initShader3D(&gl3state.si3DlmOpaque, vertexSrc3Dlm, opq))
		{
			Com_Printf("WARNING: Failed to create the opaque lightmapped shader, faces stay on the alpha tested one\n");
			gl3state.si3DlmOpaque.shaderProgram = 0;
		}

		free(opq);
	}
	/* OpenMerc: the variant for faces without dynamic light this frame (the static world VBO, GL3_World_AddPoly:
	   those faces carry no light flags). The per-pixel light code is folded out, its texture-axis derivatives
	   included - compilers hoist derivatives out of the branch, so every world pixel paid for them (Adreno 610). */
	{
		char *nodl = strdup(lightmappedFrag);

		if(nodl == NULL)
		{
			return false;
		}

		FoldAll(nodl, "passLightFlags != 0u", "false");

		if(!initShader3D(&gl3state.si3DlmNoDl, vertexSrc3Dlm, nodl))
		{
			Com_Printf("WARNING: Failed to create the lightmapped shader without dynamic lights\n");
			gl3state.si3DlmNoDl.shaderProgram = 0;
		}

		free(nodl);
	}
	if(!initShader3D(&gl3state.si3Dtrans, vertexSrc3D, fragmentSrc3D))
	{
		Com_Printf("WARNING: Failed to create shader program for rendering translucent 3D things!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DcolorOnly, vertexSrc3D, fragmentSrc3Dcolor))
	{
		Com_Printf("WARNING: Failed to create shader program for flat-colored 3D rendering!\n");
		return false;
	}
	/*
	if(!initShader3D(&gl3state.si3Dlm, vertexSrc3Dlm, fragmentSrc3D))
	{
		Com_Printf("WARNING: Failed to create shader program for blending 3D lightmaps rendering!\n");
		return false;
	}
	*/
	if(!initShader3D(&gl3state.si3Dturb, vertexSrc3Dwater, fragmentSrc3Dwater))
	{
		Com_Printf("WARNING: Failed to create shader program for water rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DlmFlow, vertexSrc3DlmFlow, lightmappedFrag))
	{
		Com_Printf("WARNING: Failed to create shader program for scrolling textured 3D rendering with lightmap!\n");
		return false;
	}
	// OpenMerc: same lightmapped shader, but blended with alpha and without alpha test
	if(!initShader3D(&gl3state.si3DlmTrans, vertexSrc3Dlm, lmTrans))
	{
		Com_Printf("WARNING: Failed to create shader program for translucent 3D rendering with lightmap!\n");
		return false;
	}
	else
	{
		GLint transLoc = glGetUniformLocation(gl3state.si3DlmTrans.shaderProgram, "omTranslucent");

		if(transLoc != -1)
		{
			glUseProgram(gl3state.si3DlmTrans.shaderProgram);
			glUniform1i(transLoc, 1);
		}
	}
	// OpenMerc GFX-modern r_om_seams: same lightmapped shader, its alpha the blend over a texture seam (gl3_surf.c)
	if(!initShader3D(&gl3state.si3DlmSeam, vertexSrc3Dlm, lmSeamSrc))
	{
		Com_Printf("WARNING: Failed to create shader program for texture seam blending!\n");
		return false;
	}
	else
	{
		GLint seamLoc = glGetUniformLocation(gl3state.si3DlmSeam.shaderProgram, "omSeam");

		if(seamLoc != -1)
		{
			glUseProgram(gl3state.si3DlmSeam.shaderProgram);
			glUniform1i(seamLoc, 1);
		}
	}
	if(!initShader3D(&gl3state.si3DtransFlow, vertexSrc3Dflow, fragmentSrc3D))
	{
		Com_Printf("WARNING: Failed to create shader program for scrolling textured translucent 3D rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3Dsky, vertexSrc3D, fragmentSrc3Dsky))
	{
		Com_Printf("WARNING: Failed to create shader program for sky rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3Dsprite, vertexSrc3D, fragmentSrc3Dsprite))
	{
		Com_Printf("WARNING: Failed to create shader program for sprite rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DspriteAlpha, vertexSrc3D, fragmentSrc3DspriteAlpha))
	{
		Com_Printf("WARNING: Failed to create shader program for alpha-tested sprite rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3Dalias, vertexSrcAlias, fragmentSrcAlias))
	{
		Com_Printf("WARNING: Failed to create shader program for rendering textured models!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DaliasColor, vertexSrcAlias, fragmentSrcAliasColor))
	{
		Com_Printf("WARNING: Failed to create shader program for rendering flat-colored models!\n");
		return false;
	}

	// OpenMerc: SoF 2D and effects
	if (!initShader2D(&gl3state.siSoF2D, vertexSrcSoF2D, fragmentSrcSoF2D, false))
	{
		Com_Printf("WARNING: Failed to create shader program for SoF 2D rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.siSoFfx, vertexSrcSoFfx, fragmentSrcSoFfx))
	{
		Com_Printf("WARNING: Failed to create shader program for SoF effects!\n");
		return false;
	}

	const char* particleFrag = fragmentSrcParticles;
	if(gl3_particle_square->value != 0.0f)
	{
		particleFrag = fragmentSrcParticlesSquare;
	}

	if(!initShader3D(&gl3state.siParticle, vertexSrcParticles, particleFrag))
	{
		Com_Printf("WARNING: Failed to create shader program for rendering particles!\n");
		return false;
	}

	gl3state.currentShaderProgram = 0;

	return true;
}

qboolean GL3_InitShaders(void)
{
	initUBOs();

	return createShaders();
}

static void deleteShaders(void)
{
	const gl3ShaderInfo_t siZero = {0};
	for(gl3ShaderInfo_t* si = &gl3state.si2D; si <= &gl3state.siParticle; ++si)
	{
		if(si->shaderProgram != 0)  glDeleteProgram(si->shaderProgram);
		*si = siZero;
	}
}

void GL3_ShutdownShaders(void)
{
	deleteShaders();

	// let's (ab)use the fact that all 4 UBO handles are consecutive fields
	// of the gl3state struct
	GL3_Ring3DShutdown();
	glDeleteBuffers(4, &gl3state.uniCommonUBO);
	gl3state.uniCommonUBO = gl3state.uni2DUBO = gl3state.uni3DUBO = gl3state.uniLightsUBO = 0;
}

static void UniformLocFlush(void);

qboolean GL3_RecreateShaders(void)
{
	// delete and recreate the existing shaders (but not the UBOs)
	deleteShaders();
	UniformLocFlush(); /* the driver may hand the new programs the old names */
	return createShaders();
}

static inline void
updateUBO(GLuint ubo, GLsizeiptr size, void* data)
{
	if(gl3state.currentUBO != ubo)
	{
		gl3state.currentUBO = ubo;
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	}

	++gl3_numBufferUniforms;

	// http://docs.gl/gl3/glBufferSubData says  "When replacing the entire data store,
	// consider using glBufferSubData rather than completely recreating the data store
	// with glBufferData. This avoids the cost of reallocating the data store."
	// no idea why glBufferData() doesn't just do that when size doesn't change, but whatever..
	// however, it also says glBufferSubData() might cause a stall so I DON'T KNOW!
	// on Linux/nvidia, by just looking at the fps, glBufferData() and glBufferSubData() make no difference
	// TODO: STREAM instead DYNAMIC?

#if 1
	// this seems to be reasonably fast everywhere.. glMapBuffer() seems to be a bit faster on OSX though..
	glBufferData(GL_UNIFORM_BUFFER, size, data, GL_DYNAMIC_DRAW);
#elif 0
	// on OSX this is super slow (200fps instead of 470-500), BUT it is as fast as glBufferData() when orphaning first
	// nvidia/linux-blob doesn't care about this vs glBufferData()
	// AMD open source linux (R3 370) is also slower here (not as bad as OSX though)
	// intel linux doesn't seem to care either (maybe 3% faster, but that might be imagination)
	// AMD Windows legacy driver (Radeon HD 6950) doesn't care, all 3 alternatives seem to be equally fast
	//glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_DYNAMIC_DRAW); // orphan
	glBufferSubData(GL_UNIFORM_BUFFER, 0, size, data);
#else
	// with my current nvidia-driver (GTX 770, 375.39), the following *really* makes it slower. (<140fps instead of ~850)
	// on OSX (Intel Haswell Iris Pro, OSX 10.11) this is fastest (~500fps instead of ~470)
	// on Linux/intel (Ivy Bridge HD-4000, Linux 4.4) this might be a tiny bit faster than the alternatives..
	glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_DYNAMIC_DRAW); // orphan
	GLvoid* ptr = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
	memcpy(ptr, data, size);
	glUnmapBuffer(GL_UNIFORM_BUFFER);
#endif

	// TODO: another alternative: glMapBufferRange() and each time update a different part
	//       of buffer asynchronously (GL_MAP_UNSYNCHRONIZED_BIT) => ringbuffer style
	//       when starting again from the beginning, synchronization must happen I guess..
	//       also, orphaning might be necessary
	//       and somehow make sure the new range is used by the UBO => glBindBufferRange()
	//  see http://git.quintin.ninja/mjones/Dolphin/blob/4a463f4588e2968c499236458c5712a489622633/Source/Plugins/Plugin_VideoOGL/Src/ProgramShaderCache.cpp#L207
	//   or https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/OGL/ProgramShaderCache.cpp
}

void GL3_UpdateUBOCommon(void)
{
	updateUBO(gl3state.uniCommonUBO, sizeof(gl3state.uniCommonData), &gl3state.uniCommonData);
}

void GL3_UpdateUBO2D(void)
{
	updateUBO(gl3state.uni2DUBO, sizeof(gl3state.uni2DData), &gl3state.uni2DData);
}

/*
 * OpenMerc: the 3D uniform block changes between draws of one batch (model matrix, emissive weight, texture size,
 * scroll, fog, alpha) - hundreds of times a frame. Each glBufferData re-specified the one buffer that queued draws
 * still read. r_om_uboring 1 (default): a ring of aligned slots in one big buffer; every update maps the next slot
 * unsynchronised (nothing queued reads it: the ring is orphaned once per lap) and binds that range.
 */
static GLuint ring3d;
static GLintptr ringoff, ringlast;
static GLsizeiptr ringstride, ringsize;
static qboolean ringbound;
static cvar_t *r_om_uboring;

#define UBORING_SLOTS 2048

static qboolean
Ring3D(void)
{
	GLint align = 256;

	if (!r_om_uboring)
	{
		r_om_uboring = ri.Cvar_Get("r_om_uboring", "1", CVAR_ARCHIVE);
	}

	if (!r_om_uboring->value)
	{
		return false;
	}

	if (ring3d)
	{
		return true;
	}

	glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
	align = (align > 0) ? align : 256;
	ringstride = ((GLsizeiptr)sizeof(gl3state.uni3DData) + align - 1) / align * align;
	ringsize = ringstride * UBORING_SLOTS;
	glGenBuffers(1, &ring3d);
	glBindBuffer(GL_UNIFORM_BUFFER, ring3d);
	gl3state.currentUBO = ring3d;
	glBufferData(GL_UNIFORM_BUFFER, ringsize, NULL, GL_STREAM_DRAW);
	ringoff = 0;
	ringlast = 0;
	return true;
}

void
GL3_Ring3DShutdown(void)
{
	if (ring3d)
	{
		glDeleteBuffers(1, &ring3d);
	}

	ring3d = 0;
	ringbound = false;
}

void GL3_UpdateUBO3D(void)
{
	void *p;

	if (!Ring3D())
	{
		if (ringbound)
		{
			glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI3D, gl3state.uni3DUBO);
			ringbound = false;
		}

		updateUBO(gl3state.uni3DUBO, sizeof(gl3state.uni3DData), &gl3state.uni3DData);
		return;
	}

	if (gl3state.currentUBO != ring3d)
	{
		gl3state.currentUBO = ring3d;
		glBindBuffer(GL_UNIFORM_BUFFER, ring3d);
	}

	if (ringoff + ringstride > ringsize)
	{
		glBufferData(GL_UNIFORM_BUFFER, ringsize, NULL, GL_STREAM_DRAW); /* orphan: a fresh store for the next lap */
		ringoff = 0;
	}

	++gl3_numBufferUniforms;
	p = glMapBufferRange(GL_UNIFORM_BUFFER, ringoff, ringstride,
		GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);

	if (p)
	{
		memcpy(p, &gl3state.uni3DData, sizeof(gl3state.uni3DData));
		glUnmapBuffer(GL_UNIFORM_BUFFER);
	}
	else
	{
		glBufferSubData(GL_UNIFORM_BUFFER, ringoff, sizeof(gl3state.uni3DData), &gl3state.uni3DData);
	}

	glBindBufferRange(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI3D, ring3d, ringoff, sizeof(gl3state.uni3DData));
	ringbound = true;
	ringlast = ringoff;
	ringoff += ringstride;
}

/* binds the 3D block where its data is now (paths that rebind all binding points) */
void
GL3_BindUBO3D(void)
{
	if (ringbound && ring3d)
	{
		glBindBufferRange(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI3D, ring3d, ringlast, sizeof(gl3state.uni3DData));
	}
	else
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI3D, gl3state.uni3DUBO);
	}
}

void GL3_UpdateUBOLights(void)
{
	updateUBO(gl3state.uniLightsUBO, sizeof(gl3state.uniLightsData), &gl3state.uniLightsData);
}

/*
 * OpenMerc (I4c): the gl_monolightmap mode (latched at map load, gl3_lightmap.c) for the lightmapped
 * world shaders; r_fullbright and night vision use the colour-table path (no lightmaps). Called once per
 * 3D view (set every time: recreated programs start at 0).
 */
/* GFX-modern r_om_rain (gl3_post.c): the strike's shadow map (texture unit 9) and its view-projection */
void GL3_SoF_LightningShadow(const float *shvp, GLuint shtex)
{
	gl3ShaderInfo_t *si[6] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam, &gl3state.si3DlmOpaque, &gl3state.si3DlmNoDl};
	int i;

	for (i = 0; i < 6; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = GL3_UniformLoc(si[i]->shaderProgram, "omLtShVP")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniformMatrix4fv(loc, 1, GL_FALSE, shvp);
		glUniform1i(GL3_UniformLoc(si[i]->shaderProgram, "omLtSh"), 9);
	}

	if (shtex)
	{
		GL3_SelectTMU(GL_TEXTURE9);
		glBindTexture(GL_TEXTURE_2D, shtex);
		GL3_SelectTMU(GL_TEXTURE0);
	}
}

/* GFX-modern r_om_rain (gl3_post.c): the lightning as a light of the world shaders - direction towards the channel
   and strength (w 0 = none), colour, the top-down depth map (texture unit 12) and its view-projection */
void GL3_SoF_LightningUniforms(const float *bolt, const float *color, const float *occvp, GLuint occtex)
{
	gl3ShaderInfo_t *si[6] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam, &gl3state.si3DlmOpaque, &gl3state.si3DlmNoDl};
	int i;

	for (i = 0; i < 6; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = GL3_UniformLoc(si[i]->shaderProgram, "omLt")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniform4fv(loc, 1, bolt);
		glUniform4fv(GL3_UniformLoc(si[i]->shaderProgram, "omLtCol"), 1, color);

		if (occvp)
		{
			glUniformMatrix4fv(GL3_UniformLoc(si[i]->shaderProgram, "omLtOccVP"), 1, GL_FALSE, occvp);
		}

		glUniform1i(GL3_UniformLoc(si[i]->shaderProgram, "omLtOcc"), 12);
	}

	if (occtex)
	{
		GL3_SelectTMU(GL_TEXTURE12);
		glBindTexture(GL_TEXTURE_2D, occtex);
		GL3_SelectTMU(GL_TEXTURE0);
	}
}

/* GFX-modern r_om_mirror: the mirror images and their planes (texture units 13 and 15) */
void GL3_SoF_MirrorUniforms(const float *on, const float *plane, const float *plane2, const float *rect, GLuint tex,
	GLuint tex2)
{
	gl3ShaderInfo_t *si[6] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam, &gl3state.si3DlmOpaque, &gl3state.si3DlmNoDl};
	int i;

	for (i = 0; i < 6; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = GL3_UniformLoc(si[i]->shaderProgram, "omMirror")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniform4fv(loc, 1, on);
		glUniform4fv(GL3_UniformLoc(si[i]->shaderProgram, "omMirrorPlane"), 1, plane);
		glUniform4fv(GL3_UniformLoc(si[i]->shaderProgram, "omMirrorRect"), 1, rect);
		glUniform1i(GL3_UniformLoc(si[i]->shaderProgram, "omMirrorTex"), 13);
		glUniform4fv(GL3_UniformLoc(si[i]->shaderProgram, "omMirrorPlane2"), 1, plane2);
		glUniform1i(GL3_UniformLoc(si[i]->shaderProgram, "omMirrorTex2"), 15);
	}

	if (tex)
	{
		GL3_SelectTMU(GL_TEXTURE13);
		glBindTexture(GL_TEXTURE_2D, tex);
		GL3_SelectTMU(GL_TEXTURE0);
	}

	if (tex2)
	{
		GL3_SelectTMU(GL_TEXTURE15);
		glBindTexture(GL_TEXTURE_2D, tex2);
		GL3_SelectTMU(GL_TEXTURE0);
	}
}

/* GFX-modern r_om_ssr: the parameters of the material classes (gl3_material.c) */
void GL3_SoF_MaterialUniforms(const float *a, const float *b, int n)
{
	gl3ShaderInfo_t *si[6] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam, &gl3state.si3DlmOpaque, &gl3state.si3DlmNoDl};
	int i;

	for (i = 0; i < 6; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = GL3_UniformLoc(si[i]->shaderProgram, "omMatA[0]")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniform4fv(loc, n, a);
		glUniform4fv(GL3_UniformLoc(si[i]->shaderProgram, "omMatB[0]"), n, b);
	}
}

/* GFX-modern r_om_ssr: reflections on, rain wetness (gl3_post.c) */
void GL3_SoF_SSRUniforms(const float *ssr)
{
	gl3ShaderInfo_t *si[6] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam, &gl3state.si3DlmOpaque, &gl3state.si3DlmNoDl};
	int i;

	for (i = 0; i < 6; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = GL3_UniformLoc(si[i]->shaderProgram, "omSsr")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniform4fv(loc, 1, ssr);
	}
}

/* the gl_monolightmap mode the world shaders run (its A / C modes cut texels by the lightmap: alpha tested) */
static int worldmono;

/* a face of this texture: without alpha (most walls) the world shader without discard, so the GPU keeps its early
   depth test (Adreno: LRZ, a discard anywhere in the shader turns it off for the draw); grates, fences and the
   gl_monolightmap modes that cut by the lightmap the alpha tested one */
gl3ShaderInfo_t *
GL3_WorldShaderFor(const gl3image_t *image)
{
	static cvar_t *r_om_worldopaque;

	if (!r_om_worldopaque)
	{
		/* 0 by default: on the Adreno 610 the split into two programs cost 10 ms (arm1, lamp shadows off) */
		r_om_worldopaque = ri.Cvar_Get("r_om_worldopaque", "0", 0);
	}

	return (image && !image->has_alpha && (worldmono < 3) && gl3state.si3DlmOpaque.shaderProgram && r_om_worldopaque->value) ?
		&gl3state.si3DlmOpaque : &gl3state.si3Dlm;
}

void GL3_SoF_ApplyMonoLightmap(qboolean fullbright)
{
	extern int gl3_monolightmap_mode;
	gl3ShaderInfo_t *si[6] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam, &gl3state.si3DlmOpaque, &gl3state.si3DlmNoDl};
	int mode = fullbright ? 0 : gl3_monolightmap_mode;
	int i;

	worldmono = mode;

	for (i = 0; i < 6; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram)
		{
			continue;
		}

		loc = GL3_UniformLoc(si[i]->shaderProgram, "omMono");

		if (loc != -1)
		{
			GL3_UseProgram(si[i]->shaderProgram);
			glUniform1i(loc, mode);
		}
	}
}

/* header/local.h GL3_UniformLoc: (program, name pointer, context) -> location, open addressing */
#define UNICACHE 512

static struct
{
	GLuint prog;
	const char *name;
	int gen;
	GLint loc;
} unicache[UNICACHE];

void
GL3_UniformCacheClear(void)
{
	memset(unicache, 0, sizeof(unicache));
}

static void
UniformLocFlush(void)
{
	memset(unicache, 0, sizeof(unicache));
}

GLint
GL3_UniformLoc(GLuint prog, const char *name)
{
	unsigned h = ((unsigned)prog * 2654435761u) ^ (unsigned)((size_t)name >> 3);
	int i;

	if (!prog)
	{
		return -1;
	}

	for (i = 0; i < 16; i++)
	{
		int k = (int)((h + (unsigned)i) % UNICACHE);

		if ((unicache[k].prog == prog) && (unicache[k].name == name) && (unicache[k].gen == gl3_contextgen))
		{
			return unicache[k].loc;
		}

		if (!unicache[k].prog || (unicache[k].gen != gl3_contextgen))
		{
			unicache[k].prog = prog;
			unicache[k].name = name;
			unicache[k].gen = gl3_contextgen;
			unicache[k].loc = glGetUniformLocation(prog, name);
			return unicache[k].loc;
		}
	}

	return glGetUniformLocation(prog, name); /* a full neighbourhood: ask the driver */
}
