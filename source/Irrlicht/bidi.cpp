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

#include "bidi.h"

#include <SheenBidi.h>

namespace irr {

namespace core {

s32 TextBidiData::visualCursorPos(s32 pos)
{
	if (TextBidi.size() == 0)
		return 0;

	if (TextBidi.size() > 0 && pos >= (s32)TextBidi.size()) {
		if (CharIsRtl[0])
			return 0;
		else
			return TextBidi.size();
	}
	
	if (pos >= 0 && pos < (s32)RtlCharPos.size()) {
		if (CharIsRtl[pos])
			return RtlCharPos[pos] + 1;
		else
			return RtlCharPos[pos];
	}
	
	return pos;
}

s32 TextBidiData::logicalCursorPos(s32 pos)
{
	if (pos < 0) 
		return TextBidi.size();

	if (TextBidi.size() > 0 && pos >= (s32)TextBidi.size()) {
		if (CharIsRtl[0])
			return 0;
		else
			return TextBidi.size();
	}
		
	for (u32 i = 0; i < RtlCharPos.size(); i++) {
		if (RtlCharPos[i] == pos)
			return i;
	}
	
	return pos;
}

core::ustring applyBidiReorderingMultiline(const core::ustring& text)
{
    if (text.empty())
        return text;

    core::ustring result;
    u32 line_start = 0;
    
    for (u32 i = 0; i <= text.size(); i++) {
        if (i == text.size() || text[i] == L'\n' || text[i] == L'\r') {
            if (i > line_start) {
                core::ustring line = text.subString(line_start, i - line_start);
                result += applyBidiReordering(line);
            }
            
            if (i < text.size()) {
                result += text[i];

                if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
                    i++;
                    result += text[i];
                }
            }
            
            line_start = i + 1;
        }
    }
    
    return result;
}

TextBidiData applyBidiReordering(const core::stringw& text)
{
	TextBidiData data;
	
	if (text.empty())
		return data;
	
	data.Text = text;

	SBCodepointSequence codepointSequence;
	codepointSequence.stringEncoding = SBStringEncodingUTF32;
	codepointSequence.stringBuffer = (void*)text.c_str();
	codepointSequence.stringLength = text.size();
	
	SBAlgorithmRef bidiAlgorithm = SBAlgorithmCreate(&codepointSequence);
	
	if (!bidiAlgorithm)
		return data;

	SBParagraphRef paragraph = SBAlgorithmCreateParagraph(bidiAlgorithm, 0, 
			text.size(), SBLevelDefaultLTR);
	
	if (!paragraph) {
		SBAlgorithmRelease(bidiAlgorithm);
		return data;
	}

	SBLineRef line = SBParagraphCreateLine(paragraph, 0, text.size());
	
	if (!line) {
		SBParagraphRelease(paragraph);
		SBAlgorithmRelease(bidiAlgorithm);
		return data;
	}

	SBUInteger runCount = SBLineGetRunCount(line);
	const SBRun *runsPtr = SBLineGetRunsPtr(line);
	
	data.TextBidi.reserve(text.size());
	data.RtlCharPos.resize(text.size());
	data.CharIsRtl.resize(text.size());
	s32 visualPos = 0;
	
	for (SBUInteger i = 0; i < runCount; i++) {
		const SBRun *run = runsPtr + i;
		bool isRTL = (run->level & 1) != 0;
		
		if (isRTL) {
			for (SBInteger j = run->length - 1; j >= 0; j--) {
				SBUInteger index = run->offset + j;
				data.TextBidi += text[index];
				data.RtlCharPos[index] = visualPos;
				data.CharIsRtl[index] = true;
				visualPos++;
			}
		} else {
			for (SBUInteger j = 0; j < run->length; j++) {
				SBUInteger index = run->offset + j;
				data.TextBidi += text[index];
				data.RtlCharPos[index] = visualPos;
				data.CharIsRtl[index] = false;
				visualPos++;
			}
		}
	}
	
	SBLineRelease(line);
	SBParagraphRelease(paragraph);
	SBAlgorithmRelease(bidiAlgorithm);

	return data;
}

core::ustring applyBidiReordering(const core::ustring& text)
{
	if (text.empty())
		return text;
	
	SBCodepointSequence codepointSequence;
	codepointSequence.stringEncoding = SBStringEncodingUTF16;
	codepointSequence.stringBuffer = (void*)text.c_str();
	codepointSequence.stringLength = text.size();
	
	SBAlgorithmRef bidiAlgorithm = SBAlgorithmCreate(&codepointSequence);
	
	if (!bidiAlgorithm)
		return text;
	
	SBParagraphRef paragraph = SBAlgorithmCreateParagraph(bidiAlgorithm, 0, 
			text.size(), SBLevelDefaultLTR);
	
	if (!paragraph) {
		SBAlgorithmRelease(bidiAlgorithm);
		return text;
	}
	
	SBLineRef line = SBParagraphCreateLine(paragraph, 0, text.size());

	if (!line) {
		SBParagraphRelease(paragraph);
		SBAlgorithmRelease(bidiAlgorithm);
		return text;
	}
	
	SBUInteger runCount = SBLineGetRunCount(line);
	const SBRun *runsPtr = SBLineGetRunsPtr(line);
	
	core::ustring result;
	result.reserve(text.size());
	
	for (SBUInteger i = 0; i < runCount; i++) {
		const SBRun *run = runsPtr + i;
		bool isRTL = (run->level & 1) != 0;
		
		if (isRTL) {
			for (SBInteger j = run->length - 1; j >= 0; j--) {
				SBUInteger index = run->offset + j;
				result += text[index];
			}
		} else {
			for (SBUInteger j = 0; j < run->length; j++) {
				SBUInteger index = run->offset + j;
				result += text[index];
			}
		}
	}
	
	SBLineRelease(line);
	SBParagraphRelease(paragraph);
	SBAlgorithmRelease(bidiAlgorithm);
	
	return result;
}

}

}