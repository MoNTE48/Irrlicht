// Copyright (C) 2002-2012 Nikolaus Gebhardt
// Copyright (C) 2022 Dawid Gan
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_

#include "CIrrDeviceSDL.h"
#include "IEventReceiver.h"
#include "irrList.h"
#include "os.h"
#include "CTimer.h"
#include "irrString.h"
#include "Keycodes.h"
#include "COSOperator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <utility>
#include "SIrrCreationParameters.h"
#if defined(_IRR_COMPILE_WITH_ANGLE_)
#include "CEGLManager.h"
#endif

#if defined(_IRR_ANDROID_PLATFORM_)
#include <jni.h>
#endif

#ifdef _MSC_VER
#pragma comment(lib, "SDL3.lib")
#endif // _MSC_VER

namespace irr::video
{
#ifdef _IRR_COMPILE_WITH_OPENGL_
	IVideoDriver* createOpenGLDriver(const SIrrlichtCreationParameters& params,
			io::IFileSystem* io, CIrrDeviceSDL* device);
#endif

#if defined(_IRR_COMPILE_WITH_OGLES2_)
	IVideoDriver* createOGLES2Driver(const SIrrlichtCreationParameters& params,
			io::IFileSystem* io, CIrrDeviceSDL* device, IContextManager* contextManager);
#endif

#if defined(_IRR_COMPILE_WITH_OGLES1_)
	IVideoDriver* createOGLES1Driver(const SIrrlichtCreationParameters& params,
			io::IFileSystem* io, CIrrDeviceSDL* device);
#endif
}

