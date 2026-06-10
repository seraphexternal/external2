#pragma once
#include "imgui.h"
#include <windows.h>

namespace KeyBind {
    inline bool IsPressed(int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

bool IsKeyPressedMap(ImGuiKey key, bool repeat = true);
void KeybindSelector(const char* label, int* key);
bool CheckboxWithColorPicker(const char* label, bool* v, float* color);
bool ColorPickerIcon(const char* id, float* color);


// keybind system check imgui.cpp

// lines 11607 at 11686 imgui.cpp