// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "IrrCompileConfig.h"
#ifdef _IRR_COMPILE_WITH_CUBE_SCENENODE_
#include "CCubeSceneNode.h"
#include "IVideoDriver.h"
#include "ISceneManager.h"
#ifdef _IRR_COMPILE_WITH_SHADOW_VOLUME_SCENENODE_
#include "CShadowVolumeSceneNode.h"
#else
#include "IShadowVolumeSceneNode.h"
#endif // _IRR_COMPILE_WITH_SHADOW_VOLUME_SCENENODE_

namespace irr
{
namespace scene
{

	/*
        011         111
          /6,8------/5        y
         /  |      / |        ^  z
        /   |     /  |        | /
    010 3,9-------2  |        |/
        |   7- - -10,4 101     *---->x
        |  /      |  /
        |/        | /
        0------11,1/
       000       100
	*/

//! constructor
CCubeSceneNode::CCubeSceneNode(f32 size, ISceneNode* parent, ISceneManager* mgr,
		s32 id, const core::vector3df& position,
		const core::vector3df& rotation, const core::vector3df& scale,
		ECUBE_MESH_TYPE type)
	: IMeshSceneNode(parent, mgr, id, position, rotation, scale),
	Mesh(0), Shadow(0), Size(size), MeshType(type)
{
	#ifdef _DEBUG
	setDebugName("CCubeSceneNode");
	#endif

	setSize();
}


CCubeSceneNode::~CCubeSceneNode()
{
	if (Shadow)
		Shadow->drop();
	if (Mesh)
		Mesh->drop();
}


void CCubeSceneNode::setSize()
{
	if (Mesh)
		Mesh->drop();
	Mesh = SceneManager->getGeometryCreator()->createCubeMesh(core::vector3df(Size), MeshType);
}


//! renders the node.
void CCubeSceneNode::render()
{
	video::IVideoDriver* driver = SceneManager->getVideoDriver();
	driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);

	if (Shadow)
		Shadow->updateShadowVolumes();

	for (u32 i=0; i<Mesh->getMeshBufferCount(); ++i)
	{
		const scene::IMeshBuffer* mb = Mesh->getMeshBuffer(i);
		{
			// for debug purposes only:
			if (DebugDataVisible & scene::EDS_HALF_TRANSPARENCY)
			{
				// overwrite half transparency
				video::SMaterial mat = mb->getMaterial();
				mat.MaterialType = video::EMT_TRANSPARENT_ADD_COLOR;
				driver->setMaterial(mat);
			}
			else
			{
				const video::SMaterial& mat = mb->getMaterial();
				driver->setMaterial(mat);
			}
			driver->drawMeshBuffer(mb);
		}
	}

