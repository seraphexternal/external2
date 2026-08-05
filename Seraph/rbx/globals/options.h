#pragma once
#include "globals.h"

namespace Options
{
	namespace Misc
	{
		inline int MenuFont = 0; // index into the pre-loaded font array in renderer.cpp
		inline bool FOVEnabled = false;
		inline float FOV = 70.f;
		inline bool CacheNPCs = false;
		inline bool KeybindList = false;
		inline float KeybindListX = 20.0f;
		inline float KeybindListY = 80.0f;
		inline bool StreamProof = true;
		inline int MenuKey = VK_RSHIFT;
		inline float MenuAccentColor[3] = {0.300f, 0.550f, 1.0f};
		inline bool RainbowAccent = false;
		inline float RainbowSpeed = 1.0f;
		// Second accent used to build a gradient across the menu header / fades.
		inline float MenuAccentColor2[3] = {0.200f, 0.400f, 0.85f};
		// When true the header fade and tab underline blend between the two accents.
		inline bool MenuGradient = false;
		// 0 = Custom (use the colors below); 1..N select a built-in preset theme.
		inline int MenuTheme = 0;
		inline float MenuBgColor[3] = { 0.031f, 0.031f, 0.031f };      // outer menu background
		inline float MenuPanelColor[3] = { 0.102f, 0.102f, 0.102f };   // inner panel background
		inline char TargetPlayer[32] = "";
		inline bool ExplorerEnabled = false;
		inline float MenuScale = 1.0f;
		inline bool ThirdPerson = false;

		// ── Stealth ──
		inline bool HideFromTabs = true;     // WS_EX_TOOLWINDOW + remove from taskbar
		inline bool HideProcess = true;      // relaunch self as a renamed copy in %TEMP%
		inline char ProcessName[64] = "RuntimeBroker"; // benign-looking spawned process name
		inline char ExclusionPath[256] = ""; // folder the trace-wiper must never touch
		inline bool ShowCertified = true;    // "certified yn" watermark in the menu footer
	}
	namespace Loader
	{
		inline bool AutoAttach = false;         // skip loader, attach immediately when game found
		inline int SelectedTheme = 1;           // index into MenuThemes::Presets (0 = Custom)
		inline int SelectedFont = 0;            // index into MenuFonts array
		inline float SelectedScale = 1.0f;      // menu scale
		inline char AutoloadConfig[128] = "";   // config filename to autoload (empty = none)
		inline bool AttachOnStart = true;       // auto-inject when Roblox detected
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
		inline bool BoxFill = false;
		inline bool BoxFillGradient = false;
		inline int BoxFillType = 0; // 0 = Vertical, 1 = Horizontal, 2 = Four-Corner
		inline bool BoxFillGradientRotate = false;
		inline float BoxFillSpeed = 2.0f;
		inline float BoxFillColor[4] = { 0.0f, 0.5f, 1.0f, 0.5f };
		inline float BoxFillTopColor[4] = { 0.96f, 0.71f, 0.96f, 0.5f };
		inline float BoxFillBottomColor[4] = { 0.0f, 0.0f, 0.0f, 0.5f };
		inline bool Tracers = true;
		inline int TracersStart = 0;
		inline bool Skeleton = true;
		inline bool Name = true;
		inline bool Distance = true;
		inline bool Health = true;
		inline bool GradientHealthbar = false;
		inline float HealthbarTopColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
		inline float HealthbarMiddleColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
		inline float HealthbarBottomColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		inline bool HeadCircle = true;
		inline bool HeadDot = true;
		inline bool CornerESP = true;
		inline bool HealthText = true;
		inline bool EnemyHealthIndicator = true;
		inline bool ESPPreview = true;
		inline bool PreviewAutoRotate = false;
		inline float PreviewRotationSpeed = 45.0f;
		inline bool Headless = false;
		inline bool ShowWeapon = true;
		inline bool RigType = false;
		inline float RigTypeColor[3] = { 1.0f, 1.0f, 1.0f };

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
		inline float HeadCircleScale = 0.10f;

