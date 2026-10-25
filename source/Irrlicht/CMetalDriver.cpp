// Copyright (C) 2026 Maksym H.
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in Irrlicht.h

#include "CMetalDriver.h"

#ifdef _IRR_COMPILE_WITH_METAL_

#include "CIrrDeviceSDL.h"
#include "CColorConverter.h"
#include "CImage.h"
#include "EVertexAttributes.h"
#include "IRenderTarget.h"
#include "IFileSystem.h"
#include "IReadFile.h"
#include "os.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

//! metal-cpp's implementation, in exactly 1 translation unit
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <TargetConditionals.h>

namespace irr::video
{

static const u32 METAL_FRAMES_IN_FLIGHT = 3;
//! Seconds a frame slot and a pipeline build may take; past them the GPU or its compiler is taken
//! as stopped for good
static const int64_t METAL_SLOT_SECONDS = 10;
static const int64_t METAL_PIPELINE_SECONDS = 30;


//! Bytes per ring block
static const u32 METAL_RING_BYTES = 2 << 20;
//! End of each block only 2D takes: the interface draws when the ring cannot grow
static const u32 METAL_RING_2D_BYTES = 512 << 10;

//! Smallest mesh pool; classes below it share one of this size
static const u32 METAL_POOL_FLOOR = 64 << 10;
//! Largest pool of up to 64 chunks: a class in little use leaves less of it unused
static const u32 METAL_POOL_CAP = 256 << 10;
//! Metal's alignment for vertex and index offsets
static const u32 METAL_STAGE_ALIGNMENT = 4;

//! Driver buffer slots, above those the game's programs use; the build puts the stages' uniform
//! blocks there
static const u32 METAL_VERTEX_INDEX = 30;
static const u32 METAL_UNIFORM_INDEX = 28;
static const u32 METAL_FOG_INDEX = 29;

//! The driver's stages as MSL in its shader folder: fixed vertex, fixed fragment, depth clear
static const c8* const METAL_DRIVER_STAGES[3] = { "vs_fixed3d.metal", "fs_fixed3d.metal",
		"vs_depth_ndc.metal" };


//! Apple GPUs take shared textures, other unified memory managed ones; a discrete GPU keeps
//! private ones, as managed ones there hold a second copy, and fills them through a buffer
static MTL::StorageMode textureStorage(MTL::Device* device)
{
	return !device->hasUnifiedMemory() ? MTL::StorageModePrivate :
			device->supportsFamily(MTL::GPUFamilyApple1) ? MTL::StorageModeShared :
			MTL::StorageModeManaged;
}

//! Apple GPUs before family 3 (A8) take 8192 a side, later ones and Macs 16384
static u32 maxTextureSide(MTL::Device* device)
{
	return device->supportsFamily(MTL::GPUFamilyApple3) ||
			device->supportsFamily(MTL::GPUFamilyMac1) ? 16384 : 8192;
}

static NS::SharedPtr<NS::AutoreleasePool> newAutoreleasePool()
{
	return NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
}


//! Textures whose mip levels the GPU builds before the next frame runs
using SMetalMipmaps = std::vector<NS::SharedPtr<MTL::Texture> >;


class CMetalTexture : public CDriverTexture
{
public:
	CMetalTexture(const io::path& name, IImage* image, MTL::CommandQueue* queue,
			ECOLOR_FORMAT format, bool keepImage, const u64* frames,
			SMetalMipmaps* mipmaps, std::vector<CMetalTexture*>* unmade)
		: CDriverTexture(name, EDT_METAL, image, format, keepImage), Frames(frames),
		Mipmaps(mipmaps), Queue(queue), Unmade(unmade)
	{
		HasMipMaps = Mipmaps && (Size.Width > 1 || Size.Height > 1);

		//! A texture of no size or larger than the device takes stays empty
		const u32 largest = maxTextureSide(queue->device());
		if (!Size.Width || !Size.Height || Size.Width > largest || Size.Height > largest)
			os::Printer::log(
					"Metal leaves empty a texture of no size or larger than the device takes",
					name.c_str(), ELL_ERROR);
		else if (!make())
		{
			//! Without memory the pixels wait for a frame that has it
			if (!Image)
			{
				Image = new CImage(ColorFormat, Size);
				image->copyTo(Image);
			}
			Keep = keepImage;
			Unmade->push_back(this);
			return;
		}
		upload((Image ? Image : image)->getData());
		settle(keepImage);
	}

	~CMetalTexture() IRR_OVERRIDE
	{
		if (Unmade)
			Unmade->erase(std::remove(Unmade->begin(), Unmade->end(), this), Unmade->end());
		//! Metal autoreleases on release, and the game drops textures outside a frame
		const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
		Texture.reset();
	}

	//! Makes the texture of one that waits for memory
	bool remake()
	{
		if (!make())
			return false;
		upload(Image->getData());
		settle(Keep);
		return true;
	}

	//! Render target texture for colour or depth, GPU only
	CMetalTexture(const io::path& name, const core::dimension2d<u32>& size, ECOLOR_FORMAT format,
			MTL::Device* device, bool memorylessDepth)
		: CDriverTexture(name, EDT_METAL)
	{
		//! Colour is always BGRA8 and depth always 32 bit float
		const bool depth = IImage::isDepthFormat(format);
		OriginalColorFormat = format;
		ColorFormat = depth ? format : ECF_A8R8G8B8;
		OriginalSize = size;
		Size = size;
		Pitch = Size.Width * IImage::getBitsPerPixelFromFormat(ColorFormat) / 8;
		IsRenderTarget = true;

		const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
		MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(depth ?
				MTL::PixelFormatDepth32Float : MTL::PixelFormatBGRA8Unorm, Size.Width, Size.Height,
				false);
		desc->setUsage(depth ? MTL::TextureUsageRenderTarget :
				MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
		//! Every pass clears depth and keeps none of it
		desc->setStorageMode(depth && memorylessDepth ? MTL::StorageModeMemoryless :
				MTL::StorageModePrivate);
		Texture = NS::TransferPtr(device->newTexture(desc));
	}

	MTL::Texture* handle() const { return Texture.get(); }

	//! The texture a draw of this frame reads
	MTL::Texture* drawn() const
	{
		if (Frames)
			DrawnAt = *Frames;
		return Texture.get();
	}

private:
	bool made() const IRR_OVERRIDE { return Texture.get(); }

	void read(IImage* into) IRR_OVERRIDE
	{
		const MTL::Region region = MTL::Region::Make2D(0, 0, Size.Width, Size.Height);
		if (Texture->storageMode() != MTL::StorageModePrivate)
		{
			Texture->getBytes(into->getData(), Pitch, region, 0);
			return;
		}

		//! Copied out after the GPU work queued so far
		const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
		const u32 bytes = Pitch * Size.Height;
		const NS::SharedPtr<MTL::Buffer> staged = NS::TransferPtr(
				Queue->device()->newBuffer(bytes, MTL::ResourceStorageModeShared));
		if (!staged)
			return;
		MTL::CommandBuffer* commands = Queue->commandBuffer();
		MTL::BlitCommandEncoder* blit = commands->blitCommandEncoder();
		blit->copyFromTexture(Texture.get(), 0, 0, region.origin, region.size,
				staged.get(), 0, Pitch, bytes);
		blit->endEncoding();
		commands->commit();
		commands->waitUntilCompleted();
		memcpy(into->getData(), staged->contents(), bytes);
	}

	void write(const IImage* pixels) IRR_OVERRIDE { upload(pixels->getData()); }

	bool make()
	{
		const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
		//! Same bit layouts; Metal names packed formats from the lowest bit
		const MTL::PixelFormat texels = ColorFormat == ECF_A1R5G5B5 ?
				MTL::PixelFormatBGR5A1Unorm : (ColorFormat == ECF_R5G6B5 ?
				MTL::PixelFormatB5G6R5Unorm : MTL::PixelFormatBGRA8Unorm);
		MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(texels,
				Size.Width, Size.Height, HasMipMaps);
		desc->setUsage(MTL::TextureUsageShaderRead);
		desc->setStorageMode(textureStorage(Queue->device()));
		Texture = NS::TransferPtr(Queue->device()->newTexture(desc));
		return Texture.get();
	}

	void upload(const void* pixels)
	{
		if (!Texture)
			return;

		const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
		//! A frame in flight may still read the texture: the pixels go to a new one
		if (DrawnAt && *Frames - DrawnAt < METAL_FRAMES_IN_FLIGHT)
		{
			MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
					Texture->pixelFormat(), Size.Width, Size.Height, HasMipMaps);
			desc->setUsage(Texture->usage());
			desc->setStorageMode(Texture->storageMode());
			//! Without memory the old texture stays and these pixels are dropped
			const NS::SharedPtr<MTL::Texture> fresh =
					NS::TransferPtr(Texture->device()->newTexture(desc));
			if (!fresh)
				return;
			Texture = fresh;
			DrawnAt = 0;
		}
		const MTL::Region region = MTL::Region::Make2D(0, 0, Size.Width, Size.Height);
		if (Texture->storageMode() != MTL::StorageModePrivate)
			Texture->replaceRegion(region, 0, pixels, Pitch);
		else
		{
			//! Queued now, so ahead of the frame and of the levels built from it
			const NS::SharedPtr<MTL::Buffer> staged = NS::TransferPtr(
					Queue->device()->newBuffer(pixels, Pitch * Size.Height,
					MTL::ResourceStorageModeShared));
			if (!staged)
				return;
			MTL::CommandBuffer* commands = Queue->commandBuffer();
			MTL::BlitCommandEncoder* blit = commands->blitCommandEncoder();
			blit->copyFromBuffer(staged.get(), 0, Pitch, Pitch * Size.Height, region.size,
					Texture.get(), 0, 0, region.origin);
			blit->endEncoding();
			commands->commit();
		}

		if (HasMipMaps && (Mipmaps->empty() || Mipmaps->back().get() != Texture.get()))
			Mipmaps->push_back(Texture);
	}

	NS::SharedPtr<MTL::Texture> Texture;
	//! The driver's frame counter, and the frame that last drew the texture
	const u64* Frames = nullptr;
	mutable u64 DrawnAt = 0;
	//! Where the texture waits for its levels, if it has them
	SMetalMipmaps* Mipmaps = nullptr;
	//! The driver's queue, which also fills and reads a texture the processor can't see
	MTL::CommandQueue* Queue = nullptr;
	//! The driver's waiting textures, and whether the game keeps this one's image
	std::vector<CMetalTexture*>* Unmade = nullptr;
	bool Keep = false;
};


class CMetalRenderTarget : public IRenderTarget
{
public:
	CMetalRenderTarget(const core::dimension2d<u32>& size) : Size(size) { DriverType = EDT_METAL; }

	~CMetalRenderTarget() IRR_OVERRIDE { setTextures(0, 0, 0, 0, 0); }

