// Copyright (C) 2026 Maksym H.
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#ifndef IRR_C_DRIVER_COMMON_H_INCLUDED
#define IRR_C_DRIVER_COMMON_H_INCLUDED

#include "CImage.h"
#include "CNullDriver.h"
#include "IFileSystem.h"
#include "IMaterialRenderer.h"
#include "IMaterialRendererServices.h"
#include "IReadFile.h"
#include "IShaderConstantSetCallBack.h"
#include "S3DVertex.h"
#include "SColor.h"
#include "irrArray.h"
#include "matrix4.h"
#include "os.h"
#include "rect.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

//! The Metal driver's parts that other drivers can share
namespace irr::video
{

//! The widest line drawn, as wide as OpenGL goes
static const f32 DRIVER_MAX_LINE_WIDTH = 16.0f;

inline bool readWhole(io::IFileSystem* fs, const io::path& path, core::array<c8>& out)
{
	io::IReadFile* file = fs ? fs->createAndOpenFile(path) : 0;
	if (!file)
		return false;

	out.set_used((u32)file->getSize());
	const bool read = out.size() && file->read(out.pointer(), out.size()) == (size_t)out.size();
	file->drop();
	return read;
}

//! Pixel quads by OpenGL's diamond exit rule, for ends at +0.375
inline void linePixels(const core::position2d<s32>& start, const core::position2d<s32>& end,
		SColor color, core::array<S3DVertex>& quads)
{
	const s32 dx = end.X - start.X, dy = end.Y - start.Y;
	const bool xMajor = core::abs_(dx) >= core::abs_(dy);
	const s32 minor = xMajor ? dy : dx;
	const s32 steps = core::abs_(xMajor ? dx : dy);
	const s32 step = (xMajor ? dx : dy) > 0 ? 1 : -1;
	//! Corner ties go left or above, below on shallow lines falling right
	const s32 tie = xMajor && (dx > 0) == (dy > 0) ? 0 : -1;
	const s32 den = 8 * steps;

	quads.set_used(0);
	for (s32 k = 0; k <= steps; ++k)
	{
		//! Minor axis offset at the middle of the step, in 1/den pixels
		const s32 num = 3 * steps + minor * (8 * k + step);
		const s32 across = (num >= 0 ? num / den : -((den - 1 - num) / den)) +
				(num % den == 0 ? tie : 0);
		const f32 x = (f32)(xMajor ? start.X + step * k : start.X + across);
		const f32 y = (f32)(xMajor ? start.Y + across : start.Y + step * k);
		quads.push_back(S3DVertex(x, y, 0, 0, 0, 1, color, 0, 0));
		quads.push_back(S3DVertex(x + 1, y, 0, 0, 0, 1, color, 0, 0));
		quads.push_back(S3DVertex(x + 1, y + 1, 0, 0, 0, 1, color, 0, 0));
		quads.push_back(S3DVertex(x, y + 1, 0, 0, 0, 1, color, 0, 0));
	}
}

//! A 3D line as a fixed pixel width quad, clipped at the near plane
inline bool thickLineQuad(const core::matrix4& projection, const core::matrix4& view,
		const core::matrix4& world, const core::recti& viewport, core::vector3df start,
		core::vector3df end, f32 thickness, SColor color, S3DVertex* quad)
{
	//! Perspective projections only
	if (core::iszero(projection[11]))
		return false;

	const core::matrix4 modelview = view * world;
	core::matrix4 inverse;
	if (!modelview.getInverse(inverse))
		return false;

	modelview.transformVect(start);
	modelview.transformVect(end);

	//! Where the projection sends the near plane to depth 0
	const f32 nearZ = core::max_(-projection[14] / projection[10], core::ROUNDING_ERROR_f32);
	if (start.Z < nearZ && end.Z < nearZ)
		return false;
	if (start.Z < nearZ)
		start = end + (start - end) * ((end.Z - nearZ) / (end.Z - start.Z));
	else if (end.Z < nearZ)
		end = start + (end - start) * ((start.Z - nearZ) / (start.Z - end.Z));

	//! Pixels a view unit covers at unit depth
	const f32 scaleX = projection[0] * (f32)viewport.getWidth() * 0.5f;
	const f32 scaleY = projection[5] * (f32)viewport.getHeight() * 0.5f;

	const f32 dx = end.X * scaleX / end.Z - start.X * scaleX / start.Z;
	const f32 dy = end.Y * scaleY / end.Z - start.Y * scaleY / start.Z;
	if (core::abs_(dx) < core::ROUNDING_ERROR_f32 && core::abs_(dy) < core::ROUNDING_ERROR_f32)
		return false;

	//! An x-major line spreads vertically, a y-major one horizontally
	const f32 half = core::min_(thickness, DRIVER_MAX_LINE_WIDTH) * 0.5f;
	const bool xMajor = core::abs_(dx) >= core::abs_(dy);
	const f32 offX = xMajor ? 0.f : half / scaleX;
	const f32 offY = xMajor ? half / scaleY : 0.f;

	//! Back to view units at the depth of each end
	const core::vector3df startOffset(offX * start.Z, offY * start.Z, 0.f);
	const core::vector3df endOffset(offX * end.Z, offY * end.Z, 0.f);

	core::vector3df corners[4] = { start - startOffset, start + startOffset, end - endOffset,
			end + endOffset };
	for (u32 i = 0; i < 4; ++i)
	{
		inverse.transformVect(corners[i]);
		quad[i] = S3DVertex(corners[i].X, corners[i].Y, corners[i].Z, 0, 0, 1, color, 0, 0);
	}
	return true;
}


//! Fixed pipeline constants, in 16 byte rows as MSL and std140 lay them out
struct SDriverUniforms { core::matrix4 worldViewProj, worldView; };
struct SDriverFog { f32 color[4]; f32 scale, end; u32 mode; f32 alphaRef; };
static_assert(sizeof(SDriverUniforms) == 128 && sizeof(SDriverFog) == 32);


//! A blend state: E_BLEND_OPERATION at bit 16 over the factors, packed as
//! pack_textureBlendFuncSeparate packs them
static constexpr u32 DRIVER_BLEND_ALPHA = EBO_ADD << 16 | EBF_SRC_ALPHA << 12 |
		EBF_ONE_MINUS_SRC_ALPHA << 8 | EBF_SRC_ALPHA << 4 | EBF_ONE_MINUS_SRC_ALPHA;


//! Blend of a material type: 0 none
inline u32 blendFor(E_MATERIAL_TYPE type)
{
	return type == EMT_TRANSPARENT_ALPHA_CHANNEL ||
			type == EMT_ONETEXTURE_BLEND ? DRIVER_BLEND_ALPHA : 0;
}


//! Chunk sizes of the shared mesh pools: powers of 2 and 1.5 times them
static constexpr u32 DRIVER_SHARED_SIZES[] = { 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536,
		2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 49152, 65536, 98304, 131072,
		196608, 262144, 393216, 524288, 786432, 1048576, 1572864, 2097152, 3145728, 4194304 };
static constexpr u32 DRIVER_SHARED_CLASSES = std::size(DRIVER_SHARED_SIZES);

//! Smallest chunk size index for the bytes, or DRIVER_SHARED_CLASSES
inline u32 sharedSizeFor(u32 bytes)
{
	return u32(std::lower_bound(std::begin(DRIVER_SHARED_SIZES), std::end(DRIVER_SHARED_SIZES),
			bytes) - std::begin(DRIVER_SHARED_SIZES));
}


//! Mesh buffers on the GPU: static meshes in equal chunks of shared pools, the rest in their own
//! buffers; TStore holds the memory, TLinkBase is CNullDriver::SHWBufferLink
template <class TStore, class TLinkBase>
class CDriverMeshBuffers
{
public:
	using SHandle = typename TStore::SHandle;

