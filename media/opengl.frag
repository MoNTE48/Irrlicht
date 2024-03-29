
uniform sampler2D myTexture;
uniform vec4 mEmissive;

void main (void)
{
    vec4 col = texture2D(myTexture, vec2(gl_TexCoord[0]));
    col *= gl_Color;
    gl_FragColor = col * 4.0;
	gl_FragColor += mEmissive;
}