namespace irr
{

int CIrrDeviceSDL::SDLDeviceInstances = 0;
bool CIrrDeviceSDL::SimulateTouchEvents = false;
bool CIrrDeviceSDL::RelativeMouseAvailable = false;

static constexpr auto KeyMap = [] {
	std::array<EKEY_CODE, SDL_SCANCODE_COUNT> map{};
	map[SDL_SCANCODE_BACKSPACE] = KEY_BACK;
	map[SDL_SCANCODE_TAB] = KEY_TAB;
	map[SDL_SCANCODE_CLEAR] = KEY_CLEAR;
	map[SDL_SCANCODE_RETURN] = KEY_RETURN;
	map[SDL_SCANCODE_PAUSE] = KEY_PAUSE;
	map[SDL_SCANCODE_CAPSLOCK] = KEY_CAPITAL;
	map[SDL_SCANCODE_ESCAPE] = KEY_ESCAPE;
	map[SDL_SCANCODE_SPACE] = KEY_SPACE;
	map[SDL_SCANCODE_PAGEUP] = KEY_PRIOR;
	map[SDL_SCANCODE_PAGEDOWN] = KEY_NEXT;
	map[SDL_SCANCODE_END] = KEY_END;
	map[SDL_SCANCODE_HOME] = KEY_HOME;
	map[SDL_SCANCODE_LEFT] = KEY_LEFT;
	map[SDL_SCANCODE_UP] = KEY_UP;
	map[SDL_SCANCODE_RIGHT] = KEY_RIGHT;
	map[SDL_SCANCODE_DOWN] = KEY_DOWN;
	map[SDL_SCANCODE_SELECT] = KEY_SELECT;
	map[SDL_SCANCODE_PRINTSCREEN] = KEY_PRINT; // KEY_SNAPSHOT?
	map[SDL_SCANCODE_EXECUTE] = KEY_EXECUT;
	map[SDL_SCANCODE_INSERT] = KEY_INSERT;
	map[SDL_SCANCODE_DELETE] = KEY_DELETE;
	map[SDL_SCANCODE_HELP] = KEY_HELP;
	map[SDL_SCANCODE_0] = KEY_KEY_0;
	map[SDL_SCANCODE_1] = KEY_KEY_1;
	map[SDL_SCANCODE_2] = KEY_KEY_2;
	map[SDL_SCANCODE_3] = KEY_KEY_3;
	map[SDL_SCANCODE_4] = KEY_KEY_4;
	map[SDL_SCANCODE_5] = KEY_KEY_5;
	map[SDL_SCANCODE_6] = KEY_KEY_6;
	map[SDL_SCANCODE_7] = KEY_KEY_7;
	map[SDL_SCANCODE_8] = KEY_KEY_8;
	map[SDL_SCANCODE_9] = KEY_KEY_9;
	map[SDL_SCANCODE_A] = KEY_KEY_A;
	map[SDL_SCANCODE_B] = KEY_KEY_B;
	map[SDL_SCANCODE_C] = KEY_KEY_C;
	map[SDL_SCANCODE_D] = KEY_KEY_D;
	map[SDL_SCANCODE_E] = KEY_KEY_E;
	map[SDL_SCANCODE_F] = KEY_KEY_F;
	map[SDL_SCANCODE_G] = KEY_KEY_G;
	map[SDL_SCANCODE_H] = KEY_KEY_H;
	map[SDL_SCANCODE_I] = KEY_KEY_I;
	map[SDL_SCANCODE_J] = KEY_KEY_J;
	map[SDL_SCANCODE_K] = KEY_KEY_K;
	map[SDL_SCANCODE_L] = KEY_KEY_L;
	map[SDL_SCANCODE_M] = KEY_KEY_M;
	map[SDL_SCANCODE_N] = KEY_KEY_N;
	map[SDL_SCANCODE_O] = KEY_KEY_O;
	map[SDL_SCANCODE_P] = KEY_KEY_P;
	map[SDL_SCANCODE_Q] = KEY_KEY_Q;
	map[SDL_SCANCODE_R] = KEY_KEY_R;
	map[SDL_SCANCODE_S] = KEY_KEY_S;
	map[SDL_SCANCODE_T] = KEY_KEY_T;
	map[SDL_SCANCODE_U] = KEY_KEY_U;
	map[SDL_SCANCODE_V] = KEY_KEY_V;
	map[SDL_SCANCODE_W] = KEY_KEY_W;
	map[SDL_SCANCODE_X] = KEY_KEY_X;
	map[SDL_SCANCODE_Y] = KEY_KEY_Y;
	map[SDL_SCANCODE_Z] = KEY_KEY_Z;
	map[SDL_SCANCODE_LGUI] = KEY_LWIN;
	map[SDL_SCANCODE_RGUI] = KEY_RWIN;
	map[SDL_SCANCODE_APPLICATION] = KEY_APPS;
	map[SDL_SCANCODE_POWER] = KEY_SLEEP;
	map[SDL_SCANCODE_SLEEP] = KEY_SLEEP;
	map[SDL_SCANCODE_KP_0] = KEY_NUMPAD0;
	map[SDL_SCANCODE_KP_1] = KEY_NUMPAD1;
	map[SDL_SCANCODE_KP_2] = KEY_NUMPAD2;
	map[SDL_SCANCODE_KP_3] = KEY_NUMPAD3;
	map[SDL_SCANCODE_KP_4] = KEY_NUMPAD4;
	map[SDL_SCANCODE_KP_5] = KEY_NUMPAD5;
	map[SDL_SCANCODE_KP_6] = KEY_NUMPAD6;
	map[SDL_SCANCODE_KP_7] = KEY_NUMPAD7;
	map[SDL_SCANCODE_KP_8] = KEY_NUMPAD8;
	map[SDL_SCANCODE_KP_9] = KEY_NUMPAD9;
	map[SDL_SCANCODE_KP_MULTIPLY] = KEY_MULTIPLY;
	map[SDL_SCANCODE_KP_PERIOD] = KEY_PERIOD;
	map[SDL_SCANCODE_KP_DECIMAL] = KEY_DECIMAL;
	map[SDL_SCANCODE_KP_PLUS] = KEY_ADD;
	map[SDL_SCANCODE_KP_MINUS] = KEY_SUBTRACT;
	map[SDL_SCANCODE_KP_DIVIDE] = KEY_DIVIDE;
	map[SDL_SCANCODE_KP_ENTER] = KEY_RETURN;
	map[SDL_SCANCODE_SEPARATOR] = KEY_SEPARATOR;
	map[SDL_SCANCODE_PERIOD] = KEY_PERIOD;
	map[SDL_SCANCODE_F1] = KEY_F1;
	map[SDL_SCANCODE_F2] = KEY_F2;
	map[SDL_SCANCODE_F3] = KEY_F3;
	map[SDL_SCANCODE_F4] = KEY_F4;
	map[SDL_SCANCODE_F5] = KEY_F5;
	map[SDL_SCANCODE_F6] = KEY_F6;
	map[SDL_SCANCODE_F7] = KEY_F7;
	map[SDL_SCANCODE_F8] = KEY_F8;
	map[SDL_SCANCODE_F9] = KEY_F9;
	map[SDL_SCANCODE_F10] = KEY_F10;
	map[SDL_SCANCODE_F11] = KEY_F11;
	map[SDL_SCANCODE_F12] = KEY_F12;
	map[SDL_SCANCODE_F13] = KEY_F13;
	map[SDL_SCANCODE_F14] = KEY_F14;
	map[SDL_SCANCODE_F15] = KEY_F15;
	map[SDL_SCANCODE_F16] = KEY_F16;
	map[SDL_SCANCODE_F17] = KEY_F17;
	map[SDL_SCANCODE_F18] = KEY_F18;
	map[SDL_SCANCODE_F19] = KEY_F19;
	map[SDL_SCANCODE_F20] = KEY_F20;
	map[SDL_SCANCODE_F21] = KEY_F21;
	map[SDL_SCANCODE_F22] = KEY_F22;
	map[SDL_SCANCODE_F23] = KEY_F23;
	map[SDL_SCANCODE_F24] = KEY_F24;
	map[SDL_SCANCODE_NUMLOCKCLEAR] = KEY_NUMLOCK;
	map[SDL_SCANCODE_SCROLLLOCK] = KEY_SCROLL;
	map[SDL_SCANCODE_LSHIFT] = KEY_LSHIFT;
	map[SDL_SCANCODE_RSHIFT] = KEY_RSHIFT;
	map[SDL_SCANCODE_LCTRL] = KEY_LCONTROL;
	map[SDL_SCANCODE_RCTRL] = KEY_RCONTROL;
	map[SDL_SCANCODE_LALT] = KEY_LMENU;
	map[SDL_SCANCODE_RALT] = KEY_RMENU;
	map[SDL_SCANCODE_MENU] = KEY_MENU;
	map[SDL_SCANCODE_COMMA] = KEY_COMMA;
	map[SDL_SCANCODE_MINUS] = KEY_MINUS;
	map[SDL_SCANCODE_AC_BACK] = KEY_ESCAPE;
	map[SDL_SCANCODE_EQUALS] = KEY_PLUS;
	map[SDL_SCANCODE_SEMICOLON] = KEY_OEM_1;
	map[SDL_SCANCODE_SLASH] = KEY_OEM_2;
	map[SDL_SCANCODE_GRAVE] = KEY_OEM_3;
	map[SDL_SCANCODE_LEFTBRACKET] = KEY_OEM_4;
	map[SDL_SCANCODE_BACKSLASH] = KEY_OEM_5;
	map[SDL_SCANCODE_RIGHTBRACKET] = KEY_OEM_6;
	map[SDL_SCANCODE_APOSTROPHE] = KEY_OEM_7;
	map[SDL_SCANCODE_CRSEL] = KEY_CRSEL;
	map[SDL_SCANCODE_EXSEL] = KEY_EXSEL;
	map[SDL_SCANCODE_MEDIA_NEXT_TRACK] = KEY_MEDIA_NEXT_TRACK;
	map[SDL_SCANCODE_MEDIA_PREVIOUS_TRACK] = KEY_MEDIA_PREV_TRACK;
	map[SDL_SCANCODE_MEDIA_STOP] = KEY_MEDIA_STOP;
	map[SDL_SCANCODE_MEDIA_PLAY] = KEY_MEDIA_PLAY_PAUSE;
	map[SDL_SCANCODE_MUTE] = KEY_VOLUME_MUTE;
	map[SDL_SCANCODE_VOLUMEDOWN] = KEY_VOLUME_DOWN;
	map[SDL_SCANCODE_VOLUMEUP] = KEY_VOLUME_UP;
	return map;
}();

static constexpr struct
{
	u8 Button;
	EMOUSE_INPUT_EVENT Down, Up;
	E_MOUSE_BUTTON_STATE_MASK Mask;
} MouseButtons[] = {
	{SDL_BUTTON_LEFT, EMIE_LMOUSE_PRESSED_DOWN, EMIE_LMOUSE_LEFT_UP, EMBSM_LEFT},
	{SDL_BUTTON_RIGHT, EMIE_RMOUSE_PRESSED_DOWN, EMIE_RMOUSE_LEFT_UP, EMBSM_RIGHT},
	{SDL_BUTTON_MIDDLE, EMIE_MMOUSE_PRESSED_DOWN, EMIE_MMOUSE_LEFT_UP, EMBSM_MIDDLE},
};

static void setMouseModifiers(SEvent::SMouseInput& input)
{
	const bool* keyboardState = SDL_GetKeyboardState(nullptr);
#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
	input.Control = keyboardState[SDL_SCANCODE_LGUI] || keyboardState[SDL_SCANCODE_RGUI];
#else
	input.Control = keyboardState[SDL_SCANCODE_LCTRL] ||
			keyboardState[SDL_SCANCODE_RCTRL];
#endif
	input.Shift = keyboardState[SDL_SCANCODE_LSHIFT] ||
			keyboardState[SDL_SCANCODE_RSHIFT];
}

static float widenSubpixelMove(float delta)
{
	if (delta > 0.0f && delta < 1.0f)
		return 1.0f;
	if (delta < 0.0f && delta > -1.0f)
		return -1.0f;
	return delta;
}

static bool isPrimaryModifierPressed(SDL_Keymod mod)
{
#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
	return (mod & SDL_KMOD_GUI) != 0;
#else
	return (mod & SDL_KMOD_CTRL) != 0;
#endif
}

//! Driver types rendering into an SDL OpenGL context (ANGLE included).
static bool usesOpenGLContext(video::E_DRIVER_TYPE driverType)
{
	return driverType == video::EDT_OPENGL ||
		driverType == video::EDT_OGLES2 ||
		driverType == video::EDT_OGLES1
#if !defined(IRR_ANGLE_CONTEXT_WITHOUT_SDL)
		|| driverType == video::EDT_ANGLE
#endif
		;
}

//! Driver types for which the device creates the CAMetalLayer itself, not SDL.
static bool createsOwnMetalView(video::E_DRIVER_TYPE driverType)
{
#if defined(IRR_ANGLE_CONTEXT_WITHOUT_SDL)
	return driverType == video::EDT_ANGLE;
#else
	(void)driverType;
	return false;
#endif
}

//! constructor
CIrrDeviceSDL::CIrrDeviceSDL(const SIrrlichtCreationParameters& param)
	: CIrrDeviceStub(param),
	Width(param.WindowSize.Width), Height(param.WindowSize.Height),
	Resizable(param.WindowResizable == 1),
#if 0
	AccelerometerIndex(0),
	AccelerometerInstance(0), GyroscopeIndex(0), GyroscopeInstance(0),
#endif
	NativeScaleX(1.0f), NativeScaleY(1.0f), LongTouchTimer(0), LongTouchX(0),
	LongTouchY(0), LongTouchHandled(true)
{
#ifdef _DEBUG
	setDebugName("CIrrDeviceSDL");
#endif

	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#if defined(_IRR_IOS_PLATFORM_)
	// Landscape-only; prevents a UIScene view-rotation bug on iOS.
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#elif defined(_IRR_OSX_PLATFORM_)
	// Enable AppleMomentumScrollSupported on macOS
	SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");
#endif

	SDLDeviceInstances++;

	if (SDLDeviceInstances == 1)
	{
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
		{
			os::Printer::log("SDL initialized", ELL_INFORMATION);
		}
		else
		{
			os::Printer::log("Unable to initialize SDL!", SDL_GetError());
			Close = true;
		}

#if 0 // The game reads no accelerometer or gyroscope
		if (!SDL_InitSubSystem(SDL_INIT_SENSOR))
		{
			os::Printer::log("Failed to init SDL sensor!", SDL_GetError());
		}
#endif

		RelativeMouseAvailable = supportsRelativeMouse();

		// Disable simulated touch and mouse events
		SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
		SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#if defined(_IRR_ANDROID_PLATFORM_) || defined(_IRR_IOS_PLATFORM_)
		// A pen comes as touches only, like a finger
		SDL_SetHint(SDL_HINT_PEN_MOUSE_EVENTS, "0");
#endif

		// Enable simulated touch events on Android versions
		// that don't support relative mouse mode.
#if defined(_IRR_ANDROID_PLATFORM_)
		if (!RelativeMouseAvailable)
		{
			SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
			SimulateTouchEvents = true;
		}
#endif
	}

	const int version = SDL_GetVersion();
	core::stringc sdlversion = "SDL Version ";
	sdlversion += SDL_VERSIONNUM_MAJOR(version);
	sdlversion += ".";
	sdlversion += SDL_VERSIONNUM_MINOR(version);
	sdlversion += ".";
	sdlversion += SDL_VERSIONNUM_MICRO(version);

	Operator = new COSOperator(sdlversion, this);
	if (SDLDeviceInstances == 1)
	{
		os::Printer::log(sdlversion.c_str(), ELL_INFORMATION);
	}


	if (CreationParams.DriverType != video::EDT_NULL)
	{
#if 0
		int num_sensors = 0;
		SDL_SensorID* sensors = SDL_GetSensors(&num_sensors);

		for (int i = 0; i < num_sensors; i++)
		{
			if (SDL_GetSensorTypeForID(sensors[i]) == SDL_SENSOR_ACCEL)
			{
				AccelerometerIndex = sensors[i];
			}
			else if (SDL_GetSensorTypeForID(sensors[i]) == SDL_SENSOR_GYRO)
			{
				GyroscopeIndex = sensors[i];
			}
		}

		SDL_free(sensors);
#endif

		// create the window, only if we do not use the null device
		bool success = createWindow();

		if (!success)
			return;

		if (usesOpenGLContext(CreationParams.DriverType))
		{
			if (param.Vsync)
			{
				// Try adaptive vsync first
				if (!SDL_GL_SetSwapInterval(-1))
				{
					SDL_ClearError();
					SDL_GL_SetSwapInterval(1);
				}
			}
			else
			{
				SDL_GL_SetSwapInterval(0);
			}
		}
	}

	// create cursor control
	CursorControl = new CCursorControl(this);

	// create driver
	createDriver();

	if (VideoDriver)
		createGUIAndScene();
}


//! destructor
CIrrDeviceSDL::~CIrrDeviceSDL()
{
	if (VideoDriver)
		VideoDriver->resetExposedData();

	for (u32 i = 0; i < Joysticks.size(); i++)
		SDL_CloseGamepad(SDL_GetGamepadFromID(Joysticks[i]));

	if (Context)
	{
		SDL_GL_DestroyContext(Context);
		Context = NULL;
	}

#if defined(_IRR_COMPILE_WITH_ANGLE_)
	if (ContextManager)
	{
		ContextManager->drop();
		ContextManager = NULL;
	}
#endif

#ifdef IRR_SDL_METAL_VIEW
	// Hidden before the Metal view leaves, which resets the root view controller on UIKit
	if (MetalView && Window)
		SDL_HideWindow(Window);
	SDL_Metal_DestroyView(MetalView);
#endif

	if (Window)
	{
		SDL_DestroyWindow(Window);
		Window = NULL;
	}

	SDLDeviceInstances--;

	if (SDLDeviceInstances == 0)
	{
		// SDL_Quit frees the error, which the caller of a failed device still reads
		const core::stringc error = SDL_GetError();
		SDL_Quit();
		if (!VideoDriver)
			SDL_SetError("%s", error.c_str());
	}
}

bool CIrrDeviceSDL::createWindow()
{
	if (Close)
		return false;

	// Get native scale before window creation on platforms that support
	// high dpi.
#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
	updateNativeScaleFromSystem();
#endif

	// SDL accepts window dimensions equal to 0 only for fullscreen
	// window. Use desktop size in windowed mode.
	if (!CreationParams.Fullscreen && (Width == 0 || Height == 0))
	{
		// Set windth and height to some non-zero values just in case that
		// get display mode fails
		Width = 640;
		Height = 480;

		int display_count = 0;
		SDL_DisplayID* displays = SDL_GetDisplays(&display_count);

		if (const SDL_DisplayMode* mode = display_count > 0 ?
				SDL_GetDesktopDisplayMode(displays[0]) : nullptr)
		{
			Width = (u32)roundf((float)mode->w * NativeScaleX);
			Height = (u32)roundf((float)mode->h * NativeScaleY);
		}

		SDL_free(displays);
	}

	bool success = createWindowWithContext();

	// The retries give up attributes only an OpenGL context has
	if (!success && usesOpenGLContext(CreationParams.DriverType))
	{
		// Tries again with the lowered attribute and says so if that worked
		auto tryAgain = [&](const char* message) {
			success = createWindowWithContext();
			if (success)
				os::Printer::log(message);
		};

		while (!success && CreationParams.AntiAlias > 1)
		{
			CreationParams.AntiAlias = CreationParams.AntiAlias > 2 ?
					CreationParams.AntiAlias - 1 : 0;
			tryAgain("Use lower AntiAliasing due to lack of support!");
		}
		if (!success && CreationParams.WithAlphaChannel)
		{
			CreationParams.WithAlphaChannel = false;
			tryAgain("AlphaChannel disabled due to lack of support!");
		}
		if (!success && CreationParams.Stencilbuffer)
		{
			CreationParams.Stencilbuffer = false;
			tryAgain("Stencilbuffer disabled due to lack of support!");
		}
		while (!success && CreationParams.ZBufferBits > 16)
		{
			CreationParams.ZBufferBits -= 8;
			tryAgain("Use lower ZBufferBits due to lack of support!");
		}
		while (!success && CreationParams.Bits > 16)
		{
			CreationParams.Bits -= 8;
			tryAgain("Use lower Bits due to lack of support!");
		}
		if (!success && CreationParams.Stereobuffer)
		{
			CreationParams.Stereobuffer = false;
			tryAgain("Stereobuffer disabled due to lack of support!");
		}
		if (!success && CreationParams.Doublebuffer)
		{
			CreationParams.Doublebuffer = false;
			tryAgain("Doublebuffer disabled due to lack of support!");
		}
	}

	if (!success)
	{
		os::Printer::log("Could not create context!");
		return false;
	}

	return true;
}

bool CIrrDeviceSDL::createWindowWithContext()
{
	SDL_WindowFlags SDL_Flags = 0;

#if defined(_IRR_COMPILE_WITH_ANGLE_) && !defined(IRR_ANGLE_CONTEXT_WITHOUT_SDL)
	{
		// SDL loads EGL and GLES from the embedded ANGLE, not from the system
		static const char *ANGLE_PATH =
			"@executable_path/../Frameworks/MetalANGLE.framework/Versions/A/MetalANGLE";
		SDL_SetHint(SDL_HINT_EGL_LIBRARY, ANGLE_PATH);
		SDL_SetHint(SDL_HINT_OPENGL_LIBRARY, ANGLE_PATH);
	}
#endif

	if (CreationParams.Fullscreen)
	{
		SDL_Flags |= SDL_WINDOW_FULLSCREEN;
	}
	else if (Resizable)
	{
		SDL_Flags |= SDL_WINDOW_RESIZABLE;
	}

	if (createsOwnMetalView(CreationParams.DriverType))
	{
		// Shown after the Metal view is attached, which resets the root view controller on UIKit
		SDL_Flags |= SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
	}

	if (usesOpenGLContext(CreationParams.DriverType))
	{
#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
		SDL_Flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

		SDL_Flags |= SDL_WINDOW_OPENGL;

		if (CreationParams.DriverType == video::EDT_OGLES2 ||
				CreationParams.DriverType == video::EDT_ANGLE)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		}
		else if (CreationParams.DriverType == video::EDT_OGLES1)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		}
		else
		{
			// As said in WGL cotext manager:
			// with 3.0 all available profiles should be usable, higher versions impose restrictions
			// we need at least 1.1
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		}