	void setTextures(ITexture* const* textures, u32 numTextures, ITexture* depthStencil,
			const E_CUBE_SURFACE*, u32) IRR_OVERRIDE
	{
		ITexture* color = numTextures && textures[0] &&
				textures[0]->getDriverType() == EDT_METAL ? textures[0] : 0;
		ITexture* depth = depthStencil && depthStencil->getDriverType() == EDT_METAL ?
				depthStencil : 0;
		//! Grab before drop: the new ones may be the old
		if (color)
			color->grab();
		if (depth)
			depth->grab();
		if (Textures.size() && Textures[0])
			Textures[0]->drop();
		if (DepthStencil)
			DepthStencil->drop();

		Textures.set_used(color ? 1 : 0);
		if (color)
		{
			Textures[0] = color;
			Size = color->getSize();
		}
		DepthStencil = depth;
	}

	const core::dimension2d<u32>& getSize() const { return Size; }

	MTL::Texture* color() const
	{ return Textures.size() ? static_cast<CMetalTexture*>(Textures[0])->handle() : nullptr; }

	MTL::Texture* depth() const
	{ return DepthStencil ? static_cast<CMetalTexture*>(DepthStencil)->handle() : nullptr; }

private:
	core::dimension2d<u32> Size;
};


//! Memory of the mesh buffers: a Metal buffer per pool and per unshared mesh
struct SMetalMeshStore
{
	using SRange = NS::SharedPtr<MTL::Buffer>;
	using SHandle = NS::SharedPtr<MTL::Buffer>;
	static const u32 Kinds = 1;
	static const u32 Frames = METAL_FRAMES_IN_FLIGHT;
	//! Most bytes one draining pass moves
	static const u32 DrainBytes = 8 << 20;

	static u32 kindOf(u32) { return 0; }
	static u32 chunkBytes(u32 size, u32) { return DRIVER_SHARED_SIZES[size]; }
	//! A chunk above 64 KB fits its data: its pool holds only it
	static u32 exactBytes(u32 size, u32 bytes)
	{
		return DRIVER_SHARED_SIZES[size] > 65536 ? (bytes + 255) / 256 * 256 : 0;
	}
	static u32 poolBytes(u32 size, u32)
	{
		const u32 chunk = DRIVER_SHARED_SIZES[size];
		return std::max(chunk * std::clamp(METAL_POOL_CAP / chunk, 1u, 64u), METAL_POOL_FLOOR);
	}
	static u8* mapped(const SRange& range) { return (u8*)range->contents(); }
	static SHandle handle(const SRange& range) { return range; }
	static u32 fullness(const SRange&) { return 0; }
	static void free(const SRange&, u32) {}
	static bool drain() { return false; }
	//! Frames in flight keep the buffer: their command buffers hold what they use
	static void retire(SHandle& buffer) { buffer.reset(); }

	void written(const SRange& range, u32 at, u32 bytes);

	bool make(SRange& range, u32 bytes);
	bool fillOwn(SHandle& buffer, u64 drawnAt, const void* data, u32 bytes);

	CMetalDriver::SImpl* Impl = nullptr;
};


//! A program constant or texture, found by pipeline reflection
struct SMetalUniform
{
	core::stringc Name;
	u32 Index = 0;
	//! Bytes the function declares
	u32 Size = 0;
	u32 Offset = 0;
	bool Fragment = false;
	bool Texture = false;
	//! Next slot with the same name, -1 for none
	s32 Next = -1;
};


//! What the compiler's queue builds; outlives a wait given up on, which the compiler may still
//! answer
template <typename T>
struct SMetalBuilt
{
	dispatch_semaphore_t Done = dispatch_semaphore_create(0);
	T* Result = nullptr;
	MTL::RenderPipelineReflection* Reflection = nullptr;
	NS::Error* Error = nullptr;
	//! Whether the main thread saw it done
	bool Ready = false;

	~SMetalBuilt()
	{
		if (Result)
			Result->release();
		if (Reflection)
			Reflection->release();
		if (Error)
			Error->release();
		dispatch_release(Done);
	}
};
//! A stage's library, built from its MSL
using SMetalStage = SMetalBuilt<MTL::Library>;


struct SMetalShaderProgram
{
	core::stringc Name;
	//! Taken from the stages at first use
	NS::SharedPtr<MTL::Function> Vertex;
	NS::SharedPtr<MTL::Function> Fragment;
	std::shared_ptr<SMetalStage> VertexStage;
	std::shared_ptr<SMetalStage> FragmentStage;
	core::array<SMetalUniform> Uniforms;
	u32 ValueBytes = 0;
	//! Whether Uniforms were filled by reflection
	bool Reflected = false;
	//! By blend, vertex type and colour mask, shared by every material of the program
	NS::SharedPtr<MTL::RenderPipelineState> Pipelines[2][EVT_STANDARD_PACKED + 1][ECP_ALL + 1];
};


struct CMetalDriver::SImpl
{
	NS::SharedPtr<MTL::Device> Device;
	NS::SharedPtr<MTL::CommandQueue> Queue;
	NS::SharedPtr<CA::MetalLayer> Layer;

	std::shared_ptr<SMetalStage> DriverStages[3];
	//! Stages by their source, each built once
	std::unordered_map<std::string, std::shared_ptr<SMetalStage>> Stages;
	//! Vertex layouts of the driver's and the game's programs
	NS::SharedPtr<MTL::VertexDescriptor> Layouts[EVT_STANDARD_PACKED + 1];

	//! By colour mask, blend, vType, alphaSource, texture and alpha test
	std::unordered_map<u32, NS::SharedPtr<MTL::RenderPipelineState>> Pipelines;
	//! The game's function constants of the precompiled programs, from 16, and their values
	struct SGameConstant
	{
		u32 Bits;
		u32 Type;
	};
	core::array<SGameConstant> GameConstants;
	NS::SharedPtr<MTL::FunctionConstantValues> GameConstantValues;

	//! A sampler per set of layer settings in use
	struct SSampler
	{
		u32 Key = 0;
		NS::SharedPtr<MTL::SamplerState> State;
	};
	core::array<SSampler> Samplers;
	//! Bound for a program's layer with no texture
	NS::SharedPtr<MTL::Texture> Black;

	//! Apple GPU: 16 bit textures and memoryless depth
	bool PackedTextures = false;

	NS::SharedPtr<MTL::RenderPipelineState> DepthQuad;
	//! By comparison function and depth write
	NS::SharedPtr<MTL::DepthStencilState> DepthStates[ECFN_NEVER + 1][2];

	NS::SharedPtr<MTL::Texture> Depth;

	NS::SharedPtr<MTL::CommandBuffer> Commands;
	NS::SharedPtr<MTL::RenderCommandEncoder> Encoder;
	NS::SharedPtr<CA::MetalDrawable> Drawable;

#if 0
	//! Last presented frame, for screenshots
	NS::SharedPtr<MTL::Texture> Presented;
	bool Readable = false;
	NS::SharedPtr<MTL::CommandBuffer> Submitted;
#endif

	//! Per-frame autorelease pool; the game loop has none
	NS::SharedPtr<NS::AutoreleasePool> Pool;

	struct SRingBlock
	{
		NS::SharedPtr<MTL::Buffer> Buffer;
		u8* Mapped = nullptr;
	};

	std::vector<SRingBlock> Rings[METAL_FRAMES_IN_FLIGHT];

	using SMeshes = CDriverMeshBuffers<SMetalMeshStore, SHWBufferLink>;
	SMeshes Meshes;

	u64 FrameCounter = 0;
	//! Textures that found no memory; they try again each frame
	std::vector<CMetalTexture*> Unmade;
	//! Frame whose ring or mesh memory was refused; it asks no more
	u64 RingRefused = ~0ull, MeshRefused = ~0ull;
	SMetalMipmaps Mipmaps;

	u32 RingBlock = 0;
	u32 RingUsed = 0;
	//! Furthest block each ring reached
	u32 RingReach[METAL_FRAMES_IN_FLIGHT] = { 0 };
	u32 RingIndex = 0;
	dispatch_semaphore_t Frames = nullptr;

	//! Whether a frame holds a slot in flight
	bool Started = false;

	MTL::RenderPipelineState* BoundPipeline = nullptr;
	//! Renderer whose constants the encoder holds
	CMetalShaderRenderer* BoundConstants = 0;
	MTL::DepthStencilState* BoundDepth = nullptr;
	MTL::Texture* BoundTexture = nullptr;
	MTL::SamplerState* BoundSampler = nullptr;
	MTL::Buffer* BoundVertices = nullptr;
	u32 BoundVertexOffset = 0;

	//! Whether the 2D matrices and empty fog are bound
	bool TwoDStanding = false;
	MTL::ScissorRect BoundScissor = { 0, 0, 0, 0 };
	MTL::Viewport BoundViewport = { 0, 0, 0, 0, 0, 0 };

	//! GPU time of the last finished frame
	std::atomic<u32> GpuMicros{0};
	//! MTLCommandBufferError of a failed frame, for the main thread to count
	std::atomic<s32> GpuError{0};
	//! Real time since which frames keep failing on the GPU and of the last failure; 5 s of it
	//! is taken as a lost device, which nothing draws on again
	u32 GpuFailingSince = 0;
	u32 GpuFailed = 0;
	//! Waits for a frame slot that ran out in a row; 2 take the device as lost
	u32 SlotTimeouts = 0;
	bool Lost = false;

	std::optional<MTL::CullMode> BoundCull;
	std::optional<MTL::TriangleFillMode> BoundFill;
	std::optional<SDriverFog> Fog;

	//! Slots whose pipeline the device refused, so it is not asked again
	std::vector<const void*> Broken;

	//! Updates a bind mark, true on change; marks cover slot 0 only
	template <class T, class V>
	static bool changed(T& mark, V value)
	{
		if (mark == value)
			return false;
		mark = value;
		return true;
	}

	void bindPipeline(MTL::RenderPipelineState* state)
	{
		if (changed(BoundPipeline, state))
			Encoder->setRenderPipelineState(state);
	}

	void bindTexture(MTL::Texture* texture)
	{
		//! An empty texture reads as opaque black, as an incomplete one in OpenGL
		if (!texture)
			texture = Black.get();
		if (changed(BoundTexture, texture))
			Encoder->setFragmentTexture(texture, (NS::UInteger)0);
	}

	void bindSampler(MTL::SamplerState* sampler)
	{
		if (changed(BoundSampler, sampler))
			Encoder->setFragmentSamplerState(sampler, (NS::UInteger)0);
	}

	void bindDepth(MTL::DepthStencilState* state)
	{
		if (changed(BoundDepth, state))
			Encoder->setDepthStencilState(state);
	}

	void bindCull(MTL::CullMode mode)
	{
		if (changed(BoundCull, mode))
			Encoder->setCullMode(mode);
	}

	void bindFill(MTL::TriangleFillMode mode)
	{
		if (changed(BoundFill, mode))
			Encoder->setTriangleFillMode(mode);
	}

	//! Quad index buffer, grown to the largest batch
	NS::SharedPtr<MTL::Buffer> QuadOrder;
	u32 QuadOrderQuads = 0;

	void forgetBindings()
	{
		//! Marks only, never dereferenced
		BoundPipeline = nullptr; BoundDepth = nullptr; BoundTexture = nullptr;
		BoundSampler = nullptr; BoundVertices = nullptr;
		BoundCull.reset(); BoundFill.reset(); Fog.reset(); TwoDStanding = false;
		BoundScissor.x = BoundScissor.y = BoundScissor.width = BoundScissor.height = 0;
		BoundViewport = MTL::Viewport{ 0, 0, 0, 0, 0, 0 };
		BoundConstants = 0;
	}

