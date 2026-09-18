// Copyright (C) 2026 Maksym H.
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in Irrlicht.h

#ifndef __C_METAL_DRIVER_H_INCLUDED__
#define __C_METAL_DRIVER_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_METAL_

#include "CDriverCommon.h"
#include "SIrrCreationParameters.h"

namespace irr
{
	class CIrrDeviceSDL;
}

namespace irr::video
{

class CMetalShaderRenderer;
struct SMetalShaderProgram;


class CMetalDriver : public CDriverCommon<CMetalShaderRenderer, SMetalShaderProgram>
{
	friend IVideoDriver* createMetalDriver(const SIrrlichtCreationParameters& params,
			io::IFileSystem* io, CIrrDeviceSDL* device);
	friend class CMetalShaderRenderer;

public:
	CMetalDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io);
	~CMetalDriver() IRR_OVERRIDE;

	bool beginScene(u16 clearFlag, SColor clearColor = SColor(255,0,0,0), f32 clearDepth = 1.f,
			u8 clearStencil = 0, const SExposedVideoData& videoData = SExposedVideoData(),
			core::rect<s32>* sourceRect = 0) IRR_OVERRIDE;
	bool endScene() IRR_OVERRIDE;

	bool checkDriverReset() IRR_OVERRIDE;

	void setViewPort(const core::rect<s32>& area, bool clipToRenderTarget = true) IRR_OVERRIDE;

	void drawVertexPrimitiveList(const void* vertices, u32 vertexCount,
			const void* indexList, u32 primitiveCount,
			E_VERTEX_TYPE vType = EVT_STANDARD,
			scene::E_PRIMITIVE_TYPE pType = scene::EPT_TRIANGLES,
			E_INDEX_TYPE iType = EIT_16BIT) IRR_OVERRIDE;

	void draw3DLine(const core::vector3df& start, const core::vector3df& end,
			SColor color = SColor(255, 255, 255, 255)) IRR_OVERRIDE;

	void clearBuffers(u16 flag, SColor color = SColor(255,0,0,0), f32 depth = 1.f,
			u8 stencil = 0) IRR_OVERRIDE;

	void OnResize(const core::dimension2d<u32>& size) IRR_OVERRIDE;

	ITexture* addRenderTargetTexture(const core::dimension2d<u32>& size,
			const io::path& name, const ECOLOR_FORMAT format = ECF_UNKNOWN) IRR_OVERRIDE;

	IRenderTarget* addRenderTarget() IRR_OVERRIDE;

	bool setRenderTargetEx(IRenderTarget* target, u16 clearFlag,
			SColor clearColor = SColor(255,0,0,0), f32 clearDepth = 1.f,
			u8 clearStencil = 0) IRR_OVERRIDE;

	u32 getGPUTimeMicros() const IRR_OVERRIDE;

#if 0
	IImage* createScreenShot(video::ECOLOR_FORMAT format = video::ECF_UNKNOWN,
			video::E_RENDER_TARGET target = video::ERT_FRAME_BUFFER) IRR_OVERRIDE;
#endif

	E_DRIVER_TYPE getDriverType() const IRR_OVERRIDE { return EDT_METAL; }
	core::dimension2du getMaxTextureSize() const IRR_OVERRIDE;

	bool addPrecompiledShader(const c8* name, const void* vertexProgram, u32 vertexSize,
			const void* fragmentProgram, u32 fragmentSize) IRR_OVERRIDE;

	//! The driver's Metal objects
	struct SImpl;

protected:
	ITexture* createDeviceDependentTexture(const io::path& name, IImage* image) IRR_OVERRIDE;

	SHWBufferLink* createHardwareBuffer(const scene::IMeshBuffer* mb) IRR_OVERRIDE;
	void drawHardwareBuffer(SHWBufferLink* buffer) IRR_OVERRIDE;
	void deleteHardwareBuffer(SHWBufferLink* buffer) IRR_OVERRIDE;

private:
	bool genericDriverInit(const SIrrlichtCreationParameters& params, CIrrDeviceSDL* device);

	//! Opens the command buffer and encoder, taking a drawable if needed; the pass starts with its
	//! depth cleared
	bool beginEncoding(bool clearColor, SColor color, f32 depth);
	void endEncoding();

	//! Matches the depth texture to the drawable size
	void updateDepthBuffer();

	//! Sets the encoder's viewport, leaving ViewPort as is
	void applyViewPort(const core::rect<s32>& area);

	bool bindMaterialPipeline(E_VERTEX_TYPE vType);

	//! 2D state: own projection, no fog, no depth test
	void bind2DState();

	//! Sends the matrices and fog for a 3D draw
	void bindDrawState();

	void drawQuads2D(const ITexture* texture, const S3DVertex* quads, u32 count,
			const core::rect<s32>* clip, bool alpha, bool alphaChannel) IRR_OVERRIDE;

	CMetalShaderRenderer* newShaderRenderer(u32 index, IShaderConstantSetCallBack* callback,
			E_MATERIAL_TYPE base, s32 userData) IRR_OVERRIDE;
	//! Whether the bound pipeline is the program's, which sends its own constants
	bool ProgramBound = false;

	SImpl* Impl;
	//! The encoder's viewport; a render target switch resets it
	core::rect<s32> ActiveViewPort;
};

} // end namespace irr::video

#endif // _IRR_COMPILE_WITH_METAL_
#endif
