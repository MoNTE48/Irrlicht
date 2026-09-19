// Copyright (C) 2014 Patryk Nadrowski
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in Irrlicht.h

#include "COGLES2FixedPipelineRenderer.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "IVideoDriver.h"

namespace irr
{
namespace video
{

// Base callback

COGLES2MaterialBaseCB::COGLES2MaterialBaseCB() :
	FirstUpdateBase(true), WVPMatrixID(-1), WVMatrixID(-1), FogEnableID(-1), FogTypeID(-1),
			FogColorID(-1), FogStartID(-1), FogEndID(-1), FogDensityID(-1), ThicknessID(-1),
	FogEnable(0), FogType(1), FogColor(SColorf(0.f, 0.f, 0.f, 1.f)), FogStart(0.f), FogEnd(0.f),
			FogDensity(0.f), Thickness(1.f), TransformSent(false)
{
}

void COGLES2MaterialBaseCB::OnSetMaterial(const SMaterial& material)
{
	FogEnable = material.FogEnable ? 1 : 0;

	Thickness = (material.Thickness > 0.f) ? material.Thickness : 1.f;
}

static_assert(sizeof(SColorf) == 4 * sizeof(f32));
static_assert(sizeof(core::matrix4) == 16 * sizeof(f32));

//! Sends a uniform unless the program already holds the value
template <class T, class E = T>
static void sendIfChanged(IMaterialRendererServices* services, s32 id, const T& value,
		std::optional<T>& sent)
{
	if (sent && !memcmp(&value, &*sent, sizeof(T)))
		return;
	services->setPixelShaderConstant(id, reinterpret_cast<const E*>(&value), sizeof(T) / sizeof(E));
	sent = value;
}

void COGLES2MaterialBaseCB::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
	IVideoDriver* driver = services->getVideoDriver();

	if (FirstUpdateBase)
	{
		WVPMatrixID = services->getVertexShaderConstantID("uWVPMatrix");
		WVMatrixID = services->getVertexShaderConstantID("uWVMatrix");
		FogEnableID = services->getVertexShaderConstantID("uFogEnable");
		FogTypeID = services->getVertexShaderConstantID("uFogType");
		FogColorID = services->getVertexShaderConstantID("uFogColor");
		FogStartID = services->getVertexShaderConstantID("uFogStart");
		FogEndID = services->getVertexShaderConstantID("uFogEnd");
		FogDensityID = services->getVertexShaderConstantID("uFogDensity");
		ThicknessID = services->getVertexShaderConstantID("uThickness");

		FirstUpdateBase = false;
	}

	const core::matrix4& W = driver->getTransform(ETS_WORLD);
	const core::matrix4& V = driver->getTransform(ETS_VIEW);
	const core::matrix4& P = driver->getTransform(ETS_PROJECTION);

	//! Multiply the camera pair only when it moved
	const bool cameraMoved = P != Projection || V != View;
	if (cameraMoved)
	{
		Projection = P;
		View = V;
		ViewProjection = P * V;
	}

	if (!TransformSent || cameraMoved || W != World)
	{
		World = W;
		const core::matrix4 Matrix = ViewProjection * W;
		services->setPixelShaderConstant(WVPMatrixID, Matrix.pointer(), 16);
		WorldView = V * W;
		services->setPixelShaderConstant(WVMatrixID, WorldView.pointer(), 16);
		TransformSent = true;
	}

	sendIfChanged(services, FogEnableID, FogEnable, SentFogEnable);

	if (FogEnable)
	{
		SColor TempColor(0);
		E_FOG_TYPE TempType = EFT_FOG_LINEAR;
		bool TempPerFragment = false;
		bool TempRange = false;

		driver->getFog(TempColor, TempType, FogStart, FogEnd, FogDensity, TempPerFragment, TempRange);

		FogType = (s32)TempType;
		FogColor = SColorf(TempColor);

		sendIfChanged(services, FogTypeID, FogType, SentFogType);
		sendIfChanged<SColorf, f32>(services, FogColorID, FogColor, SentFogColor);
		sendIfChanged(services, FogStartID, FogStart, SentFogStart);
		sendIfChanged(services, FogEndID, FogEnd, SentFogEnd);
		sendIfChanged(services, FogDensityID, FogDensity, SentFogDensity);
	}