		inline bool VisibilityCheck = true;
		inline float MaxRenderDistance = 2000.f;
		inline bool VisibilityChams = true;
		inline float VisibilityMaxDistance = 450.f;
		inline float VisibleColor[3] = {0.35f, 1.0f, 0.45f};
		inline float HiddenColor[3] = {1.0f, 0.30f, 0.30f};

		inline bool LodLine = false;
		inline float LodLineLength = 200.0f;
		inline float LodLineThickness = 2.0f;
		inline float LodLineColor[3] = { 1.0f, 0.0f, 0.0f };

		inline bool Arrows = false;
		inline float ArrowSize = 12.0f;
		inline float ArrowRadius = 200.0f;
		inline float ArrowThickness = 2.0f;
		inline float ArrowColor[3] = { 1.0f, 1.0f, 1.0f };

		inline bool Radar = false;
		inline float RadarSize = 150.0f;
		inline float RadarRange = 500.0f;
		inline float RadarX = 20.0f;
		inline float RadarY = 200.0f;
		inline float RadarBgColor[3] = { 0.1f, 0.1f, 0.1f };
		inline float RadarEnemyColor[3] = { 1.0f, 0.0f, 0.0f };
		inline float RadarLocalColor[3] = { 0.0f, 1.0f, 0.0f };
		inline int RadarTheme = 0; // 0 = Classic, 1 = Minimal, 2 = Neon, 3 = Compass

		// ---- ESP Effects ----
		inline bool Glow = false;            // outer glow pass on boxes
		inline bool Pulse = false;           // breathing alpha on boxes
		inline float PulseSpeed = 1.0f;
		inline bool Rings = false;           // circular ring around the player
		inline float RingRadius = 40.0f;
		inline bool Trails = false;          // motion trail of recent positions
		inline int TrailLength = 20;
		inline bool LocalOnly = false;       // effects only drawn on the local player
        inline bool AvatarIcon = false;      // draw the player's avatar thumbnail by the name
        inline int NameMode = 0;             // 0 = Username, 1 = Health%
        inline bool CustomImage = false;     // draw a custom PNG near the player
		inline char CustomImagePath[256] = { 0 };
		inline float CustomImageScale = 1.0f;
	}
	namespace Aimbot
	{
		inline int AimbotKey = 0;
		inline int AimingType = 0;

		inline int ToggleType = 0;

		inline bool Aimbot = false;
		inline bool TeamCheck = false;
		inline bool DownedCheck = false;
		inline bool WallCheck = false;
		inline bool StickyAim = false;
		inline bool IgnoreJump = false;
		inline float JumpThreshold = 20.0f;
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
		// On Chickynoid/Overkill the in-game shot ignores the camera/viewport
		// memory tricks, so to make bullets actually land we snap the REAL cursor
		// onto the target while firing. That swings your view onto the enemy (a
		// visible "flick"). Turn this off to keep your aim steady, but then shots
		// won't connect on Overkill.
		inline bool SilentAimRealCursor = true;
		// Experimental: instead of snapping the real cursor, briefly teleport your
		// real character next to the target so the hit registers, then restore it.
		// No crosshair movement, but may flick for ~1 frame and can desync if the
		// game's hit detection isn't character-position based.
		inline bool SilentAimTeleport = false;
		inline float Range = 100.f;

		// Silent Lock: hold to silently keep the camera aimed at the locked target
		// without moving the real cursor or flicking the view (camera-rotation write).
		inline bool SilentLock = false;
		inline int SilentLockKey = 0;
		inline int SilentLockMode = 0; // 0 = camera rotation, 1 = viewport offset