		if (CreationParams.Bits <= 16)
		{
			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 4);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 4);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 4);
		}
		else
		{
			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		}

		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, CreationParams.WithAlphaChannel ? 1 : 0);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, CreationParams.ZBufferBits);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, CreationParams.Doublebuffer ? 1 : 0);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, CreationParams.Stencilbuffer ? 1 : 0);
		SDL_GL_SetAttribute(SDL_GL_STEREO, CreationParams.Stereobuffer ? 1 : 0);

		if (CreationParams.AntiAlias > 1)
		{
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, CreationParams.AntiAlias);
		}
		else
		{
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		}
	}

	Window = SDL_CreateWindow("",
							(int)roundf((float)Width / NativeScaleX),
							(int)roundf((float)Height / NativeScaleY), SDL_Flags);

	if (!Window)
		os::Printer::log("SDL_CreateWindow failed", SDL_GetError(), ELL_ERROR);

	if (usesOpenGLContext(CreationParams.DriverType))
	{
		if (Window)
		{
			Context = SDL_GL_CreateContext(Window);
		}
	}

	if (!Context && CreationParams.DriverType == video::EDT_OGLES2)
	{
		if (Window)
		{
			SDL_DestroyWindow(Window);
			Window = NULL;
		}

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);

		Window = SDL_CreateWindow("",
								(int)roundf((float)Width / NativeScaleX),
								(int)roundf((float)Height / NativeScaleY), SDL_Flags);

		if (!Window)
			os::Printer::log("SDL_CreateWindow failed", SDL_GetError(), ELL_ERROR);

		if (Window)
		{
			Context = SDL_GL_CreateContext(Window);
		}
	}

	if (usesOpenGLContext(CreationParams.DriverType))
	{
		if (!Context)
		{
			if (Window)
			{
				os::Printer::log("SDL_GL_CreateContext failed", SDL_GetError(), ELL_ERROR);
				SDL_DestroyWindow(Window);
				Window = NULL;
			}

			return false;
		}
	}

