#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../overlay/imgui/KeyBind.h"
#include <thread>
#include <chrono>
#include <mutex>

namespace DesyncVisual
{
    inline std::mutex desyncMutex;
    inline bool isActive = false;
    inline bool wasActive = false;
    inline Vectors::Vector3 ghostPosition = { 0, 0, 0 };
    inline Vectors::Vector3 realPosition = { 0, 0, 0 };
    inline Vectors::Vector3 localHeadPos = { 0, 0, 0 };
    inline bool hasValidData = false;

    inline void OnActivate()
    {
        std::lock_guard<std::mutex> lock(desyncMutex);
        ghostPosition = realPosition;
        hasValidData = true;
    }

    inline void OnDeactivate()
    {
        std::lock_guard<std::mutex> lock(desyncMutex);
        hasValidData = false;
    }

    inline void UpdatePosition()
    {
        if (!Globals::Roblox::LocalPlayer.address)
        {
            hasValidData = false;
            return;
        }

        auto character = Globals::Roblox::LocalPlayer.Character();
        auto hrp = character.FindFirstChild("HumanoidRootPart");
        if (!hrp.address) hrp = character.FindFirstChild("Torso");
        if (!hrp.address) hrp = character.FindFirstChild("UpperTorso");

        auto head = character.FindFirstChild("Head");

        if (hrp.address)
        {
            Vectors::Vector3 pos = hrp.Position();
            {
                std::lock_guard<std::mutex> lock(desyncMutex);
                realPosition = pos;
            }
        }

        if (head.address)
        {
            std::lock_guard<std::mutex> lock(desyncMutex);
            localHeadPos = head.Position();
        }
    }

