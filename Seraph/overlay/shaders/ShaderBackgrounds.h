#pragma once

// Shader-backed panel backgrounds.
//
// Each effect is a self-contained class (ColorBends, Galaxy, Plasma, ...)
// exposing the same surface:
//
//     void Init();
//     void Update(float dt_sec, ImVec2 winSize, ImVec2 winPos);
//     FBO outputFBO;              // outputFBO.srv is the ImGui texture
//
// They are unrelated types, so ShaderSlot keeps one instance of each and
// dispatches on an index. Two slots exist because the content panel and the
// sidebar need different aspect ratios; both always run the SAME selected
// effect so the menu reads as one theme.
//
// Effects self-initialise inside Update(), so nothing needs calling Init().
//
// The effect headers were written assuming they own the whole ImGui window and
// gate on ImGui::IsWindowHovered(). Here they only ever draw into a sub-rect of
// the menu, so that test is replaced with MouseInRect() against the rect passed
// to Update(). Without this, moving the mouse over the sidebar still drives the
// content shader (and vice versa), and the pointer jumps to the edges.
//
// Adapted from ImGui-Shader (CC BY-NC 4.0) - see LICENSE.md in the upstream
// project. Non-commercial use only.

#include "../imgui/imgui.h"

namespace shader
{
    // True when the cursor is inside the rect this shader instance renders to.
    // Declared before the effect headers so they can call it from inside their
    // own namespace shader block.
    inline bool MouseInRect(ImVec2 winPos, ImVec2 winSize)
    {
        if (winSize.x <= 0.0f || winSize.y <= 0.0f)
            return false;
        const ImVec2 m = ImGui::GetMousePos();
        return m.x >= winPos.x && m.x <= winPos.x + winSize.x
            && m.y >= winPos.y && m.y <= winPos.y + winSize.y;
    }
}

#include "ColorBends.h"
#include "Galaxy.h"
#include "Lightning.h"
#include "LightRays.h"
#include "LineWaves.h"
#include "LiquidChrome.h"
#include "LiquidEther.h"
#include "Particles.h"
#include "PixelSnow.h"
#include "Plasma.h"
#include "Prism.h"
#include "PrismaticBurst.h"

namespace shader
{
    enum BackgroundId
    {
        BG_COLOR_BENDS = 0,
        BG_GALAXY,
        BG_PLASMA,
        BG_LINE_WAVES,
        BG_LIGHTNING,
        BG_LIGHT_RAYS,
        BG_LIQUID_CHROME,
        BG_LIQUID_ETHER,
        BG_PARTICLES,
        BG_PIXEL_SNOW,
        BG_PRISM,
        BG_PRISMATIC_BURST,
        BG_COUNT
    };

    inline const char* const* BackgroundNames()
    {
        static const char* names[BG_COUNT] = {
            "ColorBends", "Galaxy", "Plasma", "LineWaves",
            "Lightning", "LightRays", "LiquidChrome", "LiquidEther",
            "Particles", "PixelSnow", "Prism", "PrismaticBurst"
        };
        return names;
    }

    struct ShaderSlot
    {
        ColorBends     colorBends;
        Galaxy         galaxy;
        Plasma         plasma;
        LineWaves      lineWaves;
        Lightning      lightning;
        LightRays      lightRays;
        LiquidChrome   liquidChrome;
        LiquidEther    liquidEther;
        Particles      particles;
        PixelSnow      pixelSnow;
        Prism          prism;
        PrismaticBurst prismaticBurst;

        void Update(int index, float dt_sec, ImVec2 size, ImVec2 pos)
        {
            switch (index)
            {
            case BG_GALAXY:          galaxy.Update(dt_sec, size, pos);          break;
            case BG_PLASMA:          plasma.Update(dt_sec, size, pos);          break;
            case BG_LINE_WAVES:      lineWaves.Update(dt_sec, size, pos);       break;
            case BG_LIGHTNING:       lightning.Update(dt_sec, size, pos);       break;
            case BG_LIGHT_RAYS:      lightRays.Update(dt_sec, size, pos);       break;
            case BG_LIQUID_CHROME:   liquidChrome.Update(dt_sec, size, pos);    break;
            case BG_LIQUID_ETHER:    liquidEther.Update(dt_sec, size, pos);     break;
            case BG_PARTICLES:       particles.Update(dt_sec, size, pos);       break;
            case BG_PIXEL_SNOW:      pixelSnow.Update(dt_sec, size, pos);       break;
            case BG_PRISM:           prism.Update(dt_sec, size, pos);           break;
            case BG_PRISMATIC_BURST: prismaticBurst.Update(dt_sec, size, pos); break;
            case BG_COLOR_BENDS:
            default:                 colorBends.Update(dt_sec, size, pos);      break;
            }
        }

        ID3D11ShaderResourceView* Srv(int index)
        {
            switch (index)
            {
            case BG_GALAXY:          return galaxy.outputFBO.srv;
            case BG_PLASMA:          return plasma.outputFBO.srv;
            case BG_LINE_WAVES:      return lineWaves.outputFBO.srv;
            case BG_LIGHTNING:       return lightning.outputFBO.srv;
            case BG_LIGHT_RAYS:      return lightRays.outputFBO.srv;
            case BG_LIQUID_CHROME:   return liquidChrome.outputFBO.srv;
            case BG_LIQUID_ETHER:    return liquidEther.outputFBO.srv;
            case BG_PARTICLES:       return particles.outputFBO.srv;
            case BG_PIXEL_SNOW:      return pixelSnow.outputFBO.srv;
            case BG_PRISM:           return prism.outputFBO.srv;
            case BG_PRISMATIC_BURST: return prismaticBurst.outputFBO.srv;
            case BG_COLOR_BENDS:
            default:                 return colorBends.outputFBO.srv;
            }
        }
    };
}