	//! A pool cut into equal chunks
	struct SPool
	{
		//! Chunks it can still hand out
		u32 room() const { return u32(Free.size()) + (Bytes - Carved) / Chunk; }

		typename TStore::SRange Range;
		//! Index in DRIVER_SHARED_SIZES, kind, chunk length and pool length
		u32 Size = 0;
		u32 Kind = 0;
		u32 Chunk = 0;
		u32 Bytes = 0;
		//! Bytes carved from the start, chunks in use, and chunks freed
		u32 Carved = 0;
		u32 Used = 0;
		std::vector<u32> Free;
		//! Index in Pools, index in its Open list or -1, and that list:
		//! 0 over a quarter used, 1 the rest
		u32 Place = 0;
		s32 Open = -1;
		u32 Bucket = 0;
		//! Takes no new chunks while its chunks move out, then goes
		bool Draining = false;
		//! Holds one chunk the size of its data and goes once it is free
		bool Exact = false;
	};

	//! A mesh buffer: buffers of its own, or chunks of pools
	struct SLink : public TLinkBase
	{
		SLink(const scene::IMeshBuffer* mb) : TLinkBase(mb) {}

		SHandle Vertices = {};
		SHandle Indices = {};
		SPool* VertexPool = nullptr;
		SPool* IndexPool = nullptr;
		u32 VertexOffset = 0;
		u32 IndexOffset = 0;
		u32 IndexCount = 0;
		u64 DrawnAt = 0;
		E_VERTEX_TYPE VertexType = EVT_STANDARD;
		E_INDEX_TYPE IndexType = EIT_16BIT;
		bool Shared = false;
	};

	~CDriverMeshBuffers()
	{
		for (SPool* pool : Pools)
			delete pool;
	}

	//! Copies a mesh buffer's data when it changed; false when there is none to draw
	bool update(SLink* link)
	{
		const scene::IMeshBuffer* mb = link->MeshBuffer;
		if (!mb)
			return false;
		//! Up to date while the change counters match
		if (link->Vertices && link->ChangedID_Vertex == mb->getChangedID_Vertex() &&
				link->ChangedID_Index == mb->getChangedID_Index())
			return true;

		const u32 vertexCount = mb->getVertexCount();
		const u32 indexCount = mb->getIndexCount();
		if (!vertexCount || !indexCount)
			return false;

		const u32 stride = getVertexPitchFromType(mb->getVertexType());
		const u32 vertexBytes = vertexCount * stride;
		const u32 indexBytes = indexCount * (mb->getIndexType() == EIT_32BIT ? 4 : 2);
		//! A mesh grown past the largest chunk moves to buffers of its own
		if (link->Shared && (sharedSizeFor(vertexBytes) >= DRIVER_SHARED_CLASSES ||
				sharedSizeFor(indexBytes) >= DRIVER_SHARED_CLASSES))
		{
			give(link->VertexPool, link->Vertices, link->VertexOffset);
			give(link->IndexPool, link->Indices, link->IndexOffset);
			link->Shared = false;
		}

		const bool filled = link->Shared ?
				put(link->VertexPool, link->Vertices, link->VertexOffset,
						sharedSizeFor(vertexBytes),
						Store.kindOf(stride), mb->getVertices(), vertexBytes) &&
				put(link->IndexPool, link->Indices, link->IndexOffset, sharedSizeFor(indexBytes), 0,
						mb->getIndices(), indexBytes) :
				Store.fillOwn(link->Vertices, link->DrawnAt, mb->getVertices(), vertexBytes) &&
				Store.fillOwn(link->Indices, link->DrawnAt, mb->getIndices(), indexBytes);
		if (!filled)
			return false;

		link->IndexCount = indexCount;
		link->VertexType = mb->getVertexType();
		link->IndexType = mb->getIndexType();
		link->ChangedID_Vertex = mb->getChangedID_Vertex();
		link->ChangedID_Index = mb->getChangedID_Index();
		return true;
	}

