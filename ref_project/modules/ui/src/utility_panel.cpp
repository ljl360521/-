#include "utility_panel.h"

UtilityPanel g_utilityPanel;

// =====================================================================
// 夜视功能
// =====================================================================

bool UtilityPanel::findAndBackupFadeData(IL2CPPRuntimeTool* tool) {
    if (!tool) return false;
    
    m_fadeDataFound.store(false);
    m_nvBackup.saved = false;

    // 获取 GraphicSettings 实例
    auto GraphicSettingsClass = tool->findClass("", "GraphicSettings");
    if (!GraphicSettingsClass) return false;

    // 调用静态方法 get_Instance() 获取实例
    auto getInstanceMethod = tool->findMethod(GraphicSettingsClass, "get_Instance", 0);
    if (!getInstanceMethod) return false;

    Il2CppObject_API* graphicSettingsInst = tool->invokeMethod(getInstanceMethod, nullptr);
    if (!graphicSettingsInst) return false;

    // 从 GraphicSettings 读取 RenderProfile (offset 0x80)
    Il2CppObject_API* renderProfile = *(Il2CppObject_API**)((uintptr_t)graphicSettingsInst + 0x80);
    if (!renderProfile || (uintptr_t)renderProfile < 0x10000) return false;

    // 从 RenderProfile 读取 ViewModeFadeData (offset 0x18)
    Il2CppObject_API* viewModeFadeData = *(Il2CppObject_API**)((uintptr_t)renderProfile + 0x18);
    if (!viewModeFadeData || (uintptr_t)viewModeFadeData < 0x10000) return false;

    uintptr_t base = (uintptr_t)viewModeFadeData;

    // 备份所有需要的数据 (selfEmissionData 结构在每个字段)
    // firstViewData @ 0x10, thirdViewData @ 0x18, outsideViewData @ 0x20
    uintptr_t fv = *(uintptr_t*)(base + 0x10);
    uintptr_t tv = *(uintptr_t*)(base + 0x18);
    uintptr_t ov = *(uintptr_t*)(base + 0x20);
    
    if (!fv || !tv || !ov || fv < 0x10000 || tv < 0x10000 || ov < 0x10000) return false;

    // 备份颜色和淡化值 (color @ +0x10, fade @ +0x20)
    m_nvBackup.firstViewColor = readVector4(fv + 0x10);
    m_nvBackup.firstViewFade = readVector4(fv + 0x20);
    m_nvBackup.thirdViewColor = readVector4(tv + 0x10);
    m_nvBackup.thirdViewFade = readVector4(tv + 0x20);
    m_nvBackup.outsideViewColor = readVector4(ov + 0x10);
    m_nvBackup.outsideViewFade = readVector4(ov + 0x20);

    // 备份 Tonemapping
    m_nvBackup.nvInsideTonemapping = readVector4(base + 0x58);
    m_nvBackup.nvOutsideTonemapping = readVector4(base + 0x68);

    m_nvBackup.saved = true;
    m_fadeDataFound.store(true);
    return true;
}

void UtilityPanel::applyNightVision(IL2CPPRuntimeTool* tool) {
    if (!m_fadeDataFound.load() || !tool || !tool->isSceneValid()) return;

    auto GraphicSettingsClass = tool->findClass("", "GraphicSettings");
    if (!GraphicSettingsClass) return;

    auto getInstanceMethod = tool->findMethod(GraphicSettingsClass, "get_Instance", 0);
    if (!getInstanceMethod) return;

    Il2CppObject_API* graphicSettingsInst = tool->invokeMethod(getInstanceMethod, nullptr);
    if (!graphicSettingsInst) return;

    Il2CppObject_API* renderProfile = *(Il2CppObject_API**)((uintptr_t)graphicSettingsInst + 0x80);
    if (!renderProfile || (uintptr_t)renderProfile < 0x10000) return;

    Il2CppObject_API* viewModeFadeData = *(Il2CppObject_API**)((uintptr_t)renderProfile + 0x18);
    if (!viewModeFadeData || (uintptr_t)viewModeFadeData < 0x10000) return;

    uintptr_t base = (uintptr_t)viewModeFadeData;

    uintptr_t fv = *(uintptr_t*)(base + 0x10);
    uintptr_t tv = *(uintptr_t*)(base + 0x18);
    uintptr_t ov = *(uintptr_t*)(base + 0x20);

    if (!fv || !tv || !ov || fv < 0x10000 || tv < 0x10000 || ov < 0x10000) return;

    // 应用夜视参数
    Vector4 color = {3.5f, 3.0f, 4.0f, 1.0f};
    Vector4 fade1 = {1000.0f, 0.1f, 3.0f, 0.01f};
    Vector4 fade2 = {1000.0f, 0.5f, 0.7f, 0.01f};

    writeVector4(fv + 0x10, color);
    writeVector4(fv + 0x20, fade1);
    writeVector4(tv + 0x10, color);
    writeVector4(tv + 0x20, fade1);
    writeVector4(ov + 0x10, color);
    writeVector4(ov + 0x20, fade2);
}

