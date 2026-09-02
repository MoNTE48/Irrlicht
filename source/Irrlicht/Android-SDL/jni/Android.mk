LOCAL_PATH := $(call my-dir)/../..

include $(CLEAR_VARS)

IRRLICHT_LIB_PATH := $(LOCAL_PATH)/../../lib/Android-SDL

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

SDL_PATH ?= $(error SDL_PATH variable is not set)

LOCAL_C_INCLUDES += $(SDL_PATH)/include

LOCAL_SRC_FILES := \
					minizip-ng/mz_crypt.c \
					minizip-ng/mz_crypt_openssl.c \
					CAnimatedMeshSceneNode.cpp \
					CAttributes.cpp \
					CB3DMeshFileLoader.cpp \
					CBillboardSceneNode.cpp \
					CBoneSceneNode.cpp \
					CCameraSceneNode.cpp \
					CColorConverter.cpp \
					CDefaultGUIElementFactory.cpp \
					CDefaultSceneNodeFactory.cpp \
					CDummyTransformationSceneNode.cpp \
					CEmptySceneNode.cpp \
					CFileList.cpp \
					CFileSystem.cpp \
					CFPSCounter.cpp \
					leakHunter.cpp \
					CGeometryCreator.cpp \
					CGUIButton.cpp \
					CGUICheckBox.cpp \
					CGUIComboBox.cpp \
					CGUIEditBox.cpp \
					CGUIEnvironment.cpp \
					CGUIFileOpenDialog.cpp \
					CGUIFont.cpp \
					CGUIImage.cpp \
					CGUIImageList.cpp \
					CGUIListBox.cpp \
					CGUIModalScreen.cpp \
					CGUIScrollBar.cpp \
					CGUISkin.cpp \
					CGUISpriteBank.cpp \
					CGUIStaticText.cpp \
					CGUITabControl.cpp \
					CImage.cpp \
					CImageLoaderJPG.cpp \
					CImageLoaderPNG.cpp \
					CImageLoaderTGA.cpp \
					CImageWriterJPG.cpp \
					CImageWriterPNG.cpp \
					CIrrDeviceSDL.cpp \
					CIrrDeviceStub.cpp \
					CLightSceneNode.cpp \
					CLimitReadFile.cpp \
					CLogger.cpp \
					CMemoryFile.cpp \
					CMeshCache.cpp \
					CMeshManipulator.cpp \
					CMeshSceneNode.cpp \
					CMeshTextureLoader.cpp \
					CMetaTriangleSelector.cpp \
					CNullDriver.cpp \
					COBJMeshFileLoader.cpp \
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
					COpenGLCacheHandler.cpp \
					COpenGLDriver.cpp \
					COpenGLExtensionHandler.cpp \
					COpenGLNormalMapRenderer.cpp \
					COpenGLParallaxMapRenderer.cpp \
					COpenGLShaderMaterialRenderer.cpp \
					COpenGLSLMaterialRenderer.cpp \
					COSOperator.cpp \
					CProfiler.cpp \
					CReadFile.cpp \
					CSceneCollisionManager.cpp \
					CSceneManager.cpp \
					CSkinnedMesh.cpp \
					CSkyBoxSceneNode.cpp \
					CTerrainTriangleSelector.cpp \
					CTextSceneNode.cpp \
					CTriangleBBSelector.cpp \
					CTriangleSelector.cpp \
					CVideoModeList.cpp \
					CVolumeLightSceneNode.cpp \
					CWriteFile.cpp \
					CXMeshFileLoader.cpp \
					CXMLReader.cpp \
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
