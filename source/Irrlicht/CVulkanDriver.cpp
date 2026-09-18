// Copyright (C) 2026 Maksym H.
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in Irrlicht.h

#include "CVulkanDriver.h"

#ifdef _IRR_COMPILE_WITH_VULKAN_

#include "CIrrDeviceSDL.h"
#include "CColorConverter.h"
#include "CImage.h"
#include "EVertexAttributes.h"
#include "IFileSystem.h"
#include "os.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dlfcn.h>

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

namespace irr::video
{

//! Vulkan functions, loaded at run time
#define IRR_VULKAN_GLOBAL(X) \
	X(vkCreateInstance)

#define IRR_VULKAN_INSTANCE(X) \
	X(vkDestroyInstance) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceFeatures) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkGetPhysicalDeviceFormatProperties) \
	X(vkEnumerateDeviceExtensionProperties) \
	X(vkCreateDevice) \
	X(vkGetDeviceProcAddr) \
	X(vkDestroySurfaceKHR) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkCreateAndroidSurfaceKHR)

#define IRR_VULKAN_DEVICE(X) \
	X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) \
	X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
	X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) \
	X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) \
	X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
	X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
	X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
	X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
	X(vkCreateShaderModule) X(vkDestroyShaderModule) \
	X(vkCreatePipelineCache) X(vkDestroyPipelineCache) \
	X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
	X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
	X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
	X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) \
	X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
	X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkResetCommandPool) \
	X(vkAllocateCommandBuffers) \
	X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
	X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkWaitForFences) \
	X(vkCreateSemaphore) X(vkDestroySemaphore) \
	X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) \
	X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) \
	X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdDraw) X(vkCmdDrawIndexed) \
	X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdPipelineBarrier) X(vkCmdBlitImage) \
	X(vkCmdClearAttachments) X(vkCmdClearColorImage)

#define IRR_VULKAN_DEVICE_1_3(X) \
	X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdSetCullMode)

struct SVulkanFunctions
{
#define IRR_VULKAN_FIELD(name) decltype(&::name) name = nullptr;
	IRR_VULKAN_GLOBAL(IRR_VULKAN_FIELD)
	IRR_VULKAN_INSTANCE(IRR_VULKAN_FIELD)
	IRR_VULKAN_DEVICE(IRR_VULKAN_FIELD)
	IRR_VULKAN_DEVICE_1_3(IRR_VULKAN_FIELD)
#undef IRR_VULKAN_FIELD
	decltype(&::vkGetInstanceProcAddr) vkGetInstanceProcAddr = nullptr;
};


static const u32 VULKAN_FRAMES_IN_FLIGHT = 3;

//! Nanoseconds to wait for an image; the compositor may hold every one
static const u64 VULKAN_WAIT_NANOSECONDS = 5000000000;

static const VkCommandBufferBeginInfo VULKAN_ONE_TIME_SUBMIT = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr };

//! Bytes per ring block; a frame takes as many as it needs
static const u32 VULKAN_RING_BYTES = 2 << 20;
//! End of each block only 2D takes: the interface draws when the ring cannot grow
static const u32 VULKAN_RING_2D_BYTES = 512 << 10;

//! Bytes a uniform block binding covers, for every stage
static const u32 VULKAN_UNIFORM_RANGE = 1024;

//! Texture bindings per draw
static const u32 VULKAN_LAYERS = 3;

//! Sets per descriptor pool, with room for any mix of layouts
static const u32 VULKAN_POOL_SETS = 512;


//! Arena alignment, a multiple of the index width and 3 vertex sizes
static const u32 VULKAN_ARENA_ALIGN = 1980;
static const u32 VULKAN_ARENA_BYTES = 8 << 20;
//! Mali may read past a buffer's end (Arm errata 3781554, 3779191): a page of slack covers it
static const u32 VULKAN_BUFFER_SLACK = 4096;
//! Most bytes one draining pass moves
static const u32 VULKAN_DRAIN_BYTES = 512 << 10;
//! Smallest pool; pools of small classes hold more than 64 chunks
static const u32 VULKAN_POOL_FLOOR = 64 << 10;
//! Largest pool of up to 64 chunks: a class in little use leaves less of it unused
static const u32 VULKAN_POOL_CAP = 256 << 10;
//! Chunk units per kind: indices, then the 3 vertex sizes and the packed standard vertex
static const u32 VULKAN_SHARED_UNITS[] = { 4, 36, 44, 60, sizeof(S3DVertexPacked) };

//! Texture memory blocks, shared by textures up to half a block
static const u32 VULKAN_IMAGE_BLOCK_BYTES = 2 << 20;
//! The game's limit on a texture side; the device may take less
static const u32 VULKAN_MAX_TEXTURE_SIZE = 16384;

//! Pipeline key program fields of the driver's own stages and of the depth clear triangle
static const u32 VULKAN_FIXED_PROGRAM = 0;
static const u32 VULKAN_CLEAR_PROGRAM = 0xffff;

//! The stages Metal and Vulkan share, as SPIR-V read from OGLES2ShaderPath
static const c8* const VULKAN_FIXED_VERTEX = "CDriverCommon.vert.spv";
static const c8* const VULKAN_FIXED_FRAGMENT = "CDriverCommon.frag.spv";
static const c8* const VULKAN_DEPTH_CLEAR[2] = { "CDriverCommonDepth.vert.spv",
		"CDriverCommonDepth.frag.spv" };


//! A buffer with its memory, mapped for its whole life
struct SVulkanBuffer
{
	VkBuffer Buffer = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	u8* Mapped = nullptr;
	u32 Bytes = 0;
};


//! A free range of a block
struct SVulkanRange
{
	u32 Base;
	u32 Bytes;
};


//! Large buffer holding shared pools and staged texture pixels
struct SVulkanArena
{
	SVulkanBuffer Buffer;
	u32 Bytes = 0;
	u32 Used = 0;
	//! Free ranges sorted by start
	std::vector<SVulkanRange> Free;
	//! Takes no new ranges while its pools move out, then goes
	bool Draining = false;
};


//! Memory of the mesh buffers: pools in arena ranges, a buffer per unshared mesh
struct SVulkanMeshStore
{
	struct SRange
	{
		SVulkanArena* Arena = nullptr;
		u32 Base = 0;
	};
	using SHandle = SVulkanBuffer*;
	static const u32 Kinds = std::size(VULKAN_SHARED_UNITS);
	static const u32 Frames = VULKAN_FRAMES_IN_FLIGHT;
	static const u32 DrainBytes = VULKAN_DRAIN_BYTES;

	//! Kind by vertex size, as in VULKAN_SHARED_UNITS
	static u32 kindOf(u32 stride)
	{
		return stride == 44 ? 2 : stride == 60 ? 3 : stride == sizeof(S3DVertexPacked) ? 4 : 1;
	}
	static u32 chunkBytes(u32 size, u32 kind)
	{
		const u32 unit = VULKAN_SHARED_UNITS[kind];
		return (DRIVER_SHARED_SIZES[size] + unit - 1) / unit * unit;
	}
	static u32 poolBytes(u32 size, u32 kind)
	{
		const u32 chunk = chunkBytes(size, kind);
		const u32 count = core::clamp(VULKAN_POOL_CAP / chunk, 1u, 64u);
		const u32 bytes = core::max_(chunk * count, VULKAN_POOL_FLOOR);
		return (bytes + VULKAN_ARENA_ALIGN - 1) / VULKAN_ARENA_ALIGN * VULKAN_ARENA_ALIGN;
	}
	//! A chunk above 64 KB fits its data, to the arena alignment: its pool holds only it
	static u32 exactBytes(u32 size, u32 bytes)
	{
		if (DRIVER_SHARED_SIZES[size] <= 65536)
			return 0;
		return (bytes + VULKAN_ARENA_ALIGN - 1) / VULKAN_ARENA_ALIGN * VULKAN_ARENA_ALIGN;
	}
	static u8* mapped(const SRange& range) { return range.Arena->Buffer.Mapped + range.Base; }
	static SHandle handle(const SRange& range) { return &range.Arena->Buffer; }
	static u32 fullness(const SRange& range) { return range.Arena->Used; }
	static void written(const SRange&, u32, u32) {}

	bool make(SRange& range, u32 bytes);
	void free(const SRange& range, u32 bytes);
	bool fillOwn(SHandle& buffer, u64 drawnAt, const void* data, u32 bytes);
	void retire(SHandle& buffer);
	//! Drains one sparse arena at a time if the others can take its pools
	bool drain();

	CVulkanDriver::SImpl* Impl = nullptr;
};


//! Pinned meshes would scatter over the arenas as new land streams in
template <>
struct SDriverHoldsOnlyCopy<SVulkanMeshStore> { static const bool Value = false; };


//! Device memory of one type, shared by small textures
struct SVulkanImageBlock
{
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	u32 Type = 0;
	u32 Used = 0;
	//! Free ranges sorted by start
	std::vector<SVulkanRange> Free;
};


struct SVulkanImage
{
	VkImage Image = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	VkImageView View = VK_NULL_HANDLE;
	//! Its range of a shared block, if it has no memory of its own
	SVulkanImageBlock* Block = nullptr;
	u32 Offset = 0;
	u32 Bytes = 0;
};


//! Pixels waiting to be copied into a texture
struct SVulkanTextureWrite
{
	VkImage Image = VK_NULL_HANDLE;
	//! Mip levels, built from level 0 after the copy
	u32 Levels = 1;
	bool First = false;
	//! Arena range holding the staged pixels, which start at Offset; none clears the image
	SVulkanArena* Arena = nullptr;
	u32 Base = 0;
	u32 Bytes = 0;
	u32 Offset = 0;
	u32 Width = 0;
	u32 Height = 0;
};


//! A program constant, read from its stage's SPIR-V
struct SVulkanUniform
{
	core::stringc Name;
	//! 0 vertex, 1 fragment
	u32 Stage = 0;
	u32 Offset = 0;
	//! Floats per element, and their layout in the block
	u32 Components = 0;
	u32 Columns = 1;
	u32 ColumnStride = 0;
	u32 Elements = 1;
	u32 Stride = 0;
	bool Texture = false;
	//! For a texture, its binding in set 2
	u32 Binding = 0;
	//! The same-named constant of the other stage, or -1
	s32 Next = -1;
};


//! Two stages from the game's library, with their constants
struct SVulkanShaderProgram
{
	core::stringc Name;
	VkShaderModule Modules[2] = {};
	core::array<SVulkanUniform> Uniforms;
	u32 BlockBytes[2] = {};
	//! Locations the vertex stage reads, a bit each
	u32 Inputs = 0;
};


//! Locations of a stage's inputs, a bit each; a matrix takes one per column
static u32 inputLocations(const u32* words, size_t count)
{
	std::unordered_map<u32, u32> locations, columns;
	u32 inputs = 0;
	for (size_t i = 5; i < count;)
	{
		const u32 length = words[i] >> 16;
		const u32* w = &words[i];
		if (!length || i + length > count)
			return ~0u;
		//! Location decorations, matrix and pointer types, then Input variables
		const u32 op = w[0] & 0xffff;
		if (op == 71 && length > 3 && w[2] == 30)
			locations[w[1]] = w[3];
		else if (op == 24 && length > 3)
			columns[w[1]] = w[3];
		else if (op == 32 && length > 3)
			columns[w[1]] = columns[w[3]];
		else if (op == 59 && length > 3 && w[3] == 1)
			if (const auto found = locations.find(w[2]); found != locations.end())
				for (u32 l = found->second, end = l + core::max_(columns[w[1]], 1u);
						l < end && l < 32; ++l)
					inputs |= 1u << l;
		i += length;
	}
	return inputs;
}


//! Reflects a stage; block to set <stage>, textures to set 2
static bool reflectStage(std::vector<u32>& words, u32 stage, SVulkanShaderProgram* program,
		u32& textures)
{
	if (words.size() < 5 || words[0] != 0x07230203)
		return false;

	struct SType
	{
		u32 Op = 0;
		u32 A = 0;
		u32 B = 0;
		std::vector<u32> Members;
	};

	std::unordered_map<u32, std::string> names;
	std::unordered_map<u64, std::string> memberNames;
	std::unordered_map<u64, u32> memberOffsets, matrixStrides;
	std::unordered_map<u32, u32> arrayStrides, constants;
	std::unordered_map<u32, SType> types;
	std::unordered_map<u32, size_t> bindings, sets;
	std::vector<std::pair<u32, u32> > variables;

	for (size_t i = 5; i < words.size();)
	{
		const u32 count = words[i] >> 16;
		const u32 op = words[i] & 0xffff;
		if (!count || i + count > words.size())
			return false;

		const u32* w = &words[i];
		switch (op)
		{
		case 5: // OpName
			if (count > 2)
				names[w[1]] = std::string((const char*)(w + 2),
						strnlen((const char*)(w + 2), (count - 2) * 4));
			break;
		case 6: // OpMemberName
			if (count > 3)
				memberNames[((u64)w[1] << 32) | w[2]] =
						std::string((const char*)(w + 3),
						strnlen((const char*)(w + 3), (count - 3) * 4));
			break;
		case 71: // OpDecorate
			if (count > 3 && w[2] == 33)
				bindings[w[1]] = i + 3;
			else if (count > 3 && w[2] == 34)
				sets[w[1]] = i + 3;
			else if (count > 3 && w[2] == 6)
				arrayStrides[w[1]] = w[3];
			break;
		case 72: // OpMemberDecorate
			if (count > 4 && w[3] == 35)
				memberOffsets[((u64)w[1] << 32) | w[2]] = w[4];
			else if (count > 4 && w[3] == 7)
				matrixStrides[((u64)w[1] << 32) | w[2]] = w[4];
			break;
		case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 32:
			types[w[1]] = { op, count > 2 ? w[2] : 0, count > 3 ? w[3] : 0, {} };
			break;
		case 30: // OpTypeStruct
			types[w[1]] = { op, 0, 0, std::vector<u32>(w + 2, w + count) };
			break;
		case 43: // OpConstant
			if (count > 3)
				constants[w[2]] = w[3];
			break;
		case 59: // OpVariable
			variables.emplace_back(w[1], w[2]);
			break;
		default:
			break;
		}
		i += count;
	}

	for (const auto [pointerId, id] : variables)
	{
		const SType& pointer = types[pointerId];
		if (pointer.Op != 32)
			continue;

		//! Only the stage's uniform block and its textures
		const SType& pointee = types[pointer.B];
		const bool block = pointer.A == 2 && pointee.Op == 30;
		if (!block && (pointer.A != 0 || pointee.Op != 27))
			continue;

		if (const auto binding = bindings.find(id); binding != bindings.end())
			words[binding->second] = block ? 0 : textures;
		if (const auto set = sets.find(id); set != sets.end())
			words[set->second] = block ? stage : 2;

		if (block)
		{
			u32 bytes = 0;
			for (u32 m = 0; m < (u32)pointee.Members.size(); ++m)
			{
				const u64 key = ((u64)pointer.B << 32) | m;
				SVulkanUniform u{ memberNames[key].c_str(), stage, memberOffsets[key] };

				u32 typeId = pointee.Members[m];
				if (types[typeId].Op == 28)
				{
					u.Elements = constants[types[typeId].B];
					u.Stride = arrayStrides[typeId];
					typeId = types[typeId].A;
				}

				const SType& base = types[typeId];
				u32 elementBytes = 4;
				if (base.Op == 22 || base.Op == 21 || base.Op == 20)
					u.Components = 1;
				else if (base.Op == 23)
				{
					u.Components = base.B;
					elementBytes = 4 * base.B;
				}
				else if (base.Op == 24)
				{
					const u32 column = types[base.A].Op == 23 ? types[base.A].B : 1;
					u.Columns = base.B;
					u.ColumnStride = matrixStrides.count(key) ? matrixStrides[key] : 16;
					u.Components = column * base.B;
					elementBytes = u.ColumnStride * (base.B - 1) + 4 * column;
				}
				else
					continue;

				if (!u.Stride)
					u.Stride = elementBytes;
				bytes = core::max_(bytes, u.Offset + u.Stride * (u.Elements - 1) + elementBytes);
				program->Uniforms.push_back(u);
			}
			program->BlockBytes[stage] = (bytes + 15) & ~15u;
		}
		else
		{
			SVulkanUniform u{ names[id].c_str(), stage };
			u.Texture = true;
			u.Binding = textures++;
			program->Uniforms.push_back(u);
		}
	}

	return true;
}


struct CVulkanDriver::SImpl
{
	void* Library = nullptr;
	SVulkanFunctions Vk;

	VkInstance Instance = VK_NULL_HANDLE;
	VkPhysicalDevice Gpu = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;
	VkQueue Queue = VK_NULL_HANDLE;
	u32 QueueFamily = 0;
	VkPhysicalDeviceProperties Properties = {};
	VkPhysicalDeviceMemoryProperties Memory = {};
	VkPhysicalDeviceFeatures Features = {};
	//! Device lost; nothing is drawn after that
	bool Lost = false;

	//! Native window the surface was made for
	void* Window = nullptr;
	VkSurfaceKHR Surface = VK_NULL_HANDLE;
	VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
	VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
	VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
	VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
	VkExtent2D Extent = { 0, 0 };
	//! Panel transform the chain was made for; Turn rotates positions: 1 by 90 degrees, -1 by 270
	VkSurfaceTransformFlagBitsKHR Transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	s32 Turn = 0;
	std::vector<VkImage> Images;
	//! Per image: a present may hold it until the image comes back
	std::vector<VkSemaphore> RenderDone;
	std::vector<VkImageView> Views;
	std::vector<VkFramebuffer> Framebuffers;
	SVulkanImage Depth;
	bool SwapchainStale = false;
	u32 ImageIndex = 0;
	bool HaveImage = false;
#if 0
	//! Swapchain images can be copied from, for screenshots
	bool Readable = false;
#endif

	//! Vulkan 1.3 path: dynamic rendering, no passes or framebuffers
	bool Use13 = false;
	//! Culling set per draw, outside the pipeline
	bool DynamicCull = false;
	//! [0] discards the image, [1] clears it; both are compatible
	VkRenderPass Passes[2] = {};
	//! Sets: vertex block, fragment block, textures
	VkDescriptorSetLayout SetLayouts[3] = {};
	VkPipelineLayout Layout = VK_NULL_HANDLE;
	VkPipelineCache Cache = VK_NULL_HANDLE;
	VkShaderModule Fixed[2] = {};
	//! Old Qualcomm, GE8300 and GE8322 fail on clears inside a pass: depth clears draw a triangle
	SVulkanShaderProgram Clear;
	u32 FixedInputs = 0;
	//! The draw being bound reads split vertices
	bool SplitVertices = false;
	std::unordered_map<u64, VkPipeline> Pipelines;
	u64 LastPipelineKey = UINT64_MAX;
	VkPipeline LastPipeline = VK_NULL_HANDLE;
	//! Specialization constants of the game's programs from id 16, as raw bits
	std::vector<u32> GameConstants;

