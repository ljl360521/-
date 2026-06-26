@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: ==================== 配置 ====================
set ABI=arm64-v8a
set API=24
set STL=c++_static
set SRC_DIR=%~dp0
if "%SRC_DIR:~-1%"=="\" set SRC_DIR=%SRC_DIR:~0,-1%

:: ==================== 自动检测工具链 ====================
:: SDK
if not defined ANDROID_SDK_ROOT (
    set "ANDROID_SDK_ROOT=%LOCALAPPDATA%\Android\Sdk"
)
if not exist "%ANDROID_SDK_ROOT%\platforms" (
    echo [ERROR] 找不到 SDK，请设置 ANDROID_SDK_ROOT
    exit /b 1
)

:: NDK — 优先环境变量，否则取 sdk/ndk 下最新版
if defined ANDROID_NDK_ROOT (
    if exist "%ANDROID_NDK_ROOT%\build\cmake\android.toolchain.cmake" goto :ndk_ok
)
set "ANDROID_NDK_ROOT="
for /f "delims=" %%d in ('dir /b /ad /o-n "%ANDROID_SDK_ROOT%\ndk" 2^>nul') do (
    if not defined ANDROID_NDK_ROOT (
        if exist "%ANDROID_SDK_ROOT%\ndk\%%d\build\cmake\android.toolchain.cmake" (
            set "ANDROID_NDK_ROOT=%ANDROID_SDK_ROOT%\ndk\%%d"
        )
    )
)
if not defined ANDROID_NDK_ROOT (
    echo [ERROR] 找不到 NDK
    exit /b 1
)
:ndk_ok

:: 取各工具最新版本的通用宏: 在 dir 下找第一个含目标文件的子目录
:: build-tools (需要 d8.bat)
call :find_latest "%ANDROID_SDK_ROOT%\build-tools" "d8.bat" BUILD_TOOLS
if not defined BUILD_TOOLS (echo [ERROR] 找不到 build-tools & exit /b 1)

:: android.jar
set "ANDROID_JAR="
for /f "delims=" %%d in ('dir /b /ad /o-n "%ANDROID_SDK_ROOT%\platforms" 2^>nul') do (
    if not defined ANDROID_JAR (
        if exist "%ANDROID_SDK_ROOT%\platforms\%%d\android.jar" (
            set "ANDROID_JAR=%ANDROID_SDK_ROOT%\platforms\%%d\android.jar"
        )
    )
)
if not defined ANDROID_JAR (echo [ERROR] 找不到 android.jar & exit /b 1)

:: cmake — SDK 内置优先，回退系统 PATH
set "CMAKE_EXE="
call :find_latest "%ANDROID_SDK_ROOT%\cmake" "bin\cmake.exe" _CMAKE_DIR
if defined _CMAKE_DIR set "CMAKE_EXE=%_CMAKE_DIR%\bin\cmake.exe"
if not defined CMAKE_EXE (
    where cmake >nul 2>&1 && (set "CMAKE_EXE=cmake") || (
        echo [ERROR] 找不到 cmake & exit /b 1
    )
)

:: ninja
set "NINJA_EXE="
if defined _CMAKE_DIR (
    if exist "%_CMAKE_DIR%\bin\ninja.exe" set "NINJA_EXE=%_CMAKE_DIR%\bin\ninja.exe"
)
if not defined NINJA_EXE (where ninja >nul 2>&1 && set "NINJA_EXE=ninja")

set TOOLCHAIN=%ANDROID_NDK_ROOT%\build\cmake\android.toolchain.cmake
set JAVA_SRC_DIR=%SRC_DIR%\modules\native\java
set DEX_HEADER=%JAVA_SRC_DIR%\classes_dex.h

echo [OK] NDK: %ANDROID_NDK_ROOT%
echo [OK] SDK: %ANDROID_SDK_ROOT%
echo [OK] CMake: %CMAKE_EXE%

:: ==================== Java → DEX → Header ====================
echo.
echo ====== Java → DEX → Header ======

set "TMP_JAVA=%SRC_DIR%\build_java"
if exist "%TMP_JAVA%" rd /s /q "%TMP_JAVA%"
mkdir "%TMP_JAVA%\classes"