	~SImpl()
	{
		if (Frames)
			dispatch_release(Frames);
	}
};


//! Metal's write mask bits run in reverse of the colour planes
static MTL::ColorWriteMask writeMaskFor(u32 planes)
{
	MTL::ColorWriteMask mask = MTL::ColorWriteMaskNone;
	if (planes & ECP_RED)   mask |= MTL::ColorWriteMaskRed;
	if (planes & ECP_GREEN) mask |= MTL::ColorWriteMaskGreen;
	if (planes & ECP_BLUE)  mask |= MTL::ColorWriteMaskBlue;
	if (planes & ECP_ALPHA) mask |= MTL::ColorWriteMaskAlpha;
	return mask;
}


//! Factors past EBF_SRC_ALPHA_SATURATE read as zero, as OpenGL's
static const MTL::BlendFactor METAL_BLEND_FACTORS[16] = {
	MTL::BlendFactorZero,                     // EBF_ZERO
	MTL::BlendFactorOne,                      // EBF_ONE
	MTL::BlendFactorDestinationColor,         // EBF_DST_COLOR
	MTL::BlendFactorOneMinusDestinationColor, // EBF_ONE_MINUS_DST_COLOR
	MTL::BlendFactorSourceColor,              // EBF_SRC_COLOR
	MTL::BlendFactorOneMinusSourceColor,      // EBF_ONE_MINUS_SRC_COLOR
	MTL::BlendFactorSourceAlpha,              // EBF_SRC_ALPHA
	MTL::BlendFactorOneMinusSourceAlpha,      // EBF_ONE_MINUS_SRC_ALPHA
	MTL::BlendFactorDestinationAlpha,         // EBF_DST_ALPHA
	MTL::BlendFactorOneMinusDestinationAlpha, // EBF_ONE_MINUS_DST_ALPHA
	MTL::BlendFactorSourceAlphaSaturated,     // EBF_SRC_ALPHA_SATURATE
};

//! Metal has no factor or alpha variants of min and max, as OpenGL without their extensions
static const MTL::BlendOperation METAL_BLEND_OPERATIONS[EBO_MAX_ALPHA + 1] = {
	MTL::BlendOperationAdd,             // EBO_NONE
	MTL::BlendOperationAdd,             // EBO_ADD
	MTL::BlendOperationSubtract,        // EBO_SUBTRACT
	MTL::BlendOperationReverseSubtract, // EBO_REVSUBTRACT
	MTL::BlendOperationMin,             // EBO_MIN
	MTL::BlendOperationMax,             // EBO_MAX
	MTL::BlendOperationMin,             // EBO_MIN_FACTOR
	MTL::BlendOperationMax,             // EBO_MAX_FACTOR
	MTL::BlendOperationMin,             // EBO_MIN_ALPHA
	MTL::BlendOperationMax,             // EBO_MAX_ALPHA
};


static NS::SharedPtr<MTL::RenderPipelineDescriptor> describePipeline(MTL::Function* vertex,
		MTL::Function* fragment, MTL::VertexDescriptor* layout, u32 blend, u32 planes)
{
	NS::SharedPtr<MTL::RenderPipelineDescriptor> desc = NS::TransferPtr(
			MTL::RenderPipelineDescriptor::alloc()->init());
	desc->setVertexFunction(vertex);
	desc->setFragmentFunction(fragment);
	desc->setVertexDescriptor(layout);
	desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

	MTL::RenderPipelineColorAttachmentDescriptor* color = desc->colorAttachments()->object(0);
	color->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
	color->setWriteMask(writeMaskFor(planes));
	if (blend)
	{
		color->setBlendingEnabled(true);
		color->setRgbBlendOperation(METAL_BLEND_OPERATIONS[blend >> 16]);
		color->setAlphaBlendOperation(METAL_BLEND_OPERATIONS[blend >> 16]);
		color->setSourceRGBBlendFactor(METAL_BLEND_FACTORS[blend >> 4 & 15]);
		color->setSourceAlphaBlendFactor(METAL_BLEND_FACTORS[blend >> 12 & 15]);
		color->setDestinationRGBBlendFactor(METAL_BLEND_FACTORS[blend & 15]);
		color->setDestinationAlphaBlendFactor(METAL_BLEND_FACTORS[blend >> 8 & 15]);
	}
	return desc;
}


static void loseDevice(CMetalDriver::SImpl* impl, const char* at)
{
	if (impl->Lost)
		return;
	impl->Lost = true;
	os::Printer::log("The Metal device was lost at", at, ELL_ERROR);
}


//! Waits up to the seconds the app is awake: a second that took far longer passed in the
//! background or asleep, where neither the GPU nor its compiler had to answer
static bool waitAwake(dispatch_semaphore_t done, int64_t seconds)
{
	for (int64_t awake = 0; awake < seconds;)
	{
		const u32 start = os::Timer::getRealTime();
		if (!dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)))
			return true;
		awake += os::Timer::getRealTime() - start < 2000;
	}
	return false;
}


//! Waits for a build METAL_PIPELINE_SECONDS at most; a compiler that never answers takes the
//! device as lost
template <typename T>
static bool awaitBuilt(CMetalDriver::SImpl* impl, SMetalBuilt<T>& build, const c8* what)
{
	if (build.Ready)
		return true;
	if (!waitAwake(build.Done, METAL_PIPELINE_SECONDS))
	{
		char at[96];
		snprintf(at, sizeof(at), "%s its compiler did not build in %lld s", what,
				(long long)METAL_PIPELINE_SECONDS);
		loseDevice(impl, at);
		return false;
	}
	build.Ready = true;
	return true;
}


static bool brokenSlot(CMetalDriver::SImpl* impl, const void* slot)
{
	return std::find(impl->Broken.begin(), impl->Broken.end(), slot) != impl->Broken.end();
}


static void refuseSlot(CMetalDriver::SImpl* impl, const void* slot, core::stringc what,
		NS::Error* error)
{
	impl->Broken.push_back(slot);
	what += ": ";
	what += error ? error->localizedDescription()->utf8String() : "no reason given";
	os::Printer::log("Could not build a Metal pipeline for", what.c_str(), ELL_ERROR);
}


//! Builds a pipeline on the compiler's queue and waits for it; error and reflection come
//! autoreleased
static NS::SharedPtr<MTL::RenderPipelineState> newPipeline(CMetalDriver::SImpl* impl,
		const MTL::RenderPipelineDescriptor* desc, NS::Error** error,
		MTL::RenderPipelineReflection** reflection = nullptr)
{
	*error = nullptr;
	if (impl->Lost)
		return {};

	const auto build = std::make_shared<SMetalBuilt<MTL::RenderPipelineState>>();
	impl->Device->newRenderPipelineState(desc, reflection ? MTL::PipelineOptionBindingInfo :
			MTL::PipelineOptionNone, [build](MTL::RenderPipelineState* state,
			MTL::RenderPipelineReflection* reflected, NS::Error* failure)
	{
		build->Result = state ? state->retain() : nullptr;
		build->Reflection = reflected ? reflected->retain() : nullptr;
		build->Error = failure ? failure->retain() : nullptr;
		dispatch_semaphore_signal(build->Done);
	});
	if (!awaitBuilt(impl, *build, "a pipeline"))
		return {};

	if (build->Error)
		*error = build->Error->retain()->autorelease();
	if (reflection && build->Reflection)
		*reflection = build->Reflection->retain()->autorelease();
	if (!build->Result)
		return {};
	return NS::RetainPtr(build->Result);
}


//! Starts building a stage from its MSL, once per source
static std::shared_ptr<SMetalStage> buildStage(CMetalDriver::SImpl* impl, const std::string& source)
{
	std::shared_ptr<SMetalStage>& stage = impl->Stages[source];
	if (stage)
		return stage;

	const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
	stage = std::make_shared<SMetalStage>();
	const NS::SharedPtr<MTL::CompileOptions> options = NS::TransferPtr(
			MTL::CompileOptions::alloc()->init());
	options->setLanguageVersion(MTL::LanguageVersion2_1);
	const std::shared_ptr<SMetalStage> build = stage;
	impl->Device->newLibrary(NS::String::string(source.c_str(), NS::UTF8StringEncoding),
			options.get(), [build](MTL::Library* library, NS::Error* error)
	{
		build->Result = library ? library->retain() : nullptr;
		build->Error = error ? error->retain() : nullptr;
		dispatch_semaphore_signal(build->Done);
	});
	return stage;
}


//! The entry function of a stage, once its library is built
static NS::SharedPtr<MTL::Function> stageFunction(CMetalDriver::SImpl* impl, SMetalStage* stage,
		MTL::FunctionConstantValues* constants, NS::Error** error)
{
	*error = nullptr;
	if (!stage || impl->Lost || !awaitBuilt(impl, *stage, "a shader library"))
		return {};
	if (!stage->Result)
	{
		*error = stage->Error;
		return {};
	}

	const NS::Array* names = stage->Result->functionNames();
	if (!names || !names->count())
		return {};
	const NS::String* name = static_cast<const NS::String*>(names->object(0));
	return NS::TransferPtr(constants ? stage->Result->newFunction(name, constants, error) :
			stage->Result->newFunction(name));
}


static std::shared_ptr<SMetalStage> openDriverStage(CMetalDriver::SImpl* impl, io::IFileSystem* fs,
		const io::path& folder, const c8* name)
{
	core::array<c8> bytes;
	if (!readWhole(fs, folder + name, bytes))
	{
		os::Printer::log("Could not read the Metal stage", (folder + name).c_str(), ELL_ERROR);
		return {};
	}
	return buildStage(impl, std::string(bytes.const_pointer(), bytes.size()));
}


//! The fixed pipeline for a state, built on first use
static MTL::RenderPipelineState* pipelineFor(CMetalDriver::SImpl* impl, bool textured,
		bool alphaTest, u32 blend, E_VERTEX_TYPE vType, u32 alphaSource = 0, u32 planes = ECP_ALL)
{
	if ((u32)vType > (u32)EVT_STANDARD_PACKED || blend >> 16 > (u32)EBO_MAX_ALPHA || alphaSource > 2)
		return nullptr;

	NS::SharedPtr<MTL::RenderPipelineState>& slot = impl->Pipelines
			[planes << 28 | blend << 8 | (u32)vType << 4 | alphaSource << 2 |
					(textured ? 2u : 0u) | (alphaTest ? 1u : 0u)];
	if (slot || brokenSlot(impl, &slot))
		return slot.get();

	const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();

	const NS::SharedPtr<MTL::FunctionConstantValues> constants = NS::TransferPtr(
			MTL::FunctionConstantValues::alloc()->init());
	constants->setConstantValue(&textured, MTL::DataTypeBool, (NS::UInteger)0);
	constants->setConstantValue(&alphaTest, MTL::DataTypeBool, (NS::UInteger)1);
	constants->setConstantValue(&alphaSource, MTL::DataTypeInt, (NS::UInteger)3);

	NS::Error* failure = nullptr;
	const NS::SharedPtr<MTL::Function> fragment = stageFunction(impl,
			impl->DriverStages[1].get(), constants.get(), &failure);
	NS::SharedPtr<MTL::Function> vertex;
	if (fragment)
		vertex = stageFunction(impl, impl->DriverStages[0].get(), constants.get(), &failure);
	if (vertex)
	{
		const NS::SharedPtr<MTL::RenderPipelineDescriptor> desc = describePipeline(vertex.get(),
				fragment.get(), impl->Layouts[vType].get(), blend, planes);
		slot = newPipeline(impl, desc.get(), &failure);
	}

	if (!slot)
		refuseSlot(impl, &slot, "the fixed pipeline", failure);
	return slot.get();
}