		// --- Raycast Silent Aim (new) ---
		inline bool SilentAimEnabled = false;
		inline int SilentAimKey = 0;
		inline int SilentAimToggleType = 1; // 0=Hold, 1=Toggle, 2=Always
		inline bool SilentAimToggled = false;
		inline float SilentAimFOV = 100.f;
		inline float SilentAimSmoothness = 0.f; // 0=instant, >0=lerp
		inline bool SilentAimCamera = true; // rotate camera
		inline bool SilentAimMouse = false; // move mouse
		inline bool SilentAimRealCursor2 = true; // use real cursor
		inline int SilentAimTargetBone = 0; // 0=Head, 1=Torso, 2=UpperTorso, 3=LowerTorso
		inline bool SilentAimTeamCheck = true;
		inline bool SilentAimPrediction = false;
		inline float SilentAimPredictionX = 1.0f;
		inline float SilentAimPredictionY = 1.0f;
		inline int SilentAimMethod = 0; // 0=Camera, 1=Mouse, 2=Both

		// Aim Info: draws a small HUD with the current target's name, distance,
		// health and the body part that would be hit.
		inline bool AimInfo = false;
		inline bool AimInfoName = true;
		inline bool AimInfoDistance = true;
		inline bool AimInfoHealth = true;
		inline bool AimInfoPart = true;

		// Flickbot: on key press, performs a fast snap to the best target (one
		// frame) then releases -- a quick "flick" assist without holding the
		// aimbot key.
		inline bool Flickbot = false;
		inline int FlickbotKey = 0;
		inline float FlickbotFOV = 120.f;     // acquisition FOV for the flick
		inline float FlickbotSmoothing = 0.0f; // 0 = instant snap, >0 = quick lerp
		inline bool FlickbotTeamCheck = true;

		inline float FOVColor[3] = {1.0f, 1.0f, 1.0f}; // White color
		inline float FOVFillColor[4] = {1.0f, 1.0f, 1.0f, 0.1f}; // White with transparency
		inline float FOVThickness = 1.0f;
		
		inline int TargetBone = 0;
		inline int AirTargetBone = 0; // Air part selection

		// Targeting priority when multiple enemies are inside FOV.
		// 0 = Closest Part (cursor-nearest body part, the "smart" mode the user
		//     asked for), 1 = Closest to Crosshair, 2 = Lowest Health,
		// 3 = Farthest, 4 = Highest Health.
		inline int TargetPriority = 0;

		// When true and TargetBone is set to a fixed part, the aimbot instead
		// picks the body part nearest the cursor (equivalent to TargetPriority 0
		// with a head fallback). Driven by the "Hitbox Mode" dropdown (0=Fixed Bone, 1=Closest Part).
		inline bool ClosestPart = false;
		// 0 = Fixed Bone (uses Hit Part / Air Hit Part), 1 = Closest Part.
		inline int HitboxMode = 0;

		// 0 = Circle, 1 = Square, 2 = Triangle, 3 = Hexagon FOV zone.
		inline int FOVShape = 0;

		// FOV color rendering mode:
		// 0 = Solid, 1 = Gradient (main->accent2), 2 = Shift/rainbow, 3 = Pulse/Breathing.
		inline int FOVColorMode = 0;
		inline float FOVGradientSpeed = 1.0f;
		inline bool FOVGlow = false;       // draw a soft outer glow pass
		inline bool FOVBreathing = false;  // pulse radius/alpha over time
		inline bool FOVSpin = false;       // rotate the gradient phase
		inline float FOVSpinSpeed = 1.0f;

		// Only lock onto players that are visible (not behind walls).
		inline bool OnlyVisible = false;

		// Delay (ms) before the aimbot is allowed to switch to a new target.
		inline float TargetSwitchDelay = 0.f;

		// Draw the numeric FOV value in the center of the FOV circle.
		inline bool ShowFOVText = false;
		
		inline bool Prediction = false;
		inline float PredictionX = 1.0f;
		inline float PredictionY = 1.0f;
		
		inline bool TargetLine = false;
		inline float TargetLineColor[3] = { 1.0f, 0.0f, 0.0f };
		inline float TargetLineThickness = 1.5f;

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
		inline bool WallCheck = false;
		inline float Radius = 15.f;
		inline float Range = 100.f;
		inline int Delay = 50;
		inline bool Prediction = false;
		inline float PredictionX = 1.0f;
		inline float PredictionY = 1.0f;
		
