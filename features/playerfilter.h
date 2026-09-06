#pragma once
#include "../rbx/globals/options.h"
#include <string>
#include <vector>
#include <algorithm>

// Pure lookup helpers for the player filter system (focus / exclude / friends).
// All matching is done by Roblox username because that is the only stable
// identifier we have across cache rebuilds / rejoins / restarts.

namespace PlayerFilter
{
    // Highest mark precedence for a name: Exclude beats Focus. A name that is
    // both focused and excluded effectively resolves to excluded.
    inline int GetMark(const std::string& name)
    {
        int result = Options::PlayerFilter::None;
        for (const auto& e : Options::PlayerFilter::Entries)
        {
            if (e.name == name)
            {
                if (e.mark == Options::PlayerFilter::Exclude)
                    return Options::PlayerFilter::Exclude;
                if (e.mark == Options::PlayerFilter::Focus)
                    result = Options::PlayerFilter::Focus;
            }
        }
        return result;
    }

    inline void SetMark(const std::string& name, int mark)
    {
        // Clear entirely.
        if (mark == Options::PlayerFilter::None)
        {
            Options::PlayerFilter::Entries.erase(
                std::remove_if(Options::PlayerFilter::Entries.begin(), Options::PlayerFilter::Entries.end(),
                               [&](const Options::PlayerFilter::Entry& e) { return e.name == name; }),
                Options::PlayerFilter::Entries.end());
            return;
        }

        for (auto& e : Options::PlayerFilter::Entries)
        {
            if (e.name == name)
            {
                e.mark = mark;
                return;
            }
        }
        Options::PlayerFilter::Entry n;
        n.name = name;
        n.mark = mark;
        Options::PlayerFilter::Entries.push_back(n);
    }

    inline bool IsFriend(const std::string& name)
    {
        return std::find(Options::PlayerFilter::Friends.begin(),
                         Options::PlayerFilter::Friends.end(), name)
               != Options::PlayerFilter::Friends.end();
    }

    inline void AddFriend(const std::string& name)
    {
        if (name.empty() || IsFriend(name))
            return;
        Options::PlayerFilter::Friends.push_back(name);
    }

    inline void RemoveFriend(const std::string& name)
    {
        Options::PlayerFilter::Friends.erase(
            std::remove(Options::PlayerFilter::Friends.begin(),
                        Options::PlayerFilter::Friends.end(), name),
            Options::PlayerFilter::Friends.end());
    }

    // True when the player should be hidden from ESP and skipped by the aimbot.
    inline bool IsExcluded(const std::string& name)
    {
        if (Options::PlayerFilter::ExcludeFriends && IsFriend(name))
            return true;
        return GetMark(name) == Options::PlayerFilter::Exclude;
    }

    inline bool IsFocused(const std::string& name)
    {
        return GetMark(name) == Options::PlayerFilter::Focus;
    }

    inline bool HasFocus()
    {
        for (const auto& e : Options::PlayerFilter::Entries)
            if (e.mark == Options::PlayerFilter::Focus)
                return true;
        return false;
    }

    // True when the aimbot is allowed to pick this player. Returns true when the
    // whole system is disabled (callers never need to special-case the toggle).
    inline bool AimbotAllowed(const std::string& name)
    {
        if (!Options::PlayerFilter::Enabled)
            return true;
        if (IsExcluded(name))
            return false;
        if (Options::PlayerFilter::FocusOnly && HasFocus())
            return IsFocused(name);
        return true;
    }

    // True when the player should be drawn by ESP.
    inline bool EspVisible(const std::string& name)
    {
        if (!Options::PlayerFilter::Enabled)
            return true;
        return !IsExcluded(name);
    }
}