static MTL::SamplerState* samplerFor(CMetalDriver::SImpl* impl, const SMaterialLayer& layer,
		bool mipmaps)
{
	const u32 key = (layer.BilinearFilter ? 1 : 0) | (layer.TrilinearFilter ? 2 : 0) |
			(layer.AnisotropicFilter ? 4 : 0) |
			((u32)layer.TextureWrapU << 3) | ((u32)layer.TextureWrapV << 7) |
			(mipmaps ? 1 << 11 : 0);
	for (u32 i = 0; i < impl->Samplers.size(); ++i)
		if (impl->Samplers[i].Key == key)
			return impl->Samplers[i].State.get();

	const NS::SharedPtr<MTL::SamplerDescriptor> desc = NS::TransferPtr(
			MTL::SamplerDescriptor::alloc()->init());
	const bool smooth = layer.BilinearFilter || layer.TrilinearFilter;
	desc->setMinFilter(smooth ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
	desc->setMagFilter(desc->minFilter());
	//! A material without mipmaps reads level 0 of any texture
	desc->setMipFilter(!mipmaps ? MTL::SamplerMipFilterNotMipmapped :
			layer.TrilinearFilter ? MTL::SamplerMipFilterLinear : MTL::SamplerMipFilterNearest);
	desc->setMaxAnisotropy(layer.AnisotropicFilter ?
			core::clamp((u32)layer.AnisotropicFilter, 1u, 16u) : 1);

	//! Repeat, or clamp to edge for any other mode
	desc->setSAddressMode(layer.TextureWrapU == ETC_REPEAT ?
			MTL::SamplerAddressModeRepeat : MTL::SamplerAddressModeClampToEdge);
	desc->setTAddressMode(layer.TextureWrapV == ETC_REPEAT ?
			MTL::SamplerAddressModeRepeat : MTL::SamplerAddressModeClampToEdge);

	const NS::SharedPtr<MTL::SamplerState> state = NS::TransferPtr(
			impl->Device->newSamplerState(desc.get()));
	if (!state)
		return nullptr;

	impl->Samplers.push_back({ key, state });
	return state.get();
}


CMetalDriver::CMetalDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io)
	: CDriverCommon(io, params.WindowSize), Impl(new SImpl())
{
	Impl->Meshes.Store.Impl = Impl;
	#ifdef _DEBUG
	setDebugName("CMetalDriver");
	#endif
}


CMetalDriver::~CMetalDriver()
{
	const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
	if (Impl->Started)
		endScene();

#if 0
	//! Waits for the last frame
	if (Impl->Submitted)
		Impl->Submitted->waitUntilCompleted();
#endif

	//! Waits out all frames, then restores the count libdispatch expects; frames a stopped GPU
	//! keeps leave the semaphore to leak, as releasing it below that count aborts
	if (Impl->Frames)
	{
		const dispatch_time_t until = Impl->Lost ? DISPATCH_TIME_NOW :
				dispatch_time(DISPATCH_TIME_NOW, METAL_SLOT_SECONDS * NSEC_PER_SEC);
		u32 back = 0;
		while (back < METAL_FRAMES_IN_FLIGHT && !dispatch_semaphore_wait(Impl->Frames, until))
			++back;
		for (u32 f = 0; f < back; ++f)
			dispatch_semaphore_signal(Impl->Frames);
		if (back < METAL_FRAMES_IN_FLIGHT)
			Impl->Frames = nullptr;
	}

	//! Freed before Impl, which mesh buffers refer to
	removeEveryHardwareBuffer();
	deleteMaterialRenders();
	deleteAllTextures();

	for (u32 i = 0; i < Programs.size(); ++i)
		delete Programs[i];

	delete Impl;
}


bool CMetalDriver::genericDriverInit(const SIrrlichtCreationParameters& params,
		CIrrDeviceSDL* device)
{
	const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
	Impl->Layer = NS::RetainPtr((CA::MetalLayer*)(device ? device->getDrawTarget() : 0));
	if (!Impl->Layer)
	{
		os::Printer::log("The device made no Metal layer to draw into", ELL_ERROR);
		return false;
	}

	Impl->Device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
	if (!Impl->Device)
	{
		os::Printer::log("No Metal device on this machine", ELL_ERROR);
		return false;
	}

	Impl->Queue = NS::TransferPtr(Impl->Device->newCommandQueue());

	//! The simulator answers as an Apple GPU but takes neither 16 bit textures nor memoryless depth
	Impl->PackedTextures = Impl->Device->supportsFamily(MTL::GPUFamilyApple1) &&
			!TARGET_OS_SIMULATOR;
	Impl->Frames = dispatch_semaphore_create(METAL_FRAMES_IN_FLIGHT);

	Impl->Layer->setDevice(Impl->Device.get());
	Impl->Layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
#if !defined(_IRR_IOS_PLATFORM_)
#if 0
	//! Lets screenshots read the drawable back
	Impl->Readable = true;
	Impl->Layer->setFramebufferOnly(false);
#endif
	//! iOS always syncs to the display
	Impl->Layer->setDisplaySyncEnabled(params.Vsync);
#endif

	NS::Error* error = nullptr;
	//! The driver's stages build while the rest starts
	for (u32 s = 0; s < 3; ++s)
		if (!(Impl->DriverStages[s] = openDriverStage(Impl, FileSystem, params.OGLES2ShaderPath,
				METAL_DRIVER_STAGES[s])))
			return false;

	//! Attribute offsets per vertex type; missing ones alias others
	static const struct { MTL::VertexFormat Format; u32 Offset[EVT_TANGENTS +
			1]; } attributes[EVA_COUNT] = {
		{ MTL::VertexFormatFloat3, { 0, 0, 0 } },                    // position
		{ MTL::VertexFormatFloat3, { 12, 12, 12 } },                 // normal
		//! Raw bytes; the shaders swizzle them to RGBA
		{ MTL::VertexFormatUChar4Normalized, { 24, 24, 24 } },       // colour
		{ MTL::VertexFormatFloat2, { 28, 28, 28 } },                 // texture coordinate
		{ MTL::VertexFormatFloat2, { 28, 36, 28 } },                 // second texture coordinate
		{ MTL::VertexFormatFloat3, { 12, 12, 36 } },                 // tangent
		{ MTL::VertexFormatFloat3, { 12, 12, 48 } },                 // binormal
	};
	for (u32 i = 0; i <= (u32)EVT_TANGENTS; ++i)
	{
		Impl->Layouts[i] = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
		for (u32 a = 0; a < EVA_COUNT; ++a)
		{
			MTL::VertexAttributeDescriptor* attribute = Impl->Layouts[i]->attributes()->object(a);
			attribute->setFormat(attributes[a].Format);
			attribute->setOffset(attributes[a].Offset[i]);
			attribute->setBufferIndex(METAL_VERTEX_INDEX);
		}
		Impl->Layouts[i]->layouts()->object(METAL_VERTEX_INDEX)->setStride(
				getVertexPitchFromType((E_VERTEX_TYPE)i));
	}
	//! The packed standard vertex; missing attributes alias as above
	static const struct { MTL::VertexFormat Format; u32 Offset; } packed[EVA_COUNT] = {
		{ MTL::VertexFormatFloat3, 0 },
		{ MTL::VertexFormatChar3Normalized, 12 },
		{ MTL::VertexFormatUChar4Normalized, 16 },
		{ MTL::VertexFormatFloat2, 20 },
		{ MTL::VertexFormatFloat2, 20 },
		{ MTL::VertexFormatFloat3, 0 },
		{ MTL::VertexFormatFloat3, 0 },
	};
	Impl->Layouts[EVT_STANDARD_PACKED] = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
	for (u32 a = 0; a < EVA_COUNT; ++a)
	{
		MTL::VertexAttributeDescriptor* attribute =
				Impl->Layouts[EVT_STANDARD_PACKED]->attributes()->object(a);
		attribute->setFormat(packed[a].Format);
		attribute->setOffset(packed[a].Offset);
		attribute->setBufferIndex(METAL_VERTEX_INDEX);
	}
	Impl->Layouts[EVT_STANDARD_PACKED]->layouts()->object(METAL_VERTEX_INDEX)->setStride(
			sizeof(S3DVertexPacked));

	MTL::TextureDescriptor* black = MTL::TextureDescriptor::texture2DDescriptor(
			MTL::PixelFormatBGRA8Unorm, 1, 1, false);
	black->setUsage(MTL::TextureUsageShaderRead);
	Impl->Black = NS::TransferPtr(Impl->Device->newTexture(black));
	if (!Impl->Black)
		return false;
	const u8 opaqueBlack[4] = { 0, 0, 0, 255 };
	Impl->Black->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, opaqueBlack, 4);

	//! The first block of each ring comes before any texture, so 2D always has room
	for (std::vector<SImpl::SRingBlock>& ring : Impl->Rings)
		if (NS::SharedPtr<MTL::Buffer> buffer = NS::TransferPtr(
				Impl->Device->newBuffer(METAL_RING_BYTES, MTL::ResourceStorageModeShared)))
			ring.push_back({ buffer, (u8*)buffer->contents() });

	if (!pipelineFor(Impl, false, false, 0, EVT_STANDARD))
		return false;

	//! Prebuilds the 2D, cut and blended pipelines
	pipelineFor(Impl, true, false, 0, EVT_STANDARD);
	pipelineFor(Impl, true, false, DRIVER_BLEND_ALPHA, EVT_STANDARD);
	pipelineFor(Impl, false, false, DRIVER_BLEND_ALPHA, EVT_STANDARD);
	pipelineFor(Impl, true, true, 0, EVT_STANDARD, 1);
	pipelineFor(Impl, true, true, DRIVER_BLEND_ALPHA, EVT_STANDARD, 1);
	pipelineFor(Impl, false, true, DRIVER_BLEND_ALPHA, EVT_STANDARD);

	//! ECFN_DISABLED always passes and is bound without depth writes
	static const MTL::CompareFunction compare[ECFN_NEVER + 1] = {
		MTL::CompareFunctionAlways,       // ECFN_DISABLED
		MTL::CompareFunctionLessEqual,    // ECFN_LESSEQUAL
		MTL::CompareFunctionEqual,        // ECFN_EQUAL
		MTL::CompareFunctionLess,         // ECFN_LESS
		MTL::CompareFunctionNotEqual,     // ECFN_NOTEQUAL
		MTL::CompareFunctionGreaterEqual, // ECFN_GREATEREQUAL
		MTL::CompareFunctionGreater,      // ECFN_GREATER
		MTL::CompareFunctionAlways,       // ECFN_ALWAYS
		MTL::CompareFunctionNever,        // ECFN_NEVER
	};

	const NS::SharedPtr<MTL::DepthStencilDescriptor> depth = NS::TransferPtr(
			MTL::DepthStencilDescriptor::alloc()->init());
	for (u32 i = 0; i <= (u32)ECFN_NEVER; ++i)
		for (u32 w = 0; w < 2; ++w)
		{
			depth->setDepthCompareFunction(compare[i]);
			depth->setDepthWriteEnabled(w ? true : false);
			Impl->DepthStates[i][w] = NS::TransferPtr(Impl->Device->newDepthStencilState(
					depth.get()));
		}

	const NS::SharedPtr<MTL::Function> depthVertex = stageFunction(Impl,
			Impl->DriverStages[2].get(), nullptr, &error);
	if (depthVertex)
	{
		const NS::SharedPtr<MTL::RenderPipelineDescriptor> clearPass = describePipeline(
				depthVertex.get(), nullptr, nullptr, 0, ECP_NONE);
		Impl->DepthQuad = newPipeline(Impl, clearPass.get(), &error);
	}
	if (!Impl->DepthQuad)
	{
		os::Printer::log("Could not build the Metal depth clear pipeline",
				error ? error->localizedDescription()->utf8String() : "", ELL_ERROR);
		return false;
	}

	updateDepthBuffer();

	Name = L"Metal ";
	Name += core::stringw(Impl->Device->name()->utf8String());

	//! Exposes the SDL window to the game
	ExposedData.OpenGLSDL.Window = device->getWindow();

	ActiveViewPort = ViewPort;

	os::Printer::log("Metal driver", core::stringc(Name.c_str()).c_str(), ELL_INFORMATION);
	return true;
}


