#version 310 es
// Fixed pipeline of the Metal driver; inputs numbered as E_VERTEX_ATTRIBUTES
precision highp float;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform Uniforms
{
	mat4 worldViewProj;
	mat4 worldView;
#if 0
	// Lit colour from the material, 0 when unlit
	vec4 lit;
	// Vertex colour's weight in the lit colour, 1 when unlit
	vec4 litByVertex;
	// First layer's texture matrix: rows over (u, v, 1)
	vec4 textureRows[2];
#endif
} uniforms;

// Same position for a vertex in every pipeline
invariant gl_Position;

layout(location = 0) out lowp vec4 vColor;
layout(location = 1) out mediump vec2 vTexCoord;
layout(location = 3) out float vFog;

void main()
{
	gl_Position = uniforms.worldViewProj * vec4(inPosition, 1.0);
	// SColor sits in memory as B, G, R, A
	vec4 color = inColor.bgra;
#if 0
	// OpenGL's lit colour with no lights
	vColor = vec4(clamp(uniforms.lit.rgb + color.rgb * uniforms.litByVertex.rgb, 0.0, 1.0),
			mix(uniforms.lit.a, color.a, uniforms.litByVertex.a));
	vTexCoord = vec2(dot(uniforms.textureRows[0].xyz, vec3(inTexCoord, 1.0)),
			dot(uniforms.textureRows[1].xyz, vec3(inTexCoord, 1.0)));
#endif
	vColor = color;
	vTexCoord = inTexCoord;
	// Range fog: distance from the eye, not depth
	vFog = length((uniforms.worldView * vec4(inPosition, 1.0)).xyz);
}
