#include "gui.h"
#include "aura_ui.hpp"
#include "native_register_vk.h"

void 绘制控件() {
    // 初始化 LOGO 纹理（仅首次调用时创建 Vulkan 纹理）
    aura_ui::init();

    // 渲染主窗口（AuraNexus UI，三标签页布局）
    aura_ui::render_window();

    // 渲染灵动岛（窗口上方胶囊，点击切换窗口显示/隐藏）
    aura_ui::render_dynamic_island();
}