	//! A sampler per layer setting
	std::unordered_map<u32, VkSampler> Samplers;
	//! Owned by Samplers
	VkSampler Sampler2D = VK_NULL_HANDLE;

	//! Opaque black, sampled from layers with no texture
	SVulkanImage Black;

	//! Views and samplers of a draw, the key of its texture set
	struct STextureSet
	{
		VkImageView Views[VULKAN_LAYERS];
		VkSampler Samplers[VULKAN_LAYERS];

		bool operator==(const STextureSet& other) const
		{
			return memcmp(this, &other, sizeof(other)) == 0;
		}

		void fill(VkImageView view, VkSampler sampler)
		{
			std::fill(std::begin(Views), std::end(Views), view);
			std::fill(std::begin(Samplers), std::end(Samplers), sampler);
		}
	};
	struct STextureSetHash
	{
		size_t operator()(const STextureSet& set) const
		{
			size_t hash = 0;
			for (u32 i = 0; i < VULKAN_LAYERS; ++i)
				hash = hash * 131 + (size_t)(u64)set.Views[i] * 31 + (size_t)(u64)set.Samplers[i];
			return hash;
		}
	};
	std::unordered_map<STextureSet, VkDescriptorSet, STextureSetHash> TextureSets;
	std::vector<VkDescriptorPool> SetPools;
	//! Sets left in the newest pool
	u32 PoolSetsLeft = 0;
	//! The next pool, made ahead, for when memory refuses one
	VkDescriptorPool SparePool = VK_NULL_HANDLE;
	//! Sets no frame reads, by layout; sets are reused, never freed
	std::vector<VkDescriptorSet> SpareSets[3];

	//! B8G8R8A8 where supported, else RGBA with bytes swapped per copy
	VkFormat TextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
	//! The device samples and copies textures of each 16 bit format
	bool PackedR5G6B5 = false;
	bool PackedA1R5G5B5 = false;
	//! The device builds mip levels of textures in 32 bits, and in each 16 bit format
	bool Mipmaps32 = false;
	bool MipmapsR5G6B5 = false;
	bool MipmapsA1R5G5B5 = false;

	//! Texture copies for the next submit, and per frame those whose staging it still reads
	std::vector<SVulkanTextureWrite> Writes;
	u32 WriteBytes = 0;
	std::vector<SVulkanTextureWrite> Staged[VULKAN_FRAMES_IN_FLIGHT];
	//! Pixels that found no memory, with their mip levels; they try again each frame
	std::vector<std::tuple<VkImage, IImage*, u32> > Waiting;
	//! Textures that found no memory for their image; they try again each frame
	std::vector<CVulkanTexture*> Unmade;
	//! Copies waited for at once, outside the frames
	VkCommandPool UploadPool = VK_NULL_HANDLE;
	VkCommandBuffer OneOff = VK_NULL_HANDLE;

	//! A library stage, with its module once a program uses it
	struct SStage
	{
		std::vector<u32> Words;
		VkShaderModule Module = VK_NULL_HANDLE;
	};
	//! Library stages by name, and every module made from them
	std::unordered_map<std::string, SStage> Modules;
	std::vector<VkShaderModule> StageModules;

	//! What a frame in flight records into and waits on
	struct SSlot
	{
		VkCommandPool Pool = VK_NULL_HANDLE;
		VkCommandBuffer Commands = VK_NULL_HANDLE;
		//! The frame's texture copies, submitted ahead of it
		VkCommandBuffer Uploads = VK_NULL_HANDLE;
		VkFence Done = VK_NULL_HANDLE;
		VkSemaphore ImageReady = VK_NULL_HANDLE;
	};
	SSlot Slots[VULKAN_FRAMES_IN_FLIGHT];
	bool Encoding = false;
	//! A frame is under way and will be submitted
	bool Started = false;
	//! The frame was submitted early, for a screenshot
	bool Submitted = false;

	//! Ring block, with a uniform set for each stage
	struct SRingBlock
	{
		SVulkanBuffer Buffer;
		VkDescriptorSet Uniforms[2] = {};
	};
	//! A staged uniform block: its ring block, null if none, and offset
	struct SPlaced
	{
		SRingBlock* Block = nullptr;
		u32 Offset = 0;
		bool operator!=(const SPlaced& other) const
		{
			return Block != other.Block || Offset != other.Offset;
		}
	};

	std::vector<SRingBlock*> Rings[VULKAN_FRAMES_IN_FLIGHT];
	//! Buffers of meshes and of draws larger than a ring block, freed once no frame reads them
	std::vector<SVulkanBuffer*> Retired[VULKAN_FRAMES_IN_FLIGHT];
	//! Images freed once no frame in flight reads or fills them
	std::vector<SVulkanImage> RetiredImages[VULKAN_FRAMES_IN_FLIGHT];
	std::vector<VkDescriptorSet> RetiredSets[VULKAN_FRAMES_IN_FLIGHT];

	using SMeshes = CDriverMeshBuffers<SVulkanMeshStore, SHWBufferLink>;
	SMeshes Meshes;
	std::vector<SVulkanArena*> Arenas;
	std::vector<SVulkanImageBlock*> ImageBlocks;

	//! Number of the frame being built
	u64 FrameCounter = 0;
	//! Frame whose ring, arena or image memory was refused; it asks no more
	u64 RingRefused = UINT64_MAX;
	u64 ArenaRefused = UINT64_MAX;
	u64 ImageRefused = UINT64_MAX;

	u32 RingBlock = 0;
	u32 RingUsed = 0;
	//! From the frame's first 2D draw on, 3D draws are interface too, as hotbar items are
	bool Interface = false;
	//! Furthest block each frame's ring used; trimRing keeps up to it
	u32 RingReach[VULKAN_FRAMES_IN_FLIGHT] = {};
	//! Slot and ring of the frame being built
	u32 RingIndex = 0;

	//! State last bound on the command buffer
	VkPipeline BoundPipeline = VK_NULL_HANDLE;
	//! Uniform block per stage, vertex first
	SPlaced BoundUniforms[2];
	VkDescriptorSet BoundTextures = VK_NULL_HANDLE;
	STextureSet BoundTextureKey;
	VkBuffer BoundVertices = VK_NULL_HANDLE;
	VkDeviceSize BoundVertexOffset = 0;
	VkBuffer BoundIndices = VK_NULL_HANDLE;
	VkDeviceSize BoundIndexOffset = 0;
	VkIndexType BoundIndexType = VK_INDEX_TYPE_UINT16;
	std::optional<VkRect2D> BoundScissor;
	std::optional<VkCullModeFlags> BoundCull;

	//! Last staged fog block, reused while the fog is the same
	SPlaced FogPlaced;
	SDriverFog Fog = {};
	//! 2D matrices and empty fog, staged once per pass
	SPlaced TwoD[2];
	//! Changes whenever a material's textures may have changed
	u64 MaterialCounter = 0;

	//! 2D quad indices, reused between draws
	core::array<u16> QuadOrder;

	void forgetBindings()
	{
		BoundPipeline = VK_NULL_HANDLE;
		BoundUniforms[0] = BoundUniforms[1] = SPlaced();
		BoundTextures = VK_NULL_HANDLE;
		BoundVertices = VK_NULL_HANDLE;
		BoundIndices = VK_NULL_HANDLE;
		BoundScissor.reset();
		BoundCull.reset();
		FogPlaced = SPlaced();
		TwoD[0] = TwoD[1] = SPlaced();
		SplitVertices = false;
	}

	VkCommandBuffer commands() const { return Slots[RingIndex].Commands; }
};


static s32 memoryTypeFor(const CVulkanDriver::SImpl* impl, u32 allowed,
		VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
{
	for (const VkMemoryPropertyFlags wanted : { required | preferred, required })
		for (u32 i = 0; i < impl->Memory.memoryTypeCount; ++i)
			if ((allowed & (1u << i)) &&
					(impl->Memory.memoryTypes[i].propertyFlags & wanted) == wanted)
				return (s32)i;
	return -1;
}


template <typename O, typename F, typename T>
static void destroyIfMade(O owner, F destroy, T handle)
{
	if (handle)
		destroy(owner, handle, 0);
}


static void destroyBuffer(CVulkanDriver::SImpl* impl, SVulkanBuffer& buffer)
{
	destroyIfMade(impl->Device, impl->Vk.vkDestroyBuffer, buffer.Buffer);
	destroyIfMade(impl->Device, impl->Vk.vkFreeMemory, buffer.Memory);
	buffer = SVulkanBuffer();
}


//! Host-visible, mapped buffer, device-local where possible
static bool createBuffer(CVulkanDriver::SImpl* impl, u32 bytes, VkBufferUsageFlags usage,
		SVulkanBuffer& out)
{
	const VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0,
			bytes + VULKAN_BUFFER_SLACK, usage, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr };
	if (impl->Vk.vkCreateBuffer(impl->Device, &info, 0, &out.Buffer) != VK_SUCCESS)
		return false;

	VkMemoryRequirements needs;
	impl->Vk.vkGetBufferMemoryRequirements(impl->Device, out.Buffer, &needs);
	const s32 type = memoryTypeFor(impl, needs.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	const VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
			needs.size, (u32)type };
	void* mapped = nullptr;
	if (type < 0 || impl->Vk.vkAllocateMemory(impl->Device, &alloc, 0, &out.Memory) != VK_SUCCESS ||
			impl->Vk.vkBindBufferMemory(impl->Device, out.Buffer, out.Memory, 0) != VK_SUCCESS ||
			impl->Vk.vkMapMemory(impl->Device, out.Memory, 0, VK_WHOLE_SIZE, 0,
			&mapped) != VK_SUCCESS)
	{
		destroyBuffer(impl, out);
		return false;
	}

	out.Mapped = (u8*)mapped;
	out.Bytes = bytes;
	return true;
}


//! Returns a range to a sorted free list, merging neighbours
static void freeRange(std::vector<SVulkanRange>& free, u32 base, u32 bytes)
{
	const auto at = free.insert(std::find_if(free.begin(), free.end(),
			[base](const SVulkanRange& range) { return range.Base >= base; }), { base, bytes });
	if (at + 1 != free.end() && at->Base + at->Bytes == (at + 1)->Base)
	{
		at->Bytes += (at + 1)->Bytes;
		free.erase(at + 1);
	}
	if (at != free.begin() && (at - 1)->Base + (at - 1)->Bytes == at->Base)
	{
		(at - 1)->Bytes += at->Bytes;
		free.erase(at);
	}
}


//! First fit among the type's blocks, else a new block
static VkDeviceMemory takeImageRange(CVulkanDriver::SImpl* impl, const VkMemoryRequirements& needs,
		u32 type, SVulkanImage& out)
{
	const u32 bytes = (u32)needs.size;
	const u32 mask = (u32)needs.alignment - 1;
	for (SVulkanImageBlock* block : impl->ImageBlocks)
	{
		if (block->Type != type)
			continue;
		std::vector<SVulkanRange>& free = block->Free;
		for (size_t i = 0; i < free.size(); ++i)
		{
			const auto [start, length] = free[i];
			const u32 at = (start + mask) & ~mask;
			if (at + bytes > start + length)
				continue;
			free.erase(free.begin() + (ptrdiff_t)i);
			if (at + bytes < start + length)
				free.insert(free.begin() + (ptrdiff_t)i,
						{ at + bytes, start + length - at - bytes });
			if (at > start)
				free.insert(free.begin() + (ptrdiff_t)i, { start, at - start });
			block->Used += bytes;
			out.Block = block;
			out.Offset = at;
			out.Bytes = bytes;
			return block->Memory;
		}
	}

	VkDeviceMemory memory = VK_NULL_HANDLE;
	const VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
			VULKAN_IMAGE_BLOCK_BYTES, type };
	if (impl->ImageRefused == impl->FrameCounter ||
			impl->Vk.vkAllocateMemory(impl->Device, &alloc, 0, &memory) != VK_SUCCESS)
	{
		impl->ImageRefused = impl->FrameCounter;
		return VK_NULL_HANDLE;
	}
	impl->ImageBlocks.push_back(new SVulkanImageBlock{ memory, type, 0,
			{ { 0u, VULKAN_IMAGE_BLOCK_BYTES } } });
	return takeImageRange(impl, needs, type, out);
}


static void destroyImage(CVulkanDriver::SImpl* impl, SVulkanImage& image)
{
	destroyIfMade(impl->Device, impl->Vk.vkDestroyImageView, image.View);
	destroyIfMade(impl->Device, impl->Vk.vkDestroyImage, image.Image);
	destroyIfMade(impl->Device, impl->Vk.vkFreeMemory, image.Memory);
	if (SVulkanImageBlock* block = image.Block)
	{
		freeRange(block->Free, image.Offset, image.Bytes);
		block->Used -= image.Bytes;
		if (!block->Used)
		{
			impl->ImageBlocks.erase(std::find(impl->ImageBlocks.begin(), impl->ImageBlocks.end(),
					block));
			impl->Vk.vkFreeMemory(impl->Device, block->Memory, 0);
			delete block;
		}
	}
	image = SVulkanImage();
}


static VkImageAspectFlags aspectsOf(VkFormat format)
{
	const bool stencil = format == VK_FORMAT_D24_UNORM_S8_UINT ||
			format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D16_UNORM_S8_UINT;
	return VK_IMAGE_ASPECT_DEPTH_BIT |
			(stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_NONE);
}


//! lasting: false when memory ran out, so a later try may succeed
static bool makeImage(CVulkanDriver::SImpl* impl, u32 width, u32 height, VkFormat format,
		VkImageUsageFlags usage, SVulkanImage& out, u32 levels = 1, bool* lasting = nullptr)
{
	const bool depth = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

	const VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0,
			VK_IMAGE_TYPE_2D, format, { width, height, 1 }, levels, 1, VK_SAMPLE_COUNT_1_BIT,
			VK_IMAGE_TILING_OPTIMAL, usage, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr,
			VK_IMAGE_LAYOUT_UNDEFINED };
	const VkResult created = impl->Vk.vkCreateImage(impl->Device, &info, 0, &out.Image);
	if (created != VK_SUCCESS)
	{
		if (lasting)
			*lasting = created != VK_ERROR_OUT_OF_HOST_MEMORY &&
					created != VK_ERROR_OUT_OF_DEVICE_MEMORY;
		return false;
	}

	VkMemoryRequirements needs;
	impl->Vk.vkGetImageMemoryRequirements(impl->Device, out.Image, &needs);
	const s32 type = memoryTypeFor(impl, needs.memoryTypeBits, 0,
			(usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) ?
			VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT :
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	const VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
			needs.size, (u32)type };
	VkDeviceMemory memory = VK_NULL_HANDLE;
	if (type >= 0 && !depth && needs.size <= VULKAN_IMAGE_BLOCK_BYTES / 2)
		memory = takeImageRange(impl, needs, (u32)type, out);
	else if (type >= 0 &&
			impl->Vk.vkAllocateMemory(impl->Device, &alloc, 0, &out.Memory) == VK_SUCCESS)
		memory = out.Memory;
	if (!memory || impl->Vk.vkBindImageMemory(impl->Device, out.Image, memory,
			out.Offset) != VK_SUCCESS)
	{
		if (lasting)
			*lasting = type < 0;
		destroyImage(impl, out);
		return false;
	}

	const VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0,
			out.Image, VK_IMAGE_VIEW_TYPE_2D, format, {},
			{ depth ? aspectsOf(format) : VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1 } };
	if (impl->Vk.vkCreateImageView(impl->Device, &view, 0, &out.View) != VK_SUCCESS)
	{
		destroyImage(impl, out);
		return false;
	}
	return true;
}