void UtilityPanel::restoreNightVision(IL2CPPRuntimeTool* tool) {
    if (!m_nvBackup.saved || !tool || !tool->isSceneValid()) return;

    auto GraphicSettingsClass = tool->findClass("", "GraphicSettings");
    if (!GraphicSettingsClass) return;

    auto getInstanceMethod = tool->findMethod(GraphicSettingsClass, "get_Instance", 0);
    if (!getInstanceMethod) return;

    Il2CppObject_API* graphicSettingsInst = tool->invokeMethod(getInstanceMethod, nullptr);
    if (!graphicSettingsInst) return;

    Il2CppObject_API* renderProfile = *(Il2CppObject_API**)((uintptr_t)graphicSettingsInst + 0x80);
    if (!renderProfile || (uintptr_t)renderProfile < 0x10000) return;

    Il2CppObject_API* viewModeFadeData = *(Il2CppObject_API**)((uintptr_t)renderProfile + 0x18);
    if (!viewModeFadeData || (uintptr_t)viewModeFadeData < 0x10000) return;

    uintptr_t base = (uintptr_t)viewModeFadeData;
    uintptr_t fv = *(uintptr_t*)(base + 0x10);
    uintptr_t tv = *(uintptr_t*)(base + 0x18);
    uintptr_t ov = *(uintptr_t*)(base + 0x20);

    if (!fv || !tv || !ov || fv < 0x10000 || tv < 0x10000 || ov < 0x10000) return;

    // 恢复原始值
    writeVector4(fv + 0x10, m_nvBackup.firstViewColor);
    writeVector4(fv + 0x20, m_nvBackup.firstViewFade);
    writeVector4(tv + 0x10, m_nvBackup.thirdViewColor);
    writeVector4(tv + 0x20, m_nvBackup.thirdViewFade);
    writeVector4(ov + 0x10, m_nvBackup.outsideViewColor);
    writeVector4(ov + 0x20, m_nvBackup.outsideViewFade);
}

// =====================================================================
// 场景切换
// =====================================================================

