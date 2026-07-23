#pragma once

#include "imgui/imgui.h"
#include "imgui/KeyBind.h"
#include "../rbx/globals/options.h"
#include "ui.h"

// Reusable Rage-tab subtab renderers. The same panels also back the Movement
// tab so the UI lives in exactly one place. Autopsy-style: card::begin/end,
// labelsection, UI::Bind, UI::Status, 2-column grid via ContentX + CardW.

inline void RenderRagebotSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(UI::ContentX);
	if (UI::card::begin("##rage_main", ImVec2(UI::CardW, 470 * UI::sc), "RAGE"))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::Rage::Enabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Orbits the target with a ghost 'you' and auto-kills it while you stay free to move.");

		bool rageActive = Options::Rage::Enabled &&
			(Options::Rage::ToggleType == 2 ||
			 (Options::Rage::RageKey != 0 && Options::Rage::Toggled));
		UI::Status(rageActive ? "ACTIVE" : "INACTIVE", rageActive);

		static const char* modes[]{ "Hold", "Toggle", "Always On" };
		UI::Combo("Mode", &Options::Rage::ToggleType, modes, IM_ARRAYSIZE(modes));

		if (Options::Rage::ToggleType != 2)
			UI::Bind("##rage_key", &Options::Rage::RageKey, &Options::Rage::ToggleType);

		if (Options::Rage::ToggleType == 1 && Options::Rage::RageKey != 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, Options::Rage::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.5f) : ImVec4(0.15f, 0.15f, 0.18f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Options::Rage::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.6f) : ImVec4(0.20f, 0.20f, 0.24f, 0.9f));
			if (UI::Button(Options::Rage::Toggled ? "ACTIVE" : "INACTIVE", ImVec2(-1, 24)))
				Options::Rage::Toggled = !Options::Rage::Toggled;
			ImGui::PopStyleColor(2);
		}

		UI::labelsection("FEATURES");
		UI::Checkbox("Kill On Orbit", &Options::Rage::KillOnOrbit);
		UI::Checkbox("Auto Aim At Target", &Options::Rage::AutoKillAim);
		UI::Checkbox("Show Ghost (orbiting you)", &Options::Rage::ShowGhost);
		if (Options::Rage::ShowGhost)
			UI::Checkbox("Show Ghost Line", &Options::Rage::ShowGhostLine);

		UI::labelsection("ORBIT");
		UI::SliderFloat("Orbit Radius", &Options::Rage::OrbitRadius, 1.f, 20.f, "%.1f");
		UI::SliderFloat("Orbit Speed", &Options::Rage::OrbitSpeed, 0.1f, 10.f, "%.1f");

		static const char* targetModes[]{ "Aimed At", "By Username" };
		UI::Combo("Target", &Options::Rage::TargetMode, targetModes, IM_ARRAYSIZE(targetModes));
		if (Options::Rage::TargetMode == 1)
			ImGui::InputText("Username", Options::Rage::TargetPlayer, sizeof(Options::Rage::TargetPlayer));

		ImGui::Dummy(ImVec2(0, 6));
		ImGui::Text("Fire Rate (ms): %d", Options::Ragebot::FireRate);
	}
	UI::card::end();
}

