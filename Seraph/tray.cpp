#include "tray.h"
#include <windows.h>

// Forward declarations already in tray.h

// Implementation of InitTray runs a message loop on a separate thread.
void InitTray() {
    g_TrayRunning = true;
    AddTrayIcon();
    TrayMessageLoop();
}

void ShutdownTray() {
    g_TrayRunning = false;
    RemoveTrayIcon();
    if (g_TrayMsgWnd) {
        PostMessageW(g_TrayMsgWnd, WM_QUIT, 0, 0);
    }
}
