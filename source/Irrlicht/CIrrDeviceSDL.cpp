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
#include "SIrrCreationParameters.h"

#if defined(_IRR_IOS_PLATFORM_)
#import <UIKit/UIKit.h>
#elif defined(_IRR_OSX_PLATFORM_)
#import <AppKit/AppKit.h>
#endif

#ifdef _MSC_VER
#pragma comment(lib, "SDL2.lib")
#endif // _MSC_VER

static int SDLDeviceInstances = 0;

namespace irr
{
	namespace video
	{
#ifdef _IRR_COMPILE_WITH_OPENGL_
		IVideoDriver* createOpenGLDriver(const SIrrlichtCreationParameters& params,
				io::IFileSystem* io, CIrrDeviceSDL* device);
#endif

#if defined(_IRR_COMPILE_WITH_OGLES2_)
		IVideoDriver* createOGLES2Driver(const SIrrlichtCreationParameters& params,
				io::IFileSystem* io, CIrrDeviceSDL* device);
#endif

#if defined(_IRR_COMPILE_WITH_OGLES1_)
		IVideoDriver* createOGLES1Driver(const SIrrlichtCreationParameters& params,
				io::IFileSystem* io, CIrrDeviceSDL* device);
#endif
	} // end namespace video

} // end namespace irr


namespace irr
{

//! constructor
CIrrDeviceSDL::CIrrDeviceSDL(const SIrrlichtCreationParameters& param)
	: CIrrDeviceStub(param),
	Window(0), Context(0),
	MouseX(0), MouseY(0), MouseButtonStates(0),
	Width(param.WindowSize.Width), Height(param.WindowSize.Height),
	WindowHasFocus(false), WindowMinimized(false),
	Resizable(param.WindowResizable == 1),
	AccelerometerIndex(-1), AccelerometerInstance(-1),
	GyroscopeIndex(-1), GyroscopeInstance(-1),
	NativeScaleX(1.0f), NativeScaleY(1.0f),
	IgnoreWarpMouseEvent(false)
{
#ifdef _DEBUG
	setDebugName("CIrrDeviceSDL");
#endif

	if ( ++SDLDeviceInstances == 1 )
	{
		SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
		SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");

		// Disable simulated mouse events
		SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

#if !defined(_IRR_COMPILE_WITH_SDL_MOUSE_EVENTS_)
		// Enable simulated touch events if mouse events are disabled
		SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
#endif

		u32 flags = SDL_INIT_TIMER | SDL_INIT_VIDEO;

#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
		flags |= SDL_INIT_JOYSTICK;
#endif

		// Initialize SDL... Timer for sleep, video for the obvious
		if (SDL_Init(flags) < 0)
		{
			os::Printer::log( "Unable to initialize SDL!", SDL_GetError());
			Close = true;
		}
		else
		{
			os::Printer::log("SDL initialized", ELL_INFORMATION);
		}

		if (SDL_InitSubSystem(SDL_INIT_SENSOR) < 0)
		{
			os::Printer::log("Failed to init SDL sensor!", SDL_GetError());
		}

#if defined(_IRR_OSX_PLATFORM_)
		// Enable AppleMomentumScrollSupported on macOS
		[[NSUserDefaults standardUserDefaults] setBool: YES
							   forKey: @"AppleMomentumScrollSupported"];
#endif
	}

	SDL_version version;
	SDL_VERSION(&version);

	SDL_GetVersion(&version);
	core::stringc sdlversion = "SDL Version ";
	sdlversion += version.major;
	sdlversion += ".";
	sdlversion += version.minor;
	sdlversion += ".";
	sdlversion += version.patch;

	Operator = new COSOperator(sdlversion, this);
	if ( SDLDeviceInstances == 1 )
	{
		os::Printer::log(sdlversion.c_str(), ELL_INFORMATION);
	}

	// create keymap
	createKeyMap();

	if (CreationParams.DriverType != video::EDT_NULL)
	{
		for (int i = 0; i < SDL_NumSensors(); i++)
		{
			if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_ACCEL)
			{
				AccelerometerIndex = i;
			}
			else if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_GYRO)
			{
				GyroscopeIndex = i;
			}
		}

		// create the window, only if we do not use the null device
		createWindow();

