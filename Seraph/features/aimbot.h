#pragma once
#include <algorithm>
#include <cmath>
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../overlay/imgui/KeyBind.h"

inline Vectors::Vector3 GetVelocity(const RobloxInstance& part)
{
	if (!part.address)
		return Vectors::Vector3{ 0.f, 0.f, 0.f };
	
	uintptr_t primitiveAddr = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
	if (!primitiveAddr)
		return Vectors::Vector3{ 0.f, 0.f, 0.f };
	
	return Memory->read<Vectors::Vector3>(primitiveAddr + Offsets::Primitive::AssemblyLinearVelocity);
}

inline Vectors::Vector3 GetTargetPosition(const RobloxPlayer& player)
{
    Vectors::Vector3 basePos;
    RobloxInstance targetPart(0);
    
    // Check if player is in air (Y velocity > 1 or < -1)
    Vectors::Vector3 velocity = player.Velocity;
    bool isInAir = (velocity.y > 1.0f || velocity.y < -1.0f);
    
    // Use air target bone if player is in air, otherwise use normal target bone
    int boneToUse = isInAir ? Options::Aimbot::AirTargetBone : Options::Aimbot::TargetBone;
    
    switch (boneToUse)
    {
        case 0: // Head
            targetPart = player.Head;
            basePos = player.Head.Position();
            break;
        case 1: // Torso/HumanoidRootPart
            targetPart = player.HumanoidRootPart;
            basePos = player.HumanoidRootPart.Position();
            break;
        case 2: // Left Arm
            if (player.RigType == 0)
            {
                targetPart = player.Left_Arm;
                basePos = player.Left_Arm.Position();
            }
            else
            {
                targetPart = player.Left_Hand;
                basePos = player.Left_Hand.Position();
            }
            break;
        case 3: // Right Arm
            if (player.RigType == 0)
            {
                targetPart = player.Right_Arm;
                basePos = player.Right_Arm.Position();
            }
            else
            {
                targetPart = player.Right_Hand;
                basePos = player.Right_Hand.Position();
            }
            break;
        case 4: // Left Leg
            if (player.RigType == 0)
            {
                targetPart = player.Left_Leg;
                basePos = player.Left_Leg.Position();
            }
            else
            {
                targetPart = player.Left_Foot;
                basePos = player.Left_Foot.Position();
            }
            break;
        case 5: // Right Leg
            if (player.RigType == 0)
            {
                targetPart = player.Right_Leg;
                basePos = player.Right_Leg.Position();
            }
            else
            {
                targetPart = player.Right_Foot;
                basePos = player.Right_Foot.Position();
            }
            break;
        case 6: // Lower Torso
            if (player.RigType == 1) // R15 only
            {
                targetPart = player.Lower_Torso;
                basePos = player.Lower_Torso.Position();
            }
            else
            {
                targetPart = player.HumanoidRootPart;
                basePos = player.HumanoidRootPart.Position();
            }
            break;
        case 7: // Upper Torso
            if (player.RigType == 1) // R15 only
            {
                targetPart = player.Upper_Torso;
                basePos = player.Upper_Torso.Position();
            }
            else
            {
                targetPart = player.HumanoidRootPart;
                basePos = player.HumanoidRootPart.Position();
            }
            break;
        default:
            targetPart = player.Head;
            basePos = player.Head.Position();
            break;
    }
    
    // Apply prediction if enabled
    if (Options::Aimbot::Prediction && targetPart.address != 0)
    {
        Vectors::Vector3 targetVelocity = player.Velocity;
        
        // Multiply velocity by prediction factors
        Vectors::Vector3 predictionOffset = {
            targetVelocity.x * Options::Aimbot::PredictionX,
            targetVelocity.y * Options::Aimbot::PredictionY,
            targetVelocity.z * Options::Aimbot::PredictionX
        };
        
        // Add prediction offset to base position
        return Vectors::Vector3{
            basePos.x + predictionOffset.x,
            basePos.y + predictionOffset.y,
            basePos.z + predictionOffset.z
        };
    }
    
    return basePos;
}