	// for debug purposes only:
	if (DebugDataVisible)
	{
		video::SMaterial m;
		m.Lighting = false;
		m.AntiAliasing=0;
		driver->setMaterial(m);

		if (DebugDataVisible & scene::EDS_BBOX)
		{
			driver->draw3DBox(Mesh->getBoundingBox(), video::SColor(255,255,255,255));
		}
		if (DebugDataVisible & scene::EDS_BBOX_BUFFERS)
		{
			driver->draw3DBox(Mesh->getBoundingBox(), video::SColor(255,190,128,128));
		}
		if (DebugDataVisible & scene::EDS_NORMALS)
		{
			// draw normals
			const f32 debugNormalLength = SceneManager->getParameters()->getAttributeAsFloat(DEBUG_NORMAL_LENGTH);
			const video::SColor debugNormalColor = SceneManager->getParameters()->getAttributeAsColor(DEBUG_NORMAL_COLOR);
			for (u32 i=0; i < Mesh->getMeshBufferCount(); ++i)
			{
				driver->drawMeshBufferNormals(Mesh->getMeshBuffer(i), debugNormalLength, debugNormalColor);
			}
		}

		// show mesh
		if (DebugDataVisible & scene::EDS_MESH_WIRE_OVERLAY)
		{
			m.Wireframe = true;
			driver->setMaterial(m);

			for (u32 i=0; i < Mesh->getMeshBufferCount(); ++i)
			{
				driver->drawMeshBuffer(Mesh->getMeshBuffer(i));
			}
		}
	}
}


//! returns the axis aligned bounding box of this node
const core::aabbox3d<f32>& CCubeSceneNode::getBoundingBox() const
{
	return Mesh->getBoundingBox();
}


//! Removes a child from this scene node.
//! Implemented here, to be able to remove the shadow properly, if there is one,
//! or to remove attached child.
bool CCubeSceneNode::removeChild(ISceneNode* child)
{
	if (child && Shadow == child)
	{
		Shadow->drop();
		Shadow = 0;
	}

	return ISceneNode::removeChild(child);
}


//! Creates shadow volume scene node as child of this node
//! and returns a pointer to it.
IShadowVolumeSceneNode* CCubeSceneNode::addShadowVolumeSceneNode(
		const IMesh* shadowMesh, s32 id, bool zfailmethod, f32 infinity)
{
#ifdef _IRR_COMPILE_WITH_SHADOW_VOLUME_SCENENODE_
	if (!SceneManager->getVideoDriver()->queryFeature(video::EVDF_STENCIL_BUFFER))
		return 0;

	if (!shadowMesh)
		shadowMesh = Mesh; // if null is given, use the mesh of node

	if (Shadow)
		Shadow->drop();

	Shadow = new CShadowVolumeSceneNode(shadowMesh, this, SceneManager, id,  zfailmethod, infinity);
	return Shadow;
#else
	return 0;
#endif
}


void CCubeSceneNode::OnRegisterSceneNode()
{
	if (IsVisible)
		SceneManager->registerNodeForRendering(this);
	ISceneNode::OnRegisterSceneNode();
}


//! returns the material based on the zero based index i.
video::SMaterial& CCubeSceneNode::getMaterial(u32 i)
{
	return Mesh->getMeshBuffer(i)->getMaterial();
}


//! returns amount of materials used by this scene node.
u32 CCubeSceneNode::getMaterialCount() const
{
	if ( Mesh )
		return Mesh->getMeshBufferCount();
	return 0;
}


//! Writes attributes of the scene node.
void CCubeSceneNode::serializeAttributes(io::IAttributes* out, io::SAttributeReadWriteOptions* options) const
{
	ISceneNode::serializeAttributes(out, options);

	out->addFloat("Size", Size);
	out->addEnum("MeshType", (irr::s32)MeshType, CubeMeshTypeNames);
}


//! Reads attributes of the scene node.
void CCubeSceneNode::deserializeAttributes(io::IAttributes* in, io::SAttributeReadWriteOptions* options)
{
	f32 newSize = in->getAttributeAsFloat("Size", Size);
	ECUBE_MESH_TYPE newMeshType = (ECUBE_MESH_TYPE)in->getAttributeAsEnumeration("MeshType", CubeMeshTypeNames, (irr::s32)MeshType);
	newSize = core::max_(newSize, 0.0001f);
	if (newSize != Size || newMeshType != MeshType)
	{
		Size = newSize;
		MeshType = newMeshType;
		setSize();
	}

	ISceneNode::deserializeAttributes(in, options);
}


//! Creates a clone of this scene node and its children.
ISceneNode* CCubeSceneNode::clone(ISceneNode* newParent, ISceneManager* newManager)
{
	if (!newParent)
		newParent = Parent;
	if (!newManager)
		newManager = SceneManager;

	CCubeSceneNode* nb = new CCubeSceneNode(Size, newParent,
		newManager, ID, RelativeTranslation, RelativeRotation, RelativeScale, MeshType);

	nb->cloneMembers(this, newManager);
	for ( irr::u32 i=0; i < getMaterialCount(); ++i )
		nb->getMaterial(i) = getMaterial(i);
	nb->Shadow = Shadow;
	if ( nb->Shadow )
		nb->Shadow->grab();

	if ( newParent )
		nb->drop();
	return nb;
}


} // end namespace scene
} // end namespace irr

#endif // _IRR_COMPILE_WITH_CUBE_SCENENODE_