static void transition(CVulkanDriver::SImpl* impl, VkCommandBuffer commands, VkImage image,
		VkImageLayout from, VkImageLayout to)
{
	const auto useOf = [](VkImageLayout layout, bool source) -> std::pair<VkAccessFlags,
			VkPipelineStageFlags>
	{
		switch (layout)
		{
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return { VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return { VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return { VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return { VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return { VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };
		//! As a source it follows a draw; the present semaphore does the rest
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			if (source)
				return { VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			return { VK_ACCESS_NONE, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
		default:
			return { VK_ACCESS_NONE,
					source ?
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
		}
	};

	//! From UNDEFINED, still waits for the previous use of the image
	const auto [srcAccess, srcStage] = useOf(from == VK_IMAGE_LAYOUT_UNDEFINED ? to : from, true);
	const auto [dstAccess, dstStage] = useOf(to, false);
	const VkImageAspectFlags aspects = from != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
			to != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ?
			VK_IMAGE_ASPECT_COLOR_BIT : aspectsOf(impl->DepthFormat);
	const VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
			srcAccess, dstAccess, from, to,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, { aspects, 0, 1, 0, 1 } };
	impl->Vk.vkCmdPipelineBarrier(commands, srcStage, dstStage, 0, 0, 0, 0, 0, 1, &barrier);
}


static VkCommandBuffer beginOneOff(CVulkanDriver::SImpl* impl)
{
	const VkResult reset = impl->Vk.vkResetCommandPool(impl->Device, impl->UploadPool,
			VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
	const VkResult result = reset != VK_SUCCESS ? reset :
			impl->Vk.vkBeginCommandBuffer(impl->OneOff, &VULKAN_ONE_TIME_SUBMIT);
	return result == VK_SUCCESS ? impl->OneOff : VK_NULL_HANDLE;
}


//! Marks the device lost and says so once, so a crash report carries it
static void loseDevice(CVulkanDriver::SImpl* impl, const c8* where)
{
	if (!impl->Lost)
		os::Printer::log("The Vulkan device was lost at", where, ELL_ERROR);
	impl->Lost = true;
}


static bool waitIdle(CVulkanDriver::SImpl* impl)
{
	const VkResult result = impl->Vk.vkDeviceWaitIdle(impl->Device);
	if (result == VK_ERROR_DEVICE_LOST)
		loseDevice(impl, "vkDeviceWaitIdle");
	return result == VK_SUCCESS;
}


static bool finishOneOff(CVulkanDriver::SImpl* impl)
{
	if (impl->Vk.vkEndCommandBuffer(impl->OneOff) != VK_SUCCESS)
		return false;
	const VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1,
			&impl->OneOff, 0, nullptr };
	const VkResult result = impl->Vk.vkQueueSubmit(impl->Queue, 1, &submit, VK_NULL_HANDLE);
	if (result == VK_ERROR_DEVICE_LOST)
		loseDevice(impl, "vkQueueSubmit for copies");
	return result == VK_SUCCESS && waitIdle(impl);
}


static std::pair<SVulkanArena*, u32> takeArenaRange(CVulkanDriver::SImpl* impl, u32 bytes);
static void giveArenaRange(CVulkanDriver::SImpl* impl, SVulkanArena* arena, u32 base, u32 bytes);
static void retireTexture(CVulkanDriver::SImpl* impl, SVulkanImage& image);
static bool submitFrame(CVulkanDriver::SImpl* impl);


static void recordWrites(CVulkanDriver::SImpl* impl, VkCommandBuffer commands)
{
	//! A barrier per image before the copies, and per layout its levels end in after them
	std::vector<VkImageMemoryBarrier> before, after;
	for (const SVulkanTextureWrite& write : impl->Writes)
	{
		before.push_back({ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_NONE,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				write.First ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				write.Image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, write.Levels, 0, 1 } });
		//! The levels a blit read end as transfer sources, the last one as the destination
		if (write.Levels > 1 && write.Arena)
		{
			after.push_back(before.back());
			after.back().srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			after.back().oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			after.back().subresourceRange.levelCount = write.Levels - 1;
		}
		after.push_back(before.back());
		after.back().srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		after.back().oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		if (write.Arena)
		{
			after.back().subresourceRange.baseMipLevel = write.Levels - 1;
			after.back().subresourceRange.levelCount = 1;
		}
	}
	for (VkImageMemoryBarrier& barrier : after)
	{
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	impl->Vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, (u32)before.size(), before.data());
	const VkClearColorValue clear = {};
	for (const SVulkanTextureWrite& write : impl->Writes)
	{
		if (!write.Arena)
		{
			const VkImageSubresourceRange all =
					{ VK_IMAGE_ASPECT_COLOR_BIT, 0, write.Levels, 0, 1 };
			impl->Vk.vkCmdClearColorImage(commands, write.Image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &all);
			continue;
		}
		const VkBufferImageCopy copy = { write.Offset, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
				{}, { write.Width, write.Height, 1 } };
		impl->Vk.vkCmdCopyBufferToImage(commands, write.Arena->Buffer.Buffer, write.Image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	}
	for (const SVulkanTextureWrite& write : impl->Writes)
		for (u32 level = 1; write.Arena && level < write.Levels; ++level)
		{
			const VkImageMemoryBarrier source = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED,
					write.Image, { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, 1 } };
			impl->Vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &source);
			VkImageBlit blit = {};
			blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 };
			blit.srcOffsets[1] = { (s32)core::max_(write.Width >> (level - 1), 1u),
					(s32)core::max_(write.Height >> (level - 1), 1u), 1 };
			blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
			blit.dstOffsets[1] = { (s32)core::max_(write.Width >> level, 1u),
					(s32)core::max_(write.Height >> level, 1u), 1 };
			impl->Vk.vkCmdBlitImage(commands, write.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					write.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
		}
	impl->Vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, (u32)after.size(), after.data());
}


//! Runs the pending copies at once and frees their staging; failed ones wait for the next try
static void flushWrites(CVulkanDriver::SImpl* impl)
{
	if (impl->Writes.empty() || !beginOneOff(impl))
		return;
	recordWrites(impl, impl->OneOff);
	if (!finishOneOff(impl))
		return;
	for (const SVulkanTextureWrite& write : impl->Writes)
		giveArenaRange(impl, write.Arena, write.Base, write.Bytes);
	impl->Writes.clear();
	impl->WriteBytes = 0;
}


//! Vulkan format of a texture; the 16 bit layouts match bit for bit
static VkFormat textureFormatOf(const CVulkanDriver::SImpl* impl, ECOLOR_FORMAT format)
{
	if (format == ECF_R5G6B5)
		return VK_FORMAT_R5G6B5_UNORM_PACK16;
	if (format == ECF_A1R5G5B5)
		return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
	return impl->TextureFormat;
}


//! Whether the device samples and copies textures of a 16 bit format
static bool packs(const CVulkanDriver::SImpl* impl, ECOLOR_FORMAT format)
{
	return format == ECF_R5G6B5 ? impl->PackedR5G6B5 :
			format == ECF_A1R5G5B5 && impl->PackedA1R5G5B5;
}


static bool buildsMipmaps(const CVulkanDriver::SImpl* impl, ECOLOR_FORMAT format)
{
	return format == ECF_R5G6B5 ? impl->MipmapsR5G6B5 :
			format == ECF_A1R5G5B5 ? impl->MipmapsA1R5G5B5 : impl->Mipmaps32;
}


//! Stages pixels for the next upload, false without memory; A8R8G8B8 is B8G8R8A8 there
static bool stageTextureWrite(CVulkanDriver::SImpl* impl, VkImage image,
		const IImage* pixels, bool first, u32 levels)
{
	SVulkanTextureWrite write{ image, levels, first };
	const auto [width, height] = pixels->getDimension();
	const u32 bytes = (u32)pixels->getImageDataSizeInBytes();
	//! Vulkan requires copy offsets aligned to 4 bytes at least
	const u32 align = (u32)core::max_(impl->Properties.limits.optimalBufferCopyOffsetAlignment,
			(VkDeviceSize)4);
	write.Bytes = (bytes + align - 1 + VULKAN_ARENA_ALIGN -
			1) / VULKAN_ARENA_ALIGN * VULKAN_ARENA_ALIGN;
	std::tie(write.Arena, write.Base) = takeArenaRange(impl, write.Bytes);
	if (!write.Arena)
		return false;
	write.Offset = (write.Base + align - 1) / align * align;

	u8* const staged = write.Arena->Buffer.Mapped + write.Offset;
	if (pixels->getColorFormat() != ECF_A8R8G8B8 || impl->TextureFormat == VK_FORMAT_B8G8R8A8_UNORM)
		memcpy(staged, pixels->getData(), bytes);
	else
		CColorConverter::convert_A8R8G8B8toA8B8G8R8(pixels->getData(), width * height, staged);

	write.Width = width;
	write.Height = height;
	const auto waiting = std::find_if(impl->Writes.begin(), impl->Writes.end(),
			[image](const SVulkanTextureWrite& other) { return other.Image == image; });
	if (waiting != impl->Writes.end())
	{
		write.First = waiting->First;
		impl->WriteBytes -= waiting->Bytes;
		giveArenaRange(impl, waiting->Arena, waiting->Base, waiting->Bytes);
		*waiting = write;
	}
	else
		impl->Writes.push_back(write);
	impl->WriteBytes += write.Bytes;

	//! Bounds the staging that waits for a frame
	if (impl->Writes.size() >= 64 || impl->WriteBytes >= (8 << 20))
		flushWrites(impl);
	return true;
}


static void dropWaiting(CVulkanDriver::SImpl* impl, VkImage image)
{
	auto& waiting = impl->Waiting;
	for (size_t i = 0; i < waiting.size();)
		if (std::get<0>(waiting[i]) == image)
		{
			std::get<1>(waiting[i])->drop();
			waiting.erase(waiting.begin() + (ptrdiff_t)i);
		}
		else
			++i;
}


//! Pixels without memory wait for a frame with it; a new image is transparent till then
static void queueTextureWrite(CVulkanDriver::SImpl* impl, VkImage image,
		const IImage* pixels, bool first, u32 levels = 1)
{
	dropWaiting(impl, image);
	const core::dimension2du size = pixels->getDimension();
	if (!image || !size.Width || !size.Height ||
			stageTextureWrite(impl, image, pixels, first, levels))
		return;

	IImage* copy = new CImage(pixels->getColorFormat(), size, pixels->getData(), false);
	impl->Waiting.emplace_back(image, copy, levels);
	if (first)
		impl->Writes.push_back({ image, levels, true });
}


//! Retries waiting pixels once a frame; a failure goes to the back and ends the loop
static void retryWaiting(CVulkanDriver::SImpl* impl)
{
	auto& waiting = impl->Waiting;
	for (size_t left = waiting.size(); left; --left)
	{
		const auto [image, pixels, levels] = waiting.front();
		waiting.erase(waiting.begin());
		if (!stageTextureWrite(impl, image, pixels, false, levels))
		{
			waiting.emplace_back(image, pixels, levels);
			return;
		}
		pixels->drop();
	}
}


static bool readTexture(CVulkanDriver::SImpl* impl, VkImage image, IImage* out)
{
	flushWrites(impl);

	const auto [width, height] = out->getDimension();
	const u32 bytes = (u32)out->getImageDataSizeInBytes();
	const VkCommandBuffer commands = beginOneOff(impl);
	SVulkanBuffer staging;
	if (!commands || !createBuffer(impl, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging))
		return false;

	transition(impl, commands, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	const VkBufferImageCopy copy = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, {},
			{ width, height, 1 } };
	impl->Vk.vkCmdCopyImageToBuffer(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			staging.Buffer, 1, &copy);
	transition(impl, commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	const bool read = finishOneOff(impl);
	if (read)
	{
		if (out->getColorFormat() != ECF_A8R8G8B8 ||
				impl->TextureFormat == VK_FORMAT_B8G8R8A8_UNORM)
			memcpy(out->getData(), staging.Mapped, bytes);
		else
			CColorConverter::convert_A8R8G8B8toA8B8G8R8(staging.Mapped, width * height,
					out->getData());
	}

	destroyBuffer(impl, staging);
	return read;
}


class CVulkanTexture : public CDriverTexture
{
public:
	CVulkanTexture(const io::path& name, IImage* image, CVulkanDriver* driver, bool keepImage)
		: CDriverTexture(name, EDT_VULKAN, image,
				textureFormatFor(driver, packs(driver->Impl, image->getColorFormat()),
						image->getColorFormat()),
				keepImage),
		Driver(driver)
	{
		HasMipMaps = Driver->getTextureCreationFlag(ETCF_CREATE_MIP_MAPS) &&
				(Size.Width > 1 || Size.Height > 1) && buildsMipmaps(Driver->Impl, ColorFormat);
		for (u32 side = core::max_(Size.Width, Size.Height); HasMipMaps && side > 1; side >>= 1)
			++Levels;

		const core::dimension2du largest = Driver->getMaxTextureSize();
		if (!Size.Width || !Size.Height || Size.Width > largest.Width ||
				Size.Height > largest.Height)
			os::Printer::log(
					"Vulkan leaves empty a texture of no size or larger than the device takes",
					name.c_str(), ELL_ERROR);
		else if (bool lasting = false; !make(Image ? Image : image, &lasting))
		{
			//! Without memory the pixels wait for a frame that has it; else it stays empty
			if (!lasting)
			{
				if (!Image)
				{
					Image = new CImage(ColorFormat, Size);
					image->copyTo(Image);
				}
				Keep = keepImage;
				Driver->Impl->Unmade.push_back(this);
				return;
			}
			os::Printer::log("Could not make a Vulkan texture for", name.c_str(), ELL_ERROR);
		}
		settle(keepImage);
	}

	~CVulkanTexture() IRR_OVERRIDE
	{
		std::vector<CVulkanTexture*>& unmade = Driver->Impl->Unmade;
		unmade.erase(std::remove(unmade.begin(), unmade.end(), this), unmade.end());
		retireTexture(Driver->Impl, Pixels);
	}

	//! Retries making the image; false while the texture still waits for memory
	bool remake()
	{
		bool lasting = false;
		if (!make(Image, &lasting) && !lasting)
			return false;
		settle(Keep);
		return true;
	}

	VkImageView view() const { return Pixels.View; }

private:
	bool made() const IRR_OVERRIDE { return Pixels.Image != VK_NULL_HANDLE; }
	void read(IImage* into) IRR_OVERRIDE { readTexture(Driver->Impl, Pixels.Image, into); }
	void write(const IImage* pixels) IRR_OVERRIDE
	{
		queueTextureWrite(Driver->Impl, Pixels.Image, pixels, false, Levels);
	}

	bool make(const IImage* pixels, bool* lasting)
	{
		const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (!makeImage(Driver->Impl, Size.Width, Size.Height,
				textureFormatOf(Driver->Impl, ColorFormat), usage, Pixels, Levels, lasting))
			return false;
		queueTextureWrite(Driver->Impl, Pixels.Image, pixels, true, Levels);
		return true;
	}

	CVulkanDriver* Driver = nullptr;
	SVulkanImage Pixels;
	u32 Levels = 1;
	//! Whether the game keeps the image, for a texture that waits for memory
	bool Keep = false;
};


static void releaseAll(CVulkanDriver::SImpl* impl, std::vector<SVulkanBuffer*>& list)
{
	for (SVulkanBuffer* buffer : list)
	{
		destroyBuffer(impl, *buffer);
		delete buffer;
	}
	list.clear();
}


//! ECFN_DISABLED maps to ALWAYS; depth writes go off separately
static const VkCompareOp VULKAN_COMPARES[ECFN_NEVER + 1] = {
	VK_COMPARE_OP_ALWAYS,           // ECFN_DISABLED
	VK_COMPARE_OP_LESS_OR_EQUAL,    // ECFN_LESSEQUAL
	VK_COMPARE_OP_EQUAL,            // ECFN_EQUAL
	VK_COMPARE_OP_LESS,             // ECFN_LESS
	VK_COMPARE_OP_NOT_EQUAL,        // ECFN_NOTEQUAL
	VK_COMPARE_OP_GREATER_OR_EQUAL, // ECFN_GREATEREQUAL
	VK_COMPARE_OP_GREATER,          // ECFN_GREATER
	VK_COMPARE_OP_ALWAYS,           // ECFN_ALWAYS
	VK_COMPARE_OP_NEVER,            // ECFN_NEVER
};

//! Factors past EBF_SRC_ALPHA_SATURATE read as zero, as OpenGL's
static const VkBlendFactor VULKAN_BLEND_FACTORS[16] = {
	VK_BLEND_FACTOR_ZERO,                // EBF_ZERO
	VK_BLEND_FACTOR_ONE,                 // EBF_ONE
	VK_BLEND_FACTOR_DST_COLOR,           // EBF_DST_COLOR
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, // EBF_ONE_MINUS_DST_COLOR
	VK_BLEND_FACTOR_SRC_COLOR,           // EBF_SRC_COLOR
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // EBF_ONE_MINUS_SRC_COLOR
	VK_BLEND_FACTOR_SRC_ALPHA,           // EBF_SRC_ALPHA
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // EBF_ONE_MINUS_SRC_ALPHA
	VK_BLEND_FACTOR_DST_ALPHA,           // EBF_DST_ALPHA
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, // EBF_ONE_MINUS_DST_ALPHA
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,  // EBF_SRC_ALPHA_SATURATE
};

//! Core Vulkan has no factor or alpha variants of min and max, as OpenGL without their extensions
static const VkBlendOp VULKAN_BLEND_OPERATIONS[EBO_MAX_ALPHA + 1] = {
	VK_BLEND_OP_ADD,              // EBO_NONE
	VK_BLEND_OP_ADD,              // EBO_ADD
	VK_BLEND_OP_SUBTRACT,         // EBO_SUBTRACT
	VK_BLEND_OP_REVERSE_SUBTRACT, // EBO_REVSUBTRACT
	VK_BLEND_OP_MIN,              // EBO_MIN
	VK_BLEND_OP_MAX,              // EBO_MAX
	VK_BLEND_OP_MIN,              // EBO_MIN_FACTOR
	VK_BLEND_OP_MAX,              // EBO_MAX_FACTOR
	VK_BLEND_OP_MIN,              // EBO_MIN_ALPHA
	VK_BLEND_OP_MAX,              // EBO_MAX_ALPHA
};


static VkPipeline buildPipeline(CVulkanDriver::SImpl* impl, const SVulkanShaderProgram* program,
		u32 vType, u32 topology, u32 blend, bool textured, bool alphaTest,
		u32 cull, u32 compare, bool write, bool wire, u32 alphaSource, VkResult& result)
{
	//! Both stages get every constant; each ignores the IDs it does not declare
	std::vector<u32> constants = { textured ? VK_TRUE : VK_FALSE, alphaTest ? VK_TRUE : VK_FALSE,
			alphaSource };
	constants.insert(constants.end(), impl->GameConstants.begin(), impl->GameConstants.end());
	std::vector<VkSpecializationMapEntry> entries = { { 0, 0, 4 }, { 1, 4, 4 }, { 3, 8, 4 } };
	for (u32 i = 0; i < impl->GameConstants.size(); ++i)
		entries.push_back({ 16 + i, 12 + 4 * i, 4 });
	const VkSpecializationInfo specialization = { (u32)entries.size(), entries.data(),
			constants.size() * sizeof(u32), constants.data() };

	const VkShaderModule* const modules = program ? program->Modules : impl->Fixed;
	const VkPipelineShaderStageCreateInfo stages[2] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				VK_SHADER_STAGE_VERTEX_BIT, modules[0], "main", &specialization },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				VK_SHADER_STAGE_FRAGMENT_BIT, modules[1], "main", &specialization } };

	//! Missing attributes alias the first coordinate and the normal
	const bool packed = vType == (u32)EVT_STANDARD_PACKED;
	const u32 pitch = getVertexPitchFromType((E_VERTEX_TYPE)vType);
	const bool split = impl->SplitVertices;
	const VkVertexInputBindingDescription bindings[2] = {
		{ 0, split ? 12 : pitch, VK_VERTEX_INPUT_RATE_VERTEX },
		{ 1, pitch - 12, VK_VERTEX_INPUT_RATE_VERTEX } };
	VkVertexInputAttributeDescription attributes[EVA_COUNT] = {
		{ EVA_POSITION, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
		{ EVA_NORMAL, 0, packed ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R32G32B32_SFLOAT, 12 },
		//! Raw bytes; the shaders swizzle them to RGBA
		{ EVA_COLOR, 0, VK_FORMAT_R8G8B8A8_UNORM, packed ? 16u : 24u },
		{ EVA_TCOORD0, 0, VK_FORMAT_R32G32_SFLOAT, packed ? 20u : 28u },
		{ EVA_TCOORD1, 0, VK_FORMAT_R32G32_SFLOAT,
				vType == EVT_2TCOORDS ? 36u : packed ? 20u : 28u },
		{ EVA_TANGENT, 0, VK_FORMAT_R32G32B32_SFLOAT, vType == EVT_TANGENTS ? 36u : 12u },
		{ EVA_BINORMAL, 0, VK_FORMAT_R32G32B32_SFLOAT, vType == EVT_TANGENTS ? 48u : 12u } };
	const u32 inputs = program ? program->Inputs : impl->FixedInputs;
	u32 count = 0;
	for (VkVertexInputAttributeDescription each : attributes)
	{
		if (!(inputs >> each.location & 1))
			continue;
		if (split && each.location != EVA_POSITION)
		{
			each.binding = 1;
			each.offset -= 12;
		}
		attributes[count++] = each;
	}
	const VkPipelineVertexInputStateCreateInfo vertexInput =
			{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0,
			!count ? 0u : split ? 2u : 1u, bindings, count, attributes };

	VkPipelineInputAssemblyStateCreateInfo assembly = {};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = (VkPrimitiveTopology)topology;

	const VkPipelineViewportStateCreateInfo viewport =
			{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1,
			nullptr };

	VkPipelineRasterizationStateCreateInfo raster = {};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = wire &&
			impl->Features.fillModeNonSolid ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	raster.cullMode = cull;
	//! Irrlicht's front faces are clockwise
	raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
	raster.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = {};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	const VkPipelineDepthStencilStateCreateInfo depth =
			{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0,
			VK_TRUE, write ? VK_TRUE : VK_FALSE, VULKAN_COMPARES[compare],
			VK_FALSE, VK_FALSE, {}, {}, 0.0f, 0.0f };

	VkPipelineColorBlendAttachmentState attachment = {};
	attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	if (blend)
	{
		attachment.blendEnable = VK_TRUE;
		attachment.colorBlendOp = VULKAN_BLEND_OPERATIONS[blend >> 16];
		attachment.alphaBlendOp = VULKAN_BLEND_OPERATIONS[blend >> 16];
		attachment.srcColorBlendFactor = VULKAN_BLEND_FACTORS[blend >> 4 & 15];
		attachment.dstColorBlendFactor = VULKAN_BLEND_FACTORS[blend & 15];
		attachment.srcAlphaBlendFactor = VULKAN_BLEND_FACTORS[blend >> 12 & 15];
		attachment.dstAlphaBlendFactor = VULKAN_BLEND_FACTORS[blend >> 8 & 15];
	}
	const VkPipelineColorBlendStateCreateInfo colorBlend =
			{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_LOGIC_OP_CLEAR, 1, &attachment, {} };

	const VkDynamicState dynamics[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_CULL_MODE };
	const VkPipelineDynamicStateCreateInfo dynamic =
			{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
			impl->DynamicCull ? 3u : 2u, dynamics };
	const VkPipelineRenderingCreateInfo rendering =
			{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1,
			&impl->ColorFormat, impl->DepthFormat, VK_FORMAT_UNDEFINED };

	const VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			impl->Use13 ? &rendering : nullptr, 0, 2,
			stages, &vertexInput, &assembly, nullptr, &viewport, &raster, &multisample, &depth,
			&colorBlend, &dynamic, impl->Layout,
			impl->Passes[1], 0, VK_NULL_HANDLE, 0 };

	VkPipeline pipeline = VK_NULL_HANDLE;
	result = impl->Vk.vkCreateGraphicsPipelines(impl->Device, impl->Cache, 1, &info, 0, &pipeline);
	return result == VK_SUCCESS ? pipeline : VK_NULL_HANDLE;
}


static VkPipeline pipelineFor(CVulkanDriver::SImpl* impl, const SVulkanShaderProgram* program,
		u32 programKey, u32 vType, u32 topology, u32 blend, bool textured, bool alphaTest,
		u32 cull, u32 compare, bool write, bool wire, u32 alphaSource = 0)
{
	if (vType > (u32)EVT_STANDARD_PACKED || blend >> 16 > (u32)EBO_MAX_ALPHA)
		return VK_NULL_HANDLE;

	if (impl->DynamicCull)
	{
		if (impl->Encoding && impl->BoundCull != cull)
			impl->Vk.vkCmdSetCullMode(impl->commands(), *(impl->BoundCull = cull));
		cull = 0;
	}

	const u64 key = programKey | (u64)vType << 16 | (u64)topology << 18 | (u64)textured << 23 |
			(u64)alphaTest << 24 | (u64)impl->SplitVertices << 25 | (u64)cull << 32 |
			(u64)compare << 34 | (u64)write << 38 | (u64)wire << 39 | (u64)alphaSource << 40 |
			(u64)blend << 42;
	if (key == impl->LastPipelineKey)
		return impl->LastPipeline;
	auto found = impl->Pipelines.find(key);
	if (found == impl->Pipelines.end())
	{
		//! A failed build is cached as none, unless memory ran out
		VkResult result = VK_SUCCESS;
		const VkPipeline pipeline = buildPipeline(impl, program, vType, topology, blend, textured,
				alphaTest, cull, compare, write, wire, alphaSource, result);
		if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
			return VK_NULL_HANDLE;
		if (!pipeline)
			os::Printer::log("Could not build a Vulkan pipeline for",
					program ? program->Name.c_str() : "the fixed pipeline", ELL_ERROR);
		found = impl->Pipelines.emplace(key, pipeline).first;
	}
	impl->LastPipelineKey = key;
	impl->LastPipeline = found->second;
	return found->second;
}


static VkSampler samplerFor(CVulkanDriver::SImpl* impl, const SMaterialLayer& layer, bool mipmaps)
{
	const u32 key = (layer.BilinearFilter ? 1 : 0) | (layer.TrilinearFilter ? 2 : 0) |
			(mipmaps ? 4 : 0) | ((u32)core::min_((u32)layer.AnisotropicFilter, 16u) << 16) |
			((u32)layer.TextureWrapU << 3) | ((u32)layer.TextureWrapV << 7);
	if (const auto found = impl->Samplers.find(key); found != impl->Samplers.end())
		return found->second;

	const bool smooth = layer.BilinearFilter || layer.TrilinearFilter;
	VkSamplerCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter = smooth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	info.minFilter = info.magFilter;
	//! Nearest keeps a material without mipmaps on level 0 under maxLod
	info.mipmapMode = mipmaps &&
			layer.TrilinearFilter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	//! Mirror modes never reach here
	info.addressModeU = layer.TextureWrapU == ETC_REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT :
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	info.addressModeV = layer.TextureWrapV == ETC_REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT :
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (layer.AnisotropicFilter > 1 && impl->Features.samplerAnisotropy)
	{
		info.anisotropyEnable = VK_TRUE;
		info.maxAnisotropy = core::min_((f32)layer.AnisotropicFilter,
				impl->Properties.limits.maxSamplerAnisotropy);
	}
	//! Above 0 keeps minFilter in use
	info.maxLod = mipmaps ? VK_LOD_CLAMP_NONE : 0.25f;

	VkSampler sampler = VK_NULL_HANDLE;
	if (impl->Vk.vkCreateSampler(impl->Device, &info, 0, &sampler) != VK_SUCCESS)
		return impl->Sampler2D;

	impl->Samplers[key] = sampler;
	return sampler;
}


static VkDescriptorSet allocateSet(CVulkanDriver::SImpl* impl, u32 layout)
{
	std::vector<VkDescriptorSet>& spare = impl->SpareSets[layout];
	if (!spare.empty())
	{
		const VkDescriptorSet set = spare.back();
		spare.pop_back();
		return set;
	}

	if (!impl->PoolSetsLeft)
	{
		const VkDescriptorPoolSize sizes[2] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VULKAN_POOL_SETS },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VULKAN_POOL_SETS * VULKAN_LAYERS } };
		const VkDescriptorPoolCreateInfo made = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				nullptr, 0, VULKAN_POOL_SETS, 2, sizes };
		VkDescriptorPool pool = std::exchange(impl->SparePool, VK_NULL_HANDLE);
		const VkResult created = pool ? VK_SUCCESS :
				impl->Vk.vkCreateDescriptorPool(impl->Device, &made, 0, &pool);
		if (created != VK_SUCCESS)
			return VK_NULL_HANDLE;
		impl->SetPools.push_back(pool);
		impl->PoolSetsLeft = VULKAN_POOL_SETS;
		VkDescriptorPool next = VK_NULL_HANDLE;
		if (impl->Vk.vkCreateDescriptorPool(impl->Device, &made, 0, &next) == VK_SUCCESS)
			impl->SparePool = next;
	}

	const VkDescriptorSetAllocateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			nullptr, impl->SetPools.back(), 1, &impl->SetLayouts[layout] };
	VkDescriptorSet set = VK_NULL_HANDLE;
	if (impl->Vk.vkAllocateDescriptorSets(impl->Device, &info, &set) != VK_SUCCESS)
	{
		impl->PoolSetsLeft = 0;
		return VK_NULL_HANDLE;
	}
	--impl->PoolSetsLeft;
	return set;
}


