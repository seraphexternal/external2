#include "renderer.h"
#include "../rbx/configs/configs.h"
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;


bool IsGameOnTop(const std::string& expectedTitle) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;

    char windowTitle[256];
    int length = GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));

    if (length == 0) return false;

    return expectedTitle == std::string(windowTitle);
}

void ApplyOverlayWindowStyle(HWND hwnd, bool clickThrough)
{
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;

    if (clickThrough)
        exStyle |= WS_EX_TRANSPARENT;
    else
        exStyle &= ~WS_EX_TRANSPARENT;

    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void SetTransparency(HWND hwnd, bool clickThrough)
{
    ApplyOverlayWindowStyle(hwnd, clickThrough);
}

void HideFromTaskbar(HWND hwnd)
{
    ApplyOverlayWindowStyle(hwnd, false);

    ITaskbarList* taskbarList = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList, reinterpret_cast<void**>(&taskbarList))))
    {
        taskbarList->HrInit();
        taskbarList->DeleteTab(hwnd);
        taskbarList->Release();
    }
}

void DrawNode(RobloxInstance& node)
{
    const auto& children = node.GetChildren();
    if (children.empty())
    {
        ImGui::BulletText(node.Name().c_str());
    }
    else
    {
        if (ImGui::TreeNode(node.Name().c_str()))
        {
            for (auto child : children)
            {
                DrawNode(child);
            }
            ImGui::TreePop();
        }
    }
}