		if (CreationParams.DriverType == video::EDT_OPENGL ||
			CreationParams.DriverType == video::EDT_OGLES2 ||
			CreationParams.DriverType == video::EDT_OGLES1)
		{
			if (param.Vsync)
			{
				// Try adaptive vsync first
				int ret = SDL_GL_SetSwapInterval(-1);

				if (ret == -1)
				{
					SDL_GL_SetSwapInterval(param.Vsync ? 1 : 0);
				}
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
	if ( --SDLDeviceInstances == 0 )
	{
		if (VideoDriver)
		{
#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
		const u32 numJoysticks = Joysticks.size();
		for (u32 i=0; i<numJoysticks; ++i)
			SDL_JoystickClose(Joysticks[i]);
#endif
		if (Context)
		{
			SDL_GL_DeleteContext(Context);
			Context = NULL;
		}
		if (Window)
		{
			SDL_DestroyWindow(Window);
			Window = NULL;
		}

		SDL_Quit();
		os::Printer::log("Quit SDL", ELL_INFORMATION);
		}
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
		SDL_DisplayMode mode = {};
		int err = SDL_GetDesktopDisplayMode(0, &mode);

		if (err == 0)
		{
			Width = mode.w * NativeScaleX;
			Height = mode.h * NativeScaleY;

		}
		else
		{
			// Shouldn't happen, just in case
			Width = 640;
			Height = 480;

		}
	}

	bool success = createWindowWithContext();

	if (!success)
	{
		if (CreationParams.AntiAlias > 1)
		{
			while (--CreationParams.AntiAlias > 1)
			{
				success = createWindowWithContext();

				if (success)
					break;
			}

			if (!success)
			{
				CreationParams.AntiAlias = 0;

				success = createWindowWithContext();

				if (success)
				{
					os::Printer::log("AntiAliasing disabled due to lack of support!");
				}
			}
		}

		if (!success && CreationParams.Doublebuffer)
		{
			// Try single buffer
			CreationParams.Doublebuffer = 0;

			success = createWindowWithContext();

			if (success)
			{
				os::Printer::log("Doublebuffer disabled due to lack of support!");
			}
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
	int SDL_Flags = 0;

	if (CreationParams.Fullscreen)
	{
		SDL_Flags |= SDL_WINDOW_FULLSCREEN;
	}
	else if (Resizable)
	{
		SDL_Flags |= SDL_WINDOW_RESIZABLE;
	}

	if (CreationParams.DriverType == video::EDT_OPENGL ||
		CreationParams.DriverType == video::EDT_OGLES2 ||
		CreationParams.DriverType == video::EDT_OGLES1)
	{
#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
		SDL_Flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif

		SDL_Flags |= SDL_WINDOW_OPENGL;

		if (CreationParams.DriverType == video::EDT_OGLES2)
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

		if (CreationParams.Bits == 16)
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

	Window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
							Width / NativeScaleX, Height / NativeScaleY, SDL_Flags);

	if (!Window && CreationParams.DriverType == video::EDT_OGLES2)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);

		Window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
								Width / NativeScaleX, Height / NativeScaleY, SDL_Flags);
	}

	if (!Window)
		return false;

	if (CreationParams.DriverType == video::EDT_OPENGL ||
		CreationParams.DriverType == video::EDT_OGLES2 ||
		CreationParams.DriverType == video::EDT_OGLES1)
	{
		Context = SDL_GL_CreateContext(Window);

		if (!Context)
		{
			SDL_DestroyWindow(Window);
			Window = NULL;
			return false;
		}
	}

	updateNativeScale();

	if (CreationParams.WindowSize.Width == 0 || CreationParams.WindowSize.Height == 0)
	{
		int w = 0;
		int h = 0;
		SDL_GetWindowSize(Window, &w, &h);

		Width = w * NativeScaleX;
		Height = h * NativeScaleX;
	}

	CreationParams.WindowSize.Width = Width;
	CreationParams.WindowSize.Height = Height;

	return true;
}

void CIrrDeviceSDL::updateNativeScaleFromSystem()
{
	float scaleFactor = 1.0f;

#if defined(_IRR_IOS_PLATFORM_)
	scaleFactor = UIScreen.mainScreen.scale;
#elif defined(_IRR_OSX_PLATFORM_)
	scaleFactor = [[NSScreen mainScreen] backingScaleFactor];
#endif

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

	if (CreationParams.DriverType == video::EDT_OPENGL ||
		CreationParams.DriverType == video::EDT_OGLES2 ||
		CreationParams.DriverType == video::EDT_OGLES1)
	{
		SDL_GL_GetDrawableSize(Window, &real_width, &real_height);
	}

	NativeScaleX = (f32)real_width / (f32)width;
	NativeScaleY = (f32)real_height / (f32)height;
}

void CIrrDeviceSDL::setCursorVisible(bool visible)
{
#if defined(_IRR_OSX_PLATFORM_)
	if (visible)
		CGDisplayShowCursor(CGMainDisplayID());
	else
		CGDisplayHideCursor(CGMainDisplayID());
#else
	if (visible)
	{
		SDL_ShowCursor(SDL_ENABLE);
//#if defined(_IRR_OSX_PLATFORM_)
//		NSApp.presentationOptions &= ~NSApplicationPresentationDisableCursorLocationAssistance;
//#endif
	}
	else
	{
		SDL_ShowCursor(SDL_DISABLE);
//#if defined(_IRR_OSX_PLATFORM_)
//		NSApp.presentationOptions |= NSApplicationPresentationDisableCursorLocationAssistance;
//#endif
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
		VideoDriver = video::createOGLES2Driver(CreationParams, FileSystem, this);
#else
		os::Printer::log("No OpenGL ES2 support compiled in.", ELL_ERROR);
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

	SEvent irrevent;
	SDL_Event SDL_event;

	while (!Close && SDL_PollEvent(&SDL_event))
	{
		// os::Printer::log("event: ", core::stringc((int)SDL_event.type).c_str(), ELL_INFORMATION); // just for debugging

		switch (SDL_event.type)
		{
		case SDL_APP_WILLENTERBACKGROUND:
			irrevent.EventType = irr::EET_APPLICATION_EVENT;
			irrevent.ApplicationEvent.EventType = irr::EAET_WILL_PAUSE;
			postEventFromUser(irrevent);
			break;

		case SDL_APP_DIDENTERBACKGROUND:
			irrevent.EventType = irr::EET_APPLICATION_EVENT;
			irrevent.ApplicationEvent.EventType = irr::EAET_DID_PAUSE;
			postEventFromUser(irrevent);
			break;

		case SDL_APP_WILLENTERFOREGROUND:
			irrevent.EventType = irr::EET_APPLICATION_EVENT;
			irrevent.ApplicationEvent.EventType = irr::EAET_WILL_RESUME;
			postEventFromUser(irrevent);
			break;

		case SDL_APP_DIDENTERFOREGROUND:
			irrevent.EventType = irr::EET_APPLICATION_EVENT;
			irrevent.ApplicationEvent.EventType = irr::EAET_DID_RESUME;
			postEventFromUser(irrevent);
			break;

		case SDL_APP_LOWMEMORY:
			irrevent.EventType = irr::EET_APPLICATION_EVENT;
			irrevent.ApplicationEvent.EventType = irr::EAET_MEMORY_WARNING;
			postEventFromUser(irrevent);
			break;

		case SDL_APP_TERMINATING:
			irrevent.EventType = irr::EET_APPLICATION_EVENT;
			irrevent.ApplicationEvent.EventType = irr::EAET_WILL_TERMINATE;
			postEventFromUser(irrevent);
			break;

		// From https://github.com/libsdl-org/SDL/blob/main/docs/README-android.md
		// However, there's a chance (on older hardware, or on systems under heavy load),
		// where the GL context can not be restored. In that case you have to
		// listen for a specific message (SDL_RENDER_DEVICE_RESET) and restore
		// your textures manually or quit the app.
		case SDL_RENDER_DEVICE_RESET:
			Close = true;
			break;

		case SDL_SENSORUPDATE:
			if (SDL_event.sensor.which == AccelerometerInstance)
			{
				SDL_DisplayOrientation orientation = SDL_GetDisplayOrientation(0);
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

		case SDL_FINGERMOTION:
			irrevent.EventType = irr::EET_TOUCH_INPUT_EVENT;
			irrevent.TouchInput.Event = irr::ETIE_MOVED;
			irrevent.TouchInput.ID = SDL_event.tfinger.fingerId;
			irrevent.TouchInput.X = SDL_event.tfinger.x * Width;
			irrevent.TouchInput.Y = SDL_event.tfinger.y * Height;
			irrevent.TouchInput.touchedCount = TouchIDs.size();
			postEventFromUser(irrevent);
			break;

		case SDL_FINGERDOWN:
			TouchIDs.insert(SDL_event.tfinger.fingerId);
			irrevent.EventType = irr::EET_TOUCH_INPUT_EVENT;
			irrevent.TouchInput.Event = irr::ETIE_PRESSED_DOWN;
			irrevent.TouchInput.ID = SDL_event.tfinger.fingerId;
			irrevent.TouchInput.X = SDL_event.tfinger.x * Width;
			irrevent.TouchInput.Y = SDL_event.tfinger.y * Height;
			irrevent.TouchInput.touchedCount = TouchIDs.size();
			postEventFromUser(irrevent);
			break;

		case SDL_FINGERUP:
			irrevent.EventType = irr::EET_TOUCH_INPUT_EVENT;
			irrevent.TouchInput.Event = irr::ETIE_LEFT_UP;
			irrevent.TouchInput.ID = SDL_event.tfinger.fingerId;
			irrevent.TouchInput.X = SDL_event.tfinger.x * Width;
			irrevent.TouchInput.Y = SDL_event.tfinger.y * Height;
			irrevent.TouchInput.touchedCount = TouchIDs.size();
			postEventFromUser(irrevent);
			TouchIDs.erase(SDL_event.tfinger.fingerId);
			break;

#if defined(_IRR_COMPILE_WITH_SDL_MOUSE_EVENTS_)
		case SDL_MOUSEWHEEL:
			{
				irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
				irrevent.MouseInput.Event = irr::EMIE_MOUSE_WHEEL;
				irrevent.MouseInput.X = MouseX;
				irrevent.MouseInput.Y = MouseY;

				const Uint8* keyboardState = SDL_GetKeyboardState(NULL);

#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
				irrevent.MouseInput.Control = keyboardState[SDL_SCANCODE_LGUI] ||
					keyboardState[SDL_SCANCODE_RGUI];
#else
				irrevent.MouseInput.Control = keyboardState[SDL_SCANCODE_LCTRL] ||
					keyboardState[SDL_SCANCODE_RCTRL];
#endif
				irrevent.MouseInput.Shift = keyboardState[SDL_SCANCODE_LSHIFT] ||
					keyboardState[SDL_SCANCODE_RSHIFT];

				irrevent.MouseInput.ButtonStates = MouseButtonStates;
#if SDL_VERSION_ATLEAST(2, 0, 18)
				irrevent.MouseInput.Wheel = SDL_event.wheel.preciseX + SDL_event.wheel.preciseY;
#else
				irrevent.MouseInput.Wheel = SDL_event.wheel.x + SDL_event.wheel.y;
#endif

				postEventFromUser(irrevent);
			}
			break;
		case SDL_MOUSEMOTION:
			{
				if (IgnoreWarpMouseEvent)
				{
					IgnoreWarpMouseEvent = false;
					break;
				}

				irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
				irrevent.MouseInput.Event = irr::EMIE_MOUSE_MOVED;
				MouseX = irrevent.MouseInput.X = SDL_event.motion.x * NativeScaleX;
				MouseY = irrevent.MouseInput.Y = SDL_event.motion.y * NativeScaleY;

				const Uint8* keyboardState = SDL_GetKeyboardState(NULL);

#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
				irrevent.MouseInput.Control = keyboardState[SDL_SCANCODE_LGUI] ||
					keyboardState[SDL_SCANCODE_RGUI];
#else
				irrevent.MouseInput.Control = keyboardState[SDL_SCANCODE_LCTRL] ||
					keyboardState[SDL_SCANCODE_RCTRL];
#endif
				irrevent.MouseInput.Shift = keyboardState[SDL_SCANCODE_LSHIFT] ||
					keyboardState[SDL_SCANCODE_RSHIFT];

				irrevent.MouseInput.ButtonStates = MouseButtonStates;

				postEventFromUser(irrevent);
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			{
				irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
				irrevent.MouseInput.X = SDL_event.button.x * NativeScaleX;
				irrevent.MouseInput.Y = SDL_event.button.y * NativeScaleY;

				const Uint8* keyboardState = SDL_GetKeyboardState(NULL);

#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
				irrevent.MouseInput.Control = keyboardState[SDL_SCANCODE_LGUI] ||
					keyboardState[SDL_SCANCODE_RGUI];
#else
				irrevent.MouseInput.Control = keyboardState[SDL_SCANCODE_LCTRL] ||
					keyboardState[SDL_SCANCODE_RCTRL];
#endif
				irrevent.MouseInput.Shift = keyboardState[SDL_SCANCODE_LSHIFT] ||
					keyboardState[SDL_SCANCODE_RSHIFT];

				irrevent.MouseInput.Event = irr::EMIE_MOUSE_MOVED;

				switch(SDL_event.button.button)
				{
				case SDL_BUTTON_LEFT:
					if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
					{
						irrevent.MouseInput.Event = irr::EMIE_LMOUSE_PRESSED_DOWN;
						MouseButtonStates |= irr::EMBSM_LEFT;
					}
					else
					{
						irrevent.MouseInput.Event = irr::EMIE_LMOUSE_LEFT_UP;
						MouseButtonStates &= ~irr::EMBSM_LEFT;
					}
					break;

				case SDL_BUTTON_RIGHT:
					if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
					{
						irrevent.MouseInput.Event = irr::EMIE_RMOUSE_PRESSED_DOWN;
						MouseButtonStates |= irr::EMBSM_RIGHT;
					}
					else
					{
						irrevent.MouseInput.Event = irr::EMIE_RMOUSE_LEFT_UP;
						MouseButtonStates &= ~irr::EMBSM_RIGHT;
					}
					break;

				case SDL_BUTTON_MIDDLE:
					if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
					{
						irrevent.MouseInput.Event = irr::EMIE_MMOUSE_PRESSED_DOWN;
						MouseButtonStates |= irr::EMBSM_MIDDLE;
					}
					else
					{
						irrevent.MouseInput.Event = irr::EMIE_MMOUSE_LEFT_UP;
						MouseButtonStates &= ~irr::EMBSM_MIDDLE;
					}
					break;
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
#endif

		case SDL_KEYDOWN:
		case SDL_KEYUP:
			{
				SDL_Scancode scancode = SDL_GetScancodeFromKey(SDL_event.key.keysym.sym);

				SKeyMap mp;
				mp.Scancode = scancode;
				s32 idx = KeyMap.binary_search(mp);

				if (idx == -1)
				{
					mp.Scancode = SDL_event.key.keysym.scancode;
					idx = KeyMap.binary_search(mp);
				}

				EKEY_CODE key;
				if (idx == -1)
					key = (EKEY_CODE)0;
				else
					key = (EKEY_CODE)KeyMap[idx].IrrKeycode;

				irrevent.EventType = irr::EET_KEY_INPUT_EVENT;
				irrevent.KeyInput.Char = 0;
				irrevent.KeyInput.Key = key;
				irrevent.KeyInput.PressedDown = (SDL_event.type == SDL_KEYDOWN);
				irrevent.KeyInput.Shift = (SDL_event.key.keysym.mod & KMOD_SHIFT) != 0;
#if defined(_IRR_IOS_PLATFORM_) || defined(_IRR_OSX_PLATFORM_)
				irrevent.KeyInput.Control = (SDL_event.key.keysym.mod & KMOD_GUI) != 0;
#else
				irrevent.KeyInput.Control = (SDL_event.key.keysym.mod & KMOD_CTRL) != 0;
#endif
				postEventFromUser(irrevent);
			}
			break;

		case SDL_QUIT:
			Close = true;
			break;

		case SDL_WINDOWEVENT:
			{
				if (SDL_event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				{
					updateNativeScale();

					u32 new_width = SDL_event.window.data1 * NativeScaleX;
					u32 new_height = SDL_event.window.data2 * NativeScaleY;

					if (new_width != Width || new_height != Height)
					{
						Width = new_width;
						Height = new_height;

						if (VideoDriver)
							VideoDriver->OnResize(core::dimension2d<u32>(Width, Height));
					}
				}
				else if (SDL_event.window.event == SDL_WINDOWEVENT_MINIMIZED)
				{
					WindowMinimized = true;
				}
				else if (SDL_event.window.event == SDL_WINDOWEVENT_MAXIMIZED)
				{
					WindowMinimized = false;
				}
				else if (SDL_event.window.event == SDL_WINDOWEVENT_RESTORED)
				{
					WindowMinimized = false;
				}
				else if (SDL_event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
				{
					WindowHasFocus = true;
				}
				else if (SDL_event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
				{
					WindowHasFocus = false;
				}
				else if (SDL_event.window.event == SDL_WINDOWEVENT_MOVED)
				{
				}
			}
			break;

		case SDL_TEXTEDITING:
			{
				irrevent.EventType = irr::EET_SDL_TEXT_EVENT;
				irrevent.SDLTextEvent.Type = irr::ESDLET_TEXTEDITING;
				const size_t size = sizeof(irrevent.SDLTextEvent.Text);
				const size_t other_size = sizeof(SDL_event.edit.text);
				static_assert(sizeof(size) == sizeof(other_size), "Wrong size");
				memcpy(irrevent.SDLTextEvent.Text, SDL_event.edit.text, size);
				irrevent.SDLTextEvent.Start = SDL_event.edit.start;
				irrevent.SDLTextEvent.Length = SDL_event.edit.length;
				postEventFromUser(irrevent);
			}
			break;

		case SDL_TEXTINPUT:
			{
				irrevent.EventType = irr::EET_SDL_TEXT_EVENT;
				irrevent.SDLTextEvent.Type = irr::ESDLET_TEXTINPUT;
				const size_t size = sizeof(irrevent.SDLTextEvent.Text);
				const size_t other_size = sizeof(SDL_event.text.text);
				static_assert(sizeof(size) == sizeof(other_size), "Wrong size");
				memcpy(irrevent.SDLTextEvent.Text, SDL_event.text.text, size);
				irrevent.SDLTextEvent.Start = 0;
				irrevent.SDLTextEvent.Length = 0;
				postEventFromUser(irrevent);
			}
			break;

		case SDL_USEREVENT:
			irrevent.EventType = irr::EET_USER_EVENT;
			irrevent.UserEvent.UserData1 = reinterpret_cast<uintptr_t>(SDL_event.user.data1);
			irrevent.UserEvent.UserData2 = reinterpret_cast<uintptr_t>(SDL_event.user.data2);

			postEventFromUser(irrevent);
			break;

		default:
			break;
		} // end switch

	} // end while

#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
	// TODO: Check if the multiple open/close calls are too expensive, then
	// open/close in the constructor/destructor instead

	// update joystick states manually
	SDL_JoystickUpdate();
	// we'll always send joystick input events...
	SEvent joyevent;
	joyevent.EventType = EET_JOYSTICK_INPUT_EVENT;
	for (u32 i=0; i<Joysticks.size(); ++i)
	{
		SDL_Joystick* joystick = Joysticks[i];
		if (joystick)
		{
			int j;
			// query all buttons
			const int numButtons = core::min_(SDL_JoystickNumButtons(joystick), 32);
			joyevent.JoystickEvent.ButtonStates=0;
			for (j=0; j<numButtons; ++j)
				joyevent.JoystickEvent.ButtonStates |= (SDL_JoystickGetButton(joystick, j)<<j);

			// query all axes, already in correct range
			const int numAxes = core::min_(SDL_JoystickNumAxes(joystick), (int)SEvent::SJoystickEvent::NUMBER_OF_AXES);
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_X]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_Y]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_Z]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_R]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_U]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_V]=0;
			for (j=0; j<numAxes; ++j)
				joyevent.JoystickEvent.Axis[j] = SDL_JoystickGetAxis(joystick, j);

			// we can only query one hat, SDL only supports 8 directions
			if (SDL_JoystickNumHats(joystick)>0)
			{
				switch (SDL_JoystickGetHat(joystick, 0))
				{
					case SDL_HAT_UP:
						joyevent.JoystickEvent.POV=0;
						break;
					case SDL_HAT_RIGHTUP:
						joyevent.JoystickEvent.POV=4500;
						break;
					case SDL_HAT_RIGHT:
						joyevent.JoystickEvent.POV=9000;
						break;
					case SDL_HAT_RIGHTDOWN:
						joyevent.JoystickEvent.POV=13500;
						break;
					case SDL_HAT_DOWN:
						joyevent.JoystickEvent.POV=18000;
						break;
					case SDL_HAT_LEFTDOWN:
						joyevent.JoystickEvent.POV=22500;
						break;
					case SDL_HAT_LEFT:
						joyevent.JoystickEvent.POV=27000;
						break;
					case SDL_HAT_LEFTUP:
						joyevent.JoystickEvent.POV=31500;
						break;
					case SDL_HAT_CENTERED:
					default:
						joyevent.JoystickEvent.POV=65535;
						break;
				}
			}
			else
			{
				joyevent.JoystickEvent.POV=65535;
			}

			// we map the number directly
			joyevent.JoystickEvent.Joystick=static_cast<u8>(i);
			// now post the event
			postEventFromUser(joyevent);
			// and close the joystick
		}
	}
#endif
	return !Close;
}

//! Activate any joysticks, and generate events for them.
bool CIrrDeviceSDL::activateJoysticks(core::array<SJoystickInfo> & joystickInfo)
{
#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
	joystickInfo.clear();

	// we can name up to 256 different joysticks
	const int numJoysticks = core::min_(SDL_NumJoysticks(), 256);
	Joysticks.reallocate(numJoysticks);
	joystickInfo.reallocate(numJoysticks);

	int joystick = 0;
	for (; joystick<numJoysticks; ++joystick)
	{
		Joysticks.push_back(SDL_JoystickOpen(joystick));
		SJoystickInfo info;

		info.Joystick = joystick;
		info.Axes = SDL_JoystickNumAxes(Joysticks[joystick]);
		info.Buttons = SDL_JoystickNumButtons(Joysticks[joystick]);
		info.Name = SDL_JoystickNameForIndex(joystick);
		info.PovHat = (SDL_JoystickNumHats(Joysticks[joystick]) > 0)
						? SJoystickInfo::POV_HAT_PRESENT : SJoystickInfo::POV_HAT_ABSENT;

		joystickInfo.push_back(info);
	}

	for(joystick = 0; joystick < (int)joystickInfo.size(); ++joystick)
	{
		char logString[256];
		(void)sprintf(logString, "Found joystick %d, %d axes, %d buttons '%s'",
		joystick, joystickInfo[joystick].Axes,
		joystickInfo[joystick].Buttons, joystickInfo[joystick].Name.c_str());
		os::Printer::log(logString, ELL_INFORMATION);
	}

	return true;

#endif // _IRR_COMPILE_WITH_JOYSTICK_EVENTS_

	return false;
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
	size_t length = wcslen(text);
	char* textc = new char[length * sizeof(wchar_t) + 1]();
	irr::core::wcharToUtf8(text, textc, length * sizeof(wchar_t) + 1);

	SDL_SetWindowTitle(Window, textc);

	delete[] textc;
}


//! presents a surface in the client area
bool CIrrDeviceSDL::present(video::IImage* surface, void* windowId, core::rect<s32>* srcClip)
{
	SDL_Surface *sdlSurface = SDL_CreateRGBSurfaceFrom(
			surface->getData(), surface->getDimension().Width, surface->getDimension().Height,
			surface->getBitsPerPixel(), surface->getPitch(),
			surface->getRedMask(), surface->getGreenMask(), surface->getBlueMask(), surface->getAlphaMask());
	if (!sdlSurface)
		return false;
	SDL_SetSurfaceAlphaMod(sdlSurface, 0);
	SDL_SetColorKey(sdlSurface, 0, 0);
	sdlSurface->format->BitsPerPixel=surface->getBitsPerPixel();
	sdlSurface->format->BytesPerPixel=surface->getBytesPerPixel();
	if ((surface->getColorFormat()==video::ECF_R8G8B8) ||
			(surface->getColorFormat()==video::ECF_A8R8G8B8))
	{
		sdlSurface->format->Rloss=0;
		sdlSurface->format->Gloss=0;
		sdlSurface->format->Bloss=0;
		sdlSurface->format->Rshift=16;
		sdlSurface->format->Gshift=8;
		sdlSurface->format->Bshift=0;
		if (surface->getColorFormat()==video::ECF_R8G8B8)
		{
			sdlSurface->format->Aloss=8;
			sdlSurface->format->Ashift=32;
		}
		else
		{
			sdlSurface->format->Aloss=0;
			sdlSurface->format->Ashift=24;
		}
	}
	else if (surface->getColorFormat()==video::ECF_R5G6B5)
	{
		sdlSurface->format->Rloss=3;
		sdlSurface->format->Gloss=2;
		sdlSurface->format->Bloss=3;
		sdlSurface->format->Aloss=8;
		sdlSurface->format->Rshift=11;
		sdlSurface->format->Gshift=5;
		sdlSurface->format->Bshift=0;
		sdlSurface->format->Ashift=16;
	}
	else if (surface->getColorFormat()==video::ECF_A1R5G5B5)
	{
		sdlSurface->format->Rloss=3;
		sdlSurface->format->Gloss=3;
		sdlSurface->format->Bloss=3;
		sdlSurface->format->Aloss=7;
		sdlSurface->format->Rshift=10;
		sdlSurface->format->Gshift=5;
		sdlSurface->format->Bshift=0;
		sdlSurface->format->Ashift=15;
	}

	SDL_Surface* scr = (SDL_Surface*)windowId;
	if (scr)
	{
		if (srcClip)
		{
			SDL_Rect sdlsrcClip;
			sdlsrcClip.x = srcClip->UpperLeftCorner.X;
			sdlsrcClip.y = srcClip->UpperLeftCorner.Y;
			sdlsrcClip.w = srcClip->getWidth();
			sdlsrcClip.h = srcClip->getHeight();
			SDL_BlitSurface(sdlSurface, &sdlsrcClip, scr, NULL);
		}
		else
			SDL_BlitSurface(sdlSurface, NULL, scr, NULL);
	}

	SDL_FreeSurface(sdlSurface);
	return (scr != 0);
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
		if ((display_count = SDL_GetNumVideoDisplays()) < 1)
		{
			os::Printer::log("No display created: ", SDL_GetError(), ELL_ERROR);
			return VideoModeList;
		}

		int mode_count = 0;
		if ((mode_count = SDL_GetNumDisplayModes(0)) < 1)
		{
			os::Printer::log("No display modes available: ", SDL_GetError(), ELL_ERROR);
			return VideoModeList;
		}

		SDL_DisplayMode mode = { SDL_PIXELFORMAT_UNKNOWN, 0, 0, 0, 0 };
		if (SDL_GetDesktopDisplayMode(0, &mode) == 0)
		{
			VideoModeList->setDesktop(SDL_BITSPERPIXEL(mode.format),
				core::dimension2d<u32>(mode.w, mode.h));
		}

		for (int i = 0; i < mode_count; i++)
		{
			if (SDL_GetDisplayMode(0, i, &mode) == 0)
			{
				VideoModeList->addMode(core::dimension2d<u32>(mode.w, mode.h),
					SDL_BITSPERPIXEL(mode.format));
			}
		}
	}

	return VideoModeList;
}

//! Sets if the window should be resizable in windowed mode.
void CIrrDeviceSDL::setResizable(bool resize)
{
	if (CreationParams.Fullscreen)
		return;

	SDL_SetWindowResizable(Window, resize ? SDL_TRUE : SDL_FALSE);
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

bool CIrrDeviceSDL::isFullscreen() const
{
	return CIrrDeviceStub::isFullscreen();
}


//! returns if window is active. if not, nothing need to be drawn
bool CIrrDeviceSDL::isWindowActive() const
{
	return (WindowHasFocus && !WindowMinimized);
}


//! returns if window has focus.
bool CIrrDeviceSDL::isWindowFocused() const
{
	return WindowHasFocus;
}


//! returns if window is minimized.
bool CIrrDeviceSDL::isWindowMinimized() const
{
	return WindowMinimized;
}


//! Set the current Gamma Value for the Display
bool CIrrDeviceSDL::setGammaRamp( f32 red, f32 green, f32 blue, f32 brightness, f32 contrast )
{
	/*
	// todo: Gamma in SDL takes ints, what does Irrlicht use?
	return (SDL_SetGamma(red, green, blue) != -1);
	*/
	return false;
}

//! Get the current Gamma Value for the Display
bool CIrrDeviceSDL::getGammaRamp( f32 &red, f32 &green, f32 &blue, f32 &brightness, f32 &contrast )
{
/*	brightness = 0.f;
	contrast = 0.f;
	return (SDL_GetGamma(&red, &green, &blue) != -1);*/
	return false;
}

//! gets text from the clipboard
//! \return Returns empty string on failure.
const c8* CIrrDeviceSDL::getTextFromClipboard() const
{
	return SDL_GetClipboardText();
}

//! copies text to the clipboard
void CIrrDeviceSDL::copyToClipboard(const c8* text) const
{
	SDL_SetClipboardText(text);
}

//! returns color format of the window.
video::ECOLOR_FORMAT CIrrDeviceSDL::getColorFormat() const
{
	if (Window)
	{
		u32 pixel_format = SDL_GetWindowPixelFormat(Window);
		if (SDL_BITSPERPIXEL(pixel_format) == 16)
		{
			if (SDL_ISPIXELFORMAT_ALPHA(pixel_format))
				return video::ECF_A1R5G5B5;
			else
				return video::ECF_R5G6B5;
		}
		else
		{
			if (SDL_ISPIXELFORMAT_ALPHA(pixel_format))
				return video::ECF_A8R8G8B8;
			else
				return video::ECF_R8G8B8;
		}
	}
	else
		return CIrrDeviceStub::getColorFormat();
}


void CIrrDeviceSDL::createKeyMap()
{
	KeyMap.reallocate(136);

	KeyMap.push_back(SKeyMap(SDL_SCANCODE_BACKSPACE, KEY_BACK));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_TAB, KEY_TAB));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_CLEAR, KEY_CLEAR));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_RETURN, KEY_RETURN));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_PAUSE, KEY_PAUSE));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_CAPSLOCK, KEY_CAPITAL));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_ESCAPE, KEY_ESCAPE));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_SPACE, KEY_SPACE));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_PAGEUP, KEY_PRIOR));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_PAGEDOWN, KEY_NEXT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_END, KEY_END));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_HOME, KEY_HOME));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_LEFT, KEY_LEFT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_UP, KEY_UP));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_RIGHT, KEY_RIGHT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_DOWN, KEY_DOWN));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_SELECT, KEY_SELECT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_PRINTSCREEN, KEY_PRINT)); // KEY_SNAPSHOT?
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_EXECUTE, KEY_EXECUT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_INSERT, KEY_INSERT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_DELETE, KEY_DELETE));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_HELP, KEY_HELP));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_0, KEY_KEY_0));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_1, KEY_KEY_1));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_2, KEY_KEY_2));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_3, KEY_KEY_3));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_4, KEY_KEY_4));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_5, KEY_KEY_5));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_6, KEY_KEY_6));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_7, KEY_KEY_7));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_8, KEY_KEY_8));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_9, KEY_KEY_9));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_A, KEY_KEY_A));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_B, KEY_KEY_B));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_C, KEY_KEY_C));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_D, KEY_KEY_D));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_E, KEY_KEY_E));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F, KEY_KEY_F));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_G, KEY_KEY_G));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_H, KEY_KEY_H));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_I, KEY_KEY_I));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_J, KEY_KEY_J));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_K, KEY_KEY_K));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_L, KEY_KEY_L));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_M, KEY_KEY_M));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_N, KEY_KEY_N));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_O, KEY_KEY_O));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_P, KEY_KEY_P));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_Q, KEY_KEY_Q));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_R, KEY_KEY_R));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_S, KEY_KEY_S));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_T, KEY_KEY_T));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_U, KEY_KEY_U));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_V, KEY_KEY_V));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_W, KEY_KEY_W));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_X, KEY_KEY_X));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_Y, KEY_KEY_Y));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_Z, KEY_KEY_Z));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_LGUI, KEY_LWIN));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_RGUI, KEY_RWIN));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_APPLICATION, KEY_APPS));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_POWER, KEY_SLEEP));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_SLEEP, KEY_SLEEP));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_0, KEY_NUMPAD0));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_1, KEY_NUMPAD1));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_2, KEY_NUMPAD2));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_3, KEY_NUMPAD3));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_4, KEY_NUMPAD4));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_5, KEY_NUMPAD5));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_6, KEY_NUMPAD6));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_7, KEY_NUMPAD7));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_8, KEY_NUMPAD8));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_9, KEY_NUMPAD9));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_MULTIPLY, KEY_MULTIPLY));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_PERIOD, KEY_PERIOD));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_DECIMAL, KEY_DECIMAL));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_PLUS, KEY_ADD));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_MINUS, KEY_SUBTRACT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_DIVIDE, KEY_DIVIDE));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_KP_ENTER, KEY_RETURN));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_SEPARATOR, KEY_SEPARATOR));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_PERIOD, KEY_PERIOD));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F1,  KEY_F1));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F2,  KEY_F2));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F3,  KEY_F3));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F4,  KEY_F4));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F5,  KEY_F5));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F6,  KEY_F6));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F7,  KEY_F7));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F8,  KEY_F8));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F9,  KEY_F9));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F10, KEY_F10));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F11, KEY_F11));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F12, KEY_F12));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F13, KEY_F13));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F14, KEY_F14));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F15, KEY_F15));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F16, KEY_F16));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F17, KEY_F17));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F18, KEY_F18));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F19, KEY_F19));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F20, KEY_F20));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F21, KEY_F21));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F22, KEY_F22));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F23, KEY_F23));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_F24, KEY_F24));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_NUMLOCKCLEAR, KEY_NUMLOCK));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_SCROLLLOCK, KEY_SCROLL));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_LSHIFT, KEY_LSHIFT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_RSHIFT, KEY_RSHIFT));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_LCTRL,  KEY_LCONTROL));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_RCTRL,  KEY_RCONTROL));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_LALT,  KEY_LMENU));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_RALT,  KEY_RMENU));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_MENU,  KEY_MENU));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_COMMA,  KEY_COMMA));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_MINUS,  KEY_MINUS));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_AC_BACK, KEY_ESCAPE));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_EQUALS, KEY_PLUS));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_SEMICOLON, KEY_OEM_1));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_SLASH, KEY_OEM_2));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_GRAVE, KEY_OEM_3));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_LEFTBRACKET, KEY_OEM_4));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_BACKSLASH, KEY_OEM_5));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_RIGHTBRACKET, KEY_OEM_6));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_APOSTROPHE, KEY_OEM_7));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_CRSEL, KEY_CRSEL));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_EXSEL, KEY_EXSEL));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_AUDIONEXT, KEY_MEDIA_NEXT_TRACK));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_AUDIOPREV, KEY_MEDIA_PREV_TRACK));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_AUDIOSTOP, KEY_MEDIA_STOP));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_AUDIOPLAY, KEY_MEDIA_PLAY_PAUSE));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_AUDIOMUTE, KEY_VOLUME_MUTE));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_VOLUMEDOWN, KEY_VOLUME_DOWN));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_VOLUMEUP, KEY_VOLUME_UP));
	KeyMap.push_back(SKeyMap(SDL_SCANCODE_WWW, KEY_BROWSER_HOME));

	KeyMap.sort();
}

