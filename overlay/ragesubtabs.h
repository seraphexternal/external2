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
    ImGui::SetCursorPosX(16.0f * UI::sc);
    if (UI::CollapsibleSection("RAGEBOT", UI::CardW))
    {
        UI::labelsection("MAIN");
        UI::Checkbox("Enabled", &Options::Ragebot::Enabled);
        UI::Tooltip("Camera-based aimbot with multi-point targeting.");

        bool rageActive = Options::Ragebot::Enabled &&
            (Options::Ragebot::ToggleType == 2 ||
             (Options::Ragebot::RagebotKey != 0 && Options::Ragebot::Toggled));
        UI::Status(rageActive ? "ACTIVE" : "INACTIVE", rageActive);

        static const char* modes[]{ "Hold", "Toggle", "Always On" };
        UI::Combo("Mode", &Options::Ragebot::ToggleType, modes, IM_ARRAYSIZE(modes));

        if (Options::Ragebot::ToggleType != 2)
            UI::Bind("##ragebot_key", &Options::Ragebot::RagebotKey, &Options::Ragebot::ToggleType);

        if (Options::Ragebot::ToggleType == 1 && Options::Ragebot::RagebotKey != 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Options::Ragebot::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.5f) : ImVec4(0.15f, 0.15f, 0.18f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Options::Ragebot::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.6f) : ImVec4(0.20f, 0.20f, 0.24f, 0.9f));
            if (UI::Button(Options::Ragebot::Toggled ? "ACTIVE" : "INACTIVE", ImVec2(-1, 24)))
                Options::Ragebot::Toggled = !Options::Ragebot::Toggled;
            ImGui::PopStyleColor(2);
        }

        UI::gap(6);
        UI::labelsection("TARGETING");
        static const char* bones[]{ "Head", "Torso", "LowerTorso", "UpperTorso" };
        UI::Combo("Hitbox", &Options::Ragebot::TargetBone, bones, IM_ARRAYSIZE(bones));

        static const char* priorities[]{ "Crosshair", "Distance", "Hit Chance" };
        UI::Combo("Priority", &Options::Ragebot::TargetPriority, priorities, IM_ARRAYSIZE(priorities));

        UI::Checkbox("Team Check", &Options::Ragebot::TeamCheck);
        UI::Checkbox("Wall Check", &Options::Ragebot::WallCheck);
        UI::Checkbox("Downed Check", &Options::Ragebot::DownedCheck);
        UI::SliderFloat("Range", &Options::Ragebot::Range, 10.f, 500.f, "%.0f");
        UI::SliderFloat("FOV", &Options::Ragebot::FOV, 10.f, 360.f, "%.0f");
        UI::SliderFloat("Smoothness", &Options::Ragebot::Smoothness, 0.0f, 1.0f, "%.2f");
    }
    UI::CollapsibleEnd();

    ImGui::SetCursorPosY(panelY);
    ImGui::SetCursorPosX(16.0f * UI::sc + UI::CardW + 10.0f * UI::sc);
    if (UI::CollapsibleSection("AUTO FIRE", UI::CardW))
    {
        UI::Checkbox("Auto Fire", &Options::Ragebot::AutoFire);
        UI::SliderInt("Fire Rate", &Options::Ragebot::FireRate, 10, 500, "%d ms");

        UI::Checkbox("Double Tap", &Options::Ragebot::DoubleTap);
        if (Options::Ragebot::DoubleTap)
            UI::SliderInt("DT Delay", &Options::Ragebot::DoubleTapDelay, 10, 200, "%d ms");

        UI::gap(6);
        UI::labelsection("HIT CHANCE");
        UI::Checkbox("Enabled", &Options::Ragebot::HitChanceEnabled);
        if (Options::Ragebot::HitChanceEnabled)
            UI::SliderFloat("Min Chance", &Options::Ragebot::MinHitChance, 0.0f, 100.0f, "%.0f%%");

        UI::gap(6);
        UI::labelsection("PREDICTION");
        UI::Checkbox("Predict", &Options::Ragebot::Prediction);
        if (Options::Ragebot::Prediction)
        {
            UI::SliderFloat("X", &Options::Ragebot::PredictionX, 0.0f, 2.0f, "%.2f");
            UI::SliderFloat("Y", &Options::Ragebot::PredictionY, 0.0f, 2.0f, "%.2f");
        }

        UI::gap(6);
        UI::labelsection("DAMAGE");
        UI::SliderFloat("Min Damage", &Options::Ragebot::MinDamage, 1.0f, 100.0f, "%.0f");
    }
    UI::CollapsibleEnd();
}