set JAVA_COUNT=0
set "JAVA_FILES="
for /r "%JAVA_SRC_DIR%" %%f in (*.java) do (
    set /a JAVA_COUNT+=1
    set "JAVA_FILES=!JAVA_FILES! "%%f""
)
if %JAVA_COUNT% equ 0 (echo   无 .java 文件，跳过 & goto :skip_java)

javac --release 11 -classpath "%ANDROID_JAR%" -d "%TMP_JAVA%\classes" %JAVA_FILES% || (echo [ERROR] javac 失败 & exit /b 1)

set "CLASS_FILES="
for /r "%TMP_JAVA%\classes" %%f in (*.class) do set "CLASS_FILES=!CLASS_FILES! "%%f""
call "%BUILD_TOOLS%\d8.bat" --min-api %API% --output "%TMP_JAVA%" %CLASS_FILES% || (echo [ERROR] d8 失败 & exit /b 1)

:: xxd 替代: PowerShell 生成 C 头文件
powershell -NoProfile -Command ^
    "$b=[IO.File]::ReadAllBytes('%TMP_JAVA%\classes.dex');" ^
    "$s=[Text.StringBuilder]::new();" ^
    "$s.AppendLine('// Auto-generated — do not edit').AppendLine('#pragma once').AppendLine('#include <cstddef>').AppendLine('')|Out-Null;" ^
    "$s.AppendLine('unsigned char classes_dex[] = {')|Out-Null;" ^
    "for($i=0;$i -lt $b.Length;$i++){" ^
    "  if($i%%12 -eq 0){$s.Append('  ')|Out-Null}" ^
    "  $s.Append(('0x{0:x2}'-f $b[$i]))|Out-Null;" ^
    "  if($i -lt $b.Length-1){$s.Append(', ')|Out-Null}" ^
    "  if(($i+1)%%12 -eq 0){$s.AppendLine('')|Out-Null}" ^
    "}" ^
    "$s.AppendLine('').AppendLine('};')|Out-Null;" ^
    "$s.AppendLine('unsigned int classes_dex_len = '+$b.Length+';')|Out-Null;" ^
    "[IO.File]::WriteAllText('%DEX_HEADER%',$s.ToString(),[Text.Encoding]::UTF8)" || (echo [ERROR] 头文件生成失败 & exit /b 1)

echo   OK: %DEX_HEADER%

:skip_java

:: ==================== 编译 native ====================
call :build_native debug "-DRELEASE_MODE=OFF" || exit /b 1
call :build_native release "-DRELEASE_MODE=ON" || exit /b 1

echo.
echo ====== 全部完成 ======
echo   debug:   %SRC_DIR%\out\debug\%ABI%\libhook.so
echo   release: %SRC_DIR%\out\release\%ABI%\libhook.so
endlocal
exit /b 0

:: ==================== 子函数 ====================

:build_native
set "MODE=%~1"
set "BUILD_DIR=%SRC_DIR%\build_%MODE%"
set "OUT_DIR=%SRC_DIR%\out\%MODE%\%ABI%"
echo.
echo ── %MODE%

set "GEN_ARGS="
if defined NINJA_EXE set "GEN_ARGS=-G "Ninja" -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%""

"%CMAKE_EXE%" -B "%BUILD_DIR%" -S "%SRC_DIR%" %GEN_ARGS% ^
    -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
    -DANDROID_ABI=%ABI% -DANDROID_PLATFORM=android-%API% -DANDROID_STL=%STL% ^
    %~2 || (echo [ERROR] %MODE% cmake 配置失败 & exit /b 1)

"%CMAKE_EXE%" --build "%BUILD_DIR%" -j %NUMBER_OF_PROCESSORS% || (echo [ERROR] %MODE% 编译失败 & exit /b 1)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
copy /y "%BUILD_DIR%\libhook.so" "%OUT_DIR%\" >nul
echo   OK: %OUT_DIR%\libhook.so
exit /b 0

:find_latest
:: %1=搜索目录  %2=目标文件  %3=输出变量名
set "%~3="
for /f "delims=" %%d in ('dir /b /ad /o-n "%~1" 2^>nul') do (
    if not defined %~3 (
        if exist "%~1\%%d\%~2" set "%~3=%~1\%%d"
    )
)
exit /b 0