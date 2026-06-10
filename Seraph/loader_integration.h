#pragma once
#include <atomic>

// Global flag to control loader -> cheat transition
namespace LoaderState
{
    inline std::atomic<bool> authenticated = false;
    inline std::atomic<bool> should_exit_loader = false;
}