	sendIfChanged(services, ThicknessID, Thickness, SentThickness);
}

// EMT_SOLID + EMT_TRANSPARENT_ADD_COLOR + EMT_TRANSPARENT_ALPHA_CHANNEL + EMT_TRANSPARENT_VERTEX_ALPHA

COGLES2MaterialSolidCB::COGLES2MaterialSolidCB() :
	FirstUpdate(true), TMatrix0ID(-1), AlphaRefID(-1), TextureUsage0ID(-1), TextureUnit0ID(-1), AlphaRef(0.5f), TextureUsage0(0), TextureUnit0(0)
{
}

void COGLES2MaterialSolidCB::OnSetMaterial(const SMaterial& material)
{
	COGLES2MaterialBaseCB::OnSetMaterial(material);

	AlphaRef = material.MaterialTypeParam;
	TextureUsage0 = (material.TextureLayer[0].Texture) ? 1 : 0;
}

void COGLES2MaterialSolidCB::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
	COGLES2MaterialBaseCB::OnSetConstants(services, userData);

	IVideoDriver* driver = services->getVideoDriver();

	if (FirstUpdate)
	{
		TMatrix0ID = services->getVertexShaderConstantID("uTMatrix0");
		AlphaRefID = services->getVertexShaderConstantID("uAlphaRef");
		TextureUsage0ID = services->getVertexShaderConstantID("uTextureUsage0");
		TextureUnit0ID = services->getVertexShaderConstantID("uTextureUnit0");

		FirstUpdate = false;
	}

	sendIfChanged<core::matrix4,
			f32>(services, TMatrix0ID, driver->getTransform(ETS_TEXTURE_0), SentTMatrix0);
	sendIfChanged(services, AlphaRefID, AlphaRef, SentAlphaRef);
	sendIfChanged(services, TextureUsage0ID, TextureUsage0, SentTextureUsage0);
	sendIfChanged(services, TextureUnit0ID, TextureUnit0, SentTextureUnit0);
}

// EMT_SOLID_2_LAYER + EMT_DETAIL_MAP

COGLES2MaterialSolid2CB::COGLES2MaterialSolid2CB() :
	FirstUpdate(true), TMatrix0ID(-1), TMatrix1ID(-1), TextureUsage0ID(-1), TextureUsage1ID(-1), TextureUnit0ID(-1), TextureUnit1ID(-1),
	TextureUsage0(0), TextureUsage1(0), TextureUnit0(0), TextureUnit1(1)
{
}

void COGLES2MaterialSolid2CB::OnSetMaterial(const SMaterial& material)
{
	COGLES2MaterialBaseCB::OnSetMaterial(material);

	TextureUsage0 = (material.TextureLayer[0].Texture) ? 1 : 0;
	TextureUsage1 = (material.TextureLayer[1].Texture) ? 1 : 0;
}

void COGLES2MaterialSolid2CB::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
	COGLES2MaterialBaseCB::OnSetConstants(services, userData);

	IVideoDriver* driver = services->getVideoDriver();

	if (FirstUpdate)
	{
		TMatrix0ID = services->getVertexShaderConstantID("uTMatrix0");
		TMatrix1ID = services->getVertexShaderConstantID("uTMatrix1");
		TextureUsage0ID = services->getVertexShaderConstantID("uTextureUsage0");
		TextureUsage1ID = services->getVertexShaderConstantID("uTextureUsage1");
		TextureUnit0ID = services->getVertexShaderConstantID("uTextureUnit0");
		TextureUnit1ID = services->getVertexShaderConstantID("uTextureUnit1");

		FirstUpdate = false;
	}

	core::matrix4 Matrix = driver->getTransform(ETS_TEXTURE_0);
	services->setPixelShaderConstant(TMatrix0ID, Matrix.pointer(), 16);

	Matrix = driver->getTransform(ETS_TEXTURE_1);
	services->setPixelShaderConstant(TMatrix1ID, Matrix.pointer(), 16);

	services->setPixelShaderConstant(TextureUsage0ID, &TextureUsage0, 1);
	services->setPixelShaderConstant(TextureUsage1ID, &TextureUsage1, 1);
	services->setPixelShaderConstant(TextureUnit0ID, &TextureUnit0, 1);
	services->setPixelShaderConstant(TextureUnit1ID, &TextureUnit1, 1);
}