static VkDescriptorSet textureSetFor(CVulkanDriver::SImpl* impl,
		const CVulkanDriver::SImpl::STextureSet& textures)
{
	if (const auto found = impl->TextureSets.find(textures); found != impl->TextureSets.end())
		return found->second;

	const VkDescriptorSet set = allocateSet(impl, 2);
	if (!set)
		return VK_NULL_HANDLE;

	VkDescriptorImageInfo images[VULKAN_LAYERS];
	VkWriteDescriptorSet writes[VULKAN_LAYERS];
	for (u32 i = 0; i < VULKAN_LAYERS; ++i)
	{
		images[i] = { textures.Samplers[i], textures.Views[i],
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, i, 0, 1,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[i], nullptr, nullptr };
	}
	impl->Vk.vkUpdateDescriptorSets(impl->Device, VULKAN_LAYERS, writes, 0, 0);
	impl->TextureSets[textures] = set;
	return set;
}


static void freeRingBlock(CVulkanDriver::SImpl* impl, CVulkanDriver::SImpl::SRingBlock* block)
{
	for (u32 s = 0; s < 2; ++s)
		if (block->Uniforms[s])
			impl->SpareSets[s].push_back(block->Uniforms[s]);
	destroyBuffer(impl, block->Buffer);
	delete block;
}


static CVulkanDriver::SImpl::SRingBlock* newRingBlock(CVulkanDriver::SImpl* impl)
{
	SVulkanBuffer buffer;
	if (!createBuffer(impl, VULKAN_RING_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, buffer))
		return nullptr;
	auto* block = new CVulkanDriver::SImpl::SRingBlock{ buffer };

	const VkDescriptorBufferInfo info = { buffer.Buffer, 0, VULKAN_UNIFORM_RANGE };
	VkWriteDescriptorSet writes[2];
	for (u32 s = 0; s < 2; ++s)
	{
		block->Uniforms[s] = allocateSet(impl, s);
		if (!block->Uniforms[s])
		{
			freeRingBlock(impl, block);
			return nullptr;
		}
		writes[s] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, block->Uniforms[s], 0, 0, 1,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &info, nullptr };
	}
	impl->Vk.vkUpdateDescriptorSets(impl->Device, 2, writes, 0, 0);
	return block;
}


static void trimRing(CVulkanDriver::SImpl* impl)
{
	std::vector<CVulkanDriver::SImpl::SRingBlock*>& blocks = impl->Rings[impl->RingIndex];
	const size_t wanted = impl->RingReach[impl->RingIndex] + 1;
	if (blocks.size() > wanted)
	{
		freeRingBlock(impl, blocks.back());
		blocks.pop_back();
	}
	impl->RingReach[impl->RingIndex] = 0;
}


//! Copies into the ring, reserving reach bytes if more; power-of-2 alignment
static std::pair<VkBuffer, u32> stageInto(CVulkanDriver::SImpl* impl, const void* data, u32 bytes,
		u32 alignment = 4, bool twoD = false, u32 reach = 0)
{
	reach = core::max_(reach, bytes);
	impl->Interface = impl->Interface || twoD;
	const u32 room = impl->Interface ? VULKAN_RING_BYTES : VULKAN_RING_BYTES - VULKAN_RING_2D_BYTES;
	const bool refused = impl->RingRefused == impl->FrameCounter;

	if (reach > room)
	{
		SVulkanBuffer one;
		if (refused || !createBuffer(impl, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT, one))
		{
			impl->RingRefused = impl->FrameCounter;
			return {};
		}
		memcpy(one.Mapped, data, bytes);
		impl->Retired[impl->RingIndex].push_back(new SVulkanBuffer(one));
		return { one.Buffer, 0 };
	}

	std::vector<CVulkanDriver::SImpl::SRingBlock*>& blocks = impl->Rings[impl->RingIndex];
	u32 offset = (impl->RingUsed + alignment - 1) & ~(alignment - 1);

	if (impl->RingBlock >= blocks.size() || offset + reach > room)
	{
		const u32 next = impl->RingBlock + (impl->RingBlock < blocks.size() ? 1 : 0);
		//! On a refusal the current block stays, keeping its end for 2D
		if (next >= blocks.size())
		{
			CVulkanDriver::SImpl::SRingBlock* block =
					refused ? nullptr : newRingBlock(impl);
			if (!block)
			{
				impl->RingRefused = impl->FrameCounter;
				return {};
			}
			blocks.push_back(block);
		}
		impl->RingBlock = next;
		offset = 0;
	}

	if (impl->RingBlock > impl->RingReach[impl->RingIndex])
		impl->RingReach[impl->RingIndex] = impl->RingBlock;

	const SVulkanBuffer& buffer = blocks[impl->RingBlock]->Buffer;
	memcpy(buffer.Mapped + offset, data, bytes);
	impl->RingUsed = offset + bytes;
	return { buffer.Buffer, offset };
}


static CVulkanDriver::SImpl::SPlaced stageBlock(CVulkanDriver::SImpl* impl,
		const void* data, u32 bytes, bool twoD = false)
{
	const u32 alignment = (u32)core::max_((u64)4,
			(u64)impl->Properties.limits.minUniformBufferOffsetAlignment);
	const auto [buffer, at] = stageInto(impl, data, bytes, alignment, twoD, VULKAN_UNIFORM_RANGE);
	//! A uniform block always lies in the current ring block
	return { buffer ? impl->Rings[impl->RingIndex][impl->RingBlock] : nullptr, at };
}


//! Best fit in the fullest arena with room, else a new arena
static std::pair<SVulkanArena*, u32> takeArenaRange(CVulkanDriver::SImpl* impl, u32 bytes)
{
	SVulkanArena* best = nullptr;
	size_t range = 0;
	for (SVulkanArena* arena : impl->Arenas)
	{
		if (arena->Draining || (best && arena->Used < best->Used))
			continue;
		const std::vector<SVulkanRange>& free = arena->Free;
		for (size_t i = 0; i < free.size(); ++i)
			if (free[i].Bytes >= bytes &&
					(best != arena || free[i].Bytes < best->Free[range].Bytes))
			{
				best = arena;
				range = i;
			}
	}

	if (best)
	{
		SVulkanRange& free = best->Free[range];
		const u32 base = free.Base;
		free.Base += bytes;
		free.Bytes -= bytes;
		if (!free.Bytes)
			best->Free.erase(best->Free.begin() + (ptrdiff_t)range);
		best->Used += bytes;
		return { best, base };
	}
	if (impl->ArenaRefused == impl->FrameCounter)
		return {};

	SVulkanArena* arena = new SVulkanArena();
	arena->Bytes = core::max_(VULKAN_ARENA_BYTES, bytes) / VULKAN_ARENA_ALIGN * VULKAN_ARENA_ALIGN;
	if (!createBuffer(impl, arena->Bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, arena->Buffer))
	{
		impl->ArenaRefused = impl->FrameCounter;
		delete arena;
		return {};
	}
	impl->Arenas.push_back(arena);
	if (arena->Bytes > bytes)
		arena->Free.push_back({ bytes, arena->Bytes - bytes });
	arena->Used = bytes;
	return { arena, 0 };
}


//! An emptied arena, unless the only one, takes no new ranges and is freed next frame
static void giveArenaRange(CVulkanDriver::SImpl* impl, SVulkanArena* arena, u32 base, u32 bytes)
{
	if (!arena)
		return;
	freeRange(arena->Free, base, bytes);
	arena->Used -= bytes;
	if (!arena->Used && impl->Arenas.size() > 1)
		arena->Draining = true;
}


//! Destroys the arenas that emptied a frame ago; the last one stays and takes ranges again
static void dropEmptyArenas(CVulkanDriver::SImpl* impl)
{
	std::vector<SVulkanArena*>& arenas = impl->Arenas;
	for (size_t i = 0; i < arenas.size();)
	{
		SVulkanArena* arena = arenas[i];
		if (arena->Used || arenas.size() < 2)
		{
			arena->Draining &= arena->Used != 0;
			++i;
			continue;
		}
		arenas.erase(arenas.begin() + (ptrdiff_t)i);
		destroyBuffer(impl, arena->Buffer);
		delete arena;
	}
}


bool SVulkanMeshStore::make(SRange& range, u32 bytes)
{
	std::tie(range.Arena, range.Base) = takeArenaRange(Impl, bytes);
	return range.Arena != 0;
}


void SVulkanMeshStore::free(const SRange& range, u32 bytes)
{
	giveArenaRange(Impl, range.Arena, range.Base, bytes);
}


bool SVulkanMeshStore::drain()
{
	std::vector<SVulkanArena*>& arenas = Impl->Arenas;
	if (arenas.size() < 2)
		return false;

	SVulkanArena* sparse = nullptr;
	u64 room = 0;
	for (SVulkanArena* arena : arenas)
	{
		if (arena->Draining)
			return false;
		room += arena->Bytes - arena->Used;
		if (!sparse || arena->Used < sparse->Used)
			sparse = arena;
	}
	//! Copies out of mapped memory are slow reads
	if (sparse->Used > VULKAN_DRAIN_BYTES || room - (sparse->Bytes - sparse->Used) < sparse->Used)
		return false;

	sparse->Draining = true;
	CVulkanDriver::SImpl::SMeshes& meshes = Impl->Meshes;
	std::vector<CVulkanDriver::SImpl::SMeshes::SPool*> pools;
	for (CVulkanDriver::SImpl::SMeshes::SPool* pool : meshes.Pools)
		if (pool->Range.Arena == sparse)
			pools.push_back(pool);
	bool moving = false;
	for (CVulkanDriver::SImpl::SMeshes::SPool* pool : pools)
	{
		if (!pool->Used)
		{
			meshes.drop(pool);
			continue;
		}
		pool->Draining = true;
		meshes.place(pool);
		moving = true;
	}
	return moving;
}


void SVulkanMeshStore::retire(SHandle& buffer)
{
	if (buffer)
		Impl->Retired[Impl->RingIndex].push_back(std::exchange(buffer, nullptr));
}


bool SVulkanMeshStore::fillOwn(SHandle& buffer, u64 drawnAt, const void* data, u32 bytes)
{
	const bool inFlight = drawnAt && Impl->FrameCounter - drawnAt < VULKAN_FRAMES_IN_FLIGHT;
	if (buffer && (buffer->Bytes < bytes || inFlight))
		retire(buffer);

	if (!buffer)
	{
		SVulkanBuffer made;
		if (!createBuffer(Impl, bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, made))
			return false;
		buffer = new SVulkanBuffer(made);
	}

	memcpy(buffer->Mapped, data, bytes);
	return true;
}


static VkShaderModule moduleFrom(CVulkanDriver::SImpl* impl, const u32* words, size_t count)
{
	const VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
			count * 4, words };
	VkShaderModule module = VK_NULL_HANDLE;
	return impl->Vk.vkCreateShaderModule(impl->Device, &info, 0, &module) == VK_SUCCESS ?
			module : VK_NULL_HANDLE;
}


static VkShaderModule openDriverStage(CVulkanDriver::SImpl* impl, io::IFileSystem* fs,
		const io::path& folder, const c8* name, u32* inputs = nullptr)
{
	core::array<c8> bytes;
	if (!readWhole(fs, folder + name, bytes) || bytes.size() % 4)
	{
		os::Printer::log("Could not read the Vulkan stage", (folder + name).c_str(), ELL_ERROR);
		return VK_NULL_HANDLE;
	}

	std::vector<u32> words(bytes.size() / 4);
	memcpy(words.data(), bytes.const_pointer(), bytes.size());
	if (inputs)
		*inputs = inputLocations(words.data(), words.size());
	return moduleFrom(impl, words.data(), words.size());
}


