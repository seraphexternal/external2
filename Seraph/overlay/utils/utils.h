#ifndef UTILS_H
#define UTILS_H

inline float CustomClamp(float value, float min, float max) {
    return (value < min) ? min : (value > max) ? max : value;
}

#endif // UTILS_H

// Premium minimal theme - soft hot pink accent
// Background: matte charcoal (#0F0F12 to #16161A)
// Accent: soft hot pink (#FF5AA5)
inline ImVec4 main_color = ImVec4(1.0f, 0.353f, 0.647f, 1.0f); // #FF5AA5
inline ImVec4 main_color2 = ImVec4(0.353f, 0.647f, 1.0f, 1.0f); // gradient end accent

// Theme color constants
namespace Theme {
    // Backgrounds
    inline constexpr float BgBase[4]      = { 0.059f, 0.059f, 0.071f, 1.0f };  // #0F0F12
    inline constexpr float BgPanel[4]     = { 0.078f, 0.078f, 0.094f, 0.85f };  // #141417
    inline constexpr float BgPanelSolid[4]= { 0.086f, 0.086f, 0.102f, 1.0f };   // #16161A
    inline constexpr float BgDark[4]      = { 0.047f, 0.047f, 0.059f, 1.0f };   // #0C0C0F
    inline constexpr float BgHeader[4]    = { 0.067f, 0.067f, 0.078f, 1.0f };   // #111114

    // Borders
    inline constexpr float Border[4]      = { 1.0f, 1.0f, 1.0f, 0.06f };
    inline constexpr float BorderHover[4] = { 1.0f, 1.0f, 1.0f, 0.12f };

    // Text
    inline constexpr float TextPrimary[4]   = { 1.0f, 1.0f, 1.0f, 0.92f };
    inline constexpr float TextSecondary[4] = { 1.0f, 1.0f, 1.0f, 0.40f };
    inline constexpr float TextMuted[4]     = { 1.0f, 1.0f, 1.0f, 0.25f };

    // Accent
    inline constexpr float Accent[4]       = { 1.0f, 0.353f, 0.647f, 1.0f };    // #FF5AA5
    inline constexpr float AccentDim[4]    = { 1.0f, 0.353f, 0.647f, 0.15f };
    inline constexpr float AccentGlow[4]   = { 1.0f, 0.353f, 0.647f, 0.30f };

    // Animations
    constexpr float AnimFast   = 180.0f;  // ms
    constexpr float AnimNormal = 200.0f;
    constexpr float AnimSlow   = 220.0f;
}
