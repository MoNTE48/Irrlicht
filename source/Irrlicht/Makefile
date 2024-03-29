VERSION_MAJOR = 1
VERSION_MINOR = 9
VERSION_RELEASE = 0
# Irrlicht Engine 1.9.0
# Makefile for Linux
#
# To use, just run:
#
# make
#
# This will compile Irrlicht, create a static lib (libIrrlicht.a), and copy it
# into the subdirectory lib/Linux. That's all.
#
# If you switch any settings (like debug/release/static/shared) then first run:
#
# make clean
#
# If you want Irrlicht to be compiled as shared lib (libIrrlicht.so.versionnumber), then run:
#
# make sharedlib
# make install
#
# If you want to compile in release mode run:
#
# make NDEBUG=1
#
# For cross-compilation for Win32 under Linux, just use the win32 targets. You have to set
# at least CXX, CC, and AR to the proper binaries.
#
# For compiling on MinGW you can run it with:
#
# make win32
#
# Or as MinGW by default has CC set to cc - but has no compiler by that name - you might have to do:
# make CC=gcc win32

#List of object files, separated based on engine architecture
IRRMESHLOADER = CBSPMeshFileLoader.o CMD2MeshFileLoader.o CMD3MeshFileLoader.o CMS3DMeshFileLoader.o CB3DMeshFileLoader.o C3DSMeshFileLoader.o COgreMeshFileLoader.o COBJMeshFileLoader.o CColladaFileLoader.o CCSMLoader.o CDMFLoader.o CLMTSMeshFileLoader.o CMY3DMeshFileLoader.o COCTLoader.o CXMeshFileLoader.o CIrrMeshFileLoader.o CSTLMeshFileLoader.o CLWOMeshFileLoader.o CPLYMeshFileLoader.o CSMFMeshFileLoader.o CMeshTextureLoader.o
IRRMESHWRITER = CColladaMeshWriter.o CIrrMeshWriter.o CSTLMeshWriter.o COBJMeshWriter.o CPLYMeshWriter.o CB3DMeshWriter.o
IRRMESHOBJ = $(IRRMESHLOADER) $(IRRMESHWRITER) \
	CSkinnedMesh.o CBoneSceneNode.o CMeshSceneNode.o \
	CAnimatedMeshSceneNode.o CAnimatedMeshMD2.o CAnimatedMeshMD3.o \
	CQ3LevelMesh.o CQuake3ShaderSceneNode.o CAnimatedMeshHalfLife.o
IRROBJ = CBillboardSceneNode.o CCameraSceneNode.o CDummyTransformationSceneNode.o CEmptySceneNode.o CGeometryCreator.o CLightSceneNode.o CMeshManipulator.o CMetaTriangleSelector.o COctreeSceneNode.o COctreeTriangleSelector.o CSceneCollisionManager.o CSceneManager.o CShadowVolumeSceneNode.o CSkyBoxSceneNode.o CSkyDomeSceneNode.o CTerrainSceneNode.o CTerrainTriangleSelector.o CVolumeLightSceneNode.o CCubeSceneNode.o CSphereSceneNode.o CTextSceneNode.o CTriangleBBSelector.o CTriangleSelector.o CWaterSurfaceSceneNode.o CMeshCache.o CDefaultSceneNodeAnimatorFactory.o CDefaultSceneNodeFactory.o CSceneLoaderIrr.o
IRRPARTICLEOBJ = CParticleAnimatedMeshSceneNodeEmitter.o CParticleBoxEmitter.o CParticleCylinderEmitter.o CParticleMeshEmitter.o CParticlePointEmitter.o CParticleRingEmitter.o CParticleSphereEmitter.o CParticleAttractionAffector.o CParticleFadeOutAffector.o CParticleGravityAffector.o CParticleRotationAffector.o CParticleSystemSceneNode.o CParticleScaleAffector.o
IRRANIMOBJ = CSceneNodeAnimatorCameraFPS.o CSceneNodeAnimatorCameraMaya.o CSceneNodeAnimatorCollisionResponse.o CSceneNodeAnimatorDelete.o CSceneNodeAnimatorFlyCircle.o CSceneNodeAnimatorFlyStraight.o CSceneNodeAnimatorFollowSpline.o CSceneNodeAnimatorRotation.o CSceneNodeAnimatorTexture.o
IRRDRVROBJ = CNullDriver.o COpenGLCacheHandler.o COpenGLDriver.o COpenGLNormalMapRenderer.o COpenGLParallaxMapRenderer.o COpenGLShaderMaterialRenderer.o COpenGLSLMaterialRenderer.o COpenGLExtensionHandler.o \
	COGLESDriver.o COGLESExtensionHandler.o COGLES2Driver.o COGLES2ExtensionHandler.o COGLES2FixedPipelineRenderer.o COGLES2MaterialRenderer.o COGLES2NormalMapRenderer.o COGLES2ParallaxMapRenderer.o COGLES2Renderer2D.o