inline void RenderOrbitSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(UI::ContentX);
	if (UI::card::begin("##orbit_main", ImVec2(UI::CardW, 470 * UI::sc), "ORBIT"))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::Orbit::Enabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Circles around a target player automatically.");

		bool orbitActive = Options::Orbit::Enabled &&
			(Options::Orbit::ToggleType == 2 ||
			 (Options::Orbit::OrbitKey != 0 && Options::Orbit::Toggled));
		UI::Status(orbitActive ? "ACTIVE" : "INACTIVE", orbitActive);

		static const char* orbitTargetModes[]{ "Aimed At", "By Username" };
		UI::Combo("Target##orbit", &Options::Orbit::TargetMode, orbitTargetModes, IM_ARRAYSIZE(orbitTargetModes));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Orbit around whoever you aim at, or a specific player name.");
		if (Options::Orbit::TargetMode == 1)
			ImGui::InputText("Username##orbit", Options::Orbit::TargetPlayer, sizeof(Options::Orbit::TargetPlayer));
	}
	UI::card::end();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(UI::ContentX + UI::CardW + 6.0f * UI::sc);
	if (UI::card::begin("##orbit_settings", ImVec2(UI::CardW, 470 * UI::sc), "SETTINGS"))
	{
		UI::labelsection("ORBIT");
		UI::SliderFloat("Orbit Speed", &Options::Orbit::Speed, 0.1f, 10.f, "%.1f");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast you orbit around the target.");
		UI::SliderFloat("Orbit Radius", &Options::Orbit::Radius, 2.f, 50.f, "%.1f");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance in studs from the target while orbiting.");

		UI::labelsection("TOGGLE");
		static const char* orbitModes[]{ "Hold", "Toggle", "Always On" };
		UI::Combo("Mode##orbit", &Options::Orbit::ToggleType, orbitModes, IM_ARRAYSIZE(orbitModes));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold = while key held, Toggle = press once, Always On = always orbit.");

		if (Options::Orbit::ToggleType != 2)
			UI::Bind("##orbit_key", &Options::Orbit::OrbitKey, &Options::Orbit::ToggleType);

		if (Options::Orbit::ToggleType == 1 && Options::Orbit::OrbitKey != 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, Options::Orbit::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.5f) : ImVec4(0.15f, 0.15f, 0.18f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Options::Orbit::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.6f) : ImVec4(0.20f, 0.20f, 0.24f, 0.9f));
			if (UI::Button(Options::Orbit::Toggled ? "ACTIVE" : "INACTIVE", ImVec2(-1, 24)))
				Options::Orbit::Toggled = !Options::Orbit::Toggled;
			ImGui::PopStyleColor(2);
		}
	}
	UI::card::end();
}

inline void RenderAntiAimSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(UI::ContentX);
	if (UI::card::begin("##antiaim_main", ImVec2(UI::CardW, 470 * UI::sc), "ANTI-AIM"))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::AntiAim::Enabled);

		static const char* aaModes[]{ "Spin", "Jitter", "Random" };
		UI::Combo("Mode", &Options::AntiAim::Mode, aaModes, IM_ARRAYSIZE(aaModes));
	}
	UI::card::end();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(UI::ContentX + UI::CardW + 6.0f * UI::sc);
	if (UI::card::begin("##antiaim_settings", ImVec2(UI::CardW, 470 * UI::sc), "SETTINGS"))
	{
		UI::labelsection("PARAMETERS");
		UI::SliderFloat("Speed", &Options::AntiAim::Speed, 1.0f, 50.0f, "%.1f");
		UI::SliderFloat("Strength", &Options::AntiAim::Strength, 5.0f, 180.0f, "%.0f");
	}
	UI::card::end();
}

inline void RenderDesyncSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(UI::ContentX);
	if (UI::card::begin("##desync_main", ImVec2(UI::CardW, 470 * UI::sc), "DESYNC"))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::Desync::Enabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Desynchronises your client position from the server.");

		if (Options::Desync::Enabled)
		{
			bool desyncActive = Options::Desync::Enabled &&
				(Options::Desync::ToggleType == 2 ||
				 (Options::Desync::DesyncKey != 0 && Options::Desync::Toggled));
			UI::Status(desyncActive ? "ACTIVE" : "INACTIVE", desyncActive);
		}

		static const char* desyncModes[]{ "Hold", "Toggle", "Always On" };
		UI::Combo("Mode##desync", &Options::Desync::ToggleType, desyncModes, IM_ARRAYSIZE(desyncModes));

		if (Options::Desync::ToggleType != 2)
			UI::Bind("##desync_key", &Options::Desync::DesyncKey, &Options::Desync::ToggleType);

		if (Options::Desync::ToggleType == 1 && Options::Desync::DesyncKey != 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, Options::Desync::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.5f) : ImVec4(0.15f, 0.15f, 0.18f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Options::Desync::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.6f) : ImVec4(0.20f, 0.20f, 0.24f, 0.9f));
			if (UI::Button(Options::Desync::Toggled ? "ACTIVE" : "INACTIVE", ImVec2(-1, 24)))
				Options::Desync::Toggled = !Options::Desync::Toggled;
			ImGui::PopStyleColor(2);
		}
	}
	UI::card::end();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(UI::ContentX + UI::CardW + 6.0f * UI::sc);
	if (UI::card::begin("##desync_settings", ImVec2(UI::CardW, 470 * UI::sc), "SETTINGS"))
	{
		UI::labelsection("METHOD");
		static const char* methodNames[]{ "Freeze Server", "Velocity Boost" };
		UI::Combo("Method##desync", &Options::Desync::Method, methodNames, IM_ARRAYSIZE(methodNames));

		if (Options::Desync::Method == 1)
		{
			UI::SliderFloat("Boost Speed", &Options::Desync::BoostSpeed, 10.f, 500.f, "%.0f");
			static const char* axisNames[]{ "Forward", "Up", "Backward" };
			UI::Combo("Direction##desync", &Options::Desync::BoostAxis, axisNames, IM_ARRAYSIZE(axisNames));
		}

		UI::labelsection("VISUALS");
		UI::Checkbox("Show Ghost", &Options::Desync::ShowVisual);
		if (Options::Desync::ShowVisual)
		{
			UI::ColorEdit3("Ghost Color", Options::Desync::VisualColor, ImGuiColorEditFlags_NoInputs);
			UI::SliderFloat("Ghost Alpha", &Options::Desync::VisualAlpha, 0.1f, 1.0f, "%.2f");
			UI::Checkbox("Show Line", &Options::Desync::ShowLine);
			if (Options::Desync::ShowLine)
				UI::ColorEdit3("Line Color", Options::Desync::LineColor, ImGuiColorEditFlags_NoInputs);
		}
	}
	UI::card::end();
}

