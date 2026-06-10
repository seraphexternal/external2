#pragma once
#include "globals.h"

namespace Options
{
	namespace Misc
	{
		inline bool Bypass = false;
		inline bool FOVEnabled = false;
		inline float FOV = 70.f;
		inline bool CacheNPCs = false;
		inline bool KeybindList = false;
		inline float KeybindListX = 20.0f;
		inline float KeybindListY = 80.0f;
		inline bool StreamProof = true;
		inline int MenuKey = VK_INSERT;
		inline float MenuAccentColor[3] = {1.0f, 0.41f, 0.71f}; // Pink (255, 105, 180) converted to 0-1 range
	}
	namespace HitboxExpander
	{
		inline bool Enabled = false;
		inline float HorizontalSize = 10.0f;
		inline float VerticalSize = 10.0f;
		inline bool ShowHitbox = false;
		inline float HitboxTransparency = 0.5f;
		inline bool WalkThrough = false;
	}
	namespace ESP
	{
		inline bool Enabled = true;
		inline int ESPKey = 0;
		inline int ToggleType = 1;
		inline bool Toggled = true;
		
		inline bool TeamCheck = false;
		inline int BoxType = 1; // 0 = None, 1 = Normal Box, 2 = 3D Box
		inline bool Tracers = true;
		inline int TracersStart = 0;
		inline bool Skeleton = true;
		inline bool Name = true;
		inline bool Distance = true;
		inline bool Health = true;
		inline bool HeadCircle = true;
		inline bool HeadDot = true;
		inline bool CornerESP = true;
		inline bool HealthText = true;
		inline bool EnemyHealthIndicator = true;
		inline bool ESPPreview = true;
		inline bool Headless = false;

		inline float Color[3] = {1.0f, 1.0f, 1.0f};
		inline float BoxColor[3] = {1.0f, 1.0f, 1.0f};
		inline float CornerColor[3] = {1.0f, 1.0f, 1.0f};
		inline float SkeletonColor[3] = {1.0f, 1.0f, 1.0f};
		inline float DistanceColor[3] = {1.0f, 1.0f, 1.0f};
		inline float TracerColor[3] = {1.0f, 1.0f, 1.0f};
		inline float TracerThickness = 1.0f;
		inline float BoxThickness = 1.0f;
		inline float SkeletonThickness = 1.0f;
		inline float ESP3DThickness = 1.0f;
		inline float NameSize = 13.0f;
		inline float NameThickness = 1.0f;
		inline bool RemoveBorders = false;
		inline float ESP3DColor[3] = {1.0f, 1.0f, 1.0f};
		inline float HeadCircleColor[3] = {1.0f, 1.0f, 1.0f};
		inline float HeadDotColor[3] = {1.0f, 1.0f, 1.0f};
		inline float HeadCircleThickness = 1.0f;
		inline float HeadCircleScale = 0.10f; // Fraction of on-screen box height
		inline float HeadCircleMaxScale = 2.5f; // Legacy config compat (unused for radius)

		inline bool VisibilityCheck = true;
		inline float MaxRenderDistance = 2000.f; // Maximum distance for ESP rendering
		inline bool VisibilityChams = true;
		inline float VisibilityMaxDistance = 450.f;
		inline float VisibleColor[3] = {0.35f, 1.0f, 0.45f};
		inline float HiddenColor[3] = {1.0f, 0.30f, 0.30f};

		inline float ChamsColor[3] = {1.0f, 1.0f, 1.0f}; // White color
	}
	namespace Aimbot
	{
		inline int AimbotKey = 0;
		inline int AimingType = 0;

		inline int ToggleType = 0;

		inline bool Aimbot = false;
		inline bool TeamCheck = false;
		inline bool DownedCheck = false;
		inline bool StickyAim = false;
		inline float FOV = 100.f;
		inline float Smoothness = 0.f;
		inline int SmoothnessCurve = 0; // 0=Linear, 1=Ease In, 2=Ease Out, 3=Ease In-Out, 4=Custom
		inline bool CustomCurveEnabled = false;
		inline float CustomCurveP1[2] = {0.25f, 0.25f}; // First control point
		inline float CustomCurveP2[2] = {0.75f, 0.75f}; // Second control point
		inline bool ShowFOV = false;
		inline bool ShowFOVFill = false;
		inline int FOVPositionMode = 0; // 0 = screen center, 1 = follow target
		inline bool SilentAim = false;
		inline int SilentAimMode = 0; // 0 = camera (no cursor), 1 = mouse spoof
		inline float Range = 100.f;

		inline float FOVColor[3] = {1.0f, 1.0f, 1.0f}; // White color
		inline float FOVFillColor[4] = {1.0f, 1.0f, 1.0f, 0.1f}; // White with transparency
		inline float FOVThickness = 1.0f;
		
		inline int TargetBone = 0;
		inline int AirTargetBone = 0; // Air part selection
		
		inline bool Prediction = false;
		inline float PredictionX = 1.0f;
		inline float PredictionY = 1.0f;
		
		inline bool Shake = false;
		inline float ShakeIntensity = 1.0f;
		
		inline bool Stutter = false;
		inline int StutterTicks = 5;
		