	//! Gives a mesh buffer's chunks back and retires its own buffers
	void release(SLink* link)
	{
		give(link->VertexPool, link->Vertices, link->VertexOffset);
		give(link->IndexPool, link->Indices, link->IndexOffset);
		Store.retire(link->Vertices);
		Store.retire(link->Indices);
	}

	//! At a frame's start: reuses what the ring's last frame gave back; every 100 frames drains
	//! sparse pools
	void advance(u32 ring, u64 frame, const std::list<TLinkBase*>& links)
	{
		Ring = ring;
		for (auto [pool, offset] : Freed[Ring])
		{
			pool->Free.push_back(offset);
			--pool->Used;
			place(pool);
			const std::vector<SPool*>* open = Open[pool->Size][pool->Kind];
			if (!pool->Used && (pool->Exact || pool->Draining ||
					open[0].size() + open[1].size() > 1))
				drop(pool);
		}
		Freed[Ring].clear();

		if (frame % 100)
			return;
		const bool pools = drain();
		if (!Store.drain() && !pools)
			return;
		for (TLinkBase* each : links)
		{
			SLink* link = static_cast<SLink*>(each);
			move(link->VertexPool, link->Vertices, link->VertexOffset);
			move(link->IndexPool, link->Indices, link->IndexOffset);
		}
	}

	//! Files a pool in Open by use; unlists it if full or draining
	void place(SPool* pool)
	{
		const bool open = !pool->Draining && pool->room();
		const u32 bucket = pool->Used * 4 * pool->Chunk > pool->Bytes ? 0 : 1;
		if (pool->Open >= 0 && (!open || bucket != pool->Bucket))
			unlist(pool);
		if (open && pool->Open < 0)
		{
			std::vector<SPool*>& pools = Open[pool->Size][pool->Kind][bucket];
			pool->Bucket = bucket;
			pool->Open = (s32)pools.size();
			pools.push_back(pool);
		}
	}

	//! Frees a pool with no chunks in use
	void drop(SPool* pool)
	{
		if (pool->Open >= 0)
			unlist(pool);
		Pools[pool->Place] = Pools.back();
		Pools[pool->Place]->Place = pool->Place;
		Pools.pop_back();
		Store.free(pool->Range, pool->Bytes);
		delete pool;
	}

	TStore Store;
	std::vector<SPool*> Pools;

private:
	void unlist(SPool* pool)
	{
		std::vector<SPool*>& pools = Open[pool->Size][pool->Kind][pool->Bucket];
		pools[(size_t)pool->Open] = pools.back();
		pools[(size_t)pool->Open]->Open = pool->Open;
		pools.pop_back();
		pool->Open = -1;
	}

	//! Takes a chunk from the pool in the fullest memory, over a quarter used pools
	//! first, else from a new pool; a class the store sizes to the data never has open pools
	SPool* take(u32 size, u32 kind, u32 bytes, u32& offset)
	{
		const u32 exact = Store.exactBytes(size, bytes);
		SPool* taken = nullptr;
		for (u32 bucket = 0; bucket < 2 && !taken; ++bucket)
			for (SPool* pool : Open[size][kind][bucket])
				if (!taken || Store.fullness(pool->Range) > Store.fullness(taken->Range))
					taken = pool;
		if (!taken)
		{
			taken = new SPool();
			taken->Size = size;
			taken->Kind = kind;
			taken->Exact = exact != 0;
			taken->Chunk = exact ? exact : Store.chunkBytes(size, kind);
			taken->Bytes = exact ? exact : Store.poolBytes(size, kind);
			if (!Store.make(taken->Range, taken->Bytes))
			{
				delete taken;
				return nullptr;
			}
			taken->Place = (u32)Pools.size();
			Pools.push_back(taken);
		}

		if (!taken->Free.empty())
		{
			offset = taken->Free.back();
			taken->Free.pop_back();
		}
		else
		{
			offset = taken->Carved;
			taken->Carved += taken->Chunk;
		}
		++taken->Used;
		place(taken);
		return taken;
	}

	//! Gives a chunk back, reusable once no frame in flight reads it
	void give(SPool*& pool, SHandle& handle, u32& offset)
	{
		if (!pool)
			return;
		Freed[Ring].emplace_back(pool, offset);
		pool = nullptr;
		handle = SHandle();
		offset = 0;
	}

	//! Copies bytes into a new chunk of the size and kind, then gives the old chunk back
	bool put(SPool*& pool, SHandle& handle, u32& offset, u32 size, u32 kind, const void* data,
			u32 bytes)
	{
		u32 at = 0;
		SPool* taken = take(size, kind, bytes, at);
		if (!taken)
			return false;
		memcpy(Store.mapped(taken->Range) + at, data, bytes);
		Store.written(taken->Range, at, bytes);
		give(pool, handle, offset);
		pool = taken;
		handle = Store.handle(taken->Range);
		offset = at;
		return true;
	}

	void move(SPool*& pool, SHandle& handle, u32& offset)
	{
		if (pool && pool->Draining)
			put(pool, handle, offset, pool->Size, pool->Kind, Store.mapped(pool->Range) + offset,
					pool->Chunk);
	}

