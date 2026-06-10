#ifndef UTILS_H
#define UTILS_H

inline float CustomClamp(float value, float min, float max) {
    return (value < min) ? min : (value > max) ? max : value;
}

#endif // UTILS_H

// Menu accent color - dynamically updated from Options::Misc::MenuAccentColor
inline ImVec4 main_color = ImVec4(1.0f, 0.41f, 0.71f, 1.0f); // Default pink (255, 105, 180)
