/*
MIT License

Copyright (c) 2024 Stephane Cuillerdier (aka Aiekick)
Copyright (c) 2025 NewYaroslav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "imgui.h"

#if !defined(IMCOOLBAR_HAS_DOCKING) && defined(IMGUI_HAS_DOCK)
#define IMCOOLBAR_HAS_DOCKING
#endif

/// \brief Configuration flags for \c ImCoolBar.
/// \note Only one orientation flag should be specified at a time.
typedef int ImCoolBarFlags;                ///< Alias for bitfield flags.
enum ImCoolBarFlags_ {
    ImCoolBarFlags_None       = 0,         ///< No flags.
    ImCoolBarFlags_Vertical   = (1 << 0),  ///< Arrange items vertically.
    ImCoolBarFlags_Horizontal = (1 << 1),  ///< Arrange items horizontally.
};

/// \brief Configuration parameters for \c ImCoolBar.
struct ImCoolBarConfig {
    ImVec2 anchor                    = ImVec2(-1.0f, -1.0f);  ///< Anchor within the viewport [0..1].
    float normal_size                = 40.0f;                 ///< Default item size in pixels.
    float hovered_size               = 60.0f;                 ///< Item size when fully hovered (px).
    float anim_step                  = 0.15f;                 ///< Step for animation when smoothing is disabled.
    float effect_strength            = 0.5f;                  ///< Strength of the hover bubble effect [0..1].
    float mouse_smoothing_ms         = 50.0f;                 ///< EMA half-life for mouse smoothing in ms (<=0 disables).
    float anim_smoothing_ms          = 50.0f;                 ///< EMA half-life for animation smoothing in ms (<=0 uses step).
    bool snap_window_to_pixels       = true;                  ///< Snap window position to integer pixels.
    bool snap_items_to_pixels        = true;                  ///< Snap internal item offsets to integer pixels.
    bool local_antialiasing          = true;                  ///< Enable antialiasing only for the bar.
    float frame_rounding_override    = -1.0f;                 ///< <0 keeps style, >=0 pushes FrameRounding.
    bool embed_mode                  = false;                 ///< true = render inside parent window, no separate window.
    /// \brief Construct with optional parameter overrides.
    ImCoolBarConfig(                                          
        const ImVec2 vAnchor         = ImVec2(-1.0f, -1.0f),  
        const float  vNormalSize     = 40.0f,                 
        const float  vHoveredSize    = 60.0f,                 
        const float  vAnimStep       = 0.15f,                 
        const float  vEffectStrength = 0.5f,                  
        const float  vMouseSmoothingMs      = 50.0f,          
        const float  vAnimSmoothingMs       = 50.0f,          
        const bool   vSnapWindowToPixels    = true,           
        const bool   vSnapItemsToPixels     = true,
        const bool   vLocalAntialiasing     = true,
        const float  vFrameRoundingOverride = -1.0f,
        const bool   vEmbedMode             = false)          
        :                                                     
          anchor(vAnchor),                                    
          normal_size(vNormalSize),                           
          hovered_size(vHoveredSize),                         
          anim_step(vAnimStep),                               
          effect_strength(vEffectStrength),                   
          mouse_smoothing_ms(vMouseSmoothingMs),              
          anim_smoothing_ms(vAnimSmoothingMs),                
          snap_window_to_pixels(vSnapWindowToPixels),         
          snap_items_to_pixels(vSnapItemsToPixels),
          local_antialiasing(vLocalAntialiasing),             
          frame_rounding_override(vFrameRoundingOverride),
          embed_mode(vEmbedMode)
    {
    }
};

namespace ImGui {

/// \brief Begin a CoolBar container.
IMGUI_API bool BeginCoolBar(const char* vLabel, ImCoolBarFlags vCBFlags = ImCoolBarFlags_Vertical, const ImCoolBarConfig& vConfig = {}, ImGuiWindowFlags vFlags = ImGuiWindowFlags_None);

/// \brief Close the current CoolBar container.
IMGUI_API void  EndCoolBar();

/// \brief Declare an item inside the current CoolBar.
IMGUI_API bool  CoolBarItem();

/// \brief Get the width of the last CoolBar item in pixels.
IMGUI_API float GetCoolBarItemWidth();

/// \brief Get the scale of the last CoolBar item relative to its normal size.
IMGUI_API float GetCoolBarItemScale();

/// \brief Show a debug window with internal CoolBar metrics.
IMGUI_API void  ShowCoolBarMetrics(bool* vOpened);

}  // namespace ImGui