void CMetalDriver::updateDepthBuffer()
{
	const CGSize size = Impl->Layer->drawableSize();
	if (size.width < 1 || size.height < 1)
		return;

	if (Impl->Depth && Impl->Depth->width() == (NS::UInteger)size.width &&
			Impl->Depth->height() == (NS::UInteger)size.height)
		return;

	const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
	MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
			MTL::PixelFormatDepth32Float, (NS::UInteger)size.width, (NS::UInteger)size.height,
			false);
	desc->setUsage(MTL::TextureUsageRenderTarget);
	//! Depth lives in tile memory only on Apple GPUs
	desc->setStorageMode(Impl->PackedTextures ?
			MTL::StorageModeMemoryless : MTL::StorageModePrivate);
	Impl->Depth = NS::TransferPtr(Impl->Device->newTexture(desc));
}


//! Frees at most 1 unneeded ring block per frame
static void trimRing(CMetalDriver::SImpl* impl)
{
	std::vector<CMetalDriver::SImpl::SRingBlock>& blocks = impl->Rings[impl->RingIndex];
	if (blocks.size() > impl->RingReach[impl->RingIndex] + 1)
		blocks.pop_back();
	impl->RingReach[impl->RingIndex] = 0;
}


//! Builds the levels of the textures uploaded since the last frame, ahead of the frame on the queue
static void buildMipmaps(CMetalDriver::SImpl* impl)
{
	if (impl->Mipmaps.empty())
		return;

	MTL::CommandBuffer* commands = impl->Queue->commandBuffer();
	MTL::BlitCommandEncoder* blit = commands->blitCommandEncoder();
	for (const NS::SharedPtr<MTL::Texture>& texture : impl->Mipmaps)
		blit->generateMipmaps(texture.get());
	blit->endEncoding();
	commands->commit();
	impl->Mipmaps.clear();
}


bool CMetalDriver::checkDriverReset()
{
	return Impl->Lost;
}


bool CMetalDriver::beginScene(u16 clearFlag, SColor clearColor, f32 clearDepth, u8 clearStencil,
		const SExposedVideoData& videoData, core::rect<s32>* sourceRect)
{
	//! Closes a frame an exception left open
	if (Impl->Started)
		endScene();
	CNullDriver::beginScene(clearFlag, clearColor, clearDepth, clearStencil, videoData, sourceRect);

	if (Impl->Lost)
		return false;
	//! A slot that does not come back means a GPU that stopped
	if (!waitAwake(Impl->Frames, METAL_SLOT_SECONDS))
	{
		if (++Impl->SlotTimeouts >= 2)
		{
			char at[64];
			snprintf(at, sizeof(at), "frames the GPU did not finish in %lld s",
					(long long)METAL_SLOT_SECONDS * 2);
			loseDevice(Impl, at);
		}
		return false;
	}
	Impl->SlotTimeouts = 0;
	if (!Impl->Pool)
		Impl->Pool = newAutoreleasePool();
	Impl->Started = true;

	updateDepthBuffer();

	if (!beginEncoding((clearFlag & ECBF_COLOR) != 0, clearColor, clearDepth))
	{
		//! No drawable: gives the slot back and keeps RingIndex
		Impl->Started = false;
		Impl->Pool.reset();
		dispatch_semaphore_signal(Impl->Frames);
		return false;
	}

	//! Next ring slot; its last frame is done after the wait
	Impl->RingIndex = (Impl->RingIndex + 1) % METAL_FRAMES_IN_FLIGHT;
	Impl->Meshes.advance(Impl->RingIndex, Impl->FrameCounter, HWBufferList);
	trimRing(Impl);
	++Impl->FrameCounter;
	//! Textures without memory try again, once a frame
	std::vector<CMetalTexture*>& unmade = Impl->Unmade;
	while (!unmade.empty() && unmade.front()->remake())
		unmade.erase(unmade.begin());
	Impl->RingBlock = 0;
	Impl->RingUsed = 0;
	return true;
}


bool CMetalDriver::beginEncoding(bool clearColor, SColor color, f32 depth)
{
	if (Impl->Encoder)
		return true;

	if (!Impl->Commands)
		Impl->Commands = NS::RetainPtr(Impl->Queue->commandBuffer());

	MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
	if (CurrentRenderTarget)
	{
		const auto* texture = static_cast<const CMetalRenderTarget*>(CurrentRenderTarget);
		pass->colorAttachments()->object(0)->setTexture(texture->color());
		pass->depthAttachment()->setTexture(texture->depth());
	}
	else
	{
		if (!Impl->Drawable)
		{
			Impl->Drawable = NS::RetainPtr(Impl->Layer->nextDrawable());
			if (!Impl->Drawable)
				return false;
		}

		pass->colorAttachments()->object(0)->setTexture(Impl->Drawable->texture());
		pass->depthAttachment()->setTexture(Impl->Depth.get());
	}

	pass->colorAttachments()->object(0)->setLoadAction(clearColor ?
			MTL::LoadActionClear : MTL::LoadActionLoad);
	pass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
	pass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(color.getRed() / 255.0,
			color.getGreen() / 255.0, color.getBlue() / 255.0, color.getAlpha() / 255.0));
	//! Depth is always cleared: memoryless has nothing to load
	pass->depthAttachment()->setLoadAction(MTL::LoadActionClear);
	pass->depthAttachment()->setStoreAction(MTL::StoreActionDontCare);
	pass->depthAttachment()->setClearDepth((double)depth);

	Impl->Encoder = NS::RetainPtr(Impl->Commands->renderCommandEncoder(pass));
	Impl->forgetBindings();

	//! Irrlicht's front faces are clockwise
	Impl->Encoder->setFrontFacingWinding(MTL::WindingClockwise);
	applyViewPort(ActiveViewPort);
	return true;
}


void CMetalDriver::endEncoding()
{
	if (Impl->Encoder)
	{
		Impl->Encoder->endEncoding();
		Impl->Encoder.reset();
	}
}


bool CMetalDriver::endScene()
{
	CNullDriver::endScene();
	endEncoding();

	//! Releases what a failed beginScene left
	if (!Impl->Started)
	{
		Impl->Commands.reset();
		Impl->Drawable.reset();
		return false;
	}

	Impl->Started = false;
	if (!Impl->Commands)
	{
		Impl->Pool.reset();
		dispatch_semaphore_signal(Impl->Frames);
		return true;
	}

#if 0
	if (Impl->Drawable)
	{
		if (Impl->Readable)
			Impl->Presented = NS::RetainPtr(Impl->Drawable->texture());
		Impl->Commands->presentDrawable(Impl->Drawable.get());
	}
#endif
	if (Impl->Drawable)
		Impl->Commands->presentDrawable(Impl->Drawable.get());

	if (const s32 error = Impl->GpuError.exchange(0, std::memory_order_relaxed))
	{
		//! A frame sent from the background fails by design
		if (error != (s32)MTL::CommandBufferErrorNotPermitted)
		{
			const u32 now = os::Timer::getRealTime();
			if (!Impl->GpuFailingSince || now - Impl->GpuFailed > 1000)
				Impl->GpuFailingSince = now;
			Impl->GpuFailed = now;
			if (now - Impl->GpuFailingSince >= 5000)
				loseDevice(Impl, "frames failing on the GPU for 5 s");
		}
	}

	SImpl* impl = Impl;
	Impl->Commands->addCompletedHandler(^(MTL::CommandBuffer* buffer)
	{
		if (buffer->status() == MTL::CommandBufferStatusError)
			impl->GpuError.store(buffer->error() ? (s32)buffer->error()->code() : -1,
					std::memory_order_relaxed);
		const CFTimeInterval spent = buffer->GPUEndTime() - buffer->GPUStartTime();
		if (spent > 0.0)
			impl->GpuMicros.store((u32)(spent * 1000000.0), std::memory_order_relaxed);
		dispatch_semaphore_signal(impl->Frames);
	});

	buildMipmaps(Impl);
	Impl->Commands->commit();

#if 0
	//! Kept only where screenshots can read the frame back
	if (Impl->Readable)
		Impl->Submitted = std::move(Impl->Commands);
#endif
	Impl->Commands.reset();
	Impl->Drawable.reset();

	Impl->Pool.reset();
	return true;
}


//! A new buffer, none for the rest of a frame in which one was refused
static NS::SharedPtr<MTL::Buffer> newBufferUnlessRefused(CMetalDriver::SImpl* impl, u64& refused,
		u32 bytes, const void* data = nullptr,
		MTL::ResourceOptions options = MTL::ResourceStorageModeShared)
{
	if (refused == impl->FrameCounter)
		return {};
	NS::SharedPtr<MTL::Buffer> buffer = NS::TransferPtr(data ?
			impl->Device->newBuffer(data, bytes, options) :
			impl->Device->newBuffer(bytes, options));
	if (!buffer)
		refused = impl->FrameCounter;
	return buffer;
}