		inline bool AdvancedFOV = false;
		inline bool ShowAdvancedFOV = false;
		inline bool DynamicFOV = false;
		inline float DynamicFOVScale = 1.0f;
		inline float DynamicFOVBaseDist = 50.f;
		
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
	namespace Ragebot
	{
		inline bool Enabled = false;
		inline int RagebotKey = 0;
		inline int ToggleType = 0;
		inline bool TeamCheck = false;
		inline bool DownedCheck = false;
		inline bool WallCheck = true;
		inline float Range = 150.f;
		inline float FOV = 180.f;
		inline float Smoothness = 0.5f;
		inline int TargetBone = 0;
		inline bool Prediction = false;
		inline float PredictionX = 1.0f;
		inline float PredictionY = 1.0f;
		inline bool AutoFire = true;
		inline int FireRate = 80;
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
		inline int Style = 0; // 0 = Static, 1 = Pulse, 2 = Spin, 3 = Dynamic (spread)
		inline float Size = 10.0f;
		inline float Gap = 5.0f;
		inline float Thickness = 2.0f;
		inline float SpinSpeed = 50.0f;
		inline float GapSpeed = 1.0f;
		inline bool GapTween = false;
		inline bool ShowText = true;
		inline float Color[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // White with full alpha

		// Extended config
		inline bool ShowDot = false;          // center dot
		inline float DotSize = 2.0f;
		inline bool Outline = true;           // black outline on lines/dot
		inline float OutlineThickness = 1.0f;
		inline float OutlineColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		inline bool TStyle = false;           // T-style crosshair
		inline int ColorMode = 0;             // 0 = Static, 1 = Rainbow
		inline float RainbowSpeed = 1.0f;
		inline float Opacity = 1.0f;          // global alpha multiplier
		inline int LengthMode = 0;            // 0 = equal 4 lines, 1 = vertical longer
		inline float VLength = 10.0f;         // vertical line extra length
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
		inline int HitSoundType = 0; // 0 = Custom file, 1 = click, 2 = bell, 3 = bass, 4 = skeet, 5 = neverlose, 6 = rust, 7 = quake, 8 = cod, 9 = bubble, 10 = minecraft, 11 = fatality
		inline char HitSoundFile[256] = "";
		inline float HitSoundVolume = 1.0f;
		inline bool HitNotifications = false;
		inline bool HitChams = false;
		inline bool HitEffects = false;
		inline float HitChamsDuration = 0.4f;
		inline float HitEffectDuration = 0.6f;
		inline float MinDamage = 1.0f;
		inline float HitChamsColor[3] = { 1.0f, 0.35f, 0.35f };
		inline float HitEffectColor[3] = { 1.0f, 0.5f, 0.2f };

		inline bool BulletTracers = false;
		inline bool BulletTracersAlways = false;
		inline float BulletTracerColor[3] = { 1.0f, 0.5f, 0.2f };
		inline float BulletTracerDuration = 1.0f;
		inline float BulletTracerThickness = 1.5f;
		inline int BulletTracerStyle = 0; // 0 solid, 1 glow, 2 dashed, 3 pulse

		// Hitmarker
		inline int HitmarkerStyle = 0;   // 0 = cross, 1 = circle, 2 = dot
		inline float HitmarkerSize = 8.0f;
		inline bool HitmarkerOnCrosshair = false; // draw at screen center instead of on the enemy
		inline float HitmarkerThickness = 1.5f;
	}

	namespace World
	{
		inline bool Enabled = false;
		inline bool Fullbright = false;
		inline bool NoFog = false;
		inline bool NoShadows = false;
		inline bool AutoSunPosition = true;
		inline float FogStart = 0.0f;
		inline float FogEnd = 500.0f;
		inline float ClockTime = 14.0f;
		inline float Brightness = 1.0f;
		inline float Ambient[3] = { 0.5f, 0.5f, 0.5f };
		inline float OutdoorAmbient[3] = { 0.5f, 0.5f, 0.5f };
		inline float FogColor[3] = { 0.75f, 0.75f, 0.75f };
		inline bool SkyboxChanger = false;
		inline int SkyboxPreset = 0; // 0 default, 1-10 skybox presets
		inline bool RotateSkybox = false;
		inline float SkyboxRotateSpeed = 1.0f;
		inline bool Exposure = false;
		inline float ExposureValue = 0.0f;