inline RobloxPlayer GetClosestPlayer()
{
    // Dynamically retrieve LocalPlayer to prevent stale pointer issues when rejoining
    if (Globals::Roblox::Players.address != 0)
    {
        Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));
    }

    RobloxPlayer target;
    auto maxDistance = FLT_MAX;
    auto localTeam = Globals::Roblox::LocalPlayer.Team();
    auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");

    POINT p;
    GetCursorPos(&p);

    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        auto HRP = player.HumanoidRootPart;
        if (!HRP.address)
            continue;

        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (Options::Aimbot::TeamCheck && IsTeammate(player))
        {
            continue;
        }

        if (player.Health == 0)
            continue;

        // Skip knocked/downed players if check is enabled (health at or below 5)
        if (player.Health > 0 && player.Health <= 5.0f && Options::Aimbot::DownedCheck)
            continue;

        auto targetPos = GetTargetPosition(player);
        auto targetPos2D = WorldToScreen(targetPos);

        if (targetPos2D.x == -1 && targetPos2D.y == -1)
            continue;

        Vectors::Vector3 diff = localHRP.Position() - targetPos;
        float distance3D = diff.Magnitude();
        
        if (distance3D > Options::Aimbot::Range)
            continue;

        auto distance = targetPos2D.Distance({ static_cast<float>(p.x), static_cast<float>(p.y) });

        if (distance < maxDistance && distance <= Options::Aimbot::FOV)
        {
            maxDistance = distance;
            target = player;
        }
    }
    return target;
}

inline float ApplySmoothnessCurve(float smoothness, int curveType)
{
    // Apply curve transformation based on selected type
    // Use exponential scaling for more balanced control across the range
    float t;
    switch (curveType)
    {
        case 0: // Linear - exponential scaling for better balance
        {
            // Map 0.0-1.0 smoothness to exponential speed curve
            // Lower values = faster, higher values = much slower
            float exponent = 1.0f + (smoothness * 4.0f); // 1.0 to 5.0
            t = pow(1.0f - smoothness, exponent);
            break;
        }
        case 1: // Ease In (starts slow, ends fast)
        {
            float exponent = 1.5f + (smoothness * 3.0f);
            t = pow(1.0f - smoothness, exponent);
            break;
        }
        case 2: // Ease Out (starts fast, ends slow)
        {
            float exponent = 2.0f + (smoothness * 2.5f);
            t = pow(1.0f - smoothness, exponent);
            break;
        }
        case 3: // Ease In-Out (smooth on both ends)
        {
            float exponent = 1.8f + (smoothness * 3.5f);
            t = pow(1.0f - smoothness, exponent);
            break;
        }
        case 4: // Custom Bezier Curve
        {
            if (Options::Aimbot::CustomCurveEnabled)
            {
                // Cubic Bezier curve with control points
                float p0 = 0.0f;
                float p1 = Options::Aimbot::CustomCurveP1[1];
                float p2 = Options::Aimbot::CustomCurveP2[1];
                float p3 = 1.0f;
                
                float u = 1.0f - smoothness;
                float tt = smoothness * smoothness;
                float ttt = tt * smoothness;
                float uu = u * u;
                float uuu = uu * u;
                
                // Bezier formula
                float curveValue = uuu * p0 + 3 * uu * smoothness * p1 + 3 * u * tt * p2 + ttt * p3;
                t = 1.0f - curveValue;
            }
            else
            {
                // Fallback to linear if custom not enabled
                float exponent = 1.0f + (smoothness * 4.0f);
                t = pow(1.0f - smoothness, exponent);
            }
            break;
        }
        default:
        {
            float exponent = 1.0f + (smoothness * 4.0f);
            t = pow(1.0f - smoothness, exponent);
            break;
        }
    }
    return std::clamp<float>(t, 0.001f, 1.0f);
}

