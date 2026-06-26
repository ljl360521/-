#include "bullet_trail.h"
#include <cmath>
#include <algorithm>

BulletTrailSystem g_bullet_trail;

BulletTrailSystem::BulletTrailSystem()
    : trail_duration(5.0f),
      fade_start(1.5f),
      enabled(true),
      trail_color(IM_COL32(255, 80, 80, 220)),
      endpoint_color(IM_COL32(255, 50, 50, 255)),
      trail_thickness(2.0f),
      endpoint_radius(5.0f),
      camera_world_to_screen(nullptr),
      screen_width(1920), screen_height(1080) {}

void BulletTrailSystem::init(const MethodInfo* cam_w2s) {
    camera_world_to_screen = cam_w2s;
}

bool BulletTrailSystem::w2s(void* cam, Vec3 w, ImVec2& out) {
    if (!cam || !camera_world_to_screen) return false;
    typedef Vec3(*Fn)(void*, Vec3, const MethodInfo*);
    Vec3 s = ((Fn)camera_world_to_screen->methodPointer)(cam, w, camera_world_to_screen);
    if (s.z < 0 || std::isnan(s.x) || std::isnan(s.y)) return false;
    out.x = s.x;
    out.y = screen_height - s.y;
    return true;
}

void BulletTrailSystem::beginFrame() {
    seen_this_frame.clear();
}

void BulletTrailSystem::trackBullet(int32_t itemId, float x, float y, float z) {
    if (!enabled) return;

    seen_this_frame.push_back(itemId);

    auto it = active_trails.find(itemId);
    if (it == active_trails.end()) {
        // 第一次发现这颗子弹 → 记录起点
        BulletTrail t;
        t.startX = x; t.startY = y; t.startZ = z;
        t.endX = x;   t.endY = y;   t.endZ = z;
        t.lifetime = trail_duration;
        t.maxLifetime = trail_duration;
        t.itemId = itemId;
        t.bullet_gone = false;
        t.miss_frames = 0;
        active_trails[itemId] = t;
    } else {
        // 已在跟踪 → 更新终点为子弹最新位置
        it->second.endX = x;
        it->second.endY = y;
        it->second.endZ = z;
        it->second.miss_frames = 0;
    }
}

void BulletTrailSystem::endFrame(float dt) {
    if (!enabled) return;

    // 检查哪些活跃子弹本帧没出现 → 子弹已消失
    std::vector<int32_t> to_remove;
    for (auto& pair : active_trails) {
        bool seen = false;
        for (auto id : seen_this_frame) {
            if (id == pair.first) { seen = true; break; }
        }

        if (!seen) {
            pair.second.miss_frames++;
            // 连续3帧没见到 → 确认消失（容错抖动）
            if (pair.second.miss_frames >= 3) {
                pair.second.bullet_gone = true;
                pair.second.lifetime = trail_duration;
                fading_trails.push_back(pair.second);
                to_remove.push_back(pair.first);
            }
        }
    }

    for (auto id : to_remove) {
        active_trails.erase(id);
    }

    // 更新淡出轨迹计时器
    for (auto& t : fading_trails) {
        t.lifetime -= dt;
    }

    fading_trails.erase(
        std::remove_if(fading_trails.begin(), fading_trails.end(),
            [](const BulletTrail& t) { return t.lifetime <= 0; }),
        fading_trails.end()
    );
}

void BulletTrailSystem::draw(void* cam, float scrW, float scrH) {
    if (!enabled) return;

    screen_width = scrW;
    screen_height = scrH;

    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    // 绘制函数：同时处理活跃轨迹和淡出轨迹
    auto drawTrail = [&](const BulletTrail& t, float alpha) {
        uint8_t a = (uint8_t)(alpha * 220);
        uint8_t a_end = (uint8_t)(alpha * 255);

        ImU32 lineCol = (trail_color & 0x00FFFFFF) | ((ImU32)a << 24);
        ImU32 endCol  = (endpoint_color & 0x00FFFFFF) | ((ImU32)a_end << 24);
        ImU32 shadow  = IM_COL32(0, 0, 0, (uint8_t)(alpha * 120));

        Vec3 startW = {t.startX, t.startY, t.startZ};
        Vec3 endW   = {t.endX, t.endY, t.endZ};

        ImVec2 startS, endS;
        bool sv = w2s(cam, startW, startS);
        bool ev = w2s(cam, endW, endS);

        if (sv && ev) {
            dl->AddLine(startS, endS, shadow, trail_thickness + 1.5f);
            dl->AddLine(startS, endS, lineCol, trail_thickness);
        }

        if (ev) {
            dl->AddCircle(endS, endpoint_radius, endCol, 12, 2.0f);
            float cr = endpoint_radius + 4.0f;
            dl->AddLine({endS.x - cr, endS.y}, {endS.x + cr, endS.y}, endCol, 1.5f);
            dl->AddLine({endS.x, endS.y - cr}, {endS.x, endS.y + cr}, endCol, 1.5f);
            dl->AddCircleFilled(endS, 2.0f, endCol);
        }

        if (sv) {
            dl->AddCircleFilled(startS, 3.0f, lineCol);
        }
    };

    // 活跃轨迹（子弹还在飞，全亮）
    for (auto& pair : active_trails) {
        drawTrail(pair.second, 1.0f);
    }

    // 淡出轨迹
    for (auto& t : fading_trails) {
        float alpha = 1.0f;
        if (t.lifetime < fade_start) {
            alpha = t.lifetime / fade_start;
            if (alpha < 0) alpha = 0;
        }
        drawTrail(t, alpha);
    }
}

void BulletTrailSystem::drawPanel() {
    ImGui::Checkbox("子弹轨迹##bt", &enabled);
    if (!enabled) return;

    ImGui::Text("活跃: %zu  淡出: %zu", active_trails.size(), fading_trails.size());
    ImGui::SliderFloat("持续时间##bt", &trail_duration, 1.0f, 15.0f, "%.1f s");
    ImGui::SliderFloat("淡出时间##bt", &fade_start, 0.5f, 5.0f, "%.1f s");
    ImGui::SliderFloat("线条粗细##bt", &trail_thickness, 1.0f, 5.0f, "%.1f");
    ImGui::SliderFloat("终点标记##bt", &endpoint_radius, 3.0f, 15.0f, "%.1f");

    if (ImGui::Button("清除##bt")) clear();
}

void BulletTrailSystem::clear() {
    active_trails.clear();
    fading_trails.clear();
    seen_this_frame.clear();
}