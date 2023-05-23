LOCAL_PATH := $(call my-dir)/../..

include $(CLEAR_VARS)

IRRLICHT_LIB_PATH := $(LOCAL_PATH)/../../lib/Android-SDL2

include $(CLEAR_VARS)

LOCAL_MODULE := Irrlicht
IRRLICHT_LIB_NAME := lib$(LOCAL_MODULE).a

LOCAL_CFLAGS := -Wall -pipe -fno-exceptions -fno-rtti -fstrict-aliasing

LOCAL_CFLAGS += -DNO_IRR_COMPILE_WITH_ANDROID_DEVICE_

ifndef NDEBUG
LOCAL_CFLAGS += -g -D_DEBUG
else
LOCAL_CFLAGS += -fexpensive-optimizations -O3
endif

LOCAL_C_INCLUDES := ../../../include

SDL2_PATH ?= $(error SDL2_PATH variable is not set)

LOCAL_C_INCLUDES += $(SDL2_PATH)/include

LOCAL_SRC_FILES := \
					aesGladman/aescrypt.cpp \
					aesGladman/aeskey.cpp \
					aesGladman/aestab.cpp \
					aesGladman/fileenc.cpp \
					aesGladman/hmac.cpp \
					aesGladman/prng.cpp \
					aesGladman/pwd2key.cpp \
					aesGladman/sha1.cpp \
					aesGladman/sha2.cpp \
					C3DSMeshFileLoader.cpp \
					CAnimatedMeshHalfLife.cpp \
					CAnimatedMeshMD2.cpp \
					CAnimatedMeshMD3.cpp \
					CAnimatedMeshSceneNode.cpp \
					CAttributes.cpp \
					CB3DMeshFileLoader.cpp \
					CB3DMeshWriter.cpp \
					CBillboardSceneNode.cpp \
					CBoneSceneNode.cpp \
					CBSPMeshFileLoader.cpp \
					CCameraSceneNode.cpp \
					CColladaFileLoader.cpp \
					CColladaMeshWriter.cpp \
					CColorConverter.cpp \
					CCSMLoader.cpp \
					CCubeSceneNode.cpp \
					CDefaultGUIElementFactory.cpp \
					CDefaultSceneNodeAnimatorFactory.cpp \
					CDefaultSceneNodeFactory.cpp \
					CDMFLoader.cpp \
					CDummyTransformationSceneNode.cpp \
					CEmptySceneNode.cpp \
					CFileList.cpp \
					CFileSystem.cpp \
					CFPSCounter.cpp \
					leakHunter.cpp \
					CGeometryCreator.cpp \
					CGUIButton.cpp \
					CGUICheckBox.cpp \
					CGUIColorSelectDialog.cpp \
					CGUIComboBox.cpp \
					CGUIContextMenu.cpp \
					CGUIEditBox.cpp \
					CGUIEnvironment.cpp \
					CGUIFileOpenDialog.cpp \
					CGUIFont.cpp \
					CGUIImage.cpp \
					CGUIImageList.cpp \
					CGUIInOutFader.cpp \
					CGUIListBox.cpp \
					CGUIMenu.cpp \
					CGUIMeshViewer.cpp \
					CGUIMessageBox.cpp \
					CGUIModalScreen.cpp \
					CGUIScrollBar.cpp \
					CGUISkin.cpp \
					CGUISpinBox.cpp \
					CGUISpriteBank.cpp \
					CGUIStaticText.cpp \
					CGUITabControl.cpp \
					CGUITable.cpp \
					CGUIToolBar.cpp \
					CGUITreeView.cpp \
					CGUIWindow.cpp \
					CGUIProfiler.cpp \
					CImage.cpp \
					CImageLoaderBMP.cpp \
					CImageLoaderDDS.cpp \
					CImageLoaderJPG.cpp \
					CImageLoaderPCX.cpp \
					CImageLoaderPNG.cpp \
					CImageLoaderPPM.cpp \
					CImageLoaderPSD.cpp \
					CImageLoaderRGB.cpp \
					CImageLoaderTGA.cpp \
					CImageLoaderWAL.cpp \
					CImageWriterBMP.cpp \
					CImageWriterJPG.cpp \
					CImageWriterPCX.cpp \
					CImageWriterPNG.cpp \
					CImageWriterPPM.cpp \
					CImageWriterPSD.cpp \
					CImageWriterTGA.cpp \
					CImageLoaderPVR.cpp \
					CIrrDeviceSDL.cpp \
					CIrrDeviceStub.cpp \
					CIrrMeshFileLoader.cpp \
					CIrrMeshWriter.cpp \
					CLightSceneNode.cpp \
					CLimitReadFile.cpp \
					CLMTSMeshFileLoader.cpp \
					CLogger.cpp \
					CLWOMeshFileLoader.cpp \
					CMD2MeshFileLoader.cpp \
					CMD3MeshFileLoader.cpp \
					CMemoryFile.cpp \
					CMeshCache.cpp \
					CMeshManipulator.cpp \
					CMeshSceneNode.cpp \
					CMeshTextureLoader.cpp \
					CMetaTriangleSelector.cpp \
					CMountPointReader.cpp \
					CMS3DMeshFileLoader.cpp \
					CMY3DMeshFileLoader.cpp \
					CNPKReader.cpp \
					CNullDriver.cpp \
					COBJMeshFileLoader.cpp \
					COBJMeshWriter.cpp \
					COCTLoader.cpp \
					COctreeSceneNode.cpp \
					COctreeTriangleSelector.cpp \
					COGLES2Driver.cpp \
					COGLES2ExtensionHandler.cpp \
					COGLES2MaterialRenderer.cpp \
					COGLES2FixedPipelineRenderer.cpp \
					COGLES2NormalMapRenderer.cpp \
					COGLES2ParallaxMapRenderer.cpp \
					COGLES2Renderer2D.cpp \
					COGLESDriver.cpp \
					COGLESExtensionHandler.cpp \
					COgreMeshFileLoader.cpp \
					COpenGLCacheHandler.cpp \
					COpenGLDriver.cpp \
					COpenGLExtensionHandler.cpp \
					COpenGLNormalMapRenderer.cpp \
					COpenGLParallaxMapRenderer.cpp \
					COpenGLShaderMaterialRenderer.cpp \
					COpenGLSLMaterialRenderer.cpp \
					COSOperator.cpp \
					CPakReader.cpp \
					CParticleAnimatedMeshSceneNodeEmitter.cpp \
					CParticleAttractionAffector.cpp \
					CParticleBoxEmitter.cpp \
					CParticleCylinderEmitter.cpp \
					CParticleFadeOutAffector.cpp \
					CParticleGravityAffector.cpp \
					CParticleMeshEmitter.cpp \
					CParticlePointEmitter.cpp \
					CParticleRingEmitter.cpp \
					CParticleRotationAffector.cpp \
					CParticleScaleAffector.cpp \
					CParticleSphereEmitter.cpp \
					CParticleSystemSceneNode.cpp \
					CPLYMeshFileLoader.cpp \
					CPLYMeshWriter.cpp \
					CProfiler.cpp \
					CQ3LevelMesh.cpp \
					CQuake3ShaderSceneNode.cpp \
					CReadFile.cpp \
					CSceneCollisionManager.cpp \
					CSceneLoaderIrr.cpp \
					CSceneManager.cpp \
					CSceneNodeAnimatorCameraFPS.cpp \
					CSceneNodeAnimatorCameraMaya.cpp \
					CSceneNodeAnimatorCollisionResponse.cpp \
					CSceneNodeAnimatorDelete.cpp \
					CSceneNodeAnimatorFlyCircle.cpp \
					CSceneNodeAnimatorFlyStraight.cpp \
					CSceneNodeAnimatorFollowSpline.cpp \
					CSceneNodeAnimatorRotation.cpp \
					CSceneNodeAnimatorTexture.cpp \
					CShadowVolumeSceneNode.cpp \
					CSkinnedMesh.cpp \
					CSkyBoxSceneNode.cpp \
					CSkyDomeSceneNode.cpp \
					CSMFMeshFileLoader.cpp \
					CSphereSceneNode.cpp \
					CSTLMeshFileLoader.cpp \
					CSTLMeshWriter.cpp \
					CTarReader.cpp \
					CTerrainSceneNode.cpp \
					CTerrainTriangleSelector.cpp \
					CTextSceneNode.cpp \
					CTriangleBBSelector.cpp \
					CTriangleSelector.cpp \
					CVideoModeList.cpp \
					CVolumeLightSceneNode.cpp \
					CWADReader.cpp \
					CWaterSurfaceSceneNode.cpp \
					CWriteFile.cpp \
					CXMeshFileLoader.cpp \
					CXMLReader.cpp \
					CXMLWriter.cpp \
					CZipReader.cpp \
					Irrlicht.cpp \
					irrXML.cpp \
					os.cpp \
					utf8.cpp

include $(BUILD_STATIC_LIBRARY)

all: $(IRRLICHT_LIB_PATH)/$(TARGET_ARCH_ABI)/$(IRRLICHT_LIB_NAME)
$(IRRLICHT_LIB_PATH)/$(TARGET_ARCH_ABI)/$(IRRLICHT_LIB_NAME) : $(TARGET_OUT)/$(IRRLICHT_LIB_NAME)
	mkdir -p "`dirname $@`"
	cp $< $@