void UtilityPanel::invalidateAllCaches() {
    m_hackPaused.store(true);
    
    // 等待 hack 线程确认已暂停
    while (m_inCriticalSection.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    m_fadeDataFound.store(false);
    m_nvBackup.saved = false;
    m_nvNeedRestore.store(false);  // 场景切换时取消恢复请求
    m_needClearHackCache.store(true);
    {
        std::lock_guard<std::mutex> lock(m_weaponLiveMtx);
        m_weaponLiveObjs.clear();
    }
    m_weaponObjCount.store(0);
}

void UtilityPanel::update(float dt) {
    if (!g_il2cpp_tool || !g_il2cpp_tool->isInitialized()) return;
    bool valid = g_il2cpp_tool->isSceneValid();
    if (!valid) {
        if (!m_sceneWasInvalid) invalidateAllCaches();
        m_sceneWasInvalid = true;
        return;
    }
    if (m_sceneWasInvalid) {
        invalidateAllCaches();
        m_hackPaused.store(false);
        m_sceneWasInvalid = false;
    }
}

// =====================================================================
// hack 线程
// =====================================================================

UtilityPanel::~UtilityPanel() { shutdown(); }
void UtilityPanel::shutdown() { m_threadRunning.store(false); }

void UtilityPanel::hackThreadFunc() {
    auto* tool = g_il2cpp_tool.get();
    if (!tool) return;
    Il2CppThread* t = tool->attachCurrentThread();

    std::vector<Il2CppObject_API*> interactObjs, fogObjs, weaponObjs;
    int weaponDebugTimer = 0;

    while (m_threadRunning.load()) {
        // 清空通知
        if (m_needClearHackCache.exchange(false)) {
            interactObjs.clear();
            fogObjs.clear();
            weaponObjs.clear();
            m_fadeDataFound.store(false);
            m_nvBackup.saved = false;
        }

        // 暂停
        if (m_hackPaused.load()) {
            m_inCriticalSection.store(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // 线程自检场景有效性
        if (!tool->isSceneValid()) {
            interactObjs.clear();
            fogObjs.clear();
            weaponObjs.clear();
            m_fadeDataFound.store(false);
            m_nvBackup.saved = false;
            m_hackPaused.store(true);
            LOGI("[hackThread] 场景无效，暂停");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        
        m_inCriticalSection.store(true);
        if (!tool->isSceneValid()) {
            m_inCriticalSection.store(false);
            continue;
        }

        // 夜视恢复
        if (m_nvNeedRestore.exchange(false)) {
            if (tool->isSceneValid()) {
                restoreNightVision(tool);
            }
            m_fadeDataFound.store(false);
            m_nvBackup.saved = false;
        }

        // —— 秒交互 ——
        if (m_交互.load()) {
            if (interactObjs.empty())
                interactObjs = tool->findObjectsByType("", "UGC2ObjectInteractUI");
            for (auto it = interactObjs.begin(); it != interactObjs.end(); ) {
                if (!*it || (uintptr_t)*it < 0x10000) { it = interactObjs.erase(it); continue; }
                *(float*)((uintptr_t)*it + 0x104) = 0.f;
                ++it;
            }
        } else if (!interactObjs.empty()) {
            interactObjs.clear();
        }

        // —— 除雾 ——
        if (m_除雾.load()) {
            if (fogObjs.empty())
                fogObjs = tool->findObjectsByType("PuzzleBox", "FogRenderHelper");
            for (auto it = fogObjs.begin(); it != fogObjs.end(); ) {
                if (!*it || (uintptr_t)*it < 0x10000) { it = fogObjs.erase(it); continue; }
                *(float*)((uintptr_t)*it + 0x3C) = 0.f;
                ++it;
            }
        } else if (!fogObjs.empty()) {
            fogObjs.clear();
        }

        // —— 夜视 ——
        if (m_夜视.load()) {
            if (!m_fadeDataFound.load()) 
                findAndBackupFadeData(tool);
            if (m_fadeDataFound.load()) 
                applyNightVision(tool);
        }

        // —— 武器范围 ——
        if (m_武器范围.load()) {
            if (weaponObjs.empty()) {
                auto* weaponClass = tool->findClass("", "CloseCombatAttackConfig");
                if (weaponClass)
                    weaponObjs = tool->findObjectsByType(weaponClass);
            }

            for (auto it = weaponObjs.begin(); it != weaponObjs.end(); ) {
                if (!*it || (uintptr_t)*it < 0x10000) { it = weaponObjs.erase(it); continue; }
                uintptr_t addr = (uintptr_t)*it;
                *(float*)(addr + 0x2C) = 10.f;
                *(float*)(addr + 0x30) = 10.f;
                ++it;
            }
            m_weaponObjCount.store((int)weaponObjs.size());

            // 每秒更新调试显示
            weaponDebugTimer++;
            if (weaponDebugTimer >= 60) {
                std::vector<WeaponLiveInfo> debugList;
                for (auto* obj : weaponObjs) {
                    if (!obj || (uintptr_t)obj < 0x10000) continue;
                    uintptr_t addr = (uintptr_t)obj;
                    debugList.push_back({addr, *(float*)(addr + 0x2C), *(float*)(addr + 0x30)});
                }
                {
                    std::lock_guard<std::mutex> lock(m_weaponLiveMtx);
                    m_weaponLiveObjs = std::move(debugList);
                }
                weaponDebugTimer = 0;
            }
        } else {
            if (!weaponObjs.empty()) {
                weaponObjs.clear();
                m_weaponObjCount.store(0);
                std::lock_guard<std::mutex> lock(m_weaponLiveMtx);
                m_weaponLiveObjs.clear();
            }
            weaponDebugTimer = 0;
        }
            
        m_inCriticalSection.store(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    tool->detachThread(t);
}

void UtilityPanel::ensureThread() {
    if (m_threadRunning.load()) return;
    m_threadRunning.store(true);
    m_hackPaused.store(false);
    std::thread([this]() { hackThreadFunc(); }).detach();
}

// =====================================================================
// UI
// =====================================================================

void UtilityPanel::drawTabContent() {
    bool v1 = m_交互.load();
    if (ImGui::Checkbox("秒交互", &v1)) {
        m_交互.store(v1);
        if (v1) ensureThread();
    }

    bool v2 = m_除雾.load();
    if (ImGui::Checkbox("除雾", &v2)) {
        m_除雾.store(v2);
        if (v2) ensureThread();
    }

    bool v3 = m_夜视.load();
    if (ImGui::Checkbox("夜视", &v3)) {
        m_夜视.store(v3);
        if (v3) ensureThread();
        else {
            // 只在场景有效时才请求恢复
            if (g_il2cpp_tool && g_il2cpp_tool->isSceneValid())
                m_nvNeedRestore.store(true);
            else {
                m_fadeDataFound.store(false);
                m_nvBackup.saved = false;
            }
        }
    }
    if (v3) {
        ImGui::SameLine();
        ImGui::TextColored(
            m_fadeDataFound.load() ? ImVec4(0,1,0,1) : ImVec4(1,1,0,1),
            m_fadeDataFound.load() ? "已生效" : "查找中...");
    }

}