	//! Marks the emptiest pools to drain into fuller ones, within the store's budget
	bool drain()
	{
		bool draining = false;
		u32 moving = 0;
		for (auto& kinds : Open)
			for (std::vector<SPool*>* open : kinds)
			{
				if (open[0].size() + open[1].size() < 2)
					continue;
				u32 room = 0;
				for (u32 bucket = 0; bucket < 2; ++bucket)
					for (const SPool* pool : open[bucket])
						room += pool->room();

				std::vector<SPool*> sparse(open[1]);
				std::sort(sparse.begin(), sparse.end(),
						[](const SPool* a, const SPool* b) { return a->Used < b->Used; });
				for (SPool* pool : sparse)
				{
					if (room < pool->room() + pool->Used ||
							moving + pool->Used * pool->Chunk > TStore::DrainBytes)
						break;
					room -= pool->room() + pool->Used;
					if (!pool->Used)
					{
						drop(pool);
						continue;
					}
					moving += pool->Used * pool->Chunk;
					pool->Draining = true;
					place(pool);
					draining = true;
				}
			}
		return draining;
	}

	//! Pools with room by size and kind, and chunks given back per ring slot
	std::vector<SPool*> Open[DRIVER_SHARED_CLASSES][TStore::Kinds][2];
	std::vector<std::pair<SPool*, u32> > Freed[TStore::Frames];
	u32 Ring = 0;
};


//! A 16 bit image stays 16 bit where the device takes it, unless the game asks for 32 bits
inline ECOLOR_FORMAT textureFormatFor(const IVideoDriver* driver, bool packed, ECOLOR_FORMAT format)
{
	const bool sixteen = (format == ECF_A1R5G5B5 || format == ECF_R5G6B5) &&
			!driver->getTextureCreationFlag(ETCF_ALWAYS_32_BIT);
	return packed && sixteen ? format : ECF_A8R8G8B8;
}


//! A texture on the GPU; the image it keeps, or a copy from lock to unlock, stands for its pixels
class CDriverTexture : public ITexture
{
public:
	~CDriverTexture() IRR_OVERRIDE
	{
		if (Locked)
			Locked->drop();
		if (Image)
			Image->drop();
	}

	void* lock(E_TEXTURE_LOCK_MODE mode = ETLM_READ_WRITE, u32 mipmapLevel = 0, u32 layer = 0,
			E_TEXTURE_LOCK_FLAGS = ETLF_FLIP_Y_UP_RTT) IRR_OVERRIDE
	{
		LockReadOnly = mode == ETLM_READ_ONLY;
		if (Image)
			return Image->getData();

		//! Only level 0 of a single layer exists; render targets stay on the GPU
		if (!made() || mipmapLevel || layer || IsRenderTarget)
			return 0;

		if (!Locked)
		{
			Locked = new CImage(ColorFormat, Size);
			if (mode != ETLM_WRITE_ONLY)
				read(Locked);
		}
		return Locked->getData();
	}

	void unlock() IRR_OVERRIDE
	{
		IImage* const written = LockReadOnly ? 0 : (Image ? Image : Locked);
		LockReadOnly = false;
		if (written)
			write(written);

		if (Locked)
		{
			Locked->drop();
			Locked = 0;
		}
	}

	//! Every upload rebuilds the levels
	void regenerateMipMapLevels(void* = 0, u32 = 0) IRR_OVERRIDE {}

protected:
	CDriverTexture(const io::path& name, E_DRIVER_TYPE driver) : ITexture(name, ETT_2D)
	{
		DriverType = driver;
	}

	//! Takes the image's size; copies it in the format if the game keeps it or the formats differ
	CDriverTexture(const io::path& name, E_DRIVER_TYPE driver, IImage* image, ECOLOR_FORMAT format,
			bool keep)
		: CDriverTexture(name, driver)
	{
		OriginalColorFormat = image->getColorFormat();
		ColorFormat = format;
		OriginalSize = image->getDimension();
		Size = OriginalSize;
		Pitch = Size.Width * IImage::getBitsPerPixelFromFormat(ColorFormat) / 8;
		if (keep || OriginalColorFormat != ColorFormat)
		{
			Image = new CImage(ColorFormat, Size);
			image->copyTo(Image);
		}
	}

	//! Drops the copy of the first upload unless the game keeps it
	void settle(bool keep)
	{
		if (!keep && Image)
		{
			Image->drop();
			Image = 0;
		}
	}

	//! Whether the GPU holds the texture
	virtual bool made() const = 0;
	//! Reads the pixels back, in Size and ColorFormat
	virtual void read(IImage* into) = 0;
	virtual void write(const IImage* pixels) = 0;

	IImage* Image = 0;
	//! Copy from lock to unlock of a texture that keeps no image
	IImage* Locked = 0;
	bool LockReadOnly = false;
};


//! A built-in type's renderer, which only reports transparency
class CDriverMaterialRenderer : public IMaterialRenderer
{
public:
	CDriverMaterialRenderer(bool transparent) : Transparent(transparent) {}
	bool isTransparent() const IRR_OVERRIDE { return Transparent; }
private:
	bool Transparent;
};


//! A game program's renderer; the driver's own class keeps and sends the constant values
template <class TDriver, class TProgram>
class CDriverShaderRenderer : public IMaterialRenderer, public IMaterialRendererServices
{
public:
	CDriverShaderRenderer(TDriver* driver, TProgram* program, IShaderConstantSetCallBack* callback,
			E_MATERIAL_TYPE base, s32 userData)
		: Driver(driver), Program(program), CallBack(callback), UserData(userData),
			Blend(blendFor(base)), BaseMaterial(base)
	{
		if (CallBack)
			CallBack->grab();
	}

	~CDriverShaderRenderer() IRR_OVERRIDE
	{
		if (CallBack)
			CallBack->drop();
	}

	void OnSetMaterial(const SMaterial& material, const SMaterial&, bool,
			IMaterialRendererServices*) IRR_OVERRIDE
	{
		if (CallBack)
			CallBack->OnSetMaterial(material);
	}

	bool isTransparent() const IRR_OVERRIDE
	{
		return Blend != 0;
	}

