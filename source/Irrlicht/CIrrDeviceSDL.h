// Copyright (C) 2002-2012 Nikolaus Gebhardt
// Copyright (C) 2022 Dawid Gan
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h
// This device code is based on the original SDL device implementation
// contributed by Shane Parker (sirshane).

#ifndef IRR_C_IRR_DEVICE_SDL_H_INCLUDED
#define IRR_C_IRR_DEVICE_SDL_H_INCLUDED

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_

#include "IrrlichtDevice.h"
#include "CIrrDeviceStub.h"
#include "IImagePresenter.h"
#include "ICursorControl.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

//! Set for the drivers that draw into a CAMetalLayer the device owns
#if defined(_IRR_COMPILE_WITH_ANGLE_)
#define IRR_SDL_METAL_VIEW
#endif

#include <array>
#include <set>

namespace irr
{
	class CIrrDeviceSDL : public CIrrDeviceStub, video::IImagePresenter
	{
	public:

		//! constructor
		CIrrDeviceSDL(const SIrrlichtCreationParameters& param);

		//! destructor
		virtual ~CIrrDeviceSDL();

		//! runs the device. Returns false if device wants to be deleted
		virtual bool run() IRR_OVERRIDE;

		//! pause execution temporarily
		virtual void yield() IRR_OVERRIDE;

		//! pause execution for a specified time
		virtual void sleep(u32 timeMs, bool pauseTimer) IRR_OVERRIDE;

#if defined(_IRR_OSX_PLATFORM_)
		bool paceFrames(f32 fps) IRR_OVERRIDE;
#endif

#if defined(_IRR_ANDROID_PLATFORM_) || defined(_IRR_IOS_PLATFORM_)
		void setFrameRate(f32 fps) IRR_OVERRIDE;
#endif

		//! sets the caption of the window
		virtual void setWindowCaption(const wchar_t* text) IRR_OVERRIDE;

		//! returns if window is active. if not, nothing need to be drawn
		virtual bool isWindowActive() const IRR_OVERRIDE;

		//! returns if window has focus.
		bool isWindowFocused() const IRR_OVERRIDE;

		//! returns if window is minimized.
		bool isWindowMinimized() const IRR_OVERRIDE;

		//! returns color format of the window.
		video::ECOLOR_FORMAT getColorFormat() const IRR_OVERRIDE;

		//! presents a surface in the client area
		virtual bool present(video::IImage* surface, void* windowId=0, core::rect<s32>* src=0) IRR_OVERRIDE;

		//! notifies the device that it should close itself
		virtual void closeDevice() IRR_OVERRIDE;

		//! \return Returns a pointer to a list with all video modes supported
		virtual video::IVideoModeList* getVideoModeList() IRR_OVERRIDE;

		//! Sets if the window should be resizable in windowed mode.
		virtual void setResizable(bool resize=false) IRR_OVERRIDE;

		//! Minimizes the window.
		virtual void minimizeWindow() IRR_OVERRIDE;

		//! Maximizes the window.
		virtual void maximizeWindow() IRR_OVERRIDE;

		//! Restores the window size.
		virtual void restoreWindow() IRR_OVERRIDE;

		//! Get the position of this window on screen
		virtual core::position2di getWindowPosition() IRR_OVERRIDE;

		//! Activate any joysticks, and generate events for them.
		virtual bool activateJoysticks(core::array<SJoystickInfo> & joystickInfo) IRR_OVERRIDE;

		//! gets text from the clipboard
		//! \return Returns empty string on failure.
		virtual const c8* getTextFromClipboard() const;

		//! copies text to the clipboard
		virtual void copyToClipboard(const c8* text) const;

		//! Get the device type
		virtual E_DEVICE_TYPE getType() const IRR_OVERRIDE
		{
			return EIDT_SDL;
		}

#if 0 // The game reads no accelerometer or gyroscope
		virtual bool activateAccelerometer(float updateInterval) IRR_OVERRIDE;

		virtual bool deactivateAccelerometer() IRR_OVERRIDE;

		virtual bool isAccelerometerActive() IRR_OVERRIDE;

		virtual bool isAccelerometerAvailable() IRR_OVERRIDE;

		virtual bool activateGyroscope(float updateInterval) IRR_OVERRIDE;

		virtual bool deactivateGyroscope() IRR_OVERRIDE;

		virtual bool isGyroscopeActive() IRR_OVERRIDE;

		virtual bool isGyroscopeAvailable() IRR_OVERRIDE;
#endif

		SDL_Window* getWindow() const { return Window; }

		SDL_GLContext getContext() const { return Context; }

		f32 getNativeScaleX() { return NativeScaleX; }

		f32 getNativeScaleY() { return NativeScaleY; }

		//! Implementation of the linux cursor control
		class CCursorControl : public gui::ICursorControl
		{
		public:

			CCursorControl(CIrrDeviceSDL* dev)
				: Device(dev), IsVisible(true), ActiveIcon(gui::ECI_NORMAL)
			{
				initCursors();
			}

			~CCursorControl();

			//! Changes the visible state of the mouse cursor.
			virtual void setVisible(bool visible) IRR_OVERRIDE
			{
				Device->setCursorVisible(visible);
				IsVisible = visible;
			}

			//! Returns if the cursor is currently visible.
			virtual bool isVisible() const IRR_OVERRIDE
			{
				return IsVisible;
			}

			//! Sets the new position of the cursor.
			virtual void setPosition(const core::position2d<f32> &pos) IRR_OVERRIDE
			{
				setPosition(pos.X, pos.Y);
			}

			//! Sets the new position of the cursor.
			virtual void setPosition(f32 x, f32 y) IRR_OVERRIDE
			{
				setPosition((s32)(x*Device->Width), (s32)(y*Device->Height));
			}