inline void CameraRotation(const RobloxPlayer& target)
{
    Matrixes::Matrix3x3 currentRotation = Memory->read<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation);

    sCFrame cameraCFrame = Globals::Roblox::Camera.CFrame();
    Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position);

    Vectors::Vector3 targetPos = GetTargetPosition(target);
    
    // Add shake if enabled
    if (Options::Aimbot::Shake && Options::Aimbot::ShakeIntensity > 0.0f)
    {
        static float shakeTime = 0.0f;
        shakeTime += 0.1f;
        
        float shakeX = sin(shakeTime * 10.0f) * Options::Aimbot::ShakeIntensity * 0.1f;
        float shakeY = cos(shakeTime * 8.0f) * Options::Aimbot::ShakeIntensity * 0.1f;
        float shakeZ = sin(shakeTime * 12.0f) * Options::Aimbot::ShakeIntensity * 0.1f;
        
        targetPos.x += shakeX;
        targetPos.y += shakeY;
        targetPos.z += shakeZ;
    }

    sCFrame lookAtCFrame = LookAt(camPos, targetPos);

    Vectors::Vector3 rightVec = lookAtCFrame.GetRightVector();
    Vectors::Vector3 upVec = lookAtCFrame.GetUpVector();
    Vectors::Vector3 lookVec = lookAtCFrame.GetLookVector();

    Matrixes::Matrix3x3 rotationMatrix
    {
        rightVec.x, upVec.x, lookVec.x,
        rightVec.y, upVec.y, lookVec.y,
        rightVec.z, upVec.z, lookVec.z
    };

    Vectors::Vector4 currentQuat = Vectors::Vector4::FromMatrix(currentRotation);
    Vectors::Vector4 targetQuat = Vectors::Vector4::FromMatrix(rotationMatrix);

    // Apply smoothness curve
    float t = ApplySmoothnessCurve(Options::Aimbot::Smoothness, Options::Aimbot::SmoothnessCurve);

    Vectors::Vector4 smoothedQuat = Vectors::Vector4::Slerp(currentQuat, targetQuat, t);
    Matrixes::Matrix3x3 smoothedMatrix = smoothedQuat.ToMatrix();

    Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, smoothedMatrix);
}

inline void Mouse(const Vectors::Vector2& targetPos, const POINT& p)
{
    static float accumulatedX = 0.0f;
    static float accumulatedY = 0.0f;

    float dx = static_cast<float>(targetPos.x - p.x);
    float dy = static_cast<float>(targetPos.y - p.y);

    // Add shake if enabled
    if (Options::Aimbot::Shake && Options::Aimbot::ShakeIntensity > 0.0f)
    {
        static float shakeTime = 0.0f;
        shakeTime += 0.1f;
        
        float shakeX = sin(shakeTime * 10.0f) * Options::Aimbot::ShakeIntensity * 0.5f;
        float shakeY = cos(shakeTime * 8.0f) * Options::Aimbot::ShakeIntensity * 0.5f;
        
        dx += shakeX;
        dy += shakeY;
    }

    // Apply smoothness curve
    float t = ApplySmoothnessCurve(Options::Aimbot::Smoothness, Options::Aimbot::SmoothnessCurve);
    
    // Scale for mouse movement (higher = faster)
    float speedScale = 50.0f;
    t = t * speedScale;

    float moveX = dx * t;
    float moveY = dy * t;

    accumulatedX += moveX;
    accumulatedY += moveY;

    int intMoveX = static_cast<int>(accumulatedX);
    int intMoveY = static_cast<int>(accumulatedY);

    accumulatedX -= intMoveX;
    accumulatedY -= intMoveY;

    if (intMoveX != 0 || intMoveY != 0)
    {
        SetCursorPos(p.x + intMoveX, p.y + intMoveY);
    }
}

inline void SpoofMousePosition(const Vectors::Vector2& screenPos)
{
    if (!Globals::Roblox::DataModel.address)
        return;

    auto mouseService = Globals::Roblox::DataModel.FindFirstChildWhichIsA("MouseService");
    if (!mouseService.address)
        return;

    uintptr_t inputObject = Memory->read<uintptr_t>(mouseService.address + Offsets::MouseService::InputObject);
    if (!inputObject)
        inputObject = Memory->read<uintptr_t>(mouseService.address + Offsets::MouseService::InputObject2);

    if (inputObject)
        Memory->write<Vectors::Vector2>(inputObject + Offsets::MouseService::MousePosition, screenPos);
}

inline bool ShouldUseSilentAim()
{
    return Options::Aimbot::AimingType == 2 || Options::Aimbot::SilentAim;
}