// EMT_LIGHTMAP + EMT_LIGHTMAP_ADD + EMT_LIGHTMAP_M2 + EMT_LIGHTMAP_M4

COGLES2MaterialLightmapCB::COGLES2MaterialLightmapCB(float modulate) :
	FirstUpdate(true), TMatrix0ID(-1), TMatrix1ID(-1), ModulateID(-1), TextureUsage0ID(-1), TextureUsage1ID(-1), TextureUnit0ID(-1), TextureUnit1ID(-1),
	Modulate(modulate), TextureUsage0(0), TextureUsage1(0), TextureUnit0(0), TextureUnit1(1)
{
}

void COGLES2MaterialLightmapCB::OnSetMaterial(const SMaterial& material)
{
	COGLES2MaterialBaseCB::OnSetMaterial(material);

	TextureUsage0 = (material.TextureLayer[0].Texture) ? 1 : 0;
	TextureUsage1 = (material.TextureLayer[1].Texture) ? 1 : 0;
}

void COGLES2MaterialLightmapCB::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
	COGLES2MaterialBaseCB::OnSetConstants(services, userData);

	IVideoDriver* driver = services->getVideoDriver();

	if (FirstUpdate)
	{
		TMatrix0ID = services->getVertexShaderConstantID("uTMatrix0");
		TMatrix1ID = services->getVertexShaderConstantID("uTMatrix1");
		ModulateID = services->getVertexShaderConstantID("uModulate");
		TextureUsage0ID = services->getVertexShaderConstantID("uTextureUsage0");
		TextureUsage1ID = services->getVertexShaderConstantID("uTextureUsage1");
		TextureUnit0ID = services->getVertexShaderConstantID("uTextureUnit0");
		TextureUnit1ID = services->getVertexShaderConstantID("uTextureUnit1");

		FirstUpdate = false;
	}

	core::matrix4 Matrix = driver->getTransform(ETS_TEXTURE_0);
	services->setPixelShaderConstant(TMatrix0ID, Matrix.pointer(), 16);

	Matrix = driver->getTransform(ETS_TEXTURE_1);
	services->setPixelShaderConstant(TMatrix1ID, Matrix.pointer(), 16);

	services->setPixelShaderConstant(ModulateID, &Modulate, 1);
	services->setPixelShaderConstant(TextureUsage0ID, &TextureUsage0, 1);
	services->setPixelShaderConstant(TextureUsage1ID, &TextureUsage1, 1);
	services->setPixelShaderConstant(TextureUnit0ID, &TextureUnit0, 1);
	services->setPixelShaderConstant(TextureUnit1ID, &TextureUnit1, 1);
}

// EMT_SPHERE_MAP + EMT_REFLECTION_2_LAYER + EMT_TRANSPARENT_REFLECTION_2_LAYER

COGLES2MaterialReflectionCB::COGLES2MaterialReflectionCB() :
	FirstUpdate(true), TMatrix0ID(-1), TextureUsage0ID(-1), TextureUsage1ID(-1), TextureUnit0ID(-1), TextureUnit1ID(-1),
	TextureUsage0(0), TextureUsage1(0), TextureUnit0(0), TextureUnit1(1)
{
}

void COGLES2MaterialReflectionCB::OnSetMaterial(const SMaterial& material)
{
	COGLES2MaterialBaseCB::OnSetMaterial(material);

	TextureUsage0 = (material.TextureLayer[0].Texture) ? 1 : 0;
	TextureUsage1 = (material.TextureLayer[1].Texture) ? 1 : 0;
}

