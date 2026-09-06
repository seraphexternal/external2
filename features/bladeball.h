#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/SDK/sdk.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include <Windows.h>
#include <string>
#include <vector>

namespace BladeBall
{
    inline ImU32 ToCol(const float c[4])
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
    }

    // Fire a parry input. By default we send a fast double left-click
    // (most reliable for Blade Ball); optionally use the 'F' key instead.
    inline void Parry()
    {
        if (Options::BladeBall::UseF)
        {
            keybd_event(0x46, 0, 0, 0);
            Sleep(10);
            keybd_event(0x46, 0, KEYEVENTF_KEYUP, 0);
        }
        else
        {
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            Sleep(10);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            Sleep(10);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            Sleep(10);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        }
    }

    inline Vectors::Vector3 GetVel(const RobloxInstance& part)
    {
        uintptr_t prim = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
        if (!prim) return { 0, 0, 0 };
        return Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity);
    }

    // Collect every ball in the workspace. Balls are usually direct children of
    // Workspace (class "Ball" or a part named "ball"), but some versions put them
    // inside a "Balls" folder/model, so we descend one level too.
    inline std::vector<RobloxInstance> CollectBalls()
    {
        std::vector<RobloxInstance> balls;
        if (!Globals::Roblox::Workspace) return balls;

        auto check = [&](const RobloxInstance& inst)
        {
            if (!inst.address) return;
            std::string cls = inst.Class();
            std::string name = inst.Name();
            bool isBall = (cls == "Ball") ||
                ((cls == "Part" || cls == "MeshPart") && _strnicmp(name.c_str(), "ball", 4) == 0);
            if (isBall) balls.push_back(inst);
        };

        for (auto& child : Globals::Roblox::Workspace.GetChildren())
        {
            check(child);
            if (child.Class() == "Model" || child.Class() == "Folder")
            {
                for (auto& gc : child.GetChildren())
                    check(gc);
            }
        }
        return balls;
    }

    // Returns a ball that is currently threatening the local player: within parry
    // range AND moving toward them. (We can't read the ball's "target" StringValue
    // from Seraph's offsets, so we approximate with range + approach velocity.)
    inline RobloxInstance DetectThreatBall()
    {
        RobloxInstance result(0);
        auto lp = Globals::Roblox::LocalPlayer.Character();
        if (!lp) return result;
        auto hrp = lp.FindFirstChild("HumanoidRootPart");
        if (!hrp) return result;

        Vectors::Vector3 localPos = hrp.Position();
        double parryRange = (double)Options::BladeBall::ParryRange;

        for (auto& ball : CollectBalls())
        {
            Vectors::Vector3 ballPos = ball.Position();
            if (ballPos.Distance(localPos) > parryRange) continue;

            Vectors::Vector3 vel = GetVel(ball);
            Vectors::Vector3 toLocal = localPos - ballPos;
            double dot = vel.x * toLocal.x + vel.y * toLocal.y + vel.z * toLocal.z;
            if (dot <= 0.0) continue; // moving away / parallel -> not a threat

            result = ball;
            break;
        }
        return result;
    }

    inline void Render(ImDrawList* dl)
    {
        if (!Globals::Roblox::isBladeBall) return;

        auto lp = Globals::Roblox::LocalPlayer.Character();
        auto hrp = lp ? lp.FindFirstChild("HumanoidRootPart") : RobloxInstance(0);
        Vectors::Vector3 localPos = hrp ? hrp.Position() : Vectors::Vector3{ 0, 0, 0 };

        ImU32 ballCol = ToCol(Options::BladeBall::BallColor);

        for (auto& ball : CollectBalls())
        {
            Vectors::Vector3 ballPos = ball.Position();
            auto screen = WorldToScreen(ballPos);
            if (screen.x < 0 || screen.y < 0) continue;

            if (Options::BladeBall::BallESP)
            {
                dl->AddCircle(ImVec2(screen.x, screen.y), 8.0f, ballCol, 0, 2.0f);
                if (Options::BladeBall::Distance)
                {
                    char buf[32];
                    sprintf_s(buf, "%.0f", ballPos.Distance(localPos));
                    dl->AddText(ImVec2(screen.x + 12.0f, screen.y - 4.0f), ballCol, buf);
                }
            }
        }

        if (Options::BladeBall::ParryRangeESP && hrp)
        {
            auto lpScreen = WorldToScreen(localPos);
            if (lpScreen.x >= 0)
            {
                double camDist = localPos.Distance(Globals::Roblox::Camera.Position());
                if (camDist < 0.1) camDist = 0.1;
                double radiusPx = ((double)Options::BladeBall::ParryRange * 3.5) / camDist *
                    (Globals::Viewport::Dimensions.y / 2.0);
                dl->AddCircle(ImVec2(lpScreen.x, lpScreen.y), (float)radiusPx,
                    ToCol(Options::BladeBall::ParryRangeColor), 0, 2.0f);
            }
        }

        if (Options::BladeBall::ShowStatus)
        {
            const auto display = Globals::Viewport::Dimensions;
            const float pad = 10.0f;
            const ImVec2 pos = ImVec2(pad, display.y - 54.0f);
            char line1[64];
            sprintf_s(line1, "Blade Ball  |  Auto Parry: %s", Options::BladeBall::AutoParry ? "ON" : "OFF");
            ImU32 col1 = Options::BladeBall::AutoParry ? IM_COL32(0.3f * 255, 1.0f * 255, 0.4f * 255, 255) : IM_COL32(180, 180, 180, 255);
            dl->AddText(ImGui::GetFont(), 16.0f, pos, col1, line1);

            const bool parrying = Globals::BladeBall::isActive;
            char line2[64];
            sprintf_s(line2, "Status: %s", parrying ? "PARRYING" : "idle");
            ImU32 col2 = parrying ? IM_COL32(255, 80, 80, 255) : IM_COL32(200, 200, 200, 255);
            dl->AddText(ImGui::GetFont(), 16.0f, ImVec2(pos.x, pos.y + 20.0f), col2, line2);
        }
    }

    // Background thread: auto-parry loop.
    inline void RunService()
    {
        while (Globals::running)
        {
            if (Globals::Roblox::isBladeBall && Options::BladeBall::AutoParry &&
                Globals::Roblox::LocalPlayer.address)
            {
                if (DetectThreatBall().address != 0)
                {
                    Globals::BladeBall::isActive = true;
                    Globals::BladeBall::status = "PARRYING";
                    Parry();
                    Sleep(110);
                }
                else
                {
                    Globals::BladeBall::isActive = false;
                    Globals::BladeBall::status = "idle";
                    if (Options::BladeBall::ParryKey != 0 &&
                        (GetAsyncKeyState(Options::BladeBall::ParryKey) & 0x8000))
                    {
                        Parry();
                    }
                    Sleep(15);
                }
            }
            else
            {
                Sleep(200);
            }
        }
    }
}
