//
// MD3 颜色系统实现 (适配 C++17)
//

#include "md3_color.hpp"

#include <algorithm>
#include <cstring>

namespace md3 {

// C++17 没有 std::numbers::pi，自定义
static constexpr double k_pi = 3.14159265358979323846;

// C++17 没有 std::lerp，自定义
template <typename T>
static constexpr T lerp(T a, T b, T t) noexcept {
    return a + (b - a) * t;
}

// ============================================================================
// argb_t
// ============================================================================
argb_t argb_t::from_imvec(ImVec4 const v) noexcept {
    auto const clamp8 = [](float const x) noexcept -> std::uint8_t {
        return static_cast<std::uint8_t>(std::round(std::clamp(x, 0.0f, 1.0f) * 255.0f));
    };
    return { clamp8(v.w), clamp8(v.x), clamp8(v.y), clamp8(v.z) };
}

// ============================================================================
// color_utils
// ============================================================================
double color_utils::srgb_to_linear(double const channel) noexcept {
    double const normalized = channel / 255.0;
    return normalized <= 0.040449936 ? normalized / 12.92 : std::pow((normalized + 0.055) / 1.055, 2.4);
}

double color_utils::linear_to_srgb(double const channel) noexcept {
    double const v = std::clamp(channel, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

double color_utils::y_from_lstar(double const lstar) noexcept {
    double const ke = 8.0;
    if (lstar > ke) {
        double const cube_root = (lstar + 16.0) / 116.0;
        return 100.0 * cube_root * cube_root * cube_root;
    }
    return 100.0 * lstar / (24389.0 / 27.0);
}

double color_utils::lstar_from_y(double const y) noexcept {
    double const e = 216.0 / 24389.0;
    double const y_norm = y / 100.0;
    if (y_norm <= e)
        return y_norm * (24389.0 / 27.0);
    return 116.0 * std::cbrt(y_norm) - 16.0;
}

std::array<double, 3> color_utils::xyz_from_argb(argb_t const value) noexcept {
    double const rl = srgb_to_linear(value.r) * 100.0;
    double const gl = srgb_to_linear(value.g) * 100.0;
    double const bl = srgb_to_linear(value.b) * 100.0;
    auto const& m = k_srgb_to_xyz;
    return {
        m[0][0] * rl + m[0][1] * gl + m[0][2] * bl,
        m[1][0] * rl + m[1][1] * gl + m[1][2] * bl,
        m[2][0] * rl + m[2][1] * gl + m[2][2] * bl
    };
}

argb_t color_utils::argb_from_linrgb(std::array<double, 3> const linrgb) noexcept {
    auto const encode = [](double const v) noexcept -> std::uint8_t {
        double const s = linear_to_srgb(v / 100.0);
        return static_cast<std::uint8_t>(std::round(std::clamp(s, 0.0, 1.0) * 255.0));
    };
    return { 255, encode(linrgb[0]), encode(linrgb[1]), encode(linrgb[2]) };
}

argb_t color_utils::argb_from_lstar(double const lstar) noexcept {
    double const y = y_from_lstar(lstar);
    auto const component = [](double const v) noexcept -> std::uint8_t {
        double const s = linear_to_srgb(v / 100.0);
        return static_cast<std::uint8_t>(std::round(std::clamp(s, 0.0, 1.0) * 255.0));
    };
    std::uint8_t const v = component(y);
    return { 255, v, v, v };
}

double color_utils::sanitize_degrees(double degrees) noexcept {
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0)
        degrees += 360.0;
    return degrees;
}

// ============================================================================
// viewing_conditions_t
// ============================================================================
viewing_conditions_t viewing_conditions_t::make_default() noexcept {
    auto const& wp = color_utils::k_white_point_d65;
    double const adapting_luminance = (200.0 / k_pi) * color_utils::y_from_lstar(50.0) / 100.0;
    double const background_lstar = 50.0;
    double const surround = 2.0;
    bool const discounting_illuminant = false;

    double const r_w = 0.401288 * wp[0] + 0.650173 * wp[1] - 0.051461 * wp[2];
    double const g_w = -0.250268 * wp[0] + 1.204414 * wp[1] + 0.045854 * wp[2];
    double const b_w = -0.002079 * wp[0] + 0.048952 * wp[1] + 0.953127 * wp[2];

    double const f = 0.8 + surround / 10.0;
    double const c = f >= 0.9
                         ? lerp(0.59, 0.69, (f - 0.9) * 10.0)
                         : lerp(0.525, 0.59, (f - 0.8) * 10.0);
    double d = discounting_illuminant
                   ? 1.0
                   : f * (1.0 - (1.0 / 3.6) * std::exp((-adapting_luminance - 42.0) / 92.0));
    d = std::clamp(d, 0.0, 1.0);
    double const nc = f;
    std::array<double, 3> const rgb_d = {
        d * (100.0 / r_w) + 1.0 - d,
        d * (100.0 / g_w) + 1.0 - d,
        d * (100.0 / b_w) + 1.0 - d
    };
    double const k = 1.0 / (5.0 * adapting_luminance + 1.0);
    double const k4 = k * k * k * k;
    double const k4_m1 = 1.0 - k4;
    double const fl = k4 * adapting_luminance + 0.1 * k4_m1 * k4_m1 * std::cbrt(5.0 * adapting_luminance);
    double const n_val = color_utils::y_from_lstar(background_lstar) / wp[1];
    double const z = 1.48 + std::sqrt(n_val);
    double const nbb = 0.725 / std::pow(n_val, 0.2);
    double const ncb = nbb;
    std::array<double, 3> const rgb_a_factors = {
        std::pow(fl * rgb_d[0] * r_w / 100.0, 0.42),
        std::pow(fl * rgb_d[1] * g_w / 100.0, 0.42),
        std::pow(fl * rgb_d[2] * b_w / 100.0, 0.42)
    };
    std::array<double, 3> const rgb_a = {
        400.0 * rgb_a_factors[0] / (rgb_a_factors[0] + 27.13),
        400.0 * rgb_a_factors[1] / (rgb_a_factors[1] + 27.13),
        400.0 * rgb_a_factors[2] / (rgb_a_factors[2] + 27.13)
    };
    double const aw = (2.0 * rgb_a[0] + rgb_a[1] + 0.05 * rgb_a[2]) * nbb;

    return { n_val, aw, nbb, ncb, c, nc, fl, std::pow(fl, 0.25), z, rgb_d };
}

// ============================================================================
// cam16
// ============================================================================
cam16 cam16::from_argb(argb_t const value) noexcept {
    return from_argb_in_vc(value, viewing_conditions_t::make_default());
}

cam16 cam16::from_argb_in_vc(argb_t const value, viewing_conditions_t const& vc) noexcept {
    auto const xyz = color_utils::xyz_from_argb(value);
    double const x = xyz[0], y = xyz[1], z = xyz[2];

    double const r_c = 0.401288 * x + 0.650173 * y - 0.051461 * z;
    double const g_c = -0.250268 * x + 1.204414 * y + 0.045854 * z;
    double const b_c = -0.002079 * x + 0.048952 * y + 0.953127 * z;

    double const r_d = vc.rgb_d[0] * r_c;
    double const g_d = vc.rgb_d[1] * g_c;
    double const b_d = vc.rgb_d[2] * b_c;

    double const r_af = std::pow(vc.fl * std::abs(r_d) / 100.0, 0.42);
    double const g_af = std::pow(vc.fl * std::abs(g_d) / 100.0, 0.42);
    double const b_af = std::pow(vc.fl * std::abs(b_d) / 100.0, 0.42);

    double const r_a = std::copysign(400.0 * r_af / (r_af + 27.13), r_d);
    double const g_a = std::copysign(400.0 * g_af / (g_af + 27.13), g_d);
    double const b_a = std::copysign(400.0 * b_af / (b_af + 27.13), b_d);

    double const a = (11.0 * r_a - 12.0 * g_a + b_a) / 11.0;
    double const bb = (r_a + g_a - 2.0 * b_a) / 9.0;
    double const u = (20.0 * r_a + 20.0 * g_a + 21.0 * b_a) / 20.0;
    double const p2 = (40.0 * r_a + 20.0 * g_a + b_a) / 20.0;

    double const atan2_rad = std::atan2(bb, a);
    double const hue = color_utils::sanitize_degrees(atan2_rad * 180.0 / k_pi);
    double const hue_rad = hue * k_pi / 180.0;
    double const ac = p2 * vc.nbb;

    double const j = 100.0 * std::pow(ac / vc.aw, vc.c * vc.z);
    double const q = (4.0 / vc.c) * std::sqrt(j / 100.0) * (vc.aw + 4.0) * vc.fl_root;

    double const hue_prime = hue < 20.14 ? hue + 360.0 : hue;
    double const e_hue = 0.25 * (std::cos(hue_prime * k_pi / 180.0 + 2.0) + 3.8);
    double const p1 = 50000.0 / 13.0 * e_hue * vc.nc * vc.ncb;
    double const t = p1 * std::sqrt(a * a + bb * bb) / (u + 0.305);
    double const alpha = std::pow(t, 0.9) * std::pow(1.64 - std::pow(0.29, vc.n), 0.73);
    double const chroma = alpha * std::sqrt(j / 100.0);
    double const m = chroma * vc.fl_root;
    double const s = 50.0 * std::sqrt(alpha * vc.c / (vc.aw + 4.0));

    double const jstar = (1.0 + 100.0 * 0.007) * j / (1.0 + 0.007 * j);
    double const mstar = std::log(1.0 + 0.0228 * m) / 0.0228;
    return { hue, chroma, j, q, m, s, jstar, mstar * std::cos(hue_rad), mstar * std::sin(hue_rad) };
}

cam16 cam16::from_jch_in_vc(double const jv, double const cv, double const hv, viewing_conditions_t const& vc) noexcept {
    double const q = (4.0 / vc.c) * std::sqrt(jv / 100.0) * (vc.aw + 4.0) * vc.fl_root;
    double const m = cv * vc.fl_root;
    double const alpha = cv / std::sqrt(jv / 100.0 + 1e-9);
    double const s = 50.0 * std::sqrt(alpha * vc.c / (vc.aw + 4.0));
    double const hue_rad = hv * k_pi / 180.0;
    double const jstar = (1.0 + 100.0 * 0.007) * jv / (1.0 + 0.007 * jv);
    double const mstar = std::log(1.0 + 0.0228 * m) / 0.0228;
    return { hv, cv, jv, q, m, s, jstar, mstar * std::cos(hue_rad), mstar * std::sin(hue_rad) };
}

argb_t cam16::viewed(viewing_conditions_t const& vc) const noexcept {
    double const alpha = (chroma == 0.0 || j == 0.0) ? 0.0 : chroma / std::sqrt(j / 100.0);
    double const t = std::pow(alpha / std::pow(1.64 - std::pow(0.29, vc.n), 0.73), 1.0 / 0.9);
    double const h_rad = hue * k_pi / 180.0;
    double const e_hue = 0.25 * (std::cos(h_rad + 2.0) + 3.8);
    double const ac = vc.aw * std::pow(j / 100.0, 1.0 / vc.c / vc.z);
    double const p1 = e_hue * 50000.0 / 13.0 * vc.nc * vc.ncb;
    double const p2 = ac / vc.nbb;

    double const h_sin = std::sin(h_rad);
    double const h_cos = std::cos(h_rad);
    double const gamma = 23.0 * (p2 + 0.305) * t / (23.0 * p1 + 11.0 * t * h_cos + 108.0 * t * h_sin);
    double const a = gamma * h_cos;
    double const b = gamma * h_sin;

    double const r_a = (460.0 * p2 + 451.0 * a + 288.0 * b) / 1403.0;
    double const g_a = (460.0 * p2 - 891.0 * a - 261.0 * b) / 1403.0;
    double const b_a = (460.0 * p2 - 220.0 * a - 6300.0 * b) / 1403.0;

    auto const invert = [&vc](double const c_a) noexcept -> double {
        double const abs_val = std::max(0.0, 27.13 * std::abs(c_a) / (400.0 - std::abs(c_a)));
        return std::copysign(100.0 / vc.fl * std::pow(abs_val, 1.0 / 0.42), c_a);
    };

    double const r_c = invert(r_a) / vc.rgb_d[0];
    double const g_c = invert(g_a) / vc.rgb_d[1];
    double const b_c = invert(b_a) / vc.rgb_d[2];

    double const x = 1.86206786 * r_c - 1.01125463 * g_c + 0.14918677 * b_c;
    double const y = 0.38752654 * r_c + 0.62144744 * g_c - 0.00897398 * b_c;
    double const z = -0.01584150 * r_c - 0.03412294 * g_c + 1.04996444 * b_c;

    auto const& inv = color_utils::k_xyz_to_srgb;
    double const rl = inv[0][0] * x + inv[0][1] * y + inv[0][2] * z;
    double const gl = inv[1][0] * x + inv[1][1] * y + inv[1][2] * z;
    double const bl = inv[2][0] * x + inv[2][1] * y + inv[2][2] * z;

    return color_utils::argb_from_linrgb({ rl, gl, bl });
}

// ============================================================================
// hct_solver
// ============================================================================
argb_t hct_solver::find_result_by_j(double const hue_rad, double const chroma_in, double const y_target) noexcept {
    auto const vc = viewing_conditions_t::make_default();
    double const hue_deg = color_utils::sanitize_degrees(hue_rad * 180.0 / k_pi);
    double j = std::sqrt(y_target) * 11.0;

    for (int iteration = 0; iteration < 30; ++iteration) {
        auto const cam = cam16::from_jch_in_vc(j, chroma_in, hue_deg, vc);
        auto const rgb = cam.viewed(vc);
        auto const xyz = color_utils::xyz_from_argb(rgb);
        double const y_actual = xyz[1];
        double const err = y_actual - y_target;
        if (std::abs(err) < 0.002)
            return rgb;
        double const dy_dj = std::max(2.0 * j * y_target / (j * j + 1e-6), 1e-3);
        j = std::clamp(j - err / dy_dj, 0.001, 100.0);
    }
    return cam16::from_jch_in_vc(j, chroma_in, hue_deg, vc).viewed(vc);
}

argb_t hct_solver::bisect_chroma(double const hue_deg, double const chroma_max, double const lstar) noexcept {
    double lo = 0.0;
    double hi = chroma_max;
    double const y_target = color_utils::y_from_lstar(lstar);
    double const hue_rad = hue_deg * k_pi / 180.0;
    argb_t best = color_utils::argb_from_lstar(lstar);

    for (int iteration = 0; iteration < 20; ++iteration) {
        double const mid = 0.5 * (lo + hi);
        auto const candidate = find_result_by_j(hue_rad, mid, y_target);
        auto const cam = cam16::from_argb(candidate);
        double const tone_actual = color_utils::lstar_from_y(color_utils::xyz_from_argb(candidate)[1]);
        bool const tone_ok = std::abs(tone_actual - lstar) < 0.5;
        bool const chroma_ok = cam.chroma >= mid * 0.9;
        if (tone_ok && chroma_ok) {
            best = candidate;
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return best;
}

argb_t hct_solver::solve(double const hue_deg, double const chroma_in, double const lstar) noexcept {
    if (chroma_in < 0.0001 || lstar < 0.0001 || lstar > 99.9999) {
        return color_utils::argb_from_lstar(lstar);
    }
    double const hue_sanitized = color_utils::sanitize_degrees(hue_deg);
    double const y_target = color_utils::y_from_lstar(lstar);
    double const hue_rad = hue_sanitized * k_pi / 180.0;

    auto const exact = find_result_by_j(hue_rad, chroma_in, y_target);
    auto const cam_check = cam16::from_argb(exact);
    double const tone_check = color_utils::lstar_from_y(color_utils::xyz_from_argb(exact)[1]);
    if (std::abs(tone_check - lstar) < 0.5 && cam_check.chroma >= chroma_in * 0.95) {
        return exact;
    }
    return bisect_chroma(hue_sanitized, chroma_in, lstar);
}

// ============================================================================
// hct_t
// ============================================================================
hct_t hct_t::from_argb(argb_t const value) noexcept {
    auto const cam = cam16::from_argb(value);
    double const tone = color_utils::lstar_from_y(color_utils::xyz_from_argb(value)[1]);
    return { cam.hue, cam.chroma, tone, value };
}

hct_t hct_t::from_hct(double const h, double const c, double const t) noexcept {
    argb_t const argb = hct_solver::solve(h, c, t);
    return { h, c, t, argb };
}

// ============================================================================
// tonal_palette
// ============================================================================
tonal_palette tonal_palette::from_argb(argb_t const value) noexcept {
    auto const src = hct_t::from_argb(value);
    return { src.hue, src.chroma };
}

argb_t tonal_palette::get(double const tone) const noexcept {
    return hct_solver::solve(hue, chroma, std::clamp(tone, 0.0, 100.0));
}

// ============================================================================
// core_palette_t
// ============================================================================
core_palette_t core_palette_t::from_source(argb_t const accent) noexcept {
    auto const source = hct_t::from_argb(accent);
    double const hue = source.hue;
    double const primary_chroma = std::max(source.chroma, 48.0);
    return {
        tonal_palette{ hue, primary_chroma },
        tonal_palette{ hue, 16.0 },
        tonal_palette{ color_utils::sanitize_degrees(hue + 60.0), 24.0 },
        tonal_palette{ hue, 4.0 },
        tonal_palette{ hue, 8.0 },
        tonal_palette{ 25.0, 84.0 }
    };
}

// ============================================================================
// palette_bundle_t
// ============================================================================
palette_bundle_t palette_bundle_t::from_source(argb_t const accent) noexcept {
    auto const core = core_palette_t::from_source(accent);

    auto const assign_light = [&core]() noexcept -> scheme {
        scheme out{};
        auto& r = out.roles;
        r[static_cast<std::size_t>(scheme_role_e::primary)] = core.primary.get(40);
        r[static_cast<std::size_t>(scheme_role_e::on_primary)] = core.primary.get(100);
        r[static_cast<std::size_t>(scheme_role_e::primary_container)] = core.primary.get(90);
        r[static_cast<std::size_t>(scheme_role_e::on_primary_container)] = core.primary.get(10);
        r[static_cast<std::size_t>(scheme_role_e::secondary)] = core.secondary.get(40);
        r[static_cast<std::size_t>(scheme_role_e::on_secondary)] = core.secondary.get(100);
        r[static_cast<std::size_t>(scheme_role_e::secondary_container)] = core.secondary.get(90);
        r[static_cast<std::size_t>(scheme_role_e::on_secondary_container)] = core.secondary.get(10);
        r[static_cast<std::size_t>(scheme_role_e::tertiary)] = core.tertiary.get(40);
        r[static_cast<std::size_t>(scheme_role_e::on_tertiary)] = core.tertiary.get(100);
        r[static_cast<std::size_t>(scheme_role_e::tertiary_container)] = core.tertiary.get(90);
        r[static_cast<std::size_t>(scheme_role_e::on_tertiary_container)] = core.tertiary.get(10);
        r[static_cast<std::size_t>(scheme_role_e::error)] = core.error.get(40);
        r[static_cast<std::size_t>(scheme_role_e::on_error)] = core.error.get(100);
        r[static_cast<std::size_t>(scheme_role_e::error_container)] = core.error.get(90);
        r[static_cast<std::size_t>(scheme_role_e::on_error_container)] = core.error.get(10);
        r[static_cast<std::size_t>(scheme_role_e::background)] = core.neutral.get(98);
        r[static_cast<std::size_t>(scheme_role_e::on_background)] = core.neutral.get(10);
        r[static_cast<std::size_t>(scheme_role_e::surface)] = core.neutral.get(98);
        r[static_cast<std::size_t>(scheme_role_e::on_surface)] = core.neutral.get(10);
        r[static_cast<std::size_t>(scheme_role_e::surface_variant)] = core.neutral_variant.get(90);
        r[static_cast<std::size_t>(scheme_role_e::on_surface_variant)] = core.neutral_variant.get(30);
        r[static_cast<std::size_t>(scheme_role_e::surface_dim)] = core.neutral.get(87);
        r[static_cast<std::size_t>(scheme_role_e::surface_bright)] = core.neutral.get(98);
        r[static_cast<std::size_t>(scheme_role_e::surface_container_lowest)] = core.neutral.get(100);
        r[static_cast<std::size_t>(scheme_role_e::surface_container_low)] = core.neutral.get(96);
        r[static_cast<std::size_t>(scheme_role_e::surface_container)] = core.neutral.get(94);
        r[static_cast<std::size_t>(scheme_role_e::surface_container_high)] = core.neutral.get(92);
        r[static_cast<std::size_t>(scheme_role_e::surface_container_highest)] = core.neutral.get(90);
        r[static_cast<std::size_t>(scheme_role_e::outline)] = core.neutral_variant.get(50);
        r[static_cast<std::size_t>(scheme_role_e::outline_variant)] = core.neutral_variant.get(80);
        r[static_cast<std::size_t>(scheme_role_e::inverse_surface)] = core.neutral.get(20);
        r[static_cast<std::size_t>(scheme_role_e::inverse_on_surface)] = core.neutral.get(95);
        r[static_cast<std::size_t>(scheme_role_e::inverse_primary)] = core.primary.get(80);
        r[static_cast<std::size_t>(scheme_role_e::scrim)] = core.neutral.get(0);
        r[static_cast<std::size_t>(scheme_role_e::shadow)] = core.neutral.get(0);
        return out;
    };

    auto const assign_dark = [&core]() noexcept -> scheme {
        scheme out{};
        auto& r = out.roles;
        r[static_cast<std::size_t>(scheme_role_e::primary)] = core.primary.get(80);
        r[static_cast<std::size_t>(scheme_role_e::on_primary)] = core.primary.get(20);
        r[static_cast<std::size_t>(scheme_role_e::primary_container)] = core.primary.get(30);
        r[static_cast<std::size_t>(scheme_role_e::on_primary_container)] = core.primary.get(90);
        r[static_cast<std::size_t>(scheme_role_e::secondary)] = core.secondary.get(80);
        r[static_cast<std::size_t>(scheme_role_e::on_secondary)] = core.secondary.get(20);
        r[static_cast<std::size_t>(scheme_role_e::secondary_container)] = core.secondary.get(30);
        r[static_cast<std::size_t>(scheme_role_e::on_secondary_container)] = core.secondary.get(90);
        r[static_cast<std::size_t>(scheme_role_e::tertiary)] = core.tertiary.get(80);
        r[static_cast<std::size_t>(scheme_role_e::on_tertiary)] = core.tertiary.get(20);
        r[static_cast<std::size_t>(scheme_role_e::tertiary_container)] = core.tertiary.get(30);
        r[static_cast<std::size_t>(scheme_role_e::on_tertiary_container)] = core.tertiary.get(90);
        r[static_cast<std::size_t>(scheme_role_e::error)] = core.error.get(80);
        r[static_cast<std::size_t>(scheme_role_e::on_error)] = core.error.get(20);
        r[static_cast<std::size_t>(scheme_role_e::error_container)] = core.error.get(30);
        r[static_cast<std::size_t>(scheme_role_e::on_error_container)] = core.error.get(90);
        r[static_cast<std::size_t>(scheme_role_e::background)] = core.neutral.get(6);
        r[static_cast<std::size_t>(scheme_role_e::on_background)] = core.neutral.get(90);
        r[static_cast<std::size_t>(scheme_role_e::surface)] = core.neutral.get(6);
        r[static_cast<std::size_t>(scheme_role_e::on_surface)] = core.neutral.get(90);
        r[static_cast<std::size_t>(scheme_role_e::surface_variant)] = core.neutral_variant.get(30);
        r[static_cast<std::size_t>(scheme_role_e::on_surface_variant)] = core.neutral_variant.get(80);
        r[static_cast<std::size_t>(scheme_role_e::surface_dim)] = core.neutral.get(6);
        r[static_cast<std::size_t>(scheme_role_e::surface_bright)] = core.neutral.get(24);
        r[static_cast<std::size_t>(scheme_role_e::surface_container_lowest)] = core.neutral.get(4);
        r[static_cast<std::size_t>(scheme_role_e::surface_container_low)] = core.neutral.get(10);
        r[static_cast<std::size_t>(scheme_role_e::surface_container)] = core.neutral.get(12);
        r[static_cast<std::size_t>(scheme_role_e::surface_container_high)] = core.neutral.get(17);
        r[static_cast<std::size_t>(scheme_role_e::surface_container_highest)] = core.neutral.get(22);
        r[static_cast<std::size_t>(scheme_role_e::outline)] = core.neutral_variant.get(60);
        r[static_cast<std::size_t>(scheme_role_e::outline_variant)] = core.neutral_variant.get(30);
        r[static_cast<std::size_t>(scheme_role_e::inverse_surface)] = core.neutral.get(90);
        r[static_cast<std::size_t>(scheme_role_e::inverse_on_surface)] = core.neutral.get(20);
        r[static_cast<std::size_t>(scheme_role_e::inverse_primary)] = core.primary.get(40);
        r[static_cast<std::size_t>(scheme_role_e::scrim)] = core.neutral.get(0);
        r[static_cast<std::size_t>(scheme_role_e::shadow)] = core.neutral.get(0);
        return out;
    };

    return { core, assign_light(), assign_dark() };
}

// ============================================================================
// imgui_style
// ============================================================================
argb_t imgui_style::mix(argb_t const base, argb_t const overlay, float const opacity) noexcept {
    float const t = std::clamp(opacity, 0.0f, 1.0f);
    auto const lerp_u8 = [t](std::uint8_t const a, std::uint8_t const b) noexcept -> std::uint8_t {
        return static_cast<std::uint8_t>(std::round(a + (b - a) * t));
    };
    return { base.a, lerp_u8(base.r, overlay.r), lerp_u8(base.g, overlay.g), lerp_u8(base.b, overlay.b) };
}

ImVec4 imgui_style::with_alpha(argb_t const color, float const alpha) noexcept {
    ImVec4 v = color.to_imvec();
    v.w = std::clamp(alpha, 0.0f, 1.0f);
    return v;
}

void imgui_style::apply(scheme const& sch, ImGuiStyle* style) noexcept {
    auto const imv = [&sch](scheme_role_e const r) noexcept -> ImVec4 {
        return sch.get(r).to_imvec();
    };

    argb_t const primary_c = sch.get(scheme_role_e::primary);
    argb_t const on_primary_c = sch.get(scheme_role_e::on_primary);
    argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
    argb_t const surface_container_c = sch.get(scheme_role_e::surface_container);
    argb_t const surface_container_highest_c = sch.get(scheme_role_e::surface_container_highest);
    argb_t const secondary_container_c = sch.get(scheme_role_e::secondary_container);
    argb_t const on_secondary_container_c = sch.get(scheme_role_e::on_secondary_container);
    argb_t const outline_variant_c = sch.get(scheme_role_e::outline_variant);
    argb_t const scrim_c = sch.get(scheme_role_e::scrim);

    auto& col = style->Colors;

    col[ImGuiCol_Text] = imv(scheme_role_e::on_surface);
    col[ImGuiCol_TextDisabled] = with_alpha(on_surface_c, 0.38f);
    col[ImGuiCol_WindowBg] = imv(scheme_role_e::surface);
    col[ImGuiCol_ChildBg] = imv(scheme_role_e::surface_container_high);
    col[ImGuiCol_PopupBg] = imv(scheme_role_e::surface_container_high);
    col[ImGuiCol_Border] = imv(scheme_role_e::outline_variant);
    col[ImGuiCol_BorderShadow] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    col[ImGuiCol_FrameBg] = imv(scheme_role_e::surface_container_highest);
    col[ImGuiCol_FrameBgHovered] = mix(surface_container_highest_c, on_surface_c, 0.08f).to_imvec();
    col[ImGuiCol_FrameBgActive] = mix(surface_container_highest_c, on_surface_c, 0.12f).to_imvec();
    col[ImGuiCol_TitleBg] = imv(scheme_role_e::surface_container);
    col[ImGuiCol_TitleBgActive] = imv(scheme_role_e::surface_container_high);
    col[ImGuiCol_TitleBgCollapsed] = imv(scheme_role_e::surface_container_low);
    col[ImGuiCol_MenuBarBg] = imv(scheme_role_e::surface_container);
    col[ImGuiCol_ScrollbarBg] = { 0, 0, 0, 0 };
    col[ImGuiCol_ScrollbarGrab] = imv(scheme_role_e::outline_variant);
    col[ImGuiCol_ScrollbarGrabHovered] = imv(scheme_role_e::outline);
    col[ImGuiCol_ScrollbarGrabActive] = imv(scheme_role_e::primary);
    col[ImGuiCol_CheckMark] = imv(scheme_role_e::primary);
    col[ImGuiCol_SliderGrab] = imv(scheme_role_e::primary);
    col[ImGuiCol_SliderGrabActive] = imv(scheme_role_e::primary);
    col[ImGuiCol_Button] = imv(scheme_role_e::primary);
    col[ImGuiCol_ButtonHovered] = mix(primary_c, on_primary_c, 0.08f).to_imvec();
    col[ImGuiCol_ButtonActive] = mix(primary_c, on_primary_c, 0.12f).to_imvec();
    col[ImGuiCol_Header] = imv(scheme_role_e::secondary_container);
    col[ImGuiCol_HeaderHovered] = mix(secondary_container_c, on_secondary_container_c, 0.08f).to_imvec();
    col[ImGuiCol_HeaderActive] = mix(secondary_container_c, on_secondary_container_c, 0.12f).to_imvec();
    col[ImGuiCol_Separator] = imv(scheme_role_e::outline_variant);
    col[ImGuiCol_SeparatorHovered] = imv(scheme_role_e::outline);
    col[ImGuiCol_SeparatorActive] = imv(scheme_role_e::primary);
    col[ImGuiCol_ResizeGrip] = with_alpha(outline_variant_c, 0.6f);
    col[ImGuiCol_ResizeGripHovered] = imv(scheme_role_e::outline);
    col[ImGuiCol_ResizeGripActive] = imv(scheme_role_e::primary);
    col[ImGuiCol_Tab] = imv(scheme_role_e::surface_container);
    col[ImGuiCol_TabHovered] = mix(surface_container_c, on_surface_c, 0.08f).to_imvec();
    col[ImGuiCol_TabActive] = imv(scheme_role_e::primary_container);
    col[ImGuiCol_TabUnfocused] = imv(scheme_role_e::surface_container_low);
    col[ImGuiCol_TabUnfocusedActive] = imv(scheme_role_e::surface_container);
    col[ImGuiCol_PlotLines] = imv(scheme_role_e::tertiary);
    col[ImGuiCol_PlotLinesHovered] = imv(scheme_role_e::primary);
    col[ImGuiCol_PlotHistogram] = imv(scheme_role_e::primary);
    col[ImGuiCol_PlotHistogramHovered] = imv(scheme_role_e::tertiary);
    col[ImGuiCol_TableHeaderBg] = imv(scheme_role_e::surface_container_high);
    col[ImGuiCol_TableBorderStrong] = imv(scheme_role_e::outline);
    col[ImGuiCol_TableBorderLight] = imv(scheme_role_e::outline_variant);
    col[ImGuiCol_TableRowBg] = imv(scheme_role_e::surface);
    col[ImGuiCol_TableRowBgAlt] = imv(scheme_role_e::surface_container_low);
    col[ImGuiCol_TextSelectedBg] = with_alpha(primary_c, 0.24f);
    col[ImGuiCol_DragDropTarget] = imv(scheme_role_e::tertiary);
    col[ImGuiCol_NavHighlight] = imv(scheme_role_e::primary);
    col[ImGuiCol_NavWindowingHighlight] = with_alpha(primary_c, 0.7f);
    col[ImGuiCol_NavWindowingDimBg] = with_alpha(scrim_c, 0.32f);
    col[ImGuiCol_ModalWindowDimBg] = with_alpha(scrim_c, 0.32f);
}

} // namespace md3
