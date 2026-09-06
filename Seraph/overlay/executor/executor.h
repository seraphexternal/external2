#pragma once
#include <string>
#include <vector>

struct ImDrawList;

// External Luau executor. Runs scripts inside Seraph's own process -- no
// injection. Scripts get a Roblox-style game/draw API backed by the external
// memory caches and the overlay renderer.
//
// Threading model: every public entry point must be called from the overlay
// thread (the one running ImGui). Scripts execute cooperatively -- wait()/
// task.wait() suspend the coroutine and Tick() resumes it once due. A runaway
// loop is capped by an instruction budget in the Luau interrupt hook, which
// forces a yield back to the host each resume so the menu never freezes.
namespace Executor
{
    // Creates the Lua state, opens the standard libraries and registers the
    // game/draw script API. Call once before any other function.
    void Initialize();

    // Destroys the Lua state. Call during overlay shutdown.
    void Shutdown();

    // Compiles `code` and starts executing it on a new coroutine. Any
    // currently running script is stopped first. Errors are appended to the
    // console buffer. Must be called from the overlay thread.
    void Run(const std::string& code);

    // Stops the running script (and any task.spawn'd coroutines).
    void Stop();

    // True while a script coroutine is loaded (running or suspended).
    bool IsRunning();

    // Pump. Resumes suspended coroutines whose wake time has passed, fires
    // RunService events and re-schedules fast (wait() == 0) loops. Call once
    // every frame from the overlay thread after ImGui::NewFrame().
    void Tick();

    // Draws all live draw.* items onto the overlay. Call from the overlay
    // thread right before ImGui::Render().
    void RenderOverlay(ImDrawList* dl);

    // Snapshot of the print/warn/error console for the Executor tab.
    std::vector<std::string> ConsoleSnapshot();

    // Clears the console buffer.
    void ClearConsole();

    // Appends a line to the console (visible in Executor tab).
    void ConsolePush(const std::string& line);
}