    inline void RenderDesyncVisual(ImDrawList* drawList)
    {
        if (!Options::Desync::Enabled || !Options::Desync::ShowVisual)
            return;

        bool shouldDraw = false;
        Vectors::Vector3 gPos, rPos, headPos;

        {
            std::lock_guard<std::mutex> lock(desyncMutex);
            shouldDraw = hasValidData;
            gPos = ghostPosition;
            rPos = realPosition;
            headPos = localHeadPos;
        }

        if (!shouldDraw)
            return;

        const auto ghostScreen = WorldToScreen(gPos);
        const auto realScreen = WorldToScreen(rPos);

        if (ghostScreen.x == -1.f || ghostScreen.y == -1.f)
            return;
        if (realScreen.x == -1.f || realScreen.y == -1.f)
            return;

        const ImU32 ghostColor = IM_COL32(
            static_cast<int>(Options::Desync::VisualColor[0] * 255.f),
            static_cast<int>(Options::Desync::VisualColor[1] * 255.f),
            static_cast<int>(Options::Desync::VisualColor[2] * 255.f),
            static_cast<int>(Options::Desync::VisualAlpha * 255.f));

        const ImU32 lineColor = IM_COL32(
            static_cast<int>(Options::Desync::LineColor[0] * 255.f),
            static_cast<int>(Options::Desync::LineColor[1] * 255.f),
            static_cast<int>(Options::Desync::LineColor[2] * 255.f),
            220);

        float dist = rPos.Distance(gPos);

        float viewDist = headPos.Distance(rPos);
        if (viewDist < 1.f) viewDist = 200.f;
        const float scale = 450.f / fmaxf(viewDist, 1.f);
        const float clampedScale = fminf(fmaxf(scale, 0.3f), 3.0f);

        const float boxW = 12.f * clampedScale;
        const float boxH = 24.f * clampedScale;

        ImVec2 ghostMin(ghostScreen.x - boxW * 0.5f, ghostScreen.y - boxH * 0.5f);
        ImVec2 ghostMax(ghostScreen.x + boxW * 0.5f, ghostScreen.y + boxH * 0.5f);

        ImU32 ghostFill = IM_COL32(
            static_cast<int>(Options::Desync::VisualColor[0] * 255.f),
            static_cast<int>(Options::Desync::VisualColor[1] * 255.f),
            static_cast<int>(Options::Desync::VisualColor[2] * 255.f),
            static_cast<int>(40.f * Options::Desync::VisualAlpha));

        drawList->AddRectFilled(ghostMin, ghostMax, ghostFill, 4.0f);
        drawList->AddRect(ghostMin, ghostMax, ghostColor, 4.0f, 0, 2.0f);

        const float cornerLen = boxW * 0.3f;
        auto drawCorner = [&](ImVec2 a, ImVec2 b, ImVec2 c)
        {
            drawList->AddLine(a, b, ghostColor, 1.8f);
            drawList->AddLine(a, c, ghostColor, 1.8f);
        };
        drawCorner(ghostMin, ImVec2(ghostMin.x + cornerLen, ghostMin.y), ImVec2(ghostMin.x, ghostMin.y + cornerLen));
        drawCorner(ghostMax, ImVec2(ghostMax.x - cornerLen, ghostMax.y), ImVec2(ghostMax.x, ghostMax.y + cornerLen));
        drawCorner(ImVec2(ghostMin.x, ghostMax.y), ImVec2(ghostMin.x + cornerLen, ghostMax.y), ImVec2(ghostMin.x, ghostMax.y - cornerLen));
        drawCorner(ImVec2(ghostMax.x, ghostMin.y), ImVec2(ghostMax.x - cornerLen, ghostMin.y), ImVec2(ghostMax.x, ghostMin.y + cornerLen));

        const float headRadius = boxW * 0.35f;
        ImVec2 headCenter(ghostScreen.x, ghostMin.y - headRadius - 2.f);
        drawList->AddCircle(headCenter, headRadius, ghostColor, 16, 1.5f);
        drawList->AddCircleFilled(headCenter, headRadius * 0.4f, ghostColor, 12);

        const float shoulderY = headCenter.y + headRadius + boxH * 0.05f;
        const float hipY = ghostMin.y + boxH * 0.55f;
        const float footY = ghostMax.y - 3.f;
        const float shoulderHalf = boxW * 0.32f;
        const float hipHalf = boxW * 0.18f;

        ImVec2 hipCenter(ghostScreen.x, hipY);
        ImVec2 lShoulder(ghostScreen.x - shoulderHalf, shoulderY);
        ImVec2 rShoulder(ghostScreen.x + shoulderHalf, shoulderY);
        ImVec2 lElbow(ghostScreen.x - shoulderHalf, shoulderY + boxH * 0.18f);
        ImVec2 rElbow(ghostScreen.x + shoulderHalf, shoulderY + boxH * 0.18f);
        ImVec2 lHand(ghostScreen.x - shoulderHalf, shoulderY + boxH * 0.32f);
        ImVec2 rHand(ghostScreen.x + shoulderHalf, shoulderY + boxH * 0.32f);
        ImVec2 lKnee(ghostScreen.x - hipHalf, hipY + (footY - hipY) * 0.5f);
        ImVec2 rKnee(ghostScreen.x + hipHalf, hipY + (footY - hipY) * 0.5f);
        ImVec2 lFoot(ghostScreen.x - hipHalf, footY);
        ImVec2 rFoot(ghostScreen.x + hipHalf, footY);

        drawList->AddLine(headCenter, ImVec2(ghostScreen.x, shoulderY), ghostColor, 1.2f);
        drawList->AddLine(ImVec2(ghostScreen.x, shoulderY), hipCenter, ghostColor, 1.2f);
        drawList->AddLine(lShoulder, lElbow, ghostColor, 1.2f);
        drawList->AddLine(lElbow, lHand, ghostColor, 1.2f);
        drawList->AddLine(rShoulder, rElbow, ghostColor, 1.2f);
        drawList->AddLine(rElbow, rHand, ghostColor, 1.2f);
        drawList->AddLine(hipCenter, lKnee, ghostColor, 1.2f);
        drawList->AddLine(lKnee, lFoot, ghostColor, 1.2f);
        drawList->AddLine(hipCenter, rKnee, ghostColor, 1.2f);
        drawList->AddLine(rKnee, rFoot, ghostColor, 1.2f);

        if (Options::Desync::ShowLine && dist > 1.f)
        {
            drawList->AddLine(
                ImVec2(realScreen.x, realScreen.y),
                ImVec2(ghostScreen.x, ghostScreen.y),
                lineColor, 2.0f);
        }

        const ImU32 distLabelColor = IM_COL32(255, 255, 255, 230);
        char distText[32];
        snprintf(distText, sizeof(distText), "%.0f studs", dist);
        const float fontSize = fmaxf(11.f * clampedScale, 10.f);
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.f, distText);

        ImVec2 labelPos;
        if (dist > 1.f)
        {
            float midX = (realScreen.x + ghostScreen.x) * 0.5f;
            float midY = (realScreen.y + ghostScreen.y) * 0.5f;
            labelPos = ImVec2(midX - textSize.x * 0.5f, midY - textSize.y * 0.5f);
        }
        else
        {
            labelPos = ImVec2(ghostScreen.x - textSize.x * 0.5f, ghostMax.y + 4.f);
        }

