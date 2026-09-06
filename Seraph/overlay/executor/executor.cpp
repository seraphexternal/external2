// External Luau executor host. Runs scripts cooperatively on the overlay
// thread inside Seraph's own process. No injection into Roblox.
//
// Script API (minimal v1):
//   print/warn, wait/task.wait/task.spawn, tick/time
//   game.PlaceId / game:GetService("Players"|"Workspace"|"RunService")
//   Players.LocalPlayer / :GetPlayers() / :FindFirstChild
//   player.Name .Character .Health .MaxHealth .TeamName .TeamColor + children parts
//   part.Position .Size .Velocity .Name + :FindFirstChild / :GetChildren
//   workspace.CurrentCamera (.Position .LookVector .FieldOfView)
//   instance:WorldToScreen(vector)
//   Vector2/Vector3(NEW)/Color3 constructors, draw.text/line/square/circle/text_size/clear
//   RunService.RenderStepped/.Heartbeat:Connect(fn)
#define NOMINMAX
#include "executor.h"

#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <windows.h>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>

#include "../imgui/imgui.h"
#include "../../rbx/math/math.h"
#include "../../rbx/globals/options.h"
#include "../../rbx/globals/globals.h"
#include "../utils/W2S.h"

#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

namespace Executor
{
    namespace
    {
        constexpr int kConsoleLimit = 600;
        constexpr int kBudgetPerResume = 150000; // instruction interrupts per resume before forcing a host yield
        constexpr float kDefaultFontSize = 16.0f;
        constexpr const char* kExecTable = "__EXEC";

        std::once_flag g_initFlag;
        std::mutex g_mutex;
        lua_State* g_L = nullptr;

        struct Runner
        {
            lua_State* thread = nullptr;
            double wake = 0.0;
            bool waiting = false;
            std::string key; // "" for the main script, "g<N>" for task.spawn'd coroutines
        };
        Runner g_script;
        std::vector<Runner> g_goroutines;
        std::vector<int> g_cbs; // RenderStepped/Heartbeat callback ids (keys into __EXEC.cbs)

        // ---- draw scene ----
        enum DrawKind
        {
            kText = 0,
            kLine,
            kRect,
            kCircle,
            kTriangle,
            kQuad,
            kImage
        };
        struct DrawItem
        {
            long long id = 0;
            int kind = kText;
            bool visible = true;
            ImVec2 a;  // text pos / line from / rect topleft / circle center
            ImVec2 b;  // line to / rect bottomright
            ImU32 color = IM_COL32(255, 255, 255, 255);
            float radius = 0.0f;
            float thickness = 1.0f;
            int numSides = 64;
            bool filled = false;
            bool center = false;
            bool outline = false;
            float fontSize = kDefaultFontSize;
            std::string text;
        };
        std::deque<DrawItem> g_items;
        long long g_nextItemId = 1;

        // ---- console ----
        std::vector<std::string> g_console;

        // ---- instruction budget ----
        long long g_budgetCounter = 0;
        bool g_budgetFired = false;

        inline double Now()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        void ExecLog(const char* fmt, ...); // fwd decl

        // Writes a line to %LOCALAPPDATA%\Seraph\executor.log so executor
        // behaviour can be diagnosed without a visible console.
        void ExecLog(const char* fmt, ...)
        {
            char path[MAX_PATH];
            char* app = nullptr;
            size_t appLen = 0;
            if (_dupenv_s(&app, &appLen, "LOCALAPPDATA") != 0 || !app)
                return;
            snprintf(path, sizeof(path), "%s\\Seraph", app);
            CreateDirectoryA(path, nullptr);
            snprintf(path, sizeof(path), "%s\\Seraph\\executor.log", app);
            free(app);
            FILE* f = nullptr;
            if (fopen_s(&f, path, "a") != 0 || !f)
                return;
            va_list ap;
            va_start(ap, fmt);
            vfprintf(f, fmt, ap);
            va_end(ap);
            fputc('\n', f);
            fclose(f);
        }

        // ------------------------------------------------------------------
        // userdata types
        // ------------------------------------------------------------------
        constexpr const char* MT_VEC2 = "Executor.Vector2";
        constexpr const char* MT_COLOR3 = "Executor.Color3";
        constexpr const char* MT_OBJ = "Executor.Instance";
        constexpr const char* MT_DRAW = "Executor.DrawItem";
        constexpr const char* MT_EVENT = "Executor.Event";
        constexpr const char* MT_UDIM2 = "Executor.UDim2";
        constexpr const char* MT_UDIM = "Executor.UDim";
        constexpr const char* MT_COLORSEQ = "Executor.ColorSequence";
        constexpr const char* MT_TWEENINFO = "Executor.TweenInfo";
        constexpr const char* MT_ENUM = "Executor.Enum";
        constexpr const char* MT_SIGNAL = "Executor.Signal";

        struct Vector2UD
        {
            float x = 0.0f;
            float y = 0.0f;
        };
        struct Color3UD
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
        };
        struct DrawObjUD
        {
            long long id = 0;
        };
        struct ConnUD
        {
            int cbId = 0;
            bool connected = false;
        };
        struct UDim2UD
        {
            float xScale = 0.0f;
            float xOffset = 0.0f;
            float yScale = 0.0f;
            float yOffset = 0.0f;
        };
        struct UDimUD
        {
            float scale = 0.0f;
            float offset = 0.0f;
        };
        struct ColorSeqUD
        {
            // Simplified: store as vector of keypoints (time, color)
            struct Keypoint { float time; float r, g, b; };
            std::vector<Keypoint> keypoints;
        };
        struct TweenInfoUD
        {
            float time = 1.0f;
            int easingStyle = 0; // Enum.EasingStyle
            int easingDirection = 0; // Enum.EasingDirection
            int repeatCount = 0;
            bool reverses = false;
            float delayTime = 0.0f;
        };

        enum ObjKind
        {
            kGame = 0,
            kPlayers,
            kWorkspace,
            kCamera,
            kPlayer,
            kCharacter,
            kPart,
            kHumanoid,
            kTeam,
            kRunService,
            kEvent,
            kUDim2,
            kUDim,
            kColorSequence,
            kTweenInfo
        };
        struct LuaObj
        {
            int kind = kGame;
            uintptr_t addr = 0;
        };

        // ------------------------------------------------------------------
        // small helpers
        // ------------------------------------------------------------------
        static Runner* FindRunner(lua_State* T)
        {
            if (g_script.thread == T)
                return &g_script;
            for (auto& r : g_goroutines)
                if (r.thread == T)
                    return &r;
            return nullptr;
        }

        static DrawItem* FindItem(long long id)
        {
            for (auto& i : g_items)
                if (i.id == id)
                    return &i;
            return nullptr;
        }

        static ImU32 ToImU32(float r, float g, float b, float a = 1.0f)
        {
            auto cc = [](float v) -> int
            {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                return static_cast<int>(v * 255.0f + 0.5f);
            };
            return IM_COL32(cc(r), cc(g), cc(b), cc(a));
        }

        // Accepts: Color3 userdata, {R,G,B} or {r,g,b} table (0-255),
        // or a single number used for all channels (0-255).
        static bool ReadColor(lua_State* L, int idx, ImU32& out)
        {
            int t = lua_type(L, idx);
            if (t == LUA_TUSERDATA)
            {
                if (lua_getmetatable(L, idx))
                {
                    luaL_getmetatable(L, MT_COLOR3);
                    bool isColor = lua_rawequal(L, -1, -2) != 0;
                    lua_pop(L, 2);
                    if (isColor)
                    {
                        Color3UD* c = static_cast<Color3UD*>(lua_touserdata(L, idx));
                        if (c)
                        {
                            out = ToImU32(c->r, c->g, c->b);
                            return true;
                        }
                    }
                }
                return false;
            }
            if (t == LUA_TTABLE)
            {
                double r = 255.0, g = 255.0, b = 255.0;
                bool any = false;
                lua_getfield(L, idx, "R");
                if (lua_isnumber(L, -1)) { r = lua_tonumber(L, -1); any = true; }
                lua_getfield(L, idx, "G");
                if (lua_isnumber(L, -1)) { g = lua_tonumber(L, -1); any = true; }
                lua_getfield(L, idx, "B");
                if (lua_isnumber(L, -1)) { b = lua_tonumber(L, -1); any = true; }
                lua_pop(L, 3);
                if (!any)
                {
                    lua_getfield(L, idx, "r");
                    if (lua_isnumber(L, -1)) { r = lua_tonumber(L, -1); any = true; }
                    lua_getfield(L, idx, "g");
                    if (lua_isnumber(L, -1)) { g = lua_tonumber(L, -1); any = true; }
                    lua_getfield(L, idx, "b");
                    if (lua_isnumber(L, -1)) { b = lua_tonumber(L, -1); any = true; }
                    lua_pop(L, 3);
                }
                out = ToImU32((float)(r / 255.0), (float)(g / 255.0), (float)(b / 255.0));
                return true;
            }
            if (t == LUA_TNUMBER)
            {
                double v = lua_tonumber(L, idx) / 255.0;
                out = ToImU32((float)v, (float)v, (float)v);
                return true;
            }
            return false;
        }

        static void PushColor3(lua_State* L, ImU32 c)
        {
            Color3UD* ud = static_cast<Color3UD*>(lua_newuserdata(L, sizeof(Color3UD)));
            ud->r = ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
            ud->g = ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
            ud->b = ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
            luaL_getmetatable(L, MT_COLOR3);
            lua_setmetatable(L, -2);
        }

        static void PushVector2(lua_State* L, const ImVec2& v)
        {
            Vector2UD* ud = static_cast<Vector2UD*>(lua_newuserdata(L, sizeof(Vector2UD)));
            ud->x = v.x;
            ud->y = v.y;
            luaL_getmetatable(L, MT_VEC2);
            lua_setmetatable(L, -2);
        }

        static bool ReadVector2(lua_State* L, int idx, ImVec2& out)
        {
            int t = lua_type(L, idx);
            if (t == LUA_TUSERDATA)
            {
                Vector2UD* v = static_cast<Vector2UD*>(lua_touserdata(L, idx));
                if (!v)
                    return false;
                out = ImVec2(v->x, v->y);
                return true;
            }
            if (t == LUA_TTABLE)
            {
                lua_getfield(L, idx, "X");
                double x = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
                lua_getfield(L, idx, "Y");
                double y = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
                lua_pop(L, 2);
                out = ImVec2((float)x, (float)y);
                return true;
            }
            return false;
        }

        static bool ReadVector3(lua_State* L, int idx, Vectors::Vector3& out)
        {
            if (lua_type(L, idx) == LUA_TVECTOR)
            {
                const float* v = lua_tovector(L, idx);
                if (v)
                {
                    out = {(float)v[0], (float)v[1], (float)v[2]};
                    return true;
                }
            }
            if (lua_type(L, idx) == LUA_TTABLE)
            {
                lua_getfield(L, idx, "X");
                double x = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
                lua_getfield(L, idx, "Y");
                double y = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
                lua_getfield(L, idx, "Z");
                double z = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
                lua_pop(L, 3);
                out = {(float)x, (float)y, (float)z};
                return true;
            }
            return false;
        }

        static void PushVector3(lua_State* L, const Vectors::Vector3& v)
        {
            lua_getglobal(L, "vector");
            lua_getfield(L, -1, "create");
            lua_pushnumber(L, v.x);
            lua_pushnumber(L, v.y);
            lua_pushnumber(L, v.z);
            lua_call(L, 3, 1);
            lua_remove(L, -2);
        }

        static int draw_remove(lua_State* L); // fwd decl (defined below in the draw section)

        // ------------------------------------------------------------------
        // coroutine lifecycle (run on g_L only)
        // ------------------------------------------------------------------
        // Strongly references `T` in the shared __EXEC table so the GC never
        // collects a suspended coroutine. Key "main" for the running script.
        static void StoreThreadRef(lua_State* T, const std::string& key)
        {
            lua_pushthread(T);
            lua_xmove(T, g_L, 1);
            lua_getglobal(g_L, kExecTable);
            lua_pushstring(g_L, key.c_str());
            lua_pushvalue(g_L, -3);
            lua_settable(g_L, -3); // __EXEC[key] = thread
            lua_pop(g_L, 2);
        }

