// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#ifndef IRR_E_DRIVER_TYPES_H_INCLUDED
#define IRR_E_DRIVER_TYPES_H_INCLUDED

#include "irrTypes.h"

namespace irr
{
namespace video
{

	//! An enum for all types of drivers the Irrlicht Engine supports.
	enum E_DRIVER_TYPE
	{
		//! Null driver, useful for applications to run the engine without visualization.
		/** The null device is able to load textures, but does not
		render and display any graphics. */
		EDT_NULL,

		// obsolete constants to make some code happy
		// TODO delete
		EDT_SOFTWARE,
		EDT_BURNINGSVIDEO,
		DEPRECATED_EDT_DIRECT3D8_NO_LONGER_EXISTS,
		EDT_DIRECT3D9,

		//! OpenGL device, available on most platforms.
		/** Performs hardware accelerated rendering of 3D and 2D
		primitives. */
		EDT_OPENGL,

		//! OpenGL-ES 2.x driver, for embedded and mobile systems
		/** Supports shaders etc. */
		EDT_OGLES2,

		//! WebGL1 friendly subset of OpenGL-ES 2.x driver for Emscripten
		EDT_WEBGL1,

		//! ANGLE device: the OpenGL-ES 2.x path over the platform's native graphics API
		/** Requires _IRR_COMPILE_WITH_ANGLE_ and linking against ANGLE. */
		EDT_ANGLE,

		//! Native Metal device for iOS and macOS, without a translation layer.
		/** Requires _IRR_COMPILE_WITH_METAL_. */
		EDT_METAL,

		//! Native Vulkan device for Android
		/** Requires _IRR_COMPILE_WITH_VULKAN_ */
		EDT_VULKAN,

		//! No driver, just for counting the elements
		EDT_COUNT
	};

} // end namespace video
} // end namespace irr

#endif