//! Copies into the frame's ring; returns buffer and offset, or none
static std::pair<NS::SharedPtr<MTL::Buffer>, u32> stageInto(CMetalDriver::SImpl* impl,
		const void* data, u32 bytes, bool twoD = false)
{
	const u32 room = twoD ? METAL_RING_BYTES : METAL_RING_BYTES - METAL_RING_2D_BYTES;
	//! A draw over a block gets its own buffer, which the command buffer holds once the draw is
	//! encoded
	if (bytes > room)
		return { newBufferUnlessRefused(impl, impl->RingRefused, bytes, data), 0 };

	std::vector<CMetalDriver::SImpl::SRingBlock>& blocks = impl->Rings[impl->RingIndex];
	u32 offset = (impl->RingUsed + METAL_STAGE_ALIGNMENT - 1) & ~(METAL_STAGE_ALIGNMENT - 1);
	if (blocks.empty() || offset + bytes > room)
	{
		const u32 next = blocks.empty() ? 0 : impl->RingBlock + 1;
		//! A refused block stays the current one, so 2D still has its end
		if (next >= blocks.size())
		{
			NS::SharedPtr<MTL::Buffer> buffer = newBufferUnlessRefused(impl, impl->RingRefused,
					METAL_RING_BYTES);
			if (!buffer)
				return {};
			u8* const mapped = (u8*)buffer->contents();
			blocks.push_back({ std::move(buffer), mapped });
		}
		impl->RingBlock = next;
		offset = 0;
	}

	if (impl->RingBlock > impl->RingReach[impl->RingIndex])
		impl->RingReach[impl->RingIndex] = impl->RingBlock;

	const CMetalDriver::SImpl::SRingBlock& block = blocks[impl->RingBlock];
	memcpy(block.Mapped + offset, data, bytes);
	impl->RingUsed = offset + bytes;
	return { block.Buffer, offset };
}


bool SMetalMeshStore::make(SRange& range, u32 bytes)
{
	//! A discrete GPU reads a managed pool from its own memory
	range = newBufferUnlessRefused(Impl, Impl->MeshRefused, bytes, nullptr,
			Impl->Device->hasUnifiedMemory() ?
			MTL::ResourceStorageModeShared : MTL::ResourceStorageModeManaged);
	return range.get();
}


//! Writes in place, or into a new buffer if too small or in flight
bool SMetalMeshStore::fillOwn(SHandle& buffer, u64 drawnAt, const void* data, u32 bytes)
{
	const bool inFlight = drawnAt && Impl->FrameCounter - drawnAt < METAL_FRAMES_IN_FLIGHT;
	if (buffer && (buffer->length() < bytes || inFlight))
		retire(buffer);

	if (!buffer)
		buffer = newBufferUnlessRefused(Impl, Impl->MeshRefused, bytes);
	if (!buffer)
		return false;

	memcpy(buffer->contents(), data, bytes);
	return true;
}


void SMetalMeshStore::written(const SRange& range, u32 at, u32 bytes)
{
	if (range->storageMode() == MTL::StorageModeManaged)
		range->didModifyRange(NS::Range::Make(at, bytes));
}


//! A game program's renderer, which keeps its constant values
class CMetalShaderRenderer : public CDriverShaderRenderer<CMetalDriver, SMetalShaderProgram>
{
public:
	using CDriverShaderRenderer::CDriverShaderRenderer;

	bool bind(E_VERTEX_TYPE vType)
	{
		CMetalDriver::SImpl* impl = Driver->Impl;
		MTL::RenderPipelineState* state = pipelineFor(vType, Driver->Material.ColorMask);
		if (!state)
			return false;

		impl->bindPipeline(state);

		Values.resize(Program->ValueBytes);
		Stale.resize(Program->Uniforms.size(), 1);

		//! All constants go again after a program or encoder change
		const bool all = impl->BoundConstants != this;
		impl->BoundConstants = this;

		settleLayers();

		if (CallBack)
			CallBack->OnSetConstants(this, UserData);

		for (u32 i = 0; i < Program->Uniforms.size(); ++i)
		{
			const SMetalUniform& u = Program->Uniforms[i];
			if (!u.Texture)
			{
				if (!u.Size || (!all && !Stale[i]))
					continue;
				const u8* value = Values.data() + u.Offset;
				if (u.Fragment)
					impl->Encoder->setFragmentBytes(value, u.Size, u.Index);
				else
					impl->Encoder->setVertexBytes(value, u.Size, u.Index);
				Stale[i] = 0;
				continue;
			}

			if (Layers[i] < 0 || (u32)Layers[i] >= MATERIAL_MAX_TEXTURES)
				continue;
			CMetalTexture* texture =
					static_cast<CMetalTexture*>(Driver->Material.getTexture((u32)Layers[i]));
			MTL::Texture* handle = texture ? texture->drawn() : nullptr;

			MTL::SamplerState* sampler =
					samplerFor(impl, Driver->Material.TextureLayer[(u32)Layers[i]],
					Driver->Material.UseMipMaps);

			//! Only slot 0 has a bind mark
			if (u.Index == 0)
			{
				impl->bindTexture(handle);
				impl->bindSampler(sampler);
			}
			else
			{
				impl->Encoder->setFragmentTexture(handle ? handle : impl->Black.get(), u.Index);
				impl->Encoder->setFragmentSamplerState(sampler, u.Index);
			}
		}
		return true;
	}

	//! The pipeline for a vertex type and colour mask, built on first use; the first build also
	//! reflects the program
	MTL::RenderPipelineState* pipelineFor(E_VERTEX_TYPE vType, u32 planes = ECP_ALL)
	{
		CMetalDriver::SImpl* impl = Driver->Impl;
		if ((u32)vType > (u32)EVT_STANDARD_PACKED)
			return nullptr;
		NS::SharedPtr<MTL::RenderPipelineState>& slot =
				Program->Pipelines[Blend ? 1 : 0][vType][planes];
		if (slot || brokenSlot(impl, &slot))
			return slot.get();

		const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
		NS::Error* error = nullptr;
		if (!Program->Vertex)
			Program->Vertex = stageFunction(impl, Program->VertexStage.get(),
					impl->GameConstantValues.get(), &error);
		if (Program->Vertex && !Program->Fragment)
			Program->Fragment = stageFunction(impl, Program->FragmentStage.get(),
					impl->GameConstantValues.get(), &error);
		if (!Program->Vertex || !Program->Fragment)
		{
			refuseSlot(impl, &slot, programName(), error);
			return nullptr;
		}

		const NS::SharedPtr<MTL::RenderPipelineDescriptor> desc = describePipeline(
				Program->Vertex.get(), Program->Fragment.get(), impl->Layouts[vType].get(), Blend,
				planes);
		MTL::RenderPipelineReflection* reflection = nullptr;
		slot = newPipeline(impl, desc.get(), &error, Program->Reflected ? nullptr : &reflection);
		if (!slot)
			refuseSlot(impl, &slot, programName(), error);
		else if (reflection)
			takeReflection(reflection);
		return slot.get();
	}

private:

	void takeReflection(const MTL::RenderPipelineReflection* reflection)
	{
		//! Arguments became bindings in macOS 13 and iOS 16; an entry of either answers the same
		//! selectors
		if (__builtin_available(macOS 13.0, iOS 16.0, *))
		{
			record(reflection->vertexBindings(), false, true);
			record(reflection->fragmentBindings(), true, true);
		}
		else
		{
			record(reflection->vertexArguments(), false, false);
			record(reflection->fragmentArguments(), true, false);
		}
		Program->Reflected = true;
	}

	//! Bindings tell whether the stage reads them by isUsed, arguments by isActive
	void record(const NS::Array* bindings, bool fragment, bool isBinding)
	{
		if (!bindings)
			return;

		for (NS::UInteger a = 0; a < bindings->count(); ++a)
		{
			const auto* binding = static_cast<const MTL::Binding*>(bindings->object(a));
			const bool buffer = binding->type() == MTL::BindingTypeBuffer;
			if (!buffer && binding->type() != MTL::BindingTypeTexture)
				continue;
			//! Skips the driver's vertex buffer slot
			if (buffer && !fragment && binding->index() == METAL_VERTEX_INDEX)
				continue;
			//! A constant the stage never reads is never sent
			if (buffer && !(isBinding ? binding->isUsed() :
					reinterpret_cast<const MTL::Argument*>(binding)->isActive()))
				continue;

			SMetalUniform u{ binding->name()->utf8String(), (u32)binding->index(),
					buffer ?
					(u32)static_cast<const MTL::BufferBinding*>(binding)->bufferDataSize() : 0,
					Program->ValueBytes, fragment, !buffer };
			Program->ValueBytes += (u.Size + 15) & ~15u;

			//! Chain it behind the last slot of the same name
			for (u32 i = 0; i < Program->Uniforms.size(); ++i)
				if (Program->Uniforms[i].Name == u.Name && Program->Uniforms[i].Next < 0)
					Program->Uniforms[i].Next = (s32)Program->Uniforms.size();

			Program->Uniforms.push_back(u);
		}
	}

	bool setConstant(s32 index, const f32* floats, int count) IRR_OVERRIDE
	{
		if (index < 0 || index >= (s32)Program->Uniforms.size() || !floats || count <= 0)
			return false;

		bool wrote = false;
		for (s32 i = index; i >= 0; i = Program->Uniforms[(u32)i].Next)
		{
			const SMetalUniform& u = Program->Uniforms[(u32)i];
			if (u.Texture || !u.Size || u.Offset + u.Size > Values.size())
				continue;

			memcpy(Values.data() + u.Offset, floats,
					core::min_((u32)count * (u32)sizeof(f32), u.Size));
			Stale[(size_t)i] = 1;
			wrote = true;
		}

		return wrote;
	}

	//! Packed constant values, and which ones are yet to be sent
	std::vector<u8> Values;
	std::vector<u8> Stale;
};


static void bindVertices(CMetalDriver::SImpl* impl, MTL::Buffer* buffer, u32 offset)
{
	if (buffer != impl->BoundVertices)
	{
		impl->Encoder->setVertexBuffer(buffer, offset, METAL_VERTEX_INDEX);
		impl->BoundVertices = (buffer);
		impl->BoundVertexOffset = offset;
	}
	else if (offset != impl->BoundVertexOffset)
	{
		impl->Encoder->setVertexBufferOffset(offset, METAL_VERTEX_INDEX);
		impl->BoundVertexOffset = offset;
	}
}


bool CMetalDriver::bindMaterialPipeline(E_VERTEX_TYPE vType)
{
	Impl->bindFill(Material.Wireframe ? MTL::TriangleFillModeLines : MTL::TriangleFillModeFill);

	Impl->bindCull((MTL::CullMode)cullMode());
	Impl->bindDepth(Impl->DepthStates[depthCompare()][depthWrite() ? 1 : 0].get());

	//! A program without a pipeline draws with the material the game named for it
	ProgramBound = ShaderRenderer && ShaderRenderer->bind(vType);
	if (ProgramBound)
		return true;

	const E_MATERIAL_TYPE base = ShaderRenderer ?
			ShaderRenderer->baseMaterial() : Material.MaterialType;
	const u32 blend = blendState(base);
	const bool alphaTest = base == EMT_TRANSPARENT_ALPHA_CHANNEL ||
			base == EMT_TRANSPARENT_ALPHA_CHANNEL_REF;

	//! Textured alpha channel materials take alpha from the texture alone
	const bool textured = Material.getTexture(0) != 0;
	MTL::RenderPipelineState* pipeline = pipelineFor(Impl, textured, alphaTest, blend, vType,
			textured && alphaTest ? 1 : 0, Material.ColorMask);
	if (!pipeline)
		return false;

	Impl->bindPipeline(pipeline);
	//! After the pipeline that reads it; a program binds the textures it reads itself
	if (CMetalTexture* texture = static_cast<CMetalTexture*>(Material.getTexture(0)))
	{
		Impl->bindTexture(texture->drawn());
		Impl->bindSampler(samplerFor(Impl, Material.TextureLayer[0], Material.UseMipMaps));
	}
	return true;
}


