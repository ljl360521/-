#pragma once
#include "gui.h"
#include "il2cpp.h"
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <string>

struct Vector4 {
    float x, y, z, w;
};

inline Vector4 readVector4(uintptr_t addr) {
    if (!addr) return {};
    return { *(float*)addr, *(float*)(addr+4), *(float*)(addr+8), *(float*)(addr+12) };
}

inline void writeVector4(uintptr_t addr, const Vector4& v) {
    if (!addr) return;
    *(float*)addr = v.x; *(float*)(addr+4) = v.y;
    *(float*)(addr+8) = v.z; *(float*)(addr+12) = v.w;
}

class UtilityPanel {
public:
    UtilityPanel() = default;
    ~UtilityPanel();

    void drawTabContent();
    void update(float dt);
    void shutdown();

private:
    std::atomic<bool> m_threadRunning{false};
    std::atomic<bool> m_交互{false};
    std::atomic<bool> m_除雾{false};
    std::atomic<bool> m_夜视{false};
    std::atomic<bool> m_武器范围{false};
    std::atomic<bool> m_hackPaused{false};
    std::atomic<bool> m_needClearHackCache{false};
    std::atomic<bool> m_inCriticalSection{false};

    void ensureThread();
    void hackThreadFunc();

    // 夜视 - 备份上层对象的配置值
    struct NightVisionBackup {
        Vector4 firstViewColor, firstViewFade;
        Vector4 thirdViewColor, thirdViewFade;
        Vector4 outsideViewColor, outsideViewFade;
        Vector4 nvInsideTonemapping, nvOutsideTonemapping;
        bool saved = false;
    };
    NightVisionBackup m_nvBackup;
    std::atomic<bool> m_fadeDataFound{false};
    std::atomic<bool> m_nvNeedRestore{false};

    bool findAndBackupFadeData(IL2CPPRuntimeTool* tool);
    void applyNightVision(IL2CPPRuntimeTool* tool);
    void restoreNightVision(IL2CPPRuntimeTool* tool);

    // 武器范围调试
    struct WeaponLiveInfo {
        uintptr_t addr;
        float colliderDistance;
        float colliderRidus;
    };
    std::mutex m_weaponLiveMtx;
    std::vector<WeaponLiveInfo> m_weaponLiveObjs;
    std::atomic<int> m_weaponObjCount{0};

    // 场景检测
    bool m_sceneWasInvalid = false;
    void invalidateAllCaches();
};

extern UtilityPanel g_utilityPanel;