inline void RenderRageSubtab(ImVec4 main_color)
{
    const float panelY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosX(16.0f * UI::sc);
    if (UI::CollapsibleSection("RAGE (ORBIT KILL)", UI::CardW))
    {
        UI::labelsection("MAIN");
        UI::Checkbox("Enabled", &Options::Rage::Enabled);
        UI::Tooltip("Orbits the target with a ghost 'you' and auto-kills it while you stay free to move.");

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

        UI::gap(6);
        UI::labelsection("KILL SETTINGS");
        UI::Checkbox("Kill On Orbit", &Options::Rage::KillOnOrbit);
        UI::Checkbox("Auto Aim At Target", &Options::Rage::AutoKillAim);
        UI::Checkbox("Show Ghost (orbiting you)", &Options::Rage::ShowGhost);
        if (Options::Rage::ShowGhost)
            UI::Checkbox("Show Ghost Line", &Options::Rage::ShowGhostLine);

        UI::gap(6);
        UI::labelsection("ORBIT CONFIG");
        UI::SliderFloat("Orbit Radius", &Options::Rage::OrbitRadius, 1.f, 20.f, "%.1f");
        UI::SliderFloat("Orbit Speed", &Options::Rage::OrbitSpeed, 0.1f, 10.f, "%.1f");
        UI::SliderFloat("Orbit Height", &Options::Rage::OrbitHeight, -10.f, 10.f, "%.1f");
    }
    UI::CollapsibleEnd();

    ImGui::SetCursorPosY(panelY);
    ImGui::SetCursorPosX(16.0f * UI::sc + UI::CardW + 10.0f * UI::sc);
    if (UI::CollapsibleSection("SETTINGS", UI::CardW))
    {
        UI::labelsection("TARGET");
        static const char* targetModes[]{ "Aimed At", "By Username" };
        UI::Combo("Target", &Options::Rage::TargetMode, targetModes, IM_ARRAYSIZE(targetModes));
        if (Options::Rage::TargetMode == 1)
            ImGui::InputText("Username", Options::Rage::TargetPlayer, sizeof(Options::Rage::TargetPlayer));

        UI::gap(6);
        UI::labelsection("GHOST VISUALS");
        UI::ColorEdit3("Ghost Color", Options::Rage::GhostColor, ImGuiColorEditFlags_NoInputs);
        UI::SliderFloat("Ghost Alpha", &Options::Rage::GhostAlpha, 0.1f, 1.0f, "%.2f");

        UI::gap(6);
        UI::labelsection("PERFORMANCE");
        ImGui::TextColored(UI::P.textMid, "Fire Rate (ms): %d", Options::Ragebot::FireRate);
    }
    UI::CollapsibleEnd();
}

inline void RenderAntiAimSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(16.0f * UI::sc);
	if (UI::CollapsibleSection("ANTI-AIM", UI::CardW))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::AntiAim::Enabled);

		static const char* aaModes[]{ "Spin", "Jitter", "Random" };
		UI::Combo("Mode", &Options::AntiAim::Mode, aaModes, IM_ARRAYSIZE(aaModes));
		
		UI::gap(6);
		UI::labelsection("MODE INFO");
		if (Options::AntiAim::Mode == 0)
			ImGui::TextWrapped("Spin: Continuously rotates your character angles at set speed.");
		else if (Options::AntiAim::Mode == 1)
			ImGui::TextWrapped("Jitter: Rapidly alternates back and forth between angles.");
		else
			ImGui::TextWrapped("Random: Randomizes target angles every tick to throw off aimbots.");
	}
	UI::CollapsibleEnd();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(16.0f * UI::sc + UI::CardW + 10.0f * UI::sc);
	if (UI::CollapsibleSection("SETTINGS", UI::CardW))
	{
		UI::labelsection("PARAMETERS");
		UI::SliderFloat("Speed", &Options::AntiAim::Speed, 1.0f, 50.0f, "%.1f");
		UI::SliderFloat("Strength", &Options::AntiAim::Strength, 5.0f, 180.0f, "%.0f");
	}
	UI::CollapsibleEnd();
}

inline void RenderDesyncSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(16.0f * UI::sc);
	if (UI::CollapsibleSection("DESYNC", UI::CardW))
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
	UI::CollapsibleEnd();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(16.0f * UI::sc + UI::CardW + 10.0f * UI::sc);
	if (UI::CollapsibleSection("SETTINGS", UI::CardW))
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
			UI::Checkbox("Wall Check", &Options::Desync::WallCheck);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide the ghost when your real character is behind a wall.");
		}
	}
	UI::CollapsibleEnd();
}

inline void RenderVoidHideSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(16.0f * UI::sc);
	if (UI::CollapsibleSection("VOIDHIDE", UI::CardW))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::VoidHide::Enabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically hides you when falling into the void.");

		bool voidHideActive = Options::VoidHide::Enabled &&
			(Options::VoidHide::ToggleType == 2 ||
			 (Options::VoidHide::VoidHideKey != 0 && Options::VoidHide::Toggled));
		UI::Status(voidHideActive ? "ACTIVE" : "INACTIVE", voidHideActive);
	}
	UI::CollapsibleEnd();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(16.0f * UI::sc + UI::CardW + 10.0f * UI::sc);
	if (UI::CollapsibleSection("SETTINGS", UI::CardW))
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
	UI::CollapsibleEnd();
}

inline void RenderBhopSubtab(ImVec4 main_color)
{
	const float panelY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX(16.0f * UI::sc);
	if (UI::CollapsibleSection("BHOP", UI::CardW))
	{
		UI::labelsection("MAIN");
		UI::Checkbox("Enabled", &Options::Bhop::Enabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically jumps when you touch the ground while holding the key.");
	}
	UI::CollapsibleEnd();

	ImGui::SetCursorPosY(panelY);
	ImGui::SetCursorPosX(16.0f * UI::sc + UI::CardW + 10.0f * UI::sc);
	if (UI::CollapsibleSection("SETTINGS", UI::CardW))
	{
		UI::labelsection("KEYBIND");
		UI::Bind("##bhop_key", &Options::Bhop::BhopKey);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold this key while moving to bunny hop.");

		bool bhopActive = Options::Bhop::Enabled && Options::Bhop::BhopKey != 0 &&
			(GetAsyncKeyState(Options::Bhop::BhopKey) & 0x8000) != 0;
		UI::Status(bhopActive ? "ACTIVE" : "INACTIVE", bhopActive);
	}
	UI::CollapsibleEnd();
}