	s32 getVertexShaderConstantID(const c8* name) IRR_OVERRIDE { return uniformID(name); }
	s32 getPixelShaderConstantID(const c8* name) IRR_OVERRIDE { return uniformID(name); }

	bool setVertexShaderConstant(s32 index, const f32* floats, int count) IRR_OVERRIDE
	{ return setConstant(index, floats, count); }

	bool setPixelShaderConstant(s32 index, const f32* floats, int count) IRR_OVERRIDE
	{ return setConstant(index, floats, count); }

	bool setVertexShaderConstant(s32 index, const s32* ints, int count) IRR_OVERRIDE
	{ return setTexture(index, ints, count); }

	bool setPixelShaderConstant(s32 index, const s32* ints, int count) IRR_OVERRIDE
	{ return setTexture(index, ints, count); }

	bool setVertexShaderConstant(s32 index, const u32* ints, int count) IRR_OVERRIDE
	{ return setTexture(index, (const s32*)ints, count); }

	bool setPixelShaderConstant(s32 index, const u32* ints, int count) IRR_OVERRIDE
	{ return setTexture(index, (const s32*)ints, count); }

	void setVertexShaderConstant(const f32*, s32, s32) IRR_OVERRIDE {}
	void setPixelShaderConstant(const f32*, s32, s32) IRR_OVERRIDE {}

	IVideoDriver* getVideoDriver() IRR_OVERRIDE { return Driver; }

	const core::stringc& programName() const { return Program->Name; }

	//! What the game asked to draw with when the program will not
	E_MATERIAL_TYPE baseMaterial() const { return BaseMaterial; }

protected:
	virtual bool setConstant(s32 index, const f32* floats, int count) = 0;

	//! Sets the texture layer a sampler of the program reads
	bool setTexture(s32 index, const s32* ints, int count)
	{
		if (index < 0 || index >= (s32)Layers.size() || !ints || count <= 0 ||
				(u32)ints[0] >= MATERIAL_MAX_TEXTURES)
			return false;

		bool moved = false;
		for (s32 i = index; i >= 0; i = Program->Uniforms[(u32)i].Next)
		{
			if (!Program->Uniforms[(u32)i].Texture)
				continue;
			LayersMoved |= Layers[(u32)i] != ints[0];
			Layers[(u32)i] = ints[0];
			moved = true;
		}
		return moved;
	}

	//! The n-th texture reads layer n until the game sets another; reflection may come after the
	//! program
	void settleLayers()
	{
		if (Layers.size() == Program->Uniforms.size())
			return;
		u32 layer = 0;
		Layers.set_used(0);
		for (u32 i = 0; i < Program->Uniforms.size(); ++i)
			Layers.push_back(Program->Uniforms[i].Texture ? (s32)layer++ : -1);
		LayersMoved = true;
	}

	s32 uniformID(const c8* name) const
	{
		for (u32 i = 0; i < Program->Uniforms.size(); ++i)
			if (Program->Uniforms[i].Name == name)
				return (s32)i;
		return -1;
	}

	TDriver* Driver;
	TProgram* Program;
	IShaderConstantSetCallBack* CallBack;
	s32 UserData;
	u32 Blend = 0;
	E_MATERIAL_TYPE BaseMaterial = EMT_SOLID;
	//! Texture layer each program constant reads, -1 for values, and whether one moved since last
	//! gathered
	core::array<s32> Layers;
	bool LayersMoved = true;
};


//! Transforms, 2D drawing as quads, the fixed pipeline's constants and the prebuilt programs
template <class TRenderer, class TProgram>
class CDriverCommon : public CNullDriver
{
public:
	CDriverCommon(io::IFileSystem* io, const core::dimension2d<u32>& screenSize) : CNullDriver(io,
			screenSize)
	{
		//! Pooled meshes leave their chunks after 100 frames unused
		HWBufferIdleFrames = 100;
		//! A renderer per built-in type, in enum order
		for (u32 i = 0; i <= (u32)EMT_ONETEXTURE_BLEND; ++i)
			addAndDropMaterialRenderer(new CDriverMaterialRenderer(
					blendFor((E_MATERIAL_TYPE)i) != 0));
	}

	void setTransform(E_TRANSFORMATION_STATE state, const core::matrix4& mat) IRR_OVERRIDE
	{
		if (state == ETS_VIEW || state == ETS_PROJECTION)
			ViewProjectionStale = true;

		Matrices[state] = mat;
	}

	const core::matrix4& getTransform(E_TRANSFORMATION_STATE state) const IRR_OVERRIDE
	{ return Matrices[state]; }

	const wchar_t* getName() const IRR_OVERRIDE { return Name.c_str(); }
	ECOLOR_FORMAT getColorFormat() const IRR_OVERRIDE { return ECF_R8G8B8; }

	bool queryFeature(E_VIDEO_DRIVER_FEATURE feature) const IRR_OVERRIDE
	{
		switch (feature)
		{
		case EVDF_TEXTURE_NPOT:
		case EVDF_TEXTURE_NSQUARE:
		case EVDF_VERTEX_BUFFER_OBJECT:
			return true;
		//! GLSL only with prebuilt programs
		case EVDF_ARB_GLSL:
			return !Programs.empty();
		default:
			return false;
		}
	}

	void setMaterial(const SMaterial& material) IRR_OVERRIDE
	{
		Material = material;
		OverrideMaterial.apply(Material);
		//! Texture matrices, readable through getTransform
		for (u32 i = 0; i < MATERIAL_MAX_TEXTURES_USED; ++i)
			Matrices[ETS_TEXTURE_0 + i] = Material.getTextureMatrix(i);

		ShaderRenderer = Material.MaterialType < ShaderMaterials.size() ?
				ShaderMaterials[Material.MaterialType] : 0;
		//! Culling both sides draws nothing
		MaterialVisible = !(Material.BackfaceCulling && Material.FrontfaceCulling);
		if (ShaderRenderer)
			ShaderRenderer->OnSetMaterial(Material, Material, true, ShaderRenderer);
	}

