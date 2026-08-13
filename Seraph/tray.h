#pragma once
#include <windows.h>
#include <shellapi.h>
#include <thread>

#define IDI_ICON1 101

#pragma comment(lib, "Shell32.lib")

// Unique message ID for tray icon callbacks
#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 1

static NOTIFYICONDATAW g_TrayIconData = {};
static HWND            g_TrayMsgWnd   = NULL;
static bool            g_TrayRunning  = false;

// Minimal hidden message-only window proc for tray icon events
static LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TRAYICON)
    {
        // No context menu needed – icon is purely informational
        return 0;
    }
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Add the tray icon with the tooltip "Fleasion - Running"
static void AddTrayIcon()
{
    // Register a minimal message-only window class
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = TrayWndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = L"FleasionTrayClass";
    RegisterClassExW(&wc);

    // Create a message-only window (invisible, no taskbar entry)
    g_TrayMsgWnd = CreateWindowExW(
        0, L"FleasionTrayClass", L"FleasionTray",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);

    if (!g_TrayMsgWnd)
        return;

    // Build the NOTIFYICONDATA structure
    g_TrayIconData              = {};
    g_TrayIconData.cbSize       = sizeof(NOTIFYICONDATAW);
    g_TrayIconData.hWnd         = g_TrayMsgWnd;
    g_TrayIconData.uID          = TRAY_ICON_ID;
    g_TrayIconData.uFlags       = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_TrayIconData.uCallbackMessage = WM_TRAYICON;

    // Use the embedded application icon from seraph.ico
    g_TrayIconData.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_ICON1));

    // Tooltip shown in the hidden icons menu – max 128 wide chars (NOTIFYICONADATA v2)
    wcscpy_s(g_TrayIconData.szTip, L"Fleasion - Running");

    Shell_NotifyIconW(NIM_ADD, &g_TrayIconData);
}

// Remove the tray icon cleanly
static void RemoveTrayIcon()
{
    if (g_TrayMsgWnd)
    {
        Shell_NotifyIconW(NIM_DELETE, &g_TrayIconData);
        DestroyWindow(g_TrayMsgWnd);
        g_TrayMsgWnd = NULL;
    }
}

// Pump the message-only window so tray callbacks are processed
static void TrayMessageLoop()
{
    MSG msg = {};
    while (g_TrayRunning && GetMessageW(&msg, g_TrayMsgWnd, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Forward declarations – implementations are in tray.cpp
void InitTray();
void ShutdownTray();