		inline RobloxPlayer CurrentTarget;
		inline bool Toggled = false;
	}
	namespace Triggerbot
	{
		inline int TriggerbotKey = 0;
		inline int ToggleType = 0;

		inline bool Enabled = false;
		inline bool TeamCheck = false;
		inline bool DownedCheck = false;
		inline float Radius = 15.f;
		inline float Range = 100.f;
		inline int Delay = 50;
		inline bool Prediction = false;
		inline float PredictionX = 1.0f;
		inline float PredictionY = 1.0f;
		
		inline bool AdvancedFOV = false;
		inline bool ShowAdvancedFOV = false;
		
		// Advanced FOV per body part (X = horizontal, Y = vertical)
		inline float HeadFOV_X = 0.0f;
		inline float HeadFOV_Y = 0.0f;
		inline float TorsoFOV_X = 0.0f;
		inline float TorsoFOV_Y = 0.0f;
		inline float UpperTorsoFOV_X = 0.0f;
		inline float UpperTorsoFOV_Y = 0.0f;
		inline float LowerTorsoFOV_X = 0.0f;
		inline float LowerTorsoFOV_Y = 0.0f;
		inline float LeftUpperArmFOV_X = 0.0f;
		inline float LeftUpperArmFOV_Y = 0.0f;
		inline float LeftLowerArmFOV_X = 0.0f;
		inline float LeftLowerArmFOV_Y = 0.0f;
		inline float LeftHandFOV_X = 0.0f;
		inline float LeftHandFOV_Y = 0.0f;
		inline float RightUpperArmFOV_X = 0.0f;
		inline float RightUpperArmFOV_Y = 0.0f;
		inline float RightLowerArmFOV_X = 0.0f;
		inline float RightLowerArmFOV_Y = 0.0f;
		inline float RightHandFOV_X = 0.0f;
		inline float RightHandFOV_Y = 0.0f;
		inline float LeftUpperLegFOV_X = 0.0f;
		inline float LeftUpperLegFOV_Y = 0.0f;
		inline float LeftLowerLegFOV_X = 0.0f;
		inline float LeftLowerLegFOV_Y = 0.0f;
		inline float LeftFootFOV_X = 0.0f;
		inline float LeftFootFOV_Y = 0.0f;
		inline float RightUpperLegFOV_X = 0.0f;
		inline float RightUpperLegFOV_Y = 0.0f;
		inline float RightLowerLegFOV_X = 0.0f;
		inline float RightLowerLegFOV_Y = 0.0f;
		inline float RightFootFOV_X = 0.0f;
		inline float RightFootFOV_Y = 0.0f;
		
		inline bool Toggled = false;
	}
	namespace Macro
	{
		inline int MacroKey = 0;
		inline int ToggleType = 0;

		inline bool Enabled = false;
		inline int Delay = 100;
		
		inline bool Toggled = false;
	}
	namespace Crosshair
	{
		inline bool Enabled = false;
		inline int Style = 0; // 0 = Static, 1 = Pulse
		inline float Size = 10.0f;
		inline float Gap = 5.0f;
		inline float Thickness = 2.0f;
		inline float SpinSpeed = 50.0f;
		inline float GapSpeed = 1.0f;
		inline bool GapTween = false;
		inline bool ShowText = true;
		inline float Color[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // White with full alpha
	}
	namespace Fly
	{
		inline int FlyKey = 0;
		inline int ToggleType = 0;
		
		inline bool Enabled = false;
		inline float Speed = 50.0f;
		
		inline bool Toggled = false;
	}
	namespace WalkSpeed
	{
		inline int WalkSpeedKey = 0;
		inline int ToggleType = 0;
		
		inline bool Enabled = false;
		inline float Speed = 16.0f;
		
		inline bool Toggled = false;
	}

	namespace Combat
	{
		inline bool HitSounds = false;
		inline int HitSoundType = 0; // 0 click, 1 bell, 2 bass
		inline bool HitNotifications = false;
		inline bool HitChams = false;
		inline bool HitEffects = false;
		inline float HitChamsDuration = 0.4f;
		inline float HitEffectDuration = 0.6f;
		inline float MinDamage = 1.0f;
		inline float HitChamsColor[3] = { 1.0f, 0.35f, 0.35f };
		inline float HitEffectColor[3] = { 1.0f, 0.5f, 0.2f };
	}

	namespace World
	{
		inline bool Enabled = false;
		inline bool Fullbright = false;
		inline bool NoFog = false;
		inline float FogStart = 0.0f;
		inline float FogEnd = 500.0f;
		inline float ClockTime = 14.0f;
		inline float Brightness = 1.0f;
		inline float Ambient[3] = { 0.5f, 0.5f, 0.5f };
		inline float OutdoorAmbient[3] = { 0.5f, 0.5f, 0.5f };
		inline float FogColor[3] = { 0.75f, 0.75f, 0.75f };
		inline bool SkyboxChanger = false;
		inline int SkyboxPreset = 0; // 0 default, 1 night, 2 space, 3 sunset, 4 storm
	}

	namespace AntiAim
	{
		inline bool Enabled = false;
		inline int Mode = 0; // 0 spin, 1 jitter, 2 random
		inline float Speed = 12.0f;
		inline float Strength = 35.0f;
	}
}
