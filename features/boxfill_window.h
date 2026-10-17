#pragma once

#include <windows.h>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>

#pragma comment(lib, "gdi32.lib")

namespace BoxFillWindow
{
    inline bool Active = true;
    inline bool ExcludeFromCapture = false;
    inline HWND OverlayHwnd = nullptr;

    struct Rect
    {
        float x0, y0, x1, y1;
        uint32_t ctl, ctr, cbr, cbl;
        bool gradient;
    };

    inline std::vector<Rect> s_rects;

    inline HWND      s_wnd = nullptr;
    inline HDC       s_dc = nullptr;
    inline HBITMAP   s_bmp = nullptr;
    inline uint32_t* s_bits = nullptr;
    inline int       s_w = 0;
    inline int       s_h = 0;
    inline bool      s_visible = false;
    inline int       s_affinity = -1;

    inline LRESULT CALLBACK FillWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        return ::DefWindowProcW(h, m, w, l);
    }

    inline void EnsureWindow()
    {
        if (s_wnd) return;
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = FillWndProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SeraphOverlayBoxFillSurface";
        ::RegisterClassExW(&wc);
        s_wnd = ::CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"", WS_POPUP,
            0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    }

    inline void EnsureSurface(int w, int h)
    {
        if (s_bmp && s_w == w && s_h == h) return;
        if (s_bmp) { ::DeleteObject(s_bmp); s_bmp = nullptr; }
        if (s_dc) { ::DeleteDC(s_dc); s_dc = nullptr; }
        s_dc = ::CreateCompatibleDC(nullptr);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        s_bmp = ::CreateDIBSection(s_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (s_bmp) ::SelectObject(s_dc, s_bmp);
        s_bits = static_cast<uint32_t*>(bits);
        s_w = w;
        s_h = h;
    }

    inline void Begin()
    {
        s_rects.clear();
    }

    inline void AddRect(float x0, float y0, float x1, float y1,
                        uint32_t ctl, uint32_t ctr, uint32_t cbr, uint32_t cbl, bool gradient)
    {
        if (x0 > x1) { const float t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { const float t = y0; y0 = y1; y1 = t; }
        if (x1 - x0 < 0.5f || y1 - y0 < 0.5f) return;
        Rect r;
        r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
        r.ctl = ctl; r.ctr = ctr; r.cbr = cbr; r.cbl = cbl;
        r.gradient = gradient;
        s_rects.push_back(r);
    }

    inline void ClearRegion(int x, int y, int w, int h)
    {
        for (int yy = y; yy < y + h; ++yy)
            std::memset(s_bits + static_cast<size_t>(yy) * s_w + x, 0, static_cast<size_t>(w) * sizeof(uint32_t));
    }

    inline void BlendPixel(uint32_t& dst, int sr, int sg, int sb, int sa)
    {
        if (sa <= 0) return;
        const int da = static_cast<int>((dst >> 24) & 0xFF);
        const int outA = sa + da * (255 - sa) / 255;
        const int dr = static_cast<int>((dst >> 16) & 0xFF);
        const int dg = static_cast<int>((dst >> 8) & 0xFF);
        const int db = static_cast<int>(dst & 0xFF);
        int pr = sr * sa / 255 + dr * (255 - sa) / 255;
        int pg = sg * sa / 255 + dg * (255 - sa) / 255;
        int pb = sb * sa / 255 + db * (255 - sa) / 255;
        if (pr > 255) pr = 255;
        if (pg > 255) pg = 255;
        if (pb > 255) pb = 255;
        dst = (static_cast<uint32_t>(outA) << 24) |
              (static_cast<uint32_t>(pr) << 16) |
              (static_cast<uint32_t>(pg) << 8) |
              static_cast<uint32_t>(pb);
    }

    inline void Present()
    {
        EnsureWindow();
        if (!s_wnd) return;

        const int scrW = ::GetSystemMetrics(SM_CXSCREEN);
        const int scrH = ::GetSystemMetrics(SM_CYSCREEN);
        if (scrW < 1 || scrH < 1) return;

        if (!Active || s_rects.empty())
        {
            if (s_visible) { ::ShowWindow(s_wnd, SW_HIDE); s_visible = false; }
            return;
        }

        float minX = s_rects[0].x0, minY = s_rects[0].y0, maxX = s_rects[0].x1, maxY = s_rects[0].y1;
        for (const Rect& r : s_rects)
        {
            if (r.x0 < minX) minX = r.x0;
            if (r.y0 < minY) minY = r.y0;
            if (r.x1 > maxX) maxX = r.x1;
            if (r.y1 > maxY) maxY = r.y1;
        }

        int bx = static_cast<int>(floorf(minX));
        int by = static_cast<int>(floorf(minY));
        int bw = static_cast<int>(ceilf(maxX)) - bx;
        int bh = static_cast<int>(ceilf(maxY)) - by;
        if (bx < 0) { bw += bx; bx = 0; }
        if (by < 0) { bh += by; by = 0; }
        if (bx + bw > scrW) bw = scrW - bx;
        if (by + bh > scrH) bh = scrH - by;
        if (bw < 1 || bh < 1)
        {
            if (s_visible) { ::ShowWindow(s_wnd, SW_HIDE); s_visible = false; }
            return;
        }

        EnsureSurface(scrW, scrH);
        if (!s_bits) return;

        ClearRegion(bx, by, bw, bh);

        for (const Rect& r : s_rects)
        {
            float x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
            if (x0 < static_cast<float>(bx)) x0 = static_cast<float>(bx);
            if (y0 < static_cast<float>(by)) y0 = static_cast<float>(by);
            if (x1 > static_cast<float>(bx + bw)) x1 = static_cast<float>(bx + bw);
            if (y1 > static_cast<float>(by + bh)) y1 = static_cast<float>(by + bh);
            if (x1 - x0 < 0.5f || y1 - y0 < 0.5f) continue;

            const int ix0 = static_cast<int>(floorf(x0));
            const int iy0 = static_cast<int>(floorf(y0));
            const int ix1 = static_cast<int>(ceilf(x1));
            const int iy1 = static_cast<int>(ceilf(y1));
            const float rw = r.x1 - r.x0;
            const float rh = r.y1 - r.y0;

            const int tlA = static_cast<int>((r.ctl >> 24) & 0xFF);
            const int tlR = static_cast<int>(r.ctl & 0xFF);
            const int tlG = static_cast<int>((r.ctl >> 8) & 0xFF);
            const int tlB = static_cast<int>((r.ctl >> 16) & 0xFF);
            const int trA = static_cast<int>((r.ctr >> 24) & 0xFF);
            const int trR = static_cast<int>(r.ctr & 0xFF);
            const int trG = static_cast<int>((r.ctr >> 8) & 0xFF);
            const int trB = static_cast<int>((r.ctr >> 16) & 0xFF);
            const int brA = static_cast<int>((r.cbr >> 24) & 0xFF);
            const int brR = static_cast<int>(r.cbr & 0xFF);
            const int brG = static_cast<int>((r.cbr >> 8) & 0xFF);
            const int brB = static_cast<int>((r.cbr >> 16) & 0xFF);
            const int blA = static_cast<int>((r.cbl >> 24) & 0xFF);
            const int blR = static_cast<int>(r.cbl & 0xFF);
            const int blG = static_cast<int>((r.cbl >> 8) & 0xFF);
            const int blB = static_cast<int>((r.cbl >> 16) & 0xFF);

            for (int y = iy0; y < iy1; ++y)
            {
                if (y < by || y >= by + bh) continue;
                float v = (static_cast<float>(y) + 0.5f - r.y0) / rh;
                if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
                uint32_t* row = s_bits + static_cast<size_t>(y) * s_w;
                for (int x = ix0; x < ix1; ++x)
                {
                    if (x < bx || x >= bx + bw) continue;
                    float u = (static_cast<float>(x) + 0.5f - r.x0) / rw;
                    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;

                    int cr, cg, cb, ca;
                    if (r.gradient)
                    {
                        const float ftop_r = tlR + (trR - tlR) * u;
                        const float fbot_r = blR + (brR - blR) * u;
                        const float ftop_g = tlG + (trG - tlG) * u;
                        const float fbot_g = blG + (brG - blG) * u;
                        const float ftop_b = tlB + (trB - tlB) * u;
                        const float fbot_b = blB + (brB - blB) * u;
                        const float ftop_a = tlA + (trA - tlA) * u;
                        const float fbot_a = blA + (brA - blA) * u;
                        cr = static_cast<int>(ftop_r + (fbot_r - ftop_r) * v);
                        cg = static_cast<int>(ftop_g + (fbot_g - ftop_g) * v);
                        cb = static_cast<int>(ftop_b + (fbot_b - ftop_b) * v);
                        ca = static_cast<int>(ftop_a + (fbot_a - ftop_a) * v);
                    }
                    else
                    {
                        cr = tlR; cg = tlG; cb = tlB; ca = tlA;
                    }
                    BlendPixel(row[x], cr, cg, cb, ca);
                }
            }
        }

        HDC screen = ::GetDC(nullptr);
        POINT src = { bx, by };
        POINT dst = { bx, by };
        SIZE  sz = { bw, bh };
        BLENDFUNCTION bf = {};
        bf.BlendOp = AC_SRC_OVER;
        bf.SourceConstantAlpha = 255;
        bf.AlphaFormat = AC_SRC_ALPHA;
        ::UpdateLayeredWindow(s_wnd, screen, &dst, &sz, s_dc, &src, 0, &bf, ULW_ALPHA);
        ::ReleaseDC(nullptr, screen);

        const int wantAff = ExcludeFromCapture ? 0x00000011 : 0x00000000;
        if (wantAff != s_affinity)
        {
            ::SetWindowDisplayAffinity(s_wnd, static_cast<DWORD>(wantAff));
            s_affinity = wantAff;
        }

        HWND insertAfter = OverlayHwnd ? OverlayHwnd : HWND_TOPMOST;
        ::SetWindowPos(s_wnd, insertAfter, bx, by, bw, bh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        s_visible = true;
    }
}
