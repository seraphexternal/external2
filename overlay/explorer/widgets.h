#pragma once
// Ported from jew-dick-hack: src/gui/widgets/{text,input,checkbox}.{h,cpp}
#include "../imgui/imgui.h"
#include <string>

namespace widgets
{
	inline void text_outlined(ImDrawList* draw, ImVec2 pos, ImU32 col, const char* text)
	{
		if (!draw || !text)
			return;

		ImU32 outline = IM_COL32(0, 0, 0, 220);
		const float o = 1.f;
		draw->AddText(ImVec2(pos.x - o, pos.y), outline, text);
		draw->AddText(ImVec2(pos.x + o, pos.y), outline, text);
		draw->AddText(ImVec2(pos.x, pos.y - o), outline, text);
		draw->AddText(ImVec2(pos.x, pos.y + o), outline, text);
		draw->AddText(pos, col, text);
	}

	bool input_text(const char* label, char* buf, int buf_size);
	bool checkbox(const char* label, bool* value);
}
