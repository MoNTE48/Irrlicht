/*
Copyright (C) 2025 Dawid Gan <deveee@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3.0 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef IRR_BIDI_H_INCLUDED
#define IRR_BIDI_H_INCLUDED

#include "irrUString.h"

#include <vector>

namespace irr
{
namespace core
{

struct TextBidiData
{
	core::stringw Text;
	core::stringw TextBidi;
	std::vector<s32> RtlCharPos;
	std::vector<bool> CharIsRtl;

	s32 visualCursorPos(s32 pos);
	s32 logicalCursorPos(s32 pos);
};

TextBidiData applyBidiReordering(const core::stringw& text);
core::ustring applyBidiReordering(const core::ustring& text);
core::ustring applyBidiReorderingMultiline(const core::ustring& text);

} // end namespace core
} // end namespace irr

#endif