        static void ClearThreadRef(const std::string& key)
        {
            if (!g_L)
                return;
            lua_getglobal(g_L, kExecTable);
            lua_pushstring(g_L, key.c_str());
            lua_pushnil(g_L);
            lua_settable(g_L, -3);
            lua_pop(g_L, 1);
        }

        static int Resume(Runner& r)
        {
            if (!r.thread)
                return LUA_OK;
            g_budgetCounter = 0;
            g_budgetFired = false;
            return lua_resume(r.thread, g_L, 0);
        }

        static const char* StatusName(int status)
        {
            switch (status)
            {
            case LUA_OK: return "finished";
            case LUA_YIELD: return "yielded";
            case LUA_ERRRUN: return "runtime error";
            case LUA_ERRMEM: return "out of memory";
            case LUA_ERRERR: return "error in error handler";
            case LUA_ERRSYNTAX: return "syntax error";
            default: return "error";
            }
        }

        static void ReportRunnerError(Runner& r)
        {
            lua_State* T = r.thread;
            const char* msg = "";
            if (T && lua_gettop(T) > 0 && lua_isstring(T, -1))
                msg = lua_tolstring(T, -1, nullptr);
            std::string line = "[executor] ";
            line += StatusName(LUA_ERRRUN);
            line += ": ";
            line += msg;
            Executor::ConsolePush(line);
        }

        static void CloseRunner(Runner& r)
        {
            if (!r.thread)
                return;
            ClearThreadRef(r.key.empty() ? "main" : r.key);
            r.thread = nullptr;
            r.waiting = false;
        }

        static void StopLocked()
        {
            if (g_script.thread)
                CloseRunner(g_script);
            for (auto& r : g_goroutines)
                CloseRunner(r);
            g_goroutines.clear();
        }

        // ------------------------------------------------------------------
        // interrupt hook: instruction budget + runaway-loop protection
        // ------------------------------------------------------------------
        static void InterruptHook(lua_State* L, int)
        {
            if (g_budgetFired)
                return;
            if (++g_budgetCounter < kBudgetPerResume)
                return;
            g_budgetFired = true;
            if (Runner* r = FindRunner(L))
            {
                r->wake = 0.0;
                r->waiting = false;
                lua_yield(L, 0); // hand control back to the host each budget slice
            }
            else
            {
                // No coroutine (e.g. a runaway RenderStepped callback running on
                // the main thread): abort that callback so the frame survives.
                luaL_error(L, "instruction limit exceeded");
            }
        }

