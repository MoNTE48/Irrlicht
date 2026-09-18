#version 310 es
// Full-screen triangle at a given depth, for the depth clears Metal draws inside a pass
precision highp float;

layout(set = 0, binding = 0) uniform Depth
{
	float depth;
} target;

void main()
{
	gl_Position = vec4(gl_VertexIndex == 2 ? 3.0 : -1.0, gl_VertexIndex == 1 ? 3.0 : -1.0,
			target.depth, 1.0);
}