static bool hasExtension(const std::vector<VkExtensionProperties>& list, const c8* name)
{
	return std::any_of(list.begin(), list.end(), [name](const VkExtensionProperties& other)
	{
		return !strcmp(other.extensionName, name);
	});
}


static void logFailed(const c8* what, VkResult result)
{
	char line[96];
	snprintf(line, sizeof(line), "%s, VkResult %d", what, (s32)result);
	os::Printer::log(line, ELL_ERROR);
}


static VkResult createSurface(CVulkanDriver::SImpl* impl)
{
	const VkAndroidSurfaceCreateInfoKHR info = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
			nullptr, 0, (ANativeWindow*)impl->Window };
	return impl->Vk.vkCreateAndroidSurfaceKHR(impl->Instance, &info, 0, &impl->Surface);
}


//! First RGBA or BGRA format in sRGB space, ColorFormat if listed
static VkFormat surfaceFormat(CVulkanDriver::SImpl* impl)
{
	u32 count = 0;
	impl->Vk.vkGetPhysicalDeviceSurfaceFormatsKHR(impl->Gpu, impl->Surface, &count, 0);
	std::vector<VkSurfaceFormatKHR> formats(count);
	impl->Vk.vkGetPhysicalDeviceSurfaceFormatsKHR(impl->Gpu, impl->Surface, &count, formats.data());
	VkFormat first = VK_FORMAT_UNDEFINED;
	for (const VkSurfaceFormatKHR& format : formats)
		if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
				(format.format == VK_FORMAT_R8G8B8A8_UNORM ||
				format.format == VK_FORMAT_B8G8R8A8_UNORM))
		{
			if (format.format == impl->ColorFormat)
				return format.format;
			if (first == VK_FORMAT_UNDEFINED)
				first = format.format;
		}
	return first;
}


//! Qualcomm's driver as 512.minor.patch; older formats count as older than any
static bool qualcommBefore(const CVulkanDriver::SImpl* impl, u32 minor, u32 patch)
{
	const u32 version = impl->Properties.driverVersion;
	return impl->Properties.vendorID == 0x5143 &&
			(!(version & 0x80000000) || (version & 0x3fffff) < (minor << 12 | patch));
}


static bool isGE8300OrGE8322(const CVulkanDriver::SImpl* impl)
{
	return impl->Properties.vendorID == 0x1010 && (strstr(impl->Properties.deviceName, "GE8300") ||
			strstr(impl->Properties.deviceName, "GE8322"));
}


static VkRenderPass createPass(CVulkanDriver::SImpl* impl, bool clearColor)
{
	const VkAttachmentDescription attachments[2] = {
		{ 0, impl->ColorFormat, VK_SAMPLE_COUNT_1_BIT,
				clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
		{ 0, impl->DepthFormat, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL } };

	const VkAttachmentReference color = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	const VkAttachmentReference depth = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	const VkSubpassDescription subpass = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 1,
			&color, nullptr, &depth, 0, nullptr };
	//! Also waits for the last frame's writes to the shared depth image
	const VkSubpassDependency dependency = { VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0 };

	const VkRenderPassCreateInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, 2,
			attachments, 1, &subpass, 1, &dependency };
	VkRenderPass pass = VK_NULL_HANDLE;
	return impl->Vk.vkCreateRenderPass(impl->Device, &info, 0, &pass) == VK_SUCCESS ?
			pass : VK_NULL_HANDLE;
}


//! Destroys the semaphores after the swapchain
static void destroySwapchain(CVulkanDriver::SImpl* impl)
{
	for (VkFramebuffer framebuffer : impl->Framebuffers)
		impl->Vk.vkDestroyFramebuffer(impl->Device, framebuffer, 0);
	for (VkImageView view : impl->Views)
		impl->Vk.vkDestroyImageView(impl->Device, view, 0);
	impl->Framebuffers.clear();
	impl->Views.clear();
	impl->Images.clear();
	destroyImage(impl, impl->Depth);
	destroyIfMade(impl->Device, impl->Vk.vkDestroySwapchainKHR, std::exchange(impl->Swapchain, {}));
	for (VkSemaphore done : impl->RenderDone)
		destroyIfMade(impl->Device, impl->Vk.vkDestroySemaphore, done);
	impl->RenderDone.clear();
}


//! VK_NOT_READY for a minimised window, else why the chain could not be made
static VkResult createSwapchain(CVulkanDriver::SImpl* impl, u32 width, u32 height, bool vsync)
{
	//! Stale until every part below is made
	impl->SwapchainStale = true;

	VkSurfaceCapabilitiesKHR caps;
	const VkResult asked = impl->Vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(impl->Gpu,
			impl->Surface, &caps);
	if (asked != VK_SUCCESS)
		return asked;

	VkExtent2D extent = caps.currentExtent;
	if (extent.width == UINT32_MAX)
	{
		extent.width = core::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
		extent.height = core::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
	}
	if (!extent.width || !extent.height)
		return VK_NOT_READY;
	//! Turned by 90 or 270 degrees, the image keeps the panel's own orientation
	const bool turned = (caps.currentTransform & caps.supportedTransforms &
			(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
			VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)) != 0;
	if (turned)
		std::swap(extent.width, extent.height);

	u32 count = core::max_(caps.minImageCount, VULKAN_FRAMES_IN_FLIGHT);
	if (caps.maxImageCount && count > caps.maxImageCount)
		count = caps.maxImageCount;

	u32 modes = 0;
	impl->Vk.vkGetPhysicalDeviceSurfacePresentModesKHR(impl->Gpu, impl->Surface, &modes, 0);
	std::vector<VkPresentModeKHR> available(modes);
	impl->Vk.vkGetPhysicalDeviceSurfacePresentModesKHR(impl->Gpu, impl->Surface, &modes,
			available.data());
	impl->PresentMode = VK_PRESENT_MODE_FIFO_KHR;
	//! Without vsync: immediate if available, else mailbox
	if (!vsync)
		for (VkPresentModeKHR mode : available)
			if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR || (mode == VK_PRESENT_MODE_MAILBOX_KHR &&
					impl->PresentMode == VK_PRESENT_MODE_FIFO_KHR))
				impl->PresentMode = mode;

	VkSwapchainCreateInfoKHR info = {};
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = impl->Surface;
	info.minImageCount = count;
	info.imageFormat = impl->ColorFormat;
	info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	info.imageExtent = extent;
	info.imageArrayLayers = 1;
#if 0
	impl->Readable = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			(impl->Readable ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
#endif
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	//! Only 90 and 270 degrees are drawn turned; the system turns the rest
	info.preTransform = turned ||
			!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ?
			caps.currentTransform : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	static const VkCompositeAlphaFlagBitsKHR alphas[] = { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR };
	const auto alpha = std::find_if(std::begin(alphas), std::end(alphas),
			[&caps](u32 bit) { return (caps.supportedCompositeAlpha & bit) != 0; });
	info.compositeAlpha = alpha != std::end(alphas) ? *alpha : alphas[0];
	info.presentMode = impl->PresentMode;
	info.clipped = VK_TRUE;
	info.oldSwapchain = impl->Swapchain;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	const VkResult made = impl->Vk.vkCreateSwapchainKHR(impl->Device, &info, 0, &swapchain);
	//! A failed call still retires the old chain
	destroySwapchain(impl);
	if (made != VK_SUCCESS)
		return made;
	impl->Swapchain = swapchain;
	impl->Extent = extent;
	impl->Transform = caps.currentTransform;
	impl->Turn = !turned ? 0 :
			(caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ? 1 : -1);

	u32 images = 0;
	VkResult listed = impl->Vk.vkGetSwapchainImagesKHR(impl->Device, swapchain, &images, 0);
	if (listed == VK_SUCCESS && !images)
		listed = VK_ERROR_INITIALIZATION_FAILED;
	if (listed != VK_SUCCESS)
		return listed;
	impl->Images.resize(images);
	impl->Vk.vkGetSwapchainImagesKHR(impl->Device, swapchain, &images, impl->Images.data());

	const VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
	impl->RenderDone.assign(images, VK_NULL_HANDLE);
	for (VkSemaphore& done : impl->RenderDone)
		if (const VkResult signal = impl->Vk.vkCreateSemaphore(impl->Device, &semaphore, 0,
				&done); signal != VK_SUCCESS)
			return signal;

	if (!makeImage(impl, extent.width, extent.height, impl->DepthFormat,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
			impl->Depth))
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;

	for (u32 i = 0; i < images; ++i)
	{
		const VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0,
				impl->Images[i], VK_IMAGE_VIEW_TYPE_2D,
				impl->ColorFormat, {}, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
		VkImageView created = VK_NULL_HANDLE;
		if (const VkResult viewed = impl->Vk.vkCreateImageView(impl->Device, &view, 0,
				&created); viewed != VK_SUCCESS)
			return viewed;
		impl->Views.push_back(created);
		if (impl->Use13)
			continue;

		const VkImageView attachments[2] = { created, impl->Depth.View };
		const VkFramebufferCreateInfo framebuffer = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				nullptr, 0, impl->Passes[1], 2, attachments, extent.width, extent.height, 1 };
		VkFramebuffer target = VK_NULL_HANDLE;
		if (const VkResult framed = impl->Vk.vkCreateFramebuffer(impl->Device, &framebuffer, 0,
				&target); framed != VK_SUCCESS)
			return framed;
		impl->Framebuffers.push_back(target);
	}

	impl->SwapchainStale = false;
	return VK_SUCCESS;
}


CVulkanDriver::CVulkanDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io,
		CIrrDeviceSDL* device)
	: CDriverCommon(io, params.WindowSize), Params(params), SDLDevice(device), Impl(new SImpl())
{
	Impl->Meshes.Store.Impl = Impl;
	#ifdef _DEBUG
	setDebugName("CVulkanDriver");
	#endif
}


CVulkanDriver::~CVulkanDriver()
{
	SImpl* impl = Impl;
	if (impl->Device)
	{
		if (impl->Started)
			endScene();
		impl->Vk.vkDeviceWaitIdle(impl->Device);
	}

	//! Meshes, materials and textures go before the Vulkan state
	removeEveryHardwareBuffer();
	deleteMaterialRenders();
	deleteAllTextures();
	dropWaiting(impl, impl->Black.Image);

	if (impl->Device)
	{
		for (u32 f = 0; f < VULKAN_FRAMES_IN_FLIGHT; ++f)
		{
			for (SImpl::SRingBlock* block : impl->Rings[f])
				freeRingBlock(impl, block);
			releaseAll(impl, impl->Retired[f]);
			for (SVulkanImage& image : impl->RetiredImages[f])
				destroyImage(impl, image);
		}
		for (SVulkanArena* arena : impl->Arenas)
		{
			destroyBuffer(impl, arena->Buffer);
			delete arena;
		}

		for (VkShaderModule module : impl->StageModules)
			destroyIfMade(impl->Device, impl->Vk.vkDestroyShaderModule, module);
		for (u32 i = 0; i < Programs.size(); ++i)
			delete Programs[i];

		for (const auto& [key, pipeline] : impl->Pipelines)
			impl->Vk.vkDestroyPipeline(impl->Device, pipeline, 0);
		for (const auto& [key, sampler] : impl->Samplers)
			impl->Vk.vkDestroySampler(impl->Device, sampler, 0);
		for (VkDescriptorPool pool : impl->SetPools)
			impl->Vk.vkDestroyDescriptorPool(impl->Device, pool, 0);
		destroyIfMade(impl->Device, impl->Vk.vkDestroyDescriptorPool, impl->SparePool);
		destroyImage(impl, impl->Black);

		destroySwapchain(impl);
		for (VkDescriptorSetLayout layout : impl->SetLayouts)
			destroyIfMade(impl->Device, impl->Vk.vkDestroyDescriptorSetLayout, layout);
		for (VkRenderPass pass : impl->Passes)
			destroyIfMade(impl->Device, impl->Vk.vkDestroyRenderPass, pass);
		for (VkShaderModule stage : impl->Fixed)
			destroyIfMade(impl->Device, impl->Vk.vkDestroyShaderModule, stage);
		for (VkShaderModule stage : impl->Clear.Modules)
			destroyIfMade(impl->Device, impl->Vk.vkDestroyShaderModule, stage);
		destroyIfMade(impl->Device, impl->Vk.vkDestroyPipelineLayout, impl->Layout);
		destroyIfMade(impl->Device, impl->Vk.vkDestroyPipelineCache, impl->Cache);

		for (const SImpl::SSlot& slot : impl->Slots)
		{
			destroyIfMade(impl->Device, impl->Vk.vkDestroyFence, slot.Done);
			destroyIfMade(impl->Device, impl->Vk.vkDestroySemaphore, slot.ImageReady);
			destroyIfMade(impl->Device, impl->Vk.vkDestroyCommandPool, slot.Pool);
		}
		destroyIfMade(impl->Device, impl->Vk.vkDestroyCommandPool, impl->UploadPool);

		impl->Vk.vkDestroyDevice(impl->Device, 0);
	}

	destroyIfMade(impl->Instance, impl->Vk.vkDestroySurfaceKHR, impl->Surface);
	if (impl->Instance)
		impl->Vk.vkDestroyInstance(impl->Instance, 0);
	if (impl->Library)
		dlclose(impl->Library);

	delete impl;
}