			//! Sets the new position of the cursor.
			virtual void setPosition(const core::position2d<s32> &pos) IRR_OVERRIDE
			{
				setPosition(pos.X, pos.Y);
			}

			//! Sets the new position of the cursor.
			virtual void setPosition(s32 x, s32 y) IRR_OVERRIDE
			{
#if !defined(_IRR_ANDROID_PLATFORM_) && !defined(_IRR_IOS_PLATFORM_)
				if (!SDL_GetWindowRelativeMouseMode(Device->Window) &&
						!Device->SimulateTouchEvents)
				{
					SDL_WarpMouseInWindow(Device->Window,
						(float)x / Device->getNativeScaleX(),
						(float)y / Device->getNativeScaleY());

#if defined(_IRR_OSX_PLATFORM_)
					Device->IgnoreWarpMouseEvent = true;
#endif
				}
#endif

				Device->MouseX = x;
				Device->MouseY = y;

				CursorPos.X = x;
				CursorPos.Y = y;
			}

			//! Returns the current position of the mouse cursor.
			virtual const core::position2d<s32>& getPosition(bool updateCursor) IRR_OVERRIDE
			{
				if ( updateCursor )
					updateCursorPos();
				return CursorPos;
			}

			//! Returns the current position of the mouse cursor.
			virtual core::position2d<f32> getRelativePosition(bool updateCursor) IRR_OVERRIDE
			{
				if ( updateCursor )
					updateCursorPos();
				return core::position2d<f32>(CursorPos.X / (f32)Device->Width,
					CursorPos.Y / (f32)Device->Height);
			}

			virtual void setReferenceRect(core::rect<s32>* rect=0) IRR_OVERRIDE
			{
			}

			virtual bool getReferenceRect(core::rect<s32>& rect) IRR_OVERRIDE
			{
				rect.UpperLeftCorner = core::vector2di(0,0);
				rect.LowerRightCorner.X = (irr::s32)Device->Width;
				rect.LowerRightCorner.Y = (irr::s32)Device->Height;
				return false;
			}

			virtual void setActiveIcon(gui::ECURSOR_ICON iconId) IRR_OVERRIDE;

			virtual gui::ECURSOR_ICON getActiveIcon() const IRR_OVERRIDE
			{
				return ActiveIcon;
			}

		private:

			void updateCursorPos()
			{
				CursorPos.X = core::clamp(Device->MouseX, 0, (s32)Device->Width);
				CursorPos.Y = core::clamp(Device->MouseY, 0, (s32)Device->Height);
			}

			void initCursors();

			CIrrDeviceSDL* Device;
			core::position2d<s32> CursorPos;
			bool IsVisible;
			std::array<SDL_Cursor*, gui::ECI_COUNT> Cursors{};
			gui::ECURSOR_ICON ActiveIcon;
		};

	private:
		void createDriver();

		bool createWindow();

		bool createWindowWithContext();

		void updateNativeScaleFromSystem();

		void updateNativeScale();

		void setCursorVisible(bool visible);

		bool supportsRelativeMouse();

		SDL_Window* Window = 0;
		SDL_GLContext Context = 0;
#ifdef IRR_SDL_METAL_VIEW
		SDL_MetalView MetalView = 0;
#endif

	public:
		//! What a driver that talks to the platform directly has to draw into: the layer
		//! this device made on Apple, and the window the system owns everywhere else
		void* getDrawTarget() const
		{
#if defined(IRR_SDL_METAL_VIEW)
			return MetalView ? SDL_Metal_GetLayer(MetalView) : 0;
#elif defined(_IRR_ANDROID_PLATFORM_)
			return Window ? SDL_GetPointerProperty(SDL_GetWindowProperties(Window),
					SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, 0) : 0;
#else
			return 0;
#endif
		}

		//! Lets SDL take in what the system sent, as an Android pause, without handing out events or waiting for a resume
		void pumpSystemEvents() { SDL_PumpEvents(); }

		//! Lets SDL take in what the system sent and, while the app is in the background, waits for it to come back,
		//! without handing out events; true when a GL context is current again
		bool waitForForeground() { SDL_PollEvent(0); return SDL_GL_GetCurrentContext() != 0; }

	private:
#if defined(_IRR_COMPILE_WITH_ANGLE_)
		video::IContextManager* ContextManager = 0;
#endif
		core::array<SDL_JoystickID> Joysticks;

		mutable core::stringc ClipboardText;
		s32 MouseX = 0, MouseY = 0;
		u32 MouseButtonStates = 0;
		bool IgnoreWarpMouseEvent = false;

		u32 Width, Height;

		bool Resizable;

#if defined(_IRR_OSX_PLATFORM_)
		//! When the frame the device sleeps for ends, in microseconds
		u64 FrameTarget = 0;
#endif

#if 0
		SDL_SensorID AccelerometerIndex;
		SDL_SensorID AccelerometerInstance;
		SDL_SensorID GyroscopeIndex;
		SDL_SensorID GyroscopeInstance;
#endif

		f32 NativeScaleX, NativeScaleY;

		u32 LongTouchTimer;
		s32 LongTouchX;
		s32 LongTouchY;
		bool LongTouchHandled;

		std::set<SDL_FingerID> TouchIDs;

		inline static int SDLDeviceInstances = 0;
		inline static bool SimulateTouchEvents = false;
		inline static bool RelativeMouseAvailable = false;
	};

} // end namespace irr

#endif // _IRR_COMPILE_WITH_SDL_DEVICE_
#endif // IRR_C_IRR_DEVICE_SDL_H_INCLUDED
