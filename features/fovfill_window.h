#pragma once

#include <windows.h>
#include <cmath>
#include <cstdint>

#pragma comment(lib, "gdi32.lib")

namespace FovFillWindow
{
    inline bool     Active      = false;
    inline float    CenterX     = 0.0f;
    inline float    CenterY     = 0.0f;
    inline float    Radius      = 0.0f;
    inline float    Rotation    = 0.0f;
    inline int      Shape       = 0;
    inline unsigned BaseColor   = 0;
    inline unsigned TopColor    = 0;
    inline unsigned BottomColor = 0;
    inline bool     Gradient    = false;
    inline bool     ExcludeFromCapture = false;
    inline HWND     OverlayHwnd = nullptr;

    inline HWND      s_wnd      = nullptr;
    inline HDC       s_dc       = nullptr;
    inline HBITMAP   s_bmp      = nullptr;
    inline uint32_t* s_bits     = nullptr;
    inline int       s_size     = 0;
    inline bool      s_visible  = false;
    inline int       s_affinity = -1;

    inline int       s_rRadius  = -1;
    inline int       s_rShape   = -1;
    inline int       s_rRot     = 0;
    inline unsigned  s_rBase    = 0;
    inline unsigned  s_rTop     = 0;
    inline unsigned  s_rBot     = 0;
    inline bool      s_rGrad    = false;