bool CVulkanDriver::genericDriverInit()
{
	SImpl* impl = Impl;

	impl->Library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	if (impl->Library)
		impl->Vk.vkGetInstanceProcAddr = (decltype(impl->Vk.vkGetInstanceProcAddr))dlsym(
				impl->Library, "vkGetInstanceProcAddr");
	if (!impl->Vk.vkGetInstanceProcAddr)
	{
		os::Printer::log("No Vulkan loader on this system", ELL_ERROR);
		return false;
	}

	//! Device functions come from the device, which skips the loader's dispatch
#define IRR_VULKAN_TAKE(name) \
	impl->Vk.name = (decltype(impl->Vk.name))(void*)(impl->Device ? \
			impl->Vk.vkGetDeviceProcAddr(impl->Device, #name) : \
			impl->Vk.vkGetInstanceProcAddr(impl->Instance, #name)); \
	if (!impl->Vk.name) \
	{ \
		os::Printer::log("Vulkan lacks", #name, ELL_ERROR); \
		return false; \
	}
	IRR_VULKAN_GLOBAL(IRR_VULKAN_TAKE)

	u32 count = 0;

	std::vector<const c8*> extensions;
	extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);

	u32 loaderVersion = VK_API_VERSION_1_0;
	if (const auto version =
			(decltype(&::vkEnumerateInstanceVersion))(void*)impl->Vk.vkGetInstanceProcAddr(
			VK_NULL_HANDLE, "vkEnumerateInstanceVersion"))
		version(&loaderVersion);
	VkApplicationInfo application = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr,
			"Irrlicht", 0, "Irrlicht", 0,
			core::min_(loaderVersion, (u32)VK_API_VERSION_1_3) };

	const VkInstanceCreateInfo instance = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0,
			&application, 0, nullptr, (u32)extensions.size(), extensions.data() };
	std::vector<VkPhysicalDevice> gpus;
	//! With no GPU past 1.0 the instance is remade as 1.0, so layers make no 1.1 queries
	for (;;)
	{
		const VkResult created = impl->Vk.vkCreateInstance(&instance, 0, &impl->Instance);
		if (created != VK_SUCCESS)
		{
			logFailed("Could not create a Vulkan instance", created);
			return false;
		}
		IRR_VULKAN_INSTANCE(IRR_VULKAN_TAKE)

		if (impl->Vk.vkEnumeratePhysicalDevices(impl->Instance, &count, 0) != VK_SUCCESS)
			count = 0;
		gpus.resize(count);
		if (count && impl->Vk.vkEnumeratePhysicalDevices(impl->Instance, &count,
				gpus.data()) < VK_SUCCESS)
			count = 0;
		u32 newest = VK_API_VERSION_1_0;
		for (u32 g = 0; g < count; ++g)
		{
			VkPhysicalDeviceProperties properties;
			impl->Vk.vkGetPhysicalDeviceProperties(gpus[g], &properties);
			newest = core::max_(newest, properties.apiVersion);
		}
		if (!count || newest >= VK_API_VERSION_1_1 ||
				application.apiVersion == VK_API_VERSION_1_0)
			break;
		impl->Vk.vkDestroyInstance(std::exchange(impl->Instance, nullptr), 0);
		application.apiVersion = VK_API_VERSION_1_0;
	}

	impl->Window = SDLDevice ? SDLDevice->getDrawTarget() : 0;
	if (!impl->Window)
	{
		os::Printer::log("The window is gone, Vulkan has nothing to draw into", ELL_WARNING);
		return false;
	}
	const VkResult surface = createSurface(impl);
	if (surface != VK_SUCCESS)
	{
		logFailed("Could not make a Vulkan surface for the window", surface);
		return false;
	}

	//! A software device is skipped, leaving the game to OpenGL ES
	core::stringc rejected;
	for (u32 g = 0; g < count && !impl->Gpu; ++g)
	{
		VkPhysicalDeviceProperties properties;
		impl->Vk.vkGetPhysicalDeviceProperties(gpus[g], &properties);
		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
		{
			rejected += core::stringc(rejected.size() ? ", " : "") + properties.deviceName +
					" (software)";
			continue;
		}
		u32 families = 0;
		impl->Vk.vkGetPhysicalDeviceQueueFamilyProperties(gpus[g], &families, 0);
		std::vector<VkQueueFamilyProperties> queues(families);
		impl->Vk.vkGetPhysicalDeviceQueueFamilyProperties(gpus[g], &families, queues.data());
		for (u32 f = 0; f < families; ++f)
		{
			VkBool32 presents = VK_FALSE;
			impl->Vk.vkGetPhysicalDeviceSurfaceSupportKHR(gpus[g], f, impl->Surface, &presents);
			if ((queues[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presents)
			{
				impl->Gpu = gpus[g];
				impl->QueueFamily = f;
				impl->Properties = properties;
				break;
			}
		}
		if (!impl->Gpu)
			rejected += core::stringc(rejected.size() ? ", " : "") + properties.deviceName +
					" (no queue presents)";
	}
	if (!impl->Gpu)
	{
		if (count)
			os::Printer::log("No Vulkan device draws into this window", rejected.c_str(),
					ELL_ERROR);
		else
			os::Printer::log("No Vulkan device on this system", ELL_ERROR);
		return false;
	}
	Name = L"Vulkan ";
	Name += core::stringw(impl->Properties.deviceName);
	os::Printer::log("Vulkan driver", core::stringc(Name.c_str()).c_str(), ELL_INFORMATION);

	//! Mali with index-driven vertex shading fetches positions apart from the rest
	const c8* gpu = impl->Properties.deviceName;
	impl->Meshes.SplitPositions = impl->Properties.vendorID == 0x13b5 &&
			(!strncmp(gpu, "Mali-G", 6) || !strncmp(gpu, "Immortalis-G", 12));

	impl->Vk.vkGetPhysicalDeviceMemoryProperties(impl->Gpu, &impl->Memory);
	VkPhysicalDeviceFeatures supported;
	impl->Vk.vkGetPhysicalDeviceFeatures(impl->Gpu, &supported);
	impl->Features.fillModeNonSolid = supported.fillModeNonSolid;
	impl->Features.samplerAnisotropy = supported.samplerAnisotropy;

	impl->Vk.vkEnumerateDeviceExtensionProperties(impl->Gpu, 0, &count, 0);
	std::vector<VkExtensionProperties> deviceExtensions(count);
	impl->Vk.vkEnumerateDeviceExtensionProperties(impl->Gpu, 0, &count, deviceExtensions.data());
	extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	//! Below 1.1 the flipped viewport and format transfer bits need VK_KHR_maintenance1
	const u32 version = core::min_(application.apiVersion, impl->Properties.apiVersion);
	if (version < VK_API_VERSION_1_1)
	{
		if (!hasExtension(deviceExtensions, VK_KHR_MAINTENANCE1_EXTENSION_NAME))
		{
			os::Printer::log("Vulkan 1.0 lacks", VK_KHR_MAINTENANCE1_EXTENSION_NAME, ELL_ERROR);
			return false;
		}
		extensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
	}

	//! Dynamic rendering breaks on Qualcomm before 512.801, as ANGLE notes, and on GE8300, GE8322
	impl->Use13 = version >= VK_API_VERSION_1_3 && !qualcommBefore(impl, 801, 0) &&
			!isGE8300OrGE8322(impl);
	//! Mali: stale dynamic cull mode before r52 (Arm 3809538), more device losses on r54
	impl->DynamicCull = impl->Use13 && (impl->Properties.vendorID != 0x13b5 ||
			impl->Properties.driverVersion >= VK_MAKE_API_VERSION(0, 55, 0, 0));
	VkPhysicalDeviceVulkan13Features features13 = {};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;

	const f32 priority = 1.0f;
	const VkDeviceQueueCreateInfo queue = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0,
			impl->QueueFamily, 1, &priority };
	const VkDeviceCreateInfo device = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			impl->Use13 ? &features13 : nullptr, 0, 1, &queue, 0, nullptr,
			(u32)extensions.size(), extensions.data(), &impl->Features };
	const VkResult made = impl->Vk.vkCreateDevice(impl->Gpu, &device, 0, &impl->Device);
	if (made != VK_SUCCESS)
	{
		logFailed("Could not create a Vulkan device", made);
		return false;
	}

	IRR_VULKAN_DEVICE(IRR_VULKAN_TAKE)
	if (impl->Use13)
	{
		IRR_VULKAN_DEVICE_1_3(IRR_VULKAN_TAKE)
	}
#undef IRR_VULKAN_TAKE

	impl->Vk.vkGetDeviceQueue(impl->Device, impl->QueueFamily, 0, &impl->Queue);

	impl->ColorFormat = surfaceFormat(impl);
	if (impl->ColorFormat == VK_FORMAT_UNDEFINED)
		impl->ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;

	const auto supports = [impl](VkFormat format, VkFormatFeatureFlags needs)
	{
		VkFormatProperties properties;
		impl->Vk.vkGetPhysicalDeviceFormatProperties(impl->Gpu, format, &properties);
		return (properties.optimalTilingFeatures & needs) == needs;
	};
	static const VkFormatFeatureFlags textureNeeds = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
			VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
			VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
	if (supports(VK_FORMAT_B8G8R8A8_UNORM, textureNeeds))
		impl->TextureFormat = VK_FORMAT_B8G8R8A8_UNORM;
	impl->PackedR5G6B5 = supports(VK_FORMAT_R5G6B5_UNORM_PACK16, textureNeeds);
	impl->PackedA1R5G5B5 = supports(VK_FORMAT_A1R5G5B5_UNORM_PACK16, textureNeeds);
	//! Mip levels are blits with a linear filter
	static const VkFormatFeatureFlags mipmapNeeds = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
			VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	impl->Mipmaps32 = supports(impl->TextureFormat, mipmapNeeds);
	impl->MipmapsR5G6B5 = supports(VK_FORMAT_R5G6B5_UNORM_PACK16, mipmapNeeds);
	impl->MipmapsA1R5G5B5 = supports(VK_FORMAT_A1R5G5B5_UNORM_PACK16, mipmapNeeds);

	static const VkFormat depths[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM };
	for (VkFormat depth : depths)
		if (!impl->DepthFormat && supports(depth, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
			impl->DepthFormat = depth;

	if (!impl->Use13)
	{
		impl->Passes[0] = createPass(impl, false);
		impl->Passes[1] = createPass(impl, true);
		if (!impl->Passes[0] || !impl->Passes[1])
		{
			os::Printer::log("Could not create the Vulkan render passes", ELL_ERROR);
			return false;
		}
	}

	const VkDescriptorSetLayoutBinding uniforms[2] = {
		{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, 0 },
		{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0 } };
	VkDescriptorSetLayoutBinding textures[VULKAN_LAYERS];
	for (u32 i = 0; i < VULKAN_LAYERS; ++i)
		textures[i] = { i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
				VK_SHADER_STAGE_FRAGMENT_BIT, 0 };

	for (u32 s = 0; s < 3; ++s)
	{
		const VkDescriptorSetLayoutCreateInfo layout =
				{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
				s < 2 ? 1 : VULKAN_LAYERS, s < 2 ? &uniforms[s] : textures };
		impl->Vk.vkCreateDescriptorSetLayout(impl->Device, &layout, 0, &impl->SetLayouts[s]);
	}

	const VkPushConstantRange turn = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(s32) };
	const VkPipelineLayoutCreateInfo pipelineLayout =
			{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 3, impl->SetLayouts, 1,
			&turn };
	if (!impl->SetLayouts[0] || !impl->SetLayouts[1] || !impl->SetLayouts[2] ||
			impl->Vk.vkCreatePipelineLayout(impl->Device, &pipelineLayout, 0,
			&impl->Layout) != VK_SUCCESS)
	{
		os::Printer::log("Could not create the Vulkan pipeline layout", ELL_ERROR);
		return false;
	}

	VkPipelineCacheCreateInfo cache = {};
	cache.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	impl->Vk.vkCreatePipelineCache(impl->Device, &cache, 0, &impl->Cache);

	const VkCommandPoolCreateInfo pool = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0,
			impl->QueueFamily };
	const VkFenceCreateInfo fence = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr,
			VK_FENCE_CREATE_SIGNALED_BIT };
	const VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
	for (SImpl::SSlot& slot : impl->Slots)
	{
		bool ready = impl->Vk.vkCreateCommandPool(impl->Device, &pool, 0, &slot.Pool) == VK_SUCCESS;
		const VkCommandBufferAllocateInfo alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				nullptr, slot.Pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2 };
		VkCommandBuffer buffers[2] = {};
		ready = ready &&
				impl->Vk.vkAllocateCommandBuffers(impl->Device, &alloc, buffers) == VK_SUCCESS &&
				impl->Vk.vkCreateFence(impl->Device, &fence, 0, &slot.Done) == VK_SUCCESS &&
				impl->Vk.vkCreateSemaphore(impl->Device, &semaphore, 0,
				&slot.ImageReady) == VK_SUCCESS;
		if (!ready)
		{
			os::Printer::log("Could not create what a Vulkan frame records into", ELL_ERROR);
			return false;
		}
		slot.Commands = buffers[0];
		slot.Uploads = buffers[1];
	}

	if (impl->Vk.vkCreateCommandPool(impl->Device, &pool, 0, &impl->UploadPool) != VK_SUCCESS)
		return false;
	const VkCommandBufferAllocateInfo oneOff = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			nullptr, impl->UploadPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
	if (impl->Vk.vkAllocateCommandBuffers(impl->Device, &oneOff, &impl->OneOff) != VK_SUCCESS)
		return false;

	//! A window hidden or lost while starting gets its chain with the first frame
	const VkResult chain = createSwapchain(impl, ScreenSize.Width, ScreenSize.Height, Params.Vsync);
	if (chain != VK_SUCCESS && chain != VK_NOT_READY && chain != VK_ERROR_SURFACE_LOST_KHR)
	{
		logFailed("Could not create the Vulkan swapchain", chain);
		return false;
	}

	impl->Fixed[0] = openDriverStage(impl, FileSystem, Params.OGLES2ShaderPath,
			VULKAN_FIXED_VERTEX, &impl->FixedInputs);
	impl->Fixed[1] = openDriverStage(impl, FileSystem, Params.OGLES2ShaderPath,
			VULKAN_FIXED_FRAGMENT);
	if (!impl->Fixed[0] || !impl->Fixed[1])
		return false;
	impl->Clear.Name = "the depth clear";
	//! vkCmdClearAttachments races with draws on Qualcomm before 512.762.12, as ANGLE notes, and
	//! faults on GE8300 and GE8322
	if (qualcommBefore(impl, 762, 12) || isGE8300OrGE8322(impl))
		for (u32 s = 0; s < 2; ++s)
			impl->Clear.Modules[s] = openDriverStage(impl, FileSystem, Params.OGLES2ShaderPath,
					VULKAN_DEPTH_CLEAR[s]);

	SMaterialLayer flat;
	flat.BilinearFilter = false;
	impl->Sampler2D = samplerFor(impl, flat, false);

	if (!impl->Sampler2D || !makeImage(impl, 1, 1, impl->TextureFormat,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, impl->Black))
	{
		os::Printer::log("Could not create the Vulkan fallback sampler and texture", ELL_ERROR);
		return false;
	}
	CImage black(ECF_A8R8G8B8, core::dimension2du(1, 1));
	black.fill(SColor(255, 0, 0, 0));
	queueTextureWrite(impl, impl->Black.Image, &black, true);
	flushWrites(impl);

	//! The first block of each ring comes before any texture, so 2D always has room
	for (std::vector<SImpl::SRingBlock*>& ring : impl->Rings)
		if (SImpl::SRingBlock* block = newRingBlock(impl))
			ring.push_back(block);

	ExposedData.OpenGLSDL.Window = SDLDevice->getWindow();

	char details[64];
	snprintf(details, sizeof(details), "API %u.%u.%u, path %s, driver 0x%x",
			VK_API_VERSION_MAJOR(impl->Properties.apiVersion),
			VK_API_VERSION_MINOR(impl->Properties.apiVersion),
			VK_API_VERSION_PATCH(impl->Properties.apiVersion),
			impl->Use13 ? "1.3" : version < VK_API_VERSION_1_1 ? "1.0" : "1.1",
			impl->Properties.driverVersion);
	os::Printer::log("Vulkan swapchain", details, ELL_INFORMATION);
	return true;
}


//! Frees a texture's image once no frame in flight reads it
static void retireTexture(CVulkanDriver::SImpl* impl, SVulkanImage& image)
{
	if (!impl->Device || !image.Image)
		return;

	for (auto it = impl->TextureSets.begin(); it != impl->TextureSets.end();)
		if (std::count(std::begin(it->first.Views), std::end(it->first.Views), image.View))
		{
			if (impl->BoundTextures == it->second)
				impl->BoundTextures = VK_NULL_HANDLE;
			impl->RetiredSets[impl->RingIndex].push_back(it->second);
			it = impl->TextureSets.erase(it);
		}
		else
			++it;

	//! Mid-frame the writes stay: draws recorded so far may sample the image
	const bool inFrame = impl->Started && !impl->Submitted;
	for (size_t i = 0; i < impl->Writes.size() && !inFrame;)
		if (const SVulkanTextureWrite& write = impl->Writes[i]; write.Image == image.Image)
		{
			impl->WriteBytes -= write.Bytes;
			giveArenaRange(impl, write.Arena, write.Base, write.Bytes);
			impl->Writes.erase(impl->Writes.begin() + (ptrdiff_t)i);
		}
		else
			++i;
	dropWaiting(impl, image.Image);

	//! Makes programs gather their textures again
	++impl->MaterialCounter;

	impl->RetiredImages[impl->RingIndex].push_back(std::exchange(image, {}));
}


