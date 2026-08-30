#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/SDK/sdk.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include <string>
#include <vector>

namespace MM2
{
    // Draws coin ESP for Murder Mystery 2. Coins live under
    // Workspace -> Map -> CoinContainer (and sometimes directly under Workspace).
    inline void Render(ImDrawList* dl)
    {
        if (!Options::MM2::CoinESP || !Globals::Roblox::isMM2 || !Globals::Roblox::Workspace)
            return;

        RobloxInstance coinContainer(0);
        auto map = Globals::Roblox::Workspace.FindFirstChild("Map");
        if (map) coinContainer = map.FindFirstChild("CoinContainer");
        if (!coinContainer) coinContainer = Globals::Roblox::Workspace.FindFirstChild("CoinContainer");
        if (!coinContainer) return;

        ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(
            Options::MM2::CoinColor[0], Options::MM2::CoinColor[1],
            Options::MM2::CoinColor[2], Options::MM2::CoinColor[3]));

        RobloxInstance localHRP(0);
        auto lp = Globals::Roblox::LocalPlayer.Character();
        if (lp) localHRP = lp.FindFirstChild("HumanoidRootPart");

        for (auto& coin : coinContainer.GetChildren())
        {
            if (!coin.address) continue;

            RobloxInstance part = coin;
            if (coin.Class() == "Model")
            {
                uintptr_t pp = Memory->read<uintptr_t>(coin.address + Offsets::Model::PrimaryPart);
                if (!pp) continue;
                part = RobloxInstance(pp);
            }

            uintptr_t prim = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
            if (!prim) continue;

            Vectors::Vector3 pos = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Position);
            auto screen = WorldToScreen(pos);
            if (screen.x < 0 || screen.y < 0) continue;

            dl->AddCircleFilled(ImVec2(screen.x, screen.y), 4.0f, col);
            dl->AddCircle(ImVec2(screen.x, screen.y), 6.0f, col);

            if (Options::MM2::ShowDistance && localHRP)
            {
                char buf[32];
                sprintf_s(buf, "%.0f", pos.Distance(localHRP.Position()));
                dl->AddText(ImVec2(screen.x + 10.0f, screen.y - 4.0f), col, buf);
            }
        }
    }
}