#ifdef IRR_SDL_METAL_VIEW
	if (createsOwnMetalView(CreationParams.DriverType))
	{
		if (!Window)
			return false;

		MetalView = SDL_Metal_CreateView(Window);
		if (!MetalView)
		{
			os::Printer::log("Could not create Metal view!", SDL_GetError(), ELL_ERROR);
			SDL_DestroyWindow(Window);
			Window = NULL;
			return false;
		}

#if defined(_IRR_COMPILE_WITH_ANGLE_) && defined(IRR_ANGLE_CONTEXT_WITHOUT_SDL)
		if (CreationParams.DriverType == video::EDT_ANGLE)
		{
			video::SExposedVideoData data;
			data.OpenGLOSX.Layer = SDL_Metal_GetLayer(MetalView);
			data.OpenGLOSX.Window = Window;

			ContextManager = new video::CEGLManager();
			if (!ContextManager->initialize(CreationParams, data))
			{
				os::Printer::log("Could not initialize ANGLE EGL context!", ELL_ERROR);
				SDL_DestroyWindow(Window);
				Window = NULL;
				return false;
			}
		}
#endif

		SDL_ShowWindow(Window);
	}
#endif

	updateNativeScale();

	if (CreationParams.WindowSize.Width == 0 || CreationParams.WindowSize.Height == 0)
	{
		int w = 0;
		int h = 0;
		SDL_GetWindowSize(Window, &w, &h);

		Width = (u32)roundf((float)w * NativeScaleX);
		Height = (u32)roundf((float)h * NativeScaleY);
	}

	CreationParams.WindowSize.Width = Width;
	CreationParams.WindowSize.Height = Height;

	return true;
}