static void bindPipeline(CVulkanDriver::SImpl* impl, VkPipeline pipeline)
{
	if (pipeline == impl->BoundPipeline)
		return;
	if (!impl->BoundPipeline)
		impl->Vk.vkCmdPushConstants(impl->commands(), impl->Layout, VK_SHADER_STAGE_VERTEX_BIT,
				0, sizeof(impl->Turn), &impl->Turn);
	impl->Vk.vkCmdBindPipeline(impl->commands(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	impl->BoundPipeline = pipeline;
}


//! Rebinds stages whose block moved; a null block keeps its set
static void bindUniforms(CVulkanDriver::SImpl* impl, const CVulkanDriver::SImpl::SPlaced placed[2])
{
	bool moved[2];
	for (u32 s = 0; s < 2; ++s)
		moved[s] = placed[s].Block && placed[s] != impl->BoundUniforms[s];
	if (!moved[0] && !moved[1])
		return;

	const u32 first = moved[0] ? 0 : 1;
	const u32 count = moved[0] && moved[1] ? 2 : 1;
	const VkDescriptorSet sets[2] = { placed[first].Block->Uniforms[first],
			count == 2 ? placed[1].Block->Uniforms[1] : VK_NULL_HANDLE };
	const u32 offsets[2] = { placed[first].Offset, placed[1].Offset };
	impl->Vk.vkCmdBindDescriptorSets(impl->commands(), VK_PIPELINE_BIND_POINT_GRAPHICS,
			impl->Layout, first, count, sets, count, offsets);
	for (u32 s = first; s < first + count; ++s)
		impl->BoundUniforms[s] = placed[s];
}


//! Binds the draw's textures, false when out of memory
static bool bindTextures(CVulkanDriver::SImpl* impl,
		const CVulkanDriver::SImpl::STextureSet& textures)
{
	if (impl->BoundTextures && textures == impl->BoundTextureKey)
		return true;
	const VkDescriptorSet set = textureSetFor(impl, textures);
	if (!set)
		return false;
	impl->BoundTextureKey = textures;
	if (set == impl->BoundTextures)
		return true;
	impl->Vk.vkCmdBindDescriptorSets(impl->commands(), VK_PIPELINE_BIND_POINT_GRAPHICS,
			impl->Layout, 2, 1, &set, 0, 0);
	impl->BoundTextures = set;
	return true;
}


static void bindVertices(CVulkanDriver::SImpl* impl, VkBuffer buffer, u32 offset)
{
	if (buffer == impl->BoundVertices && offset == impl->BoundVertexOffset)
		return;
	const VkDeviceSize at = offset;
	impl->Vk.vkCmdBindVertexBuffers(impl->commands(), 0, 1, &buffer, &at);
	impl->BoundVertices = buffer;
	impl->BoundVertexOffset = offset;
}


static void bindIndices(CVulkanDriver::SImpl* impl, VkBuffer buffer, u32 offset, E_INDEX_TYPE type)
{
	const VkIndexType width = type == EIT_32BIT ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
	if (buffer == impl->BoundIndices && offset == impl->BoundIndexOffset &&
			width == impl->BoundIndexType)
		return;
	impl->Vk.vkCmdBindIndexBuffer(impl->commands(), buffer, offset, width);
	impl->BoundIndices = buffer;
	impl->BoundIndexOffset = offset;
	impl->BoundIndexType = width;
}


class CVulkanShaderRenderer : public CDriverShaderRenderer<CVulkanDriver, SVulkanShaderProgram>
{
public:
	CVulkanShaderRenderer(CVulkanDriver* driver, SVulkanShaderProgram* program, u32 key,
			IShaderConstantSetCallBack* callback, E_MATERIAL_TYPE base, s32 userData)
		: CDriverShaderRenderer(driver, program, callback, base, userData), Key(key)
	{
		for (u32 s = 0; s < 2; ++s)
			Values[s].assign(core::max_(Program->BlockBytes[s], (u32)16), 0);

		settleLayers();
		for (u32 i = 0; i < Program->Uniforms.size(); ++i)
			if (Program->Uniforms[i].Texture)
				TextureUniforms.push_back(i);
	}

	bool bind(E_VERTEX_TYPE vType, u32 topology, u32 cull, u32 compare, bool write, bool wire)
	{
		CVulkanDriver::SImpl* impl = Driver->Impl;
		const VkPipeline pipeline = pipelineFor(impl, Program, Key, vType, topology, Blend, false,
				false, cull, compare, write, wire);
		if (!pipeline)
			return false;
		bindPipeline(impl, pipeline);

		if (CallBack)
			CallBack->OnSetConstants(this, UserData);

		if (TexturesFor != impl->MaterialCounter || LayersMoved)
		{
			Textures.fill(impl->Black.View, impl->Sampler2D);
			for (const u32 i : TextureUniforms)
			{
				const CVulkanTexture* texture = static_cast<const CVulkanTexture*>(
						Driver->Material.getTexture((u32)Layers[i]));
				if (!texture || !texture->view())
					continue;
				const u32 binding = Program->Uniforms[i].Binding;
				Textures.Views[binding] = texture->view();
				Textures.Samplers[binding] = samplerFor(impl,
						Driver->Material.TextureLayer[(u32)Layers[i]], Driver->Material.UseMipMaps);
			}
			TexturesFor = impl->MaterialCounter;
			LayersMoved = false;
		}
		if (!bindTextures(impl, Textures))
			return false;

		for (u32 s = 0; s < 2; ++s)
		{
			if (!Stale[s] && PlacedFrame[s] == impl->FrameCounter)
				continue;
			Placed[s] = stageBlock(impl, Values[s].data(), (u32)Values[s].size());
			if (!Placed[s].Block)
				return false;
			PlacedFrame[s] = impl->FrameCounter;
			Stale[s] = false;
		}

		bindUniforms(impl, Placed);
		return true;
	}

private:
	bool setConstant(s32 index, const f32* floats, int count) IRR_OVERRIDE
	{
		if (index < 0 || index >= (s32)Program->Uniforms.size() || !floats || count <= 0)
			return false;

		bool wrote = false;
		for (s32 i = index; i >= 0; i = Program->Uniforms[(u32)i].Next)
		{
			const SVulkanUniform& u = Program->Uniforms[(u32)i];
			if (u.Texture || !u.Components)
				continue;

			std::vector<u8>& block = Values[u.Stage];
			const u32 column = u.Components / u.Columns;
			const u32 step = u.Columns > 1 ? u.ColumnStride : 0;
			u32 left = (u32)count;
			const f32* from = floats;
			for (u32 e = 0; e < u.Elements && left; ++e)
			{
				for (u32 c = 0; c < u.Columns && left; ++c)
				{
					const u32 at = u.Offset + e * u.Stride + c * step;
					const u32 n = core::min_(column, left);
					if (at + n * 4 > block.size())
						break;
					memcpy(&block[at], from, n * 4);
					from += n;
					left -= n;
				}
			}

			Stale[u.Stage] = true;
			wrote = true;
		}
		return wrote;
	}

	u32 Key = 0;
	//! Each stage's block values, vertex first
	std::vector<u8> Values[2];
	CVulkanDriver::SImpl::SPlaced Placed[2];
	u64 PlacedFrame[2] = { 0, 0 };
	bool Stale[2] = { true, true };
	std::vector<u32> TextureUniforms;
	CVulkanDriver::SImpl::STextureSet Textures;
	u64 TexturesFor = UINT64_MAX;
};


bool CVulkanDriver::beginScene(u16 clearFlag, SColor clearColor, f32 clearDepth, u8 clearStencil,
		const SExposedVideoData& videoData, core::rect<s32>* sourceRect)
{
	SImpl* impl = Impl;
	//! Closes a frame an exception left open
	if (impl->Started)
		endScene();
	CNullDriver::beginScene(clearFlag, clearColor, clearDepth, clearStencil, videoData, sourceRect);

	if (impl->Lost)
		return false;

	//! A pause the system sent meanwhile reaches OnBackground; the frame waits it out
	if (!SDLDevice->waitForForeground())
		return false;

	if (void* const window = SDLDevice->getDrawTarget(); window != impl->Window)
	{
		waitIdle(impl);
		destroySwapchain(impl);
		destroyIfMade(impl->Instance, impl->Vk.vkDestroySurfaceKHR, impl->Surface);
		impl->Surface = VK_NULL_HANDLE;
		impl->Window = window;
		if (!window)
			return false;
		//! An unusable surface is not retried until the window changes
		VkBool32 presents = VK_FALSE;
		if (createSurface(impl) == VK_SUCCESS)
			impl->Vk.vkGetPhysicalDeviceSurfaceSupportKHR(impl->Gpu, impl->QueueFamily,
					impl->Surface, &presents);
		else
			impl->Surface = VK_NULL_HANDLE;
		if (!presents || surfaceFormat(impl) != impl->ColorFormat)
		{
			destroyIfMade(impl->Instance, impl->Vk.vkDestroySurfaceKHR, impl->Surface);
			impl->Surface = VK_NULL_HANDLE;
			return false;
		}
		impl->SwapchainStale = true;
	}
	if (!impl->Surface)
		return false;

	const u32 next = (impl->RingIndex + 1) % VULKAN_FRAMES_IN_FLIGHT;
	const VkResult waited = impl->Vk.vkWaitForFences(impl->Device, 1, &impl->Slots[next].Done,
			VK_TRUE, UINT64_MAX);
	if (waited == VK_ERROR_DEVICE_LOST)
		loseDevice(impl, "vkWaitForFences");
	if (waited != VK_SUCCESS)
		return false;

	impl->RingIndex = next;
	dropEmptyArenas(impl);
	releaseAll(impl, impl->Retired[next]);
	for (SVulkanImage& image : impl->RetiredImages[next])
		destroyImage(impl, image);
	impl->RetiredImages[next].clear();
	for (const SVulkanTextureWrite& write : impl->Staged[next])
		giveArenaRange(impl, write.Arena, write.Base, write.Bytes);
	impl->Staged[next].clear();
	retryWaiting(impl);
	std::vector<CVulkanTexture*>& unmade = impl->Unmade;
	for (size_t left = unmade.size(); left; --left)
	{
		CVulkanTexture* texture = unmade.front();
		unmade.erase(unmade.begin());
		if (!texture->remake())
		{
			unmade.push_back(texture);
			break;
		}
		++impl->MaterialCounter;
	}
	//! Sets of freed images serve new textures
	impl->SpareSets[2].insert(impl->SpareSets[2].end(), impl->RetiredSets[next].begin(),
			impl->RetiredSets[next].end());
	impl->RetiredSets[next].clear();

	impl->Meshes.advance(next, impl->FrameCounter, HWBufferList);
	trimRing(impl);
	++impl->FrameCounter;
	impl->RingBlock = 0;
	impl->RingUsed = 0;
	impl->Interface = false;

	const SImpl::SSlot& slot = impl->Slots[impl->RingIndex];
	const VkResult reset = impl->Vk.vkResetCommandPool(impl->Device, slot.Pool, 0);
	const VkResult begun = reset != VK_SUCCESS ? reset :
			impl->Vk.vkBeginCommandBuffer(slot.Commands, &VULKAN_ONE_TIME_SUBMIT);
	if (begun != VK_SUCCESS)
		return false;
	impl->Started = true;

	if (!beginEncoding((clearFlag & ECBF_COLOR) != 0, clearColor, clearDepth))
	{
		//! No image, as when minimised: drops the frame unsubmitted
		impl->Vk.vkEndCommandBuffer(impl->commands());
		impl->Started = false;
		return false;
	}
	return true;
}


bool CVulkanDriver::beginEncoding(bool clearColor, SColor color, f32 depth)
{
	SImpl* impl = Impl;
	//! A timeout drops the frame: the compositor holds every image
	for (u32 attempt = 0; attempt < 2 && !impl->HaveImage; ++attempt)
	{
		if (impl->SwapchainStale || !impl->Swapchain)
		{
			waitIdle(impl);
			if (createSwapchain(impl, ScreenSize.Width, ScreenSize.Height,
					Params.Vsync) != VK_SUCCESS)
				return false;
		}
		const VkResult result = impl->Vk.vkAcquireNextImageKHR(impl->Device, impl->Swapchain,
				VULKAN_WAIT_NANOSECONDS, impl->Slots[impl->RingIndex].ImageReady, VK_NULL_HANDLE,
				&impl->ImageIndex);
		if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
			impl->HaveImage = true;
		else if (result == VK_ERROR_OUT_OF_DATE_KHR && attempt == 0)
			impl->SwapchainStale = true;
		else
		{
			if (result == VK_ERROR_DEVICE_LOST)
				loseDevice(impl, "vkAcquireNextImageKHR");
			return false;
		}
	}

	VkClearValue clears[2];
	clears[0].color = { { color.getRed() / 255.0f, color.getGreen() / 255.0f,
			color.getBlue() / 255.0f, color.getAlpha() / 255.0f } };
	clears[1].depthStencil = { depth, 0 };

	if (impl->Use13)
	{
		transition(impl, impl->commands(), impl->Images[impl->ImageIndex],
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		transition(impl, impl->commands(), impl->Depth.Image, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		const VkRenderingAttachmentInfo attachments[2] = {
			{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, impl->Views[impl->ImageIndex],
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
					clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
					VK_ATTACHMENT_STORE_OP_STORE, clears[0] },
			{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, impl->Depth.View,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_ATTACHMENT_LOAD_OP_CLEAR,
					VK_ATTACHMENT_STORE_OP_DONT_CARE, clears[1] } };
		const VkRenderingInfo pass = { VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0,
				{ {}, impl->Extent }, 1, 0, 1, &attachments[0], &attachments[1], nullptr };
		impl->Vk.vkCmdBeginRendering(impl->commands(), &pass);
	}
	else
	{
		const VkRenderPassBeginInfo pass = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr,
				impl->Passes[clearColor ? 1 : 0],
				impl->Framebuffers[impl->ImageIndex], { {}, impl->Extent }, 2, clears };
		impl->Vk.vkCmdBeginRenderPass(impl->commands(), &pass, VK_SUBPASS_CONTENTS_INLINE);
	}

	impl->Encoding = true;
	impl->forgetBindings();
	setViewPort(ViewPort);
	return true;
}


static void endEncoding(CVulkanDriver::SImpl* impl)
{
	if (!impl->Encoding)
		return;
	if (impl->Use13)
	{
		impl->Vk.vkCmdEndRendering(impl->commands());
		transition(impl, impl->commands(), impl->Images[impl->ImageIndex],
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
	else
		impl->Vk.vkCmdEndRenderPass(impl->commands());
	impl->Encoding = false;
}


//! The panel turned since the chain was made; chains the system turns always report SUBOPTIMAL
static bool panelTurned(CVulkanDriver::SImpl* impl)
{
	if (!impl->Turn && impl->Transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		return false;
	VkSurfaceCapabilitiesKHR caps;
	const VkResult asked = impl->Vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(impl->Gpu,
			impl->Surface, &caps);
	return asked == VK_SUCCESS && caps.currentTransform != impl->Transform;
}


bool CVulkanDriver::endScene()
{
	CNullDriver::endScene();

	SImpl* impl = Impl;
	if (!impl->Started)
		return false;

	impl->Started = false;
	if (!impl->Submitted && !submitFrame(impl))
		return false;
	impl->Submitted = false;

	if (impl->HaveImage)
	{
		const VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1,
				&impl->RenderDone[impl->ImageIndex], 1,
				&impl->Swapchain, &impl->ImageIndex, nullptr };
		const VkResult result = impl->Vk.vkQueuePresentKHR(impl->Queue, &present);
		//! Remaking the chain after any error frees the image
		if (result == VK_ERROR_DEVICE_LOST)
			loseDevice(impl, "vkQueuePresentKHR");
		else if (result < VK_SUCCESS ||
				(result == VK_SUBOPTIMAL_KHR && panelTurned(impl)))
			impl->SwapchainStale = true;
		impl->HaveImage = false;
	}
	return true;
}


static bool submitFrame(CVulkanDriver::SImpl* impl)
{
	endEncoding(impl);

	CVulkanDriver::SImpl::SSlot& slot = impl->Slots[impl->RingIndex];
	VkResult result = impl->Vk.vkEndCommandBuffer(slot.Commands);

	const VkCommandBuffer buffers[2] = { slot.Uploads, slot.Commands };
	const u32 first = impl->Writes.empty() ? 1 : 0;
	if (!first && result == VK_SUCCESS)
	{
		result = impl->Vk.vkBeginCommandBuffer(slot.Uploads, &VULKAN_ONE_TIME_SUBMIT);
		if (result == VK_SUCCESS)
		{
			recordWrites(impl, slot.Uploads);
			result = impl->Vk.vkEndCommandBuffer(slot.Uploads);
		}
	}
	const bool recorded = result == VK_SUCCESS;

	impl->Vk.vkResetFences(impl->Device, 1, &slot.Done);
	const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	const u32 waits = impl->HaveImage ? 1 : 0;
	const VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, waits, &slot.ImageReady,
			&waitStage, 2 - first, buffers + first, waits,
			waits ? &impl->RenderDone[impl->ImageIndex] : nullptr };
	if (recorded)
		result = impl->Vk.vkQueueSubmit(impl->Queue, 1, &submit, slot.Done);
	if (result != VK_SUCCESS)
	{
		//! The copies stay pending for the next frame
		if (result == VK_ERROR_DEVICE_LOST)
			loseDevice(impl, "vkQueueSubmit");
		//! An empty submit consumes the semaphore and signals the fence
		const VkSubmitInfo empty = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, waits,
				&slot.ImageReady, &waitStage, 0, nullptr, 0, nullptr };
		const VkResult emptied = impl->Vk.vkQueueSubmit(impl->Queue, 1, &empty, slot.Done);
		if (emptied != VK_SUCCESS)
			loseDevice(impl, "an empty vkQueueSubmit");
		//! Rebuilds the swapchain to release the acquired image
		impl->SwapchainStale |= impl->HaveImage;
		impl->HaveImage = false;
		return false;
	}
	impl->Writes.swap(impl->Staged[impl->RingIndex]);
	impl->WriteBytes = 0;
	impl->Submitted = true;
	return true;
}


#if 0
IImage* CVulkanDriver::createScreenShot(video::ECOLOR_FORMAT format, video::E_RENDER_TARGET target)
{
	if (format == ECF_UNKNOWN)
		format = getColorFormat();

	//! convert_viaFormat writes only these 4 formats
	SImpl* impl = Impl;
	if (!impl->Started || impl->Submitted || !impl->HaveImage || !impl->Readable ||
			target != ERT_FRAME_BUFFER || (format != ECF_A1R5G5B5 && format != ECF_R5G6B5 &&
			format != ECF_R8G8B8 && format != ECF_A8R8G8B8))
		return 0;

	const u32 width = impl->Extent.width;
	const u32 height = impl->Extent.height;
	SVulkanBuffer staging;
	if (!createBuffer(impl, width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging))
		return 0;

	endEncoding(impl);
	const VkCommandBuffer commands = impl->commands();
	const VkImage frame = impl->Images[impl->ImageIndex];
	transition(impl, commands, frame, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	const VkBufferImageCopy copy = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, {},
			{ width, height, 1 } };
	impl->Vk.vkCmdCopyImageToBuffer(commands, frame, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			staging.Buffer, 1, &copy);
	transition(impl, commands, frame, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	if (!submitFrame(impl))
	{
		impl->Started = false;
		destroyBuffer(impl, staging);
		return 0;
	}
	impl->Vk.vkWaitForFences(impl->Device, 1, &impl->Slots[impl->RingIndex].Done, VK_TRUE,
			UINT64_MAX);

	//! A turned image is read back as the screen, with width and height swapped
	const u32* pixels = (const u32*)staging.Mapped;
	std::vector<u32> unturned;
	if (impl->Turn)
	{
		unturned.resize(width * height);
		for (u32 y = 0; y < width; ++y)
			for (u32 x = 0; x < height; ++x)
				unturned[y * height + x] = impl->Turn > 0 ?
						pixels[x * width + width - 1 - y] :
						pixels[(height - 1 - x) * width + y];
		pixels = unturned.data();
	}
	const core::dimension2du size = impl->Turn ? core::dimension2du(height, width) :
			core::dimension2du(width, height);

	//! ECF_A8R8G8B8 is B, G, R, A in memory; both start top left
	IImage* image = new CImage(ECF_A8R8G8B8, size);
	if (impl->ColorFormat == VK_FORMAT_R8G8B8A8_UNORM)
		CColorConverter::convert_A8R8G8B8toA8B8G8R8(pixels, width * height, image->getData());
	else
		memcpy(image->getData(), pixels, width * height * 4);
	destroyBuffer(impl, staging);

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


bool CVulkanDriver::bindMaterialPipeline(E_VERTEX_TYPE vType, u32 topology)
{
	SImpl* impl = Impl;

	const u32 compare = depthCompare();
	const bool write = depthWrite();
	const u32 cull = cullMode();

	ProgramBound = ShaderRenderer &&
			ShaderRenderer->bind(vType, topology, cull, compare, write, Material.Wireframe);
	if (ProgramBound)
		return true;

	const E_MATERIAL_TYPE base = ShaderRenderer ?
			ShaderRenderer->baseMaterial() : Material.MaterialType;
	const u32 blend = blendState(base);
	//! ALPHA_CHANNEL blends and also cuts at MaterialTypeParam
	const bool alphaTest = base == EMT_TRANSPARENT_ALPHA_CHANNEL ||
			base == EMT_TRANSPARENT_ALPHA_CHANNEL_REF;

	const CVulkanTexture* texture = static_cast<const CVulkanTexture*>(Material.getTexture(0));
	//! An empty texture reads as opaque black, as an incomplete one in OpenGL
	const bool textured = texture != 0;
	//! Alpha-tested materials take alpha from the texture alone
	const u32 alphaSource = textured && alphaTest ? 1 : 0;
	const VkPipeline pipeline = pipelineFor(impl, 0, VULKAN_FIXED_PROGRAM, vType, topology, blend,
			textured, alphaTest, cull, compare, write, Material.Wireframe, alphaSource);
	if (!pipeline)
		return false;
	bindPipeline(impl, pipeline);

	SImpl::STextureSet textures;
	textures.fill(impl->Black.View, impl->Sampler2D);
	if (textured)
	{
		if (texture->view())
			textures.Views[0] = texture->view();
		textures.Samplers[0] = samplerFor(impl, Material.TextureLayer[0], Material.UseMipMaps);
	}
	return bindTextures(impl, textures);
}


bool CVulkanDriver::bindDrawState()
{
	if (ProgramBound)
		return true;

	SImpl* impl = Impl;
	const SDriverUniforms uniforms = fixedUniforms();
	const SDriverFog fog = fixedFog(ShaderRenderer ?
			ShaderRenderer->baseMaterial() : Material.MaterialType);

	SImpl::SPlaced placed[2] = { stageBlock(impl, &uniforms, sizeof(uniforms)), impl->FogPlaced };
	if (!placed[0].Block)
		return false;

	if (!impl->FogPlaced.Block || memcmp(&impl->Fog, &fog, sizeof(fog)) != 0)
	{
		placed[1] = stageBlock(impl, &fog, sizeof(fog));
		if (!placed[1].Block)
			return false;
		impl->FogPlaced = placed[1];
		impl->Fog = fog;
	}

	bindUniforms(impl, placed);
	return true;
}


CNullDriver::SHWBufferLink* CVulkanDriver::createHardwareBuffer(const scene::IMeshBuffer* mb)
{
	return mb && Impl->Device ? createMeshLink(Impl->Meshes, mb) : 0;
}


void CVulkanDriver::drawLink(CNullDriver::SHWBufferLink* each, u32& bound)
{
	SImpl* impl = Impl;
	SImpl::SMeshes::SLink* link = static_cast<SImpl::SMeshes::SLink*>(each);
	if (!link || !meshToDraw(impl->Meshes, link, impl->Encoding))
		return;
	const bool split = impl->Meshes.SplitPositions && link->VertexPool;
	const u32 layout = (u32)link->VertexType << 1 | (split ? 1 : 0);
	if (layout != bound)
	{
		impl->SplitVertices = split;
		bound = bindMaterialPipeline(link->VertexType, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) &&
				bindDrawState() ? layout : ~0u;
		impl->SplitVertices = false;
		if (bound != layout)
			return;
	}
	link->DrawnAt = impl->FrameCounter;
	//! Split or unaligned chunks bind at their offset, others bind the arena at 0
	const u32 stride = getVertexPitchFromType(link->VertexType);
	const u32 width = link->IndexType == EIT_32BIT ? 4 : 2;
	const u32 firstIndex = link->IndexPool ?
			(link->IndexPool->Range.Base + link->IndexOffset) / width : 0;
	const u32 at = link->VertexPool ?
			link->VertexPool->Range.Base + link->VertexOffset : 0;
	if (split)
	{
		const VkBuffer buffers[2] = { link->Vertices->Buffer, link->Vertices->Buffer };
		const VkDeviceSize offsets[2] = { at, at + link->VertexCount * 12 };
		impl->Vk.vkCmdBindVertexBuffers(impl->commands(), 0, 2, buffers, offsets);
		impl->BoundVertices = VK_NULL_HANDLE;
	}
	const bool atChunk = !split && at % stride;
	if (!split)
		bindVertices(impl, link->Vertices->Buffer, atChunk ? at : 0);
	bindIndices(impl, link->Indices->Buffer, 0, link->IndexType);
	impl->Vk.vkCmdDrawIndexed(impl->commands(), link->IndexCount, 1, firstIndex,
			split || atChunk ? 0 : (s32)(at / stride), 0);
}


void CVulkanDriver::deleteHardwareBuffer(CNullDriver::SHWBufferLink* link)
{
	if (link)
		Impl->Meshes.release(static_cast<SImpl::SMeshes::SLink*>(link));
	CNullDriver::deleteHardwareBuffer(link);
}


void CVulkanDriver::drawVertexPrimitiveList(const void* vertices, u32 vertexCount,
		const void* indexList, u32 primitiveCount, E_VERTEX_TYPE vType,
		scene::E_PRIMITIVE_TYPE pType, E_INDEX_TYPE iType)
{
	SImpl* impl = Impl;
	if (!primitiveCount || !vertexCount || !checkPrimitiveCount(primitiveCount))
		return;

	CNullDriver::drawVertexPrimitiveList(vertices, vertexCount, indexList, primitiveCount,
			vType, pType, iType);

	if (!impl->Encoding || !vertices || !indexList || !MaterialVisible)
		return;

	if (pType == scene::EPT_LINES)
		return drawLineList(vertices, vertexCount, indexList, primitiveCount, vType, iType);
	if (pType != scene::EPT_TRIANGLES)
		return;

	const u32 indexCount = primitiveCount * 3;
	const u32 stride = getVertexPitchFromType(vType);
	const u32 indexWidth = iType == EIT_32BIT ? 4 : 2;
	const auto [vertexBuffer, vertexAt] = stageInto(impl, vertices, vertexCount * stride);
	if (!vertexBuffer)
		return;
	const auto [indexBuffer, indexAt] = stageInto(impl, indexList, indexCount * indexWidth);
	if (!indexBuffer)
		return;

	if (!bindMaterialPipeline(vType, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) || !bindDrawState())
		return;
	bindVertices(impl, vertexBuffer, vertexAt);
	bindIndices(impl, indexBuffer, indexAt, iType);
	impl->Vk.vkCmdDrawIndexed(impl->commands(), indexCount, 1, 0, 0, 0);
}


//! Maps a screen rect into the pre-rotated swapchain image
static core::rect<s32> turnRect(const CVulkanDriver::SImpl* impl, const core::rect<s32>& r)
{
	//! The screen is the image with width and height swapped
	const s32 width = (s32)impl->Extent.height, height = (s32)impl->Extent.width;
	const core::position2d<s32>& a = r.UpperLeftCorner;
	const core::position2d<s32>& b = r.LowerRightCorner;
	if (impl->Turn > 0)
		return core::rect<s32>(height - b.Y, a.X, height - a.Y, b.X);
	if (impl->Turn < 0)
		return core::rect<s32>(a.Y, width - b.X, b.Y, width - a.X);
	return r;
}


static void setScissor(CVulkanDriver::SImpl* impl, const core::rect<s32>& area)
{
	core::rect<s32> r = turnRect(impl, area);
	r.clipAgainst(core::rect<s32>(0, 0, (s32)impl->Extent.width, (s32)impl->Extent.height));
	const VkRect2D rect = { { r.UpperLeftCorner.X, r.UpperLeftCorner.Y },
			{ (u32)r.getWidth(), (u32)r.getHeight() } };
	if (impl->BoundScissor && memcmp(&*impl->BoundScissor, &rect, sizeof(rect)) == 0)
		return;

	impl->Vk.vkCmdSetScissor(impl->commands(), 0, 1, &rect);
	impl->BoundScissor = rect;
}


bool CVulkanDriver::bind2DState()
{
	SImpl* impl = Impl;

	if (!impl->TwoD[0].Block)
	{
		const SDriverUniforms uniforms = { ortho2D(), core::matrix4() };
		const SDriverFog fog = {};

		const SImpl::SPlaced placed[2] = {
			stageBlock(impl, &uniforms, sizeof(uniforms), true),
			stageBlock(impl, &fog, sizeof(fog), true) };
		if (!placed[0].Block || !placed[1].Block)
			return false;
		std::copy(std::begin(placed), std::end(placed), impl->TwoD);
	}

	bindUniforms(impl, impl->TwoD);
	return true;
}


void CVulkanDriver::drawQuads2D(const ITexture* texture, const S3DVertex* quads, u32 count,
		const core::rect<s32>* clip, bool alpha, bool alphaChannel)
{
	SImpl* impl = Impl;
	if (!impl->Encoding || !count)
		return;

	const core::dimension2d<u32>& target = getCurrentRenderTargetSize();
	core::rect<s32> scissor(0, 0, (s32)target.Width, (s32)target.Height);
	if (clip)
		scissor.clipAgainst(*clip);
	if (scissor.getWidth() <= 0 || scissor.getHeight() <= 0)
		return;

	//! Quad indices up to the 16 bit limit; longer runs are batched
	core::array<u16>& order = impl->QuadOrder;
	const u32 batch = core::min_(count, 65536u / 4);
	while (order.size() < batch * 6)
	{
		const u16 base = (u16)((order.size() / 6) * 4);
		order.push_back(base); order.push_back(base + 1); order.push_back(base + 2);
		order.push_back(base); order.push_back(base + 2); order.push_back(base + 3);
	}

	const CVulkanTexture* vulkan = static_cast<const CVulkanTexture*>(texture);
	const bool textured = vulkan != 0;
	//! Alpha channel only with a texture; if unused, vertex alpha alone
	alphaChannel &= textured;
	const VkPipeline pipeline = pipelineFor(impl, 0, VULKAN_FIXED_PROGRAM, EVT_STANDARD,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, alpha || alphaChannel ? DRIVER_BLEND_ALPHA : 0,
			textured, false, 0, ECFN_DISABLED, false, false,
			textured && alpha && !alphaChannel ? 2 : 0);
	if (!pipeline)
		return;
	bindPipeline(impl, pipeline);

	SImpl::STextureSet textures;
	textures.fill(impl->Black.View, impl->Sampler2D);
	if (textured)
	{
		if (vulkan->view())
			textures.Views[0] = vulkan->view();
		const SMaterial& material = material2D();
		textures.Samplers[0] = samplerFor(impl, material.TextureLayer[0], material.UseMipMaps);
	}
	//! Everything that can fail comes before the scissor narrows, so it is whole for the next draw
	const u32 indexBytes = batch * 6 * (u32)sizeof(u16);
	const auto [indexBuffer, indexAt] = stageInto(impl, order.pointer(), indexBytes, 4, true);
	if (!indexBuffer || !bindTextures(impl, textures) || !bind2DState())
		return;

	setScissor(impl, scissor);
	for (u32 done = 0; done < count; done += batch)
	{
		const u32 run = core::min_(batch, count - done);
		const u32 vertexBytes = run * 4 * (u32)sizeof(S3DVertex);
		const auto [vertexBuffer, vertexAt] = stageInto(impl, quads + done * 4, vertexBytes,
				4, true);
		if (!vertexBuffer)
			break;
		bindVertices(impl, vertexBuffer, vertexAt);
		bindIndices(impl, indexBuffer, indexAt, EIT_16BIT);
		impl->Vk.vkCmdDrawIndexed(impl->commands(), run * 6, 1, 0, 0, 0);
	}

	setScissor(impl, core::recti(0, 0, (s32)target.Width, (s32)target.Height));
}


void CVulkanDriver::draw3DLine(const core::vector3df& start, const core::vector3df& end,
		SColor color)
{
	SImpl* impl = Impl;
	if (!impl->Encoding || !MaterialVisible)
		return;

	S3DVertex line[4] = { S3DVertex(start.X, start.Y, start.Z, 0, 0, 1, color, 0, 0),
			S3DVertex(end.X, end.Y, end.Z, 0, 0, 1, color, 0, 0) };

	const bool quad = Material.Thickness > 1.0f && thickLineQuad(Matrices[ETS_PROJECTION],
			Matrices[ETS_VIEW], Matrices[ETS_WORLD], ViewPort, start, end, Material.Thickness,
			color, line);

	const u32 count = quad ? 4 : 2;
	const auto [vertexBuffer, vertexAt] = stageInto(impl, line, count * sizeof(S3DVertex));
	if (!vertexBuffer)
		return;

	//! No culling: the quad's winding flips with the line's direction
	const VkPipeline pipeline = pipelineFor(impl, 0, VULKAN_FIXED_PROGRAM, EVT_STANDARD,
			quad ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
			color.getAlpha() < 255 ? DRIVER_BLEND_ALPHA : 0, false, false,
			0, depthCompare(), depthWrite(), false);
	if (!pipeline)
		return;

	ProgramBound = false;
	bindPipeline(impl, pipeline);
	//! Binds black textures; the fixed stages declare the set
	SImpl::STextureSet textures;
	textures.fill(impl->Black.View, impl->Sampler2D);
	if (!bindTextures(impl, textures) || !bindDrawState())
		return;
	bindVertices(impl, vertexBuffer, vertexAt);
	impl->Vk.vkCmdDraw(impl->commands(), count, 1, 0, 0);
}


//! Per stage: u32 name length, padded name, u32 word count, words
bool CVulkanDriver::addPrecompiledShaderLibrary(const void* data, u32 size)
{
	SImpl* impl = Impl;
	if (!impl->Device || !data || size < 8)
		return false;

	const u8* at = (const u8*)data;
	const u8* const end = at + size;
	const auto next = [&at] { u32 value; memcpy(&value, at, 4); at += 4; return value; };
	impl->Modules.clear();
	while (at + 4 <= end)
	{
		const u32 length = next();
		//! Checks counts against the bytes left before scaling them
		if (length > (size_t)(end - at))
			return false;
		const u32 padded = (length + 3) & ~3u;
		if ((size_t)(end - at) < (size_t)padded + 4)
			return false;
		const std::string name((const c8*)at, length);
		at += padded;

		const u32 words = next();
		if (words > (size_t)(end - at) / 4)
			return false;
		std::vector<u32>& module = impl->Modules[name].Words;
		module.resize(words);
		memcpy(module.data(), at, (size_t)words * 4);
		at += (size_t)words * 4;
	}
	return !impl->Modules.empty();
}


bool CVulkanDriver::addPrecompiledShader(const c8* name, const void* vertexProgram, u32,
		const void* fragmentProgram, u32)
{
	SImpl* impl = Impl;
	if (impl->Modules.empty() || !name || !vertexProgram || !fragmentProgram)
		return false;

	//! vertexProgram and fragmentProgram name stages in the library
	const std::string stages[2] = { (const c8*)vertexProgram, (const c8*)fragmentProgram };
	SVulkanShaderProgram* program = new SVulkanShaderProgram();
	program->Name = name;

	u32 textures = 0;
	bool built = true;
	for (u32 s = 0; s < 2 && built; ++s)
	{
		const auto found = impl->Modules.find(stages[s]);
		auto* stage = found != impl->Modules.end() ? &found->second : nullptr;
		std::vector<u32> words = stage ? stage->Words : std::vector<u32>();
		//! Only the fragment stage may have textures
		built = stage && reflectStage(words, s, program, textures) &&
				program->BlockBytes[s] <= VULKAN_UNIFORM_RANGE && textures <= VULKAN_LAYERS &&
				(s == 1 || !textures);
		//! Reflection binds a stage the same way in every program, so programs share its module
		if (built && !stage->Module)
		{
			stage->Module = moduleFrom(impl, words.data(), words.size());
			if (stage->Module)
				impl->StageModules.push_back(stage->Module);
		}
		if (built)
			program->Modules[s] = stage->Module;
		if (built && s == 0)
			program->Inputs = inputLocations(words.data(), words.size());
		built = built && program->Modules[s];
	}

	if (!built)
	{
		os::Printer::log("The Vulkan shader library holds no usable stages for", name, ELL_ERROR);
		delete program;
		return false;
	}

	for (u32 i = 0; i < program->Uniforms.size(); ++i)
		for (u32 j = i + 1; j < program->Uniforms.size(); ++j)
			if (program->Uniforms[j].Name == program->Uniforms[i].Name)
			{
				program->Uniforms[i].Next = (s32)j;
				break;
			}

	Programs.push_back(program);
	return true;
}


void CVulkanDriver::setPrecompiledConstant(u32 id, bool value)
{
	setGameConstant(id, value ? VK_TRUE : VK_FALSE);
}


void CVulkanDriver::setPrecompiledConstant(u32 id, f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	setGameConstant(id, bits);
}


void CVulkanDriver::setPrecompiledConstant(u32 id, s32 value)
{
	setGameConstant(id, (u32)value);
}


//! The game's constants live in the pipelines: a new value remakes them all
void CVulkanDriver::setGameConstant(u32 id, u32 bits)
{
	SImpl* impl = Impl;
	if (id < 16)
		return;
	const u32 index = id - 16;
	std::vector<u32>& constants = impl->GameConstants;
	if (index < constants.size() && constants[index] == bits)
		return;
	if (constants.size() <= index)
		constants.resize(index + 1, 0);
	constants[index] = bits;
	if (!impl->Device || !waitIdle(impl))
		return;
	for (const auto& [key, pipeline] : impl->Pipelines)
		impl->Vk.vkDestroyPipeline(impl->Device, pipeline, 0);
	impl->Pipelines.clear();
	impl->LastPipelineKey = UINT64_MAX;
	impl->LastPipeline = VK_NULL_HANDLE;
	impl->forgetBindings();
}


void CVulkanDriver::setMaterial(const SMaterial& material)
{
	//! Makes programs gather their textures again
	++Impl->MaterialCounter;
	CDriverCommon::setMaterial(material);
}


CVulkanShaderRenderer* CVulkanDriver::newShaderRenderer(u32 index,
		IShaderConstantSetCallBack* callback, E_MATERIAL_TYPE base, s32 userData)
{
	return new CVulkanShaderRenderer(this, Programs[index], index + 1, callback, base, userData);
}


void CVulkanDriver::setViewPort(const core::rect<s32>& area, bool clipToRenderTarget)
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

	SImpl* impl = Impl;
	if (!impl->Encoding)
		return;

	//! Negative height: clip space y points up
	const core::rect<s32> r = turnRect(impl, vp);
	const VkViewport port = { (f32)r.UpperLeftCorner.X, (f32)r.LowerRightCorner.Y,
			(f32)r.getWidth(), -(f32)r.getHeight(), 0.0f, 1.0f };
	impl->Vk.vkCmdSetViewport(impl->commands(), 0, 1, &port);

	if (!impl->BoundScissor)
	{
		const core::dimension2d<u32>& target = getCurrentRenderTargetSize();
		setScissor(impl, core::recti(0, 0, (s32)target.Width, (s32)target.Height));
	}
}


//! A full-screen triangle that writes the depth alone, for clears inside a pass
static bool drawDepthClear(CVulkanDriver::SImpl* impl, f32 depth)
{
	//! Blending keeps the colour: Adreno 5xx and older 6xx write it under a zero mask (PPSSPP)
	const VkPipeline pipeline = pipelineFor(impl, &impl->Clear, VULKAN_CLEAR_PROGRAM, EVT_STANDARD,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, EBO_ADD << 16 | EBF_ONE << 8 | EBF_ONE, false,
			false, 0, ECFN_ALWAYS, true, false);
	if (!pipeline)
		return false;
	const f32 block[4] = { depth };
	const CVulkanDriver::SImpl::SPlaced placed[2] = { stageBlock(impl, block, sizeof(block)) };
	if (!placed[0].Block)
		return false;
	bindPipeline(impl, pipeline);
	bindUniforms(impl, placed);
	const VkViewport port = { 0.0f, 0.0f, (f32)impl->Extent.width, (f32)impl->Extent.height,
			0.0f, 1.0f };
	impl->Vk.vkCmdSetViewport(impl->commands(), 0, 1, &port);
	const VkRect2D whole = { {}, impl->Extent };
	impl->Vk.vkCmdSetScissor(impl->commands(), 0, 1, &whole);
	impl->Vk.vkCmdDraw(impl->commands(), 3, 1, 0, 0);
	return true;
}


void CVulkanDriver::clearBuffers(u16 flag, SColor color, f32 depth, u8)
{
	SImpl* impl = Impl;
	if (!(flag & (ECBF_COLOR | ECBF_DEPTH)) || !impl->Encoding)
		return;

	//! Clears the whole target, whatever the viewport
	VkClearAttachment clears[2] = { { VK_IMAGE_ASPECT_COLOR_BIT, 0, {} },
			{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, {} } };
	clears[0].clearValue.color = { { color.getRed() / 255.0f, color.getGreen() / 255.0f,
			color.getBlue() / 255.0f, color.getAlpha() / 255.0f } };
	clears[1].clearValue.depthStencil = { depth, 0 };
	const u32 first = (flag & ECBF_COLOR) ? 0 : 1;
	const VkClearRect whole = { { {}, impl->Extent }, 0, 1 };
	if (!(first && impl->Clear.Modules[0] && impl->Clear.Modules[1] && drawDepthClear(impl, depth)))
		impl->Vk.vkCmdClearAttachments(impl->commands(), ((flag & ECBF_DEPTH) ? 2 : 1) - first,
				clears + first, 1, &whole);

	//! Qualcomm loses the bound buffers over a clear: everything binds again
	impl->forgetBindings();
	setViewPort(ViewPort);
}


void CVulkanDriver::OnResize(const core::dimension2d<u32>& size)
{
	CNullDriver::OnResize(size);
	ViewPort = core::recti(0, 0, (s32)size.Width, (s32)size.Height);

	VkExtent2D screen = Impl->Extent;
	if (Impl->Turn)
		std::swap(screen.width, screen.height);
	Impl->SwapchainStale |= size.Width != screen.width || size.Height != screen.height;
}


//! The system reclaims a hidden app's swapchain buffers; the first frame back remakes the chain
void CVulkanDriver::OnBackground()
{
	SImpl* impl = Impl;
	if (!impl->Swapchain || impl->Started)
		return;
	waitIdle(impl);
	destroySwapchain(impl);
}


bool CVulkanDriver::checkDriverReset()
{
	return Impl->Lost;
}


core::dimension2du CVulkanDriver::getMaxTextureSize() const
{
	const u32 side = core::min_(Impl->Properties.limits.maxImageDimension2D,
			VULKAN_MAX_TEXTURE_SIZE);
	return core::dimension2du(side, side);
}


ITexture* CVulkanDriver::createDeviceDependentTexture(const io::path& name, IImage* image)
{
	if (!image || !Impl->Device)
		return 0;

	return new CVulkanTexture(name, image, this, getTextureCreationFlag(ETCF_ALLOW_MEMORY_COPY));
}


IVideoDriver* createVulkanDriver(const SIrrlichtCreationParameters& params,
		io::IFileSystem* io, CIrrDeviceSDL* device)
{
	CVulkanDriver* driver = new CVulkanDriver(params, io, device);
	if (!driver->genericDriverInit())
	{
		driver->drop();
		return 0;
	}
	return driver;
}

} // end namespace irr::video

#endif // _IRR_COMPILE_WITH_VULKAN_