    inline LRESULT CALLBACK FillWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        return ::DefWindowProcW(h, m, w, l);
    }

    inline void EnsureWindow()
    {
        if (s_wnd) return;
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc   = FillWndProc;
        wc.hInstance     = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SeraphOverlayFillSurface";
        ::RegisterClassExW(&wc);
        s_wnd = ::CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"", WS_POPUP,
            0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    }

    inline void EnsureSurface(int size)
    {
        if (s_bmp && s_size == size) return;
        if (s_bmp) { ::DeleteObject(s_bmp); s_bmp = nullptr; }
        if (s_dc)  { ::DeleteDC(s_dc); s_dc = nullptr; }
        s_dc = ::CreateCompatibleDC(nullptr);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = size;
        bi.bmiHeader.biHeight      = -size;
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        s_bmp = ::CreateDIBSection(s_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (s_bmp) ::SelectObject(s_dc, s_bmp);
        s_bits = static_cast<uint32_t*>(bits);
        s_size = size;
    }

    inline float EdgeRadius(float angle, float r, int n, float rot)
    {
        const float seg = 6.28318530718f / static_cast<float>(n);
        float a = angle - rot;
        a = a - seg * floorf(a / seg);
        a -= seg * 0.5f;
        const float apothem = r * cosf(seg * 0.5f);
        const float c = cosf(a);
        if (c < 1e-3f) return apothem * 1000.0f;
        return apothem / c;
    }

    inline void Rebuild()
    {
        const int rq = static_cast<int>(Radius + 0.5f);
        if (rq < 1) return;
        const int n = (Shape == 1) ? 4 : (Shape == 2) ? 3 : (Shape == 3) ? 6 : 96;
        const int size = rq * 2 + 6;
        EnsureSurface(size);
        if (!s_bits) return;

        const float c = static_cast<float>(size) * 0.5f;
        const float R = static_cast<float>(rq);

        const int br = BaseColor & 0xFF, bg = (BaseColor >> 8) & 0xFF, bb = (BaseColor >> 16) & 0xFF;
        const int ba = (BaseColor >> 24) & 0xFF;
        const int tr = TopColor & 0xFF, tg = (TopColor >> 8) & 0xFF, tb = (TopColor >> 16) & 0xFF;
        const int dr = BottomColor & 0xFF, dg = (BottomColor >> 8) & 0xFF, db = (BottomColor >> 16) & 0xFF;

        for (int y = 0; y < size; ++y)
        {
            const float dy = (static_cast<float>(y) + 0.5f) - c;
            for (int x = 0; x < size; ++x)
            {
                const float dx = (static_cast<float>(x) + 0.5f) - c;
                const float d = sqrtf(dx * dx + dy * dy);
                const float er = EdgeRadius(atan2f(dy, dx), R, n, Rotation);
                if (d >= er + 1.0f)
                {
                    s_bits[y * size + x] = 0;
                    continue;
                }

                int rr, gg, bcol;
                const int aa = ba;
                if (Gradient)
                {
                    float t = (dy + R) / (2.0f * R);
                    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
                    rr = static_cast<int>(tr + (dr - tr) * t);
                    gg = static_cast<int>(tg + (dg - tg) * t);
                    bcol = static_cast<int>(tb + (db - tb) * t);
                }
                else
                {
                    rr = br; gg = bg; bcol = bb;
                }

                float cov = er - d;
                if (cov > 1.0f) cov = 1.0f;
                if (cov < 0.0f) cov = 0.0f;

                float fall = 1.0f;
                const float soft = 1.5f;
                if (d > er - soft)
                {
                    fall = (er - d) / soft;
                    if (fall > 1.0f) fall = 1.0f;
                    if (fall < 0.0f) fall = 0.0f;
                }

                int a = static_cast<int>(static_cast<float>(aa) * cov * fall);
                if (a <= 0)
                {
                    s_bits[y * size + x] = 0;
                    continue;
                }
                const int pr = rr * a / 255;
                const int pg = gg * a / 255;
                const int pb = bcol * a / 255;
                s_bits[y * size + x] = (static_cast<uint32_t>(a) << 24) |
                                       (static_cast<uint32_t>(pr) << 16) |
                                       (static_cast<uint32_t>(pg) << 8) |
                                       static_cast<uint32_t>(pb);
            }
        }

        s_rRadius = rq; s_rShape = Shape; s_rRot = static_cast<int>(Rotation * 1000.0f);
        s_rBase = BaseColor; s_rTop = TopColor; s_rBot = BottomColor; s_rGrad = Gradient;
    }

    inline void Present()
    {
        EnsureWindow();
        if (!s_wnd) return;

        if (!Active || Radius < 1.0f)
        {
            if (s_visible)
            {
                ::ShowWindow(s_wnd, SW_HIDE);
                s_visible = false;
            }
            return;
        }

        const int rq = static_cast<int>(Radius + 0.5f);
        const int rotKey = static_cast<int>(Rotation * 1000.0f);
        if (rq != s_rRadius || Shape != s_rShape || rotKey != s_rRot ||
            BaseColor != s_rBase || TopColor != s_rTop || BottomColor != s_rBot || Gradient != s_rGrad)
        {
            Rebuild();
        }
        if (!s_bits) return;

        const int size = s_size;
        const int px = static_cast<int>(CenterX) - size / 2;
        const int py = static_cast<int>(CenterY) - size / 2;

        HDC screen = ::GetDC(nullptr);
        POINT src = { 0, 0 };
        POINT dst = { px, py };
        SIZE  sz  = { size, size };
        BLENDFUNCTION bf = {};
        bf.BlendOp             = AC_SRC_OVER;
        bf.SourceConstantAlpha = 255;
        bf.AlphaFormat         = AC_SRC_ALPHA;
        ::UpdateLayeredWindow(s_wnd, screen, &dst, &sz, s_dc, &src, 0, &bf, ULW_ALPHA);
        ::ReleaseDC(nullptr, screen);

        const int wantAff = ExcludeFromCapture ? 0x00000011 : 0x00000000;
        if (wantAff != s_affinity)
        {
            ::SetWindowDisplayAffinity(s_wnd, static_cast<DWORD>(wantAff));
            s_affinity = wantAff;
        }

        HWND insertAfter = OverlayHwnd ? OverlayHwnd : HWND_TOPMOST;
        ::SetWindowPos(s_wnd, insertAfter, px, py, size, size, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        s_visible = true;
    }
}