inline void RenderVoidHideSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(UI::ContentX);
	if (UI::card::begin("##voidhide_main", ImVec2(UI::CardW, 470 * UI::sc), "VOIDHIDE"))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::VoidHide::Enabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically hides you when falling into the void.");

		bool voidHideActive = Options::VoidHide::Enabled &&
			(Options::VoidHide::ToggleType == 2 ||
			 (Options::VoidHide::VoidHideKey != 0 && Options::VoidHide::Toggled));
		UI::Status(voidHideActive ? "ACTIVE" : "INACTIVE", voidHideActive);
	}
	UI::card::end();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(UI::ContentX + UI::CardW + 6.0f * UI::sc);
	if (UI::card::begin("##voidhide_settings", ImVec2(UI::CardW, 470 * UI::sc), "SETTINGS"))
	{
		UI::labelsection("TOGGLE");
		static const char* voidHideModes[]{ "Hold", "Toggle", "Always On" };
		UI::Combo("Mode##voidhide", &Options::VoidHide::ToggleType, voidHideModes, IM_ARRAYSIZE(voidHideModes));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold = while key held, Toggle = press once, Always On = always active.");

		if (Options::VoidHide::ToggleType != 2)
			UI::Bind("##voidhide_key", &Options::VoidHide::VoidHideKey, &Options::VoidHide::ToggleType);

		if (Options::VoidHide::ToggleType == 1 && Options::VoidHide::VoidHideKey != 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, Options::VoidHide::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.5f) : ImVec4(0.15f, 0.15f, 0.18f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Options::VoidHide::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.6f) : ImVec4(0.20f, 0.20f, 0.24f, 0.9f));
			if (UI::Button(Options::VoidHide::Toggled ? "ACTIVE" : "INACTIVE", ImVec2(-1, 24)))
				Options::VoidHide::Toggled = !Options::VoidHide::Toggled;
			ImGui::PopStyleColor(2);
		}
	}
	UI::card::end();
}

inline void RenderBhopSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(UI::ContentX);
	if (UI::card::begin("##bhop_main", ImVec2(UI::CardW, 470 * UI::sc), "BHOP"))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::Bhop::Enabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically jumps when you touch the ground while holding the key.");
	}
	UI::card::end();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(UI::ContentX + UI::CardW + 6.0f * UI::sc);
	if (UI::card::begin("##bhop_settings", ImVec2(UI::CardW, 470 * UI::sc), "SETTINGS"))
	{
		UI::labelsection("KEYBIND");
		UI::Bind("##bhop_key", &Options::Bhop::BhopKey);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold this key while moving to bunny hop.");

		bool bhopActive = Options::Bhop::Enabled && Options::Bhop::BhopKey != 0 &&
			(GetAsyncKeyState(Options::Bhop::BhopKey) & 0x8000) != 0;
		UI::Status(bhopActive ? "ACTIVE" : "INACTIVE", bhopActive);
	}
	UI::card::end();
}
