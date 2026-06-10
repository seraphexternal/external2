#pragma once
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include <windows.h>
#include <chrono>

inline void RenderAdvancedFOV(ImDrawList* drawList)
{
    if (!Options::Triggerbot::AdvancedFOV || !Options::Triggerbot::ShowAdvancedFOV)
        return;

    auto localTeam = Globals::Roblox::LocalPlayer.Team();
    
    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (player.Health <= 0)
            continue;

        if (Options::Triggerbot::TeamCheck && IsTeammate(player))
        {
            continue;
        }

        // Helper lambda to draw 3D box around a body part with FOV expansion
        auto drawPartFOV = [&](const RobloxInstance& part, float fovX, float fovY) {
            if (part.address == 0 || (fovX == 0.0f && fovY == 0.0f))
                return;

            Vectors::Vector3 partPos = part.Position();
            
            if (Options::Triggerbot::Prediction)
            {
                Vectors::Vector3 targetVelocity = player.Velocity;
                Vectors::Vector3 predictionOffset = {
                    targetVelocity.x * Options::Triggerbot::PredictionX,
                    targetVelocity.y * Options::Triggerbot::PredictionY,
                    targetVelocity.z * Options::Triggerbot::PredictionX
                };
                
                partPos = Vectors::Vector3{
                    partPos.x + predictionOffset.x,
                    partPos.y + predictionOffset.y,
                    partPos.z + predictionOffset.z
                };
            }
            
            sCFrame partCFrame = part.CFrame();
            
            Vectors::Vector3 rightVec = partCFrame.GetRightVector();
            Vectors::Vector3 upVec = partCFrame.GetUpVector();
            Vectors::Vector3 lookVec = partCFrame.GetLookVector();

            // Convert screen-space FOV to world-space expansion
            float worldExpansionX = fovX * 0.02f; // Scale factor for conversion
            float worldExpansionY = fovY * 0.02f;
            
            // Base size for body parts
            float baseWidth = 1.0f + worldExpansionX;
            float baseHeight = 1.0f + worldExpansionY;
            float baseDepth = 1.0f + worldExpansionX;

            // Create 8 corners of the expanded bounding box
            std::vector<Vectors::Vector3> corners3D = {
                partPos + rightVec * baseWidth + upVec * baseHeight + lookVec * baseDepth,
                partPos - rightVec * baseWidth + upVec * baseHeight + lookVec * baseDepth,
                partPos + rightVec * baseWidth - upVec * baseHeight + lookVec * baseDepth,
                partPos - rightVec * baseWidth - upVec * baseHeight + lookVec * baseDepth,
                partPos + rightVec * baseWidth + upVec * baseHeight - lookVec * baseDepth,
                partPos - rightVec * baseWidth + upVec * baseHeight - lookVec * baseDepth,
                partPos + rightVec * baseWidth - upVec * baseHeight - lookVec * baseDepth,
                partPos - rightVec * baseWidth - upVec * baseHeight - lookVec * baseDepth
            };

            // Convert to 2D screen space
            std::vector<ImVec2> corners2D;
            for (const auto& corner : corners3D)
            {
                auto screenPos = WorldToScreen(corner);
                if (screenPos.x != -1 && screenPos.y != -1)
                {
                    corners2D.push_back(ImVec2(screenPos.x, screenPos.y));
                }
            }

            if (corners2D.size() >= 8)
            {
                // Draw filled translucent box faces
                ImU32 fillColor = IM_COL32(
                    static_cast<int>(Options::ESP::ChamsColor[0] * 255.f),
                    static_cast<int>(Options::ESP::ChamsColor[1] * 255.f),
                    static_cast<int>(Options::ESP::ChamsColor[2] * 255.f),
                    30
                );
                
                ImU32 outlineColor = IM_COL32(
                    static_cast<int>(Options::ESP::ChamsColor[0] * 255.f),
                    static_cast<int>(Options::ESP::ChamsColor[1] * 255.f),
                    static_cast<int>(Options::ESP::ChamsColor[2] * 255.f),
                    150
                );
                drawList->AddQuadFilled(corners2D[0], corners2D[1], corners2D[3], corners2D[2], fillColor);
                
                // Front face
                drawList->AddQuadFilled(corners2D[0], corners2D[1], corners2D[3], corners2D[2], fillColor);
                // Back face
                drawList->AddQuadFilled(corners2D[4], corners2D[5], corners2D[7], corners2D[6], fillColor);
                // Top face
                drawList->AddQuadFilled(corners2D[0], corners2D[1], corners2D[5], corners2D[4], fillColor);
                // Bottom face
                drawList->AddQuadFilled(corners2D[2], corners2D[3], corners2D[7], corners2D[6], fillColor);
                // Left face
                drawList->AddQuadFilled(corners2D[1], corners2D[3], corners2D[7], corners2D[5], fillColor);
                // Right face
                drawList->AddQuadFilled(corners2D[0], corners2D[2], corners2D[6], corners2D[4], fillColor);

                // Draw outline edges
                float thickness = 1.5f;
                
                // Front face edges
                drawList->AddLine(corners2D[0], corners2D[1], outlineColor, thickness);
                drawList->AddLine(corners2D[1], corners2D[3], outlineColor, thickness);
                drawList->AddLine(corners2D[3], corners2D[2], outlineColor, thickness);
                drawList->AddLine(corners2D[2], corners2D[0], outlineColor, thickness);

                // Back face edges
                drawList->AddLine(corners2D[4], corners2D[5], outlineColor, thickness);
                drawList->AddLine(corners2D[5], corners2D[7], outlineColor, thickness);
                drawList->AddLine(corners2D[7], corners2D[6], outlineColor, thickness);
                drawList->AddLine(corners2D[6], corners2D[4], outlineColor, thickness);

                // Connecting edges
                drawList->AddLine(corners2D[0], corners2D[4], outlineColor, thickness);
                drawList->AddLine(corners2D[1], corners2D[5], outlineColor, thickness);
                drawList->AddLine(corners2D[2], corners2D[6], outlineColor, thickness);
                drawList->AddLine(corners2D[3], corners2D[7], outlineColor, thickness);
            }
        };

        // Draw FOV for each body part
        drawPartFOV(player.Head, Options::Triggerbot::HeadFOV_X, Options::Triggerbot::HeadFOV_Y);
        
        if (player.RigType == 0) // R6
        {
            drawPartFOV(player.Torso, Options::Triggerbot::TorsoFOV_X, Options::Triggerbot::TorsoFOV_Y);
            drawPartFOV(player.Left_Arm, Options::Triggerbot::LeftUpperArmFOV_X, Options::Triggerbot::LeftUpperArmFOV_Y);
            drawPartFOV(player.Right_Arm, Options::Triggerbot::RightUpperArmFOV_X, Options::Triggerbot::RightUpperArmFOV_Y);
            drawPartFOV(player.Left_Leg, Options::Triggerbot::LeftUpperLegFOV_X, Options::Triggerbot::LeftUpperLegFOV_Y);
            drawPartFOV(player.Right_Leg, Options::Triggerbot::RightUpperLegFOV_X, Options::Triggerbot::RightUpperLegFOV_Y);
        }
        else // R15
        {
            // If Torso FOV is set, use it for both Upper and Lower Torso, otherwise use individual settings
            float upperTorsoX = (Options::Triggerbot::TorsoFOV_X > 0.0f) ? Options::Triggerbot::TorsoFOV_X : Options::Triggerbot::UpperTorsoFOV_X;
            float upperTorsoY = (Options::Triggerbot::TorsoFOV_Y > 0.0f) ? Options::Triggerbot::TorsoFOV_Y : Options::Triggerbot::UpperTorsoFOV_Y;
            float lowerTorsoX = (Options::Triggerbot::TorsoFOV_X > 0.0f) ? Options::Triggerbot::TorsoFOV_X : Options::Triggerbot::LowerTorsoFOV_X;
            float lowerTorsoY = (Options::Triggerbot::TorsoFOV_Y > 0.0f) ? Options::Triggerbot::TorsoFOV_Y : Options::Triggerbot::LowerTorsoFOV_Y;
            
            drawPartFOV(player.Upper_Torso, upperTorsoX, upperTorsoY);
            drawPartFOV(player.Lower_Torso, lowerTorsoX, lowerTorsoY);
            drawPartFOV(player.Left_Upper_Arm, Options::Triggerbot::LeftUpperArmFOV_X, Options::Triggerbot::LeftUpperArmFOV_Y);
            drawPartFOV(player.Left_Lower_Arm, Options::Triggerbot::LeftLowerArmFOV_X, Options::Triggerbot::LeftLowerArmFOV_Y);
            drawPartFOV(player.Left_Hand, Options::Triggerbot::LeftHandFOV_X, Options::Triggerbot::LeftHandFOV_Y);
            drawPartFOV(player.Right_Upper_Arm, Options::Triggerbot::RightUpperArmFOV_X, Options::Triggerbot::RightUpperArmFOV_Y);
            drawPartFOV(player.Right_Lower_Arm, Options::Triggerbot::RightLowerArmFOV_X, Options::Triggerbot::RightLowerArmFOV_Y);
            drawPartFOV(player.Right_Hand, Options::Triggerbot::RightHandFOV_X, Options::Triggerbot::RightHandFOV_Y);
            drawPartFOV(player.Left_Upper_Leg, Options::Triggerbot::LeftUpperLegFOV_X, Options::Triggerbot::LeftUpperLegFOV_Y);
            drawPartFOV(player.Left_Lower_Leg, Options::Triggerbot::LeftLowerLegFOV_X, Options::Triggerbot::LeftLowerLegFOV_Y);
            drawPartFOV(player.Left_Foot, Options::Triggerbot::LeftFootFOV_X, Options::Triggerbot::LeftFootFOV_Y);
            drawPartFOV(player.Right_Upper_Leg, Options::Triggerbot::RightUpperLegFOV_X, Options::Triggerbot::RightUpperLegFOV_Y);
            drawPartFOV(player.Right_Lower_Leg, Options::Triggerbot::RightLowerLegFOV_X, Options::Triggerbot::RightLowerLegFOV_Y);
            drawPartFOV(player.Right_Foot, Options::Triggerbot::RightFootFOV_X, Options::Triggerbot::RightFootFOV_Y);
        }
    }
}