void COGLES2MaterialReflectionCB::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
	COGLES2MaterialBaseCB::OnSetConstants(services, userData);

	IVideoDriver* driver = services->getVideoDriver();

	if (FirstUpdate)
	{
		TMatrix0ID = services->getVertexShaderConstantID("uTMatrix0");
		TextureUsage0ID = services->getVertexShaderConstantID("uTextureUsage0");
		TextureUsage1ID = services->getVertexShaderConstantID("uTextureUsage1");
		TextureUnit0ID = services->getVertexShaderConstantID("uTextureUnit0");
		TextureUnit1ID = services->getVertexShaderConstantID("uTextureUnit1");

		FirstUpdate = false;
	}

	core::matrix4 Matrix = driver->getTransform(ETS_TEXTURE_0);
	services->setPixelShaderConstant(TMatrix0ID, Matrix.pointer(), 16);

	services->setPixelShaderConstant(TextureUsage0ID, &TextureUsage0, 1);
	services->setPixelShaderConstant(TextureUsage1ID, &TextureUsage1, 1);
	services->setPixelShaderConstant(TextureUnit0ID, &TextureUnit0, 1);
	services->setPixelShaderConstant(TextureUnit1ID, &TextureUnit1, 1);
}

// EMT_ONETEXTURE_BLEND

COGLES2MaterialOneTextureBlendCB::COGLES2MaterialOneTextureBlendCB() :
	FirstUpdate(true), TMatrix0ID(-1), BlendTypeID(-1), TextureUsage0ID(-1), TextureUnit0ID(-1), BlendType(0), TextureUsage0(0), TextureUnit0(0)
{
}

void COGLES2MaterialOneTextureBlendCB::OnSetMaterial(const SMaterial& material)
{
	COGLES2MaterialBaseCB::OnSetMaterial(material);

	BlendType = 0;

	E_BLEND_FACTOR srcRGBFact,dstRGBFact,srcAlphaFact,dstAlphaFact;
	E_MODULATE_FUNC modulate;
	u32 alphaSource;
	unpack_textureBlendFuncSeparate(srcRGBFact, dstRGBFact, srcAlphaFact, dstAlphaFact, modulate, alphaSource, material.MaterialTypeParam);

	if (textureBlendFunc_hasAlpha(srcRGBFact) || textureBlendFunc_hasAlpha(dstRGBFact) || textureBlendFunc_hasAlpha(srcAlphaFact) || textureBlendFunc_hasAlpha(dstAlphaFact))
	{
		if (alphaSource == EAS_VERTEX_COLOR)
		{
			BlendType = 1;
		}
		else if (alphaSource == EAS_TEXTURE)
		{
			BlendType = 2;
		}
	}

	TextureUsage0 = (material.TextureLayer[0].Texture) ? 1 : 0;
}

void COGLES2MaterialOneTextureBlendCB::OnSetConstants(IMaterialRendererServices* services, s32 userData)
{
	COGLES2MaterialBaseCB::OnSetConstants(services, userData);

	IVideoDriver* driver = services->getVideoDriver();

	if (FirstUpdate)
	{
		TMatrix0ID = services->getVertexShaderConstantID("uTMatrix0");
		BlendTypeID = services->getVertexShaderConstantID("uBlendType");
		TextureUsage0ID = services->getVertexShaderConstantID("uTextureUsage0");
		TextureUnit0ID = services->getVertexShaderConstantID("uTextureUnit0");

		FirstUpdate = false;
	}

	core::matrix4 Matrix = driver->getTransform(ETS_TEXTURE_0);
	services->setPixelShaderConstant(TMatrix0ID, Matrix.pointer(), 16);

	services->setPixelShaderConstant(BlendTypeID, &BlendType, 1);
	services->setPixelShaderConstant(TextureUsage0ID, &TextureUsage0, 1);
	services->setPixelShaderConstant(TextureUnit0ID, &TextureUnit0, 1);
}

}
}

#endif

