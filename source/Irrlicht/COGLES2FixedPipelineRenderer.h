// Copyright (C) 2014 Patryk Nadrowski
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in Irrlicht.h

#ifndef __C_OGLES2_FIXED_PIPELINE_SHADER_H_INCLUDED__
#define __C_OGLES2_FIXED_PIPELINE_SHADER_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "IShaderConstantSetCallBack.h"
#include "IMaterialRendererServices.h"
#include <optional>

namespace irr
{
namespace video
{

class COGLES2MaterialBaseCB : public IShaderConstantSetCallBack
{
public:
	COGLES2MaterialBaseCB();

	virtual void OnSetMaterial(const SMaterial& material);
	virtual void OnSetConstants(IMaterialRendererServices* services, s32 userData);

protected:
	bool FirstUpdateBase;

	s32 WVPMatrixID;
	s32 WVMatrixID;

	s32 FogEnableID;
	s32 FogTypeID;
	s32 FogColorID;
	s32 FogStartID;
	s32 FogEndID;
	s32 FogDensityID;

	s32 ThicknessID;

	s32 FogEnable;
	s32 FogType;
	SColorf FogColor;
	f32 FogStart;
	f32 FogEnd;
	f32 FogDensity;

	f32 Thickness;

	//! The program keeps what it was sent, so a transform goes again only when it moved
	core::matrix4 View, Projection, ViewProjection, World, WorldView;
	bool TransformSent;
	std::optional<s32> SentFogEnable, SentFogType;
	std::optional<f32> SentThickness, SentFogStart, SentFogEnd, SentFogDensity;
	std::optional<SColorf> SentFogColor;
};

class COGLES2MaterialSolidCB : public COGLES2MaterialBaseCB
{
public:
	COGLES2MaterialSolidCB();

	virtual void OnSetMaterial(const SMaterial& material);
	virtual void OnSetConstants(IMaterialRendererServices* services, s32 userData);

protected:
	bool FirstUpdate;

	s32 TMatrix0ID;
	s32 AlphaRefID;
	s32 TextureUsage0ID;
	s32 TextureUnit0ID;

	f32 AlphaRef;
	s32 TextureUsage0;
	s32 TextureUnit0;

	std::optional<core::matrix4> SentTMatrix0;
	std::optional<f32> SentAlphaRef;
	std::optional<s32> SentTextureUsage0, SentTextureUnit0;
};

class COGLES2MaterialSolid2CB : public COGLES2MaterialBaseCB
{
public:
	COGLES2MaterialSolid2CB();

	virtual void OnSetMaterial(const SMaterial& material);
	virtual void OnSetConstants(IMaterialRendererServices* services, s32 userData);

protected:
	bool FirstUpdate;

	s32 TMatrix0ID;
	s32 TMatrix1ID;
	s32 TextureUsage0ID;
	s32 TextureUsage1ID;
	s32 TextureUnit0ID;
	s32 TextureUnit1ID;

	s32 TextureUsage0;
	s32 TextureUsage1;
	s32 TextureUnit0;
	s32 TextureUnit1;
};

class COGLES2MaterialLightmapCB : public COGLES2MaterialBaseCB
{
public:
	COGLES2MaterialLightmapCB(float modulate);

	virtual void OnSetMaterial(const SMaterial& material);
	virtual void OnSetConstants(IMaterialRendererServices* services, s32 userData);

protected:
	bool FirstUpdate;

	s32 TMatrix0ID;
	s32 TMatrix1ID;
	s32 ModulateID;
	s32 TextureUsage0ID;
	s32 TextureUsage1ID;
	s32 TextureUnit0ID;
	s32 TextureUnit1ID;

	f32 Modulate;
	s32 TextureUsage0;
	s32 TextureUsage1;
	s32 TextureUnit0;
	s32 TextureUnit1;
};

class COGLES2MaterialReflectionCB : public COGLES2MaterialBaseCB
{
public:
	COGLES2MaterialReflectionCB();

	virtual void OnSetMaterial(const SMaterial& material);
	virtual void OnSetConstants(IMaterialRendererServices* services, s32 userData);

protected:
	bool FirstUpdate;

	s32 TMatrix0ID;
	s32 TextureUsage0ID;
	s32 TextureUsage1ID;
	s32 TextureUnit0ID;
	s32 TextureUnit1ID;

	s32 TextureUsage0;
	s32 TextureUsage1;
	s32 TextureUnit0;
	s32 TextureUnit1;
};

class COGLES2MaterialOneTextureBlendCB : public COGLES2MaterialBaseCB
{
public:
	COGLES2MaterialOneTextureBlendCB();

	virtual void OnSetMaterial(const SMaterial& material);
	virtual void OnSetConstants(IMaterialRendererServices* services, s32 userData);

protected:
	bool FirstUpdate;

	s32 TMatrix0ID;
	s32 BlendTypeID;
	s32 TextureUsage0ID;
	s32 TextureUnit0ID;

	s32 BlendType;
	s32 TextureUsage0;
	s32 TextureUnit0;
};

}
}

#endif
#endif

