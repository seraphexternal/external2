#pragma once
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <string>
#include "obfuscate.h"
#include "visibility.h"

// Triggerbot diagnostics: append-only per-second summary written to
// %LOCALAPPDATA%\Seraph\tb_debug.txt so triggerbot behavior can be traced
// without a debugger attached.
inline void TBDebug(const char* msg)
{
    char* env = nullptr;
    size_t sz = 0;
    std::string base = ".";
    if (_dupenv_s(&env, &sz, "LOCALAPPDATA") == 0 && env)
    {
        base = env;
        free(env);
    }
    std::string dir = base + SX("\\Seraph");
    CreateDirectoryA(dir.c_str(), nullptr);
    FILE* f = nullptr;
    fopen_s(&f, (dir + SX("\\tb_debug.txt")).c_str(), "a");
    if (f)
    {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

// Dynamic FOV: keep a constant angular hit zone so enemies at any distance
// are equally easy to hit. Screen-space radius must grow proportional to the
// target's distance; we never shrink below 1.0 so close enemies stay at full
// base radius. distance3D is the 3D range to the target.
inline float DynamicFOVScaleFor(float distance3D)
{
    if (!Options::Triggerbot::DynamicFOV)
        return 1.0f;

    float base = (Options::Triggerbot::DynamicFOVBaseDist > 0.1f)
        ? Options::Triggerbot::DynamicFOVBaseDist : 50.f;

    float scale = (distance3D / base) * Options::Triggerbot::DynamicFOVScale;
    if (scale < 1.0f) scale = 1.0f;
    return scale;
}

inline void RenderAdvancedFOV(ImDrawList* drawList)
{
    if (!Options::Triggerbot::AdvancedFOV || !Options::Triggerbot::ShowAdvancedFOV)
        return;

    auto localTeam = Globals::Roblox::LocalPlayer.Team();
    auto localHRP = Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart");

    std::vector<RobloxPlayer> cachedPlayers;
    {
        std::lock_guard<std::mutex> lock(Globals::Caches::CachedPlayerObjectsMutex);
        cachedPlayers = Globals::Caches::CachedPlayerObjects;
    }

    for (auto& player : cachedPlayers)
    {
        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (player.Health <= 0)
            continue;

        float dist3D = 0.f;
        if (localHRP.address && player.HumanoidRootPart.address)
        {
            Vectors::Vector3 diff = player.HumanoidRootPart.Position() - localHRP.Position();
            dist3D = diff.Magnitude();
        }

        if (Options::Triggerbot::TeamCheck && IsTeammate(player))
        {
            continue;
        }

        // Helper lambda to draw 3D box around a body part with FOV expansion
        auto drawPartFOV = [&](const RobloxInstance& part, float fovX, float fovY) {
            if (part.address == 0 || (fovX == 0.0f && fovY == 0.0f))
                return;

            Vectors::Vector3 partPos = part.Position();

            if (Options::Triggerbot::DynamicFOV)
            {
                float distScale = DynamicFOVScaleFor(dist3D);
                fovX *= distScale;
                fovY *= distScale;
            }
            
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
                    static_cast<int>(Options::ESP::VisibleColor[0] * 255.f),
                    static_cast<int>(Options::ESP::VisibleColor[1] * 255.f),
                    static_cast<int>(Options::ESP::VisibleColor[2] * 255.f),
                    30
                );
                
                ImU32 outlineColor = IM_COL32(
                    static_cast<int>(Options::ESP::VisibleColor[0] * 255.f),
                    static_cast<int>(Options::ESP::VisibleColor[1] * 255.f),
                    static_cast<int>(Options::ESP::VisibleColor[2] * 255.f),
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
    // per-second diagnostic counters
    static long long tbCalls = 0, tbCacheEmpty = 0, tbNoHRP = 0, tbPlayers = 0,
                     tbRangeSkip = 0, tbW2sFail = 0, tbNotFound = 0, tbFound = 0,
                     tbFired = 0, tbDelaySkip = 0;
    static auto tbLastSummary = std::chrono::steady_clock::now();
    auto tbNow = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(tbNow - tbLastSummary).count() >= 1000)
    {
        char tbBuf[320];
        sprintf_s(tbBuf, sizeof(tbBuf),
            "calls=%lld cacheEmpty=%lld noHRP=%lld players=%lld rangeSkip=%lld w2sFail=%lld notFound=%lld found=%lld fired=%lld delaySkip=%lld",
            tbCalls, tbCacheEmpty, tbNoHRP, tbPlayers, tbRangeSkip, tbW2sFail,
            tbNotFound, tbFound, tbFired, tbDelaySkip);
        TBDebug(tbBuf);
        tbCalls = 0; tbCacheEmpty = 0; tbNoHRP = 0; tbPlayers = 0; tbRangeSkip = 0;
        tbW2sFail = 0; tbNotFound = 0; tbFound = 0; tbFired = 0; tbDelaySkip = 0;
        tbLastSummary = tbNow;
    }

    // Per-frame detailed debug (toggle with F10)
    static bool tbDebugFrame = false;
    static bool f10WasPressed = false;
    bool f10Pressed = KeyBind::IsPressed(VK_F10);
    if (f10Pressed && !f10WasPressed) { tbDebugFrame = !tbDebugFrame; TBDebug(tbDebugFrame ? "FRAME DEBUG ON" : "FRAME DEBUG OFF"); }
    f10WasPressed = f10Pressed;
    if (tbDebugFrame)
    {
        char buf[512];
        sprintf_s(buf, sizeof(buf),
            "FRAME: viewportValid=%d visualEngine=%llu cacheSize=%zu localHRP=%llu keyPressed=%d toggled=%d enabled=%d",
            Globals::Viewport::Valid,
            Globals::Roblox::VisualEngine,
            Globals::Caches::CachedPlayerObjects.size(),
            Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart").address,
            KeyBind::IsPressed(Options::Triggerbot::TriggerbotKey),
            Options::Triggerbot::Toggled,
            Options::Triggerbot::Enabled);
        TBDebug(buf);
    }

    // Check keybind
    static bool wasKeyPressed = false;
    bool isKeyPressed = KeyBind::IsPressed(Options::Triggerbot::TriggerbotKey);
    
    if (Options::Triggerbot::ToggleType == 2)
    {
        // Always On
        Options::Triggerbot::Toggled = true;
    }
    else if (Options::Triggerbot::ToggleType == 1)
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
    if (!localHRP.address)
        tbNoHRP++;

    std::vector<RobloxPlayer> cachedPlayers;
    {
        std::lock_guard<std::mutex> lock(Globals::Caches::CachedPlayerObjectsMutex);
        cachedPlayers = Globals::Caches::CachedPlayerObjects;
    }

    if (cachedPlayers.empty())
    {
        tbCacheEmpty++;
        return;
    }

	// Get cursor position
	POINT p;
	GetCursorPos(&p);
	
	Vectors::Vector2 cursorPos = { static_cast<float>(p.x), static_cast<float>(p.y) };

    // Check each player
    for (auto& player : cachedPlayers)
    {
        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (player.Health <= 0)
            continue;

        if (Options::Triggerbot::TeamCheck && IsTeammate(player))
        {
            if (tbDebugFrame) { char buf[256]; sprintf_s(buf, sizeof(buf), "  SKIP team: %s", player.Name.c_str()); TBDebug(buf); }
            continue;
        }

        // Check if player is knocked (downed) - health at or below 5
        if (Options::Triggerbot::DownedCheck && player.Health > 0 && player.Health <= 5.0f)
        {
            if (tbDebugFrame) { char buf[256]; sprintf_s(buf, sizeof(buf), "  SKIP downed: %s hp=%.1f", player.Name.c_str(), player.Health); TBDebug(buf); }
            continue;
        }

        if (Globals::Roblox::isRivals && Options::Rivals::AntiKatana && IsHoldingKatana(player))
        {
            if (tbDebugFrame) { char buf[256]; sprintf_s(buf, sizeof(buf), "  SKIP katana: %s", player.Name.c_str()); TBDebug(buf); }
            continue;
        }

        if (Options::Triggerbot::WallCheck && Visibility::IsPlayerOccluded(player))
        {
            if (tbDebugFrame) { char buf[256]; sprintf_s(buf, sizeof(buf), "  SKIP wall: %s", player.Name.c_str()); TBDebug(buf); }
            continue;
        }

        tbPlayers++;

        // Check 3D distance range
        Vectors::Vector3 targetPos = player.HumanoidRootPart.Position();
        if (!localHRP.address)
            continue;
        Vectors::Vector3 diff = targetPos - localHRP.Position();
        float distance3D = diff.Magnitude();
        if (distance3D > Options::Triggerbot::Range)
        {
            tbRangeSkip++;
            continue;
        }

        // Check if cursor is near any body part
        bool foundTarget = false;
        
        // Helper lambda to check if cursor is within the 3D box projection
        auto checkPartFOV = [&](const RobloxInstance& part, float fovX, float fovY) -> bool {
            if (part.address == 0)
                return false;

            // Dynamic FOV: scale based on distance so close/far enemies are handled equally
            if (Options::Triggerbot::DynamicFOV)
            {
                float distScale = DynamicFOVScaleFor(distance3D);
                fovX *= distScale;
                fovY *= distScale;
            }

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
                {
                    tbW2sFail++;
                    return false;
                }
                    
                float distance = screenPos.Distance(cursorPos);
                float effectiveRadius = Options::Triggerbot::Radius;
                if (Options::Triggerbot::DynamicFOV)
                {
                    float distScale = DynamicFOVScaleFor(distance3D);
                    effectiveRadius *= distScale;
                }
                return (distance <= effectiveRadius);
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
            tbFound++;
        else
            tbNotFound++;

        if (tbDebugFrame)
        {
            char buf[256];
            sprintf_s(buf, sizeof(buf), "  %s: %s found=%d dist3D=%.1f", foundTarget ? "HIT" : "MISS", player.Name.c_str(), foundTarget, distance3D);
            TBDebug(buf);
        }

        if (foundTarget)
        {
            // Delay before shooting
            static auto lastFireTime = std::chrono::steady_clock::now();
            auto currentTime = std::chrono::steady_clock::now();
            auto timeSinceLastFire = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastFireTime).count();

            if (tbDebugFrame) { char buf[256]; sprintf_s(buf, sizeof(buf), "  FIRE ATTEMPT: %s delay=%d lastFireAgo=%lld", player.Name.c_str(), Options::Triggerbot::Delay, timeSinceLastFire); TBDebug(buf); }

            // The game must observe a full press->release cycle between clicks:
            // Roblox polls input once per frame (~16ms), so a minimum period is
            // enforced even when Delay is 0. Without it, UP and the next DOWN
            // merge into one long hold and click-driven weapons stop firing.
            int effectiveDelay = Options::Triggerbot::Delay;
            if (effectiveDelay < 80)
                effectiveDelay = 80;

            if (timeSinceLastFire >= effectiveDelay)
            {
                if (tbDebugFrame) { TBDebug("  FIRING!"); }
                // Simulate mouse click
                INPUT input = { 0 };
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                SendInput(1, &input, sizeof(INPUT));

                Sleep(20);

                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &input, sizeof(INPUT));

                // Stamp after the UP so the next shot keeps a full release gap
                lastFireTime = std::chrono::steady_clock::now();
                tbFired++;
            }
            else
            {
                tbDelaySkip++;
                if (tbDebugFrame) { char buf[256]; sprintf_s(buf, sizeof(buf), "  DELAY SKIP: %lldms < %dms", timeSinceLastFire, effectiveDelay); TBDebug(buf); }
            }

            return; // Only shoot at one target at a time
        }
    }
}
