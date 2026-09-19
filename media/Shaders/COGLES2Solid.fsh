precision mediump float;

/* Uniforms */

uniform int uTextureUsage0;
uniform sampler2D uTextureUnit0;
uniform int uFogEnable;
uniform int uFogType;
uniform vec4 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

/* Varyings */

varying vec2 vTextureCoord0;
varying vec4 vVertexColor;
#if 0
varying vec4 vSpecularColor;
#endif
varying float vFogCoord;

float computeFog()
{
	const float LOG2 = 1.442695;
	float FogFactor = 0.0;

	if (uFogType == 0) // Exp
	{
		FogFactor = exp2(-uFogDensity * vFogCoord * LOG2);
	}
	else if (uFogType == 1) // Linear
	{
		float Scale = 1.0 / (uFogEnd - uFogStart);
		FogFactor = (uFogEnd - vFogCoord) * Scale;
	}
	else if (uFogType == 2) // Exp2
	{
		FogFactor = exp2(-uFogDensity * uFogDensity * vFogCoord * vFogCoord * LOG2);
	}

	FogFactor = clamp(FogFactor, 0.0, 1.0);

	return FogFactor;
}

void main()
{
	vec4 Color = vVertexColor;

	if (bool(uTextureUsage0))
		Color *= texture2D(uTextureUnit0, vTextureCoord0);
#if 0
	Color += vSpecularColor;
#endif

	if (bool(uFogEnable))
	{
		float FogFactor = computeFog();
#if 0
		vec4 FogColor = uFogColor;
		FogColor.a = 1.0;
		Color = mix(FogColor, Color, FogFactor);
#endif
		Color.rgb = mix(uFogColor.rgb, Color.rgb, FogFactor);
	}

	gl_FragColor = Color;
}
