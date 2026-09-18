// Copyright (C) 2026 Maksym H.
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in Irrlicht.h

#ifndef __C_VULKAN_DRIVER_H_INCLUDED__
#define __C_VULKAN_DRIVER_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_VULKAN_

#include "CDriverCommon.h"
#include "SIrrCreationParameters.h"

namespace irr
{
	class CIrrDeviceSDL;
}

namespace irr::video
{

class CVulkanShaderRenderer;
struct SVulkanShaderProgram;
class CVulkanTexture;


class CVulkanDriver : public CDriverCommon<CVulkanShaderRenderer, SVulkanShaderProgram>
{
	friend IVideoDriver* createVulkanDriver(const SIrrlichtCreationParameters& params,
			io::IFileSystem* io, CIrrDeviceSDL* device);
	friend class CVulkanShaderRenderer;
	friend class CVulkanTexture;

public:
	CVulkanDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io,
			CIrrDeviceSDL* device);
	~CVulkanDriver() IRR_OVERRIDE;

	bool beginScene(u16 clearFlag, SColor clearColor = SColor(255,0,0,0), f32 clearDepth = 1.f,
			u8 clearStencil = 0, const SExposedVideoData& videoData = SExposedVideoData(),
			core::rect<s32>* sourceRect = 0) IRR_OVERRIDE;
	bool endScene() IRR_OVERRIDE;

	void setMaterial(const SMaterial& material) IRR_OVERRIDE;
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
	void OnBackground() IRR_OVERRIDE;

	bool checkDriverReset() IRR_OVERRIDE;

#if 0
	//! Reads the frame under way
	IImage* createScreenShot(video::ECOLOR_FORMAT format = video::ECF_UNKNOWN,
			video::E_RENDER_TARGET target = video::ERT_FRAME_BUFFER) IRR_OVERRIDE;
#endif

	E_DRIVER_TYPE getDriverType() const IRR_OVERRIDE { return EDT_VULKAN; }
	core::dimension2du getMaxTextureSize() const IRR_OVERRIDE;

	bool addPrecompiledShaderLibrary(const void* data, u32 size) IRR_OVERRIDE;

	bool addPrecompiledShader(const c8* name, const void* vertexProgram, u32 vertexSize,
			const void* fragmentProgram, u32 fragmentSize) IRR_OVERRIDE;
	void setPrecompiledConstant(u32 id, bool value) IRR_OVERRIDE;
	void setPrecompiledConstant(u32 id, f32 value) IRR_OVERRIDE;
	void setPrecompiledConstant(u32 id, s32 value) IRR_OVERRIDE;

	struct SImpl;

protected:
	ITexture* createDeviceDependentTexture(const io::path& name, IImage* image) IRR_OVERRIDE;

	SHWBufferLink* createHardwareBuffer(const scene::IMeshBuffer* mb) IRR_OVERRIDE;
	void drawLink(SHWBufferLink* link, u32& bound) IRR_OVERRIDE;
	void deleteHardwareBuffer(SHWBufferLink* buffer) IRR_OVERRIDE;

private:
	bool genericDriverInit();

	void setGameConstant(u32 id, u32 bits);

	//! Opens the pass with depth cleared, acquiring a swapchain image if needed
	bool beginEncoding(bool clearColor, SColor color, f32 depth);

	bool bindMaterialPipeline(E_VERTEX_TYPE vType, u32 topology);

	//! Binds the 2D projection and an empty fog, false when out of memory
	bool bind2DState();

	//! Binds the matrices and fog of the current material, false when out of memory
	bool bindDrawState();

	void drawQuads2D(const ITexture* texture, const S3DVertex* quads, u32 count,
			const core::rect<s32>* clip, bool alpha, bool alphaChannel) IRR_OVERRIDE;

	CVulkanShaderRenderer* newShaderRenderer(u32 index, IShaderConstantSetCallBack* callback,
			E_MATERIAL_TYPE base, s32 userData) IRR_OVERRIDE;
	//! Whether the bound pipeline is the program's, which sends its own constants
	bool ProgramBound = false;

	SIrrlichtCreationParameters Params;
	CIrrDeviceSDL* SDLDevice;
	SImpl* Impl;
};

} // end namespace irr::video

#endif // _IRR_COMPILE_WITH_VULKAN_
#endif