void CIrrDeviceSDL::updateNativeScaleFromSystem()
{
	float scaleFactor = 1.0f;

	// Not SDL_GetDisplayContentScale, which is always 1.0 on Apple platforms
	if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
			mode && mode->pixel_density > 0.0f)
		scaleFactor = mode->pixel_density;

	NativeScaleX = scaleFactor;
	NativeScaleY = scaleFactor;
}

void CIrrDeviceSDL::updateNativeScale()
{
	int width = 0;
	int height = 0;
	SDL_GetWindowSize(Window, &width, &height);

	int real_width = width;
	int real_height = height;

	if (usesOpenGLContext(CreationParams.DriverType) ||
		createsOwnMetalView(CreationParams.DriverType))
	{
		SDL_GetWindowSizeInPixels(Window, &real_width, &real_height);
	}

	NativeScaleX = (f32)real_width / (f32)width;
	NativeScaleY = (f32)real_height / (f32)height;
}

void CIrrDeviceSDL::setCursorVisible(bool visible)
{
	if (RelativeMouseAvailable)
	{
		if (visible)
		{
			if (SDL_GetWindowRelativeMouseMode(Window))
			{
				SDL_WarpMouseInWindow(Window,
					(float)Width / getNativeScaleX() / 2.0f,
					(float)Height / getNativeScaleY() / 2.0f);
			}

			SDL_SetWindowRelativeMouseMode(Window, false);
		}
		else
		{
			SDL_SetWindowRelativeMouseMode(Window, true);
		}
	}
#if !defined(_IRR_ANDROID_PLATFORM_) && !defined(_IRR_IOS_PLATFORM_)
	else if (!SimulateTouchEvents)
	{
		if (visible)
			SDL_ShowCursor();
		else
			SDL_HideCursor();
	}
#endif
}

//! create the driver
void CIrrDeviceSDL::createDriver()
{
	switch(CreationParams.DriverType)
	{
	case video::DEPRECATED_EDT_DIRECT3D8_NO_LONGER_EXISTS:
	case video::EDT_DIRECT3D9:
	case video::EDT_SOFTWARE:
	case video::EDT_BURNINGSVIDEO:
		os::Printer::log("SDL device does not support this driver. Try another one.", ELL_ERROR);
		break;

	case video::EDT_OPENGL:
#ifdef _IRR_COMPILE_WITH_OPENGL_
		VideoDriver = video::createOpenGLDriver(CreationParams, FileSystem, this);
#else
		os::Printer::log("No OpenGL support compiled in.", ELL_ERROR);
#endif
		break;

	case video::EDT_OGLES2:
#ifdef _IRR_COMPILE_WITH_OGLES2_
		VideoDriver = video::createOGLES2Driver(CreationParams, FileSystem, this, 0);
#else
		os::Printer::log("No OpenGL ES2 support compiled in.", ELL_ERROR);
#endif
		break;

	case video::EDT_ANGLE:
#if defined(_IRR_COMPILE_WITH_ANGLE_) && defined(_IRR_COMPILE_WITH_OGLES2_)
		// ANGLE hands out a plain GLES2 context, so the ES2 driver is reused as is.
		VideoDriver = video::createOGLES2Driver(CreationParams, FileSystem, this, ContextManager);
#else
		os::Printer::log("No ANGLE support compiled in.", ELL_ERROR);
#endif
		break;

	case video::EDT_OGLES1:
#ifdef _IRR_COMPILE_WITH_OGLES1_
		VideoDriver = video::createOGLES1Driver(CreationParams, FileSystem, this);
#else
		os::Printer::log("No OpenGL ES1 support compiled in.", ELL_ERROR);
#endif
		break;

	case video::EDT_NULL:
		VideoDriver = video::createNullDriver(FileSystem, CreationParams.WindowSize);
		break;

	default:
		os::Printer::log("Unable to create video driver of unknown type.", ELL_ERROR);
		break;
	}
}

