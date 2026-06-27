#ifndef BULLET_TRAIL_H
#define BULLET_TRAIL_H

#include "il2cpp.h"
#include "imgui.h"
#include <vector>
#include <unordered_map>

struct BulletTrail {
    // 子弹第一次出现的位置（射击起点）
    float startX, startY, startZ;
    // 子弹最后已知位置（射击终点）
    float endX, endY, endZ;

    float lifetime;       // 子弹消失后的倒计时
    float maxLifetime;
    int32_t itemId;
    bool bullet_gone;     // 子弹已从字典中消失，开始倒计时
    int miss_frames;      // 连续未在字典中出现的帧数
};

class BulletTrailSystem {
private:
    // 活跃轨迹：itemId -> trail
    std::unordered_map<int32_t, BulletTrail> active_trails;
    // 已完成轨迹（子弹消失后等待淡出）
    std::vector<BulletTrail> fading_trails;

    float trail_duration;
    float fade_start;
    bool enabled;

    ImU32 trail_color;
    ImU32 endpoint_color;
    float trail_thickness;
    float endpoint_radius;

    const MethodInfo* camera_world_to_screen;
    float screen_width, screen_height;

    struct Vec3 { float x, y, z; };
    bool w2s(void* cam, Vec3 w, ImVec2& out);

public:
    BulletTrailSystem();

    void init(const MethodInfo* cam_w2s);

    // 每帧对发现的子弹调用：更新或创建轨迹
    // 如果是第一次见到这个 itemId，记录位置为起点
    // 如果已经在跟踪，更新终点位置
    void trackBullet(int32_t itemId, float x, float y, float z);

    // 每帧结束时调用：把本帧没出现的活跃子弹标记为消失
    void endFrame(float dt);

    // 绘制
    void draw(void* cam, float scrW, float scrH);

    // 面板
    void drawPanel();

    void clear();
    bool isEnabled() const { return enabled; }

    // 每帧开始时调用，标记新一帧
    void beginFrame();

private:
    // 本帧看到的子弹ID集合
    std::vector<int32_t> seen_this_frame;
};

extern BulletTrailSystem g_bullet_trail;

#endif