//
// MD3 颜色系统 (Material Design 3 Color System)
// 从 imgui_md3 项目提取，适配 C++17
// 基于 HCT (Hue-Chroma-Tone) 色彩空间和 CAM16 模型
//

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <imgui.h>

namespace md3 {

// ============================================================================
// ARGB 颜色
// ============================================================================
struct argb_t {
    std::uint8_t a = 255;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    ImVec4 to_imvec() const noexcept {
        return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    }
    static argb_t from_imvec(ImVec4 const v) noexcept;
};

// ============================================================================
// 色彩工具 (sRGB <-> XYZ, Lstar 等)
// ============================================================================
class color_utils {
public:
    static constexpr std::array<std::array<double, 3>, 3> k_srgb_to_xyz = {{
        {0.41233895, 0.35762064, 0.18051042},
        {0.2126, 0.7152, 0.0722},
        {0.01932141, 0.11916382, 0.95034478}
    }};
    static constexpr std::array<std::array<double, 3>, 3> k_xyz_to_srgb = {{
        {3.2413774792388685, -1.5376652402851851, -0.49885366846268053},
        {-0.9691452513005321, 1.8758853451067872, 0.04156585616912061},
        {0.05562093689691305, -0.20395524564742123, 1.0571799111220335}
    }};
    static constexpr std::array<double, 3> k_white_point_d65 = {95.047, 100.0, 108.883};

    static double srgb_to_linear(double channel) noexcept;
    static double linear_to_srgb(double channel) noexcept;
    static double y_from_lstar(double lstar) noexcept;
    static double lstar_from_y(double y) noexcept;
    static std::array<double, 3> xyz_from_argb(argb_t const value) noexcept;
    static argb_t argb_from_linrgb(std::array<double, 3> const linrgb) noexcept;
    static argb_t argb_from_lstar(double lstar) noexcept;
    static double sanitize_degrees(double degrees) noexcept;
};

// ============================================================================
// 观察条件
// ============================================================================
struct viewing_conditions_t {
    double n;
    double aw;
    double nbb;
    double ncb;
    double c;
    double nc;
    double fl;
    double fl_root;
    double z;
    std::array<double, 3> rgb_d;

    static viewing_conditions_t make_default() noexcept;
};

// ============================================================================
// CAM16 颜色模型
// ============================================================================
class cam16 {
public:
    double hue;
    double chroma;
    double j;
    double q;
    double m;
    double s;
    double jstar;
    double astar;
    double bstar;

    static cam16 from_argb(argb_t const value) noexcept;
    static cam16 from_argb_in_vc(argb_t const value, viewing_conditions_t const& vc) noexcept;
    static cam16 from_jch_in_vc(double jv, double cv, double hv, viewing_conditions_t const& vc) noexcept;

    argb_t viewed(viewing_conditions_t const& vc) const noexcept;
};

// ============================================================================
// HCT 求解器
// ============================================================================
class hct_solver {
public:
    static argb_t solve(double hue_deg, double chroma_in, double lstar) noexcept;

private:
    static argb_t find_result_by_j(double hue_rad, double chroma_in, double y_target) noexcept;
    static argb_t bisect_chroma(double hue_deg, double chroma_max, double lstar) noexcept;
};

// ============================================================================
// HCT 颜色
// ============================================================================
struct hct_t {
    double hue;
    double chroma;
    double tone;
    argb_t argb;

    static hct_t from_argb(argb_t const value) noexcept;
    static hct_t from_hct(double h, double c, double t) noexcept;
};

// ============================================================================
// 方案角色枚举
// ============================================================================
enum class scheme_role_e : std::uint8_t {
    primary,
    on_primary,
    primary_container,
    on_primary_container,
    secondary,
    on_secondary,
    secondary_container,
    on_secondary_container,
    tertiary,
    on_tertiary,
    tertiary_container,
    on_tertiary_container,
    error,
    on_error,
    error_container,
    on_error_container,
    background,
    on_background,
    surface,
    on_surface,
    surface_variant,
    on_surface_variant,
    surface_dim,
    surface_bright,
    surface_container_lowest,
    surface_container_low,
    surface_container,
    surface_container_high,
    surface_container_highest,
    outline,
    outline_variant,
    inverse_surface,
    inverse_on_surface,
    inverse_primary,
    scrim,
    shadow,
    count
};

// ============================================================================
// 色调调色板
// ============================================================================
class tonal_palette {
public:
    double hue;
    double chroma;

    tonal_palette(double hue_in, double chroma_in) noexcept : hue(hue_in), chroma(chroma_in) {}
    static tonal_palette from_argb(argb_t const value) noexcept;

    argb_t get(double tone) const noexcept;
    ImVec4 imvec(double tone) const noexcept { return get(tone).to_imvec(); }
};

// ============================================================================
// 颜色方案
// ============================================================================
class scheme {
public:
    std::array<argb_t, static_cast<std::size_t>(scheme_role_e::count)> roles{};

    argb_t get(scheme_role_e const role) const noexcept {
        return roles[static_cast<std::size_t>(role)];
    }

    ImVec4 imvec(scheme_role_e const role) const noexcept {
        return get(role).to_imvec();
    }
};

// ============================================================================
// 核心调色板
// ============================================================================
struct core_palette_t {
    tonal_palette primary;
    tonal_palette secondary;
    tonal_palette tertiary;
    tonal_palette neutral;
    tonal_palette neutral_variant;
    tonal_palette error;

    static core_palette_t from_source(argb_t const accent) noexcept;
};

// ============================================================================
// 调色板包 (light + dark)
// ============================================================================
struct palette_bundle_t {
    core_palette_t core;
    scheme light;
    scheme dark;

    static palette_bundle_t from_source(argb_t const accent) noexcept;
    static palette_bundle_t from_source(ImVec4 const accent) noexcept {
        return from_source(argb_t::from_imvec(accent));
    }
};

// ============================================================================
// ImGui 样式应用器
// ============================================================================
class imgui_style {
public:
    static void apply(scheme const& scheme, ImGuiStyle* style) noexcept;
    static argb_t mix(argb_t base, argb_t overlay, float opacity) noexcept;
    static ImVec4 with_alpha(argb_t color, float alpha) noexcept;
};

} // namespace md3