//! runs the device. Returns false if device wants to be deleted
bool CIrrDeviceSDL::run()
{
	os::Timer::tick();

	if (Close)
		return false;

	SEvent irrevent;
	SDL_Event SDL_event;

	auto postTouch = [&](ETOUCH_INPUT_EVENT event) {
		irrevent.EventType = irr::EET_TOUCH_INPUT_EVENT;
		irrevent.TouchInput.Event = event;
		irrevent.TouchInput.ID = SDL_event.tfinger.fingerID;
		irrevent.TouchInput.X = (f64)(SDL_event.tfinger.x * (f32)Width);
		irrevent.TouchInput.Y = (f64)(SDL_event.tfinger.y * (f32)Height);
		irrevent.TouchInput.touchedCount = (f64)TouchIDs.size();
		postEventFromUser(irrevent);
	};

	while (!Close && SDL_PollEvent(&SDL_event))
	{
		// os::Printer::log("event: ", core::stringc((int)SDL_event.type).c_str(), ELL_INFORMATION); // just for debugging

		switch (SDL_event.type)
		{
		// From https://github.com/libsdl-org/SDL/blob/main/docs/README-android.md
		// However, there's a chance (on older hardware, or on systems under heavy load),
		// where the GL context can not be restored. In that case you have to
		// listen for a specific message (SDL_EVENT_RENDER_DEVICE_RESET) and restore
		// your textures manually or quit the app.
		case SDL_EVENT_RENDER_DEVICE_RESET:
		case SDL_EVENT_RENDER_DEVICE_LOST:
			Close = true;
			return false;

#if 0
		case SDL_EVENT_SENSOR_UPDATE:
			if (SDL_event.sensor.which == AccelerometerInstance)
			{
				SDL_DisplayOrientation orientation = SDL_GetCurrentDisplayOrientation(0);
				irrevent.EventType = irr::EET_ACCELEROMETER_EVENT;

				if (orientation == SDL_ORIENTATION_LANDSCAPE ||
					orientation == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
				{
					irrevent.AccelerometerEvent.X = SDL_event.sensor.data[0];
					irrevent.AccelerometerEvent.Y = SDL_event.sensor.data[1];
				}
				else
				{
					// For android multi-window mode vertically
					irrevent.AccelerometerEvent.X = -SDL_event.sensor.data[1];
					irrevent.AccelerometerEvent.Y = -SDL_event.sensor.data[0];
				}

				irrevent.AccelerometerEvent.Z = SDL_event.sensor.data[2];

				if (irrevent.AccelerometerEvent.X < 0.0)
				{
					irrevent.AccelerometerEvent.X *= -1.0;
				}

				if (orientation == SDL_ORIENTATION_LANDSCAPE_FLIPPED ||
					orientation == SDL_ORIENTATION_PORTRAIT_FLIPPED)
				{
					irrevent.AccelerometerEvent.Y *= -1.0;
				}

				postEventFromUser(irrevent);
			}
			else if (SDL_event.sensor.which == GyroscopeInstance)
			{
				irrevent.EventType = irr::EET_GYROSCOPE_EVENT;
				irrevent.GyroscopeEvent.X = SDL_event.sensor.data[0];
				irrevent.GyroscopeEvent.Y = SDL_event.sensor.data[1];
				irrevent.GyroscopeEvent.Z = SDL_event.sensor.data[2];
				postEventFromUser(irrevent);
			}
			break;
#endif

		case SDL_EVENT_FINGER_MOTION:
			if (TouchIDs.size() == 1)
			{
				const f32 x = SDL_event.tfinger.x * (f32)Width;
				const f32 y = SDL_event.tfinger.y * (f32)Height;
				if (fabsf((f32)LongTouchX - x) > (f32)Width * 0.05f ||
					fabsf((f32)LongTouchY - y) > (f32)Height * 0.05f)
				{
					LongTouchHandled = true;
				}
			}

			postTouch(irr::ETIE_MOVED);
			break;

		case SDL_EVENT_FINGER_DOWN:
			// Long touch only for first finger
			if (TouchIDs.size() == 0)
			{
				LongTouchTimer = os::Timer::getTime();
				LongTouchX = (s32)(SDL_event.tfinger.x * (f32)Width);
				LongTouchY = (s32)(SDL_event.tfinger.y * (f32)Height);
				LongTouchHandled = false;
			}
			else
			{
				LongTouchHandled = true;
			}

			TouchIDs.insert(SDL_event.tfinger.fingerID);
			postTouch(irr::ETIE_PRESSED_DOWN);
			break;

		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED:
			if (TouchIDs.size() == 1)
			{
				LongTouchHandled = true;
			}

			postTouch(irr::ETIE_LEFT_UP);
			TouchIDs.erase(SDL_event.tfinger.fingerID);
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
			irrevent.MouseInput.Event = irr::EMIE_MOUSE_WHEEL;
			irrevent.MouseInput.X = MouseX;
			irrevent.MouseInput.Y = MouseY;
			setMouseModifiers(irrevent.MouseInput);
			irrevent.MouseInput.ButtonStates = MouseButtonStates;
			// Whole steps: SDL adds up the fractions a trackpad sends
			irrevent.MouseInput.Wheel = (f32)(SDL_event.wheel.integer_x +
					SDL_event.wheel.integer_y);

			if (!core::iszero(irrevent.MouseInput.Wheel))
				postEventFromUser(irrevent);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if (SimulateTouchEvents)
				break;

			if (IgnoreWarpMouseEvent)
			{
				IgnoreWarpMouseEvent = false;
				break;
			}

			irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
			irrevent.MouseInput.Event = irr::EMIE_MOUSE_MOVED;

			if (SDL_GetWindowRelativeMouseMode(Window))
			{
				MouseX += (s32)roundf(widenSubpixelMove(
					SDL_event.motion.xrel * NativeScaleX));
				MouseY += (s32)roundf(widenSubpixelMove(
					SDL_event.motion.yrel * NativeScaleY));
			}
			else
			{
				MouseX = (s32)roundf(SDL_event.motion.x * NativeScaleX);
				MouseY = (s32)roundf(SDL_event.motion.y * NativeScaleY);
			}

			irrevent.MouseInput.X = MouseX;
			irrevent.MouseInput.Y = MouseY;
			setMouseModifiers(irrevent.MouseInput);
			irrevent.MouseInput.ButtonStates = MouseButtonStates;

			postEventFromUser(irrevent);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				if (SimulateTouchEvents)
					break;

				irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
				irrevent.MouseInput.X = (s32)(SDL_event.button.x * NativeScaleX);
				irrevent.MouseInput.Y = (s32)(SDL_event.button.y * NativeScaleY);
				setMouseModifiers(irrevent.MouseInput);
				irrevent.MouseInput.Event = irr::EMIE_MOUSE_MOVED;

				const bool pressed = SDL_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
				for (const auto& [button, down, up, mask] : MouseButtons)
				{
					if (button != SDL_event.button.button)
						continue;
					irrevent.MouseInput.Event = pressed ? down : up;
					if (pressed)
						MouseButtonStates |= mask;
					else
						MouseButtonStates &= ~(u32)mask;
				}

				irrevent.MouseInput.ButtonStates = MouseButtonStates;

				if (irrevent.MouseInput.Event != irr::EMIE_MOUSE_MOVED)
				{
					postEventFromUser(irrevent);

					if ( irrevent.MouseInput.Event >= EMIE_LMOUSE_PRESSED_DOWN && irrevent.MouseInput.Event <= EMIE_MMOUSE_PRESSED_DOWN )
					{
						u32 clicks = checkSuccessiveClicks(irrevent.MouseInput.X, irrevent.MouseInput.Y, irrevent.MouseInput.Event);
						if ( clicks == 2 )
						{
							irrevent.MouseInput.Event = (EMOUSE_INPUT_EVENT)(EMIE_LMOUSE_DOUBLE_CLICK + irrevent.MouseInput.Event-EMIE_LMOUSE_PRESSED_DOWN);
							postEventFromUser(irrevent);
						}
						else if ( clicks == 3 )
						{
							irrevent.MouseInput.Event = (EMOUSE_INPUT_EVENT)(EMIE_LMOUSE_TRIPLE_CLICK + irrevent.MouseInput.Event-EMIE_LMOUSE_PRESSED_DOWN);
							postEventFromUser(irrevent);
						}
					}
				}
			}
			break;

		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			irrevent.EventType = irr::EET_KEY_INPUT_EVENT;
			irrevent.KeyInput.Char = 0;
			irrevent.KeyInput.Key = (size_t)SDL_event.key.scancode < KeyMap.size() ?
					KeyMap[SDL_event.key.scancode] : (EKEY_CODE)0;
			irrevent.KeyInput.PressedDown = (SDL_event.type == SDL_EVENT_KEY_DOWN);
			irrevent.KeyInput.Shift = (SDL_event.key.mod & SDL_KMOD_SHIFT) != 0;
			irrevent.KeyInput.Control = isPrimaryModifierPressed(SDL_event.key.mod);
			irrevent.KeyInput.AutoRepeat = SDL_event.key.repeat != 0;
			irrevent.KeyInput.Extended = false;
			postEventFromUser(irrevent);
			break;

		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			irrevent.EventType = irr::EET_SDL_CONTROLLER_BUTTON_EVENT;
			irrevent.SDLControllerButtonEvent.Joystick = (u8)SDL_event.gbutton.which;
			irrevent.SDLControllerButtonEvent.Button = SDL_event.gbutton.button;
			irrevent.SDLControllerButtonEvent.Pressed = SDL_event.gbutton.down;
			postEventFromUser(irrevent);
			break;

		case SDL_EVENT_GAMEPAD_ADDED:
			if (SDL_Gamepad* gameController = SDL_OpenGamepad(SDL_event.gdevice.which))
				Joysticks.push_back(SDL_GetJoystickID(
						SDL_GetGamepadJoystick(gameController)));
			break;

		case SDL_EVENT_GAMEPAD_REMOVED:
			if (const s32 i = Joysticks.linear_search(SDL_event.gdevice.which); i != -1)
			{
				SDL_CloseGamepad(SDL_GetGamepadFromID(Joysticks[(u32)i]));
				Joysticks.erase((u32)i);
			}
			break;

		case SDL_EVENT_QUIT:
			Close = true;
			return false;

		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			{
				updateNativeScale();

				u32 new_width = (u32)SDL_event.window.data1;
				u32 new_height = (u32)SDL_event.window.data2;

				if (new_width != Width || new_height != Height)
				{
					Width = new_width;
					Height = new_height;

					if (VideoDriver)
						VideoDriver->OnResize(core::dimension2d<u32>(Width, Height));
				}
			}
			break;
		case SDL_EVENT_WINDOW_MOVED:
			break;

		case SDL_EVENT_TEXT_EDITING:
			irrevent.EventType = irr::EET_SDL_TEXT_EVENT;
			irrevent.SDLTextEvent.Type = irr::ESDLET_TEXTEDITING;
			irrevent.SDLTextEvent.Text = SDL_event.edit.text;
			irrevent.SDLTextEvent.Start = SDL_event.edit.start;
			irrevent.SDLTextEvent.Length = SDL_event.edit.length;
			postEventFromUser(irrevent);
			break;

		case SDL_EVENT_TEXT_INPUT:
			irrevent.EventType = irr::EET_SDL_TEXT_EVENT;
			irrevent.SDLTextEvent.Type = irr::ESDLET_TEXTINPUT;
			irrevent.SDLTextEvent.Text = SDL_event.text.text;
			irrevent.SDLTextEvent.Start = 0;
			irrevent.SDLTextEvent.Length = 0;
			postEventFromUser(irrevent);
			break;

		case SDL_EVENT_USER:
			irrevent.EventType = irr::EET_USER_EVENT;
			irrevent.UserEvent.UserData1 = reinterpret_cast<uintptr_t>(SDL_event.user.data1);
			irrevent.UserEvent.UserData2 = reinterpret_cast<uintptr_t>(SDL_event.user.data2);

			postEventFromUser(irrevent);
			break;

		default:
			break;
		} // end switch

	} // end while

	for (u32 i = 0; i < Joysticks.size(); i++)
	{
		if (SDL_Gamepad* gameController = SDL_GetGamepadFromID(Joysticks[i]))
		{
			SEvent axisEvent;
			axisEvent.EventType = EET_SDL_CONTROLLER_AXIS_EVENT;
			axisEvent.SDLControllerAxisEvent.Joystick = (u8)Joysticks[i];

			for (s32 j = 0; j < 6; j++)
			{
				axisEvent.SDLControllerAxisEvent.Axis[j] = (u8)j;
				axisEvent.SDLControllerAxisEvent.Value[j] = SDL_GetGamepadAxis(gameController, (SDL_GamepadAxis)j);
			}

			postEventFromUser(axisEvent);
		}
	}

	if (os::Timer::getTime() > LongTouchTimer + 1000 && !LongTouchHandled)
	{
		LongTouchHandled = true;

		SEvent touchEvent;
		touchEvent.EventType = irr::EET_TOUCH_INPUT_EVENT;
		touchEvent.TouchInput.Event = irr::ETIE_PRESSED_LONG;
		touchEvent.TouchInput.ID = *(TouchIDs.begin());
		touchEvent.TouchInput.X = LongTouchX;
		touchEvent.TouchInput.Y = LongTouchY;
		touchEvent.TouchInput.touchedCount = (f64)TouchIDs.size();
		postEventFromUser(touchEvent);
	}

	return !Close;
}