		// Fang-style separate toggles
		inline bool Ambience = false;
		inline float AmbienceColor[4] = { 0.960784f, 0.709804f, 0.960784f, 1.0f };
		inline bool FogEnabled = false;
		inline float FogDistance = 300.0f;
		inline float FogColor2[4] = { 0.960784f, 0.709804f, 0.960784f, 1.0f };
		inline bool BrightnessEnabled = false;
		inline float BrightnessValue = 1.0f;
	}

	namespace AntiAim
	{
		inline bool Enabled = false;
		inline int Method = 0; // 0 = rotation, 1 = position jitter
		inline int Mode = 0; // 0 spin, 1 jitter, 2 random
		inline float Speed = 12.0f;
		inline float Strength = 35.0f;
	}

	namespace Spin360
	{
		inline bool Enabled = false;
		inline float Speed = 20.0f;
		inline int HotKey = 0;
	}

	namespace TickRate
	{
		inline bool Enabled = false;
		inline float Rate = 60.0f;
	}

namespace Chams
	{
		inline bool Enabled = false;
		inline bool ChamsFade = false;
		inline int ChamsFadeSpeed = 2;
		inline bool TeamCheck = true;
		inline bool GradientFill = false;
		inline bool Wireframe = false;
		inline float WireframeThickness = 1.5f;
		inline bool IncludeAccessories = true;
		inline float FillColor[4] = { 0.960784f, 0.709804f, 0.960784f, 0.5f };
		inline float FillColor2[4] = { 0.0f, 0.0f, 0.0f, 0.5f };
		inline float OutlineColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	}

	namespace Weather
	{
		// Snow / rain particle effect for the menu background. The
		// MenuWeather engine in renderer.cpp reads these each frame
		// via MenuWeather::SyncFromOptions(), so changes here (and
		// from the Misc tab UI) flow through immediately and are
		// saved with the user's config.
		inline bool  Enabled       = false;
		inline int   Type          = 0;     // 0 = snow, 1 = rain
		inline int   Intensity     = 150;   // particle count (clamped 64 - 2000)
		inline float Speed         = 1.0f;  // vertical fall speed multiplier
		inline float Wind          = 0.0f;  // horizontal drift, negative = left
		inline float Color[3]      = { 1.0f, 1.0f, 1.0f };
		inline float SnowSize      = 1.8f;  // pixel radius of each snowflake
		inline float RainThickness = 1.4f;  // pixel width of each rain streak
	}

	namespace Noclip
	{
		inline bool Enabled = false;
		inline int  NoclipKey  = 0;
		inline int  ToggleType = 2; // 0 = Hold, 1 = Toggle, 2 = Always On
		inline bool Toggled    = false;
	}

	namespace Bhop
	{
		inline bool Enabled = false;
		inline int  BhopKey  = 0;
	}

	namespace Rage
	{
		// Behaviour shared by the Rage tab. The "real you" visually orbits the
		// locked target while the actual character is free to walk around;
		// meanwhile an auto-kill keeps damaging the target from the orbit point.
		inline bool Enabled = false;          // master toggle for the rage kill/orbit loop
		inline int  RageKey = 0;              // hold key to engage; 0 = always on when Enabled
		inline int  ToggleType = 2;          // 0 = Hold, 1 = Toggle, 2 = Always On
		inline bool Toggled = false;

		inline float OrbitRadius = 4.0f;     // how far the orbiting "you" sits from the target
		inline float OrbitSpeed = 3.0f;      // orbit angular speed
		inline bool  KillOnOrbit = true;      // auto-kill the target while orbiting
		inline bool  AutoKillAim = true;      // snap camera/aim at the target while killing
		inline int   TargetMode = 0;          // 0 = aimed-at/closest, 1 = by name
		inline char  TargetPlayer[32] = "";

