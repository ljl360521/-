LOCAL_PATH := $(call my-dir)

# 设置 ImGui 相关路径
IMGUI_ROOT := $(LOCAL_PATH)/ImGui
IMGUI_INCLUDE := $(IMGUI_ROOT)
IMGUI_LIB_PATH := $(IMGUI_ROOT)/imgui/$(TARGET_ARCH_ABI)

include $(CLEAR_VARS)

LOCAL_MODULE := hook
LOCAL_CPP_EXTENSION := .cpp .cc

# 编译标志
LOCAL_CFLAGS := -Wno-error=format-security -fvisibility=hidden -fexceptions
LOCAL_CPPFLAGS := -Wno-error=format-security -fvisibility=hidden -std=c++17
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing -fpermissive -Wall -fexceptions

# 包含路径
LOCAL_C_INCLUDES += $(LOCAL_PATH)/App
LOCAL_C_INCLUDES += $(LOCAL_PATH)/xdl/include
LOCAL_C_INCLUDES += $(IMGUI_INCLUDE)
LOCAL_C_INCLUDES += $(IMGUI_INCLUDE)/imgui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/t3sdk
LOCAL_C_INCLUDES += $(LOCAL_PATH)/res
LOCAL_C_INCLUDES += $(LOCAL_PATH)/res/music
LOCAL_C_INCLUDES += $(LOCAL_PATH)/res/music/nlohmann
LOCAL_C_INCLUDES += $(LOCAL_PATH)/res/music/curl/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/thirdparty/pugixml

# 源文件列表
C_FILE_LIST := $(wildcard $(LOCAL_PATH)/xdl/src/*.c)
FILE_LIST := $(filter-out %.bak, $(wildcard $(LOCAL_PATH)/App/*.cpp))
FILE_LIST += $(filter-out %.bak, $(wildcard $(LOCAL_PATH)/App/*.cc))
FILE_LIST += $(filter-out %.bak, $(wildcard $(LOCAL_PATH)/App/*.c))
LOCAL_SRC_FILES := $(C_FILE_LIST:$(LOCAL_PATH)/%=%)
LOCAL_SRC_FILES += $(FILE_LIST:$(LOCAL_PATH)/%=%)
RES_FILE_LIST := $(filter-out %.bak, $(wildcard $(LOCAL_PATH)/res/*.cpp))
LOCAL_SRC_FILES += $(RES_FILE_LIST:$(LOCAL_PATH)/%=%)
MUSIC_FILE_LIST := $(filter-out %.bak, $(wildcard $(LOCAL_PATH)/res/music/*.cpp))
LOCAL_SRC_FILES += $(MUSIC_FILE_LIST:$(LOCAL_PATH)/%=%)
LOCAL_SRC_FILES += t3sdk/t3sdk.cpp
LOCAL_SRC_FILES += thirdparty/pugixml/pugixml.cpp

# 添加 ImGui 源文件
IMGUI_SOURCES := $(wildcard $(IMGUI_ROOT)/*.cpp)
IMGUI_SOURCES += $(wildcard $(IMGUI_ROOT)/backends/imgui_impl_android.cpp)
IMGUI_SOURCES += $(wildcard $(IMGUI_ROOT)/backends/imgui_impl_opengl3.cpp)
LOCAL_SRC_FILES += $(IMGUI_SOURCES:$(LOCAL_PATH)/%=%)

# 链接库
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv1_CM -lGLESv2 -lGLESv3 -lm -ldl -lz
LOCAL_STATIC_LIBRARIES := imgui musiccurl musicssl musiccrypto

include $(BUILD_SHARED_LIBRARY)


# 导入音乐播放所需 curl/ssl/crypto 预编译库
include $(CLEAR_VARS)
LOCAL_MODULE := musiccurl
LOCAL_SRC_FILES := res/music/curl/libcurl.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := musicssl
LOCAL_SRC_FILES := res/music/curl/libssl.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := musiccrypto
LOCAL_SRC_FILES := res/music/curl/libcrypto.a
include $(PREBUILT_STATIC_LIBRARY)

# 导入预编译的 ImGui 库
include $(CLEAR_VARS)
LOCAL_MODULE := imgui
LOCAL_SRC_FILES := ImGui/imgui/$(TARGET_ARCH_ABI)/libimgui.a
include $(PREBUILT_STATIC_LIBRARY)
