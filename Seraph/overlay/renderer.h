#pragma once
#pragma comment (lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#include <dwmapi.h>
#include <Windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <algorithm>
#include <string>
#include <thread>
#include <iostream>
#include <filesystem>
#include <map>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/KeyBind.h"
#include "utils/Header.h"
#include "utils/utils.h"
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../features/esp.h"
#include "../features/aimbot.h"
#include "../features/triggerbot.h"
#include "../features/macro.h"
#include "../features/crosshair.h"
#include "../features/combatfeedback.h"
#include "utils/W2S.h"

extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;
extern IDXGISwapChain* g_pSwapChain;
extern bool g_SwapChainOccluded;
extern UINT g_ResizeWidth, g_ResizeHeight;
extern ID3D11RenderTargetView* g_mainRenderTargetView;
extern ImVec4 main_color;
extern ImFont* InterMedium;
extern ImFont* IconFont;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool IsGameOnTop(const std::string& expectedTitle);
void SetTransparency(HWND hwnd, bool boolean);
void DrawNode(RobloxInstance& node);
void ShowImgui();

void RenderKeybindList(ImDrawList* drawList);

// Custom tab/subtab renderers from menu
bool renderer_tab(const char* label, const char* icon, bool selected);
bool renderer_subtab(const char* label, const ImVec2& size_arg, const bool selected);