bool CIrrDeviceSDL::activateAccelerometer(float updateInterval)
{
	if (AccelerometerInstance == -1 && AccelerometerIndex != -1)
	{
		SDL_Sensor* accel = SDL_SensorOpen(AccelerometerIndex);

		if (accel)
			AccelerometerInstance = SDL_SensorGetInstanceID(accel);
	}

	return AccelerometerInstance != -1;
}

bool CIrrDeviceSDL::deactivateAccelerometer()
{
	if (AccelerometerInstance == -1)
		return false;

	SDL_Sensor* accel = SDL_SensorFromInstanceID(AccelerometerInstance);

	if (!accel)
		return false;

	SDL_SensorClose(accel);
	AccelerometerInstance = -1;

	return true;
}

bool CIrrDeviceSDL::isAccelerometerActive()
{
	return AccelerometerInstance != -1;
}

bool CIrrDeviceSDL::isAccelerometerAvailable()
{
	return AccelerometerIndex != -1;
}

bool CIrrDeviceSDL::activateGyroscope(float updateInterval)
{
	if (GyroscopeInstance == -1 && GyroscopeIndex != -1)
	{
		SDL_Sensor* gyro = SDL_SensorOpen(GyroscopeIndex);

		if (gyro)
			GyroscopeInstance = SDL_SensorGetInstanceID(gyro);
	}

	return GyroscopeInstance != -1;
}

bool CIrrDeviceSDL::deactivateGyroscope()
{
	if (GyroscopeInstance == -1)
		return false;

	SDL_Sensor* gyro = SDL_SensorFromInstanceID(GyroscopeInstance);

	if (!gyro)
		return false;

	SDL_SensorClose(gyro);
	GyroscopeInstance = -1;

	return true;
}

bool CIrrDeviceSDL::isGyroscopeActive()
{
	return GyroscopeInstance != -1;
}

bool CIrrDeviceSDL::isGyroscopeAvailable()
{
	return GyroscopeIndex != -1;
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_SDL_DEVICE_