static void setScissor(CMetalDriver::SImpl* impl, const MTL::ScissorRect& rect)
{
	if (memcmp(&rect, &impl->BoundScissor, sizeof(rect)) == 0)
		return;

	impl->Encoder->setScissorRect(rect);
	impl->BoundScissor = rect;
}


//! Every draw but the clipped 2D quads covers the whole target
static void setFullScissor(CMetalDriver::SImpl* impl, const core::dimension2d<u32>& target)
{
	setScissor(impl, MTL::ScissorRect{ 0, 0, target.Width, target.Height });
}


void CMetalDriver::bindDrawState()
{
	setFullScissor(Impl, getCurrentRenderTargetSize());

	//! Game programs set their own constants
	if (ProgramBound)
		return;

	const SDriverUniforms uniforms = fixedUniforms();
	const SDriverFog fog = fixedFog(ShaderRenderer ?
			ShaderRenderer->baseMaterial() : Material.MaterialType);

	//! Matrices go with each bind, the fog only when it changes
	Impl->TwoDStanding = false;
	Impl->Encoder->setVertexBytes(&uniforms, sizeof(uniforms), METAL_UNIFORM_INDEX);

	if (!Impl->Fog || memcmp(&*Impl->Fog, &fog, sizeof(fog)) != 0)
	{
		Impl->Encoder->setFragmentBytes(&fog, sizeof(fog), METAL_FOG_INDEX);
		Impl->Fog = fog;
	}
}


static_assert(MTL::IndexTypeUInt16 == (NS::UInteger)EIT_16BIT &&
		MTL::IndexTypeUInt32 == (NS::UInteger)EIT_32BIT);


CNullDriver::SHWBufferLink* CMetalDriver::createHardwareBuffer(const scene::IMeshBuffer* mb)
{
	return createMeshLink(Impl->Meshes, mb);
}


void CMetalDriver::drawLink(CNullDriver::SHWBufferLink* each, u32& bound)
{
	SImpl::SMeshes::SLink* link = static_cast<SImpl::SMeshes::SLink*>(each);
	if (!link || !meshToDraw(Impl->Meshes, link, Impl->Encoder.get()))
		return;
	const u32 layout = (u32)link->VertexType;
	if (layout != bound)
	{
		bound = bindMaterialPipeline(link->VertexType) ? layout : ~0u;
		if (bound != layout)
			return;
		bindDrawState();
	}
	link->DrawnAt = Impl->FrameCounter;
	bindVertices(Impl, link->Vertices.get(), link->VertexOffset);

	Impl->Encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger)link->IndexCount,
			(MTL::IndexType)link->IndexType, link->Indices.get(), (NS::UInteger)link->IndexOffset);
}


void CMetalDriver::deleteHardwareBuffer(CNullDriver::SHWBufferLink* link)
{
	if (link)
		Impl->Meshes.release(static_cast<SImpl::SMeshes::SLink*>(link));
	CNullDriver::deleteHardwareBuffer(link);
}


void CMetalDriver::drawVertexPrimitiveList(const void* vertices, u32 vertexCount,
		const void* indexList, u32 primitiveCount, E_VERTEX_TYPE vType,
		scene::E_PRIMITIVE_TYPE pType, E_INDEX_TYPE iType)
{
	if (!primitiveCount || !vertexCount || !checkPrimitiveCount(primitiveCount))
		return;

	CNullDriver::drawVertexPrimitiveList(vertices, vertexCount, indexList, primitiveCount,
			vType, pType, iType);

	if (!Impl->Encoder || !vertices || !indexList || !MaterialVisible)
		return;

	if (pType == scene::EPT_LINES)
		return drawLineList(vertices, vertexCount, indexList, primitiveCount, vType, iType);
	if (pType != scene::EPT_TRIANGLES)
		return;

	const u32 indexCount = primitiveCount * 3;
	const auto [vertexBuffer, vertexAt] = stageInto(Impl, vertices,
			vertexCount * getVertexPitchFromType(vType));
	const auto [indexBuffer, indexAt] = stageInto(Impl, indexList,
			indexCount * (iType == EIT_32BIT ? 4 : 2));
	if (!vertexBuffer || !indexBuffer)
		return;

	if (!bindMaterialPipeline(vType))
		return;
	bindDrawState();
	bindVertices(Impl, vertexBuffer.get(), vertexAt);
	Impl->Encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger)indexCount,
			(MTL::IndexType)iType, indexBuffer.get(), (NS::UInteger)indexAt);
}


void CMetalDriver::bind2DState()
{
	//! Always rebound: 3D draws change them without clearing TwoDStanding
	Impl->bindDepth(Impl->DepthStates[ECFN_DISABLED][0].get());
	Impl->bindCull(MTL::CullModeNone);
	Impl->bindFill(MTL::TriangleFillModeFill);

	if (Impl->TwoDStanding)
		return;

	SDriverUniforms uniforms = {};
	uniforms.worldViewProj = ortho2D();
	const SDriverFog fog = {};

	Impl->Encoder->setVertexBytes(&uniforms, sizeof(uniforms), METAL_UNIFORM_INDEX);
	Impl->Encoder->setFragmentBytes(&fog, sizeof(fog), METAL_FOG_INDEX);
	Impl->Fog.reset();
	Impl->TwoDStanding = true;
}


void CMetalDriver::drawQuads2D(const ITexture* texture, const S3DVertex* quads, u32 count,
		const core::rect<s32>* clip, bool alpha, bool alphaChannel)
{
	if (!Impl->Encoder || !count)
		return;

	const core::dimension2d<u32>& target = getCurrentRenderTargetSize();
	core::rect<s32> scissor(0, 0, (s32)target.Width, (s32)target.Height);
	if (clip)
	{
		//! Metal rejects a scissor that leaves the attachment
		scissor.clipAgainst(*clip);
		if (scissor.getWidth() <= 0 || scissor.getHeight() <= 0)
			return;
	}

	//! Only grows; frames in flight retain the old buffer
	if (Impl->QuadOrderQuads < count)
	{
		core::array<u32> order;
		order.reallocate(count * 6);
		for (u32 quad = 0; quad < count; ++quad)
		{
			const u32 base = quad * 4;
			order.push_back(base); order.push_back(base + 1); order.push_back(base + 2);
			order.push_back(base); order.push_back(base + 2); order.push_back(base + 3);
		}
		//! Without memory the old order stays and draws the quads in parts
		if (NS::SharedPtr<MTL::Buffer> grown = NS::TransferPtr(Impl->Device->newBuffer(
				order.pointer(), order.size() * sizeof(u32), MTL::ResourceStorageModeShared)))
		{
			Impl->QuadOrder = grown;
			Impl->QuadOrderQuads = count;
		}
	}

	const auto [vertexBuffer, vertexAt] = stageInto(Impl, quads,
			count * 4 * sizeof(S3DVertex), true);
	if (!vertexBuffer || !Impl->QuadOrderQuads)
		return;

	const CMetalTexture* metal = static_cast<const CMetalTexture*>(texture);
	//! Texture alpha only with a texture and alphaChannel
	alphaChannel &= metal != 0;
	MTL::RenderPipelineState* pipeline = pipelineFor(Impl, metal != 0, false,
			alpha || alphaChannel ? DRIVER_BLEND_ALPHA : 0, EVT_STANDARD, metal && alpha &&
			!alphaChannel ? 2 : 0);
	if (!pipeline)
		return;

	Impl->bindPipeline(pipeline);
	if (metal)
	{
		//! Filter and wrap from the 2D material's first layer
		Impl->bindTexture(metal->drawn());
		Impl->bindSampler(samplerFor(Impl, material2D().TextureLayer[0], material2D().UseMipMaps));
	}

	setScissor(Impl, MTL::ScissorRect{ (NS::UInteger)scissor.UpperLeftCorner.X,
			(NS::UInteger)scissor.UpperLeftCorner.Y, (NS::UInteger)scissor.getWidth(),
			(NS::UInteger)scissor.getHeight() });

	bind2DState();
	for (u32 done = 0; done < count; done += Impl->QuadOrderQuads)
	{
		bindVertices(Impl, vertexBuffer.get(), vertexAt + done * 4 * (u32)sizeof(S3DVertex));
		Impl->Encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
				(NS::UInteger)core::min_(count - done, Impl->QuadOrderQuads) * 6,
				MTL::IndexTypeUInt32, Impl->QuadOrder.get(), (NS::UInteger)0);
	}
}


void CMetalDriver::draw3DLine(const core::vector3df& start, const core::vector3df& end,
		SColor color)
{
	if (!Impl->Encoder || !MaterialVisible)
		return;

	S3DVertex line[4] = { S3DVertex(start.X, start.Y, start.Z, 0, 0, 1, color, 0, 0),
			S3DVertex(end.X, end.Y, end.Z, 0, 0, 1, color, 0, 0) };

	//! Metal lines are 1 pixel wide; a thicker one is a quad
	const bool quad = Material.Thickness > 1.0f && thickLineQuad(Matrices[ETS_PROJECTION],
			Matrices[ETS_VIEW], Matrices[ETS_WORLD], ActiveViewPort, start, end, Material.Thickness,
			color, line);

	const u32 count = quad ? 4 : 2;
	const auto [vertexBuffer, vertexAt] = stageInto(Impl, line, count * sizeof(S3DVertex));
	if (!vertexBuffer)
		return;

	MTL::RenderPipelineState* pipeline = pipelineFor(Impl, false, false,
			color.getAlpha() < 255 ? DRIVER_BLEND_ALPHA : 0, EVT_STANDARD, 0, Material.ColorMask);
	if (!pipeline)
		return;

	//! Draws with the driver's pipeline, not the game's program
	ProgramBound = false;

	Impl->bindPipeline(pipeline);
	//! No culling: the quad's winding follows the line's direction
	Impl->bindCull(MTL::CullModeNone);
	Impl->bindFill(MTL::TriangleFillModeFill);
	//! The material's depth test, not the 2D one
	Impl->bindDepth(Impl->DepthStates[depthCompare()][depthWrite() ? 1 : 0].get());
	bindDrawState();
	bindVertices(Impl, vertexBuffer.get(), vertexAt);
	Impl->Encoder->drawPrimitives((quad ? MTL::PrimitiveTypeTriangleStrip : MTL::PrimitiveTypeLine),
			(NS::UInteger)0, (NS::UInteger)count);
}


