#include "widgets.h"

#include "../imgui/imgui.h"

namespace widgets
{
	bool input_text(const char* label, char* buf, int buf_size)
	{
		ImGui::PushID(label);

		float width = ImGui::CalcItemWidth();
		if (width < 1.f)
			width = ImGui::GetContentRegionAvail().x;

		constexpr float label_gap = 6.f;
		constexpr float pad_x = 6.f;
		constexpr float pad_y = 3.f;

		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImVec2 label_size = ImGui::CalcTextSize(label);
		float field_h = label_size.y + pad_y * 2.f;
		float row_h = field_h > label_size.y ? field_h : label_size.y;

		float label_w = 0.f;
		bool has_label = label[0] != '#' || label[1] != '#';
		if (has_label)
			label_w = label_size.x + label_gap;

		float field_w = width - label_w;
		if (field_w < 40.f) field_w = 40.f;

		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.10f, 0.10f, 0.10f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.12f, 0.12f, 0.12f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.22f, 0.22f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(1.f, 1.f, 1.f, 0.18f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad_x, pad_y));

		if (has_label)
		{
			text_outlined(ImGui::GetWindowDrawList(), ImVec2(pos.x, pos.y + (row_h - label_size.y) * 0.5f), ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f)), label);
			ImGui::SetCursorScreenPos(ImVec2(pos.x + label_w, pos.y));
		}

		ImGui::SetNextItemWidth(field_w);
		bool changed = ImGui::InputText("##in", buf, (size_t)buf_size);

		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(6);
		ImGui::PopID();
		return changed;
	}

	bool checkbox(const char* label, bool* value)
	{
		constexpr float label_gap = 6.f;

		ImGui::PushID(label);

		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImVec2 text_size = ImGui::CalcTextSize(label);
		float box_size = text_size.y;

		ImGui::InvisibleButton("##cb", ImVec2(box_size + label_gap + text_size.x, box_size));
		bool clicked = ImGui::IsItemClicked();
		if (clicked)
			*value = !*value;
		bool hovered = ImGui::IsItemHovered();

		ImVec2 box_min = pos;
		ImVec2 box_max(box_min.x + box_size, box_min.y + box_size);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		if (*value)
			draw->AddRectFilled(box_min, box_max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f)));
		else
		{
			draw->AddRectFilled(box_min, box_max, ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.35f)));
			if (hovered)
				draw->AddRectFilled(box_min, box_max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.06f)));
		}
		draw->AddRect(box_min, box_max, ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.4f, 1.f)));

		ImU32 text_col = ImGui::GetColorU32(*value ? ImVec4(1.f, 1.f, 1.f, 1.f) : ImVec4(0.55f, 0.55f, 0.55f, 1.f));
		text_outlined(draw, ImVec2(box_max.x + label_gap, pos.y), text_col, label);

		ImGui::PopID();
		return clicked;
	}
}
