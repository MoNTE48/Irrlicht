cmake_minimum_required(VERSION 3.5)

project(Irrlicht)

find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SDL2 REQUIRED)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include"
                    "${JPEG_INCLUDE_DIR}"
                    "${PNG_INCLUDE_DIRS}"
                    "${ZLIB_INCLUDE_DIR}"
                    "${SDL2_INCLUDE_DIRS}")

add_definitions(-DNDEBUG=1 -DIRRLICHT_EXPORTS=1 -D_IRR_STATIC_LIB_)

if(WIN32)
	add_definitions(-DNO_IRR_COMPILE_WITH_SDL_TEXTINPUT_
		-DNO_IRR_COMPILE_WITH_OGLES2_
		-DNO_IRR_COMPILE_WITH_DIRECT3D_9_
		-DNO_IRR_COMPILE_WITH_DIRECTINPUT_JOYSTICK_)
endif()

set(IRRLICHT_SOURCES
	source/Irrlicht/minizip-ng/mz_crypt.c
	source/Irrlicht/minizip-ng/mz_crypt_openssl.c
	source/Irrlicht/CAnimatedMeshSceneNode.cpp
	source/Irrlicht/CAttributes.cpp
	source/Irrlicht/CB3DMeshFileLoader.cpp
	source/Irrlicht/CBillboardSceneNode.cpp
	source/Irrlicht/CBoneSceneNode.cpp
	source/Irrlicht/CCameraSceneNode.cpp
	source/Irrlicht/CColorConverter.cpp
	source/Irrlicht/CDefaultGUIElementFactory.cpp
	source/Irrlicht/CDefaultSceneNodeFactory.cpp
	source/Irrlicht/CDummyTransformationSceneNode.cpp
	source/Irrlicht/CEGLManager.cpp
	source/Irrlicht/CEmptySceneNode.cpp
	source/Irrlicht/CFileList.cpp
	source/Irrlicht/CFileSystem.cpp
	source/Irrlicht/CFPSCounter.cpp
	source/Irrlicht/CGeometryCreator.cpp
	source/Irrlicht/CGLXManager.cpp
	source/Irrlicht/CGUIButton.cpp
	source/Irrlicht/CGUICheckBox.cpp
	source/Irrlicht/CGUIColorSelectDialog.cpp
	source/Irrlicht/CGUIComboBox.cpp
	source/Irrlicht/CGUIContextMenu.cpp
	source/Irrlicht/CGUIEditBox.cpp
	source/Irrlicht/CGUIEnvironment.cpp
	source/Irrlicht/CGUIFileOpenDialog.cpp
	source/Irrlicht/CGUIFont.cpp
	source/Irrlicht/CGUIImage.cpp
	source/Irrlicht/CGUIImageList.cpp
	source/Irrlicht/CGUIInOutFader.cpp
	source/Irrlicht/CGUIListBox.cpp
	source/Irrlicht/CGUIMenu.cpp
	source/Irrlicht/CGUIMeshViewer.cpp
	source/Irrlicht/CGUIMessageBox.cpp
	source/Irrlicht/CGUIModalScreen.cpp
	source/Irrlicht/CGUIProfiler.cpp
	source/Irrlicht/CGUIScrollBar.cpp
	source/Irrlicht/CGUISkin.cpp
	source/Irrlicht/CGUISpinBox.cpp
	source/Irrlicht/CGUISpriteBank.cpp
	source/Irrlicht/CGUIStaticText.cpp
	source/Irrlicht/CGUITabControl.cpp
	source/Irrlicht/CGUITable.cpp
	source/Irrlicht/CGUIToolBar.cpp
	source/Irrlicht/CGUITreeView.cpp
	source/Irrlicht/CGUIWindow.cpp
	source/Irrlicht/CImage.cpp
	source/Irrlicht/CImageLoaderJPG.cpp
	source/Irrlicht/CImageLoaderPNG.cpp
	source/Irrlicht/CImageLoaderTGA.cpp
	source/Irrlicht/CImageWriterJPG.cpp
	source/Irrlicht/CImageWriterPNG.cpp
	source/Irrlicht/CIrrDeviceConsole.cpp
	source/Irrlicht/CIrrDeviceFB.cpp
	source/Irrlicht/CIrrDeviceLinux.cpp
	source/Irrlicht/CIrrDeviceSDL.cpp
	source/Irrlicht/CIrrDeviceStub.cpp
	source/Irrlicht/CIrrDeviceWin32.cpp
	source/Irrlicht/CLightSceneNode.cpp
	source/Irrlicht/CLimitReadFile.cpp
	source/Irrlicht/CLogger.cpp
	source/Irrlicht/CMemoryFile.cpp
	source/Irrlicht/CMeshCache.cpp
	source/Irrlicht/CMeshManipulator.cpp
	source/Irrlicht/CMeshSceneNode.cpp
	source/Irrlicht/CMeshTextureLoader.cpp
	source/Irrlicht/CMetaTriangleSelector.cpp
	source/Irrlicht/CNullDriver.cpp
	source/Irrlicht/COBJMeshFileLoader.cpp
	source/Irrlicht/COctreeTriangleSelector.cpp
	source/Irrlicht/COGLES2Driver.cpp
	source/Irrlicht/COGLES2ExtensionHandler.cpp
	source/Irrlicht/COGLES2FixedPipelineRenderer.cpp
	source/Irrlicht/COGLES2MaterialRenderer.cpp
	source/Irrlicht/COGLES2NormalMapRenderer.cpp
	source/Irrlicht/COGLES2ParallaxMapRenderer.cpp
	source/Irrlicht/COGLES2Renderer2D.cpp
	source/Irrlicht/COGLESDriver.cpp
	source/Irrlicht/COGLESExtensionHandler.cpp
	source/Irrlicht/COpenGLCacheHandler.cpp
	source/Irrlicht/COpenGLDriver.cpp
	source/Irrlicht/COpenGLExtensionHandler.cpp
	source/Irrlicht/COpenGLNormalMapRenderer.cpp
	source/Irrlicht/COpenGLParallaxMapRenderer.cpp
	source/Irrlicht/COpenGLShaderMaterialRenderer.cpp
	source/Irrlicht/COpenGLSLMaterialRenderer.cpp
	source/Irrlicht/COSOperator.cpp
	source/Irrlicht/CProfiler.cpp
	source/Irrlicht/CReadFile.cpp
	source/Irrlicht/CSceneCollisionManager.cpp
	source/Irrlicht/CSceneManager.cpp
	source/Irrlicht/CSkinnedMesh.cpp
	source/Irrlicht/CSkyBoxSceneNode.cpp
	source/Irrlicht/CTerrainTriangleSelector.cpp
	source/Irrlicht/CTextSceneNode.cpp
	source/Irrlicht/CTriangleBBSelector.cpp
	source/Irrlicht/CTriangleSelector.cpp
	source/Irrlicht/CVideoModeList.cpp
	source/Irrlicht/CVolumeLightSceneNode.cpp
	source/Irrlicht/CWebGL1Driver.cpp
	source/Irrlicht/CWGLManager.cpp
	source/Irrlicht/CWriteFile.cpp
	source/Irrlicht/CXMeshFileLoader.cpp
	source/Irrlicht/CXMLReader.cpp
	source/Irrlicht/CZipReader.cpp
	source/Irrlicht/Irrlicht.cpp
	source/Irrlicht/irrXML.cpp
	source/Irrlicht/leakHunter.cpp
	source/Irrlicht/os.cpp
	source/Irrlicht/utf8.cpp
)

add_library(irrlicht STATIC ${IRRLICHT_SOURCES})

target_link_libraries(irrlicht ${ZLIB_LIBRARY} ${PNG_LIBRARY} ${JPEG_LIBRARY})