//! vertexProgram and fragmentProgram hold the stages' MSL, which builds now; the program takes
//! its functions at first use
bool CMetalDriver::addPrecompiledShader(const c8* name, const void* vertexProgram, u32 vertexSize,
		const void* fragmentProgram, u32 fragmentSize)
{
	if (!name || !vertexProgram || !fragmentProgram || !vertexSize || !fragmentSize)
		return false;

	const c8* vertex = (const c8*)vertexProgram;
	const c8* fragment = (const c8*)fragmentProgram;
	SMetalShaderProgram* program = new SMetalShaderProgram();
	program->Name = name;
	program->VertexStage = buildStage(Impl, std::string(vertex, strnlen(vertex, vertexSize)));
	program->FragmentStage = buildStage(Impl,
			std::string(fragment, strnlen(fragment, fragmentSize)));
	Programs.push_back(program);
	return true;
}


void CMetalDriver::setPrecompiledConstant(u32 id, bool value)
{
	setGameConstant(id, value ? 1 : 0, MTL::DataTypeBool);
}


void CMetalDriver::setPrecompiledConstant(u32 id, f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	setGameConstant(id, bits, MTL::DataTypeFloat);
}


void CMetalDriver::setPrecompiledConstant(u32 id, s32 value)
{
	setGameConstant(id, (u32)value, MTL::DataTypeInt);
}


//! The functions skip the constants they do not declare, so one set serves every program
void CMetalDriver::setGameConstant(u32 id, u32 bits, u32 type)
{
	if (id < 16)
		return;
	core::array<SImpl::SGameConstant>& constants = Impl->GameConstants;
	const u32 index = id - 16;
	if (index < constants.size() && constants[index].Bits == bits &&
			constants[index].Type == type)
		return;
	while (constants.size() <= index)
		constants.push_back({ 0, MTL::DataTypeBool });
	constants[index] = { bits, type };
	Impl->GameConstantValues = NS::TransferPtr(MTL::FunctionConstantValues::alloc()->init());
	for (u32 i = 0; i < constants.size(); ++i)
	{
		//! A bool takes 1 byte, a float or an int its 4
		const bool set = constants[i].Bits != 0;
		const MTL::DataType type = (MTL::DataType)constants[i].Type;
		Impl->GameConstantValues->setConstantValue(type == MTL::DataTypeBool ?
				(const void*)&set : &constants[i].Bits, type, (NS::UInteger)(16 + i));
	}

	//! The programs take their functions and pipelines anew at next use
	for (u32 i = 0; i < Programs.size(); ++i)
	{
		Programs[i]->Vertex = NS::SharedPtr<MTL::Function>();
		Programs[i]->Fragment = NS::SharedPtr<MTL::Function>();
		for (auto& byBlend : Programs[i]->Pipelines)
			for (auto& byType : byBlend)
				for (auto& pipeline : byType)
					pipeline = NS::SharedPtr<MTL::RenderPipelineState>();
	}
	Impl->forgetBindings();
}


CMetalShaderRenderer* CMetalDriver::newShaderRenderer(u32 index,
		IShaderConstantSetCallBack* callback, E_MATERIAL_TYPE base, s32 userData)
{
	auto* renderer = new CMetalShaderRenderer(this, Programs[index], callback, base, userData);
	//! Builds its pipelines while the game loads, for the static meshes too
	renderer->pipelineFor(EVT_STANDARD);
	renderer->pipelineFor(EVT_STANDARD_PACKED);
	return renderer;
}


void CMetalDriver::setViewPort(const core::rect<s32>& area, bool clipToRenderTarget)
{
	core::rect<s32> vp = area;
	if (clipToRenderTarget)
	{
		const core::dimension2d<u32>& target = getCurrentRenderTargetSize();
		vp.clipAgainst(core::rect<s32>(0, 0, (s32)target.Width, (s32)target.Height));
	}
	if (vp.getWidth() <= 0 || vp.getHeight() <= 0)
		return;

	ViewPort = vp;
	applyViewPort(vp);
}


void CMetalDriver::applyViewPort(const core::rect<s32>& area)
{
	ActiveViewPort = area;

	if (!Impl->Encoder)
		return;

	//! Top left origin, as in core::rect
	const MTL::Viewport viewport = { (f64)area.UpperLeftCorner.X, (f64)area.UpperLeftCorner.Y,
			(f64)area.getWidth(), (f64)area.getHeight(), 0.0, 1.0 };
	if (memcmp(&viewport, &Impl->BoundViewport, sizeof(viewport)) == 0)
		return;

	Impl->Encoder->setViewport(viewport);
	Impl->BoundViewport = viewport;
}


core::dimension2du CMetalDriver::getMaxTextureSize() const
{
	const u32 side = maxTextureSide(Impl->Device.get());
	return core::dimension2du(side, side);
}


ITexture* CMetalDriver::addRenderTargetTexture(const core::dimension2d<u32>& size,
		const io::path& name, const ECOLOR_FORMAT format)
{
	//! Metal aborts on a texture descriptor of no size
	if (!size.Width || !size.Height)
		return 0;

	CMetalTexture* texture = new CMetalTexture(name, size, format, Impl->Device.get(),
			Impl->PackedTextures);
	addTexture(texture);
	texture->drop();
	return texture;
}


IRenderTarget* CMetalDriver::addRenderTarget()
{
	CMetalRenderTarget* target = new CMetalRenderTarget(ScreenSize);
	RenderTargets.push_back(target);
	return target;
}


bool CMetalDriver::setRenderTargetEx(IRenderTarget* target, u16 clearFlag, SColor clearColor,
		f32 clearDepth, u8)
{
	if (target && target->getDriverType() != EDT_METAL)
	{
		os::Printer::log("Fatal Error: Tried to set a render target not owned by this driver.",
				ELL_ERROR);
		return false;
	}

	endEncoding();

	CurrentRenderTarget = target;
	CurrentRenderTargetSize = target ? static_cast<CMetalRenderTarget*>(target)->getSize() :
			core::dimension2d<u32>(0, 0);
	const core::dimension2d<u32>& size = getCurrentRenderTargetSize();
	ActiveViewPort = core::rect<s32>(0, 0, (s32)size.Width, (s32)size.Height);

	if (!Impl->Started)
		return true;

	return beginEncoding((clearFlag & ECBF_COLOR) != 0, clearColor, clearDepth);
}


void CMetalDriver::clearBuffers(u16 flag, SColor, f32 depth, u8)
{
	//! Mid-pass only the depth is cleared, drawn over the whole target whatever the viewport
	if (!(flag & ECBF_DEPTH) || !Impl->Encoder)
		return;

	const core::dimension2d<u32>& target = getCurrentRenderTargetSize();
	const core::rect<s32> kept = ActiveViewPort;
	applyViewPort(core::rect<s32>(0, 0, (s32)target.Width, (s32)target.Height));

	Impl->bindPipeline(Impl->DepthQuad.get());
	Impl->bindDepth(Impl->DepthStates[ECFN_ALWAYS][1].get());
	Impl->bindCull(MTL::CullModeNone);
	Impl->bindFill(MTL::TriangleFillModeFill);
	setFullScissor(Impl, target);
	Impl->Encoder->setVertexBytes(&depth, sizeof(f32), METAL_UNIFORM_INDEX);
	Impl->Encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger)0, (NS::UInteger)3);
	//! The depth value took the slot of the 2D matrices
	Impl->TwoDStanding = false;

	applyViewPort(kept);
}


u32 CMetalDriver::getGPUTimeMicros() const
{
	return Impl->GpuMicros.load(std::memory_order_relaxed);
}


void CMetalDriver::OnResize(const core::dimension2d<u32>& size)
{
	CNullDriver::OnResize(size);
	ViewPort = core::recti(0, 0, (s32)size.Width, (s32)size.Height);
	ActiveViewPort = ViewPort;
}


ITexture* CMetalDriver::createDeviceDependentTexture(const io::path& name, IImage* image)
{
	if (!image)
		return 0;

	return new CMetalTexture(name, image, Impl->Queue.get(),
			textureFormatFor(this, Impl->PackedTextures, image->getColorFormat()),
			getTextureCreationFlag(ETCF_ALLOW_MEMORY_COPY), &Impl->FrameCounter,
			getTextureCreationFlag(ETCF_CREATE_MIP_MAPS) ? &Impl->Mipmaps : nullptr,
			&Impl->Unmade);
}


#if 0
IImage* CMetalDriver::createScreenShot(video::ECOLOR_FORMAT format,
		video::E_RENDER_TARGET target)
{
	if (format == ECF_UNKNOWN)
		format = getColorFormat();

	//! Presented frame only, in the 4 formats converted below
	if (!Impl->Presented || target != ERT_FRAME_BUFFER ||
			(format != ECF_A1R5G5B5 && format != ECF_R5G6B5 && format != ECF_R8G8B8 &&
			format != ECF_A8R8G8B8))
		return 0;

	const NS::SharedPtr<NS::AutoreleasePool> pool = newAutoreleasePool();
	Impl->Submitted->waitUntilCompleted();

	const u32 width = (u32)Impl->Presented->width();
	const u32 height = (u32)Impl->Presented->height();

	//! Blits the frame into a CPU-readable texture
	MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
			MTL::PixelFormatBGRA8Unorm, width, height, false);
	desc->setUsage(MTL::TextureUsageShaderRead);
	desc->setStorageMode(Impl->Device->supportsFamily(MTL::GPUFamilyApple1) ?
			MTL::StorageModeShared : MTL::StorageModeManaged);
	const NS::SharedPtr<MTL::Texture> readable = NS::TransferPtr(Impl->Device->newTexture(desc));
	if (!readable)
		return 0;

	MTL::CommandBuffer* commands = Impl->Queue->commandBuffer();
	MTL::BlitCommandEncoder* blit = commands->blitCommandEncoder();
	blit->copyFromTexture(Impl->Presented.get(), 0, 0, MTL::Origin(0, 0, 0),
			MTL::Size(width, height, 1), readable.get(), 0, 0, MTL::Origin(0, 0, 0));
	if (readable->storageMode() == MTL::StorageModeManaged)
		blit->synchronizeResource(readable.get());
	blit->endEncoding();
	commands->commit();
	commands->waitUntilCompleted();

	IImage* image = new CImage(ECF_A8R8G8B8, core::dimension2du(width, height));
	//! BGRA8 in memory is ECF_A8R8G8B8, top-down: no swizzle, no flip
	readable->getBytes(image->getData(), width * 4, MTL::Region::Make2D(0, 0, width, height), 0);

	if (format != ECF_A8R8G8B8)
	{
		IImage* wanted = new CImage(format, image->getDimension());
		CColorConverter::convert_viaFormat(image->getData(), ECF_A8R8G8B8,
				(s32)(width * height), wanted->getData(), format);
		image->drop();
		return wanted;
	}
	return image;
}
#endif


IVideoDriver* createMetalDriver(const SIrrlichtCreationParameters& params,
		io::IFileSystem* io, CIrrDeviceSDL* device)
{
	CMetalDriver* driver = new CMetalDriver(params, io);
	if (!driver->genericDriverInit(params, device))
	{
		driver->drop();
		return 0;
	}
	return driver;
}

} // end namespace irr::video

#endif // _IRR_COMPILE_WITH_METAL_
