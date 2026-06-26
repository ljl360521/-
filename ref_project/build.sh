#!/bin/bash
set -e

ABIS=("arm64-v8a")
API=24
STL=c++_static
NDK="${ANDROID_NDK_ROOT:?请设置 ANDROID_NDK_ROOT}"
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

SDK="${ANDROID_SDK_ROOT:?请设置 ANDROID_SDK_ROOT}"
BUILD_TOOLS="$SDK/build-tools/34.0.0"
ANDROID_JAR="$SDK/platforms/android-34/android.jar"

JAVA_SRC_DIR="$SRC_DIR/modules/native/java"
DEX_HEADER="$JAVA_SRC_DIR/classes_dex.h"
BUILD_ROOT="$SRC_DIR/build"
OUTPUT_DIR="$SRC_DIR/out"

# Java → DEX → C header
compile_dex_header() {
    local tmp="$BUILD_ROOT/java" classes="$BUILD_ROOT/java/classes"
    rm -rf "$tmp" && mkdir -p "$classes"

    local java_files
    mapfile -d '' java_files < <(find "$JAVA_SRC_DIR" -name '*.java' -print0)
    [[ ${#java_files[@]} -eq 0 ]] && { echo "无 .java 文件，跳过"; return 0; }

    javac --release 11 -classpath "$ANDROID_JAR" -d "$classes" "${java_files[@]}"

    local class_files
    mapfile -d '' class_files < <(find "$classes" -name '*.class' -print0)
    "$BUILD_TOOLS/d8" --min-api "$API" --output "$tmp" "${class_files[@]}"

    # 生成头文件
    ( cd "$tmp" && xxd -i classes.dex > classes_dex_raw.h )
    printf '// Auto-generated — do not edit\n#pragma once\n#include <cstddef>\n\n' > "$DEX_HEADER"
    cat "$tmp/classes_dex_raw.h" >> "$DEX_HEADER"
    echo "DEX header: $DEX_HEADER ($(wc -c < "$tmp/classes.dex") bytes)"
}

# 编译单个架构
build_abi() {
    local abi=$1 mode=$2 cmake_flag=$3
    local tag="${mode}_${abi//[^a-zA-Z0-9]/_}"
    local build_dir="$BUILD_ROOT/cmake/$tag"
    local out_dir="$OUTPUT_DIR/$mode/$abi"

    echo "── $mode ($abi)"
    cmake -B "$build_dir" -S "$SRC_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-$API \
        -DANDROID_STL=$STL \
        $cmake_flag
    cmake --build "$build_dir" -j"$(nproc)"

    mkdir -p "$out_dir"
    local so=$(find "$build_dir" -name "*.so" -type f | head -1)
    [[ -z "$so" ]] && { echo "错误: 未找到 .so"; return 1; }
    cp "$so" "$out_dir/libhook.so"
}

# 编译所有架构
build_all() {
    local mode=$1 cmake_flag=$2
    compile_dex_header
    for abi in "${ABIS[@]}"; do
        build_abi "$abi" "$mode" "$cmake_flag"
    done
    echo "输出: $OUTPUT_DIR/$mode"
}

mkdir -p "$BUILD_ROOT" "$OUTPUT_DIR"

case "${1:-all}" in
    all)     build_all debug "-DRELEASE_MODE=OFF"; build_all release "-DRELEASE_MODE=ON" ;;
    debug)   build_all debug "-DRELEASE_MODE=OFF" ;;
    release) build_all release "-DRELEASE_MODE=ON" ;;
    clean)   rm -rf "$BUILD_ROOT" "$OUTPUT_DIR" && echo "已清理" ;;
    *)       echo "用法: $0 [all|debug|release|clean]"; exit 1 ;;
esac