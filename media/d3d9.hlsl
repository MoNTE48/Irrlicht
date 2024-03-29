// Part of the Irrlicht Engine Shader example.
// These simple Direct3D9 pixel and vertex shaders will be loaded by the shaders
// example. Please note that these example shaders don't do anything really useful.
// They only demonstrate that shaders can be used in Irrlicht.

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
float4x4 mWorldViewProj; // World * View * Projection transformation
float4x4 mInvWorld;      // Inverted world matrix
float4x4 mTransWorld;    // Transposed world matrix
float3 mLightPos;        // Light position (actually just camera-pos in this case)
float4 mLightColor;      // Light color
float4 mEmissive;        // Emissive material color


// Vertex shader output structure
struct VS_OUTPUT
{
	float4 Position : POSITION;  // vertex position
	float4 Diffuse  : COLOR0;    // vertex diffuse color
	float2 TexCoord : TEXCOORD0; // tex coords
//	float3 Tangent   : TEXCOORD1;	// Not used in this example, but additional values can be passed on as tex coords
//	float3 Binormal  : TEXCOORD2;	// Not used in this example, but additional values can be passed on as tex coords
};


VS_OUTPUT vertexMain( in float4 vPosition : POSITION
					, in float3 vNormal   : NORMAL
					, float2 texCoord     : TEXCOORD0 
					//,float3 Tangent     : TEXCOORD1;	// Used for Tangent when working with S3DVertexTangents
					//,float3 Binormal    : TEXCOORD2;	// Used for Binormal when working with S3DVertexTangents
					)
{
	VS_OUTPUT Output;

	// transform position to clip space
	Output.Position = mul(vPosition, mWorldViewProj);

	// transform normal somehow (NOTE: for the real vertex normal you would use an inverse-transpose world matrix instead of mInvWorld)
	float3 normal = mul(float4(vNormal,0.0), mInvWorld);

	// renormalize normal
	normal = normalize(normal);

	// position in world coordinates (NOTE: not sure why transposed world is used instead of world?)
	float3 worldpos = mul(mTransWorld, vPosition);

	// calculate light vector, vtxpos - lightpos
	float3 lightVector = worldpos - mLightPos;

	// normalize light vector
	lightVector = normalize(lightVector);

	// calculate light color
	float3 tmp = dot(-lightVector, normal);
	tmp = lit(tmp.x, tmp.y, 1.0);

	tmp = mLightColor * tmp.y;
	Output.Diffuse = float4(tmp.x, tmp.y, tmp.z, 0);
	Output.TexCoord = texCoord;

	return Output;
}


// Pixel shader output structure
struct PS_OUTPUT
{
	float4 RGBColor : COLOR0; // Pixel color
};


sampler2D myTexture;
	
PS_OUTPUT pixelMain(float2 TexCoord : TEXCOORD0,
					float4 Position : POSITION,
					float4 Diffuse  : COLOR0 ) 
{
	PS_OUTPUT Output;

	float4 col = tex2D( myTexture, TexCoord ); // sample color map

	// multiply with diffuse and do other senseless operations
	Output.RGBColor = Diffuse * col;
	Output.RGBColor *= 4.0;
	Output.RGBColor += mEmissive;

	return Output;
}