IRRIMAGEOBJ = CColorConverter.o CImage.o CImageLoaderBMP.o CImageLoaderDDS.o CImageLoaderJPG.o CImageLoaderPCX.o CImageLoaderPNG.o CImageLoaderPSD.o CImageLoaderPVR.o CImageLoaderTGA.o CImageLoaderPPM.o CImageLoaderWAL.o CImageLoaderRGB.o \
	CImageWriterBMP.o CImageWriterJPG.o CImageWriterPCX.o CImageWriterPNG.o CImageWriterPPM.o CImageWriterPSD.o CImageWriterTGA.o
IRRVIDEOOBJ = CVideoModeList.o CFPSCounter.o $(IRRDRVROBJ) $(IRRIMAGEOBJ)
IRRIOOBJ = CFileList.o CFileSystem.o CLimitReadFile.o CMemoryFile.o CReadFile.o CWriteFile.o CXMLReader.o CXMLWriter.o CWADReader.o CZipReader.o CPakReader.o CNPKReader.o CTarReader.o CMountPointReader.o irrXML.o CAttributes.o
IRROTHEROBJ = CIrrDeviceSDL.o CIrrDeviceStub.o CLogger.o COSOperator.o Irrlicht.o os.o leakHunter.o CProfiler.o utf8.o
IRRGUIOBJ = CGUIButton.o CGUICheckBox.o CGUIComboBox.o CGUIContextMenu.o CGUIEditBox.o CGUIEnvironment.o CGUIFileOpenDialog.o CGUIFont.o CGUIImage.o CGUIInOutFader.o CGUIListBox.o CGUIMenu.o CGUIMeshViewer.o CGUIMessageBox.o CGUIModalScreen.o CGUIScrollBar.o CGUISpinBox.o CGUISkin.o CGUIStaticText.o CGUITabControl.o CGUITable.o CGUIToolBar.o CGUIWindow.o CGUIColorSelectDialog.o CDefaultGUIElementFactory.o CGUISpriteBank.o CGUIImageList.o CGUITreeView.o CGUIProfiler.o
LIBAESGM = aesGladman/aescrypt.o aesGladman/aeskey.o aesGladman/aestab.o aesGladman/fileenc.o aesGladman/hmac.o aesGladman/prng.o aesGladman/pwd2key.o aesGladman/sha1.o aesGladman/sha2.o

# Next variable is for additional scene nodes etc. of customized Irrlicht versions
EXTRAOBJ =
LINKOBJ = $(IRRMESHOBJ) $(IRROBJ) $(IRRPARTICLEOBJ) $(IRRANIMOBJ) \
	$(IRRVIDEOOBJ) $(IRRIOOBJ) $(IRROTHEROBJ) \
	$(IRRGUIOBJ) $(LIBAESGM) $(EXTRAOBJ)

emscripten: EMSCRIPTEN=1

###############
#Compiler flags

CXXINCS = -I../../include
CPPFLAGS += $(CXXINCS) -DIRRLICHT_EXPORTS=1
CXXFLAGS += -Wall -pipe -fno-exceptions -fno-rtti
#CXXFLAGS += -std=gnu++11 -U__STRICT_ANSI__
ifndef NDEBUG
CPPFLAGS += -g -D_DEBUG
else
CPPFLAGS += -O3
endif
ifdef PROFILE
CPPFLAGS += -pg
endif
ifdef EMSCRIPTEN
  CXXFLAGS += -std=gnu++11 -U__STRICT_ANSI__
  ifdef WASM
    # TODO: shouldn't this use CPPFLAGS?
    CXXFLAGS += -s WASM=1
  endif  
endif

sharedlib sharedlib_osx sharedlib_win32: LDFLAGS += -lz -ljpeg -lpng