        // ------------------------------------------------------------------
        // base API
        // ------------------------------------------------------------------
        static int lua_script_print(lua_State* L)
        {
            std::string line;
            int n = lua_gettop(L);
            int base = n;
            for (int i = 1; i <= n; ++i)
            {
                if (i > 1)
                    line += "\t";
                int t = lua_type(L, i);
                if (t == LUA_TSTRING)
                {
                    size_t len = 0;
                    const char* s = lua_tolstring(L, i, &len);
                    if (s)
                        line.append(s, len);
                }
                else if (t == LUA_TNUMBER)
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.14g", lua_tonumber(L, i));
                    line += buf;
                }
                else if (t == LUA_TBOOLEAN)
                {
                    line += lua_toboolean(L, i) ? "true" : "false";
                }
                else if (t == LUA_TNIL)
                {
                    line += "nil";
                }
                else if (t == LUA_TVECTOR)
                {
                    const float* v = lua_tovector(L, i);
                    if (v)
                    {
                        char buf[96];
                        snprintf(buf, sizeof(buf), "(%.2f, %.2f, %.2f)", v[0], v[1], v[2]);
                        line += buf;
                    }
                }
                else
                {
                    // userdata/tables/functions: use __tostring via luaL_tolstring
                    size_t len = 0;
                    const char* s = luaL_tolstring(L, i, &len);
                    if (s)
                        line.append(s, len);
                    else
                        line += lua_typename(L, t);
                }
            }
            lua_settop(L, base);
            Executor::ConsolePush(line);
            return 0;
        }

        static int lua_wait(lua_State* L)
        {
            double secs = 0.0; // default: resume next frame
            if (lua_gettop(L) >= 1 && lua_isnumber(L, 1))
            {
                double t = lua_tonumber(L, 1);
                if (t > 0.0)
                    secs = t;
            }
            if (Runner* r = FindRunner(L))
            {
                r->wake = Now() + secs;
                r->waiting = true;
            }
            ExecLog("[wait] thread=%p secs=%.3f", (void*)L, secs);
            return lua_yield(L, 0);
        }

        static int lua_tick(lua_State* L)
        {
            lua_pushnumber(L, Now());
            return 1;
        }

        // task.spawn: creates a fresh coroutine running fn, scheduled next frame.
        static int lua_spawn(lua_State* L)
        {
            if (!lua_isfunction(L, 1))
            {
                Executor::ConsolePush("[executor] task.spawn expects a function");
                return 0;
            }
            int base = lua_gettop(g_L);
            lua_State* T = lua_newthread(g_L);   // g_L: [..., T]
            lua_xmove(L, g_L, 1);                // move fn from L onto g_L: [..., T, fn]
            lua_xmove(g_L, T, 1);                // move fn into T: T=[fn]; g_L=[..., T]

            int id = 0;
            for (auto& r : g_goroutines)
            {
                if (r.key.size() > 1)
                    id = std::max(id, atoi(r.key.c_str() + 1));
            }
            Runner r;
            r.thread = T;
            r.key = "g" + std::to_string(id + 1);
            r.wake = 0.0;
            r.waiting = false;
            StoreThreadRef(T, r.key);            // g_L back to [..., T]
            lua_settop(g_L, base);               // drop the scratch T
            g_goroutines.push_back(std::move(r));
            return 0;
        }

        // ------------------------------------------------------------------
        // Vector2
        // ------------------------------------------------------------------
        static int v2_index(lua_State* L)
        {
            Vector2UD* v = static_cast<Vector2UD*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!v || !k)
                return 0;
            if (!strcmp(k, "X") || !strcmp(k, "x")) { lua_pushnumber(L, v->x); return 1; }
            if (!strcmp(k, "Y") || !strcmp(k, "y")) { lua_pushnumber(L, v->y); return 1; }
            if (!strcmp(k, "Magnitude"))
            {
                lua_pushnumber(L, std::sqrt(v->x * v->x + v->y * v->y));
                return 1;
            }
            return 0;
        }

        static int v2_newindex(lua_State* L)
        {
            Vector2UD* v = static_cast<Vector2UD*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!v || !k || !lua_isnumber(L, 3))
                return 0;
            double n = lua_tonumber(L, 3);
            if (!strcmp(k, "X") || !strcmp(k, "x")) v->x = (float)n;
            else if (!strcmp(k, "Y") || !strcmp(k, "y")) v->y = (float)n;
            return 0;
        }

        static int v2_arith_op(lua_State* L, int op)
        {
            Vector2UD* a = static_cast<Vector2UD*>(lua_touserdata(L, 1));
            Vector2UD* b = static_cast<Vector2UD*>(lua_touserdata(L, 2));
            float bx = b ? b->x : (float)lua_tonumber(L, 2);
            float by = b ? b->y : (float)lua_tonumber(L, 2);
            if (!a)
                return 0;
            float x = 0.0f, y = 0.0f;
            switch (op)
            {
            case 0: x = a->x + bx; y = a->y + by; break;
            case 1: x = a->x - bx; y = a->y - by; break;
            case 2: x = a->x * bx; y = a->y * by; break;
            default: x = (bx != 0.0f) ? a->x / bx : 0.0f; y = (by != 0.0f) ? a->y / by : 0.0f; break;
            }
            PushVector2(L, ImVec2(x, y));
            return 1;
        }
        static int v2_add(lua_State* L) { return v2_arith_op(L, 0); }
        static int v2_sub(lua_State* L) { return v2_arith_op(L, 1); }
        static int v2_mul(lua_State* L) { return v2_arith_op(L, 2); }
        static int v2_div(lua_State* L) { return v2_arith_op(L, 3); }

        static int v2_unm(lua_State* L)
        {
            Vector2UD* a = static_cast<Vector2UD*>(lua_touserdata(L, 1));
            PushVector2(L, ImVec2(-a->x, -a->y));
            return 1;
        }

        static int v2_eq(lua_State* L)
        {
            Vector2UD* a = static_cast<Vector2UD*>(lua_touserdata(L, 1));
            Vector2UD* b = static_cast<Vector2UD*>(lua_touserdata(L, 2));
            lua_pushboolean(L, a && b && a->x == b->x && a->y == b->y);
            return 1;
        }

        static int v2_tostring(lua_State* L)
        {
            Vector2UD* v = static_cast<Vector2UD*>(lua_touserdata(L, 1));
            char buf[96];
            snprintf(buf, sizeof(buf), "Vector2(%.1f, %.1f)", v->x, v->y);
            lua_pushstring(L, buf);
            return 1;
        }

        static int lua_Vector2_new(lua_State* L)
        {
            PushVector2(L, ImVec2((float)luaL_optnumber(L, 1, 0.0), (float)luaL_optnumber(L, 2, 0.0)));
            return 1;
        }

        // ------------------------------------------------------------------
        // Color3
        // ------------------------------------------------------------------
        static int c3_index(lua_State* L)
        {
            Color3UD* c = static_cast<Color3UD*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!c || !k)
                return 0;
            if (!strcmp(k, "R") || !strcmp(k, "r")) { lua_pushnumber(L, c->r); return 1; }
            if (!strcmp(k, "G") || !strcmp(k, "g")) { lua_pushnumber(L, c->g); return 1; }
            if (!strcmp(k, "B") || !strcmp(k, "b")) { lua_pushnumber(L, c->b); return 1; }
            return 0;
        }

        static Color3UD* PushColor3UD(lua_State* L, float r, float g, float b)
        {
            Color3UD* c = static_cast<Color3UD*>(lua_newuserdata(L, sizeof(Color3UD)));
            c->r = r;
            c->g = g;
            c->b = b;
            luaL_getmetatable(L, MT_COLOR3);
            lua_setmetatable(L, -2);
            return c;
        }

        static int lua_Color3_new(lua_State* L)
        {
            PushColor3UD(L, (float)luaL_optnumber(L, 1, 0.0), (float)luaL_optnumber(L, 2, 0.0), (float)luaL_optnumber(L, 3, 0.0));
            return 1;
        }

        static int lua_Color3_fromRGB(lua_State* L)
        {
            PushColor3UD(L, (float)(luaL_optnumber(L, 1, 0.0) / 255.0),
                            (float)(luaL_optnumber(L, 2, 0.0) / 255.0),
                            (float)(luaL_optnumber(L, 3, 0.0) / 255.0));
            return 1;
        }

        // ------------------------------------------------------------------
        // UDim2 / UDim / ColorSequence / TweenInfo
        // ------------------------------------------------------------------
        static UDim2UD* PushUDim2UD(lua_State* L, float xS, float xO, float yS, float yO)
        {
            UDim2UD* u = static_cast<UDim2UD*>(lua_newuserdata(L, sizeof(UDim2UD)));
            u->xScale = xS; u->xOffset = xO; u->yScale = yS; u->yOffset = yO;
            luaL_getmetatable(L, MT_UDIM2);
            lua_setmetatable(L, -2);
            return u;
        }

        static int udim2_index(lua_State* L)
        {
            UDim2UD* u = static_cast<UDim2UD*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!u || !k) return 0;
            if (!strcmp(k, "X")) { lua_pushnumber(L, u->xScale); lua_pushnumber(L, u->xOffset); return 2; }
            if (!strcmp(k, "Y")) { lua_pushnumber(L, u->yScale); lua_pushnumber(L, u->yOffset); return 2; }
            if (!strcmp(k, "Scale")) { lua_pushnumber(L, u->xScale); lua_pushnumber(L, u->yScale); return 2; }
            if (!strcmp(k, "Offset")) { lua_pushnumber(L, u->xOffset); lua_pushnumber(L, u->yOffset); return 2; }
            return 0;
        }

        static int udim2_tostring(lua_State* L)
        {
            UDim2UD* u = static_cast<UDim2UD*>(lua_touserdata(L, 1));
            if (!u) return 0;
            char buf[64];
            snprintf(buf, sizeof(buf), "UDim2.new(%.3f, %.1f, %.3f, %.1f)", u->xScale, u->xOffset, u->yScale, u->yOffset);
            lua_pushstring(L, buf);
            return 1;
        }

        static int lua_UDim2_new(lua_State* L)
        {
            PushUDim2UD(L,
                (float)luaL_optnumber(L, 1, 0.0),
                (float)luaL_optnumber(L, 2, 0.0),
                (float)luaL_optnumber(L, 3, 0.0),
                (float)luaL_optnumber(L, 4, 0.0));
            return 1;
        }

        static int lua_UDim2_fromScale(lua_State* L)
        {
            PushUDim2UD(L,
                (float)luaL_optnumber(L, 1, 0.0), 0.0f,
                (float)luaL_optnumber(L, 2, 0.0), 0.0f);
            return 1;
        }

        static int lua_UDim2_fromOffset(lua_State* L)
        {
            PushUDim2UD(L, 0.0f, (float)luaL_optnumber(L, 1, 0.0), 0.0f, (float)luaL_optnumber(L, 2, 0.0));
            return 1;
        }

        // UDim
        static UDimUD* PushUDimUD(lua_State* L, float s, float o)
        {
            UDimUD* u = static_cast<UDimUD*>(lua_newuserdata(L, sizeof(UDimUD)));
            u->scale = s; u->offset = o;
            luaL_getmetatable(L, MT_UDIM);
            lua_setmetatable(L, -2);
            return u;
        }

        static int udim_index(lua_State* L)
        {
            UDimUD* u = static_cast<UDimUD*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!u || !k) return 0;
            if (!strcmp(k, "Scale")) { lua_pushnumber(L, u->scale); return 1; }
            if (!strcmp(k, "Offset")) { lua_pushnumber(L, u->offset); return 1; }
            return 0;
        }

        static int udim_tostring(lua_State* L)
        {
            UDimUD* u = static_cast<UDimUD*>(lua_touserdata(L, 1));
            if (!u) return 0;
            char buf[64];
            snprintf(buf, sizeof(buf), "UDim.new(%.3f, %.1f)", u->scale, u->offset);
            lua_pushstring(L, buf);
            return 1;
        }

        static int lua_UDim_new(lua_State* L)
        {
            PushUDimUD(L, (float)luaL_optnumber(L, 1, 0.0), (float)luaL_optnumber(L, 2, 0.0));
            return 1;
        }

        // ColorSequence (simplified: fromRGB sequence)
        static int lua_ColorSequence_new(lua_State* L)
        {
            // ColorSequence.new(Color3) or ColorSequence.new({Color3 keypoints})
            // Simplified: create empty ColorSequence userdata
            ColorSeqUD* c = static_cast<ColorSeqUD*>(lua_newuserdata(L, sizeof(ColorSeqUD)));
            luaL_getmetatable(L, MT_COLORSEQ);
            lua_setmetatable(L, -2);
            return 1;
        }

        // TweenInfo
        static TweenInfoUD* PushTweenInfoUD(lua_State* L, float t, int style, int dir, int rep, bool rev, float delay)
        {
            TweenInfoUD* ti = static_cast<TweenInfoUD*>(lua_newuserdata(L, sizeof(TweenInfoUD)));
            ti->time = t; ti->easingStyle = style; ti->easingDirection = dir;
            ti->repeatCount = rep; ti->reverses = rev; ti->delayTime = delay;
            luaL_getmetatable(L, MT_TWEENINFO);
            lua_setmetatable(L, -2);
            return ti;
        }

        static int tweeninfo_index(lua_State* L)
        {
            TweenInfoUD* ti = static_cast<TweenInfoUD*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!ti || !k) return 0;
            if (!strcmp(k, "Time")) { lua_pushnumber(L, ti->time); return 1; }
            if (!strcmp(k, "EasingStyle")) { lua_pushnumber(L, ti->easingStyle); return 1; }
            if (!strcmp(k, "EasingDirection")) { lua_pushnumber(L, ti->easingDirection); return 1; }
            if (!strcmp(k, "RepeatCount")) { lua_pushnumber(L, ti->repeatCount); return 1; }
            if (!strcmp(k, "Reverses")) { lua_pushboolean(L, ti->reverses); return 1; }
            if (!strcmp(k, "DelayTime")) { lua_pushnumber(L, ti->delayTime); return 1; }
            return 0;
        }

        static int tweeninfo_tostring(lua_State* L)
        {
            TweenInfoUD* ti = static_cast<TweenInfoUD*>(lua_touserdata(L, 1));
            if (!ti) return 0;
            char buf[128];
            snprintf(buf, sizeof(buf), "TweenInfo.new(%.2f, %d, %d, %d, %s, %.2f)", ti->time, ti->easingStyle, ti->easingDirection, ti->repeatCount, ti->reverses ? "true" : "false", ti->delayTime);
            lua_pushstring(L, buf);
            return 1;
        }

        static int lua_TweenInfo_new(lua_State* L)
        {
            PushTweenInfoUD(L,
                (float)luaL_optnumber(L, 1, 1.0),
                (int)luaL_optinteger(L, 2, 0),
                (int)luaL_optinteger(L, 3, 0),
                (int)luaL_optinteger(L, 4, 0),
                lua_toboolean(L, 5),
                (float)luaL_optnumber(L, 6, 0.0));
            return 1;
        }

        // ------------------------------------------------------------------
        // Signal system (forward declarations and state)
        // ------------------------------------------------------------------
        struct SignalCallback
        {
            int id = 0;
            bool connected = true;
            lua_State* thread = nullptr;
        };
        static std::vector<SignalCallback> g_signalCallbacks;
        static int g_nextSignalId = 1;

        static int RunService_Disconnect(lua_State* L);
        static int Signal_Connect(lua_State* L);
        static int Signal_Wait(lua_State* L);
        static int Signal_Fire(lua_State* L);
        static int Signal_index(lua_State* L);

        static int lua_Signal_new(lua_State* L)
        {
            // Create a Signal userdata
            int cbId = g_nextSignalId++;
            g_signalCallbacks.push_back({cbId, true, nullptr});

            ConnUD* c = static_cast<ConnUD*>(lua_newuserdata(L, sizeof(ConnUD)));
            c->cbId = cbId;
            c->connected = true;

            lua_createtable(L, 0, 3);
            lua_pushcfunction(L, RunService_Disconnect, "Disconnect");
            lua_setfield(L, -2, "Disconnect");
            lua_pushcfunction(L, Signal_Connect, "Connect");
            lua_setfield(L, -2, "Connect");
            lua_pushcfunction(L, Signal_Wait, "Wait");
            lua_setfield(L, -2, "Wait");
            lua_pushcfunction(L, Signal_Fire, "Fire");
            lua_setfield(L, -2, "Fire");
            lua_setmetatable(L, -2);
            return 1;
        }

        static int Signal_Connect(lua_State* L)
        {
            if (!lua_isfunction(L, 1))
                return 0;
            int cbId = g_nextSignalId++;
            g_signalCallbacks.push_back({cbId, true, nullptr});

            std::string key = "s" + std::to_string(cbId);
            int base = lua_gettop(L);

            lua_getglobal(L, kExecTable);
            lua_getfield(L, -1, "cbs");
            lua_pushstring(L, key.c_str());
            lua_pushvalue(L, 1);
            lua_settable(L, -3);
            lua_settop(L, base);

            ConnUD* c = static_cast<ConnUD*>(lua_newuserdata(L, sizeof(ConnUD)));
            c->cbId = cbId;
            c->connected = true;

            lua_createtable(L, 0, 2);
            lua_pushcfunction(L, RunService_Disconnect, "Disconnect");
            lua_setfield(L, -2, "Disconnect");
            lua_setmetatable(L, -2);
            return 1;
        }

        static int Signal_Wait(lua_State* L)
        {
            if (!lua_isfunction(L, 1))
                return 0;
            int cbId = g_nextSignalId++;
            lua_State* T = lua_tothread(L, 1);
            if (!T)
                return 0;
            g_signalCallbacks.push_back({cbId, true, T});
            return lua_yield(L, 0);
        }

        static int Signal_Fire(lua_State* L)
        {
            int fired = 0;
            for (auto& cb : g_signalCallbacks)
            {
                if (!cb.connected)
                    continue;
                if (cb.thread)
                {
                    int nargs = lua_gettop(L) - 1;
                    lua_xmove(L, cb.thread, nargs);
                    int status = lua_resume(cb.thread, L, nargs);
                    if (status != LUA_OK && status != LUA_YIELD)
                    {
                        const char* err = lua_tostring(cb.thread, -1) ? lua_tostring(cb.thread, -1) : "resume error";
                        Executor::ConsolePush(std::string("[executor] Signal Wait error: ") + err);
                    }
                    cb.connected = false;
                    fired++;
                }
                else
                {
                    std::string key = "s" + std::to_string(cb.id);
                    lua_getglobal(L, kExecTable);
                    lua_getfield(L, -1, "cbs");
                    lua_getfield(L, -1, key.c_str());
                    if (lua_isfunction(L, -1))
                    {
                        int nargs = lua_gettop(L) - 1;
                        if (lua_pcall(L, nargs, 0, 0) != LUA_OK)
                        {
                            const char* err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
                            Executor::ConsolePush(std::string("[executor] Signal Connect error: ") + err);
                        }
                        fired++;
                    }
                    lua_settop(L, 0);
                }
            }
            lua_pushinteger(L, fired);
            return 1;
        }

        static int Signal_index(lua_State* L)
        {
            const char* k = lua_tostring(L, 2);
            if (!k) return 0;
            if (!strcmp(k, "Connect")) { lua_pushcfunction(L, Signal_Connect, nullptr); return 1; }
            if (!strcmp(k, "Wait")) { lua_pushcfunction(L, Signal_Wait, nullptr); return 1; }
            if (!strcmp(k, "Fire")) { lua_pushcfunction(L, Signal_Fire, nullptr); return 1; }
            return 0;
        }

        // ------------------------------------------------------------------
        // Instances
        // ------------------------------------------------------------------
        static LuaObj* push_obj(lua_State* L, int kind, uintptr_t addr)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_newuserdata(L, sizeof(LuaObj)));
            o->kind = kind;
            o->addr = addr;
            luaL_getmetatable(L, (kind == kEvent) ? MT_EVENT : MT_OBJ);
            lua_setmetatable(L, -2);
            return o;
        }

        static int obj_method_GetService(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            const char* name = lua_tostring(L, 2);
            if (!o || o->kind != kGame || !name)
                return 0;
            if (!strcmp(name, "Players")) { push_obj(L, kPlayers, Globals::Roblox::Players.address); return 1; }
            if (!strcmp(name, "Workspace")) { push_obj(L, kWorkspace, Globals::Roblox::Workspace.address); return 1; }
            if (!strcmp(name, "RunService")) { push_obj(L, kRunService, 0); return 1; }
            return 0;
        }

        static int obj_method_GetChildren(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            if (!o)
                return 0;
            lua_newtable(L);
            int idx = 1;
            if (o->kind == kPlayers)
            {
                auto players = Globals::Roblox::Players.GetChildren();
                for (auto& p : players)
                {
                    if (!p.address)
                        continue;
                    push_obj(L, kPlayer, p.address);
                    lua_rawseti(L, -2, idx++);
                }
            }
            else if (o->kind == kCharacter || o->kind == kPart)
            {
                auto children = RobloxInstance(o->addr).GetChildren();
                for (auto& c : children)
                {
                    if (!c.address)
                        continue;
                    std::string cls = c.Class();
                    push_obj(L, (cls == "Humanoid") ? kHumanoid : kPart, c.address);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }

        static bool ObjFromChildren(const RobloxInstance& parent, const char* name, bool byClass, LuaObj& out)
        {
            auto children = parent.GetChildren();
            for (auto& c : children)
            {
                if (!c.address)
                    continue;
                bool match = byClass ? (c.Class() == name) : (c.Name() == name);
                if (!match)
                    continue;
                std::string cls = c.Class();
                out.kind = (cls == "Humanoid") ? kHumanoid : kPart;
                out.addr = c.address;
                return true;
            }
            return false;
        }

        static int obj_method_FindFirstChild(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            const char* name = lua_tostring(L, 2);
            if (!o || !name)
                return 0;
            if (o->kind == kPlayers)
            {
                auto players = Globals::Roblox::Players.GetChildren();
                for (auto& p : players)
                {
                    if (p.address && p.Name() == name)
                    {
                        push_obj(L, kPlayer, p.address);
                        return 1;
                    }
                }
                return 0;
            }
            if (o->kind == kCharacter || o->kind == kPart)
            {
                LuaObj child;
                if (ObjFromChildren(RobloxInstance(o->addr), name, false, child))
                {
                    push_obj(L, child.kind, child.addr);
                    return 1;
                }
            }
            return 0;
        }

        static int obj_method_FindFirstChildWhichIsA(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            const char* cls = lua_tostring(L, 2);
            if (!o || !cls)
                return 0;
            if (o->kind == kCharacter || o->kind == kPart)
            {
                LuaObj child;
                if (ObjFromChildren(RobloxInstance(o->addr), cls, true, child))
                {
                    push_obj(L, child.kind, child.addr);
                    return 1;
                }
            }
            return 0;
        }

        // WaitForChild (same as FindFirstChild but yields if not found - simplified to just FindFirstChild)
        static int obj_method_WaitForChild(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            const char* name = lua_tostring(L, 2);
            if (!o || !name)
                return 0;
            // For fake instances (addr == 0), just return nil
            if (o->addr == 0)
            {
                lua_pushnil(L);
                return 1;
            }
            // For real instances, delegate to FindFirstChild
            return obj_method_FindFirstChild(L);
        }

        static int obj_method_IsA(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            const char* cls = lua_tostring(L, 2);
            if (!o || !cls)
                return 0;
            bool ok = false;
            if (o->kind == kPart || o->kind == kCharacter || o->kind == kHumanoid || o->kind == kPlayer)
            {
                if (RobloxInstance(o->addr).address)
                    ok = (RobloxInstance(o->addr).Class() == cls);
            }
            else
            {
                static const char* names[] = {"DataModel", "Players", "Workspace", "Camera", "Player", "Model", "BasePart", "Humanoid", "Team", "RunService"};
                if (o->kind >= 0 && o->kind < static_cast<int>(sizeof(names) / sizeof(names[0])))
                    ok = (std::string(names[o->kind]) == cls);
            }
            lua_pushboolean(L, ok);
            return 1;
        }

        static int obj_method_GetPosition(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            if (!o)
                return 0;
            if (o->kind == kPart)
            {
                RobloxInstance inst(o->addr);
                if (inst.address)
                {
                    PushVector3(L, inst.Position());
                    return 1;
                }
            }
            else if (o->kind == kCharacter)
            {
                RobloxInstance hrp = RobloxInstance(o->addr).FindFirstChild("HumanoidRootPart");
                if (hrp.address)
                {
                    PushVector3(L, hrp.Position());
                    return 1;
                }
            }
            return 0;
        }

        static int obj_method_W2S(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            if (!o)
                return 0;
            Vectors::Vector3 world;
            if (lua_gettop(L) >= 2)
            {
                if (!ReadVector3(L, 2, world))
                    return 0;
            }
            else if (o->kind == kPart || o->kind == kCharacter)
            {
                RobloxInstance hrp = (o->kind == kCharacter) ? RobloxInstance(o->addr).FindFirstChild("HumanoidRootPart") : RobloxInstance(o->addr);
                if (!hrp.address)
                    return 0;
                world = hrp.Position();
            }
            else
            {
                return 0;
            }
            Vectors::Vector2 s = WorldToScreen(world);
            if (s.x < 0.0f || s.y < 0.0f)
                return 0;
            PushVector2(L, ImVec2(s.x, s.y));
            return 1;
        }

        static int obj_method_GetFullName(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            std::string n = "Instance";
            if (o && o->addr)
            {
                try
                {
                    n = RobloxInstance(o->addr).Name();
                }
                catch (...)
                {
                }
            }
            lua_pushstring(L, n.c_str());
            return 1;
        }

        static int obj_method_GetPlayerFromCharacter(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            LuaObj* target = static_cast<LuaObj*>(lua_touserdata(L, 2));
            if (!o || !target || !target->addr)
                return 0;
            auto players = Globals::Roblox::Players.GetChildren();
            for (auto& p : players)
            {
                if (!p.address)
                    continue;
                if (p.Character().address == target->addr)
                {
                    push_obj(L, kPlayer, p.address);
                    return 1;
                }
            }
            return 0;
        }

        static int obj_index(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!o || !k)
                return 0;

            if (!strcmp(k, "GetService")) { lua_pushcfunction(L, obj_method_GetService, nullptr); return 1; }
            if (!strcmp(k, "GetChildren")) { lua_pushcfunction(L, obj_method_GetChildren, nullptr); return 1; }
            if (!strcmp(k, "FindFirstChild")) { lua_pushcfunction(L, obj_method_FindFirstChild, nullptr); return 1; }
            if (!strcmp(k, "FindFirstChildWhichIsA")) { lua_pushcfunction(L, obj_method_FindFirstChildWhichIsA, nullptr); return 1; }
            if (!strcmp(k, "WaitForChild")) { lua_pushcfunction(L, obj_method_WaitForChild, nullptr); return 1; }
            if (!strcmp(k, "IsA")) { lua_pushcfunction(L, obj_method_IsA, nullptr); return 1; }
            if (!strcmp(k, "GetPosition")) { lua_pushcfunction(L, obj_method_GetPosition, nullptr); return 1; }
            if (!strcmp(k, "WorldToScreen")) { lua_pushcfunction(L, obj_method_W2S, nullptr); return 1; }
            if (!strcmp(k, "GetFullName")) { lua_pushcfunction(L, obj_method_GetFullName, nullptr); return 1; }
            if (!strcmp(k, "GetPlayers")) { lua_pushcfunction(L, obj_method_GetChildren, nullptr); return 1; }
            if (!strcmp(k, "GetPlayerFromCharacter")) { lua_pushcfunction(L, obj_method_GetPlayerFromCharacter, nullptr); return 1; }

            switch (o->kind)
            {
            case kGame:
                if (!strcmp(k, "PlaceId") || !strcmp(k, "GameId"))
                {
                    lua_pushnumber(L, Globals::Roblox::lastPlaceID);
                    return 1;
                }
                if (!strcmp(k, "Workspace")) { push_obj(L, kWorkspace, Globals::Roblox::Workspace.address); return 1; }
                if (!strcmp(k, "Players")) { push_obj(L, kPlayers, Globals::Roblox::Players.address); return 1; }
                if (!strcmp(k, "RunService")) { push_obj(L, kRunService, 0); return 1; }
                return 0;

            case kPlayers:
                if (!strcmp(k, "LocalPlayer"))
                {
                    if (Globals::Roblox::LocalPlayer.address)
                        push_obj(L, kPlayer, Globals::Roblox::LocalPlayer.address);
                    return 1;
                }
                if (!strcmp(k, "ClassName")) { lua_pushstring(L, "Players"); return 1; }
                return 0;

            case kWorkspace:
                if (!strcmp(k, "CurrentCamera") || !strcmp(k, "Camera"))
                {
                    if (Globals::Roblox::Camera.address)
                        push_obj(L, kCamera, Globals::Roblox::Camera.address);
                    return 1;
                }
                if (!strcmp(k, "ClassName")) { lua_pushstring(L, "Workspace"); return 1; }
                return 0;

            case kCamera:
                if (!strcmp(k, "ClassName")) { lua_pushstring(L, "Camera"); return 1; }
                if (!strcmp(k, "Position"))
                {
                    if (o->addr)
                        PushVector3(L, Memory->read<Vectors::Vector3>(o->addr + Offsets::Camera::Position));
                    return 1;
                }
                if (!strcmp(k, "LookVector"))
                {
                    if (o->addr)
                    {
                        auto r = Memory->read<Matrixes::Matrix3x3>(o->addr + Offsets::Camera::Rotation);
                        PushVector3(L, {-r.r20, -r.r21, -r.r22});
                    }
                    return 1;
                }
                if (!strcmp(k, "FieldOfView"))
                {
                    if (o->addr)
                        lua_pushnumber(L, Memory->read<float>(o->addr + Offsets::Camera::FieldOfView));
                    return 1;
                }
                return 0;

            case kPlayer:
                if (!strcmp(k, "Name") || !strcmp(k, "DisplayName"))
                {
                    if (o->addr)
                        lua_pushstring(L, RobloxInstance(o->addr).Name().c_str());
                    return 1;
                }
                if (!strcmp(k, "ClassName")) { lua_pushstring(L, "Player"); return 1; }
                if (!strcmp(k, "Character"))
                {
                    if (o->addr)
                    {
                        auto ch = RobloxInstance(o->addr).Character();
                        if (ch.address)
                            push_obj(L, kCharacter, ch.address);
                    }
                    return 1;
                }
                if (!strcmp(k, "Team"))
                {
                    if (o->addr)
                    {
                        auto team = RobloxInstance(o->addr).Team();
                        if (team.address)
                            push_obj(L, kTeam, team.address);
                    }
                    return 1;
                }
                if (!strcmp(k, "TeamName"))
                {
                    if (o->addr)
                    {
                        auto team = RobloxInstance(o->addr).Team();
                        if (team.address)
                            lua_pushstring(L, team.Name().c_str());
                    }
                    return 1;
                }
                if (!strcmp(k, "TeamColor"))
                {
                    if (o->addr)
                        lua_pushnumber(L, Memory->read<int>(o->addr + Offsets::Player::TeamColor));
                    return 1;
                }
                if (!strcmp(k, "Health"))
                {
                    if (o->addr)
                        lua_pushnumber(L, RobloxInstance(o->addr).Health());
                    return 1;
                }
                if (!strcmp(k, "MaxHealth"))
                {
                    if (o->addr)
                        lua_pushnumber(L, RobloxInstance(o->addr).MaxHealth());
                    return 1;
                }
                return 0;

            case kCharacter:
                if (!strcmp(k, "ClassName")) { lua_pushstring(L, "Model"); return 1; }
                if (!strcmp(k, "Name"))
                {
                    if (o->addr)
                        lua_pushstring(L, RobloxInstance(o->addr).Name().c_str());
                    return 1;
                }
                if (!strcmp(k, "Position")) { return obj_method_GetPosition(L); }
                if (o->addr)
                {
                    LuaObj child;
                    if (ObjFromChildren(RobloxInstance(o->addr), k, false, child))
                    {
                        push_obj(L, child.kind, child.addr);
                        return 1;
                    }
                }
                return 0;

            case kPart:
                if (!strcmp(k, "ClassName"))
                {
                    if (o->addr)
                        lua_pushstring(L, RobloxInstance(o->addr).Class().c_str());
                    return 1;
                }
                if (!strcmp(k, "Name"))
                {
                    if (o->addr)
                        lua_pushstring(L, RobloxInstance(o->addr).Name().c_str());
                    return 1;
                }
                if (!strcmp(k, "Position"))
                {
                    if (o->addr)
                        PushVector3(L, RobloxInstance(o->addr).Position());
                    return 1;
                }
                if (!strcmp(k, "Size"))
                {
                    if (o->addr)
                        PushVector3(L, RobloxInstance(o->addr).Size());
                    return 1;
                }
                if (!strcmp(k, "Velocity"))
                {
                    if (o->addr)
                    {
                        uintptr_t prim = Memory->read<uintptr_t>(o->addr + Offsets::BasePart::Primitive);
                        if (prim)
                            PushVector3(L, Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity));
                    }
                    return 1;
                }
                if (o->addr)
                {
                    LuaObj child;
                    if (ObjFromChildren(RobloxInstance(o->addr), k, false, child))
                    {
                        push_obj(L, child.kind, child.addr);
                        return 1;
                    }
                }
                return 0;

            case kHumanoid:
                if (!strcmp(k, "ClassName")) { lua_pushstring(L, "Humanoid"); return 1; }
                if (!strcmp(k, "Health")) { if (o->addr) lua_pushnumber(L, Memory->read<float>(o->addr + Offsets::Humanoid::Health)); return 1; }
                if (!strcmp(k, "MaxHealth")) { if (o->addr) lua_pushnumber(L, Memory->read<float>(o->addr + Offsets::Humanoid::MaxHealth)); return 1; }
                if (!strcmp(k, "WalkSpeed")) { if (o->addr) lua_pushnumber(L, Memory->read<float>(o->addr + Offsets::Humanoid::Walkspeed)); return 1; }
                return 0;

            case kTeam:
                if (!strcmp(k, "ClassName")) { lua_pushstring(L, "Team"); return 1; }
                if (!strcmp(k, "Name"))
                {
                    if (o->addr)
                        lua_pushstring(L, RobloxInstance(o->addr).Name().c_str());
                    return 1;
                }
                if (!strcmp(k, "TeamColor"))
                {
                    if (o->addr)
                        lua_pushnumber(L, Memory->read<int>(o->addr + Offsets::Team::BrickColor));
                    return 1;
                }
                return 0;

            case kRunService:
                if (!strcmp(k, "RenderStepped") || !strcmp(k, "Stepped") || !strcmp(k, "PreRender"))
                {
                    push_obj(L, kEvent, 1);
                    return 1;
                }
                if (!strcmp(k, "Heartbeat"))
                {
                    push_obj(L, kEvent, 2);
                    return 1;
                }
                if (!strcmp(k, "ClassName")) { lua_pushstring(L, "RunService"); return 1; }
                return 0;
            }
            return 0;
        }

        static int obj_len(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            int n = (o && o->kind == kPlayers) ? static_cast<int>(Globals::Roblox::Players.GetChildren().size()) : 0;
            lua_pushnumber(L, n);
            return 1;
        }

        static int obj_call(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            if (o && o->kind == kGame)
                return obj_method_GetService(L);
            lua_pushnil(L);
            return 1;
        }

        static int obj_tostring(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            if (!o)
                return 0;
            std::string cls = "Instance";
            std::string name;
            if (o->addr)
            {
                try
                {
                    RobloxInstance inst(o->addr);
                    cls = inst.Class();
                    name = inst.Name();
                }
                catch (...)
                {
                }
            }
            else if (o->kind == kGame) cls = "DataModel";
            else if (o->kind == kPlayers) cls = "Players";
            else if (o->kind == kWorkspace) cls = "Workspace";
            else if (o->kind == kCamera) cls = "Camera";
            else if (o->kind == kRunService) cls = "RunService";
            else if (o->kind == kEvent) cls = "RBXScriptSignal";
            char buf[512];
            if (name.empty())
                snprintf(buf, sizeof(buf), "%s", cls.c_str());
            else
                snprintf(buf, sizeof(buf), "%s(\"%s\")", cls.c_str(), name.c_str());
            lua_pushstring(L, buf);
            return 1;
        }

        // Instance __newindex - property writes
        static int obj_newindex(lua_State* L)
        {
            LuaObj* o = static_cast<LuaObj*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!o || !k || !o->addr)
                return 0;

            // Handle common property writes
            try
            {
                RobloxInstance inst(o->addr);

                // Vector3 properties
                if (!strcmp(k, "Position") || !strcmp(k, "CFrame"))
                {
                    // Expect Vector3 on stack at index 3
                    if (lua_getmetatable(L, 3))
                    {
                        luaL_getmetatable(L, "Executor.Vector3"); // Luau's vector metatable name
                        bool isVec = lua_rawequal(L, -1, -2);
                        lua_pop(L, 2);
                        if (isVec)
                        {
                            // Read Vector3 from userdata
                            float x = 0, y = 0, z = 0;
                            lua_getfield(L, 3, "X"); if (lua_isnumber(L, -1)) x = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                            lua_getfield(L, 3, "Y"); if (lua_isnumber(L, -1)) y = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                            lua_getfield(L, 3, "Z"); if (lua_isnumber(L, -1)) z = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                            
                            if (!strcmp(k, "Position"))
                            {
                                uintptr_t prim = Memory->read<uintptr_t>(o->addr + Offsets::BasePart::Primitive);
                                if (prim)
                                    Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::Position, Vectors::Vector3{x, y, z});
                            }
                            else // CFrame
                            {
                                uintptr_t prim = Memory->read<uintptr_t>(o->addr + Offsets::BasePart::Primitive);
                                if (prim)
                                    Memory->write<sCFrame>(prim + Offsets::Primitive::Rotation, sCFrame{1,0,0,0,1,0,0,0,1,x,y,z});
                            }
                        }
                    }
                    return 0;
                }

                // Float properties
                if (!strcmp(k, "WalkSpeed") || !strcmp(k, "JumpPower") || !strcmp(k, "Health") || !strcmp(k, "MaxHealth") || !strcmp(k, "FieldOfView"))
                {
                    if (lua_isnumber(L, 3))
                    {
                        float val = (float)lua_tonumber(L, 3);
                        if (!strcmp(k, "WalkSpeed") || !strcmp(k, "JumpPower"))
                        {
                            auto humanoid = inst.FindFirstChildWhichIsA("Humanoid");
                            if (humanoid.address)
                            {
                                if (!strcmp(k, "WalkSpeed"))
                                {
                                    Memory->write(humanoid.address + Offsets::Humanoid::WalkspeedCheck, val);
                                    Memory->write(humanoid.address + Offsets::Humanoid::Walkspeed, val);
                                }
                                else
                                    Memory->write(humanoid.address + Offsets::Humanoid::JumpPower, val);
                            }
                        }
                        else if (!strcmp(k, "Health") || !strcmp(k, "MaxHealth"))
                        {
                            auto humanoid = inst.FindFirstChildWhichIsA("Humanoid");
                            if (humanoid.address)
                                Memory->write(humanoid.address + (!strcmp(k, "Health") ? Offsets::Humanoid::Health : Offsets::Humanoid::MaxHealth), val);
                        }
                        else if (!strcmp(k, "FieldOfView"))
                        {
                            if (o->kind == kCamera)
                                Memory->write(o->addr + Offsets::Camera::FieldOfView, val * 3.1415926535f / 180.0f);
                        }
                    }
                    return 0;
                }

                // String properties (Name)
                if (!strcmp(k, "Name") && lua_isstring(L, 3))
                {
                    // Note: Name is typically read-only in Roblox, but some exploits allow it
                    // Memory->writeString(inst.NameAddress(), lua_tostring(L, 3));
                }

                // Bool properties
                if (lua_isboolean(L, 3))
                {
                    bool val = lua_toboolean(L, 3) != 0;
                    // Add bool property handlers here as needed
                }
            }
            catch (...)
            {
            }
            return 0;
        }

        // ------------------------------------------------------------------
        // RunService events
        // ------------------------------------------------------------------
        static int RunService_Disconnect(lua_State* L)
        {
            ConnUD* c = static_cast<ConnUD*>(lua_touserdata(L, 1));
            if (!c || !c->connected)
                return 0;
            c->connected = false;

            std::string k2 = "c" + std::to_string(c->cbId);
            lua_getglobal(L, kExecTable);
            lua_getfield(L, -1, "cbs");
            lua_pushstring(L, k2.c_str());
            lua_pushnil(L);
            lua_settable(L, -3);
            lua_settop(L, 0);
            for (size_t i = 0; i < g_cbs.size(); ++i)
            {
                if (g_cbs[i] == c->cbId)
                {
                    g_cbs.erase(g_cbs.begin() + i);
                    break;
                }
            }
            return 0;
        }

        static int RunService_Connect(lua_State* L)
        {
            if (!lua_isfunction(L, 1))
                return 0;
            int cbId = 0;
            for (int id : g_cbs)
                cbId = std::max(cbId, id);
            ++cbId;
            g_cbs.push_back(cbId);

            std::string key = "c" + std::to_string(cbId);
            int base = lua_gettop(L);

            lua_getglobal(L, kExecTable);
            lua_getfield(L, -1, "cbs");
            lua_pushstring(L, key.c_str());
            lua_pushvalue(L, 1);        // the callback function
            lua_settable(L, -3);        // __EXEC.cbs[key] = fn
            lua_settop(L, base);

            ConnUD* c = static_cast<ConnUD*>(lua_newuserdata(L, sizeof(ConnUD)));
            c->cbId = cbId;
            c->connected = true;

            lua_createtable(L, 0, 1);
            lua_pushcfunction(L, RunService_Disconnect, "Disconnect");
            lua_setfield(L, -2, "Disconnect");
            lua_setmetatable(L, -2);
            return 1;
        }

        static int RunService_event_index(lua_State* L)
        {
            const char* k = lua_tostring(L, 2);
            if (k && !strcmp(k, "Connect"))
            {
                lua_pushcfunction(L, RunService_Connect, nullptr);
                return 1;
            }
            return 0;
        }

        static void FireFrameEvents()
        {
            if (!g_L || g_cbs.empty())
                return;
            for (int id : g_cbs)
            {
                std::string key = "c" + std::to_string(id);
                lua_getglobal(g_L, kExecTable);
                if (!lua_istable(g_L, -1)) { lua_pop(g_L, 1); return; }
                lua_getfield(g_L, -1, "cbs");
                lua_getfield(g_L, -1, key.c_str());
                if (lua_isfunction(g_L, -1))
                {
                    if (lua_pcall(g_L, 0, 0, 0) != LUA_OK)
                    {
                        const char* err = lua_tostring(g_L, -1) ? lua_tostring(g_L, -1) : "unknown error";
                        Executor::ConsolePush(std::string("[executor] RunService error: ") + err);
                    }
                }
                lua_settop(g_L, 0);
            }
        }

        // ------------------------------------------------------------------
        // draw
        // ------------------------------------------------------------------
        static int draw_index(lua_State* L)
        {
            DrawObjUD* sd = static_cast<DrawObjUD*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!sd || !k)
                return 0;
            if (!strcmp(k, "remove") || !strcmp(k, "Destroy"))
            {
                lua_pushcfunction(L, draw_remove, nullptr);
                return 1;
            }
            DrawItem* it = FindItem(sd->id);
            if (!it)
                return 0;
            if (!strcmp(k, "Visible")) { lua_pushboolean(L, it->visible); return 1; }
            if (!strcmp(k, "Text") && it->kind == kText) { lua_pushstring(L, it->text.c_str()); return 1; }
            if (!strcmp(k, "Position") || !strcmp(k, "From")) { PushVector2(L, ImVec2(it->a.x, it->a.y)); return 1; }
            if (!strcmp(k, "To")) { PushVector2(L, ImVec2(it->b.x, it->b.y)); return 1; }
            if (!strcmp(k, "Size"))
            {
                if (it->kind == kRect)
                    PushVector2(L, ImVec2(it->b.x - it->a.x, it->b.y - it->a.y));
                else
                    lua_pushnumber(L, it->fontSize);
                return 1;
            }
            if (!strcmp(k, "Radius")) { lua_pushnumber(L, it->radius); return 1; }
            if (!strcmp(k, "Thickness")) { lua_pushnumber(L, it->thickness); return 1; }
            if (!strcmp(k, "NumSides")) { lua_pushnumber(L, it->numSides); return 1; }
            if (!strcmp(k, "Filled")) { lua_pushboolean(L, it->filled); return 1; }
            if (!strcmp(k, "Center")) { lua_pushboolean(L, it->center); return 1; }
            if (!strcmp(k, "Outline")) { lua_pushboolean(L, it->outline); return 1; }
            if (!strcmp(k, "Color")) { PushColor3(L, it->color); return 1; }
            return 0;
        }

        static int draw_newindex(lua_State* L)
        {
            DrawObjUD* sd = static_cast<DrawObjUD*>(lua_touserdata(L, 1));
            const char* k = lua_tostring(L, 2);
            if (!sd || !k)
                return 0;
            DrawItem* it = FindItem(sd->id);
            if (!it)
                return 0;
            ImVec2 v2;
            bool haveV2 = ReadVector2(L, 3, v2);
            ImU32 col;
            bool haveCol = ReadColor(L, 3, col);

            if (!strcmp(k, "Visible") && lua_isboolean(L, 3)) it->visible = lua_toboolean(L, 3) != 0;
            else if (!strcmp(k, "Text")) { const char* s = lua_tostring(L, 3); if (s) it->text = s; }
            else if ((!strcmp(k, "Position") || !strcmp(k, "From")) && haveV2) it->a = v2;
            else if (!strcmp(k, "To") && haveV2) it->b = v2;
            else if (!strcmp(k, "Size") && it->kind == kRect && haveV2) it->b = ImVec2(it->a.x + v2.x, it->a.y + v2.y);
            else if (!strcmp(k, "Size") && lua_isnumber(L, 3)) it->fontSize = (float)lua_tonumber(L, 3);
            else if (!strcmp(k, "Radius") && lua_isnumber(L, 3)) it->radius = (float)lua_tonumber(L, 3);
            else if (!strcmp(k, "Thickness") && lua_isnumber(L, 3)) it->thickness = (float)lua_tonumber(L, 3);
            else if (!strcmp(k, "NumSides") && lua_isnumber(L, 3)) it->numSides = (int)lua_tonumber(L, 3);
            else if (!strcmp(k, "Filled") && lua_isboolean(L, 3)) it->filled = lua_toboolean(L, 3) != 0;
            else if (!strcmp(k, "Center") && lua_isboolean(L, 3)) it->center = lua_toboolean(L, 3) != 0;
            else if (!strcmp(k, "Outline") && lua_isboolean(L, 3)) it->outline = lua_toboolean(L, 3) != 0;
            else if (!strcmp(k, "Color") && haveCol) it->color = col;
            return 0;
        }

        static int draw_remove(lua_State* L)
        {
            DrawObjUD* sd = static_cast<DrawObjUD*>(lua_touserdata(L, 1));
            if (sd)
            {
                for (auto it = g_items.begin(); it != g_items.end(); ++it)
                {
                    if (it->id == sd->id)
                    {
                        g_items.erase(it);
                        break;
                    }
                }
            }
            return 0;
        }

        static DrawKind DrawKindFromName(const char* name)
        {
            if (!name)
                return kText;
            if (!strcmp(name, "Line") || !strcmp(name, "line")) return kLine;
            if (!strcmp(name, "Square") || !strcmp(name, "square") || !strcmp(name, "Rect") || !strcmp(name, "rect")) return kRect;
            if (!strcmp(name, "Circle") || !strcmp(name, "circle")) return kCircle;
            if (!strcmp(name, "Triangle") || !strcmp(name, "triangle")) return kTriangle;
            if (!strcmp(name, "Quad") || !strcmp(name, "quad")) return kQuad;
            if (!strcmp(name, "Image") || !strcmp(name, "image")) return kImage;
            return kText;
        }

        static int DrawPush(lua_State* L, int kind)
        {
            DrawItem it;
            it.id = g_nextItemId++;
            it.kind = kind;
            it.a = ImVec2(0, 0);
            it.b = ImVec2(100, 20);
            it.color = IM_COL32(255, 255, 255, 255);
            it.thickness = (kind == kLine) ? 1.0f : 2.0f;

            if (lua_gettop(L) >= 2 && lua_istable(L, 2))
            {
                lua_pushnil(L);
                while (lua_next(L, 2) != 0)
                {
                    const char* key = lua_tostring(L, -2);
                    if (key)
                    {
                        ImVec2 v2;
                        ImU32 col;
                        if (ReadVector2(L, -1, v2))
                        {
                            if (!strcmp(key, "Position") || !strcmp(key, "From")) it.a = v2;
                            else if (!strcmp(key, "To")) it.b = v2;
                            else if (!strcmp(key, "Size") && kind == kRect) it.b = ImVec2(it.a.x + v2.x, it.a.y + v2.y);
                        }
                        else if (ReadColor(L, -1, col))
                        {
                            it.color = col;
                        }
                        else if (lua_isnumber(L, -1))
                        {
                            double n = lua_tonumber(L, -1);
                            if (!strcmp(key, "Thickness")) it.thickness = (float)n;
                            else if (!strcmp(key, "Radius")) it.radius = (float)n;
                            else if (!strcmp(key, "NumSides")) it.numSides = (int)n;
                            else if (!strcmp(key, "Size")) it.fontSize = (float)n;
                        }
                        else if (lua_isboolean(L, -1))
                        {
                            bool b = lua_toboolean(L, -1) != 0;
                            if (!strcmp(key, "Filled")) it.filled = b;
                            else if (!strcmp(key, "Center")) it.center = b;
                            else if (!strcmp(key, "Outline")) it.outline = b;
                            else if (!strcmp(key, "Visible")) it.visible = b;
                        }
                        else if (lua_isstring(L, -1))
                        {
                            if (!strcmp(key, "Text")) it.text = lua_tostring(L, -1);
                        }
                    }
                    lua_pop(L, 1);
                }
            }
            g_items.push_back(std::move(it));

            DrawObjUD* sd = static_cast<DrawObjUD*>(lua_newuserdata(L, sizeof(DrawObjUD)));
            sd->id = g_items.back().id;
            luaL_getmetatable(L, MT_DRAW);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int draw_create(lua_State* L)
        {
            const char* name = lua_tostring(L, 1);
            return DrawPush(L, (int)DrawKindFromName(name));
        }

        static int draw_text(lua_State* L) { return DrawPush(L, kText); }
        static int draw_line(lua_State* L) { return DrawPush(L, kLine); }
        static int draw_square(lua_State* L) { return DrawPush(L, kRect); }

        static int draw_circle(lua_State* L)
        {
            int r = DrawPush(L, kCircle);
            DrawObjUD* sd = static_cast<DrawObjUD*>(lua_touserdata(L, -1));
            if (sd)
            {
                DrawItem* it = FindItem(sd->id);
                if (it)
                {
                    it->radius = 16.0f;
                    it->thickness = 1.0f;
                }
            }
            return r;
        }

        static int draw_text_size(lua_State* L)
        {
            const char* text = lua_tostring(L, 1);
            float size = (float)luaL_optnumber(L, 2, kDefaultFontSize);
            if (!text)
                text = "";
            ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            PushVector2(L, ImVec2(sz.x, sz.y));
            return 1;
        }

        static int draw_clear(lua_State*)
        {
            g_items.clear();
            return 0;
        }

        // Extended Drawing API
        static int draw_image(lua_State* L)
        {
            // draw.image(path, position, size)
            // For now, create a square as placeholder
            return DrawPush(L, kRect);
        }

        static int draw_triangle(lua_State* L)
        {
            return DrawPush(L, kTriangle);
        }

        static int draw_quad(lua_State* L)
        {
            return DrawPush(L, kQuad);
        }

        // loadstring - compile and return a function
        static int lua_loadstring(lua_State* L)
        {
            const char* src = lua_tostring(L, 1);
            if (!src)
            {
                lua_pushnil(L);
                lua_pushstring(L, "invalid argument to loadstring");
                return 2;
            }

            lua_CompileOptions opts = {};
            opts.optimizationLevel = 1;
            opts.debugLevel = 1;
            opts.typeInfoLevel = 0;
            opts.coverageLevel = 0;
            opts.vectorLib = "vector";
            opts.vectorCtor = "create";
            opts.vectorType = "vector";

            size_t bytecodeSize = 0;
            char* bytecode = luau_compile(src, strlen(src), &opts, &bytecodeSize);
            if (!bytecode || bytecodeSize == 0)
            {
                lua_pushnil(L);
                lua_pushstring(L, "compilation failed");
                return 2;
            }

            int result = luau_load(L, "loadstring", bytecode, bytecodeSize, 0);
            free(bytecode);

            if (result != 0)
            {
                // luau_load pushes error on stack
                lua_pushnil(L);
                lua_insert(L, -2);
                return 2;
            }

            // luau_load leaves the function on top of stack
            return 1;
        }

        // getgenv - return global environment
        static int lua_getgenv(lua_State* L)
        {
            lua_pushvalue(L, LUA_GLOBALSINDEX);
            return 1;
        }

        // setfenv - set function environment
        static int lua_setfenv(lua_State* L)
        {
            // setfenv(f, table)
            // Simplified: not fully implemented
            return 0;
        }

        // hookfunction - hook a function
        static int lua_hookfunction(lua_State* L)
        {
            // hookfunction(original, replacement)
            // Simplified: return original
            lua_pushvalue(L, 1);
            return 1;
        }

        // hookmetamethod - hook a metamethod
        static int lua_hookmetamethod(lua_State* L)
        {
            // hookmetamethod(instance, metamethod, replacement)
            // Simplified: return original
            lua_pushvalue(L, 1);
            return 1;
        }

        static int lua_iscclosure(lua_State* L)
        {
            lua_pushboolean(L, lua_iscfunction(L, 1));
            return 1;
        }

        static int lua_islclosure(lua_State* L)
        {
            lua_pushboolean(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1));
            return 1;
        }

        static int lua_newcclosure(lua_State* L)
        {
            // newcclosure(f) - create C closure from Lua function
            // Just return the function as-is
            lua_pushvalue(L, 1);
            return 1;
        }

        static int lua_checkcaller(lua_State* L)
        {
            // checkcaller() - returns true if caller is from script
            lua_pushboolean(L, 1);
            return 1;
        }

        // ------------------------------------------------------------------
        // registration
        // ------------------------------------------------------------------
        static void SetGlobalFn(lua_State* L, const char* name, lua_CFunction fn)
        {
            lua_pushcfunction(L, fn, nullptr);
            lua_setglobal(L, name);
        }

        static int color3_tostring(lua_State* L)
        {
            Color3UD* c = static_cast<Color3UD*>(lua_touserdata(L, 1));
            char buf[96];
            snprintf(buf, sizeof(buf), "Color3(%.2f, %.2f, %.2f)", c->r, c->g, c->b);
            lua_pushstring(L, buf);
            return 1;
        }

        static void RegisterApi(lua_State* L)
        {
            lua_newtable(L);
            lua_newtable(L);
            lua_setfield(L, -2, "cbs");
            lua_setglobal(L, kExecTable);

            // Vector2
            luaL_newmetatable(L, MT_VEC2);
            lua_pushcfunction(L, v2_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, v2_newindex, nullptr); lua_setfield(L, -2, "__newindex");
            lua_pushcfunction(L, v2_unm, nullptr); lua_setfield(L, -2, "__unm");
            lua_pushcfunction(L, v2_eq, nullptr); lua_setfield(L, -2, "__eq");
            lua_pushcfunction(L, v2_tostring, nullptr); lua_setfield(L, -2, "__tostring");
            const char* arithNames[4] = {"__add", "__sub", "__mul", "__div"};
            lua_CFunction arithFns[4] = {v2_add, v2_sub, v2_mul, v2_div};
            for (int op = 0; op < 4; ++op)
            {
                lua_pushcfunction(L, arithFns[op], nullptr);
                lua_setfield(L, -2, arithNames[op]);
            }
            lua_pop(L, 1);

            // Color3
            luaL_newmetatable(L, MT_COLOR3);
            lua_pushcfunction(L, c3_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, color3_tostring, nullptr); lua_setfield(L, -2, "__tostring");
            lua_pop(L, 1);

            // Instance
            luaL_newmetatable(L, MT_OBJ);
            lua_pushcfunction(L, obj_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, obj_newindex, nullptr); lua_setfield(L, -2, "__newindex");
            lua_pushcfunction(L, obj_len, nullptr); lua_setfield(L, -2, "__len");
            lua_pushcfunction(L, obj_call, nullptr); lua_setfield(L, -2, "__call");
            lua_pushcfunction(L, obj_tostring, nullptr); lua_setfield(L, -2, "__tostring");
            lua_pop(L, 1);

            // Draw item
            luaL_newmetatable(L, MT_DRAW);
            lua_pushcfunction(L, draw_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, draw_newindex, nullptr); lua_setfield(L, -2, "__newindex");
            lua_pushcfunction(L, draw_remove, nullptr); lua_setfield(L, -2, "__gc");
            lua_pop(L, 1);

            // Event signal (RunService)
            luaL_newmetatable(L, MT_EVENT);
            lua_pushcfunction(L, RunService_event_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pop(L, 1);

            // RBXScriptSignal (generic events)
            luaL_newmetatable(L, MT_SIGNAL);
            lua_pushcfunction(L, Signal_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pop(L, 1);

            // UDim2
            luaL_newmetatable(L, MT_UDIM2);
            lua_pushcfunction(L, udim2_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, udim2_tostring, nullptr); lua_setfield(L, -2, "__tostring");
            lua_pop(L, 1);

            // UDim
            luaL_newmetatable(L, MT_UDIM);
            lua_pushcfunction(L, udim_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, udim_tostring, nullptr); lua_setfield(L, -2, "__tostring");
            lua_pop(L, 1);

            // ColorSequence
            luaL_newmetatable(L, MT_COLORSEQ);
            lua_pop(L, 1);

            // TweenInfo
            luaL_newmetatable(L, MT_TWEENINFO);
            lua_pushcfunction(L, tweeninfo_index, nullptr); lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, tweeninfo_tostring, nullptr); lua_setfield(L, -2, "__tostring");
            lua_pop(L, 1);

            // Enum (static table, no metatable needed)
            lua_newtable(L);
            // Enum.Font
            lua_newtable(L);
            lua_pushinteger(L, 0); lua_setfield(L, -2, "Legacy");
            lua_pushinteger(L, 1); lua_setfield(L, -2, "Arial");
            lua_pushinteger(L, 2); lua_setfield(L, -2, "ArialBold");
            lua_pushinteger(L, 3); lua_setfield(L, -2, "SourceSans");
            lua_pushinteger(L, 4); lua_setfield(L, -2, "SourceSansBold");
            lua_pushinteger(L, 5); lua_setfield(L, -2, "SourceSansSemibold");
            lua_pushinteger(L, 6); lua_setfield(L, -2, "SourceSansLight");
            lua_pushinteger(L, 7); lua_setfield(L, -2, "SourceSansItalic");
            lua_pushinteger(L, 8); lua_setfield(L, -2, "Bodoni");
            lua_pushinteger(L, 9); lua_setfield(L, -2, "Garamond");
            lua_pushinteger(L, 10); lua_setfield(L, -2, "Cartoon");
            lua_pushinteger(L, 11); lua_setfield(L, -2, "Code");
            lua_pushinteger(L, 12); lua_setfield(L, -2, "Highway");
            lua_pushinteger(L, 13); lua_setfield(L, -2, "SciFi");
            lua_pushinteger(L, 14); lua_setfield(L, -2, "Arcade");
            lua_pushinteger(L, 15); lua_setfield(L, -2, "Fantasy");
            lua_pushinteger(L, 16); lua_setfield(L, -2, "Antique");
            lua_pushinteger(L, 17); lua_setfield(L, -2, "Gotham");
            lua_pushinteger(L, 18); lua_setfield(L, -2, "GothamSemibold");
            lua_pushinteger(L, 19); lua_setfield(L, -2, "GothamBold");
            lua_pushinteger(L, 20); lua_setfield(L, -2, "GothamBlack");
            lua_pushinteger(L, 21); lua_setfield(L, -2, "Oswald");
            lua_pushinteger(L, 22); lua_setfield(L, -2, "Ubuntu");
            lua_setfield(L, -2, "Font");
            // Enum.FrameStyle
            lua_newtable(L);
            lua_pushinteger(L, 0); lua_setfield(L, -2, "Custom");
            lua_pushinteger(L, 1); lua_setfield(L, -2, "ChatBlue");
            lua_pushinteger(L, 2); lua_setfield(L, -2, "ChatGreen");
            lua_pushinteger(L, 3); lua_setfield(L, -2, "ChatRed");
            lua_pushinteger(L, 4); lua_setfield(L, -2, "DropShadow");
            lua_pushinteger(L, 5); lua_setfield(L, -2, "RobloxRound");
            lua_pushinteger(L, 6); lua_setfield(L, -2, "RobloxSquare");
            lua_setfield(L, -2, "FrameStyle");
            // Enum.TextXAlignment
            lua_newtable(L);
            lua_pushinteger(L, 0); lua_setfield(L, -2, "Left");
            lua_pushinteger(L, 1); lua_setfield(L, -2, "Center");
            lua_pushinteger(L, 2); lua_setfield(L, -2, "Right");
            lua_setfield(L, -2, "TextXAlignment");
            // Enum.TextYAlignment
            lua_newtable(L);
            lua_pushinteger(L, 0); lua_setfield(L, -2, "Top");
            lua_pushinteger(L, 1); lua_setfield(L, -2, "Center");
            lua_pushinteger(L, 2); lua_setfield(L, -2, "Bottom");
            lua_setfield(L, -2, "TextYAlignment");
            // Enum.EasingStyle
            lua_newtable(L);
            lua_pushinteger(L, 0); lua_setfield(L, -2, "Linear");
            lua_pushinteger(L, 1); lua_setfield(L, -2, "Sine");
            lua_pushinteger(L, 2); lua_setfield(L, -2, "Back");
            lua_pushinteger(L, 3); lua_setfield(L, -2, "Quad");
            lua_pushinteger(L, 4); lua_setfield(L, -2, "Quart");
            lua_pushinteger(L, 5); lua_setfield(L, -2, "Quint");
            lua_pushinteger(L, 6); lua_setfield(L, -2, "Bounce");
            lua_pushinteger(L, 7); lua_setfield(L, -2, "Elastic");
            lua_pushinteger(L, 8); lua_setfield(L, -2, "Exponential");
            lua_pushinteger(L, 9); lua_setfield(L, -2, "Circular");
            lua_pushinteger(L, 10); lua_setfield(L, -2, "Cubic");
            lua_setfield(L, -2, "EasingStyle");
            // Enum.EasingDirection
            lua_newtable(L);
            lua_pushinteger(L, 0); lua_setfield(L, -2, "In");
            lua_pushinteger(L, 1); lua_setfield(L, -2, "Out");
            lua_pushinteger(L, 2); lua_setfield(L, -2, "InOut");
            lua_setfield(L, -2, "EasingDirection");
            // Enum.MouseButton
            lua_newtable(L);
            lua_pushinteger(L, 0); lua_setfield(L, -2, "Left");
            lua_pushinteger(L, 1); lua_setfield(L, -2, "Right");
            lua_setfield(L, -2, "MouseButton");
            lua_setglobal(L, "Enum");

            // base globals
            SetGlobalFn(L, "print", lua_script_print);
            SetGlobalFn(L, "warn", lua_script_print);
            SetGlobalFn(L, "wait", lua_wait);
            SetGlobalFn(L, "spawn", lua_spawn);
            SetGlobalFn(L, "tick", lua_tick);
            SetGlobalFn(L, "time", lua_tick);

            // task
            lua_newtable(L);
            lua_pushcfunction(L, lua_wait, nullptr); lua_setfield(L, -2, "wait");
            lua_pushcfunction(L, lua_spawn, nullptr); lua_setfield(L, -2, "spawn");
            lua_setglobal(L, "task");

            // Vector2 / Vector3 / Color3 constructor tables
            lua_newtable(L);
            lua_pushcfunction(L, lua_Vector2_new, nullptr); lua_setfield(L, -2, "new");
            lua_setglobal(L, "Vector2");

            lua_newtable(L);
            lua_getglobal(L, "vector");
            lua_getfield(L, -1, "create");
            lua_setfield(L, -3, "new");
            lua_pop(L, 1);
            lua_setglobal(L, "Vector3");

            lua_newtable(L);
            lua_pushcfunction(L, lua_Color3_new, nullptr); lua_setfield(L, -2, "new");
            lua_pushcfunction(L, lua_Color3_fromRGB, nullptr); lua_setfield(L, -2, "fromRGB");
            lua_setglobal(L, "Color3");

            // UDim2
            lua_newtable(L);
            lua_pushcfunction(L, lua_UDim2_new, nullptr); lua_setfield(L, -2, "new");
            lua_pushcfunction(L, lua_UDim2_fromScale, nullptr); lua_setfield(L, -2, "fromScale");
            lua_pushcfunction(L, lua_UDim2_fromOffset, nullptr); lua_setfield(L, -2, "fromOffset");
            lua_setglobal(L, "UDim2");

            // UDim
            lua_newtable(L);
            lua_pushcfunction(L, lua_UDim_new, nullptr); lua_setfield(L, -2, "new");
            lua_setglobal(L, "UDim");

            // ColorSequence
            lua_newtable(L);
            lua_pushcfunction(L, lua_ColorSequence_new, nullptr); lua_setfield(L, -2, "new");
            lua_setglobal(L, "ColorSequence");

            // TweenInfo
            lua_newtable(L);
            lua_pushcfunction(L, lua_TweenInfo_new, nullptr); lua_setfield(L, -2, "new");
            lua_setglobal(L, "TweenInfo");

            // Signal (for BindableEvent-like events)
            lua_newtable(L);
            lua_pushcfunction(L, lua_Signal_new, nullptr); lua_setfield(L, -2, "new");
            lua_setglobal(L, "Signal");

            // Instance.new (stub - creates fake instance for API compatibility)
            SetGlobalFn(L, "Instance", [] (lua_State* L) -> int {
                const char* className = lua_tostring(L, 1);
                if (!className) return 0;
                
                // Handle BindableEvent / RemoteEvent -> return a Signal
                if (!strcmp(className, "BindableEvent") || !strcmp(className, "RemoteEvent") || !strcmp(className, "BindableFunction") || !strcmp(className, "RemoteFunction"))
                {
                    int cbId = g_nextSignalId++;
                    g_signalCallbacks.push_back({cbId, true, nullptr});

                    ConnUD* c = static_cast<ConnUD*>(lua_newuserdata(L, sizeof(ConnUD)));
                    c->cbId = cbId;
                    c->connected = true;

                    lua_createtable(L, 0, 3);
                    lua_pushcfunction(L, RunService_Disconnect, "Disconnect");
                    lua_setfield(L, -2, "Disconnect");
                    lua_pushcfunction(L, Signal_Connect, "Connect");
                    lua_setfield(L, -2, "Connect");
                    lua_pushcfunction(L, Signal_Wait, "Wait");
                    lua_setfield(L, -2, "Wait");
                    lua_pushcfunction(L, Signal_Fire, "Fire");
                    lua_setfield(L, -2, "Fire");
                    lua_setmetatable(L, -2);
                    return 1;
                }

                // For other classes, return a basic instance userdata (kPart kind)
                // Real Instance.new requires Roblox DataModel::CreateInstance call
                LuaObj* o = static_cast<LuaObj*>(lua_newuserdata(L, sizeof(LuaObj)));
                o->kind = kPart;
                o->addr = 0; // null address = fake instance
                luaL_getmetatable(L, MT_OBJ);
                lua_setmetatable(L, -2);
                return 1;
            });

            // game / workspace / Players
            push_obj(L, kGame, 0);
            lua_setglobal(L, "game");
            push_obj(L, kWorkspace, Globals::Roblox::Workspace.address);
            lua_setglobal(L, "workspace");
            push_obj(L, kPlayers, Globals::Roblox::Players.address);
            lua_setglobal(L, "Players");

            // draw
            lua_newtable(L);
            lua_pushcfunction(L, draw_create, nullptr); lua_setfield(L, -2, "create");
            lua_pushcfunction(L, draw_text, nullptr); lua_setfield(L, -2, "text");
            lua_pushcfunction(L, draw_line, nullptr); lua_setfield(L, -2, "line");
            lua_pushcfunction(L, draw_square, nullptr); lua_setfield(L, -2, "square");
            lua_pushcfunction(L, draw_circle, nullptr); lua_setfield(L, -2, "circle");
            lua_pushcfunction(L, draw_text_size, nullptr); lua_setfield(L, -2, "text_size");
            lua_pushcfunction(L, draw_clear, nullptr); lua_setfield(L, -2, "clear");
            // Extended Drawing API
            lua_pushcfunction(L, draw_image, nullptr); lua_setfield(L, -2, "image");
            lua_pushcfunction(L, draw_triangle, nullptr); lua_setfield(L, -2, "triangle");
            lua_pushcfunction(L, draw_quad, nullptr); lua_setfield(L, -2, "quad");
            lua_setglobal(L, "draw");

            // loadstring
            SetGlobalFn(L, "loadstring", lua_loadstring);

            // getgenv / setfenv / hookfunction
            SetGlobalFn(L, "getgenv", lua_getgenv);
            SetGlobalFn(L, "setfenv", lua_setfenv);
            SetGlobalFn(L, "hookfunction", lua_hookfunction);
            SetGlobalFn(L, "hookmetamethod", lua_hookmetamethod);
            SetGlobalFn(L, "iscclosure", lua_iscclosure);
            SetGlobalFn(L, "islclosure", lua_islclosure);
            SetGlobalFn(L, "newcclosure", lua_newcclosure);
            SetGlobalFn(L, "checkcaller", lua_checkcaller);
        }
    } // anonymous namespace

    // ------------------------------------------------------------------
    // public API
    // ------------------------------------------------------------------
    void Initialize()
    {
        OutputDebugStringA("[Executor] Initialize() called\n");
        std::call_once(g_initFlag, []()
        {
            OutputDebugStringA("[Executor] call_once executing\n");
            g_L = luaL_newstate();
            if (!g_L)
            {
                OutputDebugStringA("[Executor] luaL_newstate failed\n");
                return;
            }
            OutputDebugStringA("[Executor] luaL_newstate OK\n");
            luaL_openlibs(g_L);
            lua_callbacks(g_L)->interrupt = InterruptHook;
            RegisterApi(g_L);
            ConsolePush("[executor] ready (Luau)");
            OutputDebugStringA("[Executor] Initialize complete\n");
        });
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_L)
        {
            try
            {
                StopLocked();
                lua_close(g_L);
            }
            catch (...)
            {
            }
            g_L = nullptr;
        }
    }

    void Run(const std::string& code)
    {
        OutputDebugStringA(("[Executor] Run called, code length: " + std::to_string(code.length()) + "\n").c_str());
        ConsolePush("[executor] Run: compiling script (" + std::to_string(code.length()) + " chars)...");
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_L)
        {
            OutputDebugStringA("[Executor] Run: no Lua state\n");
            return;
        }
        try
        {
            ConsolePush("[executor] compiling script...");
            StopLocked();

            std::string bytecode;
            try
            {
                bytecode = Luau::compile(code, Luau::CompileOptions{});
            }
            catch (const std::exception& e)
            {
                ConsolePush(std::string("[executor] compile error: ") + e.what());
                return;
            }
            if (bytecode.empty())
            {
                ConsolePush("[executor] compile produced no bytecode");
                return;
            }

            int status = luau_load(g_L, "@executor", bytecode.data(), bytecode.size(), 0);
            if (status != LUA_OK)
            {
                const char* msg = lua_tostring(g_L, -1) ? lua_tostring(g_L, -1) : "load failed";
                ConsolePush(std::string("[executor] ") + msg);
                lua_settop(g_L, 0);
                return;
            }

            int base = lua_gettop(g_L);           // fn at top
            lua_State* T = lua_newthread(g_L);    // [.., fn, T]
            lua_pushvalue(g_L, base);             // [.., fn, T, fn] (copy fn above T)
            lua_xmove(g_L, T, 1);                 // top (fn) -> T; g_L [.., fn, T]
            StoreThreadRef(T, "main");            // strong ref in __EXEC; g_L back to [.., fn, T]
            lua_settop(g_L, base);                // [.., fn] (drop scratch T; fn left on g_L is inert)

            g_script = Runner{T, 0.0, false, "main"};
            ExecLog("[Run] thread=%p compiled=%zuB resuming", (void*)T, bytecode.size());
            int r = Resume(g_script);
            ExecLog("[Run] first resume status=%d", r);
            if (r == LUA_YIELD)
            {
                // suspended until wait() elapses
            }
            else if (r == LUA_OK)
            {
                ConsolePush("[executor] script finished");
                StopLocked();
            }
            else
            {
                ReportRunnerError(g_script);
                StopLocked();
            }
        }
        catch (const std::exception&)
        {
            ConsolePush("[executor] host exception while starting script");
            StopLocked();
        }
    }

    void Stop()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_L)
            return;
        StopLocked();
        ConsolePush("[executor] script stopped");
    }

    bool IsRunning()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_script.thread != nullptr;
    }

    void Tick()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_L)
            return;
        const double now = Now();

        if (g_script.thread && g_script.waiting && now >= g_script.wake)
        {
            g_script.waiting = false;
            ExecLog("[Tick] resuming main");
            int r = Resume(g_script);
            ExecLog("[Tick] main resume status=%d", r);
            if (r == LUA_YIELD)
            {
                // stays pending; next Tick re-checks its wake time
            }
            else if (r == LUA_OK)
            {
                ConsolePush("[executor] script finished");
                StopLocked();
            }
            else
            {
                ReportRunnerError(g_script);
                StopLocked();
            }
        }

        for (auto it = g_goroutines.begin(); it != g_goroutines.end(); )
        {
            Runner r = *it;
            if (!r.thread)
            {
                it = g_goroutines.erase(it);
                continue;
            }
            if (r.waiting && now < r.wake)
            {
                ++it;
                continue;
            }
            r.waiting = false;
            int rc = Resume(r);
            if (rc == LUA_YIELD)
            {
                *it = r;
                ++it;
                continue;
            }
            if (rc == LUA_OK)
            {
                ConsolePush("[executor] task " + r.key + " finished");
            }
            else
            {
                ReportRunnerError(r);
            }
            ClearThreadRef(r.key);
            it = g_goroutines.erase(it);
        }

        FireFrameEvents();
    }

    void RenderOverlay(ImDrawList* dl)
    {
        if (!dl)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& it : g_items)
        {
            if (!it.visible)
                continue;
            switch (it.kind)
            {
            case kText:
            {
                ImFont* font = ImGui::GetFont();
                float x = it.a.x;
                float y = it.a.y;
                if (it.center)
                {
                    ImVec2 sz = font->CalcTextSizeA(it.fontSize, FLT_MAX, 0.0f, it.text.c_str());
                    x -= sz.x * 0.5f;
                    y -= sz.y * 0.5f;
                }
                if (it.outline)
                {
                    dl->AddText(font, it.fontSize, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 190), it.text.c_str());
                    dl->AddText(font, it.fontSize, ImVec2(x - 1, y + 1), IM_COL32(0, 0, 0, 190), it.text.c_str());
                    dl->AddText(font, it.fontSize, ImVec2(x + 1, y - 1), IM_COL32(0, 0, 0, 190), it.text.c_str());
                    dl->AddText(font, it.fontSize, ImVec2(x - 1, y - 1), IM_COL32(0, 0, 0, 190), it.text.c_str());
                }
                dl->AddText(font, it.fontSize, ImVec2(x, y), it.color, it.text.c_str());
                break;
            }
            case kLine:
                dl->AddLine(it.a, it.b, it.color, it.thickness);
                break;
            case kRect:
                if (it.filled)
                    dl->AddRectFilled(it.a, it.b, it.color);
                else
                    dl->AddRect(it.a, it.b, it.color, 0.0f, 0, it.thickness);
                break;
            case kCircle:
                if (it.filled)
                    dl->AddCircleFilled(it.a, it.radius, it.color, it.numSides);
                else
                    dl->AddCircle(it.a, it.radius, it.color, it.numSides, it.thickness);
                break;
            case kTriangle:
                // Triangle: it.a = point1, it.b = point2, radius/point3 stored differently
                // For simplicity, use a, b, and a third point derived from radius
                dl->AddTriangleFilled(it.a, it.b, ImVec2(it.a.x + it.radius, it.a.y), it.color);
                break;
            case kQuad:
                // Quad: it.a = topleft, it.b = bottomright (rect)
                if (it.filled)
                    dl->AddRectFilled(it.a, it.b, it.color);
                else
                    dl->AddRect(it.a, it.b, it.color, 0.0f, 0, it.thickness);
                break;
            case kImage:
                // Image placeholder - render as rect
                dl->AddRect(it.a, it.b, it.color);
                break;
            }
        }

        // on-screen execution status: visible on the game screen without the menu
        static long long lastActive = -1000000;
        if (g_script.thread)
            lastActive = Now();
        if (g_script.thread || Now() - lastActive < 3000)
        {
            ImFont* font = ImGui::GetFont();
            const int tailN = 6;
            int n = (int)g_console.size();
            int start = std::max(0, n - tailN);
            std::string status = g_script.thread ? "RUNNING..." : "finished";
            float maxW = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, status.c_str()).x;
            for (int i = start; i < n; ++i)
                maxW = std::max(maxW, font->CalcTextSizeA(12.5f, FLT_MAX, 0.0f, g_console[i].c_str()).x);
            const float w = maxW + 24.0f;
            const int rows = n > start ? n - start : 0;
            const float h = rows * 17.0f + 38.0f;
            ImVec2 r0(ImGui::GetIO().DisplaySize.x - w - 12.0f, 28.0f);
            ImVec2 r1(r0.x + w, r0.y + h);
            dl->AddRectFilled(r0, r1, IM_COL32(10, 12, 16, 210), 5.0f);
            dl->AddRect(r0, r1, IM_COL32(255, 255, 255, 40), 5.0f, 0, 1.0f);
            dl->AddText(font, 13.0f, ImVec2(r0.x + 10.0f, r0.y + 7.0f),
                        g_script.thread ? IM_COL32(120, 255, 140, 255) : IM_COL32(220, 220, 220, 255), status.c_str());
            float ly = r0.y + 28.0f;
            for (int i = start; i < n; ++i)
            {
                const std::string& line = g_console[i];
                ImU32 col = IM_COL32(230, 230, 235, 255);
                if (line.rfind("[executor] error", 0) == 0)
                    col = IM_COL32(255, 110, 110, 255);
                else if (line.rfind("[executor]", 0) == 0)
                    col = IM_COL32(160, 220, 255, 255);
                dl->AddText(font, 12.5f, ImVec2(r0.x + 10.0f, ly), col, line.c_str());
                ly += 17.0f;
            }
        }
    }

    std::vector<std::string> ConsoleSnapshot()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_console;
    }

    void ClearConsole()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_console.clear();
    }

    void ConsolePush(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_console.size() >= static_cast<size_t>(kConsoleLimit))
            g_console.erase(g_console.begin());
        g_console.push_back(line);
        ExecLog("%s", line.c_str());
    }
} // namespace Executor