		inline bool  ShowGhost = true;        // draw the orbiting "you" as a ghost
		inline float GhostColor[3] = { 1.0f, 0.2f, 0.2f };
		inline float GhostAlpha = 0.55f;
		inline bool  ShowGhostLine = true;    // line from real you to ghost
	}

	namespace VoidHide
	{
		inline bool Enabled = false;
		inline int  VoidHideKey = 0;
		inline int  ToggleType = 0; // 0 = Hold, 1 = Toggle, 2 = Always On
		inline bool Toggled = false;
	}

	namespace Orbit
	{
		inline bool Enabled    = false;
		inline float Speed     = 2.0f;
		inline float Radius    = 8.0f;
		inline int  OrbitKey   = 0;
		inline int  ToggleType = 2;
		inline bool Toggled    = false;
		inline int  TargetMode = 0;
		inline char TargetPlayer[32] = "";
	}

	namespace ArsenalGunmods
	{
		inline bool FastFireRate = false;
		inline bool NoRecoil = false;
		inline bool AllAuto = false;
		inline bool InfiniteAmmo = false;
	}

	namespace Rivals
	{
		inline bool IgnoreSmoke = false;
		inline bool IgnoreFlash = false;
	}

	namespace RivalsSkinChanger
	{
		inline bool Enabled = false;
		inline char SkinName[64] = "AKEY-47";
		inline char WeaponName[64] = "AssaultRifle";
	}

	namespace Desync
	{
		inline bool Enabled = false;
		inline int DesyncKey = 0;
		inline int ToggleType = 0; // 0 = Hold, 1 = Toggle, 2 = Always On
		inline bool Toggled = false;

		inline int Method = 0; // 0 = Freeze Server, 1 = Velocity Boost
		inline float BoostSpeed = 80.0f;
		inline int BoostAxis = 0; // 0 = Forward, 1 = Up, 2 = Backward

		inline bool ShowVisual = true;
		inline float VisualColor[3] = { 0.0f, 0.5f, 1.0f };
		inline float VisualAlpha = 0.6f;
		inline bool ShowLine = true;
		inline float LineColor[3] = { 1.0f, 0.0f, 0.0f };
	}

	namespace RampFling
	{
		inline bool Enabled = false;
		inline int FlingKey = 0;
		inline int ToggleType = 2; // 0 = Hold, 1 = Toggle, 2 = Always On
		inline bool Toggled = false;
		inline float FlingForce = 80.0f;
		inline float MinAngle = 15.0f;
		inline float MaxAngle = 75.0f;
		inline float Cooldown = 0.3f;
		inline float HorizontalBoost = 0.5f;
	}

	namespace Waypoints
	{
		inline bool Enabled = false;
		inline bool ShowOnESP = true;
		inline float Color[3] = { 0.0f, 1.0f, 0.0f };
		inline int TeleportKey = 0;
	}

	namespace SoundVisualizer
	{
		inline bool Enabled = false;
		inline float Color[3] = { 0.3f, 0.8f, 1.0f };
		inline float Duration = 1.0f;
		inline float Radius = 30.0f;
		inline int MaxSteps = 32;
	}

	namespace ClickTP
	{
		inline bool Enabled = false;
		inline int Key = VK_LBUTTON; // click to teleport (default LMB)
		inline float MaxDistance = 1000.0f;
	}

	namespace HipHeight
	{
		inline bool Enabled = false;
		inline bool Toggled = false;
		inline float Value = 2.0f;
		inline int Key = 0;
		inline int ToggleType = 2;
	}

	namespace FreeCam
	{
		inline bool Enabled = false;
		inline bool Toggled = false;
		inline int Key = 0;
		inline int ToggleType = 0; // 0 = hold, 1 = toggle, 2 = always
		inline float Speed = 50.0f;
		inline bool SaveRealCamera = true;
	}

	namespace StretchRes
	{
		inline bool Enabled = false;
		inline float ScaleX = 1.0f;
		inline float ScaleY = 1.0f;
	}
}
