#version 310 es
// Colour of the depth clear triangle, which blending leaves out
precision mediump float;

layout(location = 0) out vec4 fragColor;

void main()
{
	fragColor = vec4(0.0);
}