inline void MouseSendInput(const Vectors::Vector2& targetPos, const POINT& currentPos, float sensitivity)
{
    if (currentPos.x == targetPos.x && currentPos.y == targetPos.y)
        return;

    static float accumulatedX = 0.0f;
    static float accumulatedY = 0.0f;

    float dx = static_cast<float>(targetPos.x - currentPos.x);
    float dy = static_cast<float>(targetPos.y - currentPos.y);

    // Add shake if enabled
    if (Options::Aimbot::Shake && Options::Aimbot::ShakeIntensity > 0.0f)
    {
        static float shakeTime = 0.0f;
        shakeTime += 0.1f;
        
        float shakeX = sin(shakeTime * 10.0f) * Options::Aimbot::ShakeIntensity * 0.5f;
        float shakeY = cos(shakeTime * 8.0f) * Options::Aimbot::ShakeIntensity * 0.5f;
        
        dx += shakeX;
        dy += shakeY;
    }

    // Apply smoothness curve
    float t = ApplySmoothnessCurve(Options::Aimbot::Smoothness, Options::Aimbot::SmoothnessCurve);

    float sensitivityScale = 1.0f / (sensitivity + 0.2f);
    float speedScale = 0.5f;

    float moveX = dx * t * sensitivityScale * speedScale;
    float moveY = dy * t * sensitivityScale * speedScale;

    accumulatedX += moveX;
    accumulatedY += moveY;

    int intMoveX = static_cast<int>(accumulatedX);
    int intMoveY = static_cast<int>(accumulatedY);

    if (std::abs(dx) < 1.0f && std::abs(dy) < 1.0f)
    {
        accumulatedX = 0.0f;
        accumulatedY = 0.0f;
        return;
    }

    accumulatedX -= intMoveX;
    accumulatedY -= intMoveY;

    if (intMoveX != 0 || intMoveY != 0)
    {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = intMoveX;
        input.mi.dy = intMoveY;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;

        SendInput(1, &input, sizeof(INPUT));
    }
}