	//! Picks a built program by the source's first line "//SHADER:<name>"
	s32 addHighLevelShaderMaterial(const c8* vertexShaderProgram, const c8*, E_VERTEX_SHADER_TYPE,
			const c8*, const c8*, E_PIXEL_SHADER_TYPE, const c8*, const c8*, E_GEOMETRY_SHADER_TYPE,
			scene::E_PRIMITIVE_TYPE, scene::E_PRIMITIVE_TYPE, u32,
			IShaderConstantSetCallBack* callback,
			E_MATERIAL_TYPE baseMaterial, s32 userData) IRR_OVERRIDE
	{
		static const c8 marker[] = "//SHADER:";
		const u32 markerLength = sizeof(marker) - 1;
		if (!vertexShaderProgram || strncmp(vertexShaderProgram, marker, markerLength) != 0)
		{
			os::Printer::log("A shader reached the driver without a program name", ELL_ERROR);
			return -1;
		}

		const c8* from = vertexShaderProgram + markerLength;
		const core::stringc wanted(from, (u32)strcspn(from, "\r\n"));
		u32 index = 0;
		while (index < Programs.size() && Programs[index]->Name != wanted)
			++index;
		if (index == Programs.size())
		{
			os::Printer::log("No program was built for the shader", wanted.c_str(), ELL_ERROR);
			return -1;
		}
		os::Printer::log("Shader material built from the program", wanted.c_str(), ELL_INFORMATION);

		TRenderer* renderer = newShaderRenderer(index, callback, baseMaterial, userData);
		const s32 type = addMaterialRenderer(renderer);
		renderer->drop();
		if (type < 0)
			return -1;
		while (ShaderMaterials.size() <= (u32)type)
			ShaderMaterials.push_back(0);
		ShaderMaterials[(u32)type] = renderer;
		return type;
	}

	void draw2DImageBatch(const video::ITexture* texture,
			const core::array<core::position2d<s32> >& positions,
			const core::array<core::rect<s32> >& sourceRects,
			const core::rect<s32>* clipRect = 0,
			SColor color = SColor(255, 255, 255, 255),
			bool useAlphaChannelOfTexture = false) IRR_OVERRIDE
	{
		const u32 count = core::min_(positions.size(), sourceRects.size());
		if (!texture || !count)
			return;

		const core::dimension2du& size = texture->getOriginalSize();
		BatchQuads.set_used(0);

		for (u32 i = 0; i < count; ++i)
		{
			const core::rect<s32>& source = sourceRects[i];
			if (!source.isValid())
				continue;

			core::rect<s32> dest(positions[i], source.getSize());
			if (clipRect)
			{
				core::rect<s32> visible = dest;
				visible.clipAgainst(*clipRect);
				if (visible.getWidth() <= 0 || visible.getHeight() <= 0)
					continue;
			}

			const f32 u0 = (f32)source.UpperLeftCorner.X / (f32)size.Width;
			const f32 v0 = (f32)source.UpperLeftCorner.Y / (f32)size.Height;
			const f32 u1 = (f32)source.LowerRightCorner.X / (f32)size.Width;
			const f32 v1 = (f32)source.LowerRightCorner.Y / (f32)size.Height;
			const f32 x0 = (f32)dest.UpperLeftCorner.X, y0 = (f32)dest.UpperLeftCorner.Y;
			const f32 x1 = (f32)dest.LowerRightCorner.X, y1 = (f32)dest.LowerRightCorner.Y;

			BatchQuads.push_back(S3DVertex(x0, y0, 0, 0, 0, 1, color, u0, v0));
			BatchQuads.push_back(S3DVertex(x1, y0, 0, 0, 0, 1, color, u1, v0));
			BatchQuads.push_back(S3DVertex(x1, y1, 0, 0, 0, 1, color, u1, v1));
			BatchQuads.push_back(S3DVertex(x0, y1, 0, 0, 0, 1, color, u0, v1));
		}

		drawQuads2D(texture, BatchQuads.pointer(), BatchQuads.size() / 4, clipRect,
				color.getAlpha() < 255, useAlphaChannelOfTexture);
	}

	void draw2DImage(const video::ITexture* texture, const core::position2d<s32>& destPos,
			const core::rect<s32>& sourceRect, const core::rect<s32>* clipRect = 0,
			SColor color = SColor(255,255,255,255),
			bool useAlphaChannelOfTexture = false) IRR_OVERRIDE
	{
		const SColor colors[4] = { color, color, color, color };
		draw2DImage(texture, core::rect<s32>(destPos, sourceRect.getSize()), sourceRect, clipRect,
				colors, useAlphaChannelOfTexture);
	}

	//! A reversed source is drawn mirrored
	void draw2DImage(const video::ITexture* texture, const core::rect<s32>& destRect,
			const core::rect<s32>& sourceRect, const core::rect<s32>* clipRect = 0,
			const video::SColor* const colors = 0,
			bool useAlphaChannelOfTexture = false) IRR_OVERRIDE
	{
		if (!texture)
			return;

		const SColor white(255, 255, 255, 255);
		const SColor plain[4] = { white, white, white, white };
		const core::dimension2du& size = texture->getOriginalSize();
		const core::rect<f32> uv(
				(f32)sourceRect.UpperLeftCorner.X / (f32)size.Width,
				(f32)sourceRect.UpperLeftCorner.Y / (f32)size.Height,
				(f32)sourceRect.LowerRightCorner.X / (f32)size.Width,
				(f32)sourceRect.LowerRightCorner.Y / (f32)size.Height);
		const SColor* const from = colors ? colors : plain;
		//! Colours arrive upper left, lower left, lower right, upper right
		const SColor corners[4] = { from[0], from[3], from[2], from[1] };
		const bool alpha = from[0].getAlpha() < 255 || from[1].getAlpha() < 255 ||
				from[2].getAlpha() < 255 || from[3].getAlpha() < 255;
		drawQuad2D(texture, destRect, uv, corners, clipRect, alpha, useAlphaChannelOfTexture);
	}