//! Activate any joysticks, and generate events for them.
bool CIrrDeviceSDL::activateJoysticks(core::array<SJoystickInfo> & /*joystickInfo*/)
{
	return true;
}



//! pause execution temporarily
void CIrrDeviceSDL::yield()
{
	SDL_Delay(0);
}


//! pause execution for a specified time
void CIrrDeviceSDL::sleep(u32 timeMs, bool pauseTimer)
{
	const bool wasStopped = Timer ? Timer->isStopped() : true;
	if (pauseTimer && !wasStopped)
		Timer->stop();

	SDL_Delay(timeMs);

	if (pauseTimer && !wasStopped)
		Timer->start();
}


//! sets the caption of the window
void CIrrDeviceSDL::setWindowCaption(const wchar_t* text)
{
	std::string title(wcslen(text) * sizeof(wchar_t) + 1, '\0');
	core::wcharToUtf8(text, title.data(), title.size());
	SDL_SetWindowTitle(Window, title.c_str());
}


//! presents a surface in the client area
bool CIrrDeviceSDL::present(video::IImage* /*surface*/, void* /*windowId*/, core::rect<s32>* /*srcClip*/)
{
	return false;
}


//! notifies the device that it should close itself
void CIrrDeviceSDL::closeDevice()
{
	Close = true;
}


//! \return Pointer to a list with all video modes supported
video::IVideoModeList* CIrrDeviceSDL::getVideoModeList()
{
	if (!VideoModeList->getVideoModeCount())
	{
		// enumerate video modes.
		int display_count = 0;
		SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
		int mode_count = 0;
		SDL_DisplayMode** modes = display_count > 0 ?
			SDL_GetFullscreenDisplayModes(displays[0], &mode_count) : 0;

		if (display_count < 1)
			os::Printer::log("No display created: ", SDL_GetError(), ELL_ERROR);
		else if (mode_count < 1)
			os::Printer::log("No display modes available: ", SDL_GetError(), ELL_ERROR);
		else
		{
			if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(displays[0]))
			{
				VideoModeList->setDesktop(SDL_BITSPERPIXEL(mode->format),
					core::dimension2d<u32>((u32)mode->w, (u32)mode->h));
			}

			for (int i = 0; i < mode_count; i++)
			{
				if (const SDL_DisplayMode* mode = modes[i])
				{
					VideoModeList->addMode(
						core::dimension2d<u32>((u32)mode->w, (u32)mode->h),
						SDL_BITSPERPIXEL(mode->format));
				}
			}
		}

		SDL_free(modes);
		SDL_free(displays);
	}

	return VideoModeList;
}

