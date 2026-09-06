#pragma once
#include <windows.h>
#include <thread>
#include <chrono>
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../overlay/imgui/KeyBind.h"

// AutoClicker — sends mouse clicks at a configurable rate (CPS). Works as a
// hold / toggle / always-on feature. Optionally only fires while the user is
// physically holding the left mouse button (so it won't click the cheat menu).
inline void AutoClickerLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (!Options::Autoclicker::Enabled)
            continue;

        bool active = false;
        if (Options::Autoclicker::ToggleType == 2) // Always On
            active = true;
        else if (Options::Autoclicker::ToggleType == 1) // Toggle
        {
            static bool wasKey = false;
            bool isKey = KeyBind::IsPressed(Options::Autoclicker::Key);
            if (isKey && !wasKey)
                Options::Autoclicker::Toggled = !Options::Autoclicker::Toggled;
            wasKey = isKey;
            active = Options::Autoclicker::Toggled;
        }
        else // Hold
        {
            active = KeyBind::IsPressed(Options::Autoclicker::Key);
        }

        if (!active)
            continue;

        if (Options::Autoclicker::OnlyOnHold &&
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0)
            continue;

        float cps = Options::Autoclicker::CPS;
        if (cps < 1.f) cps = 1.f;
        int intervalMs = static_cast<int>(1000.f / cps);
        if (intervalMs < 1) intervalMs = 1;

        static auto lastClick = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClick).count();
        if (elapsed >= intervalMs)
        {
            lastClick = now;
            DWORD down = Options::Autoclicker::RightClick ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
            DWORD up   = Options::Autoclicker::RightClick ? MOUSEEVENTF_RIGHTUP   : MOUSEEVENTF_LEFTUP;
            INPUT in = {};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = down;
            SendInput(1, &in, sizeof(INPUT));
            in.mi.dwFlags = up;
            SendInput(1, &in, sizeof(INPUT));
        }
    }
}