	void draw2DRectangle(const core::rect<s32>& pos, SColor colorLeftUp, SColor colorRightUp,
			SColor colorLeftDown, SColor colorRightDown,
			const core::rect<s32>* clip = 0) IRR_OVERRIDE
	{
		core::rect<s32> area = pos;
		if (clip)
			area.clipAgainst(*clip);

		const SColor colors[4] = { colorLeftUp, colorRightUp, colorRightDown, colorLeftDown };
		const bool blend = colorLeftUp.getAlpha() < 255 || colorRightUp.getAlpha() < 255 ||
				colorLeftDown.getAlpha() < 255 || colorRightDown.getAlpha() < 255;
		drawQuad2D(0, area, core::rect<f32>(0, 0, 1, 1), colors, 0, blend, false);
	}

	void draw2DLine(const core::position2d<s32>& start, const core::position2d<s32>& end,
			SColor color = SColor(255,255,255,255)) IRR_OVERRIDE
	{
		if (start.X == end.X || start.Y == end.Y)
		{
			//! Covers both end pixels, whichever way the line runs
			const core::rect<s32> line(core::min_(start.X, end.X), core::min_(start.Y, end.Y),
					core::max_(start.X, end.X) + 1, core::max_(start.Y, end.Y) + 1);
			draw2DRectangle(line, color, color, color, color, 0);
			return;
		}

		linePixels(start, end, color, BatchQuads);
		drawQuads2D(0, BatchQuads.pointer(), BatchQuads.size() / 4, 0, color.getAlpha() < 255,
				false);
	}

	//! Triangle lists: static ones always, others as CNullDriver decides
	bool isHardwareBufferRecommend(const scene::IMeshBuffer* mb) IRR_OVERRIDE
	{
		if (!mb || mb->getPrimitiveType() != scene::EPT_TRIANGLES)
			return false;

		//! Static meshes go into the shared buffers
		if (mb->getHardwareMappingHint_Vertex() == scene::EHM_STATIC &&
				mb->getHardwareMappingHint_Index() == scene::EHM_STATIC)
			return true;

		return CNullDriver::isHardwareBufferRecommend(mb);
	}

protected:
	//! A renderer for the program at index
	virtual TRenderer* newShaderRenderer(u32 index, IShaderConstantSetCallBack* callback,
			E_MATERIAL_TYPE base, s32 userData) = 0;

	//! Links a mesh buffer to GPU buffers and lists it for ageing; static meshes go into the shared
	//! pools
	template <class TStore>
	SHWBufferLink* createMeshLink(CDriverMeshBuffers<TStore, SHWBufferLink>& meshes,
			const scene::IMeshBuffer* mb)
	{
		auto* link = new typename CDriverMeshBuffers<TStore, SHWBufferLink>::SLink(mb);
		link->Shared = mb->getHardwareMappingHint_Vertex() == scene::EHM_STATIC &&
				mb->getHardwareMappingHint_Index() == scene::EHM_STATIC;
		if (!meshes.update(link))
		{
			//! Freed here: not in HWBufferList yet
			meshes.release(link);
			delete link;
			return 0;
		}
		link->listPosition = HWBufferList.insert(HWBufferList.end(), link);
		return link;
	}

	//! Brings a mesh buffer up to date for a draw into the open pass; false when nothing is drawn
	template <class TStore>
	bool meshToDraw(CDriverMeshBuffers<TStore, SHWBufferLink>& meshes,
			typename CDriverMeshBuffers<TStore, SHWBufferLink>::SLink* link, bool encoding)
	{
		//! A frame that draws nothing still keeps the mesh, as it does on OpenGL
		link->LastUsed = 0;
		link->AgedAt = HWBufferFrame;
		if (!encoding || !link->Vertices || !link->Indices || !MaterialVisible ||
				!meshes.update(link))
			return false;
		PrimitivesDrawn += link->IndexCount / 3;
		return true;
	}

	//! A line list goes segment by segment as draw3DLine draws, each in its first vertex's colour
	void drawLineList(const void* vertices, u32 vertexCount, const void* indexList,
			u32 primitiveCount, E_VERTEX_TYPE vType, E_INDEX_TYPE iType)
	{
		const u32 stride = getVertexPitchFromType(vType);
		for (u32 i = 0; i < primitiveCount * 2; i += 2)
		{
			u32 ends[2];
			for (u32 e = 0; e < 2; ++e)
				ends[e] = iType == EIT_32BIT ?
						((const u32*)indexList)[i + e] : ((const u16*)indexList)[i + e];
			if (ends[0] >= vertexCount || ends[1] >= vertexCount)
				continue;
			const S3DVertex* from = (const S3DVertex*)(const void*)((const u8*)vertices +
					ends[0] * stride);
			const S3DVertex* to = (const S3DVertex*)(const void*)((const u8*)vertices +
					ends[1] * stride);
			draw3DLine(from->Pos, to->Pos, from->Color);
		}
	}

	//! Draws 2D quads sharing a texture and state in 1 draw
	virtual void drawQuads2D(const ITexture* texture, const S3DVertex* quads, u32 count,
			const core::rect<s32>* clip, bool alpha, bool alphaChannel) = 0;