static void PushConfigTabTheme(int* outColorCount)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(main_color.x, main_color.y, main_color.z, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(main_color.x, main_color.y, main_color.z, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.06f, 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(main_color.x, main_color.y, main_color.z, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(main_color.x, main_color.y, main_color.z, 0.38f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(main_color.x, main_color.y, main_color.z, 0.52f));
    *outColorCount = 8;
}

static void RenderConfigTab()
{
    static char configNameBuffer[128] = "";
    static std::vector<std::string> configsList;
    static int selectedConfigIndex = -1;
    static std::string configStatusMessage;
    static bool configStatusSuccess = true;
    static bool listInitialized = false;
    static AutoloadSettings autoloadSettings;
    static bool autoloadEnabled = false;

    if (!listInitialized)
    {
        configsList = ListConfigFiles();
        autoloadSettings = LoadAutoloadSettings();
        autoloadEnabled = autoloadSettings.enabled;
        listInitialized = true;
    }

    int themeColorCount = 0;
    PushConfigTabTheme(&themeColorCount);

    ImGui::SetCursorPosY(38);
    ImGui::SetCursorPosX(122);
    ImGui::MenuChild("Manage Configs", ImVec2(226, 337), false);
    {
        ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
        if (ImGui::Checkbox("Autoload on startup", &autoloadEnabled))
        {
            autoloadSettings.enabled = autoloadEnabled;
            if (SaveAutoloadSettings(autoloadSettings))
            {
                configStatusSuccess = true;
                configStatusMessage = autoloadEnabled ? "Autoload enabled" : "Autoload disabled";
            }
            else
            {
                configStatusSuccess = false;
                configStatusMessage = Config::lastError.empty() ? "Failed to save autoload setting" : Config::lastError;
            }
        }
        ImGui::PopStyleColor(1);

        if (autoloadSettings.configName.empty())
            ImGui::TextDisabled("No autoload config set");
        else
            ImGui::TextColored(main_color, "Autoload: %s", autoloadSettings.configName.c_str());

        ImGui::Dummy(ImVec2(0, 4));

        if (ImGui::Button("Refresh List", ImVec2(-1, 24)))
            configsList = ListConfigFiles();

        ImGui::TextDisabled("%d config(s)", (int)configsList.size());
        ImGui::Dummy(ImVec2(0, 4));

        const float listHeight = ImGui::GetContentRegionAvail().y;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        ImGui::BeginChild("##config_list_scroll", ImVec2(-1, listHeight > 24.0f ? listHeight : 24.0f), false);
        for (int i = 0; i < (int)configsList.size(); i++)
        {
            ImGui::PushID(i);
            const bool isSelected = (selectedConfigIndex == i);
            const bool isAutoload = !autoloadSettings.configName.empty()
                && NormalizeConfigFilename(configsList[i]) == autoloadSettings.configName;

            std::string label = configsList[i];
            if (isAutoload)
                label += "  *";

            if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(-1, 0)))
            {
                selectedConfigIndex = i;
                strncpy_s(configNameBuffer, configsList[i].c_str(), _TRUNCATE);
                
                const std::string name = NormalizeConfigFilename(configsList[i]);
                if (LoadConfig(name))
                {
                    configStatusSuccess = true;
                    configStatusMessage = "Loaded " + name;
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = Config::lastError.empty() ? ("Failed to load " + name) : Config::lastError;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::SetCursorPosY(38);
    ImGui::SetCursorPosX(358);
    ImGui::MenuChild("Actions", ImVec2(224, 337), false);
    {
        if (!configStatusMessage.empty())
        {
            const ImVec4 statusColor = configStatusSuccess
                ? ImVec4(main_color.x, main_color.y, main_color.z, 1.0f)
                : ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
            ImGui::TextWrapped("%s", configStatusMessage.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 4));
        }

        ImGui::TextDisabled("Config name");
        ImGui::InputText("##seraph_configname", configNameBuffer, IM_ARRAYSIZE(configNameBuffer));
        ImGui::Dummy(ImVec2(0, 6));

        if (ImGui::Button("Load", ImVec2(-1, 24)))
        {
            const std::string name = NormalizeConfigFilename(configNameBuffer);
            if (name.empty())
            {
                configStatusSuccess = false;
                configStatusMessage = "Enter a config name first";
            }
            else
            {
                strncpy_s(configNameBuffer, name.c_str(), _TRUNCATE);
                if (LoadConfig(name))
                {
                    configStatusSuccess = true;
                    configStatusMessage = "Loaded " + name;
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = Config::lastError.empty() ? ("Failed to load " + name) : Config::lastError;
                }
            }
        }

        ImGui::Dummy(ImVec2(0, 3));

        if (ImGui::Button("Save", ImVec2(-1, 24)))
        {
            const std::string name = NormalizeConfigFilename(configNameBuffer);
            if (name.empty())
            {
                configStatusSuccess = false;
                configStatusMessage = "Enter a config name first";
            }
            else
            {
                strncpy_s(configNameBuffer, name.c_str(), _TRUNCATE);
                if (SaveConfig(name))
                {
                    configsList = ListConfigFiles();
                    selectedConfigIndex = -1;
                    for (int i = 0; i < (int)configsList.size(); i++)
                    {
                        if (configsList[i] == name)
                        {
                            selectedConfigIndex = i;
                            break;
                        }
                    }
                    configStatusSuccess = true;
                    configStatusMessage = "Saved " + name;
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = Config::lastError.empty() ? ("Failed to save " + name) : Config::lastError;
                }
            }
        }

        ImGui::Dummy(ImVec2(0, 3));

        if (ImGui::Button("Set Autoload", ImVec2(-1, 24)))
        {
            const std::string name = NormalizeConfigFilename(configNameBuffer);
            if (name.empty())
            {
                configStatusSuccess = false;
                configStatusMessage = "Select or enter a config first";
            }
            else if (!std::filesystem::exists(GetConfigFilePath(name)))
            {
                configStatusSuccess = false;
                configStatusMessage = "Config not found";
            }
            else
            {
                autoloadSettings.configName = name;
                autoloadSettings.enabled = true;
                autoloadEnabled = true;
                if (SaveAutoloadSettings(autoloadSettings))
                {
                    configStatusSuccess = true;
                    configStatusMessage = "Autoload set to " + name;
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = Config::lastError.empty() ? "Failed to save autoload" : Config::lastError;
                }
            }
        }

        ImGui::Dummy(ImVec2(0, 3));

        if (ImGui::Button("Delete", ImVec2(-1, 24)))
        {
            const std::string name = NormalizeConfigFilename(configNameBuffer);
            if (name.empty())
            {
                configStatusSuccess = false;
                configStatusMessage = "Enter a config name first";
            }
            else
            {
                const std::filesystem::path fullPath = GetConfigFilePath(name);
                std::error_code ec;
                if (std::filesystem::exists(fullPath, ec) && !ec)
                {
                    std::filesystem::remove(fullPath, ec);
                    if (ec)
                    {
                        configStatusSuccess = false;
                        configStatusMessage = "Delete failed: " + ec.message();
                    }
                    else
                    {
                        ClearAutoloadIfMatches(name);
                        autoloadSettings = LoadAutoloadSettings();
                        autoloadEnabled = autoloadSettings.enabled;
                        configNameBuffer[0] = '\0';
                        selectedConfigIndex = -1;
                        configsList = ListConfigFiles();
                        configStatusSuccess = true;
                        configStatusMessage = "Deleted " + name;
                    }
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = "Config not found";
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleColor(themeColorCount);
}

void RenderKeybindList(ImDrawList* drawList)
{
    if (!Options::Misc::KeybindList)
        return;

    ImGuiIO& io = ImGui::GetIO();
    std::vector<std::pair<std::string, std::string>> activeBinds;

    // Check Aimbot
    if (Options::Aimbot::Aimbot && Options::Aimbot::AimbotKey != 0)
    {
        bool isActive = false;
        if (Options::Aimbot::ToggleType == 1) // Toggle
            isActive = Options::Aimbot::Toggled;
        else // Hold
            isActive = (GetAsyncKeyState(Options::Aimbot::AimbotKey) & 0x8000) != 0;
        
        if (isActive)
            activeBinds.push_back({"Aimbot", Options::Aimbot::ToggleType == 1 ? "[Toggled]" : "[Hold]"});
    }

    // Check Triggerbot
    if (Options::Triggerbot::Enabled && Options::Triggerbot::TriggerbotKey != 0)
    {
        bool isActive = false;
        if (Options::Triggerbot::ToggleType == 1) // Toggle
            isActive = Options::Triggerbot::Toggled;
        else // Hold
            isActive = (GetAsyncKeyState(Options::Triggerbot::TriggerbotKey) & 0x8000) != 0;
        
        if (isActive)
            activeBinds.push_back({"Triggerbot", Options::Triggerbot::ToggleType == 1 ? "[Toggled]" : "[Hold]"});
    }

    // Check Fly
    if (Options::Fly::Enabled && Options::Fly::FlyKey != 0)
    {
        bool isActive = false;
        if (Options::Fly::ToggleType == 1) // Toggle
            isActive = Options::Fly::Toggled;
        else // Hold
            isActive = (GetAsyncKeyState(Options::Fly::FlyKey) & 0x8000) != 0;
        
        if (isActive)
            activeBinds.push_back({"Fly", Options::Fly::ToggleType == 1 ? "[Toggled]" : "[Hold]"});
    }

    // Check WalkSpeed
    if (Options::WalkSpeed::Enabled && Options::WalkSpeed::WalkSpeedKey != 0)
    {
        bool isActive = false;
        if (Options::WalkSpeed::ToggleType == 1) // Toggle
            isActive = Options::WalkSpeed::Toggled;
        else // Hold
            isActive = (GetAsyncKeyState(Options::WalkSpeed::WalkSpeedKey) & 0x8000) != 0;
        
        if (isActive)
            activeBinds.push_back({"WalkSpeed", Options::WalkSpeed::ToggleType == 1 ? "[Toggled]" : "[Hold]"});
    }

    if (activeBinds.empty())
        return;

    // Calculate dimensions - much smaller and compact
    float padding = 8.0f;
    float lineHeight = 14.0f;
    float titleHeight = 20.0f;
    float minWidth = 150.0f; // Reduced minimum width
    float maxWidth = minWidth;
    
    for (const auto& bind : activeBinds)
    {
        std::string fullText = bind.first + " " + bind.second;
        float textWidth = ImGui::CalcTextSize(fullText.c_str()).x;
        if (textWidth > maxWidth)
            maxWidth = textWidth;
    }
    
    float boxWidth = maxWidth + padding * 2;
    float boxHeight = titleHeight + (activeBinds.size() * lineHeight) + padding;
    
    // Use custom position from sliders
    ImVec2 pos = ImVec2(Options::Misc::KeybindListX, Options::Misc::KeybindListY);
    
    // Draw background - fully opaque (255 alpha instead of 200)
    drawList->AddRectFilled(pos, ImVec2(pos.x + boxWidth, pos.y + boxHeight), IM_COL32(8, 8, 8, 255), 4.0f);
    drawList->AddRect(pos, ImVec2(pos.x + boxWidth, pos.y + boxHeight), IM_COL32(27, 27, 27, 255), 4.0f);
    
    // Draw title - centered
    const char* title = "Keybinds";
    float titleWidth = ImGui::CalcTextSize(title).x;
    float titleX = pos.x + (boxWidth - titleWidth) / 2.0f;
    drawList->AddText(ImVec2(titleX, pos.y + 4), IM_COL32(255, 255, 255, 255), title);
    drawList->AddLine(ImVec2(pos.x, pos.y + titleHeight), ImVec2(pos.x + boxWidth, pos.y + titleHeight), IM_COL32(27, 27, 27, 255));
    
    // Draw active binds - centered
    float yOffset = pos.y + titleHeight + 3;
    for (const auto& bind : activeBinds)
    {
        std::string fullText = bind.first + " " + bind.second;
        float textWidth = ImGui::CalcTextSize(fullText.c_str()).x;
        float textX = pos.x + (boxWidth - textWidth) / 2.0f;
        
        // Draw the full text centered
        drawList->AddText(ImVec2(textX, yOffset), IM_COL32(255, 255, 255, 255), bind.first.c_str());
        
        // Draw status in accent color right after the name
        float nameWidth = ImGui::CalcTextSize(bind.first.c_str()).x;
        drawList->AddText(ImVec2(textX + nameWidth + 5, yOffset), IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255), bind.second.c_str());
        
        yOffset += lineHeight;
    }
}

void ShowImgui()
{
    InitializeConfigPaths();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    size_t width = (size_t)GetSystemMetrics(SM_CXSCREEN);
    size_t height = (size_t)GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"redstoneprojrjr&kam546",
        WS_POPUP,
        0, 0, (int)width + 1, (int)height + 1,
        nullptr, nullptr, wc.hInstance, nullptr);

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS Margin = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &Margin);

    HideFromTaskbar(hwnd);

    // Apply streamproof if enabled (WDA_EXCLUDEFROMCAPTURE = 0x00000011)
    if (Options::Misc::StreamProof)
    {
        SetWindowDisplayAffinity(hwnd, 0x00000011);
    }

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CoUninitialize();
        return;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    HideFromTaskbar(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    ImFontConfig config;
    config.MergeMode = false;
    config.PixelSnapH = true;

    ImFont* baseFont = io.Fonts->AddFontDefault(&config);
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdana.ttf", 13.0f, &config, io.Fonts->GetGlyphRangesJapanese());

    config.MergeMode = true;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    ImGui_ImplDX11_CreateDeviceObjects();

    ImVec4 clear_color = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    bool done = false;
    bool menu_open = false;
    int tab = 0;
    int tab2 = 0;
    int lastTab = -1;

    SetTransparency(hwnd, true);

    while (!done && Globals::running)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, 0) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Globals::Viewport::Update();

        if (Options::Misc::MenuKey != 0 && (GetAsyncKeyState(Options::Misc::MenuKey) & 1))
        {
            menu_open = !menu_open;
            SetTransparency(hwnd, !menu_open);
        }
        
        // Fade animation
        static float menuAlpha = 0.0f;
        static float backgroundAlpha = 0.0f;
        float fadeSpeed = 0.08f; // Adjust for faster/slower fade
        
        if (menu_open)
        {
            if (menuAlpha < 1.0f) menuAlpha += fadeSpeed;
            if (menuAlpha > 1.0f) menuAlpha = 1.0f;
            
            if (backgroundAlpha < 0.7f) backgroundAlpha += fadeSpeed;
            if (backgroundAlpha > 0.7f) backgroundAlpha = 0.7f;
        }
        else
        {
            if (menuAlpha > 0.0f) menuAlpha -= fadeSpeed;
            if (menuAlpha < 0.0f) menuAlpha = 0.0f;
            
            if (backgroundAlpha > 0.0f) backgroundAlpha -= fadeSpeed;
            if (backgroundAlpha < 0.0f) backgroundAlpha = 0.0f;
        }
        
        // Dynamic streamproof toggle
        static bool lastStreamProofState = Options::Misc::StreamProof;
        if (lastStreamProofState != Options::Misc::StreamProof)
        {
            if (Options::Misc::StreamProof)
            {
                SetWindowDisplayAffinity(hwnd, 0x00000011); // WDA_EXCLUDEFROMCAPTURE
            }
            else
            {
                SetWindowDisplayAffinity(hwnd, 0x00000000); // WDA_NONE
            }
            lastStreamProofState = Options::Misc::StreamProof;
        }
        
        // Update main_color from options
        main_color = ImVec4(Options::Misc::MenuAccentColor[0], Options::Misc::MenuAccentColor[1], Options::Misc::MenuAccentColor[2], 1.0f);

        if (menu_open || menuAlpha > 0.0f)
        {
            // Draw dark background overlay
            if (backgroundAlpha > 0.0f)
            {
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(0, 0),
                    ImVec2(io.DisplaySize.x, io.DisplaySize.y),
                    IM_COL32(0, 0, 0, static_cast<int>(backgroundAlpha * 180))
                );
            }
            auto s = ImVec2{}, p = ImVec2{}, gs = ImVec2{ 600, 420 };
            ImGui::SetNextWindowSize(gs);
            ImGui::SetNextWindowBgAlpha(menuAlpha);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, menuAlpha);
            ImGui::Begin("##GUI", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
            {
                s = ImVec2(ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x * 2, ImGui::GetWindowSize().y - ImGui::GetStyle().WindowPadding.y * 2);
                p = ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, ImGui::GetWindowPos().y + ImGui::GetStyle().WindowPadding.y);
                auto draw = ImGui::GetWindowDrawList();

                draw->AddRectFilled(ImVec2(p.x, p.y + 0), ImVec2(p.x + s.x, p.y + s.y - 0), ImColor(8, 8, 8), 4); // bg
                draw->AddRect(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + s.x - 1, p.y + s.y - 1), ImColor(27, 27, 27, 255), 4.5); // outline
                draw->AddRect(ImVec2(p.x - -10, p.y + 35.3), ImVec2(p.x + s.x - 480, p.y + s.y - 36.85), ImColor(26, 26, 26, 255)); // subtabs outline

                draw->AddLine(ImVec2(p.x, p.y + s.y - 27), ImVec2(p.x + s.x, p.y + s.y - 27), ImColor(27, 27, 27, 255)); // top separator
                draw->AddLine(ImVec2(p.x, p.y + 25), ImVec2(p.x + s.x, p.y + 25), ImColor(27, 27, 27, 255)); // tab separator

                int fade_line_count = 60;
                float fade_stop = s.x;
                float center_point = fade_stop / 2.0f;

                for (int i = 0; i < fade_line_count; i++)
                {
                    float alpha = 1.0f - (i * (1.0f / fade_line_count));
                    ImVec2 start_right = ImVec2(p.x + fade_stop - i * (center_point / fade_line_count), p.y + 25);
                    ImVec2 end_right = ImVec2(p.x + fade_stop - (i + 1) * (center_point / fade_line_count), p.y + 25);
                    ImColor fade_color(main_color.x, main_color.y, main_color.z, alpha);
                    draw->AddLine(start_right, end_right, fade_color);
                }

                ImGui::PushFont(font);
                draw->AddText(ImVec2(p.x + 9.5, p.y + 7), ImColor(main_color), "Seraph");
                draw->AddText(ImVec2(p.x + 9.5, p.y + 384), ImColor(255, 255, 255, 100), "Build:");
                draw->AddText(ImVec2(p.x + 41, p.y + 384), ImColor(main_color), ("Seraph.gg | Beta"));
                
                // Get Roblox username and display it with main_color at the right edge on same level as Build:Live
                std::string username = Globals::Roblox::LocalPlayer.Name();
                ImVec2 usernameSize = font->CalcTextSizeA(13.0f, FLT_MAX, 0.f, username.c_str());
                draw->AddText(ImVec2(p.x + s.x - usernameSize.x - 10.0f, p.y + 384), ImColor(main_color), username.c_str());
                 
                ImGui::SetCursorPosX(112);
                ImGui::SetCursorPosY(10);
                ImGui::BeginGroup();
                if (ImGui::tab("Aim", tab == 0)) tab = 0; ImGui::SameLine();
                if (ImGui::tab("Visuals", tab == 1)) tab = 1; ImGui::SameLine();
                if (ImGui::tab("Misc", tab == 2)) tab = 2; ImGui::SameLine();
                if (ImGui::tab("Configs", tab == 3)) tab = 3;
                ImGui::EndGroup();

                if (tab != lastTab)
                {
                    tab2 = 0;
                    lastTab = tab;
                }

                if (tab == 0)
                {
                    ImGui::SetCursorPosY(54);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Aimbot", tab2 == 0)) tab2 = 0;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Triggerbot", tab2 == 1)) tab2 = 1;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Hitbox", tab2 == 2)) tab2 = 2;

                    if (tab2 == 0) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("Main Group", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Enabled", &Options::Aimbot::Aimbot);
                            ImGui::Checkbox("Team Check", &Options::Aimbot::TeamCheck);
                            ImGui::Checkbox("Knocked Check", &Options::Aimbot::DownedCheck);
                            ImGui::Checkbox("Sticky Aim", &Options::Aimbot::StickyAim);
                            ImGui::Checkbox("Prediction", &Options::Aimbot::Prediction);
                            ImGui::Checkbox("Shake", &Options::Aimbot::Shake);
                            ImGui::Checkbox("Stutter", &Options::Aimbot::Stutter);
                            ImGui::PopStyleColor(1);
                            
                            ImGui::Dummy(ImVec2(0, 10));
                            
                            // Curve visualization graph
                            // Center the graph horizontally
                            float panelWidth = 226.0f;
                            ImVec2 graphSize = ImVec2(200, 100);
                            float offsetX = (panelWidth - graphSize.x) / 2.0f;
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::Text("Smoothness Curve:");
                            ImGui::Dummy(ImVec2(0, 5));
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            
                            ImVec2 graphPos = ImGui::GetCursorScreenPos();
                            ImDrawList* drawList = ImGui::GetWindowDrawList();
                            
                            // Background with menu style
                            drawList->AddRectFilled(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), IM_COL32(8, 8, 8, 255), 2.0f);
                            drawList->AddRect(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), IM_COL32(27, 27, 27, 255), 2.0f);
                            
                            // Grid lines
                            for (int i = 1; i < 4; i++)
                            {
                                float y = graphPos.y + (graphSize.y / 4.0f) * i;
                                drawList->AddLine(ImVec2(graphPos.x, y), ImVec2(graphPos.x + graphSize.x, y), IM_COL32(20, 20, 20, 255), 1.0f);
                            }
                            for (int i = 1; i < 4; i++)
                            {
                                float x = graphPos.x + (graphSize.x / 4.0f) * i;
                                drawList->AddLine(ImVec2(x, graphPos.y), ImVec2(x, graphPos.y + graphSize.y), IM_COL32(20, 20, 20, 255), 1.0f);
                            }
                            
                            // Draw curve based on selected type
                            ImVec2 prevPoint = ImVec2(graphPos.x, graphPos.y + graphSize.y);
                            for (int i = 1; i <= 100; i++)
                            {
                                float t = i / 100.0f;
                                float value;
                                
                                switch (Options::Aimbot::SmoothnessCurve)
                                {
                                    case 0: // Linear
                                        value = t;
                                        break;
                                    case 1: // Ease In
                                        value = t * t;
                                        break;
                                    case 2: // Ease Out
                                        value = sqrt(t);
                                        break;
                                    case 3: // Ease In-Out
                                        value = t * t * (3.0f - 2.0f * t);
                                        break;
                                    case 4: // Custom Bezier
                                    {
                                        float p0 = 0.0f;
                                        float p1 = Options::Aimbot::CustomCurveP1[1];
                                        float p2 = Options::Aimbot::CustomCurveP2[1];
                                        float p3 = 1.0f;
                                        
                                        float u = 1.0f - t;
                                        float tt = t * t;
                                        float ttt = tt * t;
                                        float uu = u * u;
                                        float uuu = uu * u;
                                        
                                        value = uuu * p0 + 3 * uu * t * p1 + 3 * u * tt * p2 + ttt * p3;
                                        break;
                                    }
                                    default:
                                        value = t;
                                        break;
                                }
                                
                                ImVec2 point = ImVec2(
                                    graphPos.x + t * graphSize.x,
                                    graphPos.y + graphSize.y - value * graphSize.y
                                );
                                
                                drawList->AddLine(prevPoint, point, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255), 2.0f);
                                prevPoint = point;
                            }
                            
                            // Interactive control points for custom curve
                            if (Options::Aimbot::SmoothnessCurve == 4)
                            {
                                Options::Aimbot::CustomCurveEnabled = true;
                                
                                // Control point 1
                                ImVec2 cp1Pos = ImVec2(
                                    graphPos.x + Options::Aimbot::CustomCurveP1[0] * graphSize.x,
                                    graphPos.y + graphSize.y - Options::Aimbot::CustomCurveP1[1] * graphSize.y
                                );
                                
                                // Control point 2
                                ImVec2 cp2Pos = ImVec2(
                                    graphPos.x + Options::Aimbot::CustomCurveP2[0] * graphSize.x,
                                    graphPos.y + graphSize.y - Options::Aimbot::CustomCurveP2[1] * graphSize.y
                                );
                                
                                // Draw control point lines
                                drawList->AddLine(ImVec2(graphPos.x, graphPos.y + graphSize.y), cp1Pos, IM_COL32(100, 100, 100, 150), 1.0f);
                                drawList->AddLine(cp2Pos, ImVec2(graphPos.x + graphSize.x, graphPos.y), IM_COL32(100, 100, 100, 150), 1.0f);
                                
                                // Draw control points
                                float cpRadius = 5.0f;
                                drawList->AddCircleFilled(cp1Pos, cpRadius, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255));
                                drawList->AddCircle(cp1Pos, cpRadius, IM_COL32(255, 255, 255, 255), 0, 1.5f);
                                
                                drawList->AddCircleFilled(cp2Pos, cpRadius, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255));
                                drawList->AddCircle(cp2Pos, cpRadius, IM_COL32(255, 255, 255, 255), 0, 1.5f);
                                
                                // Handle dragging
                                ImVec2 mousePos = ImGui::GetMousePos();
                                bool mouseDown = ImGui::IsMouseDown(0);
                                static int draggedPoint = -1; // -1 = none, 0 = cp1, 1 = cp2
                                
                                if (mouseDown)
                                {
                                    if (draggedPoint == -1)
                                    {
                                        // Check if mouse is over cp1
                                        float dist1 = sqrt(pow(mousePos.x - cp1Pos.x, 2) + pow(mousePos.y - cp1Pos.y, 2));
                                        if (dist1 <= cpRadius + 3.0f)
                                        {
                                            draggedPoint = 0;
                                        }
                                        
                                        // Check if mouse is over cp2
                                        float dist2 = sqrt(pow(mousePos.x - cp2Pos.x, 2) + pow(mousePos.y - cp2Pos.y, 2));
                                        if (dist2 <= cpRadius + 3.0f)
                                        {
                                            draggedPoint = 1;
                                        }
                                    }
                                    
                                    // Update dragged point position
                                    if (draggedPoint == 0)
                                    {
                                        Options::Aimbot::CustomCurveP1[0] = std::clamp((mousePos.x - graphPos.x) / graphSize.x, 0.0f, 1.0f);
                                        Options::Aimbot::CustomCurveP1[1] = std::clamp((graphPos.y + graphSize.y - mousePos.y) / graphSize.y, 0.0f, 1.0f);
                                    }
                                    else if (draggedPoint == 1)
                                    {
                                        Options::Aimbot::CustomCurveP2[0] = std::clamp((mousePos.x - graphPos.x) / graphSize.x, 0.0f, 1.0f);
                                        Options::Aimbot::CustomCurveP2[1] = std::clamp((graphPos.y + graphSize.y - mousePos.y) / graphSize.y, 0.0f, 1.0f);
                                    }
                                }
                                else
                                {
                                    draggedPoint = -1;
                                }
                            }
                            else
                            {
                                Options::Aimbot::CustomCurveEnabled = false;
                            }
                            
                            // Axis labels
                            drawList->AddText(ImVec2(graphPos.x + 2, graphPos.y + graphSize.y + 2), IM_COL32(150, 150, 150, 255), "0.0");
                            drawList->AddText(ImVec2(graphPos.x + graphSize.x - 20, graphPos.y + graphSize.y + 2), IM_COL32(150, 150, 150, 255), "1.0");
                            
                            ImGui::Dummy(ImVec2(graphSize.x, graphSize.y + 15));
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Modifiers", ImVec2(224, 337), false);
                        {
                            static const char* aimingMethods[]{ "Camera", "Mouse", "Silent" };
                            ImGui::Combo("Method", &Options::Aimbot::AimingType, aimingMethods, IM_ARRAYSIZE(aimingMethods));

                            static const char* fovPositions[]{ "Screen Center", "Follow Target" };
                            ImGui::Combo("FOV Position", &Options::Aimbot::FOVPositionMode, fovPositions, IM_ARRAYSIZE(fovPositions));

                            ImGui::Checkbox("Silent Aim", &Options::Aimbot::SilentAim);
                            if (Options::Aimbot::SilentAim || Options::Aimbot::AimingType == 2)
                            {
                                static const char* silentModes[]{ "Camera Only", "Camera + Mouse Spoof" };
                                ImGui::Combo("Silent Mode", &Options::Aimbot::SilentAimMode, silentModes, IM_ARRAYSIZE(silentModes));
                            }
                            
                            static const char* hitParts[]{ "Head", "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Lower Torso", "Upper Torso" };
                            ImGui::Combo("Hit Part", &Options::Aimbot::TargetBone, hitParts, IM_ARRAYSIZE(hitParts));
                            ImGui::Combo("Air Hit Part", &Options::Aimbot::AirTargetBone, hitParts, IM_ARRAYSIZE(hitParts));
                            
                            static const char* smoothnessCurves[]{ "Linear", "Ease In", "Ease Out", "Ease In-Out", "Custom" };
                            ImGui::Combo("Curve", &Options::Aimbot::SmoothnessCurve, smoothnessCurves, IM_ARRAYSIZE(smoothnessCurves));
                            
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            ImGui::SliderFloat("Smoothness", &Options::Aimbot::Smoothness, 0.f, 1.f, "%.3f");
                            
                            if (Options::Aimbot::Shake)
                            {
                                ImGui::SliderFloat("Shake Intensity", &Options::Aimbot::ShakeIntensity, 0.1f, 10.0f, "%.1f");
                            }
                            
                            if (Options::Aimbot::Stutter)
                            {
                                ImGui::SliderInt("Stutter Ticks", &Options::Aimbot::StutterTicks, 1, 20);
                            }
                            
                            ImGui::SliderFloat("Range", &Options::Aimbot::Range, 1.f, 1000.f, "%.0f");
                            ImGui::SliderFloat("FOV", &Options::Aimbot::FOV, 10.f, 360.f, "%.0f");
                            ImGui::SliderFloat("FOV Thickness", &Options::Aimbot::FOVThickness, 1.0f, 10.0f, "%.1f");
                            
                            if (Options::Aimbot::Prediction)
                            {
                                ImGui::SliderFloat("Prediction X", &Options::Aimbot::PredictionX, 0.01f, 10.0f, "%.2f");
                                ImGui::SliderFloat("Prediction Y", &Options::Aimbot::PredictionY, 0.01f, 10.0f, "%.2f");
                            }
                            
                            ImGui::PopStyleColor(1);

                            ImGui::Dummy(ImVec2(0, 8));
                            
                            // Center keybind text
                            float panelWidth = 224.0f;
                            const char* keybindText = "Aimbot Key: [ None ]"; // Approximate max width
                            float textWidth = ImGui::CalcTextSize(keybindText).x;
                            float offsetX = (panelWidth - textWidth) / 2.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            
                            KeybindSelector(" Aimbot Key", &Options::Aimbot::AimbotKey);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 1) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("Main Group", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Enabled", &Options::Triggerbot::Enabled);
                            ImGui::Checkbox("Team Check", &Options::Triggerbot::TeamCheck);
                            ImGui::Checkbox("Knocked Check", &Options::Triggerbot::DownedCheck);
                            ImGui::Checkbox("Prediction", &Options::Triggerbot::Prediction);
                            ImGui::Checkbox("Advanced FOV", &Options::Triggerbot::AdvancedFOV);
                            
                            if (Options::Triggerbot::AdvancedFOV)
                            {
                                ImGui::Checkbox("Show FOV", &Options::Triggerbot::ShowAdvancedFOV);
                            }
                            
                            ImGui::PopStyleColor(1);
                            
                            ImGui::Dummy(ImVec2(0, 8));
                            
                            // Triggerbot keybind
                            KeybindSelector(" Triggerbot Key", &Options::Triggerbot::TriggerbotKey);
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Settings", ImVec2(224, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            
                            if (!Options::Triggerbot::AdvancedFOV)
                            {
                                ImGui::SliderFloat("Radius", &Options::Triggerbot::Radius, 0.1f, 50.f, "%.1f");
                            }
                            
                            ImGui::SliderFloat("Range", &Options::Triggerbot::Range, 0.1f, 1000.f, "%.1f");
                            ImGui::SliderInt("Delay (ms)", &Options::Triggerbot::Delay, 0, 500);

                            if (Options::Triggerbot::Prediction)
                            {
                                ImGui::SliderFloat("Prediction X", &Options::Triggerbot::PredictionX, 0.0f, 10.0f, "%.2f");
                                ImGui::SliderFloat("Prediction Y", &Options::Triggerbot::PredictionY, 0.0f, 10.0f, "%.2f");
                            }
                            
                            // Advanced FOV sliders
                            if (Options::Triggerbot::AdvancedFOV)
                            {
                                ImGui::Text(" HEAD");
                                ImGui::SliderFloat("Head FOV X", &Options::Triggerbot::HeadFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("Head FOV Y", &Options::Triggerbot::HeadFOV_Y, 0.f, 100.f, "%.1f");
                                
                                ImGui::Text(" TORSO");
                                ImGui::SliderFloat("Torso FOV X", &Options::Triggerbot::TorsoFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("Torso FOV Y", &Options::Triggerbot::TorsoFOV_Y, 0.f, 100.f, "%.1f");
                                
                                ImGui::Text(" UPPER TORSO");
                                ImGui::SliderFloat("U Torso FOV X", &Options::Triggerbot::UpperTorsoFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("U Torso FOV Y", &Options::Triggerbot::UpperTorsoFOV_Y, 0.f, 100.f, "%.1f");
                                
                                ImGui::Text(" LOWER TORSO");
                                ImGui::SliderFloat("L Torso FOV X", &Options::Triggerbot::LowerTorsoFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L Torso FOV Y", &Options::Triggerbot::LowerTorsoFOV_Y, 0.f, 100.f, "%.1f");
                                
                                ImGui::Text(" LEFT ARM");
                                ImGui::SliderFloat("L U Arm FOV X", &Options::Triggerbot::LeftUpperArmFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L U Arm FOV Y", &Options::Triggerbot::LeftUpperArmFOV_Y, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L L Arm FOV X", &Options::Triggerbot::LeftLowerArmFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L L Arm FOV Y", &Options::Triggerbot::LeftLowerArmFOV_Y, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L Hand FOV X", &Options::Triggerbot::LeftHandFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L Hand FOV Y", &Options::Triggerbot::LeftHandFOV_Y, 0.f, 100.f, "%.1f");
                                
                                ImGui::Text(" RIGHT ARM");
                                ImGui::SliderFloat("R U Arm FOV X", &Options::Triggerbot::RightUpperArmFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R U Arm FOV Y", &Options::Triggerbot::RightUpperArmFOV_Y, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R L Arm FOV X", &Options::Triggerbot::RightLowerArmFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R L Arm FOV Y", &Options::Triggerbot::RightLowerArmFOV_Y, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R Hand FOV X", &Options::Triggerbot::RightHandFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R Hand FOV Y", &Options::Triggerbot::RightHandFOV_Y, 0.f, 100.f, "%.1f");
                                
                                ImGui::Text(" LEFT LEG");
                                ImGui::SliderFloat("L U Leg FOV X", &Options::Triggerbot::LeftUpperLegFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L U Leg FOV Y", &Options::Triggerbot::LeftUpperLegFOV_Y, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L L Leg FOV X", &Options::Triggerbot::LeftLowerLegFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L L Leg FOV Y", &Options::Triggerbot::LeftLowerLegFOV_Y, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L Foot FOV X", &Options::Triggerbot::LeftFootFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("L Foot FOV Y", &Options::Triggerbot::LeftFootFOV_Y, 0.f, 100.f, "%.1f");
                                
                                ImGui::Text(" RIGHT LEG");
                                ImGui::SliderFloat("R U Leg FOV X", &Options::Triggerbot::RightUpperLegFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R U Leg FOV Y", &Options::Triggerbot::RightUpperLegFOV_Y, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R L Leg FOV X", &Options::Triggerbot::RightLowerLegFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R L Leg FOV Y", &Options::Triggerbot::RightLowerLegFOV_Y, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R Foot FOV X", &Options::Triggerbot::RightFootFOV_X, 0.f, 100.f, "%.1f");
                                ImGui::SliderFloat("R Foot FOV Y", &Options::Triggerbot::RightFootFOV_Y, 0.f, 100.f, "%.1f");
                            }
                            
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 2) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("Main Group", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Enabled", &Options::HitboxExpander::Enabled);
                            ImGui::Checkbox("Show Hitbox", &Options::HitboxExpander::ShowHitbox);
                            ImGui::Checkbox("Walk Through", &Options::HitboxExpander::WalkThrough);
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Settings", ImVec2(224, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            ImGui::SliderFloat("Horizontal Size", &Options::HitboxExpander::HorizontalSize, 1.0f, 50.0f, "%.1f");
                            ImGui::SliderFloat("Vertical Size", &Options::HitboxExpander::VerticalSize, 1.0f, 50.0f, "%.1f");
                            ImGui::SliderFloat("Transparency", &Options::HitboxExpander::HitboxTransparency, 0.0f, 1.0f, "%.2f");
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();
                    }
                }
                else if (tab == 1)
                {
                    ImGui::SetCursorPosY(54);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("ESP", tab2 == 0)) tab2 = 0;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Combat", tab2 == 1)) tab2 = 1;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("World", tab2 == 2)) tab2 = 2;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Colours", tab2 == 3)) tab2 = 3;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Preview", tab2 == 4)) tab2 = 4;

                    if (tab2 == 0) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("ESP Features", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);

                            ImGui::TextDisabled("Filter");
                            ImGui::Checkbox("Master Enable", &Options::ESP::Enabled);
                            ImGui::Checkbox("Team Check", &Options::ESP::TeamCheck);
                            ImGui::Checkbox("Visibility Colors", &Options::ESP::VisibilityCheck);
                            ImGui::Checkbox("Visibility Bones", &Options::ESP::VisibilityChams);
                            ImGui::Spacing();

                            ImGui::TextDisabled("Player Info");
                            CheckboxWithColorPicker("Names", &Options::ESP::Name, Options::ESP::Color);
                            CheckboxWithColorPicker("Distance", &Options::ESP::Distance, Options::ESP::DistanceColor);
                            ImGui::Checkbox("Health Bar", &Options::ESP::Health);
                            ImGui::Checkbox("Health Text", &Options::ESP::HealthText);
                            ImGui::Checkbox("HP Above Head", &Options::ESP::EnemyHealthIndicator);
                            ImGui::Spacing();

                            ImGui::TextDisabled("Overlays");
                            ImGui::Checkbox("Corner ESP", &Options::ESP::CornerESP);
                            CheckboxWithColorPicker("Tracers", &Options::ESP::Tracers, Options::ESP::TracerColor);
                            CheckboxWithColorPicker("Skeleton", &Options::ESP::Skeleton, Options::ESP::SkeletonColor);
                            CheckboxWithColorPicker("Head Circle", &Options::ESP::HeadCircle, Options::ESP::HeadCircleColor);
                            CheckboxWithColorPicker("Head Dot", &Options::ESP::HeadDot, Options::ESP::HeadDotColor);

                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("ESP Settings", ImVec2(224, 337), false);
                        {
                            static const char* boxTypes[]{ "None", "Normal Box", "3D Box" };
                            ImGui::TextDisabled("Box");
                            ImGui::Combo("Type", &Options::ESP::BoxType, boxTypes, IM_ARRAYSIZE(boxTypes));

                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            ImGui::SliderFloat("Box Thickness", &Options::ESP::BoxThickness, 1.0f, 10.0f);
                            ImGui::SliderFloat("3D Box Thickness", &Options::ESP::ESP3DThickness, 1.0f, 10.0f);
                            ImGui::Spacing();

                            ImGui::TextDisabled("Lines");
                            ImGui::SliderFloat("Tracer Thickness", &Options::ESP::TracerThickness, 1.0f, 10.0f);
                            ImGui::SliderFloat("Skeleton Thickness", &Options::ESP::SkeletonThickness, 1.0f, 10.0f);
                            ImGui::Spacing();

                            ImGui::TextDisabled("Text");
                            ImGui::SliderFloat("Name Size", &Options::ESP::NameSize, 8.0f, 24.0f, "%.1f");
                            ImGui::SliderFloat("Name Thickness", &Options::ESP::NameThickness, 0.0f, 5.0f, "%.1f");
                            ImGui::Spacing();

                            ImGui::TextDisabled("Head");
                            ImGui::SliderFloat("Circle Thickness", &Options::ESP::HeadCircleThickness, 1.0f, 10.0f);
                            ImGui::SliderFloat("Circle Size", &Options::ESP::HeadCircleScale, 0.05f, 0.20f, "%.2f");

                            if (Options::ESP::VisibilityCheck || Options::ESP::VisibilityChams)
                            {
                                ImGui::Spacing();
                                ImGui::TextDisabled("Visibility");
                                ImGui::SliderFloat("Scan Range", &Options::ESP::VisibilityMaxDistance, 100.0f, 800.0f, "%.0f");
                            }

                            ImGui::Spacing();
                            ImGui::TextDisabled("Keybind");
                            static const char* toggleTypes[]{ "Hold", "Toggle" };
                            ImGui::Combo("Mode##esp", &Options::ESP::ToggleType, toggleTypes, IM_ARRAYSIZE(toggleTypes));
                            
                            // Center keybind text
                            float panelWidth = 224.0f;
                            const char* keybindText = "ESP Key: [ None ]"; // Approximate max width
                            float textWidth = ImGui::CalcTextSize(keybindText).x;
                            float offsetX = (panelWidth - textWidth) / 2.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            
                            KeybindSelector(" ESP Key", &Options::ESP::ESPKey);

                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 1) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("Hit Feedback", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Hit Sounds", &Options::Combat::HitSounds);
                            ImGui::Checkbox("Hit Notifications", &Options::Combat::HitNotifications);
                            ImGui::Checkbox("Hit Chams", &Options::Combat::HitChams);
                            ImGui::Checkbox("Hit Effects", &Options::Combat::HitEffects);
                            ImGui::PopStyleColor(1);

                            static const char* hitSounds[]{ "Click", "Bell", "Bass" };
                            ImGui::Combo("Sound", &Options::Combat::HitSoundType, hitSounds, IM_ARRAYSIZE(hitSounds));
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Hit Settings", ImVec2(224, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            ImGui::SliderFloat("Min Damage", &Options::Combat::MinDamage, 1.0f, 50.0f, "%.0f");
                            ImGui::SliderFloat("Chams Duration", &Options::Combat::HitChamsDuration, 0.1f, 2.0f, "%.2fs");
                            ImGui::SliderFloat("Effect Duration", &Options::Combat::HitEffectDuration, 0.1f, 2.0f, "%.2fs");
                            ImGui::ColorEdit3("Hit Chams Color", Options::Combat::HitChamsColor, ImGuiColorEditFlags_NoInputs);
                            ImGui::ColorEdit3("Hit Effect Color", Options::Combat::HitEffectColor, ImGuiColorEditFlags_NoInputs);
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 2) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("World", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Enabled", &Options::World::Enabled);
                            ImGui::Checkbox("Fullbright", &Options::World::Fullbright);
                            ImGui::Checkbox("No Fog", &Options::World::NoFog);
                            ImGui::Checkbox("Skybox Changer", &Options::World::SkyboxChanger);
                            ImGui::PopStyleColor(1);

                            static const char* skyPresets[]{ "Default", "Night", "Space", "Sunset", "Storm" };
                            ImGui::Combo("Sky Preset", &Options::World::SkyboxPreset, skyPresets, IM_ARRAYSIZE(skyPresets));
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Lighting", ImVec2(224, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            ImGui::SliderFloat("Clock Time", &Options::World::ClockTime, 0.0f, 24.0f, "%.1f");
                            ImGui::SliderFloat("Brightness", &Options::World::Brightness, 0.0f, 5.0f, "%.1f");
                            ImGui::SliderFloat("Fog Start", &Options::World::FogStart, 0.0f, 1000.0f, "%.0f");
                            ImGui::SliderFloat("Fog End", &Options::World::FogEnd, 50.0f, 100000.0f, "%.0f");
                            ImGui::ColorEdit3("Ambient", Options::World::Ambient, ImGuiColorEditFlags_NoInputs);
                            ImGui::ColorEdit3("Outdoor Ambient", Options::World::OutdoorAmbient, ImGuiColorEditFlags_NoInputs);
                            ImGui::ColorEdit3("Fog Color", Options::World::FogColor, ImGuiColorEditFlags_NoInputs);
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 3) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("ESP Colors", ImVec2(226, 337), false);
                        {
                            float panelWidth = 226.0f;
                            float colorPickerWidth = 180.0f;
                            float offsetX = (panelWidth - colorPickerWidth) / 2.0f;

                            ImGui::TextDisabled("ESP");
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Box Color", Options::ESP::BoxColor, ImGuiColorEditFlags_NoInputs);
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("3D Box Color", Options::ESP::ESP3DColor, ImGuiColorEditFlags_NoInputs);
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Name Color", Options::ESP::Color, ImGuiColorEditFlags_NoInputs);
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Distance Color", Options::ESP::DistanceColor, ImGuiColorEditFlags_NoInputs);
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Tracer Color", Options::ESP::TracerColor, ImGuiColorEditFlags_NoInputs);
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Skeleton Color", Options::ESP::SkeletonColor, ImGuiColorEditFlags_NoInputs);

                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Head Circle Color", Options::ESP::HeadCircleColor, ImGuiColorEditFlags_NoInputs);

                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Head Dot Color", Options::ESP::HeadDotColor, ImGuiColorEditFlags_NoInputs);

                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Corner Color", Options::ESP::CornerColor, ImGuiColorEditFlags_NoInputs);

                            ImGui::Spacing();
                            ImGui::TextDisabled("Visibility");
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Visible", Options::ESP::VisibleColor, ImGuiColorEditFlags_NoInputs);

                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("Hidden", Options::ESP::HiddenColor, ImGuiColorEditFlags_NoInputs);
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("FOV Colors", ImVec2(224, 337), false);
                        {
                            // Center color pickers
                            float panelWidth = 224.0f;
                            float colorPickerWidth = 180.0f;
                            float offsetX = (panelWidth - colorPickerWidth) / 2.0f;
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit3("FOV Color", Options::Aimbot::FOVColor, ImGuiColorEditFlags_NoInputs);
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            if (ImGui::ColorEdit3("Menu Accent", Options::Misc::MenuAccentColor, ImGuiColorEditFlags_NoInputs))
                            {
                                // Update main_color when the color picker changes
                                main_color = ImVec4(Options::Misc::MenuAccentColor[0], Options::Misc::MenuAccentColor[1], Options::Misc::MenuAccentColor[2], 1.0f);
                            }
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::ColorEdit4("FOV Fill Color", Options::Aimbot::FOVFillColor, ImGuiColorEditFlags_NoInputs);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 4) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("ESP Preview", ImVec2(452, 337), false);
                        {
                            ImGui::TextDisabled("Live preview of enabled ESP options");
                            ImGui::Dummy(ImVec2(0, 4));

                            const ImVec2 previewSize(420.0f, 285.0f);
                            ImGui::BeginChild("##esp_preview_panel", previewSize, true, ImGuiWindowFlags_NoScrollbar);
                            if (Options::ESP::ESPPreview)
                            {
                                const ImVec2 previewPos = ImGui::GetCursorScreenPos();
                                const ImVec2 contentSize = ImGui::GetContentRegionAvail();
                                RenderESPPreview(ImGui::GetWindowDrawList(), previewPos, contentSize);
                            }
                            ImGui::EndChild();
                        }
                        ImGui::EndChild();
                    }
                }
                else if (tab == 2)
                {
                    ImGui::SetCursorPosY(54);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Local", tab2 == 0)) tab2 = 0;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Fly", tab2 == 1)) tab2 = 1;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("WalkSpeed", tab2 == 2)) tab2 = 2;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 28);
                    ImGui::SetCursorPosX(30);
                    if (ImGui::subtab("Anti-Aim", tab2 == 3)) tab2 = 3;

                    if (tab2 == 0) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("Main Group", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Headless", &Options::ESP::Headless);
                            ImGui::Checkbox("Show FOV", &Options::Aimbot::ShowFOV);
                            ImGui::Checkbox("Show FOV Fill", &Options::Aimbot::ShowFOVFill);
                            ImGui::Checkbox("Crosshair", &Options::Crosshair::Enabled);
                            ImGui::Checkbox("Camera FOV", &Options::Misc::FOVEnabled);
                            ImGui::Checkbox("Cache NPCs", &Options::Misc::CacheNPCs);
                            ImGui::Checkbox("Keybind List", &Options::Misc::KeybindList);
                            ImGui::Checkbox("Stream Proof", &Options::Misc::StreamProof);
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Settings", ImVec2(224, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            
                            if (Options::Misc::FOVEnabled)
                            {
                                ImGui::SliderFloat("Camera FOV", &Options::Misc::FOV, 70.f, 120.f, "%.0f");
                            }
                            
                            ImGui::Dummy(ImVec2(0, 10));
                            
                            // Center the text
                            float panelWidth = 224.0f;
                            const char* posText = "Keybind List Position:";
                            float textWidth = ImGui::CalcTextSize(posText).x;
                            float offsetX = (panelWidth - textWidth) / 2.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            ImGui::Text(posText);
                            
                            ImGui::SliderFloat("Position X", &Options::Misc::KeybindListX, 0.0f, 1920.0f, "%.0f");
                            ImGui::SliderFloat("Position Y", &Options::Misc::KeybindListY, 0.0f, 1080.0f, "%.0f");

                            ImGui::Dummy(ImVec2(0, 8));

                            const char* menuKeyText = "Menu Key: [ None ]";
                            float menuKeyTextWidth = ImGui::CalcTextSize(menuKeyText).x;
                            float menuKeyOffsetX = (panelWidth - menuKeyTextWidth) / 2.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + menuKeyOffsetX);
                            KeybindSelector(" Menu Key", &Options::Misc::MenuKey);

                            ImGui::Dummy(ImVec2(0, 15));
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.6f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.8f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                            if (ImGui::Button("Unload", ImVec2(-1, 28)))
                            {
                                exit(0);
                            }
                            ImGui::PopStyleColor(3);

                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 1) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("Main Group", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Enabled", &Options::Fly::Enabled);
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Settings", ImVec2(224, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            ImGui::SliderFloat("Fly Speed", &Options::Fly::Speed, 10.f, 200.f, "%.0f");
                            ImGui::PopStyleColor(1);

                            ImGui::Dummy(ImVec2(0, 8));
                            
                            // Center keybind text
                            float panelWidth = 224.0f;
                            const char* keybindText = "Fly Key: [ None ]";
                            float textWidth = ImGui::CalcTextSize(keybindText).x;
                            float offsetX = (panelWidth - textWidth) / 2.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            
                            KeybindSelector(" Fly Key", &Options::Fly::FlyKey);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 2) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("Main Group", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Enabled", &Options::WalkSpeed::Enabled);
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Settings", ImVec2(224, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            ImGui::SliderFloat("Walk Speed", &Options::WalkSpeed::Speed, 16.f, 1000.f, "%.0f");
                            ImGui::PopStyleColor(1);

                            ImGui::Dummy(ImVec2(0, 8));
                            
                            // Center keybind text
                            float panelWidth = 224.0f;
                            const char* keybindText = "WalkSpeed Key: [ None ]";
                            float textWidth = ImGui::CalcTextSize(keybindText).x;
                            float offsetX = (panelWidth - textWidth) / 2.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                            
                            KeybindSelector(" WalkSpeed Key", &Options::WalkSpeed::WalkSpeedKey);
                        }
                        ImGui::EndChild();
                    }
                    else if (tab2 == 3) {
                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(122);
                        ImGui::MenuChild("Anti-Aim", ImVec2(226, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                            ImGui::Checkbox("Enabled", &Options::AntiAim::Enabled);
                            ImGui::PopStyleColor(1);

                            static const char* aaModes[]{ "Spin", "Jitter", "Random" };
                            ImGui::Combo("Mode", &Options::AntiAim::Mode, aaModes, IM_ARRAYSIZE(aaModes));
                        }
                        ImGui::EndChild();

                        ImGui::SetCursorPosY(38);
                        ImGui::SetCursorPosX(358);
                        ImGui::MenuChild("Settings", ImVec2(224, 337), false);
                        {
                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                            ImGui::SliderFloat("Speed", &Options::AntiAim::Speed, 1.0f, 50.0f, "%.1f");
                            ImGui::SliderFloat("Strength", &Options::AntiAim::Strength, 5.0f, 180.0f, "%.0f");
                            ImGui::PopStyleColor(1);
                        }
                        ImGui::EndChild();
                    }
                }
                else if (tab == 3)
                {
                    RenderConfigTab();
                }

                ImGui::PopFont();
            }
            ImGui::PopStyleVar();
            ImGui::End();
        }

        if (IsGameOnTop("Roblox"))
        {
            CombatFeedback::Update();

            RenderESP(ImGui::GetBackgroundDrawList());

            if (!menu_open)
            {
                RunAimbot(ImGui::GetBackgroundDrawList());
                RunTriggerbot();
                RunMacro();
            }
            else if (Options::Aimbot::ShowFOV)
            {
                RunAimbot(ImGui::GetBackgroundDrawList());
            }
            
            // Render advanced FOV visualization even when menu is open
            RenderAdvancedFOV(ImGui::GetBackgroundDrawList());
            
            // Render crosshair even when menu is open
            RenderCrosshair(ImGui::GetBackgroundDrawList());
            
            CombatFeedback::Render(ImGui::GetBackgroundDrawList());

            // Render keybind list
            RenderKeybindList(ImGui::GetBackgroundDrawList());

            std::string str = "Seraph | " + std::to_string(static_cast<int>(io.Framerate)) + " FPS";
            ImVec2 textSize = ImGui::CalcTextSize(str.c_str());
            ImVec2 pos = ImVec2(io.DisplaySize.x - textSize.x - 10.0f, 10.0f);
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            drawList->AddText(pos, IM_COL32(255, 255, 255, 255), str.c_str());
        }

        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 4;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