        drawList->AddRectFilled(
            ImVec2(labelPos.x - 3.f, labelPos.y - 1.f),
            ImVec2(labelPos.x + textSize.x + 3.f, labelPos.y + textSize.y + 1.f),
            IM_COL32(0, 0, 0, 160), 3.0f);
        drawList->AddText(ImGui::GetFont(), fontSize, labelPos, distLabelColor, distText);
    }
}

inline bool DesyncIsActive()
{
    if (!Options::Desync::Enabled)
        return false;

    if (Options::Desync::ToggleType == 2)
        return true;

    if (Options::Desync::DesyncKey != 0)
    {
        if (Options::Desync::ToggleType == 1)
            return Options::Desync::Toggled;
        else
            return KeyBind::IsPressed(Options::Desync::DesyncKey);
    }

    return Options::Desync::Enabled;
}

inline void ApplyDesyncVelocity()
{
    if (Options::Desync::Method != 1)
        return;

    if (!Globals::Roblox::LocalPlayer.address)
        return;

    auto character = Globals::Roblox::LocalPlayer.Character();
    auto hrp = character.FindFirstChild("HumanoidRootPart");
    if (!hrp.address) hrp = character.FindFirstChild("Torso");
    if (!hrp.address) hrp = character.FindFirstChild("UpperTorso");
    if (!hrp.address) return;

    uintptr_t primitiveAddr = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
    if (!primitiveAddr) return;

    auto cframe = hrp.CFrame();
    Vectors::Vector3 boostDir;

    switch (Options::Desync::BoostAxis)
    {
    case 0: boostDir = cframe.GetLookVector(); break;
    case 1: boostDir = { 0.f, 1.f, 0.f }; break;
    case 2: boostDir = cframe.GetLookVector() * -1.f; break;
    default: boostDir = cframe.GetLookVector(); break;
    }

    Vectors::Vector3 currentVel = Memory->read<Vectors::Vector3>(primitiveAddr + Offsets::Primitive::AssemblyLinearVelocity);

    Vectors::Vector3 boostVel = {
        boostDir.x * Options::Desync::BoostSpeed,
        boostDir.y * Options::Desync::BoostSpeed,
        boostDir.z * Options::Desync::BoostSpeed
    };

    Memory->write<Vectors::Vector3>(primitiveAddr + Offsets::Primitive::AssemblyLinearVelocity, boostVel);
}

inline void DesyncLoop()
{
    bool wasActive = false;

    while (Globals::running)
    {
        try
        {
            if (Options::Desync::Enabled)
            {
                if (Options::Desync::ToggleType == 2)
                {
                    Options::Desync::Toggled = true;
                }
                else if (Options::Desync::DesyncKey != 0)
                {
                    static bool wasKeyPressed = false;
                    bool isKeyPressed = KeyBind::IsPressed(Options::Desync::DesyncKey);

                    if (Options::Desync::ToggleType == 1)
                    {
                        if (isKeyPressed && !wasKeyPressed)
                            Options::Desync::Toggled = !Options::Desync::Toggled;
                        wasKeyPressed = isKeyPressed;
                    }
                    else
                    {
                        Options::Desync::Toggled = isKeyPressed;
                        wasKeyPressed = isKeyPressed;
                    }
                }
            }

            bool shouldApply = DesyncIsActive();
            DesyncVisual::isActive = shouldApply;

            if (shouldApply && !wasActive)
            {
                DesyncVisual::OnActivate();
            }
            else if (!shouldApply && wasActive)
            {
                uintptr_t base = Memory->getBaseAddress();
                if (base)
                {
                    Memory->write<float>(base + Offsets::FFlags::GameNetCompressionLodByteBudgetThresholdPct, 1.0f);
                    Memory->write<int>(base + Offsets::FFlags::PhysicsSenderMaxBandwidthBps, 524288);
                    Memory->write<int>(base + Offsets::FFlags::NextGenReplicatorEnabledWrite4, 0);
                }
                DesyncVisual::OnDeactivate();
            }
            wasActive = shouldApply;

            if (shouldApply)
            {
                uintptr_t base = Memory->getBaseAddress();
                if (base)
                {
                    Memory->write<float>(base + Offsets::FFlags::GameNetCompressionLodByteBudgetThresholdPct, 0.0f);
                    Memory->write<int>(base + Offsets::FFlags::PhysicsSenderMaxBandwidthBps, 0);
                    Memory->write<int>(base + Offsets::FFlags::NextGenReplicatorEnabledWrite4, 1);
                }

                ApplyDesyncVelocity();
            }

            DesyncVisual::UpdatePosition();
        }
        catch (...) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