	void drawQuad2D(const ITexture* texture, const core::rect<s32>& dest, const core::rect<f32>& uv,
			const SColor* colors, const core::rect<s32>* clip, bool alpha, bool alphaChannel)
	{
		if (!dest.isValid())
			return;

		const f32 x0 = (f32)dest.UpperLeftCorner.X, y0 = (f32)dest.UpperLeftCorner.Y;
		const f32 x1 = (f32)dest.LowerRightCorner.X, y1 = (f32)dest.LowerRightCorner.Y;
		const S3DVertex quad[4] = {
			S3DVertex(x0, y0, 0, 0, 0, 1, colors[0], uv.UpperLeftCorner.X, uv.UpperLeftCorner.Y),
			S3DVertex(x1, y0, 0, 0, 0, 1, colors[1], uv.LowerRightCorner.X, uv.UpperLeftCorner.Y),
			S3DVertex(x1, y1, 0, 0, 0, 1, colors[2], uv.LowerRightCorner.X, uv.LowerRightCorner.Y),
			S3DVertex(x0, y1, 0, 0, 0, 1, colors[3], uv.UpperLeftCorner.X, uv.LowerRightCorner.Y) };
		drawQuads2D(texture, quad, 1, clip, alpha, alphaChannel);
	}

	//! Pixels to clip space of the render target, origin top left
	core::matrix4 ortho2D() const
	{
		const core::dimension2d<u32>& target = getCurrentRenderTargetSize();
		core::matrix4 m;
		f32* p = m.pointer();
		p[0] = 2.0f / (f32)target.Width;
		p[5] = -2.0f / (f32)target.Height;
		p[12] = -1.0f;
		p[13] = 1.0f;
		return m;
	}

	const SMaterial& material2D() const
	{
		return OverrideMaterial2DEnabled ? OverrideMaterial2D : InitMaterial2D;
	}

	//! Matrices of a 3D draw with the current material
	SDriverUniforms fixedUniforms()
	{
		if (ViewProjectionStale)
		{
			ViewProjection.setbyproduct_nocheck(Matrices[ETS_PROJECTION], Matrices[ETS_VIEW]);
			ViewProjectionStale = false;
		}

		SDriverUniforms uniforms = {};
		uniforms.worldViewProj.setbyproduct_nocheck(ViewProjection, Matrices[ETS_WORLD]);
		uniforms.worldView.setbyproduct_nocheck(Matrices[ETS_VIEW], Matrices[ETS_WORLD]);
		return uniforms;
	}

	//! Fog and alpha test of a 3D draw with the current material, drawn as the given built-in type
	SDriverFog fixedFog(E_MATERIAL_TYPE type) const
	{
		SDriverFog fog = {};
		fog.color[0] = (f32)FogColor.getRed() / 255.0f;
		fog.color[1] = (f32)FogColor.getGreen() / 255.0f;
		fog.color[2] = (f32)FogColor.getBlue() / 255.0f;
		fog.color[3] = 1.0f;
		fog.scale = 1.f / (FogEnd - FogStart);
		fog.end = FogEnd;
		//! Linear fog only
		fog.mode = Material.FogEnable ? 1 : 0;
		//! ALPHA_CHANNEL_REF cuts at 0.5, ALPHA_CHANNEL at MaterialTypeParam
		fog.alphaRef = type == EMT_TRANSPARENT_ALPHA_CHANNEL_REF ? 0.5f :
				(type == EMT_TRANSPARENT_ALPHA_CHANNEL ? Material.MaterialTypeParam : 0.f);
		return fog;
	}

	//! Blend state of a 3D draw with the current material, drawn as the given built-in type
	u32 blendState(E_MATERIAL_TYPE type) const
	{
		if (!blendFor(type))
			return 0;
		//! EBO_NONE adds, as OpenGL's equation starts
		const u32 operation = Material.BlendOperation == EBO_NONE ?
				EBO_ADD : Material.BlendOperation;
		f32 param = Material.MaterialTypeParam;
		u32 blend = operation << 16 |
				((type == EMT_ONETEXTURE_BLEND ? IR(param) : DRIVER_BLEND_ALPHA) & 0xFFFF);
		//! The screen reads as having no alpha, as on OpenGL ES: destination alpha is 1
		for (u32 shift = 0; !CurrentRenderTarget && shift < 16; shift += 4)
		{
			const u32 factor = blend >> shift & 15;
			if (factor == EBF_DST_ALPHA)
				blend ^= (u32)(EBF_DST_ALPHA ^ EBF_ONE) << shift;
			else if (factor == EBF_ONE_MINUS_DST_ALPHA)
				blend &= ~(15u << shift);
		}
		return blend;
	}

	//! Depth function of the material; beyond ECFN_NEVER it tests less or equal
	u32 depthCompare() const
	{
		return Material.ZBuffer <= (u32)ECFN_NEVER ? Material.ZBuffer : (u32)ECFN_LESSEQUAL;
	}

	//! A disabled test writes no depth either
	bool depthWrite() const
	{
		return depthCompare() != (u32)ECFN_DISABLED && getWriteZBuffer(Material);
	}

	//! Culled side as MTL::CullMode numbers it: 0 none, 1 front, 2 back
	u32 cullMode() const
	{
		return Material.BackfaceCulling ? 2 : (Material.FrontfaceCulling ? 1 : 0);
	}

	core::stringw Name;
	SMaterial Material;
	//! False while the material culls both sides
	bool MaterialVisible = true;
	core::matrix4 Matrices[ETS_COUNT];
	//! Projection times view, recomputed when either changes
	core::matrix4 ViewProjection;
	bool ViewProjectionStale = true;
	//! Quads of a 2D batch or line, reused between draws
	core::array<S3DVertex> BatchQuads;
	//! Prebuilt programs, the renderers of their materials by type, and the current material's
	core::array<TProgram*> Programs;
	core::array<TRenderer*> ShaderMaterials;
	TRenderer* ShaderRenderer = 0;
};

} // end namespace irr::video

#endif
