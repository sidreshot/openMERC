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
	const char* version = "#version 300 es\nprecision highp float;\n";
#else // Desktop GL
	const char* version = "#version 150\n";
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
			lmTex     += texture(lightmap1, passLMcoord) * lmScales[1];
			lmTex     += texture(lightmap2, passLMcoord) * lmScales[2];
			lmTex     += texture(lightmap3, passLMcoord) * lmScales[3];
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
			lmTex     += texture(lightmap1, passLMcoord) * lmScales[1];
			lmTex     += texture(lightmap2, passLMcoord) * lmScales[2];
			lmTex     += texture(lightmap3, passLMcoord) * lmScales[3];
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

		uniform vec4 fogplane; // view forward and -forward . eye

		void main()
		{
			vec4 wp = transModel * vec4(position, 1.0);

			passTexCoord = texCoord;
			passColor = vertColor;
			passFogDist = abs(dot(fogplane.xyz, wp.xyz) + fogplane.w);
			passFxWorld = wp.xyz;
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

		uniform vec4 omFxMat; // GFX-modern r_om_ssr: reflectivity, roughness of liquids (0 = the default material)

		float omEyeDist(float z)
		{
			float ndc = (z - omSoftP.z) / (omSoftP.w - omSoftP.z) * 2.0 - 1.0;
			return omSoftP.y / (ndc + omSoftP.x);
		}

		void main()
		{
			vec4 c;

			if(omPF0.w > 0.5)
			{
				c = vec4(passColor.rgb, omPPFog(passFxWorld, passColor.a));
			}
			else if(texmode == 2)
			{
				c = passColor;
			}
			else
			{
				vec4 t = texture(tex, passTexCoord);
				c = (texmode == 1) ? t : t * passColor;
			}

			if(alphatest != 0 && c.a <= 0.05)
				discard;

			if(fogmode != 0)
			{
				float f = clamp((fogend - passFogDist) / max(fogend - fogstart, 0.0001), 0.0, 1.0);
				c.rgb = mix(fogcolor.rgb, c.rgb, f);
			}

			if(omSoftV.w > 0.5)
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
			if(omFxMat.x > 0.0) outMaterial = vec4(omFxMat.x, omFxMat.x * omFxMat.y, 0.0, outColor.a);
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
		lmFragCsm = GL3_CSM_Prepend(joined);
		free(joined);
	}
	if(lmFragCsm == NULL)
	{
		return false;
	}
	const char* lightmappedFrag = lmFragCsm;

	if(!initShader3D(&gl3state.si3Dlm, vertexSrc3Dlm, lightmappedFrag))
	{
		Com_Printf("WARNING: Failed to create shader program for textured 3D rendering with lightmap!\n");
		return false;
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
	if(!initShader3D(&gl3state.si3DlmTrans, vertexSrc3Dlm, lightmappedFrag))
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
	if(!initShader3D(&gl3state.si3DlmSeam, vertexSrc3Dlm, lightmappedFrag))
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
	glDeleteBuffers(4, &gl3state.uniCommonUBO);
	gl3state.uniCommonUBO = gl3state.uni2DUBO = gl3state.uni3DUBO = gl3state.uniLightsUBO = 0;
}

qboolean GL3_RecreateShaders(void)
{
	// delete and recreate the existing shaders (but not the UBOs)
	deleteShaders();
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

void GL3_UpdateUBO3D(void)
{
	updateUBO(gl3state.uni3DUBO, sizeof(gl3state.uni3DData), &gl3state.uni3DData);
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
	gl3ShaderInfo_t *si[4] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam};
	int i;

	for (i = 0; i < 4; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = glGetUniformLocation(si[i]->shaderProgram, "omLtShVP")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniformMatrix4fv(loc, 1, GL_FALSE, shvp);
		glUniform1i(glGetUniformLocation(si[i]->shaderProgram, "omLtSh"), 9);
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
	gl3ShaderInfo_t *si[4] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam};
	int i;

	for (i = 0; i < 4; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = glGetUniformLocation(si[i]->shaderProgram, "omLt")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniform4fv(loc, 1, bolt);
		glUniform4fv(glGetUniformLocation(si[i]->shaderProgram, "omLtCol"), 1, color);

		if (occvp)
		{
			glUniformMatrix4fv(glGetUniformLocation(si[i]->shaderProgram, "omLtOccVP"), 1, GL_FALSE, occvp);
		}

		glUniform1i(glGetUniformLocation(si[i]->shaderProgram, "omLtOcc"), 12);
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
	gl3ShaderInfo_t *si[4] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam};
	int i;

	for (i = 0; i < 4; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = glGetUniformLocation(si[i]->shaderProgram, "omMirror")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniform4fv(loc, 1, on);
		glUniform4fv(glGetUniformLocation(si[i]->shaderProgram, "omMirrorPlane"), 1, plane);
		glUniform4fv(glGetUniformLocation(si[i]->shaderProgram, "omMirrorRect"), 1, rect);
		glUniform1i(glGetUniformLocation(si[i]->shaderProgram, "omMirrorTex"), 13);
		glUniform4fv(glGetUniformLocation(si[i]->shaderProgram, "omMirrorPlane2"), 1, plane2);
		glUniform1i(glGetUniformLocation(si[i]->shaderProgram, "omMirrorTex2"), 15);
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
	gl3ShaderInfo_t *si[4] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam};
	int i;

	for (i = 0; i < 4; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = glGetUniformLocation(si[i]->shaderProgram, "omMatA[0]")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniform4fv(loc, n, a);
		glUniform4fv(glGetUniformLocation(si[i]->shaderProgram, "omMatB[0]"), n, b);
	}
}

/* GFX-modern r_om_ssr: reflections on, rain wetness (gl3_post.c) */
void GL3_SoF_SSRUniforms(const float *ssr)
{
	gl3ShaderInfo_t *si[4] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam};
	int i;

	for (i = 0; i < 4; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram || (loc = glGetUniformLocation(si[i]->shaderProgram, "omSsr")) == -1)
		{
			continue;
		}

		GL3_UseProgram(si[i]->shaderProgram);
		glUniform4fv(loc, 1, ssr);
	}
}

void GL3_SoF_ApplyMonoLightmap(qboolean fullbright)
{
	extern int gl3_monolightmap_mode;
	gl3ShaderInfo_t *si[4] = {&gl3state.si3Dlm, &gl3state.si3DlmFlow, &gl3state.si3DlmTrans, &gl3state.si3DlmSeam};
	int mode = fullbright ? 0 : gl3_monolightmap_mode;
	int i;

	for (i = 0; i < 4; i++)
	{
		GLint loc;

		if (!si[i]->shaderProgram)
		{
			continue;
		}

		loc = glGetUniformLocation(si[i]->shaderProgram, "omMono");

		if (loc != -1)
		{
			GL3_UseProgram(si[i]->shaderProgram);
			glUniform1i(loc, mode);
		}
	}
}
