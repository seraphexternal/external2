#pragma once
#include "../rbx/globals/options.h"
#include "../overlay/imgui/KeyBind.h"
#include <windows.h>
#include <chrono>

inline void RunMacro()
{
    if (!Options::Macro::Enabled)
        return;

    // Check keybind
    static bool wasKeyPressed = false;
    bool isKeyPressed = KeyBind::IsPressed(Options::Macro::MacroKey);
    
    if (Options::Macro::ToggleType == 1)
    {
        // Toggle mode
        if (isKeyPressed && !wasKeyPressed)
        {
            Options::Macro::Toggled = !Options::Macro::Toggled;
        }
        wasKeyPressed = isKeyPressed;
        
        if (!Options::Macro::Toggled)
            return;
    }
    else
    {
        // Hold mode
        if (!isKeyPressed)
        {
            Options::Macro::Toggled = false;
            return;
        }
    }

    // Spam I and O keys with delay
    static auto lastPressTime = std::chrono::steady_clock::now();
    static bool pressI = true; // Alternate between I and O
    
    auto currentTime = std::chrono::steady_clock::now();
    auto timeSinceLastPress = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastPressTime).count();

    if (timeSinceLastPress >= Options::Macro::Delay)
    {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        
        if (pressI)
        {
            // Press I key
            input.ki.wVk = 0x49; // 'I' key
            input.ki.dwFlags = 0; // Key down
            SendInput(1, &input, sizeof(INPUT));
            
            Sleep(20);
            
            input.ki.dwFlags = KEYEVENTF_KEYUP; // Key up
            SendInput(1, &input, sizeof(INPUT));
        }
        else
        {
            // Press O key
            input.ki.wVk = 0x4F; // 'O' key
            input.ki.dwFlags = 0; // Key down
            SendInput(1, &input, sizeof(INPUT));
            
            Sleep(20);
            
            input.ki.dwFlags = KEYEVENTF_KEYUP; // Key up
            SendInput(1, &input, sizeof(INPUT));
        }
        
        pressI = !pressI; // Alternate
        lastPressTime = currentTime;
    }
}
