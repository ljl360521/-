cmake_minimum_required(VERSION 3.22)
project(hook LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fexceptions -fpermissive")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -fpermissive -frtti")

# 警告抑制
add_compile_options(
    -Wno-unused-parameter -Wno-unused-variable -Wno-format-security
    -Wno-format -Wno-write-strings -Wno-sign-compare
    -Wno-error=c++11-narrowing
)

# ============================================================================
# 编译模式：-DRELEASE_MODE=ON 为发布版，默认调试版
# ============================================================================
option(RELEASE_MODE "Build release variant with obfuscation" OFF)

if(RELEASE_MODE)
    add_definitions(-DRELEASE_MODE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -fvisibility=hidden -s")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fvisibility=hidden -fvisibility-inlines-hidden -s")
    add_definitions(-D_LIBCPP_HIDE_FROM_ABI=1 -D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall")
endif()

# ============================================================================
# 预编译静态库
# ============================================================================
set(LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)
set(MODS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/modules)

# dobby - libs/Dobby/lib/${ANDROID_ABI}/libdobby.a
add_library(dobby STATIC IMPORTED)
set_target_properties(dobby PROPERTIES
    IMPORTED_LOCATION ${LIBS_DIR}/Dobby/lib/${ANDROID_ABI}/libdobby.a
    INTERFACE_INCLUDE_DIRECTORIES ${LIBS_DIR}/Dobby/include)

# imgui - libs/ImGui/lib/${ANDROID_ABI}/libimgui.a
add_library(imgui STATIC IMPORTED)
set_target_properties(imgui PROPERTIES
    IMPORTED_LOCATION ${LIBS_DIR}/ImGui/lib/${ANDROID_ABI}/libimgui.a
    INTERFACE_INCLUDE_DIRECTORIES ${LIBS_DIR}/ImGui/include)

# openssl - libs/openssl/lib/${ANDROID_ABI}/libcrypto.a & libssl.a
add_library(crypto STATIC IMPORTED)
set_target_properties(crypto PROPERTIES
    IMPORTED_LOCATION ${LIBS_DIR}/openssl/lib/${ANDROID_ABI}/libcrypto.a
    INTERFACE_INCLUDE_DIRECTORIES ${LIBS_DIR}/openssl/include)

add_library(ssl STATIC IMPORTED)
set_target_properties(ssl PROPERTIES
    IMPORTED_LOCATION ${LIBS_DIR}/openssl/lib/${ANDROID_ABI}/libssl.a
    INTERFACE_INCLUDE_DIRECTORIES ${LIBS_DIR}/openssl/include)

# curl - libs/curl/lib/${ANDROID_ABI}/libcurl.a
add_library(curl STATIC IMPORTED)
set_target_properties(curl PROPERTIES
    IMPORTED_LOCATION ${LIBS_DIR}/curl/lib/${ANDROID_ABI}/libcurl.a
    INTERFACE_INCLUDE_DIRECTORIES ${LIBS_DIR}/curl/include)

# ============================================================================
# pugixml 静态库（不混淆）
# ============================================================================
add_library(pugixml STATIC ${MODS_DIR}/pugixml/src/pugixml.cpp)
target_include_directories(pugixml PUBLIC ${MODS_DIR}/pugixml/include)
target_compile_options(pugixml PRIVATE -g -O0)

# ============================================================================
# 收集源文件
# ============================================================================
file(GLOB C_SRC
    ${MODS_DIR}/xdl/src/*.c
    ${MODS_DIR}/xhook/src/*.c
    ${MODS_DIR}/lua/src/*.c
)
file(GLOB LUA_SRC ${MODS_DIR}/lua/src/*.c)
list(FILTER LUA_SRC EXCLUDE REGEX "luac\\.c$")

file(GLOB CPP_SRC
    ${MODS_DIR}/common/src/*.cpp
    ${MODS_DIR}/config/src/*.cpp
    ${MODS_DIR}/log/src/*.cpp
    ${MODS_DIR}/yz/src/*.cpp
    ${MODS_DIR}/ui/src/*.cpp
    ${MODS_DIR}/il2cpp/src/*.cpp
    ${MODS_DIR}/memory/src/*.cpp
    ${MODS_DIR}/imguiexpand/src/*.cpp
    ${MODS_DIR}/data/src/*.cpp
    ${MODS_DIR}/KittyMemory/src/*.cpp
    ${MODS_DIR}/unitymatrix/src/*.cpp
    ${MODS_DIR}/native/src/*.cpp
)

# ============================================================================
# 主模块
# ============================================================================
add_library(hook SHARED ${C_SRC} ${LUA_SRC} ${CPP_SRC})

target_include_directories(hook PRIVATE
    ${LIBS_DIR}/openssl/include
    ${LIBS_DIR}/curl/include
    ${LIBS_DIR}/ImGui/include
    ${LIBS_DIR}/Dobby/include
    ${MODS_DIR}/common/include
    ${MODS_DIR}/config/include
    ${MODS_DIR}/il2cpp/include
    ${MODS_DIR}/log/include
    ${MODS_DIR}/memory/include
    ${MODS_DIR}/data/include
    ${MODS_DIR}/yz/include
    ${MODS_DIR}/ui/include
    ${MODS_DIR}/imguiexpand/include
    ${MODS_DIR}/xdl/include
    ${MODS_DIR}/xhook/include
    ${MODS_DIR}/KittyMemory/include
    ${MODS_DIR}/unitymatrix/include
    ${MODS_DIR}/pugixml/include
    ${MODS_DIR}/native/include
    ${MODS_DIR}/lua/include
    ${MODS_DIR}/native/java
)

target_link_libraries(hook
    dobby imgui curl ssl crypto pugixml
    log android EGL GLESv3 z dl vulkan
    -static-libstdc++
)

if(RELEASE_MODE)
    set_target_properties(hook PROPERTIES LINK_FLAGS
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.map -Wl,--exclude-libs,ALL")
endif()