sharedlib sharedlib_osx: CPPFLAGS += -fPIC
# TODO: also necessary on sharedlib_osx?
sharedlib: LDFLAGS += -fPIC

#multilib handling
ifeq ($(HOSTTYPE), x86_64)
LIBSELECT=64
endif

#Linux specific options
staticlib sharedlib install: SYSTEM = Linux
sharedlib install: SHARED_LIB = libIrrlicht.so
sharedlib: LDFLAGS += -L/usr/X11R6/lib$(LIBSELECT) -lGL -lXxf86vm
staticlib sharedlib: CXXINCS += -I/usr/X11R6/include -I/usr/include/SDL2

#OSX specific options
staticlib_osx sharedlib_osx install_osx: SYSTEM = MacOSX
staticlib_osx sharedlib_osx: IRROTHEROBJ += MacOSX/CIrrDeviceMacOSX.o MacOSX/OSXClipboard.o MacOSX/AppDelegate.o
staticlib_osx sharedlib_osx: CXXINCS += -IMacOSX -I/usr/X11R6/include
sharedlib_osx install_osx: SHARED_LIB = libIrrlicht.dylib
staticlib_osx sharedlib_osx: LDFLAGS += --no-export-all-symbols --add-stdcall-alias
sharedlib_osx: LDFLAGS += -L/usr/X11R6/lib$(LIBSELECT) -lGL -lXxf86vm
# for non-X11 app
#sharedlib_osx: LDFLAGS += -framework cocoa -framework carbon -framework opengl -framework IOKit

#Windows specific options
IRRLICHT_DLL := ../../bin/Win32-gcc/Irrlicht.dll
sharedlib_win32 staticlib_win32: SYSTEM = Win32-gcc
sharedlib_win32: LDFLAGS += -lgdi32 -lopengl32 -ld3dx9d -lwinmm -Wl,--add-stdcall-alias
#sharedlib_win32: LDFLAGS += -lSDL
#choose either -DIRR_COMPILE_WITH_DX9_DEV_PACK or -DNO_IRR_COMPILE_WITH_DIRECT3D_9_ depending if you need dx9
#sharedlib_win32 staticlib_win32: CPPFLAGS += -DIRR_COMPILE_WITH_DX9_DEV_PACK
sharedlib_win32 staticlib_win32: CPPFLAGS += -DNO_IRR_COMPILE_WITH_DIRECTINPUT_JOYSTICK_ -DNO_IRR_COMPILE_WITH_DIRECT3D_9_
staticlib_win32: CPPFLAGS += -D_IRR_STATIC_LIB_

#emscripten specific options
staticlib_emscripten: SYSTEM = emscripten


# You might want to disable features you don't need in Irrlicht here to reduce Irrlicht library.
# This can also help to reduce the .js size when compiling for emscripten.
# Note you can also disable the same features by modifying IrrCompileConfig.h which is slightly 
# safer as you have then the same setting when compiling Irrlicht and your project (but needs 
# modifying source-code while you can simply copy the Makefile and create your own settings)

# xml - use a lot, but also takes  a lot of space. Check if you have any formats using it.s
#CXXFLAGS += -DNO_IRR_COMPILE_WITH_XML_ 
# Get rid of encrypted zip files or bzip encrypted zip's or lzma encrypte zip's - or even all zip-file support.
#CXXFLAGS += -DNO_IRR_COMPILE_WITH_ZIP_ENCRYPTION_ -DNO_IRR_COMPILE_WITH_BZIP2_ -DNO_IRR_COMPILE_WITH_LZMA_ -DNO_IRR_COMPILE_WITH_ZLIB_
# Disable mesh writers 
#CXXFLAGS += -DNO_IRR_COMPILE_WITH_IRR_WRITER_ -DNO_IRR_COMPILE_WITH_COLLADA_WRITER_ -DNO_IRR_COMPILE_WITH_STL_WRITER_ 
#CXXFLAGS += -DNO_IRR_COMPILE_WITH_OBJ_WRITER_ -DNO_IRR_COMPILE_WITH_PLY_WRITER_ -DNO_IRR_COMPILE_WITH_B3D_WRITER_
# Disable image writers
#CXXFLAGS += -DNO_IRR_COMPILE_WITH_BMP_WRITER_ -DNO_IRR_COMPILE_WITH_JPG_WRITER_ -DNO_IRR_COMPILE_WITH_PCX_WRITER_
#CXXFLAGS += -DNO_IRR_COMPILE_WITH_PNG_WRITER_ -DNO_IRR_COMPILE_WITH_PPM_WRITER_ -DNO_IRR_COMPILE_WITH_TGA_WRITER_