inline void RunAimbot(ImDrawList* drawList)
{
    // Check if aimbot is enabled first
    if (!Options::Aimbot::Aimbot)
        return;

    auto localTeam = Globals::Roblox::LocalPlayer.Team();
    auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
    auto Dimensions = Memory->read<Vectors::Vector2>(Globals::Roblox::VisualEngine + Offsets::VisualEngine::Dimensions);

    if (Globals::Caches::CachedPlayerObjects.empty())
        return;

    POINT p;
    GetCursorPos(&p);

    HWND robloxWindow = FindWindowA("Roblox", nullptr);
    if (robloxWindow)
    {
        ScreenToClient(robloxWindow, &p);
    }

    int CombatType;
    
    bool yAxisCheck;

    if (Dimensions.x < GetSystemMetrics(SM_CXSCREEN) || Dimensions.y < GetSystemMetrics(SM_CYSCREEN))
    {
        yAxisCheck = (p.y - Dimensions.y / 2) <= 25; // windowed mode
    }
    else
    {
        yAxisCheck = p.y == Dimensions.y / 2;
    }

    if (p.x == Dimensions.x / 2 && yAxisCheck)
    {                                          //likely in first person
        CombatType = 0; // FPS
    }
    else
    {
        CombatType = 1; // TPS
    }

    ImColor FOVColor = IM_COL32(
        static_cast<int>(Options::Aimbot::FOVColor[0] * 255.f),
        static_cast<int>(Options::Aimbot::FOVColor[1] * 255.f),
        static_cast<int>(Options::Aimbot::FOVColor[2] * 255.f),
        255);

    ImColor FOVFillColor = IM_COL32(
        static_cast<int>(Options::Aimbot::FOVFillColor[0] * 255.f),
        static_cast<int>(Options::Aimbot::FOVFillColor[1] * 255.f),
        static_cast<int>(Options::Aimbot::FOVFillColor[2] * 255.f),
        static_cast<int>(Options::Aimbot::FOVFillColor[3] * 255.f));

    ImVec2 fovCenter(static_cast<float>(p.x), static_cast<float>(p.y));
    if (Options::Aimbot::FOVPositionMode == 1)
    {
        RobloxPlayer fovTarget = Options::Aimbot::CurrentTarget;
        if (fovTarget.address == 0)
            fovTarget = GetClosestPlayer();

        if (fovTarget.address != 0)
        {
            const auto targetScreen = WorldToScreen(GetTargetPosition(fovTarget));
            if (targetScreen.x != -1.f && targetScreen.y != -1.f)
                fovCenter = ImVec2(targetScreen.x, targetScreen.y);
        }
    }

    if (Options::Aimbot::FOV && Options::Aimbot::ShowFOV)
    {
        drawList->AddCircle(fovCenter, Options::Aimbot::FOV, FOVColor, 0, Options::Aimbot::FOVThickness);
        if (Options::Aimbot::ShowFOVFill)
            drawList->AddCircleFilled(fovCenter, Options::Aimbot::FOV, FOVFillColor, 0);
    }

    // Toggle mode: detect key press edge (only trigger once per press)
    static bool wasKeyPressed = false;
    bool isKeyPressed = KeyBind::IsPressed(Options::Aimbot::AimbotKey);
    
    if (Options::Aimbot::ToggleType == 1)
    {
        // Toggle mode: only toggle on key press edge (not while held)
        if (isKeyPressed && !wasKeyPressed)
        {
            Options::Aimbot::Toggled = !Options::Aimbot::Toggled;
        }
        wasKeyPressed = isKeyPressed;
        
        // In toggle mode, check if toggled state is active
        if (!Options::Aimbot::Toggled)
        {
            Options::Aimbot::CurrentTarget = RobloxPlayer(0);
            return;
        }
    }
    else
    {
        // Hold mode: check if key is currently pressed
        if (!isKeyPressed)
        {
            Options::Aimbot::CurrentTarget = RobloxPlayer(0);
            Options::Aimbot::Toggled = false; // Reset toggle state when in hold mode
            return;
        }
    }

    // Stutter logic: skip aiming every X ticks
    static int stutterTickCounter = 0;
    if (Options::Aimbot::Stutter && Options::Aimbot::StutterTicks > 0)
    {
        stutterTickCounter++;
        if (stutterTickCounter >= Options::Aimbot::StutterTicks)
        {
            stutterTickCounter = 0;
            return; // Skip this tick
        }
    }
    else
    {
        stutterTickCounter = 0;
    }

    RobloxPlayer target;
    if (Options::Aimbot::StickyAim)
    {
        if (Options::Aimbot::CurrentTarget.address == 0 ||
            Options::Aimbot::CurrentTarget.Health == 0 ||
            (Options::Aimbot::CurrentTarget.Health <= 1 && Options::Aimbot::DownedCheck) ||
            (Options::Aimbot::TeamCheck && IsTeammate(Options::Aimbot::CurrentTarget)))
        {
            Options::Aimbot::CurrentTarget = GetClosestPlayer();
        }
        else
        {
            // Check if current target is still within range
            auto targetPos = GetTargetPosition(Options::Aimbot::CurrentTarget);
            Vectors::Vector3 diff = targetPos - localHRP.Position();
            float distance3D = diff.Magnitude();
            
            if (distance3D > Options::Aimbot::Range)
            {
                Options::Aimbot::CurrentTarget = GetClosestPlayer();
            }
        }

        target = Options::Aimbot::CurrentTarget;
    }
    else
    {
        target = GetClosestPlayer();
    }

    auto sensitivity = Memory->read<float>(Memory->getBaseAddress() + Offsets::MouseService::SensitivityPointer);

    if (target.address != 0)
    {
        auto targetPos = WorldToScreen(GetTargetPosition(target));

        if (targetPos.x != -1 && targetPos.y != -1)
        {
            switch (CombatType)
            {
            case 0:
            {
                if (ShouldUseSilentAim())
                {
                    CameraRotation(target);
                    if (Options::Aimbot::SilentAimMode == 1)
                        SpoofMousePosition(targetPos);
                }
                else
                {
                    switch (Options::Aimbot::AimingType)
                    {
                    case 0:
                        CameraRotation(target);
                        break;
                    case 1:
                        MouseSendInput(targetPos, p, sensitivity);
                        break;
                    case 2:
                        CameraRotation(target);
                        if (Options::Aimbot::SilentAimMode == 1)
                            SpoofMousePosition(targetPos);
                        break;
                    default:
                        break;
                    }
                }
                break;
            }

            case 1:
            {
                if (ShouldUseSilentAim())
                {
                    CameraRotation(target);
                    if (Options::Aimbot::SilentAimMode == 1)
                        SpoofMousePosition(targetPos);
                }
                else
                {
                    Mouse(targetPos, p);
                }
                break;
            }

            default:
                break;
            }

        }
    }
}



