#version 310 es
precision mediump float;

layout(constant_id = 0) const bool useTexture = false;
layout(constant_id = 1) const bool alphaTest = false;
// Alpha from: 0 vertex times texture, 1 texture, 2 vertex
layout(constant_id = 3) const int alphaSource = 0;

// Fog and alpha reference
layout(set = 1, binding = 0) uniform Fog
{
	highp vec4 color;
	// 1 / (end - start) of the linear fog
	highp float scale;
	highp float end;
	uint mode;          // 0 none, 1 linear
	highp float alphaRef;
} fog;

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(location = 0) in lowp vec4 vColor;
layout(location = 1) in mediump vec2 vTexCoord;
layout(location = 3) in highp float vFog;

layout(location = 0) out vec4 fragColor;

void main()
{
	vec4 c = vColor;
	if (useTexture)
	{
		vec4 t = texture(tex, vTexCoord);
		c.rgb *= t.rgb;
		if (alphaSource == 0)
			c.a *= t.a;
		else if (alphaSource == 1)
			c.a = t.a;
	}

	// OpenGL keeps what is greater than the reference
	if (alphaTest && c.a <= fog.alphaRef)
		discard;

	if (fog.mode != 0u)
		c.rgb = mix(fog.color.rgb, c.rgb, clamp((fog.end - vFog) * fog.scale, 0.0, 1.0));

	fragColor = c;
}