STATIC_LIB = libIrrlicht.a
LIB_PATH = ../../lib/$(SYSTEM)
INSTALL_DIR = /usr/local/lib

VERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_RELEASE)
SHARED_FULLNAME = $(SHARED_LIB).$(VERSION)
SONAME = $(SHARED_LIB).$(VERSION_MAJOR).$(VERSION_MINOR)

####################
# All target, builds Irrlicht as static lib (libIrrlicht.a) and copies it into lib/Linux
all linux : staticlib

emscripten: staticlib_emscripten

# Builds Irrlicht as shared lib (libIrrlicht.so.versionNumber) and copies it into lib/Linux
sharedlib: $(LINKOBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared -Wl,-soname,$(SONAME) -o $(SHARED_FULLNAME) $^ $(LDFLAGS)
	mkdir -p $(LIB_PATH)
	cp $(SHARED_FULLNAME) $(LIB_PATH)

# Builds Irrlicht as static lib (libIrrlicht.a)
$(STATIC_LIB): $(LINKOBJ)
	$(AR) rs $@ $^

# Copies static lib into ../../lib/($SYSTEM)
staticlib staticlib_osx staticlib_emscripten: $(STATIC_LIB)
	mkdir -p $(LIB_PATH)
	cp $^ $(LIB_PATH)

# Builds Irrlicht as dll (Irrlicht.dll) into ../../bin/Win32-gcc
all_win32 win32: sharedlib_win32
sharedlib_win32: $(IRRLICHT_DLL)
../../bin/Win32-gcc/Irrlicht.dll: $(LINKOBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared -o $@ $^ $(LDFLAGS) -Wl,--out-implib,../../lib/Win32-gcc/$(STATIC_LIB)
# Copies static lib into /lib/Win32-gcc
staticlib_win32: $(STATIC_LIB)
	cp $^ $(LIB_PATH)

# Builds Irrlicht as shared lib (libIrrlicht.so.versionNumber) and copies it into /lib/MacOSX
sharedlib_osx: $(LINKOBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -dynamiclib -Wl,-install_name,$(SONAME) -o $(SHARED_FULLNAME) $^ $(LDFLAGS)
	cp $(SHARED_FULLNAME) $(LIB_PATH)

# Installs Irrlicht if it was created as shared lib
install install_osx:
	$(RM) -r $(INSTALL_DIR)/../include/irrlicht
	mkdir -p $(INSTALL_DIR)/../include/irrlicht
	cp ../../include/*.h $(INSTALL_DIR)/../include/irrlicht/
	cp $(LIB_PATH)/$(SHARED_FULLNAME) $(INSTALL_DIR)
	cd $(INSTALL_DIR) && ln -s -f $(SHARED_FULLNAME) $(SONAME)
	cd $(INSTALL_DIR) && ln -s -f $(SONAME) $(SHARED_LIB)
#	ldconfig -n $(INSTALL_DIR)

TAGS:
	ctags *.cpp ../../include/*.h *.h

# Create dependency files for automatic recompilation
%.d:%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MM -MF $@ $<

# Create dependency files for automatic recompilation
%.d:%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MM -MF $@ $<

# Create object files from objective-c code
%.o:%.mm
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

ifneq ($(MAKECMDGOALS),clean)
-include $(LINKOBJ:.o=.d)
endif

help:
	@echo "Available targets for Irrlicht"
	@echo " sharedlib: Build shared library Irrlicht.so for Linux"
	@echo " staticlib: Build static library Irrlicht.a for Linux"
	@echo " emscripten: Build static emscripen library on Linux"
	@echo " install: Copy shared library to /usr/local/lib"
	@echo ""
	@echo " sharedlib_win32: Build shared library Irrlicht.dll for Windows"
	@echo " staticlib_win32: Build static library Irrlicht.a for Windows"
	@echo ""
	@echo " clean: Clean up directory"

# Cleans all temporary files and compilation results.
clean:
	$(RM) $(LINKOBJ) $(SHARED_FULLNAME) $(STATIC_LIB) $(LINKOBJ:.o=.d)

.PHONY: all sharedlib staticlib sharedlib_win32 staticlib_win32 emscripten staticlib_emscripten help install clean