//! Sets if the window should be resizable in windowed mode.
void CIrrDeviceSDL::setResizable(bool resize)
{
	if (CreationParams.Fullscreen)
		return;

	SDL_SetWindowResizable(Window, resize ? true : false);
	Resizable = resize;
}


//! Minimizes window if possible
void CIrrDeviceSDL::minimizeWindow()
{
	SDL_MinimizeWindow(Window);
}


//! Maximize window
void CIrrDeviceSDL::maximizeWindow()
{
	SDL_MaximizeWindow(Window);
}

//! Get the position of this window on screen
core::position2di CIrrDeviceSDL::getWindowPosition()
{
	int x = -1;
	int y = -1;
	SDL_GetWindowPosition(Window, &x, &y);

	return core::position2di(x, y);
}


//! Restore original window size
void CIrrDeviceSDL::restoreWindow()
{
	SDL_RestoreWindow(Window);
}

//! returns if window is active. if not, nothing need to be drawn
bool CIrrDeviceSDL::isWindowActive() const
{
	const SDL_WindowFlags flags = SDL_GetWindowFlags(Window);
	return (flags & SDL_WINDOW_INPUT_FOCUS) && !(flags & SDL_WINDOW_MINIMIZED);
}


//! returns if window has focus.
bool CIrrDeviceSDL::isWindowFocused() const
{
	return SDL_GetWindowFlags(Window) & SDL_WINDOW_INPUT_FOCUS;
}


//! returns if window is minimized.
bool CIrrDeviceSDL::isWindowMinimized() const
{
	return SDL_GetWindowFlags(Window) & SDL_WINDOW_MINIMIZED;
}


//! gets text from the clipboard
//! \return Returns empty string on failure.
const c8* CIrrDeviceSDL::getTextFromClipboard() const
{
	char* text = SDL_GetClipboardText();
	ClipboardText = text;
	SDL_free(text);
	return ClipboardText.c_str();
}

//! copies text to the clipboard
void CIrrDeviceSDL::copyToClipboard(const c8* text) const
{
	SDL_SetClipboardText(text);
}

//! returns color format of the window.
video::ECOLOR_FORMAT CIrrDeviceSDL::getColorFormat() const
{
	if (!Window)
		return CIrrDeviceStub::getColorFormat();

	const u32 pixel_format = SDL_GetWindowPixelFormat(Window);
	const bool alpha = SDL_ISPIXELFORMAT_ALPHA(pixel_format);
	if (SDL_BITSPERPIXEL(pixel_format) == 16)
		return alpha ? video::ECF_A1R5G5B5 : video::ECF_R5G6B5;
	return alpha ? video::ECF_A8R8G8B8 : video::ECF_R8G8B8;
}


#if 0
bool CIrrDeviceSDL::activateAccelerometer(float updateInterval)
{
	if (AccelerometerInstance == 0 && AccelerometerIndex != 0)
	{
		SDL_Sensor* accel = SDL_OpenSensor(AccelerometerIndex);

		if (accel)
			AccelerometerInstance = SDL_GetSensorID(accel);
	}

	return AccelerometerInstance != 0;
}

bool CIrrDeviceSDL::deactivateAccelerometer()
{
	if (AccelerometerInstance == 0)
		return false;

	SDL_Sensor* accel = SDL_GetSensorFromID(AccelerometerInstance);

	if (!accel)
		return false;

	SDL_CloseSensor(accel);
	AccelerometerInstance = 0;

	return true;
}

bool CIrrDeviceSDL::isAccelerometerActive()
{
	return AccelerometerInstance != 0;
}

bool CIrrDeviceSDL::isAccelerometerAvailable()
{
	return AccelerometerIndex != 0;
}

bool CIrrDeviceSDL::activateGyroscope(float updateInterval)
{
	if (GyroscopeInstance == 0 && GyroscopeIndex != 0)
	{
		SDL_Sensor* gyro = SDL_OpenSensor(GyroscopeIndex);

		if (gyro)
			GyroscopeInstance = SDL_GetSensorID(gyro);
	}

	return GyroscopeInstance != 0;
}

bool CIrrDeviceSDL::deactivateGyroscope()
{
	if (GyroscopeInstance == 0)
		return false;

	SDL_Sensor* gyro = SDL_GetSensorFromID(GyroscopeInstance);

	if (!gyro)
		return false;

	SDL_CloseSensor(gyro);
	GyroscopeInstance = 0;

	return true;
}

bool CIrrDeviceSDL::isGyroscopeActive()
{
	return GyroscopeInstance != 0;
}

bool CIrrDeviceSDL::isGyroscopeAvailable()
{
	return GyroscopeIndex != 0;
}
#endif

bool CIrrDeviceSDL::supportsRelativeMouse()
{
#if defined(_IRR_ANDROID_PLATFORM_)
	JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();

	if (!env)
		return false;

	jobject activity = (jobject)SDL_GetAndroidActivity();

	if (!activity)
		return false;

	jclass activityClass = env->GetObjectClass(activity);
	env->DeleteLocalRef(activity);

	if (!activityClass)
		return false;

	jmethodID supportsRelativeMouse = env->GetStaticMethodID(activityClass, "supportsRelativeMouse", "()Z");
	const bool supported = supportsRelativeMouse && env->CallStaticBooleanMethod(activityClass, supportsRelativeMouse);
	env->DeleteLocalRef(activityClass);

	return supported;

#else
	return true;
#endif
}

void CIrrDeviceSDL::CCursorControl::initCursors()
{
	Cursors = {
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT),     // ECI_NORMAL
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR), // ECI_CROSS
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER),      // ECI_HAND
		nullptr,                                             // ECI_HELP
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT),     // ECI_IBEAM
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED),        // ECI_NO
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT),      // ECI_WAIT
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE),   // ECI_SIZEALL
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE),  // ECI_SIZENESW
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE),  // ECI_SIZENWSE
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE),    // ECI_SIZENS
		SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE),    // ECI_SIZEWE
		nullptr,                                             // ECI_UP
	};
}

CIrrDeviceSDL::CCursorControl::~CCursorControl()
{
	for (SDL_Cursor* cursor : Cursors)
		SDL_DestroyCursor(cursor);
}

void CIrrDeviceSDL::CCursorControl::setActiveIcon(gui::ECURSOR_ICON iconId)
{
	ActiveIcon = iconId;
	if (iconId >= Cursors.size() || !Cursors[iconId])
	{
		iconId = gui::ECI_NORMAL;
		if (iconId >= Cursors.size() || !Cursors[iconId])
			return;
	}
	SDL_SetCursor(Cursors[iconId]);
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_SDL_DEVICE_