inline void RunTriggerbot()
{
    if (!Options::Triggerbot::Enabled)
        return;

    // Check keybind
    static bool wasKeyPressed = false;
    bool isKeyPressed = KeyBind::IsPressed(Options::Triggerbot::TriggerbotKey);
    
    if (Options::Triggerbot::ToggleType == 1)
    {
        // Toggle mode
        if (isKeyPressed && !wasKeyPressed)
        {
            Options::Triggerbot::Toggled = !Options::Triggerbot::Toggled;
        }
        wasKeyPressed = isKeyPressed;
        
        if (!Options::Triggerbot::Toggled)
            return;
    }
    else
    {
        // Hold mode
        if (!isKeyPressed)
        {
            Options::Triggerbot::Toggled = false;
            return;
        }
    }

    auto localTeam = Globals::Roblox::LocalPlayer.Team();
    auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
    
    if (Globals::Caches::CachedPlayerObjects.empty())
        return;

    // Get cursor position
    POINT p;
    GetCursorPos(&p);
    
    HWND robloxWindow = FindWindowA("Roblox", nullptr);
    if (robloxWindow)
    {
        ScreenToClient(robloxWindow, &p);
    }

    Vectors::Vector2 cursorPos = { static_cast<float>(p.x), static_cast<float>(p.y) };

    // Check each player
    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (player.Health <= 0)
            continue;

        if (Options::Triggerbot::TeamCheck && IsTeammate(player))
        {
            continue;
        }

        // Check if player is knocked (downed) - health at or below 5
        if (Options::Triggerbot::DownedCheck && player.Health > 0 && player.Health <= 5.0f)
            continue;

        // Check 3D distance range
        Vectors::Vector3 targetPos = player.HumanoidRootPart.Position();
        Vectors::Vector3 diff = targetPos - localHRP.Position();
        float distance3D = diff.Magnitude();
        
        if (distance3D > Options::Triggerbot::Range)
            continue;

        // Check if cursor is near any body part
        bool foundTarget = false;
        
        // Helper lambda to check if cursor is within the 3D box projection
        auto checkPartFOV = [&](const RobloxInstance& part, float fovX, float fovY) -> bool {
            if (part.address == 0)
                return false;

            if (Options::Triggerbot::AdvancedFOV && (fovX == 0.0f && fovY == 0.0f))
                return false;

            Vectors::Vector3 partPos = part.Position();
            
            if (Options::Triggerbot::Prediction)
            {
                Vectors::Vector3 targetVelocity = player.Velocity;
                Vectors::Vector3 predictionOffset = {
                    targetVelocity.x * Options::Triggerbot::PredictionX,
                    targetVelocity.y * Options::Triggerbot::PredictionY,
                    targetVelocity.z * Options::Triggerbot::PredictionX
                };
                
                partPos = Vectors::Vector3{
                    partPos.x + predictionOffset.x,
                    partPos.y + predictionOffset.y,
                    partPos.z + predictionOffset.z
                };
            }
            
            sCFrame partCFrame = part.CFrame();
            
            if (!Options::Triggerbot::AdvancedFOV)
            {
                // Legacy mode: simple circular radius check
                Vectors::Vector2 screenPos = WorldToScreen(partPos);
                if (screenPos.x == -1 || screenPos.y == -1)
                    return false;
                    
                float distance = screenPos.Distance(cursorPos);
                return (distance <= Options::Triggerbot::Radius);
            }
            
            // Advanced FOV mode: check if cursor is inside the 3D box projection
            Vectors::Vector3 rightVec = partCFrame.GetRightVector();
            Vectors::Vector3 upVec = partCFrame.GetUpVector();
            Vectors::Vector3 lookVec = partCFrame.GetLookVector();

            // Convert screen-space FOV to world-space expansion
            float worldExpansionX = fovX * 0.02f;
            float worldExpansionY = fovY * 0.02f;
            
            float baseWidth = 1.0f + worldExpansionX;
            float baseHeight = 1.0f + worldExpansionY;
            float baseDepth = 1.0f + worldExpansionX;

            // Create 8 corners of the expanded bounding box
            std::vector<Vectors::Vector3> corners3D = {
                partPos + rightVec * baseWidth + upVec * baseHeight + lookVec * baseDepth,
                partPos - rightVec * baseWidth + upVec * baseHeight + lookVec * baseDepth,
                partPos + rightVec * baseWidth - upVec * baseHeight + lookVec * baseDepth,
                partPos - rightVec * baseWidth - upVec * baseHeight + lookVec * baseDepth,
                partPos + rightVec * baseWidth + upVec * baseHeight - lookVec * baseDepth,
                partPos - rightVec * baseWidth + upVec * baseHeight - lookVec * baseDepth,
                partPos + rightVec * baseWidth - upVec * baseHeight - lookVec * baseDepth,
                partPos - rightVec * baseWidth - upVec * baseHeight - lookVec * baseDepth
            };

            // Convert to 2D screen space
            std::vector<ImVec2> corners2D;
            for (const auto& corner : corners3D)
            {
                auto screenPos = WorldToScreen(corner);
                if (screenPos.x != -1 && screenPos.y != -1)
                {
                    corners2D.push_back(ImVec2(screenPos.x, screenPos.y));
                }
            }

            if (corners2D.size() < 8)
                return false;

            // Find bounding rectangle of the 3D box projection
            float minX = FLT_MAX, minY = FLT_MAX;
            float maxX = -FLT_MAX, maxY = -FLT_MAX;
            
            for (const auto& corner : corners2D)
            {
                minX = (std::min)(minX, corner.x);
                maxX = (std::max)(maxX, corner.x);
                minY = (std::min)(minY, corner.y);
                maxY = (std::max)(maxY, corner.y);
            }

            // Check if cursor is inside the bounding rectangle
            return (cursorPos.x >= minX && cursorPos.x <= maxX && 
                    cursorPos.y >= minY && cursorPos.y <= maxY);
        };

        // Check each body part with its specific FOV
        if (checkPartFOV(player.Head, Options::Triggerbot::HeadFOV_X, Options::Triggerbot::HeadFOV_Y))
            foundTarget = true;
        
        if (!foundTarget && player.RigType == 0) // R6
        {
            if (checkPartFOV(player.Torso, Options::Triggerbot::TorsoFOV_X, Options::Triggerbot::TorsoFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Left_Arm, Options::Triggerbot::LeftUpperArmFOV_X, Options::Triggerbot::LeftUpperArmFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Right_Arm, Options::Triggerbot::RightUpperArmFOV_X, Options::Triggerbot::RightUpperArmFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Left_Leg, Options::Triggerbot::LeftUpperLegFOV_X, Options::Triggerbot::LeftUpperLegFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Right_Leg, Options::Triggerbot::RightUpperLegFOV_X, Options::Triggerbot::RightUpperLegFOV_Y))
                foundTarget = true;
        }
        else if (!foundTarget) // R15
        {
            // If Torso FOV is set, use it for both Upper and Lower Torso, otherwise use individual settings
            float upperTorsoX = (Options::Triggerbot::TorsoFOV_X > 0.0f) ? Options::Triggerbot::TorsoFOV_X : Options::Triggerbot::UpperTorsoFOV_X;
            float upperTorsoY = (Options::Triggerbot::TorsoFOV_Y > 0.0f) ? Options::Triggerbot::TorsoFOV_Y : Options::Triggerbot::UpperTorsoFOV_Y;
            float lowerTorsoX = (Options::Triggerbot::TorsoFOV_X > 0.0f) ? Options::Triggerbot::TorsoFOV_X : Options::Triggerbot::LowerTorsoFOV_X;
            float lowerTorsoY = (Options::Triggerbot::TorsoFOV_Y > 0.0f) ? Options::Triggerbot::TorsoFOV_Y : Options::Triggerbot::LowerTorsoFOV_Y;
            
            if (checkPartFOV(player.Upper_Torso, upperTorsoX, upperTorsoY))
                foundTarget = true;
            else if (checkPartFOV(player.Lower_Torso, lowerTorsoX, lowerTorsoY))
                foundTarget = true;
            else if (checkPartFOV(player.Left_Upper_Arm, Options::Triggerbot::LeftUpperArmFOV_X, Options::Triggerbot::LeftUpperArmFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Left_Lower_Arm, Options::Triggerbot::LeftLowerArmFOV_X, Options::Triggerbot::LeftLowerArmFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Left_Hand, Options::Triggerbot::LeftHandFOV_X, Options::Triggerbot::LeftHandFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Right_Upper_Arm, Options::Triggerbot::RightUpperArmFOV_X, Options::Triggerbot::RightUpperArmFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Right_Lower_Arm, Options::Triggerbot::RightLowerArmFOV_X, Options::Triggerbot::RightLowerArmFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Right_Hand, Options::Triggerbot::RightHandFOV_X, Options::Triggerbot::RightHandFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Left_Upper_Leg, Options::Triggerbot::LeftUpperLegFOV_X, Options::Triggerbot::LeftUpperLegFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Left_Lower_Leg, Options::Triggerbot::LeftLowerLegFOV_X, Options::Triggerbot::LeftLowerLegFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Left_Foot, Options::Triggerbot::LeftFootFOV_X, Options::Triggerbot::LeftFootFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Right_Upper_Leg, Options::Triggerbot::RightUpperLegFOV_X, Options::Triggerbot::RightUpperLegFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Right_Lower_Leg, Options::Triggerbot::RightLowerLegFOV_X, Options::Triggerbot::RightLowerLegFOV_Y))
                foundTarget = true;
            else if (checkPartFOV(player.Right_Foot, Options::Triggerbot::RightFootFOV_X, Options::Triggerbot::RightFootFOV_Y))
                foundTarget = true;
        }

        if (foundTarget)
        {
            // Delay before shooting
            static auto lastFireTime = std::chrono::steady_clock::now();
            auto currentTime = std::chrono::steady_clock::now();
            auto timeSinceLastFire = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastFireTime).count();

            if (timeSinceLastFire >= Options::Triggerbot::Delay)
            {
                // Simulate mouse click
                INPUT input = { 0 };
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                SendInput(1, &input, sizeof(INPUT));

                Sleep(20);

                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &input, sizeof(INPUT));

                lastFireTime = currentTime;
            }
            
            return; // Only shoot at one target at a time
        }
    }
}
