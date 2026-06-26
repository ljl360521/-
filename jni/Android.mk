LOCAL_PATH := $(call my-dir)

# 设置 ImGui 相关路径
IMGUI_ROOT := $(LOCAL_PATH)/ImGui
IMGUI_INCLUDE := $(IMGUI_ROOT)
IMGUI_LIB_PATH := $(IMGUI_ROOT)/imgui/$(TARGET_ARCH_ABI)

include $(CLEAR_VARS)

LOCAL_MODULE := hook
LOCAL_CPP_EXTENSION := .cpp .cc

# 编译标志
LOCAL_CFLAGS := -w -s -Wno-error=format-security -fvisibility=hidden -fpermissive -fexceptions
LOCAL_CPPFLAGS := -w -s -Wno-error=format-security -fvisibility=hidden -std=c++17
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing -fpermissive -Wall -fexceptions

# 包含路径
LOCAL_C_INCLUDES += $(LOCAL_PATH)/App
LOCAL_C_INCLUDES += $(LOCAL_PATH)/Dobby/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/xdl/include
LOCAL_C_INCLUDES += $(IMGUI_INCLUDE)
LOCAL_C_INCLUDES += $(IMGUI_INCLUDE)/imgui

# 源文件列表
C_FILE_LIST := $(wildcard $(LOCAL_PATH)/xdl/src/*.c)
FILE_LIST := $(filter-out %.bak, $(wildcard $(LOCAL_PATH)/App/*.cpp))
FILE_LIST += $(filter-out %.bak, $(wildcard $(LOCAL_PATH)/App/*.cc))
FILE_LIST += $(filter-out %.bak, $(wildcard $(LOCAL_PATH)/App/*.c))
LOCAL_SRC_FILES := $(C_FILE_LIST:$(LOCAL_PATH)/%=%)
LOCAL_SRC_FILES += $(FILE_LIST:$(LOCAL_PATH)/%=%)

# ImGui 的 .cpp (imgui + impl_android + impl_opengl3) 已预编译进 libimgui.a,
# 源码树只保留头文件, 此处无需额外源文件。

# 链接库
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv1_CM -lGLESv2 -lGLESv3 -lm -ldl -lz
LOCAL_STATIC_LIBRARIES := dobby imgui

include $(BUILD_SHARED_LIBRARY)

# 导入预编译的 Dobby 库
include $(CLEAR_VARS)
LOCAL_MODULE := dobby
LOCAL_SRC_FILES := Dobby/libdobby.a
include $(PREBUILT_STATIC_LIBRARY)

# 导入预编译的 ImGui 库
include $(CLEAR_VARS)
LOCAL_MODULE := imgui
LOCAL_SRC_FILES := ImGui/imgui/$(TARGET_ARCH_ABI)/libimgui.a
include $(PREBUILT_STATIC_LIBRARY)
